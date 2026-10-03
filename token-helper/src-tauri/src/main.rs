//! NorthstarPS4 token helper: signs Northstar on a PS4, or in shadPS4, in to
//! Atlas through the EA app on this computer, and keeps it signed in.
//!
//! Atlas gives out a player token only in exchange for an EA authorization
//! code (/client/origin_auth), and a token lasts about a day; minting a new
//! one ends the account's previous session. A PS4 cannot get an EA code, so
//! the helper asks the EA app for one (lsx.rs), exchanges it, hands the token
//! to the game over the network (the game shows its address and a code in the
//! Launch Northstar error), and then serves new tokens whenever the game asks.
//!
//! Without options it opens its window (gui.rs, page in ui/); with --console,
//! --local, --once or --cli it runs in the terminal (cli.rs).

// A window app on Windows; the terminal mode attaches to the console it was started from.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod cli;
mod gui;
mod lsx;
mod options;
mod service;

#[cfg(test)]
mod tests;

use options::{usage, Options, VERSION};

/// Output for the terminal mode: a window-subsystem program starts without a
/// console, so it uses the one it was started from (or, with its output
/// redirected, the pipe it was given).
#[cfg(windows)]
fn use_parent_console() {
    use windows_sys::Win32::System::Console::{AttachConsole, GetStdHandle, ATTACH_PARENT_PROCESS, STD_OUTPUT_HANDLE};
    unsafe {
        let out = GetStdHandle(STD_OUTPUT_HANDLE);
        if out.is_null() || out == windows_sys::Win32::Foundation::INVALID_HANDLE_VALUE {
            AttachConsole(ATTACH_PARENT_PROCESS);
        }
    }
}

#[cfg(not(windows))]
fn use_parent_console() {}

fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let code = match Options::parse(args.clone()) {
        Err(error) => {
            use_parent_console();
            eprintln!("{error}\n\n{}", usage());
            2
        }
        Ok(o) if o.help => {
            use_parent_console();
            print!("{}", usage());
            0
        }
        Ok(o) if o.version => {
            use_parent_console();
            println!("NorthstarPS4 token helper {VERSION}");
            0
        }
        Ok(o) if o.terminal_mode() => {
            use_parent_console();
            cli::run(&o)
        }
        Ok(o) => gui::run(&o),
    };
    std::process::exit(code);
}
