package main

import (
	"bufio"
	"crypto/aes"
	"encoding/hex"
	"encoding/xml"
	"errors"
	"fmt"
	"net"
	"strconv"
	"strings"
	"time"
)

// The EA app's local SDK protocol (LSX), as the Origin SDK speaks it (see the
// MIT-licensed ploxxxy/origin-sdk). Messages are XML, NUL-terminated, on
// 127.0.0.1:3216. The server opens with a Challenge; the client encrypts the
// challenge key with AES-128-ECB/PKCS7 under the default key (0..15),
// hex-encodes it, and derives the session key from the first two characters
// of that hex string. Every request after ChallengeAccepted is encrypted with
// the session key and hex-encoded, and so is every reply.

// lsxKey is MSVC rand() seeded as the Origin SDK does; seed 0 is the default key.
func lsxKey(seed uint32) []byte {
	key := make([]byte, 16)
	if seed == 0 {
		for i := range key {
			key[i] = byte(i)
		}
		return key
	}
	state := uint32(7)
	next := func() uint32 {
		state = state*214013 + 2531011
		return (state >> 16) & 0x7fff
	}
	state = next() + seed
	for i := range key {
		key[i] = byte(next())
	}
	return key
}

func encryptHex(key []byte, text string) string {
	block, err := aes.NewCipher(key)
	if err != nil {
		panic(err) // keys are always 16 bytes
	}
	data := []byte(text)
	pad := 16 - len(data)%16
	for i := 0; i < pad; i++ {
		data = append(data, byte(pad))
	}
	out := make([]byte, len(data))
	for i := 0; i < len(data); i += 16 {
		block.Encrypt(out[i:i+16], data[i:i+16])
	}
	return hex.EncodeToString(out)
}

func decryptHex(key []byte, text string) (string, error) {
	data, err := hex.DecodeString(strings.TrimSpace(text))
	if err != nil || len(data) == 0 || len(data)%16 != 0 {
		return "", errors.New("not an encrypted LSX message")
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return "", err
	}
	out := make([]byte, len(data))
	for i := 0; i < len(data); i += 16 {
		block.Decrypt(out[i:i+16], data[i:i+16])
	}
	pad := int(out[len(out)-1])
	if pad < 1 || pad > 16 {
		return "", errors.New("bad padding")
	}
	for _, b := range out[len(out)-pad:] {
		if int(b) != pad {
			return "", errors.New("bad padding")
		}
	}
	return string(out[:len(out)-pad]), nil
}

// findElement returns the attributes of the first element whose path from
// the root ends with suffix.
func findElement(doc string, suffix ...string) (map[string]string, bool) {
	decoder := xml.NewDecoder(strings.NewReader(doc))
	var stack []string
	for {
		token, err := decoder.Token()
		if err != nil {
			return nil, false
		}
		switch t := token.(type) {
		case xml.StartElement:
			stack = append(stack, t.Name.Local)
			if len(stack) >= len(suffix) {
				match := true
				for i := range suffix {
					if stack[len(stack)-len(suffix)+i] != suffix[i] {
						match = false
						break
					}
				}
				if match {
					attrs := map[string]string{}
					for _, a := range t.Attr {
						attrs[a.Name.Local] = a.Value
					}
					return attrs, true
				}
			}
		case xml.EndElement:
			if len(stack) > 0 {
				stack = stack[:len(stack)-1]
			}
		}
	}
}

func xmlEscape(text string) string {
	var b strings.Builder
	xml.EscapeText(&b, []byte(text))
	return b.String()
}

// eaError is a failure talking to the EA app, worded for the player.
type eaError struct{ message string }

func (e *eaError) Error() string { return e.message }

func eaFailure(err error) error {
	var netErr net.Error
	if errors.As(err, &netErr) && netErr.Timeout() {
		return &eaError{"The EA app did not answer. Make sure it is open and signed in, and try again."}
	}
	return &eaError{"The EA app closed the connection. Make sure it is open and signed in, and try again."}
}

// getAuthCode asks the EA app for an authorization code for clientID, as the
// game does. Returns the account's user id and the code.
func getAuthCode(port int, contentID, title, clientID, scope string) (string, string, error) {
	conn, err := net.DialTimeout("tcp", net.JoinHostPort("127.0.0.1", strconv.Itoa(port)), 5*time.Second)
	if err != nil {
		return "", "", &eaError{"Could not reach the EA app. Open the EA app, sign in, and try again."}
	}
	defer conn.Close()
	reader := bufio.NewReader(conn)
	read := func() (string, error) {
		conn.SetReadDeadline(time.Now().Add(30 * time.Second))
		message, err := reader.ReadString(0)
		if err != nil {
			return "", eaFailure(err)
		}
		return strings.TrimSuffix(message, "\x00"), nil
	}
	write := func(message string) error {
		conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
		if _, err := conn.Write(append([]byte(message), 0)); err != nil {
			return eaFailure(err)
		}
		return nil
	}

	key := lsxKey(0)
	challenge := ""
	for challenge == "" {
		message, err := read()
		if err != nil {
			return "", "", err
		}
		if attrs, ok := findElement(message, "LSX", "Event", "Challenge"); ok {
			challenge = attrs["key"]
		}
	}
	response := encryptHex(key, challenge)
	key = lsxKey(uint32(response[0])<<8 | uint32(response[1]))
	err = write(`<LSX><Request recipient="EALS" id="0"><ChallengeResponse response="` + response + `" key="` +
		xmlEscape(challenge) + `" version="3"><ContentId>` + xmlEscape(contentID) + `</ContentId><Title>` + xmlEscape(title) +
		`</Title><MultiplayerId>` + xmlEscape(contentID) + `</MultiplayerId><Language>en_US</Language>` +
		`<Version>10.6.1.8</Version></ChallengeResponse></Request></LSX>`)
	if err != nil {
		return "", "", err
	}
	for {
		message, err := read()
		if err != nil {
			return "", "", err
		}
		if _, ok := findElement(message, "LSX", "Response", "ChallengeAccepted"); ok {
			break
		}
		if _, ok := findElement(message, "LSX", "Response"); ok {
			return "", "", &eaError{"The EA app refused the connection. Make sure it is up to date, and try again."}
		}
	}

	request := func(id int, body string) (string, error) {
		if err := write(encryptHex(key, `<LSX><Request recipient="EbisuSDK" id="`+strconv.Itoa(id)+`">`+body+`</Request></LSX>`)); err != nil {
			return "", err
		}
		for {
			raw, err := read()
			if err != nil {
				return "", err
			}
			plain, err := decryptHex(key, raw)
			if err != nil {
				return "", &eaError{"The EA app sent a reply this helper does not understand."}
			}
			response, ok := findElement(plain, "LSX", "Response")
			if !ok || response["id"] != strconv.Itoa(id) {
				continue // events, other replies
			}
			if e, ok := findElement(plain, "ErrorSuccess"); ok && e["Code"] != "" && e["Code"] != "0" {
				return "", &eaError{fmt.Sprintf("The EA app reported: %s (%s).", e["Description"], e["Code"])}
			}
			return plain, nil
		}
	}

	profile, err := request(1, `<GetProfile index="0"/>`)
	if err != nil {
		return "", "", err
	}
	user, ok := findElement(profile, "GetProfileResponse")
	if !ok || user["UserId"] == "" || user["UserId"] == "0" {
		return "", "", &eaError{"The EA app is not signed in. Sign in, and try again."}
	}
	userID := user["UserId"]
	reply, err := request(2, `<GetAuthCode UserId="`+xmlEscape(userID)+`" ClientId="`+xmlEscape(clientID)+`" Scope="`+
		xmlEscape(scope)+`" AppendAuthSource="false"/>`)
	if err != nil {
		return "", "", err
	}
	code, ok := findElement(reply, "AuthCode")
	if !ok || code["value"] == "" {
		return "", "", &eaError{"The EA app returned no sign-in code. Try again."}
	}
	return userID, code["value"], nil
}
