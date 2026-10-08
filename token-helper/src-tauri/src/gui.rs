//! The helper's window. The page (ui/) polls `get_state` and calls `sign_in`
//! and `retry`; the work runs on background threads here, one step at a time,
//! so the window never stops responding. The page talks to this process over
//! Tauri's IPC only: nothing else on the computer or the network can drive it.

use crate::install::{self, FolderInfo, Sources};
use crate::options::{HelperState, Options, VERSION};
use crate::service::{is_address, ConsoleStatus, Event, Identity, Service};
use serde::Serialize;
use std::path::PathBuf;
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
    // The EA account ID, which the page shows only on request so it is not
    // shared by accident in a screenshot.
    pub account: String,
    // The console this helper is paired with: its key, not a code, signs it in.
    pub paired: String,
}

/// The Install tab.
#[derive(Clone, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct InstallView {
    pub folder: String,
    pub info: Option<FolderInfo>,
    pub status: Message,
    pub latest: String, // the newest release's version, once known
    pub action: String, // "", "Install", "Update to <version>" or "Reinstall"
    pub can_uninstall: bool,
    pub busy: bool,
    pub result: Message,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StateReply {
    #[serde(flatten)]
    view: View,
    events: Vec<Event>,
    install: InstallView,
    install_events: Vec<Event>,
    version: &'static str,
}

pub struct Ui {
    pub service: Arc<Service>,
    state: Mutex<HelperState>,
    view: Mutex<View>,
    identity: Mutex<Option<Identity>>,
    output: String,
    sources: Sources,
    install: Mutex<InstallView>,
    install_log: Mutex<(u64, Vec<Event>)>,
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
        let install = InstallView { folder: state.game_folder.clone(), ..InstallView::default() };
        Ok(Arc::new(Ui {
            service,
            state: Mutex::new(state),
            view: Mutex::new(view),
            identity: Mutex::new(None),
            output: o.output.display().to_string(),
            sources: o.sources.clone(),
            install: Mutex::new(install),
            install_log: Mutex::new((0, Vec::new())),
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

    pub fn reply(&self, since: u64, install_since: u64) -> StateReply {
        let install_events =
            self.install_log.lock().unwrap().1.iter().filter(|e| e.seq > install_since).cloned().collect();
        let mut view = self.view.lock().unwrap().clone();
        view.paired = self.remembered();
        StateReply {
            view,
            events: self.service.events_since(since),
            install: self.install.lock().unwrap().clone(),
            install_events,
            version: VERSION,
        }
    }

    // ---- The Install tab ----

    fn install_note(&self, text: &str) {
        let mut log = self.install_log.lock().unwrap();
        log.0 += 1;
        let seq = log.0;
        log.1.push(Event { seq, text: format!("{}  {text}", chrono::Local::now().format("%H:%M:%S")) });
        let excess = log.1.len().saturating_sub(300);
        log.1.drain(..excess);
    }

    fn begin_install(&self) -> bool {
        let mut view = self.install.lock().unwrap();
        if view.busy {
            return false;
        }
        view.busy = true;
        true
    }

    fn update_install(&self, change: impl FnOnce(&mut InstallView)) {
        change(&mut self.install.lock().unwrap());
    }

    /// Looks at the chosen folder and, when `check_release`, at the newest release.
    pub fn refresh_install(self: &Arc<Self>, check_release: bool) {
        let folder = self.install.lock().unwrap().folder.clone();
        if folder.is_empty() {
            self.update_install(|v| {
                v.info = None;
                v.status = message("Choose the Titanfall 2 game folder: the one with eboot.bin and vpk_ps4.", "");
                v.action.clear();
                v.can_uninstall = false;
            });
            if !check_release {
                return;
            }
        }
        let latest = if check_release {
            match install::latest_release(&self.sources) {
                Ok(release) => release.version,
                Err(error) => {
                    self.update_install(|v| v.result = message(error, "error"));
                    self.install.lock().unwrap().latest.clone()
                }
            }
        } else {
            self.install.lock().unwrap().latest.clone()
        };
        if folder.is_empty() {
            self.update_install(|v| v.latest = latest);
            return;
        }
        let info = install::inspect(&PathBuf::from(&folder), &self.sources);
        let installed = info.runtime_installed || info.eboot == install::Eboot::Bootstrapped;
        let action = if !info.installable() {
            String::new()
        } else if !installed {
            "Install".into()
        } else if !latest.is_empty() && info.installed_version.as_deref() != Some(latest.as_str()) {
            format!("Update to {latest}")
        } else {
            "Reinstall".into()
        };
        let kind = if !info.installable() {
            "error"
        } else if installed {
            "ok"
        } else {
            ""
        };
        self.update_install(|v| {
            v.status = message(info.describe(), kind);
            v.can_uninstall = installed && info.eboot != install::Eboot::Unknown;
            v.info = Some(info);
            v.latest = latest;
            v.action = action;
        });
    }

    pub fn set_game_folder(self: &Arc<Self>, folder: String) {
        {
            let mut state = self.state.lock().unwrap();
            state.game_folder = folder.clone();
            let _ = state.save();
        }
        self.update_install(|v| {
            v.folder = folder;
            v.result = Message::default();
        });
        self.refresh_install(false);
    }

    pub fn run_install(self: &Arc<Self>) {
        if !self.begin_install() {
            return;
        }
        let folder = PathBuf::from(self.install.lock().unwrap().folder.clone());
        self.update_install(|v| v.result = message("Installing...", ""));
        let ui = Arc::clone(self);
        let outcome = install::install(&folder, &self.sources, &move |line| ui.install_note(&line));
        if let Err(error) = &outcome {
            self.install_note(error);
        }
        self.update_install(|v| {
            v.busy = false;
            v.result = match &outcome {
                Ok(version) => message(
                    format!("PS4 Northstar {version} is installed. Start Titanfall 2 in shadPS4, then sign in on the Sign in tab."),
                    "ok",
                ),
                Err(error) => message(error.clone(), "error"),
            };
        });
        self.refresh_install(false);
    }

    pub fn run_uninstall(self: &Arc<Self>, remove_mods: bool) {
        if !self.begin_install() {
            return;
        }
        let folder = PathBuf::from(self.install.lock().unwrap().folder.clone());
        let ui = Arc::clone(self);
        let outcome = install::uninstall(&folder, remove_mods, &self.sources, &move |line| ui.install_note(&line));
        if let Err(error) = &outcome {
            self.install_note(error);
        }
        self.update_install(|v| {
            v.busy = false;
            v.result = match &outcome {
                Ok(()) => message("PS4 Northstar is uninstalled. The game starts as normal again.", "ok"),
                Err(error) => message(error.clone(), "error"),
            };
        });
        self.refresh_install(false);
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
            v.ea = message("Signed in to EA.", "ok");
            v.account = account;
            v.ready = true;
            v.result = message("Looking for Northstar...", "");
        });
        let console = self.remembered();
        let here = self.service.hello("127.0.0.1");
        let status = if here || console.is_empty() { ConsoleStatus::default() } else { self.service.probe(&console) };
        // A game too old to say whether it is paired is tried with the key.
        let paired = status.found && status.paired != Some(false);
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
        } else if status.found {
            self.update(|v| {
                v.mode = "remote".into();
                v.address = console.clone();
                v.form += 1;
            });
            self.result(format!("Northstar on {console} needs pairing again: type the code it shows."), "");
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
            // No code for a console that says it holds this helper's key, or,
            // when it is too old to say, for the one paired last time.
            let paired = match self.service.probe(&target).paired {
                Some(paired) => paired,
                None => target == self.remembered(),
            };
            if !four_digits && !(code.is_empty() && paired) {
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
fn get_state(ui: tauri::State<Arc<Ui>>, since: u64, install_since: u64) -> StateReply {
    ui.reply(since, install_since)
}

/// Opens the system folder picker. Async, so the window's thread isn't blocked while it's open.
#[tauri::command]
async fn choose_game_folder(app: tauri::AppHandle, ui: tauri::State<'_, Arc<Ui>>) -> Result<(), String> {
    use tauri_plugin_dialog::DialogExt;
    let picked = app
        .dialog()
        .file()
        .set_title("Choose the Titanfall 2 game folder (the one with eboot.bin)")
        .blocking_pick_folder();
    if let Some(path) = picked.and_then(|p| p.into_path().ok()) {
        let ui = Arc::clone(&ui);
        thread::spawn(move || ui.set_game_folder(path.display().to_string()));
    }
    Ok(())
}

#[tauri::command]
fn check_install(ui: tauri::State<Arc<Ui>>) {
    let ui = Arc::clone(&ui);
    thread::spawn(move || ui.refresh_install(true));
}

#[tauri::command]
fn install_now(ui: tauri::State<Arc<Ui>>) {
    let ui = Arc::clone(&ui);
    thread::spawn(move || ui.run_install());
}

#[tauri::command]
fn uninstall_now(ui: tauri::State<Arc<Ui>>, remove_mods: bool) {
    let ui = Arc::clone(&ui);
    thread::spawn(move || ui.run_uninstall(remove_mods));
}

/// Whether Northstar is running at an address, and paired with this helper,
/// for the page to leave out the code box.
#[tauri::command]
async fn check_console(ui: tauri::State<'_, Arc<Ui>>, address: String) -> Result<ConsoleStatus, String> {
    let ui = Arc::clone(&ui);
    tauri::async_runtime::spawn_blocking(move || ui.service.probe(address.trim()))
        .await
        .map_err(|e| e.to_string())
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
    let checker = Arc::clone(&ui);
    thread::spawn(move || checker.refresh_install(true));
    let result = tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Arc::clone(&ui))
        .invoke_handler(tauri::generate_handler![
            get_state,
            check_console,
            sign_in,
            retry,
            choose_game_folder,
            check_install,
            install_now,
            uninstall_now
        ])
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
