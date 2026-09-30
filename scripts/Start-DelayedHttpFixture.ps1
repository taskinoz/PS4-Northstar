[CmdletBinding()]
param(
    [ValidateRange(1024, 65535)][int] $Port = 18765,
    [ValidateRange(1, 60)][int] $DelaySeconds = 12,
    [ValidateRange(1, 20)][int] $Requests = 1
)
$ErrorActionPreference = 'Stop'
$listener = [Net.HttpListener]::new()
$listener.Prefixes.Add("http://127.0.0.1:$Port/")
try {
    $listener.Start()
    Write-Output "Delayed HTTP fixture listening on http://127.0.0.1:$Port/"
    for ($i = 0; $i -lt $Requests; ++$i) {
        $context = $listener.GetContext()
        Start-Sleep -Seconds $DelaySeconds
        $body = [Text.Encoding]::UTF8.GetBytes("delayed-$i")
        $context.Response.StatusCode = 200
        $context.Response.ContentType = 'text/plain'
        $context.Response.ContentLength64 = $body.Length
        $context.Response.OutputStream.Write($body, 0, $body.Length)
        $context.Response.Close()
    }
} finally {
    $listener.Stop()
    $listener.Close()
}
