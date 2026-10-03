<#
.SYNOPSIS
Signs the PS4 build in to Northstar from a PC signed in to the EA app, and keeps it signed in.

.DESCRIPTION
Atlas gives out a player token only in exchange for an EA authorization code
(/client/origin_auth), and a token lasts about a day; minting a new one also
ends the previous session for that account. A PS4 cannot get an EA code, so
this helper does it on the PC:

1. It asks the EA app for an authorization code, as Titanfall 2 itself does,
   over the EA app's local SDK connection (LSX, 127.0.0.1:3216): a challenge
   handshake as content 1039093 (Titanfall2), then GetProfile for the account
   id and GetAuthCode for client TITANFALL2-PC-SERVER.
2. It exchanges the code with Atlas at /client/origin_auth.
3. It hands the token to the game:
   - Northstar running in shadPS4 on this PC is found by itself;
   - for a PS4 (or shadPS4 on another computer), give the address and code
     that Northstar shows in its sign-in message. The helper sends the token
     to the game over the network (port 37012), and the game applies it at
     once;
   - if the game is not running here and no address is given, the helper
     writes atlas_identity.json into the shadPS4 data folder instead.
4. Unless -Once is given, it keeps running and gives the game a new token
   whenever Atlas refuses the old one (http://<this PC>:37011/atlas/token).

With -Gui (what NorthstarPS4-TokenHelper.cmd starts when double-clicked) it
does all this in a window (AtlasTokenHelperWindow.ps1). Without it, it runs in
the terminal and asks there when it needs an address.

Pairing: the game only asks this helper for tokens with the random key in
%APPDATA%\NorthstarPS4\token-helper.json, which the sign-in hands over. A
console that is already paired is signed in again without a code. The console
talks to the helper over plain HTTP, so on a LAN the key and the tokens cross
the network unencrypted: use it on a network you trust.

Neither the EA code, the Atlas token nor the key is ever printed.

Minting a token signs out any PC Northstar session on the same account.

.PARAMETER Gui
Show a window instead of using the terminal.
.PARAMETER Console
Where the game is, instead of asking: "<address> <code>" as the PS4 shows
it, "<address>" for a console already paired, or "local" for shadPS4 on this
PC.
.PARAMETER Once
Sign the game in once and exit, without serving new tokens.
.PARAMETER Output
Where atlas_identity.json is written when the game is not running on this
PC. Defaults to the shadPS4 data folder, which the PS4 build sees as
/data/northstar_ps4.
.EXAMPLE
./scripts/Start-AtlasTokenHelper.ps1 -Gui
.EXAMPLE
./scripts/Start-AtlasTokenHelper.ps1
.EXAMPLE
./scripts/Start-AtlasTokenHelper.ps1 -Console "192.168.1.20 4821"
#>
[CmdletBinding()]
param(
    [switch] $Gui,
    [string] $Console = '',
    [switch] $Once,
    [int] $Port = 37011,
    [int] $ConsolePort = 37012,
    [string] $Output = (Join-Path $env:APPDATA 'shadPS4\data\northstar_ps4\atlas_identity.json'),
    [string] $AdvertiseHost = '',
    [string] $MasterServer = 'https://northstar.tf',
    [string] $LauncherVersion = '1.31.13',
    [int] $LsxPort = 3216,
    [string] $ContentId = '1039093',
    [string] $Title = 'Titanfall2',
    [string] $ClientId = 'TITANFALL2-PC-SERVER',
    [string] $Scope = '',
    [string] $KeyFile = (Join-Path $env:APPDATA 'NorthstarPS4\token-helper.json'),
    [int] $MinSecondsBetweenTokens = 60
)
$ErrorActionPreference = 'Stop'

# Windows PowerShell (5.1) compiles against the .NET Framework, where XML is a
# separate assembly; PowerShell 7 references it already. The C# below is kept
# to C# 5 for the Framework compiler.
# HttpWebRequest is the HTTP API both have; .NET (PowerShell 7) marks it
# obsolete, and the Framework compiler rejects that warning's name.
$typeOptions = @{}
$typePrefix = '#pragma warning disable SYSLIB0014'
if ($PSVersionTable.PSEdition -ne 'Core') { $typeOptions.ReferencedAssemblies = @('System.Xml'); $typePrefix = '' }
Add-Type @typeOptions -TypeDefinition @"
$typePrefix
using System;
using System.Collections.Concurrent;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Xml;

// The EA app's local SDK protocol (LSX), as the Origin SDK speaks it.
// Messages are XML, NUL-terminated. The server opens with a Challenge; the
// client encrypts the challenge key with AES-128-ECB/PKCS7 under the default
// key (0..15), hex-encodes it, and derives the session key from the first two
// characters of that hex string. Every request after ChallengeAccepted is
// encrypted with the session key and hex-encoded, and so is every reply.
public static class NsLsx {
    static uint Next(ref uint state) { state = state * 214013u + 2531011u; return (state >> 16) & 0x7fffu; }

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
        aes.Mode = CipherMode.ECB; aes.Padding = PaddingMode.PKCS7; aes.Key = key;
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
            client.ReceiveTimeout = 30000; client.SendTimeout = 10000;
            client.Connect("127.0.0.1", port);
            var stream = client.GetStream();
            var key = Key(0);

            string challenge = null;
            while (challenge == null) {
                var doc = new XmlDocument(); doc.LoadXml(ReadMessage(stream));
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
                var doc = new XmlDocument(); doc.LoadXml(ReadMessage(stream));
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
            var doc = new XmlDocument(); doc.LoadXml(DecryptHex(key, ReadMessage(stream)));
            var response = doc.SelectSingleNode("/LSX/Response") as XmlElement;
            if (response == null || response.GetAttribute("id") != id.ToString()) continue;  // events, other replies
            var error = doc.SelectSingleNode("//ErrorSuccess") as XmlElement;
            if (error != null && error.GetAttribute("Code") != "" && error.GetAttribute("Code") != "0")
                throw new IOException("The EA app reported: " + error.GetAttribute("Description") + " (" + error.GetAttribute("Code") + ")");
            return doc;
        }
    }
}

// Everything the helper does over the network, for the window and the
// terminal alike: getting a token, finding and signing in the game, and
// serving new tokens on a background thread. Errors are thrown or returned
// as sentences meant for the player. Activity lines queue up in Events.
public class NsTokenHelper {
    public int LsxPort = 3216;
    public string ContentId = "1039093", Title = "Titanfall2", ClientId = "TITANFALL2-PC-SERVER", Scope = "";
    public string MasterServer = "https://northstar.tf";
    public string UserAgent = "R2Northstar/1.31.13+ps4 NorthstarPS4TokenHelper";
    public int Port = 37011, ConsolePort = 37012, MinSecondsBetweenTokens = 60;
    public string Output = "", AdvertiseHost = "", Key = "";
    // shadPS4 on this PC: keep atlas_identity.json current as tokens are served.
    public bool WriteIdentityOnRefresh;
    public readonly ConcurrentQueue<string> Events = new ConcurrentQueue<string>();

    readonly object mintLock = new object();
    string lastUid, lastToken;
    DateTime lastMinted = DateTime.MinValue;

    static NsTokenHelper() {
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
                pair = NsLsx.GetAuthCode(LsxPort, ContentId, Title, ClientId, Scope);
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

    public string RefreshUrlFor(string address) { return "http://" + AddressTowards(address) + ":" + Port + "/atlas/token"; }

    // Hands { uid, token } to the game. Null on success, otherwise why not.
    public string SignIn(string address, string code, string[] identity) {
        if (!IsAddress(address)) return "that is not an address";
        code = Regex.Replace(code ?? "", "[^0-9]", "");
        string json = "{\"uid\":\"" + identity[0] + "\",\"playerToken\":\"" + identity[1] + "\",\"refreshUrl\":\"" +
            RefreshUrlFor(address) + "\",\"refreshKey\":\"" + Key + "\",\"code\":\"" + code + "\"}";
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

    public void StartServing(bool allInterfaces) {
        if (listener != null && ServingAllInterfaces == allInterfaces) return;
        Stop();
        var started = new TcpListener(allInterfaces ? IPAddress.Any : IPAddress.Loopback, Port);
        started.Start();
        listener = started;
        ServingAllInterfaces = allInterfaces;
        var thread = new Thread(ServeLoop);
        thread.IsBackground = true;
        thread.Start(started);
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
            if (WriteIdentityOnRefresh) WriteIdentity(identity, "http://127.0.0.1:" + Port + "/atlas/token");
            Note("gave " + peer + " a new token for account " + identity[0]);
            Reply(stream, 200, "OK", "{\"uid\":\"" + identity[0] + "\",\"playerToken\":\"" + identity[1] + "\"}");
        } catch (Exception e) {
            Note("could not get a token for " + peer + ": " + e.Message);
            Reply(stream, 502, "Bad Gateway", "{\"error\":\"" + JsonText(e.Message) + "\"}");
        }
    }
}
"@

# The pairing key, and the console signed in last time. Returns @{ key; console }.
function Get-HelperState {
    $state = @{ key = $null; console = $null }
    if (Test-Path -LiteralPath $KeyFile) {
        try {
            $saved = Get-Content -LiteralPath $KeyFile -Raw | ConvertFrom-Json
            if ($saved.key -match '^[0-9a-f]{32}$') { $state.key = $saved.key }
            if ([NsTokenHelper]::IsAddress($saved.console)) { $state.console = $saved.console }
        } catch { }
    }
    if (-not $state.key) {
        $bytes = New-Object byte[] 16
        [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
        $state.key = -join ($bytes | ForEach-Object { $_.ToString('x2') })
        Save-HelperState $state
    }
    return $state
}

function Save-HelperState([hashtable] $state) {
    New-Item -ItemType Directory -Force -Path (Split-Path $KeyFile) | Out-Null
    [IO.File]::WriteAllText($KeyFile, ([ordered]@{ key = $state.key; console = $state.console } | ConvertTo-Json))
}

# Splits "<address> <code>" (or "<address>:<code>" / "<address>,<code>").
function Split-Target([string] $text) {
    $parts = @($text.Trim() -split '[\s,]+' | Where-Object { $_ })
    if ($parts.Count -eq 1 -and $parts[0] -match '^([0-9.]+):(\d{4})$') { $parts = @($Matches[1], $Matches[2]) }
    $target = @{ address = $null; code = '' }
    if ($parts.Count -ge 1) { $target.address = $parts[0] }
    if ($parts.Count -ge 2) { $target.code = $parts[1] }
    return $target
}

# The message of an exception thrown by NsTokenHelper, without PowerShell's wrapping.
function Get-Reason($errorRecord) {
    $exception = $errorRecord.Exception
    while ($exception.InnerException) { $exception = $exception.InnerException }
    return $exception.Message
}

$state = Get-HelperState
$helper = New-Object NsTokenHelper
$helper.LsxPort = $LsxPort; $helper.ContentId = $ContentId; $helper.Title = $Title; $helper.ClientId = $ClientId
$helper.Scope = $Scope; $helper.MasterServer = $MasterServer
$helper.UserAgent = "R2Northstar/$LauncherVersion+ps4 NorthstarPS4TokenHelper"
$helper.Port = $Port; $helper.ConsolePort = $ConsolePort; $helper.MinSecondsBetweenTokens = $MinSecondsBetweenTokens
$helper.Output = $Output; $helper.AdvertiseHost = $AdvertiseHost; $helper.Key = $state.key

# Starts serving new tokens; $null, or why it could not.
function Start-Serving([bool] $remote) {
    $helper.WriteIdentityOnRefresh = -not $remote
    try {
        $helper.StartServing($remote)
        return $null
    } catch {
        return "port $Port is in use, so the game cannot ask for new tokens. Is the helper already running? ($(Get-Reason $_))"
    }
}

if ($Gui) {
    . (Join-Path $PSScriptRoot 'AtlasTokenHelperWindow.ps1')
    Show-TokenHelperWindow
    exit 0
}

# ---- Terminal ----
Write-Host 'NorthstarPS4 token helper'
Write-Host 'Getting a Northstar token through the EA app...'
try {
    $identity = $helper.Mint()
} catch {
    Write-Host (Get-Reason $_) -ForegroundColor Red
    exit 1
}
Write-Host "Signed in to EA as account $($identity[0])."

# Where the game is: $address stays $null when the identity file is written instead.
$address = $null
if ($Console -eq 'local') {
    if ($helper.Hello('127.0.0.1') -and -not $helper.SignIn('127.0.0.1', '', $identity)) { $address = '127.0.0.1' }
} elseif ($Console) {
    $target = Split-Target $Console
    $failure = $helper.SignIn($target.address, $target.code, $identity)
    if ($failure) { Write-Host "Sign-in failed: $failure." -ForegroundColor Red; exit 1 }
    $address = $target.address
} elseif ($helper.Hello('127.0.0.1')) {
    $failure = $helper.SignIn('127.0.0.1', '', $identity)
    if ($failure) { Write-Host "Sign-in failed: $failure." -ForegroundColor Red; exit 1 }
    $address = '127.0.0.1'
} elseif ($state.console -and $helper.Hello($state.console) -and -not $helper.SignIn($state.console, '', $identity)) {
    $address = $state.console
} else {
    for (;;) {
        Write-Host ''
        Write-Host 'Type the address and code that Northstar shows on the PS4 (for example: 192.168.1.20 4821).'
        Write-Host 'If Northstar runs in shadPS4 on this PC and is not started yet, just press Enter.'
        $answer = Read-Host '>'
        if ($null -eq $answer -or -not $answer.Trim()) { break }
        $target = Split-Target $answer
        $failure = $helper.SignIn($target.address, $target.code, $identity)
        if (-not $failure) { $address = $target.address; break }
        Write-Host "Sign-in failed: $failure." -ForegroundColor Red
    }
}

$remote = $address -and -not [NsTokenHelper]::IsLoopback($address)
if ($address) {
    if ($remote) {
        Write-Host "Signed in Northstar on $address." -ForegroundColor Green
        $state.console = $address
        Save-HelperState $state
    } else {
        Write-Host 'Signed in Northstar running in shadPS4 on this PC.' -ForegroundColor Green
    }
} else {
    $helper.WriteIdentity($identity, "http://127.0.0.1:$Port/atlas/token")
    Write-Host "Saved the sign-in to $Output; Northstar in shadPS4 on this PC picks it up when it starts." -ForegroundColor Green
}
if ($Once) { exit 0 }

$failure = Start-Serving $remote
if ($failure) { Write-Host "Cannot keep the game signed in: $failure" -ForegroundColor Red; exit 1 }
Write-Host ''
Write-Host 'Leave this window open while you play: Northstar asks it for a new token when the old one expires.'
Write-Host 'Close the window or press Ctrl+C to stop.'
try {
    for (;;) {
        $line = $null
        while ($helper.Events.TryDequeue([ref] $line)) { Write-Host $line }
        Start-Sleep -Milliseconds 250
    }
} finally {
    $helper.Stop()
}
