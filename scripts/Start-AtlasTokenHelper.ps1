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
   - for a PS4 (or shadPS4 on another computer), type the address and code
     that Northstar shows in its sign-in message. The helper sends the token
     to the game over the network (port 37012), and the game applies it at
     once;
   - if the game is not running here and no address is given, the helper
     writes atlas_identity.json into the shadPS4 data folder instead.
4. Unless -Once is given, it keeps running and gives the game a new token
   whenever Atlas refuses the old one (http://<this PC>:37011/atlas/token).

Pairing: the game only asks this helper for tokens with the random key in
%APPDATA%\NorthstarPS4\token-helper.json, which the sign-in hands over. A
console that is already paired is signed in again without a code. The console
talks to the helper over plain HTTP, so on a LAN the key and the tokens cross
the network unencrypted: use it on a network you trust.

Neither the EA code, the Atlas token nor the key is ever printed.

Minting a token signs out any PC Northstar session on the same account.

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
./scripts/Start-AtlasTokenHelper.ps1
.EXAMPLE
./scripts/Start-AtlasTokenHelper.ps1 -Console "192.168.1.20 4821"
#>
[CmdletBinding()]
param(
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
# separate assembly; PowerShell 7 references it already.
$typeOptions = @{}
if ($PSVersionTable.PSEdition -ne 'Core') { $typeOptions.ReferencedAssemblies = @('System.Xml') }
Add-Type @typeOptions -TypeDefinition @"
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

# The pairing key, and the console signed in last time. Returns @{ key; console }.
function Get-HelperState {
    $state = @{ key = $null; console = $null }
    if (Test-Path -LiteralPath $KeyFile) {
        $saved = Get-Content -LiteralPath $KeyFile -Raw | ConvertFrom-Json
        if ($saved.key -match '^[0-9a-f]{32}$') { $state.key = $saved.key }
        if ($saved.console -match '^[A-Za-z0-9.\-]+$') { $state.console = $saved.console }
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

$script:lastToken = $null
$script:lastUid = $null
$script:lastMinted = [datetime]::MinValue

# EA code -> Atlas token. Returns @{ uid; token }.
function New-AtlasToken {
    if ($script:lastToken -and ((Get-Date) - $script:lastMinted).TotalSeconds -lt $MinSecondsBetweenTokens) {
        return @{ uid = $script:lastUid; token = $script:lastToken }
    }
    try {
        $pair = [NsLsx]::GetAuthCode($LsxPort, $ContentId, $Title, $ClientId, $Scope)
    } catch [Net.Sockets.SocketException] {
        throw 'Could not reach the EA app. Open the EA app, sign in, and try again.'
    } catch {
        $inner = $_.Exception
        while ($inner.InnerException) { $inner = $inner.InnerException }
        if ($inner -is [Net.Sockets.SocketException]) { throw 'Could not reach the EA app. Open the EA app, sign in, and try again.' }
        throw $inner.Message
    }
    $uid = $pair[0]
    $url = "$($MasterServer.TrimEnd('/'))/client/origin_auth?id=$uid&token=$([uri]::EscapeDataString($pair[1]))"
    try {
        $reply = Invoke-RestMethod -Uri $url -Method Get -UserAgent "R2Northstar/$LauncherVersion+ps4 NorthstarPS4TokenHelper" -TimeoutSec 20
    } catch {
        $message = $_.ErrorDetails.Message
        try { $message = ($message | ConvertFrom-Json).error.msg } catch { }
        if (-not $message) { $message = $_.Exception.Message }
        throw "Northstar refused the EA sign-in: $message"
    }
    if (-not $reply.success -or $reply.token -notmatch '^[0-9a-f]{32}$') { throw 'Northstar returned no usable token.' }
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

function Test-Loopback([string] $address) { return $address -eq 'localhost' -or $address -like '127.*' }

# Whether Northstar is running and listening for a sign-in at that address.
function Test-Game([string] $address) {
    try {
        $hello = Invoke-RestMethod -Uri "http://$($address):$ConsolePort/northstar/hello" -TimeoutSec 3
        return $hello.app -eq 'NorthstarPS4'
    } catch { return $false }
}

# This PC's address as the console sees it: the local end of a route to it.
function Get-AddressTowards([string] $address) {
    if ($AdvertiseHost) { return $AdvertiseHost }
    if (Test-Loopback $address) { return '127.0.0.1' }
    $udp = New-Object Net.Sockets.UdpClient
    try {
        $udp.Connect($address, $ConsolePort)
        return $udp.Client.LocalEndPoint.Address.ToString()
    } finally { $udp.Close() }
}

# Hands the identity to the game. Returns $null on success, or why not.
function Send-SignIn([string] $address, [string] $code, [hashtable] $identity, [string] $key) {
    $refreshUrl = "http://$(Get-AddressTowards $address):$Port/atlas/token"
    $body = [ordered]@{ uid = $identity.uid; playerToken = $identity.token; refreshUrl = $refreshUrl; refreshKey = $key; code = $code } |
        ConvertTo-Json -Compress
    try {
        $reply = Invoke-RestMethod -Uri "http://$($address):$ConsolePort/northstar/signin" -Method Post -Body $body `
            -ContentType 'application/json' -TimeoutSec 10
        if ($reply.ok) { return $null }
        return 'the game did not accept the sign-in'
    } catch {
        $message = $_.ErrorDetails.Message
        try { $message = ($message | ConvertFrom-Json).error } catch { }
        if ($message) { return $message }
        return "Northstar could not be reached at $($address):$ConsolePort. Check the address, and that the game is running"
    }
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

$state = Get-HelperState
Write-Host 'NorthstarPS4 token helper'
Write-Host 'Getting a Northstar token through the EA app...'
try {
    $identity = New-AtlasToken
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
Write-Host "Signed in to EA as account $($identity.uid)."

# Where the game is: $address is $null when the identity file is written instead.
$address = $null
$interactive = -not $Console
if ($Console -eq 'local') {
    if ((Test-Game '127.0.0.1') -and -not (Send-SignIn '127.0.0.1' '' $identity $state.key)) { $address = '127.0.0.1' }
} elseif ($Console) {
    $target = Split-Target $Console
    $failure = Send-SignIn $target.address $target.code $identity $state.key
    if ($failure) { Write-Host "Sign-in failed: $failure." -ForegroundColor Red; exit 1 }
    $address = $target.address
} elseif (Test-Game '127.0.0.1') {
    $failure = Send-SignIn '127.0.0.1' '' $identity $state.key
    if ($failure) { Write-Host "Sign-in failed: $failure." -ForegroundColor Red; exit 1 }
    $address = '127.0.0.1'
} elseif ($state.console -and (Test-Game $state.console) -and -not (Send-SignIn $state.console '' $identity $state.key)) {
    $address = $state.console
} else {
    while ($interactive) {
        Write-Host ''
        Write-Host 'Type the address and code that Northstar shows on the PS4 (for example: 192.168.1.20 4821).'
        Write-Host 'If Northstar runs in shadPS4 on this PC and is not started yet, just press Enter.'
        $answer = Read-Host '>'
        if ($null -eq $answer -or -not $answer.Trim()) { break }
        $target = Split-Target $answer
        $failure = Send-SignIn $target.address $target.code $identity $state.key
        if (-not $failure) { $address = $target.address; break }
        Write-Host "Sign-in failed: $failure." -ForegroundColor Red
    }
}

if ($address) {
    if (Test-Loopback $address) {
        Write-Host 'Signed in Northstar running in shadPS4 on this PC.' -ForegroundColor Green
    } else {
        Write-Host "Signed in Northstar on $address." -ForegroundColor Green
        $state.console = $address
        Save-HelperState $state
    }
} else {
    Write-Identity $identity "http://127.0.0.1:$Port/atlas/token" $state.key
    Write-Host "Saved the sign-in to $Output; Northstar in shadPS4 on this PC picks it up when it starts." -ForegroundColor Green
}
if ($Once) { exit 0 }

$remote = $address -and -not (Test-Loopback $address)
$listenAddress = if ($remote) { [Net.IPAddress]::Any } else { [Net.IPAddress]::Loopback }
$listener = New-Object Net.Sockets.TcpListener($listenAddress, $Port)
$listener.Start()
Write-Host ''
Write-Host 'Leave this window open while you play: Northstar asks it for a new token when the old one expires.'
Write-Host 'Close the window or press Ctrl+C to stop.'

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
            } elseif (-not (Test-KeyEqual $headers['x-northstarps4-key'] $state.key)) {
                Write-Host "$(Get-Date -Format T) refused a request from $peer (not paired with this helper)"
                Write-Reply $stream 403 'Forbidden' '{"error":"this console is not paired with the token helper"}'
            } else {
                try {
                    $identity = New-AtlasToken
                    if (-not $remote) { Write-Identity $identity "http://127.0.0.1:$Port/atlas/token" $state.key }
                    Write-Host "$(Get-Date -Format T) gave $peer a new token for account $($identity.uid)"
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
