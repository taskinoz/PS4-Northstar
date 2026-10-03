package main

import (
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

// Service is everything the helper does over the network, for the browser UI
// and the terminal alike: getting an Atlas token through the EA app, finding
// and signing in the game, and serving new tokens. Errors are worded for the
// player. Neither the EA code, a token nor the key is ever put in a message.
//
// The game side is launcher/src/runtime_signin.inl (sign-in listener, port
// 37012) and RefreshAtlasToken in runtime_server_join.inl (asks /atlas/token).
type Service struct {
	LsxPort                        int
	ContentID, Title, ClientID     string
	Scope, MasterServer, UserAgent string
	Port, ConsolePort              int
	MinSecondsBetweenTokens        int
	Output, AdvertiseHost, Key     string
	writeIdentityOnRefresh         atomic.Bool
	OnNote                         func(string) // called for each activity line

	mintMu     sync.Mutex
	last       Identity
	lastMinted time.Time

	eventsMu sync.Mutex
	events   []Event
	seq      int

	serveMu    sync.Mutex
	server     *http.Server
	servingAll bool
}

// Identity is an Atlas account id and player token.
type Identity struct {
	UID, Token string
}

// Event is one line of activity.
type Event struct {
	Seq  int    `json:"seq"`
	Text string `json:"text"`
}

var (
	hex32       = regexp.MustCompile(`^[0-9a-f]{32}$`)
	addressText = regexp.MustCompile(`^[A-Za-z0-9.\-]+$`)
)

func isAddress(address string) bool { return len(address) <= 253 && addressText.MatchString(address) }

func isLoopback(address string) bool {
	return address == "localhost" || strings.HasPrefix(address, "127.")
}

// Note records an activity line.
func (s *Service) Note(text string) {
	s.eventsMu.Lock()
	s.seq++
	event := Event{s.seq, time.Now().Format("15:04:05") + "  " + text}
	s.events = append(s.events, event)
	if len(s.events) > 300 {
		s.events = s.events[len(s.events)-300:]
	}
	callback := s.OnNote
	s.eventsMu.Unlock()
	if callback != nil {
		callback(event.Text)
	}
}

// EventsSince returns the activity lines after seq.
func (s *Service) EventsSince(seq int) []Event {
	s.eventsMu.Lock()
	defer s.eventsMu.Unlock()
	out := []Event{}
	for _, e := range s.events {
		if e.Seq > seq {
			out = append(out, e)
		}
	}
	return out
}

var (
	northstarClient = &http.Client{Timeout: 20 * time.Second}
	// The game is on this network: no proxy.
	consoleClient = &http.Client{Transport: &http.Transport{Proxy: nil}}
)

// Mint exchanges an EA code for an Atlas token. A token minted in the last
// MinSecondsBetweenTokens is reused, so a burst of requests mints one.
func (s *Service) Mint() (Identity, error) {
	s.mintMu.Lock()
	defer s.mintMu.Unlock()
	if s.last.Token != "" && time.Since(s.lastMinted) < time.Duration(s.MinSecondsBetweenTokens)*time.Second {
		return s.last, nil
	}
	uid, code, err := getAuthCode(s.LsxPort, s.ContentID, s.Title, s.ClientID, s.Scope)
	if err != nil {
		return Identity{}, err
	}
	address := strings.TrimRight(s.MasterServer, "/") + "/client/origin_auth?id=" + url.QueryEscape(uid) +
		"&token=" + url.QueryEscape(code)
	request, err := http.NewRequest("GET", address, nil)
	if err != nil {
		return Identity{}, fmt.Errorf("the master server address %q is not valid", s.MasterServer)
	}
	request.Header.Set("User-Agent", s.UserAgent)
	response, err := northstarClient.Do(request)
	if err != nil {
		return Identity{}, fmt.Errorf("Could not reach Northstar (%s). Check the internet connection and try again.", s.MasterServer)
	}
	defer response.Body.Close()
	body, _ := io.ReadAll(io.LimitReader(response.Body, 64<<10))
	var reply struct {
		Success bool   `json:"success"`
		Token   string `json:"token"`
		Error   struct {
			Msg string `json:"msg"`
		} `json:"error"`
	}
	json.Unmarshal(body, &reply) // partial replies still say what they can
	if response.StatusCode != http.StatusOK || !reply.Success || !hex32.MatchString(reply.Token) {
		if reply.Error.Msg != "" {
			return Identity{}, fmt.Errorf("Northstar refused the EA sign-in: %s.", reply.Error.Msg)
		}
		return Identity{}, fmt.Errorf("Northstar refused the EA sign-in (status %d).", response.StatusCode)
	}
	s.last = Identity{uid, reply.Token}
	s.lastMinted = time.Now()
	return s.last, nil
}

func (s *Service) consoleURL(address, path string) string {
	return "http://" + net.JoinHostPort(address, strconv.Itoa(s.ConsolePort)) + path
}

// Hello reports whether Northstar is running at that address and listening for a sign-in.
func (s *Service) Hello(address string) bool {
	if !isAddress(address) {
		return false
	}
	client := *consoleClient
	client.Timeout = 3 * time.Second
	response, err := client.Get(s.consoleURL(address, "/northstar/hello"))
	if err != nil {
		return false
	}
	defer response.Body.Close()
	var hello struct {
		App string `json:"app"`
	}
	body, _ := io.ReadAll(io.LimitReader(response.Body, 4096))
	return response.StatusCode == http.StatusOK && json.Unmarshal(body, &hello) == nil && hello.App == "NorthstarPS4"
}

// AddressTowards is this computer's address as the console sees it: the local
// end of a route to it.
func (s *Service) AddressTowards(address string) string {
	if s.AdvertiseHost != "" {
		return s.AdvertiseHost
	}
	if isLoopback(address) {
		return "127.0.0.1"
	}
	conn, err := net.Dial("udp", net.JoinHostPort(address, strconv.Itoa(s.ConsolePort)))
	if err != nil {
		return "127.0.0.1"
	}
	defer conn.Close()
	return conn.LocalAddr().(*net.UDPAddr).IP.String()
}

// LocalRefreshURL is where shadPS4 on this computer asks for new tokens.
func (s *Service) LocalRefreshURL() string {
	return "http://127.0.0.1:" + strconv.Itoa(s.Port) + "/atlas/token"
}

// SignIn hands an identity to the game. The error, if any, says why not, in
// lower case, as the game words it.
func (s *Service) SignIn(address, code string, id Identity) error {
	if !isAddress(address) {
		return errors.New("that is not an address")
	}
	digits := strings.Map(func(r rune) rune {
		if r >= '0' && r <= '9' {
			return r
		}
		return -1
	}, code)
	body, _ := json.Marshal(map[string]string{
		"uid":         id.UID,
		"playerToken": id.Token,
		"refreshUrl":  "http://" + net.JoinHostPort(s.AddressTowards(address), strconv.Itoa(s.Port)) + "/atlas/token",
		"refreshKey":  s.Key,
		"code":        digits,
	})
	client := *consoleClient
	client.Timeout = 10 * time.Second
	response, err := client.Post(s.consoleURL(address, "/northstar/signin"), "application/json", strings.NewReader(string(body)))
	if err != nil {
		return fmt.Errorf("Northstar could not be reached at %s. Check the address, and that the game is running", address)
	}
	defer response.Body.Close()
	var reply struct {
		OK    bool   `json:"ok"`
		Error string `json:"error"`
	}
	data, _ := io.ReadAll(io.LimitReader(response.Body, 4096))
	json.Unmarshal(data, &reply)
	if response.StatusCode == http.StatusOK && reply.OK {
		return nil
	}
	if reply.Error != "" {
		return errors.New(reply.Error)
	}
	return fmt.Errorf("the game did not accept the sign-in (status %d)", response.StatusCode)
}

// WriteIdentity writes atlas_identity.json for shadPS4 on this computer, in one step.
func (s *Service) WriteIdentity(id Identity, refreshURL string) error {
	data, _ := json.MarshalIndent(struct {
		UID         string `json:"uid"`
		PlayerToken string `json:"playerToken"`
		RefreshURL  string `json:"refreshUrl"`
		RefreshKey  string `json:"refreshKey"`
	}{id.UID, id.Token, refreshURL, s.Key}, "", "  ")
	if err := os.MkdirAll(filepath.Dir(s.Output), 0o755); err != nil {
		return err
	}
	temp := s.Output + ".tmp"
	if err := os.WriteFile(temp, append(data, '\n'), 0o600); err != nil {
		return err
	}
	return os.Rename(temp, s.Output)
}

// SetWriteIdentityOnRefresh keeps atlas_identity.json current as tokens are
// served, for shadPS4 on this computer.
func (s *Service) SetWriteIdentityOnRefresh(on bool) { s.writeIdentityOnRefresh.Store(on) }

// StartServing serves new tokens at /atlas/token: on every interface for a
// console elsewhere, on loopback for shadPS4 here.
func (s *Service) StartServing(allInterfaces bool) error {
	s.serveMu.Lock()
	defer s.serveMu.Unlock()
	if s.server != nil && s.servingAll == allInterfaces {
		return nil
	}
	s.stopLocked()
	host := "127.0.0.1"
	if allInterfaces {
		host = ""
	}
	listener, err := net.Listen("tcp", net.JoinHostPort(host, strconv.Itoa(s.Port)))
	if err != nil {
		return fmt.Errorf("port %d is in use, so the game cannot ask for new tokens. Is the token helper already running?", s.Port)
	}
	server := &http.Server{Handler: http.HandlerFunc(s.serveToken), ReadHeaderTimeout: 5 * time.Second}
	go server.Serve(listener)
	s.server = server
	s.servingAll = allInterfaces
	return nil
}

// Stop stops serving tokens.
func (s *Service) Stop() {
	s.serveMu.Lock()
	defer s.serveMu.Unlock()
	s.stopLocked()
}

func (s *Service) stopLocked() {
	if s.server != nil {
		s.server.Close()
		s.server = nil
	}
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	data, _ := json.Marshal(value)
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Content-Length", strconv.Itoa(len(data)))
	w.WriteHeader(status)
	w.Write(data)
}

func (s *Service) serveToken(w http.ResponseWriter, r *http.Request) {
	peer, _, _ := net.SplitHostPort(r.RemoteAddr)
	if r.Method != http.MethodGet || r.URL.Path != "/atlas/token" {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "not found"})
		return
	}
	key := r.Header.Get("X-NorthstarPS4-Key")
	if subtle.ConstantTimeCompare([]byte(key), []byte(s.Key)) != 1 || s.Key == "" {
		s.Note("refused a request from " + peer + " (not paired with this helper)")
		writeJSON(w, http.StatusForbidden, map[string]string{"error": "this console is not paired with the token helper"})
		return
	}
	id, err := s.Mint()
	if err != nil {
		s.Note("could not get a token for " + peer + ": " + err.Error())
		writeJSON(w, http.StatusBadGateway, map[string]string{"error": err.Error()})
		return
	}
	if s.writeIdentityOnRefresh.Load() {
		if err := s.WriteIdentity(id, s.LocalRefreshURL()); err != nil {
			s.Note("could not update " + s.Output + ": " + err.Error())
		}
	}
	s.Note("gave " + peer + " a new token for account " + id.UID)
	writeJSON(w, http.StatusOK, map[string]string{"uid": id.UID, "playerToken": id.Token})
}
