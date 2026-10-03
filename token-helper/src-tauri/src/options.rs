//! Command-line options, the default paths on each system, and the saved
//! pairing state.

use crate::service::{is_address, is_hex32, Settings};
use serde_json::{json, Value};
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Clone, Debug)]
pub struct Options {
    pub console: String, // "<address> [code]"
    pub install: Option<PathBuf>,
    pub uninstall: Option<PathBuf>,
    pub install_status: Option<PathBuf>,
    pub remove_mods: bool,
    pub sources: crate::install::Sources,
    pub local: bool,
    pub once: bool,
    pub cli: bool,
    pub help: bool,
    pub version: bool,
    pub port: u16,
    pub console_port: u16,
    pub lsx_port: u16,
    pub min_seconds_between_tokens: u64,
    pub output: PathBuf,
    pub key_file: PathBuf,
    pub advertise_host: String,
    pub master_server: String,
    pub launcher_version: String,
    pub content_id: String,
    pub title: String,
    pub client_id: String,
    pub scope: String,
}

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

pub fn usage() -> String {
    format!(
        "NorthstarPS4 token helper {VERSION}
Signs Northstar on a PS4, or in shadPS4, in through the EA app on this computer, and keeps
it signed in while it runs. Without options it opens its window.

  --install <game folder>       Install PS4 Northstar into the game folder (the one with eboot.bin),
                                or update it, from the newest release.
  --uninstall <game folder>     Take PS4 Northstar out again; add --remove-mods to delete R2Northstar.
  --install-status <game folder>  Say what's installed in the game folder.
  --console \"<address> <code>\"  Sign in the console at that address, with the code the game
                                shows (Launch Northstar shows both). Just the address signs in
                                a console paired before.
  --local                       Sign in Northstar in shadPS4 on this computer.
  --cli                         Run in the terminal and sign in a running game, or the console
                                paired last time.
  --once                        Sign in, then exit without serving new tokens.
  --output <file>               Where atlas_identity.json goes when shadPS4 on this computer
                                is not running (default: {}).
  --port <n>                    Port the game asks for new tokens on (37011).
  --console-port <n>            Port the game listens on for a sign-in (37012).
  --advertise-host <address>    The address the game should use to reach this computer.
  --version                     Show the version.
  --help                        Show this.

--install, --uninstall, --install-status, --console, --local, --once and --cli run in the
terminal instead of the window. Exit codes:
0 signed in (or stopped with Ctrl+C), 1 failed, 2 bad options. On Windows, run it with
`start /wait` (cmd) or `Start-Process -Wait` (PowerShell) to wait for it.
",
        default_output().display()
    )
}

/// shadPS4's user folder: %APPDATA%\\shadPS4 on Windows, ~/Library/Application
/// Support/shadPS4 on macOS, $XDG_DATA_HOME/shadPS4 (~/.local/share) on Linux.
pub fn shadps4_folder() -> PathBuf {
    if cfg!(target_os = "linux") {
        return dirs::data_dir().unwrap_or_default().join("shadPS4");
    }
    dirs::config_dir().unwrap_or_default().join("shadPS4")
}

pub fn default_output() -> PathBuf {
    shadps4_folder().join("data").join("northstar_ps4").join("atlas_identity.json")
}

pub fn default_key_file() -> PathBuf {
    dirs::config_dir().unwrap_or_default().join("NorthstarPS4").join("token-helper.json")
}

impl Default for Options {
    fn default() -> Self {
        Options {
            console: String::new(),
            install: None,
            uninstall: None,
            install_status: None,
            remove_mods: false,
            sources: crate::install::Sources::default(),
            local: false,
            once: false,
            cli: false,
            help: false,
            version: false,
            port: 37011,
            console_port: 37012,
            lsx_port: 3216,
            min_seconds_between_tokens: 60,
            output: default_output(),
            key_file: default_key_file(),
            advertise_host: String::new(),
            master_server: "https://northstar.tf".into(),
            launcher_version: "1.31.13".into(),
            content_id: "1039093".into(),
            title: "Titanfall2".into(),
            client_id: "TITANFALL2-PC-SERVER".into(),
            scope: String::new(),
        }
    }
}

impl Options {
    /// Options are --name value, --name=value, or -name value.
    pub fn parse<I: IntoIterator<Item = String>>(args: I) -> Result<Options, String> {
        let mut o = Options::default();
        let mut args = args.into_iter();
        while let Some(arg) = args.next() {
            if !arg.starts_with('-') || arg == "-" || arg == "--" {
                return Err(format!("Unexpected \"{arg}\". Put a console address after --console."));
            }
            let trimmed = arg.trim_start_matches('-');
            let (name, inline) = match trimmed.split_once('=') {
                Some((name, value)) => (name.to_ascii_lowercase(), Some(value.to_string())),
                None => (trimmed.to_ascii_lowercase(), None),
            };
            let flag = |set: &mut bool| -> Result<(), String> {
                if inline.is_some() {
                    return Err(format!("--{name} does not take a value."));
                }
                *set = true;
                Ok(())
            };
            match name.as_str() {
                "local" => flag(&mut o.local)?,
                "remove-mods" => flag(&mut o.remove_mods)?,
                "once" => flag(&mut o.once)?,
                "cli" => flag(&mut o.cli)?,
                "help" | "h" | "?" => flag(&mut o.help)?,
                "version" => flag(&mut o.version)?,
                "console" | "output" | "key-file" | "advertise-host" | "master-server" | "launcher-version" | "content-id"
                | "title" | "client-id" | "scope" | "port" | "console-port" | "lsx-port" | "min-seconds-between-tokens"
                | "install" | "uninstall" | "install-status" | "releases-api" | "northstar-zip-url" | "northstar-zip-sha256"
                | "vanilla-eboot-sha256" | "patched-eboot-sha256" => {
                    let value = match inline {
                        Some(value) => value,
                        None => args.next().ok_or_else(|| format!("--{name} needs a value."))?,
                    };
                    let port = |value: &str| -> Result<u16, String> {
                        value.parse::<u16>().ok().filter(|p| *p > 0).ok_or_else(|| format!("--{name} needs a number from 1 to 65535."))
                    };
                    match name.as_str() {
                        "console" => o.console = value.trim().to_string(),
                        "install" => o.install = Some(PathBuf::from(value)),
                        "uninstall" => o.uninstall = Some(PathBuf::from(value)),
                        "install-status" => o.install_status = Some(PathBuf::from(value)),
                        // For tests and mirrors; not in the help.
                        "releases-api" => o.sources.releases_api = value,
                        "northstar-zip-url" => o.sources.northstar_zip_url = value,
                        "northstar-zip-sha256" => o.sources.northstar_zip_sha256 = value.to_ascii_lowercase(),
                        "vanilla-eboot-sha256" => o.sources.vanilla_eboot_sha256 = value.to_ascii_lowercase(),
                        "patched-eboot-sha256" => o.sources.patched_eboot_sha256 = value.to_ascii_lowercase(),
                        "output" => o.output = PathBuf::from(value),
                        "key-file" => o.key_file = PathBuf::from(value),
                        "advertise-host" => o.advertise_host = value,
                        "master-server" => o.master_server = value,
                        "launcher-version" => o.launcher_version = value,
                        "content-id" => o.content_id = value,
                        "title" => o.title = value,
                        "client-id" => o.client_id = value,
                        "scope" => o.scope = value,
                        "port" => o.port = port(&value)?,
                        "console-port" => o.console_port = port(&value)?,
                        "lsx-port" => o.lsx_port = port(&value)?,
                        _ => {
                            o.min_seconds_between_tokens =
                                value.parse().map_err(|_| format!("--{name} needs a number of seconds."))?
                        }
                    }
                }
                _ => return Err(format!("Unknown option {arg}.")),
            }
        }
        if o.console.eq_ignore_ascii_case("local") {
            o.console.clear();
            o.local = true;
        }
        if !o.console.is_empty() && !is_address(&split_target(&o.console).0) {
            return Err(format!("\"{}\" is not an address and code, for example \"192.168.1.20 4821\".", o.console));
        }
        Ok(o)
    }

    /// Whether the options ask for the terminal rather than the window.
    pub fn terminal_mode(&self) -> bool {
        self.cli || self.local || self.once || !self.console.is_empty() || self.install_mode()
    }

    /// Whether the options ask to install, uninstall or check a game folder.
    pub fn install_mode(&self) -> bool {
        self.install.is_some() || self.uninstall.is_some() || self.install_status.is_some()
    }

    pub fn settings(&self) -> Settings {
        Settings {
            lsx_port: self.lsx_port,
            content_id: self.content_id.clone(),
            title: self.title.clone(),
            client_id: self.client_id.clone(),
            scope: self.scope.clone(),
            master_server: self.master_server.clone(),
            user_agent: format!("R2Northstar/{}+ps4 NorthstarPS4TokenHelper", self.launcher_version),
            port: self.port,
            console_port: self.console_port,
            min_seconds_between_tokens: self.min_seconds_between_tokens,
            output: self.output.clone(),
            advertise_host: self.advertise_host.clone(),
        }
    }
}

/// Splits "<address> <code>" (or "<address>:<code>", "<address>,<code>").
pub fn split_target(text: &str) -> (String, String) {
    let parts: Vec<&str> = text.split(|c: char| c == ' ' || c == '\t' || c == ',').filter(|p| !p.is_empty()).collect();
    if parts.len() == 1 {
        if let Some((address, code)) = parts[0].rsplit_once(':') {
            if code.len() == 4 && code.bytes().all(|b| b.is_ascii_digit()) && address.bytes().all(|b| b.is_ascii_digit() || b == b'.') {
                return (address.into(), code.into());
            }
        }
    }
    (parts.first().copied().unwrap_or_default().into(), parts.get(1).copied().unwrap_or_default().into())
}

/// The pairing key and the console signed in last time, in token-helper.json
/// (%APPDATA%\NorthstarPS4 on Windows): {"key": "<32 hex>", "console": "<address>" or null}.
pub struct HelperState {
    pub key: String,
    pub console: String,
    pub game_folder: String,
    path: PathBuf,
}

impl HelperState {
    pub fn load(path: &Path) -> Result<HelperState, String> {
        let mut state =
            HelperState { key: String::new(), console: String::new(), game_folder: String::new(), path: path.to_path_buf() };
        if let Ok(text) = fs::read_to_string(path) {
            if let Ok(saved) = serde_json::from_str::<Value>(&text) {
                if let Some(key) = saved["key"].as_str().filter(|k| is_hex32(k)) {
                    state.key = key.into();
                }
                if let Some(console) = saved["console"].as_str().filter(|c| is_address(c)) {
                    state.console = console.into();
                }
                if let Some(folder) = saved["gameFolder"].as_str() {
                    state.game_folder = folder.into();
                }
            }
        }
        if state.key.is_empty() {
            let mut bytes = [0u8; 16];
            getrandom::getrandom(&mut bytes).map_err(|e| e.to_string())?;
            state.key = hex::encode(bytes);
            state.save().map_err(|e| format!("Could not save {}: {e}", path.display()))?;
        }
        Ok(state)
    }

    pub fn save(&self) -> std::io::Result<()> {
        let console = if self.console.is_empty() { Value::Null } else { json!(self.console) };
        let game_folder = if self.game_folder.is_empty() { Value::Null } else { json!(self.game_folder) };
        let text =
            serde_json::to_string_pretty(&json!({"key": self.key, "console": console, "gameFolder": game_folder})).unwrap();
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(&self.path, text + "\n")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn parse(args: &str) -> Result<Options, String> {
        Options::parse(args.split_whitespace().map(String::from))
    }

    #[test]
    fn parses_options() {
        let o = parse("").unwrap();
        assert!(!o.terminal_mode() && o.port == 37011 && o.console_port == 37012);
        let o = parse("-console LOCAL").unwrap();
        assert!(o.local && o.console.is_empty() && o.terminal_mode());
        let o = Options::parse(["--console".into(), "10.0.0.5 1234".into(), "--once".into()]).unwrap();
        assert_eq!(o.console, "10.0.0.5 1234");
        assert!(o.once);
        assert_eq!(parse("--port=40000").unwrap().port, 40000);
        for (args, want) in [
            ("--nonsense", "Unknown option --nonsense"),
            ("--console", "needs a value"),
            ("--console bad/address", "is not an address"),
            ("--port 70000", "--port needs a number"),
            ("--port x", "--port needs a number"),
            ("--once=yes", "does not take a value"),
            ("stray", "Unexpected"),
        ] {
            let error = parse(args).unwrap_err();
            assert!(error.contains(want), "{args}: {error}");
        }
    }

    #[test]
    fn splits_targets() {
        assert_eq!(split_target("192.168.1.20 4821"), ("192.168.1.20".into(), "4821".into()));
        assert_eq!(split_target(" 192.168.1.20,4821 "), ("192.168.1.20".into(), "4821".into()));
        assert_eq!(split_target("192.168.1.20:4821"), ("192.168.1.20".into(), "4821".into()));
        assert_eq!(split_target("192.168.1.20"), ("192.168.1.20".into(), "".into()));
        assert_eq!(split_target(""), ("".into(), "".into()));
    }

    #[test]
    fn state_keeps_key_and_console() {
        let dir = std::env::temp_dir().join(format!("ns-state-{}", std::process::id()));
        let path = dir.join("sub").join("token-helper.json");
        let mut state = HelperState::load(&path).unwrap();
        assert!(is_hex32(&state.key) && state.console.is_empty());
        state.console = "192.168.1.20".into();
        state.save().unwrap();
        let again = HelperState::load(&path).unwrap();
        assert_eq!((again.key.as_str(), again.console.as_str()), (state.key.as_str(), "192.168.1.20"));
        // The earlier helpers' file, with "console": null, still loads.
        fs::write(&path, r#"{"key":"00112233445566778899aabbccddeeff","console":null}"#).unwrap();
        let old = HelperState::load(&path).unwrap();
        assert_eq!((old.key.as_str(), old.console.as_str()), ("00112233445566778899aabbccddeeff", ""));
        let _ = fs::remove_dir_all(dir);
    }
}
