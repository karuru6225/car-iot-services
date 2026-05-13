# send_command.ps1 - AWS IoT Jobs 経由でデバイスにコマンドを送信する
#
# Usage:
#   .\send_command.ps1 -ThingName esp32-gw-aabbccddeeff -Command charge_main_batt
#   .\send_command.ps1 -ThingName esp32-gw-aabbccddeeff -Command charge_main_batt -TimeoutSec 600
#   .\send_command.ps1 -ThingName esp32-gw-aabbccddeeff -Command ah_reset
#
# Run from the ops\ directory

param(
  [Parameter(Mandatory)][string]$ThingName,
  [Parameter(Mandatory)][ValidateSet("charge_main_batt", "ah_reset")][string]$Command,
  [int]$TimeoutSec = 600,
  [string]$Region  = "ap-northeast-1",
  [string]$Profile = ""
)

$ErrorActionPreference = "Stop"

if ($Profile) {
  $env:AWS_PROFILE = $Profile
  $credEnv = aws configure export-credentials --profile $Profile --format powershell
  if ($LASTEXITCODE -ne 0) { Write-Error "Failed to get credentials."; exit 1 }
  Invoke-Expression ($credEnv -join "`n")
}

# ジョブドキュメント生成
$doc = switch ($Command) {
  "charge_main_batt" {
    @{ operation = "charge_main_batt"; timeout_sec = $TimeoutSec } | ConvertTo-Json -Compress
  }
  "ah_reset" {
    @{ operation = "ah_reset" } | ConvertTo-Json -Compress
  }
}

# アカウント ID 取得
$accountId = (aws sts get-caller-identity --query Account --output text --region $Region)
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to get account ID."; exit 1 }

$thingArn = "arn:aws:iot:${Region}:${accountId}:thing/${ThingName}"
$jobId    = "${Command}-$(Get-Date -Format 'yyyyMMddHHmmss')" -replace "_", "-"

Write-Host ""
Write-Host "==> Job ID  : $jobId"
Write-Host "==> Target  : $thingArn"
Write-Host "==> Document: $doc"
Write-Host ""

aws iot create-job `
  --job-id    $jobId `
  --targets   $thingArn `
  --document  $doc `
  --region    $Region

if ($LASTEXITCODE -ne 0) { Write-Error "Failed to create job."; exit 1 }

Write-Host ""
Write-Host "Job created. The device will pick it up on next wakeup."
Write-Host "Check status: aws iot describe-job --job-id $jobId --region $Region"
