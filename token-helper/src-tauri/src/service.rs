//! Everything the helper does over the network, for the window and the
//! terminal alike: getting an Atlas token through the EA app, finding and
//! signing in the game, and serving new tokens. Errors are worded for the
//! player. Neither the EA code, a token nor the key is ever put in a message.
//!
//! The game side is launcher/src/runtime_signin.inl (sign-in listener, port
//! 37012) and RefreshAtlasToken in runtime_server_join.inl (asks /atlas/token).

use crate::lsx;
use serde::Serialize;
use serde_json::{json, Value};
use std::fs;
use std::net::{IpAddr, UdpSocket};
use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

#[derive(Clone, Debug, PartialEq)]
pub struct Identity {
    pub uid: String,
    pub token: String,
}

#[derive(Clone, Serialize)]
pub struct Event {
    pub seq: u64,
    pub text: String,
}

#[derive(Clone)]
pub struct Settings {
    pub lsx_port: u16,
    pub content_id: String,
    pub title: String,
    pub client_id: String,
    pub scope: String,
    pub master_server: String,
    pub user_agent: String,
    pub port: u16,
    pub console_port: u16,
    pub min_seconds_between_tokens: u64,
    pub output: PathBuf,
    pub advertise_host: String,
}

struct Serving {
    server: Arc<tiny_http::Server>,
    all_interfaces: bool,
}

pub struct Service {
    pub settings: Settings,
    pub key: String,
    write_identity_on_refresh: AtomicBool,
    last: Mutex<Option<(Identity, Instant)>>,
    events: Mutex<(u64, Vec<Event>)>,
    on_note: Mutex<Option<Box<dyn Fn(&str) + Send>>>,
    serving: Mutex<Option<Serving>>,
}

pub fn is_hex32(text: &str) -> bool {
    text.len() == 32 && text.bytes().all(|b| b.is_ascii_digit() || (b'a'..=b'f').contains(&b))
}

pub fn is_address(address: &str) -> bool {
    !address.is_empty()
        && address.len() <= 253
        && address.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'.' || b == b'-')
}

pub fn is_loopback(address: &str) -> bool {
    address == "localhost" || address.starts_with("127.")
}

/// The body for any status; None when nothing answered.
fn http(request: ureq::Request, body: Option<&str>) -> Option<(u16, String)> {
    let result = match body {
        Some(body) => request.set("Content-Type", "application/json").send_string(body),
        None => request.call(),
    };
    match result {
        Ok(response) => {
            let status = response.status();
            Some((status, response.into_string().unwrap_or_default()))
        }
        Err(ureq::Error::Status(status, response)) => Some((status, response.into_string().unwrap_or_default())),
        Err(_) => None,
    }
}

fn agent(timeout: Duration) -> ureq::Agent {
    ureq::AgentBuilder::new().timeout(timeout).build()
}

impl Service {
    pub fn new(settings: Settings, key: String) -> Arc<Service> {
        Arc::new(Service {
            settings,
            key,
            write_identity_on_refresh: AtomicBool::new(false),
            last: Mutex::new(None),
            events: Mutex::new((0, Vec::new())),
            on_note: Mutex::new(None),
            serving: Mutex::new(None),
        })
    }

    /// Prints or shows each activity line as it happens.
    pub fn set_on_note(&self, callback: Box<dyn Fn(&str) + Send>) {
        *self.on_note.lock().unwrap() = Some(callback);
    }

    pub fn note(&self, text: &str) {
        let line = format!("{}  {text}", chrono::Local::now().format("%H:%M:%S"));
        {
            let mut events = self.events.lock().unwrap();
            events.0 += 1;
            let seq = events.0;
            events.1.push(Event { seq, text: line.clone() });
            let excess = events.1.len().saturating_sub(300);
            events.1.drain(..excess);
        }
        if let Some(callback) = self.on_note.lock().unwrap().as_ref() {
            callback(&line);
        }
    }

    pub fn events_since(&self, seq: u64) -> Vec<Event> {
        self.events.lock().unwrap().1.iter().filter(|e| e.seq > seq).cloned().collect()
    }

    /// Exchanges an EA code for an Atlas token. A token minted in the last
    /// min_seconds_between_tokens is reused, so a burst of requests mints one.
    pub fn mint(&self) -> Result<Identity, String> {
        let mut last = self.last.lock().unwrap();
        if let Some((identity, minted)) = last.as_ref() {
            if minted.elapsed() < Duration::from_secs(self.settings.min_seconds_between_tokens) {
                return Ok(identity.clone());
            }
        }
        let s = &self.settings;
        let (uid, code) = lsx::get_auth_code(s.lsx_port, &s.content_id, &s.title, &s.client_id, &s.scope)?;
        let url = format!(
            "{}/client/origin_auth?id={}&token={}",
            s.master_server.trim_end_matches('/'),
            encode(&uid),
            encode(&code)
        );
        let request = agent(Duration::from_secs(20)).get(&url).set("User-Agent", &s.user_agent);
        let (status, body) = http(request, None).ok_or_else(|| {
            format!("Could not reach Northstar ({}). Check the internet connection and try again.", s.master_server)
        })?;
        let reply: Value = serde_json::from_str(&body).unwrap_or(Value::Null);
        let token = reply["token"].as_str().unwrap_or_default();
        if status != 200 || reply["success"] != Value::Bool(true) || !is_hex32(token) {
            return Err(match reply["error"]["msg"].as_str() {
                Some(message) if !message.is_empty() => format!("Northstar refused the EA sign-in: {message}."),
                _ => format!("Northstar refused the EA sign-in (status {status})."),
            });
        }
        let identity = Identity { uid, token: token.to_string() };
        *last = Some((identity.clone(), Instant::now()));
        Ok(identity)
    }

    fn console_url(&self, address: &str, path: &str) -> String {
        format!("http://{address}:{}{path}", self.settings.console_port)
    }

    /// Whether Northstar is running at that address and listening for a sign-in.
    pub fn hello(&self, address: &str) -> bool {
        if !is_address(address) {
            return false;
        }
        let request = agent(Duration::from_secs(3)).get(&self.console_url(address, "/northstar/hello"));
        match http(request, None) {
            Some((200, body)) => serde_json::from_str::<Value>(&body).map(|v| v["app"] == "NorthstarPS4").unwrap_or(false),
            _ => false,
        }
    }

    /// This computer's address as the console sees it: the local end of a route to it.
    pub fn address_towards(&self, address: &str) -> String {
        if !self.settings.advertise_host.is_empty() {
            return self.settings.advertise_host.clone();
        }
        if is_loopback(address) {
            return "127.0.0.1".into();
        }
        let local = UdpSocket::bind("0.0.0.0:0")
            .and_then(|socket| socket.connect((address, self.settings.console_port)).map(|_| socket))
            .and_then(|socket| socket.local_addr());
        match local {
            Ok(local) if !local.ip().is_unspecified() => match local.ip() {
                IpAddr::V4(ip) => ip.to_string(),
                IpAddr::V6(ip) => ip.to_string(),
            },
            _ => "127.0.0.1".into(),
        }
    }

    /// Where shadPS4 on this computer asks for new tokens.
    pub fn local_refresh_url(&self) -> String {
        format!("http://127.0.0.1:{}/atlas/token", self.settings.port)
    }

    /// Hands an identity to the game. The error says why not, in lower case,
    /// as the game words it.
    pub fn sign_in(&self, address: &str, code: &str, identity: &Identity) -> Result<(), String> {
        if !is_address(address) {
            return Err("that is not an address".into());
        }
        let digits: String = code.chars().filter(char::is_ascii_digit).collect();
        let body = json!({
            "uid": identity.uid,
            "playerToken": identity.token,
            "refreshUrl": format!("http://{}:{}/atlas/token", self.address_towards(address), self.settings.port),
            "refreshKey": self.key,
            "code": digits,
        })
        .to_string();
        let request = agent(Duration::from_secs(10)).post(&self.console_url(address, "/northstar/signin"));
        let (status, reply) = http(request, Some(&body)).ok_or_else(|| {
            format!("Northstar could not be reached at {address}. Check the address, and that the game is running")
        })?;
        let reply: Value = serde_json::from_str(&reply).unwrap_or(Value::Null);
        if status == 200 && reply["ok"] == Value::Bool(true) {
            return Ok(());
        }
        Err(match reply["error"].as_str() {
            Some(error) if !error.is_empty() => error.to_string(),
            _ => format!("the game did not accept the sign-in (status {status})"),
        })
    }

    /// Writes atlas_identity.json for shadPS4 on this computer, in one step.
    pub fn write_identity(&self, identity: &Identity, refresh_url: &str) -> Result<(), String> {
        #[derive(Serialize)]
        #[serde(rename_all = "camelCase")]
        struct File<'a> {
            uid: &'a str,
            player_token: &'a str,
            refresh_url: &'a str,
            refresh_key: &'a str,
        }
        let text = serde_json::to_string_pretty(&File {
            uid: &identity.uid,
            player_token: &identity.token,
            refresh_url,
            refresh_key: &self.key,
        })
        .unwrap();
        let output = &self.settings.output;
        let write = || -> std::io::Result<()> {
            if let Some(parent) = output.parent() {
                fs::create_dir_all(parent)?;
            }
            let mut temp = output.clone().into_os_string();
            temp.push(".tmp");
            fs::write(&temp, text + "\n")?;
            fs::rename(&temp, output)
        };
        write().map_err(|error| error.to_string())
    }

    /// Keeps atlas_identity.json current as tokens are served, for shadPS4 on this computer.
    pub fn set_write_identity_on_refresh(&self, on: bool) {
        self.write_identity_on_refresh.store(on, Ordering::SeqCst);
    }

    /// Serves new tokens at /atlas/token: on every interface for a console
    /// elsewhere, on loopback for shadPS4 here.
    pub fn start_serving(self: &Arc<Self>, all_interfaces: bool) -> Result<(), String> {
        let mut serving = self.serving.lock().unwrap();
        if let Some(current) = serving.as_ref() {
            if current.all_interfaces == all_interfaces {
                return Ok(());
            }
        }
        if let Some(old) = serving.take() {
            old.server.unblock();
        }
        let host = if all_interfaces { "0.0.0.0" } else { "127.0.0.1" };
        let server = tiny_http::Server::http((host, self.settings.port)).map_err(|_| {
            format!(
                "port {} is in use, so the game cannot ask for new tokens. Is the token helper already running?",
                self.settings.port
            )
        })?;
        let server = Arc::new(server);
        let service = Arc::clone(self);
        let listener = Arc::clone(&server);
        thread::spawn(move || {
            for request in listener.incoming_requests() {
                service.serve_token(request);
            }
        });
        *serving = Some(Serving { server, all_interfaces });
        Ok(())
    }

    pub fn stop(&self) {
        if let Some(old) = self.serving.lock().unwrap().take() {
            old.server.unblock();
        }
    }

    fn serve_token(&self, request: tiny_http::Request) {
        let reply = |request: tiny_http::Request, status: u16, body: Value| {
            let header = tiny_http::Header::from_bytes("Content-Type", "application/json").unwrap();
            let _ = request.respond(tiny_http::Response::from_string(body.to_string()).with_status_code(status).with_header(header));
        };
        let peer = request.remote_addr().map(|a| a.ip().to_string()).unwrap_or_else(|| "?".into());
        let path = request.url().split('?').next().unwrap_or_default().to_string();
        if *request.method() != tiny_http::Method::Get || path != "/atlas/token" {
            return reply(request, 404, json!({"error": "not found"}));
        }
        let key = request
            .headers()
            .iter()
            .find(|h| h.field.equiv("X-NorthstarPS4-Key"))
            .map(|h| h.value.as_str().to_string())
            .unwrap_or_default();
        if self.key.is_empty() || !constant_time_eq(key.as_bytes(), self.key.as_bytes()) {
            self.note(&format!("refused a request from {peer} (not paired with this helper)"));
            return reply(request, 403, json!({"error": "this console is not paired with the token helper"}));
        }
        match self.mint() {
            Ok(identity) => {
                if self.write_identity_on_refresh.load(Ordering::SeqCst) {
                    if let Err(error) = self.write_identity(&identity, &self.local_refresh_url()) {
                        self.note(&format!("could not update {}: {error}", self.settings.output.display()));
                    }
                }
                self.note(&format!("gave {peer} a new token for account {}", identity.uid));
                reply(request, 200, json!({"uid": identity.uid, "playerToken": identity.token}));
            }
            Err(error) => {
                self.note(&format!("could not get a token for {peer}: {error}"));
                reply(request, 502, json!({"error": error}));
            }
        }
    }
}

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |diff, (x, y)| diff | (x ^ y)) == 0
}

/// Percent-encoding for a query value.
fn encode(text: &str) -> String {
    text.bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~' => (b as char).to_string(),
            _ => format!("%{b:02X}"),
        })
        .collect()
}
