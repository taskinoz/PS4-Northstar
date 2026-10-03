//! End-to-end tests with a fake EA app, Atlas and console, all on loopback.

use crate::gui::Ui;
use crate::lsx::{decrypt_hex, encrypt_hex, find_element, key};
use crate::options::Options;
use crate::service::{is_hex32, Service};
use serde_json::{json, Value};
use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

const FAKE_UID: &str = "1012345678901";
const FAKE_CODE: &str = "QUOxFAKEcodeForTests";
const CONSOLE_CODE: &str = "4821";

/// The EA app's SDK server: handshake, GetProfile, GetAuthCode, with an
/// unrelated event before every reply.
fn fake_lsx() -> u16 {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let port = listener.local_addr().unwrap().port();
    thread::spawn(move || {
        for stream in listener.incoming().flatten() {
            thread::spawn(move || serve_fake_lsx(stream));
        }
    });
    port
}

fn serve_fake_lsx(stream: TcpStream) {
    let mut writer = stream.try_clone().unwrap();
    let mut reader = BufReader::new(stream);
    let mut send = |text: &str| {
        let _ = writer.write_all(format!("{text}\0").as_bytes());
    };
    let mut read = || {
        let mut message = Vec::new();
        match reader.read_until(0, &mut message) {
            Ok(n) if n > 0 => {
                message.pop();
                Some(String::from_utf8_lossy(&message).into_owned())
            }
            _ => None,
        }
    };
    let challenge = "00112233445566778899aabbccddeeff";
    send(&format!(r#"<LSX><Event sender="EALS"><Challenge key="{challenge}" version="3" build="fake"/></Event></LSX>"#));
    let Some(message) = read() else { return };
    let expected = encrypt_hex(&key(0), challenge);
    let accepted = find_element(&message, &["ChallengeResponse"]).is_some_and(|a| a["response"] == expected)
        && message.contains("<ContentId>1039093</ContentId>");
    if !accepted {
        send(r#"<LSX><Response id="0" sender="EALS"><ErrorSuccess Code="-1" Description="bad challenge"/></Response></LSX>"#);
        return;
    }
    let bytes = expected.as_bytes();
    let session = key((bytes[0] as u32) << 8 | bytes[1] as u32);
    send(&format!(r#"<LSX><Response id="0" sender="EALS"><ChallengeAccepted response="{expected}"/></Response></LSX>"#));
    while let Some(raw) = read() {
        let Some(plain) = decrypt_hex(&session, &raw) else { return };
        let id = find_element(&plain, &["Request"]).map(|a| a["id"].clone()).unwrap_or_default();
        let out = if find_element(&plain, &["GetProfile"]).is_some() {
            format!(r#"<GetProfileResponse UserIndex="0" UserId="{FAKE_UID}"/>"#)
        } else if let Some(code) = find_element(&plain, &["GetAuthCode"]) {
            if code["UserId"] == FAKE_UID && code["ClientId"] == "TITANFALL2-PC-SERVER" {
                format!(r#"<AuthCode value="{FAKE_CODE}"/>"#)
            } else {
                r#"<ErrorSuccess Code="-2" Description="bad client"/>"#.into()
            }
        } else {
            r#"<ErrorSuccess Code="-3" Description="unsupported"/>"#.into()
        };
        send(&encrypt_hex(&session, r#"<LSX><Event sender="EbisuSDK"><Login IsLoggedIn="true"/></Event></LSX>"#));
        send(&encrypt_hex(&session, &format!(r#"<LSX><Response id="{id}" sender="EbisuSDK">{out}</Response></LSX>"#)));
    }
}

fn respond(request: tiny_http::Request, status: u16, body: Value) {
    let header = tiny_http::Header::from_bytes("Content-Type", "application/json").unwrap();
    let _ = request.respond(tiny_http::Response::from_string(body.to_string()).with_status_code(status).with_header(header));
}

/// Atlas: mints a token for the fake EA code. Returns its URL and the tokens minted.
fn fake_atlas() -> (String, Arc<Mutex<Vec<String>>>) {
    let server = tiny_http::Server::http("127.0.0.1:0").unwrap();
    let url = format!("http://{}", server.server_addr().to_ip().unwrap());
    let tokens = Arc::new(Mutex::new(Vec::new()));
    let minted = Arc::clone(&tokens);
    thread::spawn(move || {
        for request in server.incoming_requests() {
            let agent = request.headers().iter().find(|h| h.field.equiv("User-Agent")).map(|h| h.value.to_string());
            let good = request.url() == format!("/client/origin_auth?id={FAKE_UID}&token={FAKE_CODE}")
                && agent.is_some_and(|a| a.starts_with("R2Northstar/"));
            if !good {
                respond(request, 403, json!({"success": false, "error": {"msg": "bad code"}}));
                continue;
            }
            let mut bytes = [0u8; 16];
            getrandom::getrandom(&mut bytes).unwrap();
            let token = hex::encode(bytes);
            minted.lock().unwrap().push(token.clone());
            respond(request, 200, json!({"success": true, "token": token}));
        }
    });
    (url, tokens)
}

/// The game's sign-in listener, as a console elsewhere: the code, or the key
/// of the helper it accepted last. Returns its port and the pushes it got.
fn fake_console() -> (u16, Arc<Mutex<Vec<Value>>>) {
    let server = tiny_http::Server::http("127.0.0.1:0").unwrap();
    let port = server.server_addr().to_ip().unwrap().port();
    let pushes = Arc::new(Mutex::new(Vec::<Value>::new()));
    let seen = Arc::clone(&pushes);
    thread::spawn(move || {
        let mut paired = String::new();
        for mut request in server.incoming_requests() {
            match request.url() {
                "/northstar/hello" => respond(request, 200, json!({"app": "NorthstarPS4", "signedIn": false})),
                "/northstar/signin" => {
                    let mut body = String::new();
                    request.as_reader().read_to_string(&mut body).unwrap();
                    let push: Value = serde_json::from_str(&body).unwrap_or(Value::Null);
                    seen.lock().unwrap().push(push.clone());
                    let by_key = !paired.is_empty() && push["refreshKey"] == paired.as_str();
                    if push["code"] != CONSOLE_CODE && !by_key {
                        respond(request, 403, json!({"error": "wrong code; type the code shown on the PS4"}));
                    } else {
                        paired = push["refreshKey"].as_str().unwrap_or_default().to_string();
                        respond(request, 200, json!({"ok": true}));
                    }
                }
                _ => respond(request, 404, json!({"error": "not found"})),
            }
        }
    });
    (port, pushes)
}

fn free_port() -> u16 {
    TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port()
}

fn temp_dir(name: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!("ns-helper-{name}-{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&dir);
    dir
}

fn test_options(name: &str, atlas: &str, lsx_port: u16, console_port: u16) -> Options {
    let dir = temp_dir(name);
    let key_file = dir.join("token-helper.json").display().to_string();
    let output = dir.join("atlas_identity.json").display().to_string();
    let args = [
        "--lsx-port", &lsx_port.to_string(), "--master-server", atlas,
        "--key-file", &key_file,
        "--output", &output,
        "--port", &free_port().to_string(), "--console-port", &console_port.to_string(),
        "--min-seconds-between-tokens", "0",
    ];
    Options::parse(args.iter().map(|s| s.to_string())).unwrap()
}

#[test]
fn mints_signs_in_and_serves() {
    let (atlas, tokens) = fake_atlas();
    let (console_port, pushes) = fake_console();
    let o = test_options("serve", &atlas, fake_lsx(), console_port);
    let state = crate::options::HelperState::load(&o.key_file).unwrap();
    let service = Service::new(o.settings(), state.key.clone());

    let identity = service.mint().unwrap();
    assert_eq!(identity.uid, FAKE_UID);
    assert_eq!(&identity.token, tokens.lock().unwrap().last().unwrap());

    assert!(service.hello("127.0.0.1"));
    let wrong = service.sign_in("127.0.0.1", "1111", &identity).unwrap_err();
    assert!(wrong.contains("wrong code"), "{wrong}");
    service.sign_in("127.0.0.1", "48-21", &identity).unwrap();
    let push = pushes.lock().unwrap().last().unwrap().clone();
    assert_eq!(push["playerToken"], identity.token.as_str());
    assert_eq!(push["uid"], FAKE_UID);
    assert_eq!(push["refreshKey"], state.key.as_str());
    assert_eq!(push["refreshUrl"], service.local_refresh_url().as_str());
    service.sign_in("127.0.0.1", "", &identity).unwrap(); // paired: no code

    service.start_serving(false).unwrap();
    service.set_write_identity_on_refresh(true);
    let get = |path: &str, key: &str| -> (u16, Value) {
        let mut request = ureq::get(&format!("http://127.0.0.1:{}{path}", o.port)).timeout(Duration::from_secs(10));
        if !key.is_empty() {
            request = request.set("X-NorthstarPS4-Key", key);
        }
        match request.call() {
            Ok(r) => (r.status(), serde_json::from_str(&r.into_string().unwrap()).unwrap_or(Value::Null)),
            Err(ureq::Error::Status(code, r)) => (code, serde_json::from_str(&r.into_string().unwrap()).unwrap_or(Value::Null)),
            Err(error) => panic!("{error}"),
        }
    };
    let (status, body) = get("/atlas/token", &state.key);
    assert_eq!(status, 200);
    assert_eq!(body["uid"], FAKE_UID);
    assert_eq!(&body["playerToken"], tokens.lock().unwrap().last().unwrap().as_str());
    let file = std::fs::read_to_string(&o.output).unwrap();
    assert!(file.contains(tokens.lock().unwrap().last().unwrap()) && file.contains(&service.local_refresh_url()));
    assert_eq!(get("/atlas/token", &"0".repeat(32)).0, 403);
    assert_eq!(get("/other", &state.key).0, 404);
    for event in service.events_since(0) {
        assert!(!event.text.contains(&identity.token) && !event.text.contains(&state.key), "secret in {}", event.text);
    }
    service.stop();
}

#[test]
fn reports_an_unreachable_ea_app() {
    let (atlas, _) = fake_atlas();
    let o = test_options("ea-down", &atlas, free_port(), free_port());
    let service = Service::new(o.settings(), "0".repeat(32));
    let error = service.mint().unwrap_err();
    assert!(error.contains("Could not reach the EA app"), "{error}");
}

#[test]
fn window_flow_signs_in_a_console() {
    let (atlas, tokens) = fake_atlas();
    let o = test_options("ui", &atlas, fake_lsx(), free_port());
    let ui = Ui::new(&o).unwrap();
    ui.start();
    let view = ui.reply(0);
    let view = serde_json::to_value(&view).unwrap();
    assert_eq!(view["ready"], true);
    assert_eq!(view["ea"]["kind"], "ok");
    assert!(view["result"]["text"].as_str().unwrap().contains("Choose where"), "{view}");

    let (console_port, pushes) = fake_console();
    let o2 = Options { console_port, ..o.clone() };
    // The same flow against the console, with the port it listens on.
    let ui = Ui::new(&o2).unwrap();
    ui.start();
    let result = |ui: &Arc<Ui>| serde_json::to_value(ui.reply(0)).unwrap()["result"].clone();
    // 127.0.0.1 answers hello here, so start() signs it in as this computer's game first; the
    // fake plays a remote console, which refuses that push without a code.
    ui.sign_in("remote", "127.0.0.1", "12");
    assert!(result(&ui)["text"].as_str().unwrap().contains("4-digit"));
    ui.sign_in("remote", "127.0.0.1", "1111");
    assert_eq!(result(&ui)["text"], "Wrong code; type the code shown on the PS4.");
    ui.sign_in("remote", "127.0.0.1", CONSOLE_CODE);
    let r = result(&ui);
    assert_eq!(r["kind"], "ok", "{r}");
    assert!(r["text"].as_str().unwrap().starts_with("Signed in Northstar on 127.0.0.1"));
    assert_eq!(&pushes.lock().unwrap().last().unwrap()["playerToken"], tokens.lock().unwrap().last().unwrap().as_str());
    let saved = std::fs::read_to_string(&o2.key_file).unwrap();
    assert!(saved.contains("\"console\": \"127.0.0.1\""), "{saved}");
    let events = ui.service.events_since(0);
    assert!(events.last().unwrap().text.ends_with("signed in Northstar on 127.0.0.1"));
    ui.service.stop();
}

#[test]
fn hex32_and_addresses() {
    assert!(is_hex32("0123456789abcdef0123456789abcdef"));
    assert!(!is_hex32("0123456789ABCDEF0123456789ABCDEF"));
    assert!(crate::service::is_address("192.168.1.20") && !crate::service::is_address("a/b") && !crate::service::is_address(""));
}
