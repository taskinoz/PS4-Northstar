//! The EA app's local SDK protocol (LSX), as the Origin SDK speaks it (see the
//! MIT-licensed ploxxxy/origin-sdk). Messages are XML, NUL-terminated, on
//! 127.0.0.1:3216. The server opens with a Challenge; the client encrypts the
//! challenge key with AES-128-ECB/PKCS7 under the default key (0..15),
//! hex-encodes it, and derives the session key from the first two characters
//! of that hex string. Every request after ChallengeAccepted is encrypted with
//! the session key and hex-encoded, and so is every reply.

use aes::cipher::{generic_array::GenericArray, BlockDecrypt, BlockEncrypt, KeyInit};
use aes::Aes128;
use std::collections::HashMap;
use std::io::{BufRead, BufReader, ErrorKind, Write};
use std::net::{SocketAddr, TcpStream};
use std::time::Duration;

/// MSVC rand() seeded as the Origin SDK does; seed 0 is the default key.
pub fn key(seed: u32) -> [u8; 16] {
    let mut key = [0u8; 16];
    if seed == 0 {
        for (i, byte) in key.iter_mut().enumerate() {
            *byte = i as u8;
        }
        return key;
    }
    let mut state: u32 = 7;
    let next = |state: &mut u32| {
        *state = state.wrapping_mul(214013).wrapping_add(2531011);
        (*state >> 16) & 0x7fff
    };
    state = next(&mut state).wrapping_add(seed);
    for byte in key.iter_mut() {
        *byte = next(&mut state) as u8;
    }
    key
}

pub fn encrypt_hex(key: &[u8; 16], text: &str) -> String {
    let cipher = Aes128::new(GenericArray::from_slice(key));
    let mut data = text.as_bytes().to_vec();
    let pad = 16 - data.len() % 16;
    data.extend(std::iter::repeat(pad as u8).take(pad));
    for block in data.chunks_mut(16) {
        cipher.encrypt_block(GenericArray::from_mut_slice(block));
    }
    hex::encode(data)
}

pub fn decrypt_hex(key: &[u8; 16], text: &str) -> Option<String> {
    let mut data = hex::decode(text.trim()).ok()?;
    if data.is_empty() || data.len() % 16 != 0 {
        return None;
    }
    let cipher = Aes128::new(GenericArray::from_slice(key));
    for block in data.chunks_mut(16) {
        cipher.decrypt_block(GenericArray::from_mut_slice(block));
    }
    let pad = *data.last()? as usize;
    if pad == 0 || pad > 16 || data[data.len() - pad..].iter().any(|&b| b as usize != pad) {
        return None;
    }
    data.truncate(data.len() - pad);
    String::from_utf8(data).ok()
}

/// The attributes of the first element whose path from the root ends with `suffix`.
pub fn find_element(doc: &str, suffix: &[&str]) -> Option<HashMap<String, String>> {
    let document = roxmltree::Document::parse(doc).ok()?;
    for node in document.descendants().filter(|n| n.is_element()) {
        let mut path: Vec<&str> = node.ancestors().filter(|n| n.is_element()).map(|n| n.tag_name().name()).collect();
        path.reverse();
        if path.len() >= suffix.len() && path[path.len() - suffix.len()..] == *suffix {
            return Some(node.attributes().map(|a| (a.name().to_string(), a.value().to_string())).collect());
        }
    }
    None
}

pub fn xml_escape(text: &str) -> String {
    text.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;").replace('\'', "&apos;")
}

fn failure(error: std::io::Error) -> String {
    match error.kind() {
        ErrorKind::TimedOut | ErrorKind::WouldBlock => {
            "The EA app did not answer. Make sure it is open and signed in, and try again.".into()
        }
        _ => "The EA app closed the connection. Make sure it is open and signed in, and try again.".into(),
    }
}

struct Connection {
    reader: BufReader<TcpStream>,
    writer: TcpStream,
}

impl Connection {
    fn read(&mut self) -> Result<String, String> {
        let mut message = Vec::new();
        match self.reader.read_until(0, &mut message) {
            Ok(0) => Err(failure(ErrorKind::UnexpectedEof.into())),
            Ok(_) => {
                if message.last() == Some(&0) {
                    message.pop();
                }
                Ok(String::from_utf8_lossy(&message).into_owned())
            }
            Err(error) => Err(failure(error)),
        }
    }

    fn write(&mut self, message: &str) -> Result<(), String> {
        let mut bytes = message.as_bytes().to_vec();
        bytes.push(0);
        self.writer.write_all(&bytes).map_err(failure)
    }
}

/// Asks the EA app for an authorization code for `client_id`, as the game
/// does. Returns the account's user id and the code; errors are worded for
/// the player.
pub fn get_auth_code(port: u16, content_id: &str, title: &str, client_id: &str, scope: &str) -> Result<(String, String), String> {
    let address = SocketAddr::from(([127, 0, 0, 1], port));
    let stream = TcpStream::connect_timeout(&address, Duration::from_secs(5))
        .map_err(|_| "Could not reach the EA app. Open the EA app, sign in, and try again.".to_string())?;
    stream.set_read_timeout(Some(Duration::from_secs(30))).ok();
    stream.set_write_timeout(Some(Duration::from_secs(10))).ok();
    let writer = stream.try_clone().map_err(failure)?;
    let mut conn = Connection { reader: BufReader::new(stream), writer };

    let mut session_key = key(0);
    let mut challenge = String::new();
    while challenge.is_empty() {
        let message = conn.read()?;
        if let Some(attrs) = find_element(&message, &["LSX", "Event", "Challenge"]) {
            challenge = attrs.get("key").cloned().unwrap_or_default();
        }
    }
    let response = encrypt_hex(&session_key, &challenge);
    let bytes = response.as_bytes();
    session_key = key((bytes[0] as u32) << 8 | bytes[1] as u32);
    conn.write(&format!(
        "<LSX><Request recipient=\"EALS\" id=\"0\"><ChallengeResponse response=\"{response}\" key=\"{}\" version=\"3\">\
         <ContentId>{}</ContentId><Title>{}</Title><MultiplayerId>{}</MultiplayerId><Language>en_US</Language>\
         <Version>10.6.1.8</Version></ChallengeResponse></Request></LSX>",
        xml_escape(&challenge),
        xml_escape(content_id),
        xml_escape(title),
        xml_escape(content_id)
    ))?;
    loop {
        let message = conn.read()?;
        if find_element(&message, &["LSX", "Response", "ChallengeAccepted"]).is_some() {
            break;
        }
        if find_element(&message, &["LSX", "Response"]).is_some() {
            return Err("The EA app refused the connection. Make sure it is up to date, and try again.".into());
        }
    }

    let mut request = |id: u32, body: &str| -> Result<String, String> {
        conn.write(&encrypt_hex(&session_key, &format!("<LSX><Request recipient=\"EbisuSDK\" id=\"{id}\">{body}</Request></LSX>")))?;
        loop {
            let raw = conn.read()?;
            let plain = decrypt_hex(&session_key, &raw)
                .ok_or_else(|| "The EA app sent a reply this helper does not understand.".to_string())?;
            match find_element(&plain, &["LSX", "Response"]) {
                Some(response) if response.get("id").map(String::as_str) == Some(&id.to_string()) => {}
                _ => continue, // events, other replies
            }
            if let Some(error) = find_element(&plain, &["ErrorSuccess"]) {
                let code = error.get("Code").cloned().unwrap_or_default();
                if !code.is_empty() && code != "0" {
                    return Err(format!(
                        "The EA app reported: {} ({code}).",
                        error.get("Description").cloned().unwrap_or_default()
                    ));
                }
            }
            return Ok(plain);
        }
    };

    let profile = request(1, "<GetProfile index=\"0\"/>")?;
    let user_id = find_element(&profile, &["GetProfileResponse"])
        .and_then(|attrs| attrs.get("UserId").cloned())
        .filter(|id| !id.is_empty() && id != "0")
        .ok_or_else(|| "The EA app is not signed in. Sign in, and try again.".to_string())?;
    let reply = request(
        2,
        &format!(
            "<GetAuthCode UserId=\"{}\" ClientId=\"{}\" Scope=\"{}\" AppendAuthSource=\"false\"/>",
            xml_escape(&user_id),
            xml_escape(client_id),
            xml_escape(scope)
        ),
    )?;
    let code = find_element(&reply, &["AuthCode"])
        .and_then(|attrs| attrs.get("value").cloned())
        .filter(|code| !code.is_empty())
        .ok_or_else(|| "The EA app returned no sign-in code. Try again.".to_string())?;
    Ok((user_id, code))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn key_matches_origin_sdk() {
        assert_eq!(key(1337), [251, 135, 22, 197, 214, 181, 148, 115, 149, 93, 40, 78, 123, 141, 60, 108]);
        assert_eq!(key(0)[15], 15);
    }

    #[test]
    fn encryption_round_trips() {
        let k = key(4242);
        for text in ["", "x", "exactly sixteen!", &"<LSX/>".repeat(40)] {
            assert_eq!(decrypt_hex(&k, &encrypt_hex(&k, text)).as_deref(), Some(text));
        }
        assert_eq!(decrypt_hex(&k, "abcd"), None);
    }

    #[test]
    fn finds_elements_by_path() {
        let doc = r#"<LSX><Response id="2"><AuthCode value="v"/></Response></LSX>"#;
        assert_eq!(find_element(doc, &["LSX", "Response"]).unwrap()["id"], "2");
        assert_eq!(find_element(doc, &["AuthCode"]).unwrap()["value"], "v");
        assert!(find_element(doc, &["LSX", "Event"]).is_none());
        assert!(find_element("not xml", &["LSX"]).is_none());
    }
}
