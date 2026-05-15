# send_command.ps1 - Send a command to the device via AWS IoT Jobs
#
# Usage:
#   .\send_command.ps1 -ThingName esp32-gw-aabbccddeeff -Command charge_start
#   .\send_command.ps1 -ThingName esp32-gw-aabbccddeeff -Command charge_stop
#   .\send_command.ps1 -ThingName esp32-gw-aabbccddeeff -Command ah_reset
#
# Run from the ops\ directory

param(
  [Parameter(Mandatory)][string]$ThingName,
  [Parameter(Mandatory)][ValidateSet("charge_start", "charge_stop", "ah_reset")][string]$Command,
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

$doc = '{"operation":"' + $Command + '"}'

$accountId = (aws sts get-caller-identity --query Account --output text --region $Region)
if ($LASTEXITCODE -ne 0) { Write-Error "Failed to get account ID."; exit 1 }

$thingArn = "arn:aws:iot:${Region}:${accountId}:thing/${ThingName}"
$jobId    = ($Command -replace "_", "-") + "-" + (Get-Date -Format "yyyyMMddHHmmss")

Write-Host ""
Write-Host "==> Job ID  : $jobId"
Write-Host "==> Target  : $thingArn"
Write-Host "==> Document: $doc"
Write-Host ""

$tmpFile = [System.IO.Path]::GetTempFileName()
$doc | Set-Content -Path $tmpFile -Encoding ascii

aws iot create-job --job-id $jobId --targets $thingArn --document "file://$tmpFile" --region $Region

Remove-Item $tmpFile -ErrorAction SilentlyContinue

if ($LASTEXITCODE -ne 0) { Write-Error "Failed to create job."; exit 1 }

Write-Host ""
Write-Host "Job created. The device will pick it up on next wakeup."
Write-Host "Check status: aws iot describe-job --job-id $jobId --region $Region"
