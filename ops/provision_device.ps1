# provision_device.ps1 - ESP32 デバイスの初回プロビジョニング
#
# 使い方:
#   .\provision_device.ps1 -Port COM3
#
# 必要なもの:
#   - AWS CLI（設定済み）
#   - PlatformIO（pio コマンド）
#   - infra/ ディレクトリで実行すること

param(
  [Parameter(Mandatory)][string]$Port
)

$ErrorActionPreference = "Stop"

$ScriptDir  = $PSScriptRoot
$ProjectDir = Resolve-Path "$ScriptDir\..\esp32_iot_gateway"
$Python     = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"

# ─── Terraform outputs から設定を取得 ────────────────────────────────────────

Push-Location $ScriptDir
$MqttHost   = terraform output -raw iot_endpoint
$PolicyName = terraform output -raw iot_policy_name
Pop-Location

Write-Host "=== ESP32 プロビジョニング ==="
Write-Host "Port:        $Port"
Write-Host "MQTT_HOST:   $MqttHost"
Write-Host "POLICY_NAME: $PolicyName"
Write-Host ""

# ─── 1. MAC アドレスから device ID を生成 ─────────────────────────────────────

Write-Host ">>> MAC アドレスを読み取り中..."
$MacOutput = & $Python -m esptool --port $Port read_mac 2>&1
$MacRaw    = ($MacOutput | Select-String "MAC:").ToString() -replace ".*MAC:\s*", ""
if (-not $MacRaw) { throw "MAC アドレスの読み取りに失敗しました" }

$DeviceId = "esp32-gw-$($MacRaw -replace ':', '')"
Write-Host "DEVICE_ID:   $DeviceId"
Write-Host ""

# ─── 2. AWS IoT Thing を作成 ──────────────────────────────────────────────────

Write-Host ">>> AWS IoT Thing を作成中..."
aws iot create-thing --thing-name $DeviceId | Out-Null
Write-Host "Thing 作成: $DeviceId"

# ─── 3. 証明書を発行・有効化 ──────────────────────────────────────────────────

Write-Host ">>> 証明書を発行中..."
$CertsDir = "$ProjectDir\data\certs"
New-Item -ItemType Directory -Force -Path $CertsDir | Out-Null

$CertJson = aws iot create-keys-and-certificate --set-as-active | ConvertFrom-Json
$CertArn  = $CertJson.certificateArn
$CertJson.certificatePem          | Set-Content "$CertsDir\device.crt" -NoNewline
$CertJson.keyPair.PrivateKey      | Set-Content "$CertsDir\device.key" -NoNewline
Write-Host "証明書 ARN: $CertArn"

# ─── 4. Amazon Root CA を取得 ────────────────────────────────────────────────

Write-Host ">>> Amazon Root CA を取得中..."
Invoke-WebRequest "https://www.amazontrust.com/repository/AmazonRootCA1.pem" `
  -OutFile "$CertsDir\ca.crt"
Write-Host "ca.crt を取得しました"

# ─── 5. ポリシーと証明書を Thing にアタッチ ───────────────────────────────────

Write-Host ">>> ポリシーをアタッチ中..."
aws iot attach-policy --policy-name $PolicyName --target $CertArn
aws iot attach-thing-principal --thing-name $DeviceId --principal $CertArn
Write-Host "ポリシー・証明書をアタッチしました"

# ─── 6. SPIFFS に証明書を書き込む ────────────────────────────────────────────

Write-Host ">>> SPIFFS に証明書を書き込み中..."
Push-Location $ProjectDir
pio run -t uploadfs --upload-port $Port
Pop-Location
Write-Host "SPIFFS 書き込み完了"

# ─── 7. NVS に mqtt_host を書き込む ──────────────────────────────────────────

Write-Host ">>> NVS に mqtt_host を書き込み中..."
$NvsGen  = "$env:USERPROFILE\.platformio\packages\framework-espidf\tools\nvs_flash\nvs_partition_generator\nvs_partition_generator.py"
$NvsSize = "0x5000"
$TmpCsv  = [System.IO.Path]::GetTempFileName() + ".csv"
$TmpBin  = [System.IO.Path]::GetTempFileName() + ".bin"

@"
key,type,encoding,value
device,namespace,,
mqtt_host,data,string,$MqttHost
"@ | Set-Content $TmpCsv -Encoding UTF8

& $Python $NvsGen generate $TmpCsv $TmpBin $NvsSize
& $Python -m esptool --port $Port write_flash 0x9000 $TmpBin
Remove-Item $TmpCsv, $TmpBin -Force
Write-Host "NVS 書き込み完了"

# ─── 8. 一時ファイルを削除 ────────────────────────────────────────────────────

Write-Host ">>> 証明書ファイルを削除中..."
Remove-Item "$CertsDir\device.crt", "$CertsDir\device.key", "$CertsDir\ca.crt" -Force
Write-Host "証明書ファイルを削除しました（SPIFFS に書き込み済み）"

# ─── 完了 ─────────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "=== プロビジョニング完了 ==="
Write-Host "DEVICE_ID: $DeviceId"
Write-Host "デバイスを再起動してください"
