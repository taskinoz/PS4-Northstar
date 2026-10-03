using System;
using System.IO;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Xml;

namespace NorthstarPS4.TokenHelper {

// The EA app's local SDK protocol (LSX), as the Origin SDK speaks it (see the
// MIT-licensed ploxxxy/origin-sdk). Messages are XML, NUL-terminated, on
// 127.0.0.1:3216. The server opens with a Challenge; the client encrypts the
// challenge key with AES-128-ECB/PKCS7 under the default key (0..15),
// hex-encodes it, and derives the session key from the first two characters
// of that hex string. Every request after ChallengeAccepted is encrypted with
// the session key and hex-encoded, and so is every reply.
public static class Lsx {
    static uint Next(ref uint state) { state = state * 214013u + 2531011u; return (state >> 16) & 0x7fffu; }

    // MSVC rand() seeded as the Origin SDK does; seed 0 is the default key.
    public static byte[] Key(uint seed) {
        var key = new byte[16];
        if (seed == 0) { for (int i = 0; i < 16; i++) key[i] = (byte)i; return key; }
        uint state = 7;
        state = Next(ref state) + seed;
        for (int i = 0; i < 16; i++) key[i] = (byte)Next(ref state);
        return key;
    }

    static ICryptoTransform Transform(byte[] key, bool encrypt) {
        var aes = Aes.Create();
        aes.Mode = CipherMode.ECB;
        aes.Padding = PaddingMode.PKCS7;
        aes.Key = key;
        return encrypt ? aes.CreateEncryptor() : aes.CreateDecryptor();
    }

    public static string EncryptHex(byte[] key, string text) {
        var plain = Encoding.UTF8.GetBytes(text);
        var cipher = Transform(key, true).TransformFinalBlock(plain, 0, plain.Length);
        var sb = new StringBuilder(cipher.Length * 2);
        foreach (var b in cipher) sb.Append(b.ToString("x2"));
        return sb.ToString();
    }

    public static string DecryptHex(byte[] key, string hex) {
        var cipher = new byte[hex.Length / 2];
        for (int i = 0; i < cipher.Length; i++) cipher[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
        var plain = Transform(key, false).TransformFinalBlock(cipher, 0, cipher.Length);
        return Encoding.UTF8.GetString(plain);
    }

    static string ReadMessage(Stream stream) {
        var bytes = new MemoryStream();
        for (;;) {
            int b = stream.ReadByte();
            if (b < 0) throw new IOException("The EA app closed the connection");
            if (b == 0) break;
            bytes.WriteByte((byte)b);
        }
        return Encoding.UTF8.GetString(bytes.ToArray());
    }

    static void WriteMessage(Stream stream, string text) {
        var bytes = Encoding.UTF8.GetBytes(text);
        stream.Write(bytes, 0, bytes.Length);
        stream.WriteByte(0);
        stream.Flush();
    }

    static string Escape(string text) { return System.Security.SecurityElement.Escape(text); }

    // Returns { userId, authCode }.
    public static string[] GetAuthCode(int port, string contentId, string title, string clientId, string scope) {
        using (var client = new TcpClient()) {
            client.ReceiveTimeout = 30000;
            client.SendTimeout = 10000;
            client.Connect("127.0.0.1", port);
            var stream = client.GetStream();
            var key = Key(0);

            string challenge = null;
            while (challenge == null) {
                var doc = new XmlDocument();
                doc.LoadXml(ReadMessage(stream));
                var node = doc.SelectSingleNode("/LSX/Event/Challenge") as XmlElement;
                if (node != null) challenge = node.GetAttribute("key");
            }
            string response = EncryptHex(key, challenge);
            key = Key(((uint)response[0] << 8) | (uint)response[1]);
            WriteMessage(stream, "<LSX><Request recipient=\"EALS\" id=\"0\"><ChallengeResponse response=\"" + response +
                "\" key=\"" + Escape(challenge) + "\" version=\"3\"><ContentId>" + Escape(contentId) + "</ContentId><Title>" +
                Escape(title) + "</Title><MultiplayerId>" + Escape(contentId) + "</MultiplayerId><Language>en_US</Language>" +
                "<Version>10.6.1.8</Version></ChallengeResponse></Request></LSX>");
            for (;;) {
                var doc = new XmlDocument();
                doc.LoadXml(ReadMessage(stream));
                if (doc.SelectSingleNode("/LSX/Response/ChallengeAccepted") != null) break;
                if (doc.SelectSingleNode("/LSX/Response") != null) throw new IOException("The EA app refused the SDK handshake");
            }

            var profile = Request(stream, key, 1, "<GetProfile index=\"0\"/>");
            var user = profile.SelectSingleNode("//GetProfileResponse") as XmlElement;
            if (user == null || user.GetAttribute("UserId") == "" || user.GetAttribute("UserId") == "0")
                throw new IOException("The EA app is not signed in");
            string userId = user.GetAttribute("UserId");

            var reply = Request(stream, key, 2, "<GetAuthCode UserId=\"" + Escape(userId) + "\" ClientId=\"" +
                Escape(clientId) + "\" Scope=\"" + Escape(scope) + "\" AppendAuthSource=\"false\"/>");
            var code = reply.SelectSingleNode("//AuthCode") as XmlElement;
            if (code == null || code.GetAttribute("value") == "") throw new IOException("The EA app returned no authorization code");
            return new string[] { userId, code.GetAttribute("value") };
        }
    }

    static XmlDocument Request(Stream stream, byte[] key, int id, string body) {
        WriteMessage(stream, EncryptHex(key, "<LSX><Request recipient=\"EbisuSDK\" id=\"" + id + "\">" + body + "</Request></LSX>"));
        for (;;) {
            var doc = new XmlDocument();
            doc.LoadXml(DecryptHex(key, ReadMessage(stream)));
            var response = doc.SelectSingleNode("/LSX/Response") as XmlElement;
            if (response == null || response.GetAttribute("id") != id.ToString()) continue;  // events, other replies
            var error = doc.SelectSingleNode("//ErrorSuccess") as XmlElement;
            if (error != null && error.GetAttribute("Code") != "" && error.GetAttribute("Code") != "0")
                throw new IOException("The EA app reported: " + error.GetAttribute("Description") + " (" + error.GetAttribute("Code") + ")");
            return doc;
        }
    }
}

}
