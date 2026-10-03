package main

import (
	"bufio"
	"crypto/rand"
	"crypto/subtle"
	_ "embed"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"os/exec"
	"os/signal"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// The browser UI: a page served on 127.0.0.1 at a random port, opened in the
// default browser. The page polls /api/state and posts the player's choices;
// the work runs here, one step at a time. The API only answers requests that
// name this address in Host (no DNS rebinding) and carry the session key from
// the page's URL in X-Session, and posts must be JSON (so another site's form
// cannot reach it): a sign-in sends the player's token to the address given.

//go:embed ui/index.html
var indexHTML []byte

type uiMessage struct {
	Text string `json:"text"`
	Kind string `json:"kind"` // "", "ok" or "error"
}

type uiView struct {
	EA      uiMessage `json:"ea"`
	Retry   bool      `json:"retry"`
	Ready   bool      `json:"ready"`
	Busy    bool      `json:"busy"`
	Mode    string    `json:"mode"` // "local" or "remote"
	Address string    `json:"address"`
	Form    int       `json:"form"` // bumped when the helper itself changes mode or address
	Result  uiMessage `json:"result"`
}

type ui struct {
	options *Options
	state   *HelperState
	service *Service
	session string
	host    string
	quit    chan struct{}
	once    sync.Once

	mu   sync.Mutex
	view uiView
	id   Identity
}

func (u *ui) update(change func(v *uiView)) {
	u.mu.Lock()
	change(&u.view)
	u.mu.Unlock()
}

// begin marks the UI busy; false if it already was.
func (u *ui) begin() bool {
	u.mu.Lock()
	defer u.mu.Unlock()
	if u.view.Busy {
		return false
	}
	u.view.Busy = true
	return true
}

func (u *ui) end() { u.update(func(v *uiView) { v.Busy = false }) }

func (u *ui) result(text, kind string) {
	u.update(func(v *uiView) { v.Result = uiMessage{text, kind} })
}

func sentence(text string) string {
	if text == "" {
		return text
	}
	text = strings.ToUpper(text[:1]) + text[1:]
	if !strings.HasSuffix(text, ".") {
		text += "."
	}
	return text
}

// start gets a token, then signs in a game running on this computer or the
// console paired last time.
func (u *ui) start() {
	if !u.begin() {
		return
	}
	u.update(func(v *uiView) {
		v.EA = uiMessage{"Getting a token from the EA app...", ""}
		v.Retry = false
	})
	id, err := u.service.Mint()
	if err != nil {
		u.update(func(v *uiView) {
			v.EA = uiMessage{err.Error(), "error"}
			v.Retry = true
			v.Ready = false
			v.Result = uiMessage{}
			v.Busy = false
		})
		return
	}
	u.update(func(v *uiView) {
		u.id = id
		v.EA = uiMessage{"Signed in to EA as account " + id.UID + ".", "ok"}
		v.Ready = true
		v.Result = uiMessage{"Looking for Northstar...", ""}
	})
	here := u.service.Hello("127.0.0.1")
	paired := !here && u.state.Console != "" && u.service.Hello(u.state.Console)
	u.end()
	switch {
	case here:
		u.update(func(v *uiView) { v.Mode = "local"; v.Form++ })
		u.signIn("local", "", "")
	case paired:
		u.update(func(v *uiView) { v.Mode = "remote"; v.Address = u.state.Console; v.Form++ })
		u.signIn("remote", u.state.Console, "")
	case u.state.Console != "":
		u.result("Northstar is not running on "+u.state.Console+". Start it, then select Sign in.", "")
	default:
		u.result("Choose where Northstar is running, then select Sign in.", "")
	}
}

func (u *ui) signIn(mode, address, code string) {
	remote := mode == "remote"
	target := "127.0.0.1"
	if remote {
		target = strings.TrimSpace(address)
		code = strings.TrimSpace(code)
		if !isAddress(target) {
			u.result("Type the address the game shows, for example 192.168.1.20.", "error")
			return
		}
		if !(len(code) == 4 && strings.Trim(code, "0123456789") == "") && !(code == "" && target == u.state.Console) {
			u.result("Type the 4-digit code the game shows.", "error")
			return
		}
	}
	if !u.begin() {
		return
	}
	defer u.end()
	u.result("Signing in...", "")
	// A token from the last minute is reused; an older one is replaced.
	id, err := u.service.Mint()
	if err != nil {
		u.result(err.Error(), "error")
		return
	}
	u.mu.Lock()
	u.id = id
	u.mu.Unlock()
	saved := false
	if !remote {
		if u.service.Hello(target) {
			err = u.service.SignIn(target, "", id)
		} else if err = u.service.WriteIdentity(id, u.service.LocalRefreshURL()); err != nil {
			u.result("Could not save the sign-in to "+u.options.Output+": "+err.Error(), "error")
			return
		} else {
			saved = true
		}
	} else {
		err = u.service.SignIn(target, code, id)
	}
	if err != nil {
		u.result(sentence(err.Error()), "error")
		return
	}

	u.service.SetWriteIdentityOnRefresh(!remote)
	serveErr := u.service.StartServing(remote)
	var message string
	switch {
	case remote:
		u.state.Console = target
		u.state.Save()
		message = "Signed in Northstar on " + target + ". Select Launch Northstar in the game."
		u.service.Note("signed in Northstar on " + target)
	case saved:
		message = "Saved. Northstar in shadPS4 on this computer picks up the sign-in when it starts."
		u.service.Note("saved the sign-in for shadPS4 on this computer")
	default:
		message = "Signed in Northstar in shadPS4 on this computer. Select Launch Northstar in the game."
		u.service.Note("signed in Northstar in shadPS4 on this computer")
	}
	if serveErr != nil {
		u.result(message+" But "+serveErr.Error(), "error")
	} else {
		u.result(message, "ok")
	}
}

// allowed checks that a request comes from the page this run opened.
func (u *ui) allowed(r *http.Request, api bool) bool {
	if r.Host != u.host && r.Host != strings.Replace(u.host, "127.0.0.1", "localhost", 1) {
		return false
	}
	if !api {
		return true
	}
	if subtle.ConstantTimeCompare([]byte(r.Header.Get("X-Session")), []byte(u.session)) != 1 {
		return false
	}
	return r.Method == http.MethodGet || strings.HasPrefix(r.Header.Get("Content-Type"), "application/json")
}

func (u *ui) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/" || !u.allowed(r, false) {
			http.NotFound(w, r)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		w.Header().Set("Content-Security-Policy",
			"default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; img-src data:; frame-ancestors 'none'")
		w.Header().Set("Cache-Control", "no-store")
		w.Write(indexHTML)
	})
	api := func(method string, handle func(w http.ResponseWriter, r *http.Request)) http.HandlerFunc {
		return func(w http.ResponseWriter, r *http.Request) {
			if r.Method != method || !u.allowed(r, true) {
				writeJSON(w, http.StatusForbidden, map[string]string{"error": "forbidden"})
				return
			}
			handle(w, r)
		}
	}
	mux.HandleFunc("/api/state", api(http.MethodGet, func(w http.ResponseWriter, r *http.Request) {
		since, _ := strconv.Atoi(r.URL.Query().Get("since"))
		u.mu.Lock()
		view := u.view
		u.mu.Unlock()
		writeJSON(w, http.StatusOK, struct {
			uiView
			Events  []Event `json:"events"`
			Version string  `json:"version"`
		}{view, u.service.EventsSince(since), version})
	}))
	mux.HandleFunc("/api/signin", api(http.MethodPost, func(w http.ResponseWriter, r *http.Request) {
		var request struct{ Mode, Address, Code string }
		if json.NewDecoder(io.LimitReader(r.Body, 4096)).Decode(&request) != nil {
			writeJSON(w, http.StatusBadRequest, map[string]string{"error": "bad request"})
			return
		}
		u.mu.Lock()
		ready := u.view.Ready
		u.mu.Unlock()
		if ready {
			go u.signIn(request.Mode, request.Address, request.Code)
		}
		writeJSON(w, http.StatusAccepted, map[string]bool{"started": ready})
	}))
	mux.HandleFunc("/api/retry", api(http.MethodPost, func(w http.ResponseWriter, r *http.Request) {
		go u.start()
		writeJSON(w, http.StatusAccepted, map[string]bool{"started": true})
	}))
	mux.HandleFunc("/api/quit", api(http.MethodPost, func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, map[string]bool{"stopping": true})
		u.once.Do(func() { close(u.quit) })
	}))
	return mux
}

func openBrowser(address string) error {
	switch runtime.GOOS {
	case "windows":
		return exec.Command("rundll32", "url.dll,FileProtocolHandler", address).Start()
	case "darwin":
		return exec.Command("open", address).Start()
	default:
		return exec.Command("xdg-open", address).Start()
	}
}

// waitIfInteractive keeps a double-clicked window open long enough to read an error.
func waitIfInteractive(stdin io.Reader, stdout io.Writer) {
	if file, ok := stdin.(*os.File); ok {
		if info, err := file.Stat(); err == nil && info.Mode()&os.ModeCharDevice != 0 {
			fmt.Fprintln(stdout, "Press Enter to close.")
			bufio.NewReader(stdin).ReadString('\n')
		}
	}
}

func runUI(o *Options, stdin io.Reader, stdout io.Writer) int {
	fail := func(format string, args ...any) int {
		fmt.Fprintf(stdout, format+"\n", args...)
		waitIfInteractive(stdin, stdout)
		return 1
	}
	state, err := loadState(o.KeyFile)
	if err != nil {
		return fail("%s", err)
	}
	service := o.NewService(state)
	service.OnNote = func(line string) { fmt.Fprintln(stdout, line) }
	sessionBytes := make([]byte, 16)
	if _, err := rand.Read(sessionBytes); err != nil {
		return fail("%s", err)
	}
	u := &ui{options: o, state: state, service: service, session: hex.EncodeToString(sessionBytes), quit: make(chan struct{})}
	u.view.Mode = "local"
	if state.Console != "" {
		u.view.Mode = "remote"
		u.view.Address = state.Console
	}
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		return fail("Could not start the helper's page: %s", err)
	}
	u.host = listener.Addr().String()
	page := "http://" + u.host + "/?s=" + u.session
	server := &http.Server{Handler: u.routes(), ReadHeaderTimeout: 5 * time.Second}
	go server.Serve(listener)
	go u.start()

	fmt.Fprintln(stdout, "NorthstarPS4 token helper "+version)
	if o.NoBrowser || openBrowser(page) != nil {
		fmt.Fprintln(stdout, "Open this page in your browser: "+page)
	} else {
		fmt.Fprintln(stdout, "The helper's page is open in your browser. If it isn't, open: "+page)
	}
	fmt.Fprintln(stdout, "Leave this window open while you play: Northstar asks the helper for a new sign-in when the old one expires.")
	fmt.Fprintln(stdout, "Close this window, or select Stop on the page, to stop the helper.")

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	select {
	case <-stop:
	case <-u.quit:
		time.Sleep(200 * time.Millisecond) // let the page get its reply
	}
	service.Stop()
	server.Close()
	fmt.Fprintln(stdout, "Stopped.")
	return 0
}
