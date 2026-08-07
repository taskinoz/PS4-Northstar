[CmdletBinding(SupportsShouldProcess)]
param(
    [string] $Root = (Join-Path $PSScriptRoot '..\work\stage1\sparse')
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = [System.IO.Path]::GetFullPath($Root)
if (-not (Test-Path -LiteralPath $resolvedRoot -PathType Container)) {
    throw "Sparse Stage 1 payload not found: $resolvedRoot"
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$changedFiles = 0
$changedDirectives = 0

Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
    Where-Object { $_.Extension -in '.nut', '.gnut' } |
    ForEach-Object {
        $content = [System.IO.File]::ReadAllText($_.FullName)
        $updated = [regex]::Replace($content, '#(if|elseif)\s+!VANILLA\b', '#$1 1 // PS4 Stage 1: compile the Northstar branch')
        $updated = [regex]::Replace($updated, '#(if|elseif)\s+VANILLA\b', '#$1 0 // PS4 Stage 1: vanilla coexistence branch disabled')
        if ($updated -cne $content) {
            $count = ([regex]::Matches($content, '#(?:if|elseif)\s+!?VANILLA\b')).Count
            if ($PSCmdlet.ShouldProcess($_.FullName, "Resolve $count VANILLA directive(s)")) {
                [System.IO.File]::WriteAllText($_.FullName, $updated, $utf8NoBom)
                $changedFiles++
                $changedDirectives += $count
            }
        }
    }

$remaining = Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File |
    Where-Object { $_.Extension -in '.nut', '.gnut' } |
    Select-String -Pattern '#(?:if|elseif)\s+!?VANILLA\b'
if ($remaining) { throw "Unresolved VANILLA directives remain under $resolvedRoot" }

[pscustomobject]@{
    Root = $resolvedRoot
    ChangedFiles = $changedFiles
    ChangedDirectives = $changedDirectives
    RemainingDirectives = 0
}