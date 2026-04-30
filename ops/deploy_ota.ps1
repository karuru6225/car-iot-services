# deploy_ota.ps1 - OTA ファームウェアをビルド・アップロード・Job 作成
#
# 使い方:
#   .\deploy_ota.ps1 -Version 1.2.0
#   .\deploy_ota.ps1 -Version 1.2.0 -ThingName esp32-gw-aabbccddeeff
#
# ThingName を省略すると esp32-gw-* に一致する全 Thing を対象にする
# ops\ ディレクトリで実行すること

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

# ─── Terraform outputs から設定を取得 ────────────────────────────────────────

Push-Location "$ScriptDir\..\infra"
$Bucket  = terraform output -raw firmware_bucket
$BaseUrl = terraform output -raw firmware_base_url
$Region  = "ap-northeast-1"
Pop-Location

$FirmwareKey = "firmware/v$Version.bin"
$FirmwareUrl = "$BaseUrl/$FirmwareKey"
$JobDocKey   = "jobs/v$Version.json"
$JobId       = "ota-v$Version"

Write-Host "=== OTA デプロイ ==="
Write-Host "VERSION:  $Version"
Write-Host "BUCKET:   $Bucket"
Write-Host "JOB_ID:   $JobId"
Write-Host ""

# ─── 1. ビルド ────────────────────────────────────────────────────────────────

Write-Host ">>> ファームウェアをビルド中..."
Push-Location $ProjectDir
& $Pio run
Pop-Location
Write-Host "ビルド完了: $FirmwareBin"

# ─── 2. firmware.bin を S3 にアップロード ────────────────────────────────────

Write-Host ">>> firmware.bin を S3 にアップロード中..."
aws s3 cp $FirmwareBin "s3://$Bucket/$FirmwareKey"
Write-Host "アップロード完了: $FirmwareUrl"

# ─── 3. ジョブドキュメントを生成・アップロード ───────────────────────────────

Write-Host ">>> ジョブドキュメントを生成中..."
$JobDoc = @{ operation = "ota"; version = $Version; url = $FirmwareUrl } | ConvertTo-Json
$TmpJson = [System.IO.Path]::GetTempFileName() + ".json"
$JobDoc | Set-Content $TmpJson -Encoding UTF8
aws s3 cp $TmpJson "s3://$Bucket/$JobDocKey"
Remove-Item $TmpJson -Force
Write-Host "ジョブドキュメントをアップロードしました"

# ─── 4. 対象 Thing を決定 ─────────────────────────────────────────────────────

$AccountId = (aws sts get-caller-identity | ConvertFrom-Json).Account

if ($ThingName) {
  $Targets = "arn:aws:iot:${Region}:${AccountId}:thing/$ThingName"
  Write-Host "ターゲット: $Targets"
} else {
  Write-Host ">>> esp32-gw-* に一致する Thing を検索中..."
  $Things = (aws iot list-things | ConvertFrom-Json).things |
    Where-Object { $_.thingName -like "esp32-gw-*" } |
    Select-Object -ExpandProperty thingName
  if (-not $Things) { throw "esp32-gw-* に一致する Thing が見つかりません" }
  Write-Host "対象 Thing: $($Things -join ', ')"
  $Targets = ($Things | ForEach-Object { "arn:aws:iot:${Region}:${AccountId}:thing/$_" }) -join ','
}

# ─── 5. IoT Job を作成 ────────────────────────────────────────────────────────

Write-Host ">>> IoT Job を作成中..."
$DocUrl = "https://s3.$Region.amazonaws.com/$Bucket/$JobDocKey"
aws iot create-job `
  --job-id $JobId `
  --targets $Targets `
  --document-source $DocUrl `
  --timeout-config inProgressTimeoutInMinutes=90

Write-Host ""
Write-Host "=== デプロイ完了 ==="
Write-Host "Job ID:       $JobId"
Write-Host "Firmware URL: $FirmwareUrl"
Write-Host "デバイスの次回起動時に OTA が適用されます"
