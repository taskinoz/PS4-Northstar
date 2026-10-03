//! The helper in a terminal: --console, --local, --once or --cli. Exit codes:
//! 0 signed in (and, without --once, stopped with Ctrl+C), 1 failed.

use crate::options::{split_target, HelperState, Options};
use crate::service::{is_loopback, Service};
#[cfg(windows)]
use std::sync::mpsc;

pub fn run(o: &Options) -> i32 {
    let mut state = match HelperState::load(&o.key_file) {
        Ok(state) => state,
        Err(error) => {
            println!("{error}");
            return 1;
        }
    };
    let service = Service::new(o.settings(), state.key.clone());

    println!("NorthstarPS4 token helper");
    println!("Getting a Northstar token through the EA app...");
    let identity = match service.mint() {
        Ok(identity) => identity,
        Err(error) => {
            println!("{error}");
            return 1;
        }
    };
    println!("Signed in to EA as account {}.", identity.uid);

    // Where the game is: None when the identity file is written instead.
    let address: Option<String> = if o.local {
        (service.hello("127.0.0.1") && service.sign_in("127.0.0.1", "", &identity).is_ok()).then(|| "127.0.0.1".into())
    } else if !o.console.is_empty() {
        let (target, code) = split_target(&o.console);
        if let Err(error) = service.sign_in(&target, &code, &identity) {
            println!("Sign-in failed: {error}.");
            return 1;
        }
        Some(target)
    } else if service.hello("127.0.0.1") {
        if let Err(error) = service.sign_in("127.0.0.1", "", &identity) {
            println!("Sign-in failed: {error}.");
            return 1;
        }
        Some("127.0.0.1".into())
    } else if !state.console.is_empty() && service.hello(&state.console) {
        if let Err(error) = service.sign_in(&state.console, "", &identity) {
            println!("Sign-in failed: {error}.");
            return 1;
        }
        Some(state.console.clone())
    } else {
        println!("Northstar is not running on this computer. For a PS4, give the address and code the game shows:");
        println!("  --console \"192.168.1.20 4821\"");
        None
    };

    let remote = address.as_deref().is_some_and(|a| !is_loopback(a));
    match &address {
        Some(address) if remote => {
            println!("Signed in Northstar on {address}.");
            state.console = address.clone();
            if let Err(error) = state.save() {
                println!("Could not remember this console: {error}");
            }
        }
        Some(_) => println!("Signed in Northstar running in shadPS4 on this computer."),
        None => {
            if let Err(error) = service.write_identity(&identity, &service.local_refresh_url()) {
                println!("Could not save the sign-in to {}: {error}", o.output.display());
                return 1;
            }
            println!(
                "Saved the sign-in to {}; Northstar in shadPS4 on this computer picks it up when it starts.",
                o.output.display()
            );
        }
    }
    if o.once {
        return 0;
    }

    service.set_write_identity_on_refresh(!remote);
    service.set_on_note(Box::new(|line| println!("{line}")));
    if let Err(error) = service.start_serving(remote) {
        println!("Cannot keep the game signed in: {error}");
        return 1;
    }
    println!();
    println!("Keep this running while you play: Northstar asks it for a new token when the old one expires.");
    println!("Press Ctrl+C to stop.");
    wait_for_ctrl_c();
    service.stop();
    0
}

/// Returns on Ctrl+C, Ctrl+Break or the console closing.
#[cfg(windows)]
fn wait_for_ctrl_c() {
    use std::sync::Mutex;
    use windows_sys::Win32::System::Console::SetConsoleCtrlHandler;
    static STOP: Mutex<Option<mpsc::Sender<()>>> = Mutex::new(None);
    unsafe extern "system" fn on_event(_: u32) -> i32 {
        if let Some(stop) = STOP.lock().unwrap().as_ref() {
            let _ = stop.send(());
        }
        1
    }
    let (stop, stopped) = mpsc::channel();
    *STOP.lock().unwrap() = Some(stop);
    unsafe { SetConsoleCtrlHandler(Some(on_event), 1) };
    let _ = stopped.recv();
}

/// Ctrl+C's default action ends the process, which stops serving too.
#[cfg(not(windows))]
fn wait_for_ctrl_c() {
    loop {
        std::thread::park();
    }
}
