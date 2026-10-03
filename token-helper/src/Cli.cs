using System;
using System.Threading;

namespace NorthstarPS4.TokenHelper {

// NorthstarPS4TokenHelperCli.exe: the helper in a terminal. Exit codes: 0
// signed in (and, without --once, stopped with Ctrl+C), 1 failed, 2 bad options.
public static class Cli {
    static void Say(string text, ConsoleColor? color = null) {
        if (color.HasValue) Console.ForegroundColor = color.Value;
        Console.WriteLine(text);
        if (color.HasValue) Console.ResetColor();
    }

    public static int Run(Options options) {
        HelperState state = HelperState.Load(options.KeyFile);
        TokenService service = options.CreateService(state);

        Say("NorthstarPS4 token helper");
        Say("Getting a Northstar token through the EA app...");
        string[] identity;
        try {
            identity = service.Mint();
        } catch (Exception e) {
            Say(e.Message, ConsoleColor.Red);
            return 1;
        }
        Say("Signed in to EA as account " + identity[0] + ".");

        // Where the game is: address stays null when the identity file is written instead.
        string address = null, failure;
        if (options.Local) {
            if (service.Hello("127.0.0.1") && service.SignIn("127.0.0.1", "", identity) == null) address = "127.0.0.1";
        } else if (options.Target != "") {
            string[] target = Options.SplitTarget(options.Target);
            failure = service.SignIn(target[0], target[1], identity);
            if (failure != null) { Say("Sign-in failed: " + failure + ".", ConsoleColor.Red); return 1; }
            address = target[0];
        } else if (service.Hello("127.0.0.1")) {
            failure = service.SignIn("127.0.0.1", "", identity);
            if (failure != null) { Say("Sign-in failed: " + failure + ".", ConsoleColor.Red); return 1; }
            address = "127.0.0.1";
        } else if (state.Console != null && service.Hello(state.Console) && service.SignIn(state.Console, "", identity) == null) {
            address = state.Console;
        } else {
            for (;;) {
                Say("");
                Say("Type the address and code that Northstar shows on the PS4 (for example: 192.168.1.20 4821).");
                Say("If Northstar runs in shadPS4 on this PC and is not started yet, just press Enter.");
                Console.Write("> ");
                string answer = Console.ReadLine();
                if (answer == null || answer.Trim() == "") break;
                string[] target = Options.SplitTarget(answer);
                failure = service.SignIn(target[0], target[1], identity);
                if (failure == null) { address = target[0]; break; }
                Say("Sign-in failed: " + failure + ".", ConsoleColor.Red);
            }
        }

        bool remote = address != null && !TokenService.IsLoopback(address);
        if (address != null) {
            if (remote) {
                Say("Signed in Northstar on " + address + ".", ConsoleColor.Green);
                state.Console = address;
                state.Save();
            } else {
                Say("Signed in Northstar running in shadPS4 on this PC.", ConsoleColor.Green);
            }
        } else {
            try {
                service.WriteIdentity(identity, service.LocalRefreshUrl);
            } catch (Exception e) {
                Say("Could not save the sign-in to " + options.Output + ": " + e.Message, ConsoleColor.Red);
                return 1;
            }
            Say("Saved the sign-in to " + options.Output + "; Northstar in shadPS4 on this PC picks it up when it starts.",
                ConsoleColor.Green);
        }
        if (options.Once) return 0;

        service.WriteIdentityOnRefresh = !remote;
        failure = service.StartServing(remote);
        if (failure != null) { Say("Cannot keep the game signed in: " + failure, ConsoleColor.Red); return 1; }
        Say("");
        Say("Leave this window open while you play: Northstar asks it for a new token when the old one expires.");
        Say("Close the window or press Ctrl+C to stop.");
        var stop = new ManualResetEvent(false);
        Console.CancelKeyPress += delegate(object sender, ConsoleCancelEventArgs e) { e.Cancel = true; stop.Set(); };
        while (!stop.WaitOne(250)) {
            string line;
            while (service.Events.TryDequeue(out line)) Say(line);
        }
        service.Stop();
        return 0;
    }
}

}
