// NorthstarPS4 token helper: signs Northstar on a PS4, or in shadPS4, in to
// Atlas through the EA app on this computer, and keeps it signed in.
//
// Atlas gives out a player token only in exchange for an EA authorization
// code (/client/origin_auth), and a token lasts about a day; minting a new one
// ends the account's previous session. A PS4 cannot get an EA code, so the
// helper asks the EA app for one (lsx.go), exchanges it, hands the token to
// the game over the network (the game shows its address and a code in the
// Launch Northstar error), and then serves new tokens whenever the game asks.
//
// Without options it runs its page in the default browser (ui.go); with
// --console, --local, --once or --cli it runs in the terminal (cli.go). Build
// with scripts/Build-TokenHelper.ps1 for Windows, macOS and Linux.
package main

import (
	"fmt"
	"io"
	"os"
)

var version = "1.0.0" // set by the build

func main() { os.Exit(run(os.Args[1:], os.Stdin, os.Stdout, os.Stderr)) }

func run(args []string, stdin io.Reader, stdout, stderr io.Writer) int {
	o, err := parseOptions(args)
	if err != nil {
		fmt.Fprintln(stderr, err)
		fmt.Fprintln(stderr)
		fmt.Fprint(stderr, usageText())
		return 2
	}
	if o.Help {
		fmt.Fprint(stdout, usageText())
		return 0
	}
	if o.Version {
		fmt.Fprintln(stdout, "NorthstarPS4 token helper "+version)
		return 0
	}
	if o.TerminalMode() {
		return runCLI(o, stdin, stdout)
	}
	return runUI(o, stdin, stdout)
}
