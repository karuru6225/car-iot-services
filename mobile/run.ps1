#Requires -Version 5.1
# Wrapper for `flutter run` that embeds the current git hash via --dart-define.
# Usage: ./run.ps1 [extra flutter run args...]
[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments)][string[]]$ExtraArgs
)

$ErrorActionPreference = "Stop"

$ScriptDir = $PSScriptRoot

Push-Location $ScriptDir
try {
    $hash = (git rev-parse --short HEAD)
    if ($LASTEXITCODE -ne 0) { Write-Error "git rev-parse failed."; exit 1 }
    $hash = $hash.Trim()

    $flutterArgs = @("run", "--dart-define=GIT_HASH=$hash") + $ExtraArgs
    & flutter @flutterArgs
    if ($LASTEXITCODE -ne 0) { Write-Error "flutter run failed."; exit 1 }
} finally {
    Pop-Location
}
