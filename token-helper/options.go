package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strings"
)

// Options are the command-line options. Each can be given as --name or -name.
type Options struct {
	Console                        string // "<address> [code]"
	Local, Once, CLI, NoBrowser    bool
	Help, Version                  bool
	Port, ConsolePort, LsxPort     int
	MinSecondsBetweenTokens        int
	Output, KeyFile, AdvertiseHost string
	MasterServer, LauncherVersion  string
	ContentID, Title, ClientID     string
	Scope                          string
}

const usage = `NorthstarPS4 token helper %s
Signs Northstar on a PS4, or in shadPS4, in through the EA app on this computer, and keeps
it signed in while it runs. Without options it opens its page in your browser.

  --console "<address> <code>"  Sign in the console at that address, with the code the game
                                shows (Launch Northstar shows both). Just the address signs in
                                a console paired before.
  --local                       Sign in Northstar in shadPS4 on this computer.
  --cli                         Run in this terminal instead of the browser: find a running
                                game or the console paired last time, or ask for the address.
  --once                        Sign in, then exit without serving new tokens.
  --output <file>               Where atlas_identity.json goes when shadPS4 on this computer
                                is not running (default: %s).
  --port <n>                    Port the game asks for new tokens on (37011).
  --console-port <n>            Port the game listens on for a sign-in (37012).
  --advertise-host <address>    The address the game should use to reach this computer.
  --no-browser                  Print the page's address instead of opening it.
  --version                     Show the version.
  --help                        Show this.

--console, --local and --once run in the terminal too. Exit codes there: 0 signed in (or
stopped with Ctrl+C), 1 failed, 2 bad options.
`

func usageText() string { return fmt.Sprintf(usage, version, defaultOutput()) }

// shadPS4's user folder: %APPDATA%\shadPS4 on Windows, ~/Library/Application
// Support/shadPS4 on macOS, $XDG_DATA_HOME/shadPS4 (~/.local/share) on Linux.
func shadPS4Folder() string {
	if runtime.GOOS == "linux" {
		if data := os.Getenv("XDG_DATA_HOME"); data != "" {
			return filepath.Join(data, "shadPS4")
		}
		home, _ := os.UserHomeDir()
		return filepath.Join(home, ".local", "share", "shadPS4")
	}
	config, _ := os.UserConfigDir()
	return filepath.Join(config, "shadPS4")
}

func defaultOutput() string {
	return filepath.Join(shadPS4Folder(), "data", "northstar_ps4", "atlas_identity.json")
}

func defaultKeyFile() string {
	config, _ := os.UserConfigDir()
	return filepath.Join(config, "NorthstarPS4", "token-helper.json")
}

func parseOptions(args []string) (*Options, error) {
	o := &Options{}
	flags := flag.NewFlagSet("NorthstarPS4TokenHelper", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	flags.StringVar(&o.Console, "console", "", "")
	flags.BoolVar(&o.Local, "local", false, "")
	flags.BoolVar(&o.Once, "once", false, "")
	flags.BoolVar(&o.CLI, "cli", false, "")
	flags.BoolVar(&o.NoBrowser, "no-browser", false, "")
	flags.BoolVar(&o.Help, "help", false, "")
	flags.BoolVar(&o.Help, "h", false, "")
	flags.BoolVar(&o.Version, "version", false, "")
	flags.IntVar(&o.Port, "port", 37011, "")
	flags.IntVar(&o.ConsolePort, "console-port", 37012, "")
	flags.StringVar(&o.Output, "output", defaultOutput(), "")
	flags.StringVar(&o.AdvertiseHost, "advertise-host", "", "")
	// For tests and other Atlas deployments; not listed in the help.
	flags.IntVar(&o.LsxPort, "lsx-port", 3216, "")
	flags.IntVar(&o.MinSecondsBetweenTokens, "min-seconds-between-tokens", 60, "")
	flags.StringVar(&o.KeyFile, "key-file", defaultKeyFile(), "")
	flags.StringVar(&o.MasterServer, "master-server", "https://northstar.tf", "")
	flags.StringVar(&o.LauncherVersion, "launcher-version", "1.31.13", "")
	flags.StringVar(&o.ContentID, "content-id", "1039093", "")
	flags.StringVar(&o.Title, "title", "Titanfall2", "")
	flags.StringVar(&o.ClientID, "client-id", "TITANFALL2-PC-SERVER", "")
	flags.StringVar(&o.Scope, "scope", "", "")
	if err := flags.Parse(args); err != nil {
		message := err.Error()
		if name, found := strings.CutPrefix(message, "flag provided but not defined: "); found {
			return nil, fmt.Errorf("Unknown option %s.", name)
		}
		if name, found := strings.CutPrefix(message, "flag needs an argument: "); found {
			return nil, fmt.Errorf("%s needs a value.", name)
		}
		if m := invalidValue.FindStringSubmatch(message); m != nil {
			return nil, fmt.Errorf("--%s needs a number.", m[1])
		}
		return nil, errors.New(strings.ToUpper(message[:1]) + message[1:] + ".")
	}
	if flags.NArg() > 0 {
		return nil, fmt.Errorf("Unexpected %q. Put a console address after --console.", flags.Arg(0))
	}
	for _, port := range []struct {
		name  string
		value int
	}{{"--port", o.Port}, {"--console-port", o.ConsolePort}, {"--lsx-port", o.LsxPort}} {
		if port.value < 1 || port.value > 65535 {
			return nil, fmt.Errorf("%s needs a number from 1 to 65535.", port.name)
		}
	}
	if o.MinSecondsBetweenTokens < 0 {
		return nil, errors.New("--min-seconds-between-tokens cannot be negative.")
	}
	o.Console = strings.TrimSpace(o.Console)
	if strings.EqualFold(o.Console, "local") {
		o.Console = ""
		o.Local = true
	}
	if o.Console != "" {
		if address, _ := splitTarget(o.Console); !isAddress(address) {
			return nil, fmt.Errorf("%q is not an address and code, for example \"192.168.1.20 4821\".", o.Console)
		}
	}
	return o, nil
}

// TerminalMode reports whether the options ask for the terminal rather than the browser.
func (o *Options) TerminalMode() bool { return o.CLI || o.Local || o.Once || o.Console != "" }

var (
	codeSuffix   = regexp.MustCompile(`^([0-9.]+):(\d{4})$`)
	invalidValue = regexp.MustCompile(`^invalid value ".*" for flag -+(\S+?):`)
)

// splitTarget splits "<address> <code>" (or "<address>:<code>", "<address>,<code>").
func splitTarget(text string) (string, string) {
	parts := strings.FieldsFunc(strings.TrimSpace(text), func(r rune) bool {
		return r == ' ' || r == '\t' || r == ','
	})
	if len(parts) == 1 {
		if m := codeSuffix.FindStringSubmatch(parts[0]); m != nil {
			return m[1], m[2]
		}
	}
	switch len(parts) {
	case 0:
		return "", ""
	case 1:
		return parts[0], ""
	}
	return parts[0], parts[1]
}

func (o *Options) NewService(state *HelperState) *Service {
	return &Service{
		LsxPort:                 o.LsxPort,
		ContentID:               o.ContentID,
		Title:                   o.Title,
		ClientID:                o.ClientID,
		Scope:                   o.Scope,
		MasterServer:            o.MasterServer,
		UserAgent:               "R2Northstar/" + o.LauncherVersion + "+ps4 NorthstarPS4TokenHelper",
		Port:                    o.Port,
		ConsolePort:             o.ConsolePort,
		MinSecondsBetweenTokens: o.MinSecondsBetweenTokens,
		Output:                  o.Output,
		AdvertiseHost:           o.AdvertiseHost,
		Key:                     state.Key,
	}
}

// HelperState is the pairing key and the console signed in last time, in
// token-helper.json (in %APPDATA%\NorthstarPS4 on Windows):
// {"key": "<32 hex>", "console": "<address>" or null}.
type HelperState struct {
	Key, Console string
	path         string
}

func loadState(path string) (*HelperState, error) {
	state := &HelperState{path: path}
	if data, err := os.ReadFile(path); err == nil {
		var saved struct {
			Key     string  `json:"key"`
			Console *string `json:"console"`
		}
		if json.Unmarshal(data, &saved) == nil {
			if hex32.MatchString(saved.Key) {
				state.Key = saved.Key
			}
			if saved.Console != nil && isAddress(*saved.Console) {
				state.Console = *saved.Console
			}
		}
	}
	if state.Key == "" {
		bytes := make([]byte, 16)
		if _, err := rand.Read(bytes); err != nil {
			return nil, err
		}
		state.Key = hex.EncodeToString(bytes)
		if err := state.Save(); err != nil {
			return nil, fmt.Errorf("could not save %s: %w", path, err)
		}
	}
	return state, nil
}

func (s *HelperState) Save() error {
	var console any
	if s.Console != "" {
		console = s.Console
	}
	data, _ := json.MarshalIndent(map[string]any{"key": s.Key, "console": console}, "", "  ")
	if err := os.MkdirAll(filepath.Dir(s.path), 0o755); err != nil {
		return err
	}
	return os.WriteFile(s.path, append(data, '\n'), 0o600)
}
