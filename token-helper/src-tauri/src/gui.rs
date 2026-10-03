//! The helper's window. The page (ui/) polls `get_state` and calls `sign_in`
//! and `retry`; the work runs on background threads here, one step at a time,
//! so the window never stops responding. The page talks to this process over
//! Tauri's IPC only: nothing else on the computer or the network can drive it.

use crate::options::{HelperState, Options, VERSION};
use crate::service::{is_address, Event, Identity, Service};
use serde::Serialize;
use std::sync::{Arc, Mutex};
use std::thread;

#[derive(Clone, Default, Serialize)]
pub struct Message {
    pub text: String,
    pub kind: String, // "", "ok" or "error"
}

#[derive(Clone, Default, Serialize)]
pub struct View {
    pub ea: Message,
    pub retry: bool,
    pub ready: bool,
    pub busy: bool,
    pub mode: String, // "local" or "remote"
    pub address: String,
    pub form: u64, // bumped when the helper itself changes mode or address
    pub result: Message,
}

#[derive(Serialize)]
pub struct StateReply {
    #[serde(flatten)]
    view: View,
    events: Vec<Event>,
    version: &'static str,
}

pub struct Ui {
    pub service: Arc<Service>,
    state: Mutex<HelperState>,
    view: Mutex<View>,
    identity: Mutex<Option<Identity>>,
    output: String,
}

fn message(text: impl Into<String>, kind: &str) -> Message {
    Message { text: text.into(), kind: kind.into() }
}

fn sentence(text: &str) -> String {
    let mut chars = text.chars();
    let mut out: String = match chars.next() {
        Some(first) => first.to_uppercase().chain(chars).collect(),
        None => return String::new(),
    };
    if !out.ends_with('.') {
        out.push('.');
    }
    out
}

impl Ui {
    pub fn new(o: &Options) -> Result<Arc<Ui>, String> {
        let state = HelperState::load(&o.key_file)?;
        let service = Service::new(o.settings(), state.key.clone());
        let view = View {
            mode: if state.console.is_empty() { "local".into() } else { "remote".into() },
            address: state.console.clone(),
            ..View::default()
        };
        Ok(Arc::new(Ui {
            service,
            state: Mutex::new(state),
            view: Mutex::new(view),
            identity: Mutex::new(None),
            output: o.output.display().to_string(),
        }))
    }

    fn update(&self, change: impl FnOnce(&mut View)) {
        change(&mut self.view.lock().unwrap());
    }

    fn result(&self, text: impl Into<String>, kind: &str) {
        let text = text.into();
        self.update(|v| v.result = message(text, kind));
    }

    /// Marks the window busy; false if it already was.
    fn begin(&self) -> bool {
        let mut view = self.view.lock().unwrap();
        if view.busy {
            return false;
        }
        view.busy = true;
        true
    }

    fn end(&self) {
        self.update(|v| v.busy = false);
    }

    fn remembered(&self) -> String {
        self.state.lock().unwrap().console.clone()
    }

    pub fn reply(&self, since: u64) -> StateReply {
        StateReply { view: self.view.lock().unwrap().clone(), events: self.service.events_since(since), version: VERSION }
    }

    /// Gets a token, then signs in a game running on this computer or the
    /// console paired last time.
    pub fn start(self: &Arc<Self>) {
        if !self.begin() {
            return;
        }
        self.update(|v| {
            v.ea = message("Getting a token from the EA app...", "");
            v.retry = false;
        });
        let identity = match self.service.mint() {
            Ok(identity) => identity,
            Err(error) => {
                self.update(|v| {
                    v.ea = message(error, "error");
                    v.retry = true;
                    v.ready = false;
                    v.result = Message::default();
                    v.busy = false;
                });
                return;
            }
        };
        let account = identity.uid.clone();
        *self.identity.lock().unwrap() = Some(identity);
        self.update(|v| {
            v.ea = message(format!("Signed in to EA as account {account}."), "ok");
            v.ready = true;
            v.result = message("Looking for Northstar...", "");
        });
        let console = self.remembered();
        let here = self.service.hello("127.0.0.1");
        let paired = !here && !console.is_empty() && self.service.hello(&console);
        self.end();
        if here {
            self.update(|v| {
                v.mode = "local".into();
                v.form += 1;
            });
            self.sign_in("local", "", "");
        } else if paired {
            self.update(|v| {
                v.mode = "remote".into();
                v.address = console.clone();
                v.form += 1;
            });
            self.sign_in("remote", &console, "");
        } else if !console.is_empty() {
            self.result(format!("Northstar is not running on {console}. Start it, then select Sign in."), "");
        } else {
            self.result("Choose where Northstar is running, then select Sign in.", "");
        }
    }

    pub fn sign_in(self: &Arc<Self>, mode: &str, address: &str, code: &str) {
        let remote = mode == "remote";
        let target = if remote { address.trim().to_string() } else { "127.0.0.1".to_string() };
        let code = code.trim();
        if remote {
            if !is_address(&target) {
                return self.result("Type the address the game shows, for example 192.168.1.20.", "error");
            }
            let four_digits = code.len() == 4 && code.bytes().all(|b| b.is_ascii_digit());
            if !four_digits && !(code.is_empty() && target == self.remembered()) {
                return self.result("Type the 4-digit code the game shows.", "error");
            }
        }
        if !self.begin() {
            return;
        }
        self.result("Signing in...", "");
        let outcome = self.sign_in_steps(remote, &target, code);
        match outcome {
            Ok(text) => self.result(text, "ok"),
            Err(text) => self.result(text, "error"),
        }
        self.end();
    }

    fn sign_in_steps(self: &Arc<Self>, remote: bool, target: &str, code: &str) -> Result<String, String> {
        // A token from the last minute is reused; an older one is replaced.
        let identity = self.service.mint()?;
        *self.identity.lock().unwrap() = Some(identity.clone());
        let mut saved = false;
        if !remote {
            if self.service.hello(target) {
                self.service.sign_in(target, "", &identity).map_err(|e| sentence(&e))?;
            } else {
                self.service
                    .write_identity(&identity, &self.service.local_refresh_url())
                    .map_err(|e| format!("Could not save the sign-in to {}: {e}", self.output))?;
                saved = true;
            }
        } else {
            self.service.sign_in(target, code, &identity).map_err(|e| sentence(&e))?;
        }

        self.service.set_write_identity_on_refresh(!remote);
        let serving = self.service.start_serving(remote);
        let text = if remote {
            {
                let mut state = self.state.lock().unwrap();
                state.console = target.to_string();
                let _ = state.save();
            }
            self.service.note(&format!("signed in Northstar on {target}"));
            format!("Signed in Northstar on {target}. Select Launch Northstar in the game.")
        } else if saved {
            self.service.note("saved the sign-in for shadPS4 on this computer");
            "Saved. Northstar in shadPS4 on this computer picks up the sign-in when it starts.".to_string()
        } else {
            self.service.note("signed in Northstar in shadPS4 on this computer");
            "Signed in Northstar in shadPS4 on this computer. Select Launch Northstar in the game.".to_string()
        };
        match serving {
            Ok(()) => Ok(text),
            Err(error) => Err(format!("{text} But {error}")),
        }
    }
}

#[tauri::command]
fn get_state(ui: tauri::State<Arc<Ui>>, since: u64) -> StateReply {
    ui.reply(since)
}

#[tauri::command]
fn sign_in(ui: tauri::State<Arc<Ui>>, mode: String, address: String, code: String) -> bool {
    let ready = ui.view.lock().unwrap().ready;
    if ready {
        let ui = Arc::clone(&ui);
        thread::spawn(move || ui.sign_in(&mode, &address, &code));
    }
    ready
}

#[tauri::command]
fn retry(ui: tauri::State<Arc<Ui>>) {
    let ui = Arc::clone(&ui);
    thread::spawn(move || ui.start());
}

pub fn run(o: &Options) -> i32 {
    let ui = match Ui::new(o) {
        Ok(ui) => ui,
        Err(error) => {
            eprintln!("{error}");
            return 1;
        }
    };
    let starter = Arc::clone(&ui);
    thread::spawn(move || starter.start());
    let result = tauri::Builder::default()
        .manage(Arc::clone(&ui))
        .invoke_handler(tauri::generate_handler![get_state, sign_in, retry])
        .run(tauri::generate_context!());
    ui.service.stop();
    match result {
        Ok(()) => 0,
        Err(error) => {
            eprintln!("The window could not open: {error}");
            1
        }
    }
}
