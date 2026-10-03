<#
.SYNOPSIS
Renews the PS4 build's Atlas token from a PC signed in to the EA app.

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
3. It writes atlas_identity.json (uid, playerToken, refreshUrl, refreshKey)
   and, unless -Once is given, keeps serving fresh tokens at
   http://<host>:<port>/atlas/token to the console it is paired with.

The PS4 runtime asks for a new token by itself whenever Atlas refuses the one
it has, so after the first run nothing needs copying again while the helper
runs. Pairing: requests must carry the random key stored in
%APPDATA%\NorthstarPS4\token-helper.json, which is also written into
atlas_identity.json. Anyone holding that key could ask this helper for a token
for your account, so keep the identity file private.

Neither the EA code, the Atlas token nor the key is ever printed.

Minting a token signs out any PC Northstar session on the same account, as
starting PC Northstar does to the PS4 today.

.PARAMETER Once
Get one token, write atlas_identity.json and exit.
.PARAMETER Lan
Listen on all interfaces and advertise this PC's LAN address, for a real PS4.
Without it the helper listens on 127.0.0.1 only, which is what shadPS4 on
this PC needs. The console talks to the helper over plain HTTP, so on a LAN
the key and the tokens cross the network unencrypted: use -Lan only on a
network you trust.
.PARAMETER Output
Where atlas_identity.json goes. Defaults to the shadPS4 data folder, which the
PS4 build sees as /data/northstar_ps4. For a real PS4, copy the file to the
console once; refreshes after that arrive over the network.
.EXAMPLE
./scripts/Start-AtlasTokenHelper.ps1
.EXAMPLE
./scripts/Start-AtlasTokenHelper.ps1 -Lan -Output .\atlas_identity.json
#>
[CmdletBinding()]
param(
    [switch] $Once,
    [switch] $Lan,
    [int] $Port = 37011,
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

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
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
"@

function Get-PairingKey {
    if (Test-Path -LiteralPath $KeyFile) {
        $saved = Get-Content -LiteralPath $KeyFile -Raw | ConvertFrom-Json
        if ($saved.key -match '^[0-9a-f]{32}$') { return $saved.key }
    }
    $bytes = New-Object byte[] 16
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $key = -join ($bytes | ForEach-Object { $_.ToString('x2') })
    New-Item -ItemType Directory -Force -Path (Split-Path $KeyFile) | Out-Null
    [IO.File]::WriteAllText($KeyFile, (@{ key = $key } | ConvertTo-Json))
    return $key
}

function Get-AdvertisedHost {
    if ($AdvertiseHost) { return $AdvertiseHost }
    if (-not $Lan) { return '127.0.0.1' }
    $address = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object { $_.IPAddress -notlike '127.*' -and $_.IPAddress -notlike '169.254.*' -and $_.PrefixOrigin -ne 'WellKnown' } |
        Sort-Object InterfaceMetric | Select-Object -First 1
    if (-not $address) { throw 'No LAN IPv4 address found; pass -AdvertiseHost.' }
    return $address.IPAddress
}

$script:lastToken = $null
$script:lastUid = $null
$script:lastMinted = [datetime]::MinValue

# EA code -> Atlas token. Returns @{ uid; token }.
function New-AtlasToken {
    if ($script:lastToken -and ((Get-Date) - $script:lastMinted).TotalSeconds -lt $MinSecondsBetweenTokens) {
        return @{ uid = $script:lastUid; token = $script:lastToken }
    }
    $pair = [NsLsx]::GetAuthCode($LsxPort, $ContentId, $Title, $ClientId, $Scope)
    $uid = $pair[0]
    $url = "$($MasterServer.TrimEnd('/'))/client/origin_auth?id=$uid&token=$([uri]::EscapeDataString($pair[1]))"
    $headers = @{ 'User-Agent' = "R2Northstar/$LauncherVersion+ps4 NorthstarPS4TokenHelper" }
    try {
        $reply = Invoke-RestMethod -Uri $url -Method Get -Headers $headers -TimeoutSec 20
    } catch {
        $message = $_.ErrorDetails.Message
        try { $message = ($message | ConvertFrom-Json).error.msg } catch { }
        throw "Atlas refused the EA code: $message"
    }
    if (-not $reply.success -or $reply.token -notmatch '^[0-9a-f]{32}$') { throw 'Atlas returned no usable token.' }
    $script:lastUid = $uid
    $script:lastToken = $reply.token
    $script:lastMinted = Get-Date
    return @{ uid = $uid; token = $reply.token }
}

function Write-Identity([hashtable] $identity, [string] $refreshUrl, [string] $key) {
    $json = [ordered]@{ uid = $identity.uid; playerToken = $identity.token; refreshUrl = $refreshUrl; refreshKey = $key } |
        ConvertTo-Json
    New-Item -ItemType Directory -Force -Path (Split-Path $Output) | Out-Null
    $temp = "$Output.tmp"
    [IO.File]::WriteAllText($temp, $json)
    Move-Item -LiteralPath $temp -Destination $Output -Force
}

function Write-Reply($stream, [int] $status, [string] $reason, [string] $body) {
    $payload = [Text.Encoding]::UTF8.GetBytes($body)
    $head = "HTTP/1.1 $status $reason`r`nContent-Type: application/json`r`nContent-Length: $($payload.Length)`r`nConnection: close`r`n`r`n"
    $bytes = [Text.Encoding]::ASCII.GetBytes($head)
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Write($payload, 0, $payload.Length)
}

function Test-KeyEqual([string] $a, [string] $b) {
    if ($null -eq $a -or $a.Length -ne $b.Length) { return $false }
    $diff = 0
    for ($i = 0; $i -lt $a.Length; $i++) { $diff = $diff -bor ([int][char]$a[$i] -bxor [int][char]$b[$i]) }
    return $diff -eq 0
}

$key = Get-PairingKey
$advertise = Get-AdvertisedHost
$refreshUrl = "http://$($advertise):$Port/atlas/token"

Write-Host 'Getting an Atlas token through the EA app...'
$identity = New-AtlasToken
Write-Identity $identity $refreshUrl $key
Write-Host "Wrote $Output for account $($identity.uid) (token not shown)."
if ($Once) { return }

$listenAddress = if ($Lan) { [Net.IPAddress]::Any } else { [Net.IPAddress]::Loopback }
$listener = New-Object Net.Sockets.TcpListener($listenAddress, $Port)
$listener.Start()
Write-Host "Serving fresh tokens at $refreshUrl. Leave this window open while playing; Ctrl+C stops it."
try {
    for (;;) {
        $client = $listener.AcceptTcpClient()
        try {
            $client.ReceiveTimeout = 5000
            $stream = $client.GetStream()
            $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::ASCII, $false, 1024, $true)
            $requestLine = $reader.ReadLine()
            $headers = @{}
            while ($true) {
                $line = $reader.ReadLine()
                if ([string]::IsNullOrEmpty($line)) { break }
                $name, $value = $line -split ':\s*', 2
                if ($name) { $headers[$name.ToLowerInvariant()] = $value }
            }
            $peer = $client.Client.RemoteEndPoint.Address
            if ($requestLine -notmatch '^GET /atlas/token(\?.*)? HTTP/1\.[01]$') {
                Write-Reply $stream 404 'Not Found' '{"error":"not found"}'
            } elseif (-not (Test-KeyEqual $headers['x-northstarps4-key'] $key)) {
                Write-Host "$(Get-Date -Format T) refused a request from $peer (wrong key)"
                Write-Reply $stream 403 'Forbidden' '{"error":"this console is not paired with the token helper"}'
            } else {
                try {
                    $identity = New-AtlasToken
                    Write-Identity $identity $refreshUrl $key
                    Write-Host "$(Get-Date -Format T) gave $peer a fresh token for account $($identity.uid)"
                    Write-Reply $stream 200 'OK' (@{ uid = $identity.uid; playerToken = $identity.token } | ConvertTo-Json -Compress)
                } catch {
                    $message = "$($_.Exception.Message)" -replace '"', "'"
                    Write-Host "$(Get-Date -Format T) could not get a token for $($peer): $message"
                    Write-Reply $stream 502 'Bad Gateway' (@{ error = $message } | ConvertTo-Json -Compress)
                }
            }
        } catch {
            Write-Host "$(Get-Date -Format T) request failed: $($_.Exception.Message)"
        } finally {
            $client.Close()
        }
    }
} finally {
    $listener.Stop()
}
