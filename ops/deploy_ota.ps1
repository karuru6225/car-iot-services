# deploy_ota.ps1 - Build, upload, and create OTA firmware job
#
# Usage:
#   .\deploy_ota.ps1 -Version 1.2.0
#   .\deploy_ota.ps1 -Version 1.2.0 -ThingName esp32-gw-aabbccddeeff
#
# Omitting ThingName targets all Things matching esp32-gw-*
# Run from the ops\ directory

param(
  [Parameter(Mandatory)][string]$Version,
  [string]$ThingName = "",
  [string]$Profile = ''
)

$ErrorActionPreference = "Stop"

if ($Profile) {
  $env:AWS_PROFILE = $Profile
  Write-Host "AWS profile: $Profile"

  Write-Host '==> aws configure export-credentials'
  $credEnv = aws configure export-credentials --profile $Profile --format powershell
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to get credentials. Run 'aws login' first and try again."
    exit 1
  }
  Invoke-Expression ($credEnv -join "`n")
}

$ScriptDir   = $PSScriptRoot
$ProjectDir  = Resolve-Path "$ScriptDir\..\esp32_iot_gateway"
$BuildDir    = "$ProjectDir\.pio\build\esp32-s3-devkitc-1"
$FirmwareBin = "$BuildDir\firmware.bin"
$Pio         = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

# ─── Read settings from Terraform outputs ─────────────────────────────────────

Push-Location "$ScriptDir\..\infra"
$Bucket  = terraform output -raw firmware_bucket
$BaseUrl = terraform output -raw firmware_base_url
$Region  = "ap-northeast-1"
Pop-Location

$FirmwareKey = "firmware/v$Version.bin"
$FirmwareUrl = "$BaseUrl/$FirmwareKey"
$JobDocKey   = "jobs/v$Version.json"
$JobId       = "ota-v$($Version -replace '\.', '_')"

Write-Host "=== OTA Deploy ==="
Write-Host "VERSION:  $Version"
Write-Host "BUCKET:   $Bucket"
Write-Host "JOB_ID:   $JobId"
Write-Host ""

# ─── 1. Build ─────────────────────────────────────────────────────────────────

Write-Host ">>> Building firmware..."
Push-Location $ProjectDir
& $Pio run
Pop-Location
Write-Host "Build complete: $FirmwareBin"

# ─── 2. Upload firmware.bin to S3 ─────────────────────────────────────────────

Write-Host ">>> Uploading firmware.bin to S3..."
aws s3 cp $FirmwareBin "s3://$Bucket/$FirmwareKey"
Write-Host "Upload complete: $FirmwareUrl"

# ─── 3. Generate and upload job document ──────────────────────────────────────

Write-Host ">>> Generating job document..."
$JobDoc = @{ operation = "ota"; version = $Version; url = $FirmwareUrl } | ConvertTo-Json
$TmpJson = [System.IO.Path]::GetTempFileName() + ".json"
[System.IO.File]::WriteAllText($TmpJson, $JobDoc, (New-Object System.Text.UTF8Encoding($false)))
aws s3 cp $TmpJson "s3://$Bucket/$JobDocKey"
Remove-Item $TmpJson -Force
Write-Host "Job document uploaded"

# ─── 4. Resolve target Things ─────────────────────────────────────────────────

$AccountId = (aws sts get-caller-identity | ConvertFrom-Json).Account

if ($ThingName) {
  $Targets = "arn:aws:iot:${Region}:${AccountId}:thing/$ThingName"
  Write-Host "Target: $Targets"
} else {
  Write-Host ">>> Searching for Things matching esp32-gw-*..."
  $Things = (aws iot list-things | ConvertFrom-Json).things |
    Where-Object { $_.thingName -like "esp32-gw-*" } |
    Select-Object -ExpandProperty thingName
  if (-not $Things) { throw "No Things matching esp32-gw-* found" }
  Write-Host "Targets: $($Things -join ', ')"
  $Targets = ($Things | ForEach-Object { "arn:aws:iot:${Region}:${AccountId}:thing/$_" }) -join ','
}

# ─── 5. Create IoT Job ────────────────────────────────────────────────────────

Write-Host ">>> Creating IoT Job..."
$DocUrl = "https://s3.$Region.amazonaws.com/$Bucket/$JobDocKey"
aws iot create-job `
  --job-id $JobId `
  --targets $Targets `
  --document-source $DocUrl `
  --timeout-config inProgressTimeoutInMinutes=90

Write-Host ""
Write-Host "=== Deploy complete ==="
Write-Host "Job ID:       $JobId"
Write-Host "Firmware URL: $FirmwareUrl"
Write-Host "OTA will be applied on the device's next boot"
