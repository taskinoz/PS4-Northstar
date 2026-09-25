[CmdletBinding()]
param(
    [ValidateSet('status','launch','console','menu','back','json','cvar')][string] $Action = 'status',
    [string] $Command,
    [string] $Menu,
    [string] $Mailbox = (Join-Path $env:APPDATA 'shadPS4\data\northstar_ps4\ai_harness'),
    [int] $TimeoutSeconds = 60,
    [switch] $QueueOnly,
    [switch] $FromConsoleFile
)
$ErrorActionPreference = 'Stop'
if ($FromConsoleFile) {
    $Command = [IO.File]::ReadAllText((Join-Path (Split-Path $Mailbox -Parent) 'console.txt'))
    $Action = 'console'
}
if ($Action -eq 'console' -and [string]::IsNullOrWhiteSpace($Command)) { throw 'Supply -Command or -FromConsoleFile.' }
if ($Action -eq 'json' -and [string]::IsNullOrWhiteSpace($Command)) { throw 'Supply -Command with the JSON text to round-trip.' }
if ($Action -eq 'cvar' -and [string]::IsNullOrWhiteSpace($Command)) { throw 'Supply -Command with the convar name.' }
if ($Action -eq 'menu' -and [string]::IsNullOrWhiteSpace($Menu)) { throw 'Supply -Menu with a registered menu name.' }
New-Item -ItemType Directory -Force -Path $Mailbox | Out-Null
# Hold an exclusive writer lock through the reply. A timeout does not cancel execution.
$lock = [IO.File]::Open((Join-Path $Mailbox 'writer.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
try {
    $path = Join-Path $Mailbox 'request.json'
    if (Test-Path -LiteralPath $path) { throw 'A request is already pending; do not overwrite it.' }
    $id = [guid]::NewGuid().ToString('N')
    $json = @{id=$id; action=$Action; command=$Command; menu=$Menu} | ConvertTo-Json -Compress
    if ([Text.Encoding]::UTF8.GetByteCount($json) -gt 16384) { throw 'Request exceeds 16 KiB.' }
    $temp = Join-Path $Mailbox "$id.tmp"
    [IO.File]::WriteAllText($temp, $json, [Text.UTF8Encoding]::new($false))
    [IO.File]::Move($temp, $path)
    if ($QueueOnly) { [pscustomobject]@{id=$id;status='pending'}; return }
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $response = Join-Path $Mailbox 'response.json'
    do {
        if (Test-Path -LiteralPath $response) {
            $reply = Get-Content -LiteralPath $response -Raw | ConvertFrom-Json
            if ($reply.id -eq $id) {
                if ($reply.status -eq 'error') { throw $reply.message }
                $reply
                return
            }
        }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "No reply for $id. Execution is unknown; inspect request/response and the game log before retrying."
} finally { $lock.Dispose() }
