package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"os/signal"
	"strings"
	"syscall"
)

// runCLI is the helper in a terminal. Exit codes: 0 signed in (and, without
// --once, stopped with Ctrl+C), 1 failed.
func runCLI(o *Options, stdin io.Reader, stdout io.Writer) int {
	say := func(format string, args ...any) { fmt.Fprintf(stdout, format+"\n", args...) }
	state, err := loadState(o.KeyFile)
	if err != nil {
		say("%s", err)
		return 1
	}
	service := o.NewService(state)

	say("NorthstarPS4 token helper")
	say("Getting a Northstar token through the EA app...")
	id, err := service.Mint()
	if err != nil {
		say("%s", err)
		return 1
	}
	say("Signed in to EA as account %s.", id.UID)

	// Where the game is: address stays empty when the identity file is written instead.
	address := ""
	switch {
	case o.Local:
		if service.Hello("127.0.0.1") && service.SignIn("127.0.0.1", "", id) == nil {
			address = "127.0.0.1"
		}
	case o.Console != "":
		target, code := splitTarget(o.Console)
		if err := service.SignIn(target, code, id); err != nil {
			say("Sign-in failed: %s.", err)
			return 1
		}
		address = target
	case service.Hello("127.0.0.1"):
		if err := service.SignIn("127.0.0.1", "", id); err != nil {
			say("Sign-in failed: %s.", err)
			return 1
		}
		address = "127.0.0.1"
	case state.Console != "" && service.Hello(state.Console) && service.SignIn(state.Console, "", id) == nil:
		address = state.Console
	default:
		reader := bufio.NewReader(stdin)
		for {
			say("")
			say("Type the address and code that Northstar shows on the PS4 (for example: 192.168.1.20 4821).")
			say("If Northstar runs in shadPS4 on this computer and is not started yet, just press Enter.")
			fmt.Fprint(stdout, "> ")
			answer, _ := reader.ReadString('\n')
			if strings.TrimSpace(answer) == "" {
				break
			}
			target, code := splitTarget(answer)
			err := service.SignIn(target, code, id)
			if err == nil {
				address = target
				break
			}
			say("Sign-in failed: %s.", err)
		}
	}

	remote := address != "" && !isLoopback(address)
	switch {
	case remote:
		say("Signed in Northstar on %s.", address)
		state.Console = address
		if err := state.Save(); err != nil {
			say("Could not remember this console: %s", err)
		}
	case address != "":
		say("Signed in Northstar running in shadPS4 on this computer.")
	default:
		if err := service.WriteIdentity(id, service.LocalRefreshURL()); err != nil {
			say("Could not save the sign-in to %s: %s", o.Output, err)
			return 1
		}
		say("Saved the sign-in to %s; Northstar in shadPS4 on this computer picks it up when it starts.", o.Output)
	}
	if o.Once {
		return 0
	}

	service.SetWriteIdentityOnRefresh(!remote)
	service.OnNote = func(line string) { say("%s", line) } // before serving starts calling it
	if err := service.StartServing(remote); err != nil {
		say("Cannot keep the game signed in: %s", err)
		return 1
	}
	say("")
	say("Leave this window open while you play: Northstar asks it for a new token when the old one expires.")
	say("Close the window or press Ctrl+C to stop.")
	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	<-stop
	service.Stop()
	return 0
}
