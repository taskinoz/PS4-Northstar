package main

import (
	"bufio"
	"bytes"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"testing"
	"time"
)

const fakeUID, fakeCode, consoleCode = "1012345678901", "QUOxFAKEcodeForTests", "4821"

// fakeLSX plays the EA app's SDK server: handshake, GetProfile, GetAuthCode,
// with an unrelated event before every reply.
func fakeLSX(t *testing.T) int {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { listener.Close() })
	go func() {
		for {
			conn, err := listener.Accept()
			if err != nil {
				return
			}
			go serveFakeLSX(conn)
		}
	}()
	return listener.Addr().(*net.TCPAddr).Port
}

func serveFakeLSX(conn net.Conn) {
	defer conn.Close()
	reader := bufio.NewReader(conn)
	read := func() (string, bool) {
		message, err := reader.ReadString(0)
		return strings.TrimSuffix(message, "\x00"), err == nil
	}
	send := func(message string) { conn.Write([]byte(message + "\x00")) }
	challenge := "00112233445566778899aabbccddeeff"
	send(`<LSX><Event sender="EALS"><Challenge key="` + challenge + `" version="3" build="fake"/></Event></LSX>`)
	message, ok := read()
	if !ok {
		return
	}
	expected := encryptHex(lsxKey(0), challenge)
	reply, found := findElement(message, "ChallengeResponse")
	content, _ := findElementText(message, "ContentId")
	if !found || reply["response"] != expected || content != "1039093" {
		send(`<LSX><Response id="0" sender="EALS"><ErrorSuccess Code="-1" Description="bad challenge"/></Response></LSX>`)
		return
	}
	key := lsxKey(uint32(expected[0])<<8 | uint32(expected[1]))
	send(`<LSX><Response id="0" sender="EALS"><ChallengeAccepted response="` + expected + `"/></Response></LSX>`)
	for {
		raw, ok := read()
		if !ok {
			return
		}
		plain, err := decryptHex(key, raw)
		if err != nil {
			return
		}
		request, _ := findElement(plain, "Request")
		out := `<ErrorSuccess Code="-3" Description="unsupported"/>`
		if _, ok := findElement(plain, "GetProfile"); ok {
			out = `<GetProfileResponse UserIndex="0" UserId="` + fakeUID + `"/>`
		} else if code, ok := findElement(plain, "GetAuthCode"); ok {
			if code["UserId"] == fakeUID && code["ClientId"] == "TITANFALL2-PC-SERVER" {
				out = `<AuthCode value="` + fakeCode + `"/>`
			} else {
				out = `<ErrorSuccess Code="-2" Description="bad client"/>`
			}
		}
		send(encryptHex(key, `<LSX><Event sender="EbisuSDK"><Login IsLoggedIn="true"/></Event></LSX>`))
		send(encryptHex(key, `<LSX><Response id="`+request["id"]+`" sender="EbisuSDK">`+out+`</Response></LSX>`))
	}
}

func findElementText(doc, name string) (string, bool) {
	start := strings.Index(doc, "<"+name+">")
	end := strings.Index(doc, "</"+name+">")
	if start < 0 || end < start {
		return "", false
	}
	return doc[start+len(name)+2 : end], true
}

// fakeAtlas mints a token for the fake EA code; only the newest is kept.
type fakeAtlas struct {
	mu     sync.Mutex
	tokens []string
}

func (a *fakeAtlas) server(t *testing.T) *httptest.Server {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/client/origin_auth" || !strings.HasPrefix(r.UserAgent(), "R2Northstar/") {
			writeJSON(w, http.StatusBadRequest, map[string]any{"success": false})
			return
		}
		if r.URL.Query().Get("id") != fakeUID || r.URL.Query().Get("token") != fakeCode {
			writeJSON(w, http.StatusForbidden, map[string]any{"success": false, "error": map[string]string{"msg": "bad code"}})
			return
		}
		token := make([]byte, 16)
		rand.Read(token)
		a.mu.Lock()
		a.tokens = append(a.tokens, hex.EncodeToString(token))
		latest := a.tokens[len(a.tokens)-1]
		a.mu.Unlock()
		writeJSON(w, http.StatusOK, map[string]any{"success": true, "token": latest})
	}))
	t.Cleanup(server.Close)
	return server
}

func (a *fakeAtlas) latest() string {
	a.mu.Lock()
	defer a.mu.Unlock()
	if len(a.tokens) == 0 {
		return ""
	}
	return a.tokens[len(a.tokens)-1]
}

// fakeConsole plays the game's sign-in listener, as a console elsewhere:
// the code, or the key of the helper it accepted last.
type fakeConsole struct {
	mu     sync.Mutex
	paired string
	pushes []map[string]string
}

func (c *fakeConsole) server(t *testing.T) (*httptest.Server, int) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.URL.Path {
		case "/northstar/hello":
			writeJSON(w, http.StatusOK, map[string]any{"app": "NorthstarPS4", "signedIn": false})
		case "/northstar/signin":
			var push map[string]string
			json.NewDecoder(r.Body).Decode(&push)
			c.mu.Lock()
			defer c.mu.Unlock()
			c.pushes = append(c.pushes, push)
			if push["code"] != consoleCode && (c.paired == "" || push["refreshKey"] != c.paired) {
				writeJSON(w, http.StatusForbidden, map[string]string{"error": "wrong code; type the code shown on the PS4"})
				return
			}
			c.paired = push["refreshKey"]
			writeJSON(w, http.StatusOK, map[string]bool{"ok": true})
		default:
			http.NotFound(w, r)
		}
	}))
	t.Cleanup(server.Close)
	return server, server.Listener.Addr().(*net.TCPAddr).Port
}

func (c *fakeConsole) last() map[string]string {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.pushes[len(c.pushes)-1]
}

func freePort(t *testing.T) int {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	return listener.Addr().(*net.TCPAddr).Port
}

func testService(t *testing.T, atlas *httptest.Server, lsxPort int) (*Service, *HelperState) {
	dir := t.TempDir()
	o, err := parseOptions([]string{"--lsx-port", strconv.Itoa(lsxPort), "--master-server", atlas.URL,
		"--key-file", filepath.Join(dir, "token-helper.json"), "--output", filepath.Join(dir, "atlas_identity.json"),
		"--port", strconv.Itoa(freePort(t)), "--console-port", strconv.Itoa(freePort(t)), "--min-seconds-between-tokens", "0"})
	if err != nil {
		t.Fatal(err)
	}
	state, err := loadState(o.KeyFile)
	if err != nil {
		t.Fatal(err)
	}
	return o.NewService(state), state
}

func TestLsxKeyMatchesOriginSDK(t *testing.T) {
	want := []byte{251, 135, 22, 197, 214, 181, 148, 115, 149, 93, 40, 78, 123, 141, 60, 108}
	if got := lsxKey(1337); !bytes.Equal(got, want) {
		t.Fatalf("lsxKey(1337) = %v", got)
	}
	if got := lsxKey(0); got[0] != 0 || got[15] != 15 {
		t.Fatalf("default key = %v", got)
	}
}

func TestEncryptRoundTrip(t *testing.T) {
	key := lsxKey(4242)
	for _, text := range []string{"", "x", "exactly sixteen!", strings.Repeat("<LSX/>", 40)} {
		got, err := decryptHex(key, encryptHex(key, text))
		if err != nil || got != text {
			t.Fatalf("round trip of %q: %q, %v", text, got, err)
		}
	}
	if _, err := decryptHex(key, "abcd"); err == nil {
		t.Fatal("a short message decrypted")
	}
}

func TestFindElement(t *testing.T) {
	doc := `<LSX><Response id="2"><AuthCode value="v"/></Response></LSX>`
	if a, ok := findElement(doc, "LSX", "Response"); !ok || a["id"] != "2" {
		t.Fatal("LSX/Response not found")
	}
	if a, ok := findElement(doc, "AuthCode"); !ok || a["value"] != "v" {
		t.Fatal("AuthCode not found")
	}
	if _, ok := findElement(doc, "LSX", "Event"); ok {
		t.Fatal("found an element that is not there")
	}
}

func TestSplitTarget(t *testing.T) {
	for input, want := range map[string][2]string{
		"192.168.1.20 4821":   {"192.168.1.20", "4821"},
		" 192.168.1.20,4821 ": {"192.168.1.20", "4821"},
		"192.168.1.20:4821":   {"192.168.1.20", "4821"},
		"192.168.1.20":        {"192.168.1.20", ""},
		"":                    {"", ""},
	} {
		if address, code := splitTarget(input); address != want[0] || code != want[1] {
			t.Errorf("splitTarget(%q) = %q, %q", input, address, code)
		}
	}
}

func TestParseOptions(t *testing.T) {
	o, err := parseOptions(nil)
	if err != nil || o.TerminalMode() || o.Port != 37011 || o.ConsolePort != 37012 {
		t.Fatalf("defaults: %+v, %v", o, err)
	}
	if o, err := parseOptions([]string{"-console", "LOCAL"}); err != nil || !o.Local || o.Console != "" || !o.TerminalMode() {
		t.Fatalf("--console local: %+v, %v", o, err)
	}
	if o, err := parseOptions([]string{"--console", "10.0.0.5 1234", "--once"}); err != nil || o.Console != "10.0.0.5 1234" || !o.Once {
		t.Fatalf("--console: %+v, %v", o, err)
	}
	for args, want := range map[string]string{
		"--nonsense":            "Unknown option -nonsense",
		"--console":             "needs a value",
		"--console bad/address": "is not an address",
		"--port 70000":          "--port needs a number",
		"--port x":              "--port needs a number",
		"stray":                 "Unexpected",
	} {
		if _, err := parseOptions(strings.Fields(args)); err == nil || !strings.Contains(err.Error(), want) {
			t.Errorf("%s: %v", args, err)
		}
	}
}

func TestStateKeepsKeyAndConsole(t *testing.T) {
	path := filepath.Join(t.TempDir(), "sub", "token-helper.json")
	state, err := loadState(path)
	if err != nil || !hex32.MatchString(state.Key) || state.Console != "" {
		t.Fatalf("new state: %+v, %v", state, err)
	}
	state.Console = "192.168.1.20"
	state.Save()
	again, _ := loadState(path)
	if again.Key != state.Key || again.Console != "192.168.1.20" {
		t.Fatalf("reloaded: %+v", again)
	}
	// The PowerShell helper's file, with "console": null, still loads.
	os.WriteFile(path, []byte(`{"key":"00112233445566778899aabbccddeeff","console":null}`), 0o600)
	old, _ := loadState(path)
	if old.Key != "00112233445566778899aabbccddeeff" || old.Console != "" {
		t.Fatalf("old file: %+v", old)
	}
}

func TestMintSignInAndServe(t *testing.T) {
	atlas := &fakeAtlas{}
	service, state := testService(t, atlas.server(t), fakeLSX(t))
	id, err := service.Mint()
	if err != nil || id.UID != fakeUID || id.Token != atlas.latest() {
		t.Fatalf("Mint: %+v, %v", id, err)
	}

	console := &fakeConsole{}
	_, port := console.server(t)
	service.ConsolePort = port
	if !service.Hello("127.0.0.1") {
		t.Fatal("Hello did not find the console")
	}
	if err := service.SignIn("127.0.0.1", "1111", id); err == nil || !strings.Contains(err.Error(), "wrong code") {
		t.Fatalf("wrong code: %v", err)
	}
	if err := service.SignIn("127.0.0.1", "48-21", id); err != nil {
		t.Fatalf("right code: %v", err)
	}
	push := console.last()
	if push["playerToken"] != id.Token || push["uid"] != fakeUID || push["refreshKey"] != state.Key ||
		push["refreshUrl"] != service.LocalRefreshURL() {
		t.Fatalf("push: %v", push)
	}
	if err := service.SignIn("127.0.0.1", "", id); err != nil {
		t.Fatalf("paired, no code: %v", err)
	}

	if err := service.StartServing(false); err != nil {
		t.Fatal(err)
	}
	defer service.Stop()
	service.SetWriteIdentityOnRefresh(true)
	get := func(path, key string) (int, map[string]string) {
		request, _ := http.NewRequest("GET", "http://127.0.0.1:"+strconv.Itoa(service.Port)+path, nil)
		if key != "" {
			request.Header.Set("X-NorthstarPS4-Key", key)
		}
		response, err := http.DefaultClient.Do(request)
		if err != nil {
			t.Fatal(err)
		}
		defer response.Body.Close()
		var body map[string]string
		json.NewDecoder(response.Body).Decode(&body)
		return response.StatusCode, body
	}
	if status, body := get("/atlas/token", state.Key); status != 200 || body["playerToken"] != atlas.latest() || body["uid"] != fakeUID {
		t.Fatalf("served: %d %v", status, body)
	}
	file, _ := os.ReadFile(service.Output)
	if !strings.Contains(string(file), atlas.latest()) || !strings.Contains(string(file), service.LocalRefreshURL()) {
		t.Fatalf("identity file not updated: %s", file)
	}
	if status, _ := get("/atlas/token", strings.Repeat("0", 32)); status != 403 {
		t.Fatalf("wrong key: %d", status)
	}
	if status, _ := get("/other", state.Key); status != 404 {
		t.Fatalf("other path: %d", status)
	}
	for _, event := range service.EventsSince(0) {
		if strings.Contains(event.Text, id.Token) || strings.Contains(event.Text, state.Key) {
			t.Fatalf("a secret in the activity: %q", event.Text)
		}
	}
}

func TestMintReportsTheEAApp(t *testing.T) {
	atlas := &fakeAtlas{}
	service, _ := testService(t, atlas.server(t), freePort(t))
	if _, err := service.Mint(); err == nil || !strings.Contains(err.Error(), "Could not reach the EA app") {
		t.Fatalf("EA app down: %v", err)
	}
}

func newTestUI(t *testing.T) (*ui, *httptest.Server, *fakeAtlas) {
	atlas := &fakeAtlas{}
	service, state := testService(t, atlas.server(t), fakeLSX(t))
	u := &ui{options: &Options{Output: service.Output}, state: state, service: service, session: "s3ss10n", quit: make(chan struct{})}
	u.view.Mode = "local"
	server := httptest.NewServer(u.routes())
	t.Cleanup(server.Close)
	u.host = server.Listener.Addr().String()
	return u, server, atlas
}

func TestUIRefusesOtherSites(t *testing.T) {
	u, server, _ := newTestUI(t)
	call := func(method, path, host, session, contentType string) int {
		request, _ := http.NewRequest(method, server.URL+path, strings.NewReader("{}"))
		if host != "" {
			request.Host = host
		}
		if session != "" {
			request.Header.Set("X-Session", session)
		}
		if contentType != "" {
			request.Header.Set("Content-Type", contentType)
		}
		response, err := http.DefaultClient.Do(request)
		if err != nil {
			t.Fatal(err)
		}
		response.Body.Close()
		return response.StatusCode
	}
	checks := []struct {
		name                            string
		method, path, host, session, ct string
		want                            int
	}{
		{"page", "GET", "/", "", "", "", 200},
		{"page, other host", "GET", "/", "evil.example:80", "", "", 404},
		{"state", "GET", "/api/state", "", u.session, "", 200},
		{"state, no session", "GET", "/api/state", "", "", "", 403},
		{"state, wrong session", "GET", "/api/state", "", "nope", "", 403},
		{"state, rebound host", "GET", "/api/state", "evil.example:80", u.session, "", 403},
		{"sign-in as a form", "POST", "/api/signin", "", u.session, "text/plain", 403},
		{"sign-in by GET", "GET", "/api/signin", "", u.session, "", 403},
		{"sign-in", "POST", "/api/signin", "", u.session, "application/json", 202},
	}
	for _, c := range checks {
		if got := call(c.method, c.path, c.host, c.session, c.ct); got != c.want {
			t.Errorf("%s: %d, want %d", c.name, got, c.want)
		}
	}
}

func waitFor(t *testing.T, what string, done func() bool) {
	deadline := time.Now().Add(10 * time.Second)
	for !done() {
		if time.Now().After(deadline) {
			t.Fatalf("timed out waiting for %s", what)
		}
		time.Sleep(20 * time.Millisecond)
	}
}

func TestUISignsInAConsole(t *testing.T) {
	u, _, atlas := newTestUI(t)
	view := func() uiView { u.mu.Lock(); defer u.mu.Unlock(); return u.view }
	u.start()
	if v := view(); !v.Ready || v.EA.Kind != "ok" || !strings.Contains(v.Result.Text, "Choose where") {
		t.Fatalf("after start: %+v", v)
	}

	console := &fakeConsole{}
	_, port := console.server(t)
	u.service.ConsolePort = port
	u.signIn("remote", "127.0.0.1", "12")
	if v := view(); v.Result.Kind != "error" || !strings.Contains(v.Result.Text, "4-digit") {
		t.Fatalf("short code: %+v", v.Result)
	}
	u.signIn("remote", "127.0.0.1", "1111")
	if v := view(); v.Result.Kind != "error" || v.Result.Text != "Wrong code; type the code shown on the PS4." {
		t.Fatalf("wrong code: %+v", v.Result)
	}
	u.signIn("remote", "127.0.0.1", consoleCode)
	if v := view(); v.Result.Kind != "ok" || !strings.Contains(v.Result.Text, "Signed in Northstar on 127.0.0.1") {
		t.Fatalf("right code: %+v", v.Result)
	}
	defer u.service.Stop()
	if console.last()["playerToken"] != atlas.latest() || u.state.Console != "127.0.0.1" {
		t.Fatalf("pushed %v, remembered %q", console.last(), u.state.Console)
	}
	if events := u.service.EventsSince(0); len(events) == 0 || !strings.Contains(events[len(events)-1].Text, "signed in Northstar on 127.0.0.1") {
		t.Fatalf("activity: %v", events)
	}
}

func TestRunPrintsHelpAndRefusesBadOptions(t *testing.T) {
	var out, errs bytes.Buffer
	if code := run([]string{"--help"}, strings.NewReader(""), &out, &errs); code != 0 || !strings.Contains(out.String(), "--console") {
		t.Fatalf("--help: %d %q", code, out.String())
	}
	out.Reset()
	if code := run([]string{"--bogus"}, strings.NewReader(""), &out, &errs); code != 2 || !strings.Contains(errs.String(), "Unknown option") {
		t.Fatalf("--bogus: %d %q", code, errs.String())
	}
}

var _ = io.Discard
