using System;
using System.Collections.Concurrent;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;

namespace NorthstarPS4.TokenHelper {

// Everything the helper does over the network, for the window and the
// terminal alike: getting an Atlas token through the EA app, finding and
// signing in the game, and serving new tokens on a background thread. Errors
// are thrown or returned as sentences meant for the player; activity lines
// queue up in Events. Neither the EA code, a token nor the key is ever put
// in a message.
//
// The game side is launcher/src/runtime_signin.inl (sign-in listener, port
// 37012) and RefreshAtlasToken in runtime_server_join.inl (asks /atlas/token).
public class TokenService {
    public int LsxPort = 3216;
    public string ContentId = "1039093", Title = "Titanfall2", ClientId = "TITANFALL2-PC-SERVER", Scope = "";
    public string MasterServer = "https://northstar.tf";
    public string UserAgent = "R2Northstar/1.31.13+ps4 NorthstarPS4TokenHelper";
    public int Port = 37011, ConsolePort = 37012, MinSecondsBetweenTokens = 60;
    public string Output = "", AdvertiseHost = "", Key = "";
    // shadPS4 on this PC: keep atlas_identity.json current as tokens are served.
    public volatile bool WriteIdentityOnRefresh;
    public readonly ConcurrentQueue<string> Events = new ConcurrentQueue<string>();

    readonly object mintLock = new object();
    string lastUid, lastToken;
    DateTime lastMinted = DateTime.MinValue;

    static TokenService() {
        try { ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12; } catch (NotSupportedException) { }
    }

    public void Note(string text) { Events.Enqueue(DateTime.Now.ToString("T") + "  " + text); }

    // EA code -> Atlas token: { uid, token }. A token minted in the last
    // MinSecondsBetweenTokens is reused, so a burst of requests mints one.
    public string[] Mint() {
        lock (mintLock) {
            if (lastToken != null && (DateTime.Now - lastMinted).TotalSeconds < MinSecondsBetweenTokens)
                return new string[] { lastUid, lastToken };
            string[] pair;
            try {
                pair = Lsx.GetAuthCode(LsxPort, ContentId, Title, ClientId, Scope);
            } catch (SocketException) {
                throw new Exception("Could not reach the EA app. Open the EA app, sign in, and try again.");
            } catch (IOException e) {
                if (e.InnerException is SocketException)
                    throw new Exception("The EA app did not answer. Make sure it is open and signed in, and try again.");
                throw new Exception(e.Message + ". Make sure the EA app is open and signed in, and try again.");
            } catch (XmlException) {
                throw new Exception("The EA app sent a reply this helper does not understand.");
            }
            string url = MasterServer.TrimEnd('/') + "/client/origin_auth?id=" + Uri.EscapeDataString(pair[0]) +
                "&token=" + Uri.EscapeDataString(pair[1]);
            int status;
            string body = Http("GET", url, null, 20000, true, out status);
            if (body == null)
                throw new Exception("Could not reach Northstar (" + MasterServer + "). Check the internet connection and try again.");
            Match token = Regex.Match(body, "\"token\"\\s*:\\s*\"([0-9a-f]{32})\"");
            if (status != 200 || !Regex.IsMatch(body, "\"success\"\\s*:\\s*true") || !token.Success) {
                Match message = Regex.Match(body, "\"msg\"\\s*:\\s*\"([^\"]*)\"");
                throw new Exception("Northstar refused the EA sign-in" +
                    (message.Success ? ": " + message.Groups[1].Value : " (status " + status + ")") + ".");
            }
            lastUid = pair[0];
            lastToken = token.Groups[1].Value;
            lastMinted = DateTime.Now;
            return new string[] { lastUid, lastToken };
        }
    }

    // The body, also for an error status; null when nothing answered.
    string Http(string method, string url, string json, int timeoutMs, bool toNorthstar, out int status) {
        status = 0;
        try {
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.Method = method;
            request.Timeout = timeoutMs;
            request.ReadWriteTimeout = timeoutMs;
            if (toNorthstar) request.UserAgent = UserAgent;
            else request.Proxy = null;  // the game is on this network
            if (json != null) {
                byte[] data = Encoding.UTF8.GetBytes(json);
                request.ContentType = "application/json";
                request.ContentLength = data.Length;
                using (var stream = request.GetRequestStream()) stream.Write(data, 0, data.Length);
            }
            HttpWebResponse response;
            try {
                response = (HttpWebResponse)request.GetResponse();
            } catch (WebException e) {
                response = e.Response as HttpWebResponse;
                if (response == null) return null;
            }
            using (response) {
                status = (int)response.StatusCode;
                using (var reader = new StreamReader(response.GetResponseStream(), Encoding.UTF8)) return reader.ReadToEnd();
            }
        } catch (WebException) {
            return null;
        } catch (IOException) {
            return null;
        } catch (UriFormatException) {
            return null;
        }
    }

    public static bool IsAddress(string address) {
        return !string.IsNullOrEmpty(address) && address.Length <= 253 && Regex.IsMatch(address, "^[A-Za-z0-9.\\-]+$");
    }

    public static bool IsLoopback(string address) {
        return address == "localhost" || (address != null && address.StartsWith("127."));
    }

    // Whether Northstar is running at that address and listening for a sign-in.
    public bool Hello(string address) {
        if (!IsAddress(address)) return false;
        int status;
        string body = Http("GET", "http://" + address + ":" + ConsolePort + "/northstar/hello", null, 3000, false, out status);
        return status == 200 && body != null && body.Contains("\"NorthstarPS4\"");
    }

    // This PC's address as the console sees it: the local end of a route to it.
    public string AddressTowards(string address) {
        if (!string.IsNullOrEmpty(AdvertiseHost)) return AdvertiseHost;
        if (IsLoopback(address)) return "127.0.0.1";
        try {
            using (var udp = new UdpClient()) {
                udp.Connect(address, ConsolePort);
                return ((IPEndPoint)udp.Client.LocalEndPoint).Address.ToString();
            }
        } catch (SocketException) {
            return "127.0.0.1";
        }
    }

    public string LocalRefreshUrl { get { return "http://127.0.0.1:" + Port + "/atlas/token"; } }

    // Hands { uid, token } to the game. Null on success, otherwise why not.
    public string SignIn(string address, string code, string[] identity) {
        if (!IsAddress(address)) return "that is not an address";
        code = Regex.Replace(code ?? "", "[^0-9]", "");
        string refreshUrl = "http://" + AddressTowards(address) + ":" + Port + "/atlas/token";
        string json = "{\"uid\":\"" + identity[0] + "\",\"playerToken\":\"" + identity[1] + "\",\"refreshUrl\":\"" +
            refreshUrl + "\",\"refreshKey\":\"" + Key + "\",\"code\":\"" + code + "\"}";
        int status;
        string body = Http("POST", "http://" + address + ":" + ConsolePort + "/northstar/signin", json, 10000, false, out status);
        if (body == null)
            return "Northstar could not be reached at " + address + ". Check the address, and that the game is running";
        if (status == 200 && Regex.IsMatch(body, "\"ok\"\\s*:\\s*true")) return null;
        Match error = Regex.Match(body, "\"error\"\\s*:\\s*\"([^\"]*)\"");
        return error.Success ? error.Groups[1].Value : "the game did not accept the sign-in (status " + status + ")";
    }

    // atlas_identity.json for shadPS4 on this PC, replaced in one step.
    public void WriteIdentity(string[] identity, string refreshUrl) {
        string json = "{\r\n  \"uid\": \"" + identity[0] + "\",\r\n  \"playerToken\": \"" + identity[1] +
            "\",\r\n  \"refreshUrl\": \"" + refreshUrl + "\",\r\n  \"refreshKey\": \"" + Key + "\"\r\n}\r\n";
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(Output)));
        string temp = Output + ".tmp";
        File.WriteAllText(temp, json);
        if (File.Exists(Output)) File.Replace(temp, Output, null);
        else File.Move(temp, Output);
    }

    public Task<string[]> MintAsync() { return Task.Run(() => Mint()); }
    public Task<bool> HelloAsync(string address) { return Task.Run(() => Hello(address)); }
    public Task<string> SignInAsync(string address, string code, string[] identity) {
        return Task.Run(() => SignIn(address, code, identity));
    }

    // Serving new tokens at /atlas/token on a background thread.
    TcpListener listener;
    public bool ServingAllInterfaces { get; private set; }
    public bool Serving { get { return listener != null; } }

    // Null when serving, otherwise why not.
    public string StartServing(bool allInterfaces) {
        if (listener != null && ServingAllInterfaces == allInterfaces) return null;
        Stop();
        var started = new TcpListener(allInterfaces ? IPAddress.Any : IPAddress.Loopback, Port);
        try {
            started.Start();
        } catch (SocketException) {
            return "port " + Port + " is in use, so the game cannot ask for new tokens. Is the token helper already running?";
        }
        listener = started;
        ServingAllInterfaces = allInterfaces;
        var thread = new Thread(ServeLoop);
        thread.IsBackground = true;
        thread.Start(started);
        return null;
    }

    public void Stop() {
        var current = listener;
        listener = null;
        if (current != null) current.Stop();
    }

    void ServeLoop(object state) {
        var own = (TcpListener)state;
        while (listener == own) {
            TcpClient client;
            try {
                client = own.AcceptTcpClient();
            } catch (SocketException) {
                if (listener != own) return;
                Thread.Sleep(200);
                continue;
            } catch (ObjectDisposedException) {
                return;
            } catch (InvalidOperationException) {
                return;
            }
            try {
                Handle(client);
            } catch (Exception e) {
                Note("request failed: " + e.Message);
            } finally {
                client.Close();
            }
        }
    }

    static string JsonText(string text) { return text.Replace("\\", "\\\\").Replace("\"", "'"); }

    static void Reply(Stream stream, int status, string reason, string body) {
        byte[] payload = Encoding.UTF8.GetBytes(body);
        byte[] head = Encoding.ASCII.GetBytes("HTTP/1.1 " + status + " " + reason +
            "\r\nContent-Type: application/json\r\nContent-Length: " + payload.Length + "\r\nConnection: close\r\n\r\n");
        stream.Write(head, 0, head.Length);
        stream.Write(payload, 0, payload.Length);
    }

    static bool KeyEqual(string a, string b) {
        if (a == null || b == null || a.Length != b.Length) return false;
        int diff = 0;
        for (int i = 0; i < a.Length; i++) diff |= a[i] ^ b[i];
        return diff == 0;
    }

    void Handle(TcpClient client) {
        client.ReceiveTimeout = 5000;
        var stream = client.GetStream();
        var reader = new StreamReader(stream, Encoding.ASCII, false, 1024, true);
        string requestLine = reader.ReadLine();
        string key = null, line;
        while (!string.IsNullOrEmpty(line = reader.ReadLine())) {
            int colon = line.IndexOf(':');
            if (colon > 0 && line.Substring(0, colon).Trim().ToLowerInvariant() == "x-northstarps4-key")
                key = line.Substring(colon + 1).Trim();
        }
        string peer = ((IPEndPoint)client.Client.RemoteEndPoint).Address.ToString();
        if (requestLine == null || !Regex.IsMatch(requestLine, "^GET /atlas/token(\\?.*)? HTTP/1\\.[01]$")) {
            Reply(stream, 404, "Not Found", "{\"error\":\"not found\"}");
            return;
        }
        if (!KeyEqual(key, Key)) {
            Note("refused a request from " + peer + " (not paired with this helper)");
            Reply(stream, 403, "Forbidden", "{\"error\":\"this console is not paired with the token helper\"}");
            return;
        }
        try {
            string[] identity = Mint();
            if (WriteIdentityOnRefresh) WriteIdentity(identity, LocalRefreshUrl);
            Note("gave " + peer + " a new token for account " + identity[0]);
            Reply(stream, 200, "OK", "{\"uid\":\"" + identity[0] + "\",\"playerToken\":\"" + identity[1] + "\"}");
        } catch (Exception e) {
            Note("could not get a token for " + peer + ": " + e.Message);
            Reply(stream, 502, "Bad Gateway", "{\"error\":\"" + JsonText(e.Message) + "\"}");
        }
    }
}

}
