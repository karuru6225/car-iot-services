# provision_device.ps1 - ESP32 device initial provisioning
#
# Usage:
#   .\provision_device.ps1 -Port COM3
#
# Requirements:
#   - AWS CLI (configured)
#   - PlatformIO (pio command)
#   - Run from ops/ directory

param(
  [Parameter(Mandatory)][string]$Port,
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

$ScriptDir  = $PSScriptRoot
$ProjectDir = Resolve-Path "$ScriptDir\..\esp32_iot_gateway"
$Python     = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$Esptool    = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
$Pio        = "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"

# --- Get config from Terraform outputs ---

Push-Location "$ScriptDir\..\infra"
$MqttHost   = terraform output -raw iot_endpoint
$PolicyName = terraform output -raw iot_policy_name
Pop-Location

Write-Host "=== ESP32 Provisioning ==="
Write-Host "Port:        $Port"
Write-Host "MQTT_HOST:   $MqttHost"
Write-Host "POLICY_NAME: $PolicyName"
Write-Host ""

# --- 1. Generate device ID from MAC address ---

Write-Host ">>> Reading MAC address..."
$MacOutput = & $Python $Esptool --port $Port read_mac 2>&1
$MacRaw    = [regex]::Match($MacOutput -join '', "MAC:\s*([0-9a-fA-F:]{17})").Groups[1].Value
if (-not $MacRaw) { throw "Failed to read MAC address" }

$DeviceId = "esp32-gw-$($MacRaw -replace ':', '')"
Write-Host "DEVICE_ID:   $DeviceId"
Write-Host ""

# --- 2. Create AWS IoT Thing ---

Write-Host ">>> Creating AWS IoT Thing..."
$thingResult = aws iot create-thing --thing-name $DeviceId 2>&1
if ($LASTEXITCODE -ne 0 -and ($thingResult -notmatch 'ResourceAlreadyExistsException')) {
  throw "create-thing failed: $thingResult"
}
Write-Host "Thing: $DeviceId"

# --- 3. Deactivate and detach existing certificates ---

Write-Host ">>> Deactivating existing certificates..."
$principals = aws iot list-thing-principals --thing-name $DeviceId |
  ConvertFrom-Json | Select-Object -ExpandProperty principals
foreach ($arn in $principals) {
  $certId = ($arn -replace '.*/cert/', '').Trim()
  aws iot detach-thing-principal --thing-name $DeviceId --principal $arn
  aws iot update-certificate --certificate-id $certId --new-status INACTIVE
  Write-Host "  Deactivated: $certId"
}
Write-Host "Done ($($principals.Count) certs)"

# --- 4. Issue and activate new certificate ---

Write-Host ">>> Issuing certificate..."
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$CertJson = aws iot create-keys-and-certificate --set-as-active | ConvertFrom-Json

Write-Host "  certPem length: $($CertJson.certificatePem.Length)"
if ($CertJson.certificatePem.Length -lt 100) { throw "certificatePem is empty or missing" }
$CertArn  = $CertJson.certificateArn

# --- 5. Attach policy and certificate to Thing ---

Write-Host ">>> Attaching policy..."
aws iot attach-policy --policy-name $PolicyName --target $CertArn
aws iot attach-thing-principal --thing-name $DeviceId --principal $CertArn
Write-Host "Policy and certificate attached"

# --- 6. Generate provision_config.h ---

Write-Host ">>> Generating provision_config.h..."

$ConfigPath = "$ProjectDir\src\provision_config.h"

$DevCertEsc = $CertJson.certificatePem.TrimEnd() -replace "`r?`n", '\n'
$DevKeyEsc  = $CertJson.keyPair.PrivateKey.TrimEnd() -replace "`r?`n", '\n'

# CRLF/LF を literal \n に変換（-replace の置換文字列で \\ → literal \）
# $DevCertEsc = ($CertJson.certificatePem.TrimEnd()       -replace "`r?`n", '\\n') + '\\n'
# $DevKeyEsc  = ($CertJson.keyPair.PrivateKey.TrimEnd()   -replace "`r?`n", '\\n') + '\\n'

# Write-Host "  cert length: $($DevCertEsc.Length), key length: $($DevKeyEsc.Length)"
# if ($DevCertEsc.Length -lt 100 -or $DevKeyEsc.Length -lt 100) {
#   throw "cert or key is unexpectedly short"
# }

# here-string の変数展開が不安定なため直接文字列連結で書き込む
$content  = "#pragma once`n"
$content += "// このファイルは provision_device.ps1 が生成します。リポジトリに含めないでください。`n`n"
$content += "static const char PROV_MQTT_HOST[]   = `"$MqttHost`";`n"
$content += "static const char PROV_DEVICE_CERT[] = `"$DevCertEsc`";`n"
$content += "static const char PROV_DEVICE_KEY[]  = `"$DevKeyEsc`";`n"
[System.IO.File]::WriteAllText($ConfigPath, $content, [System.Text.Encoding]::UTF8)

Write-Host "provision_config.h written"

# --- 7. Build and flash provisioning firmware ---

Write-Host ""
Write-Host ""
Write-Host "DEVICE_ID: $DeviceId"
Write-Host "PORT:      $Port"
$confirm = Read-Host ">>> Flash provisioning firmware to device? [y/N]"
if ($confirm -notmatch '^[yY]$') {
  Remove-Item $ConfigPath -Force -ErrorAction SilentlyContinue
  Write-Host "Aborted."
  exit 0
}

Write-Host ">>> Building and flashing provisioning firmware..."
Push-Location $ProjectDir
& $Pio run -e provision -t upload --upload-port $Port
$pioBuildResult = $LASTEXITCODE
Pop-Location

if ($pioBuildResult -ne 0) {
  Remove-Item $ConfigPath -Force -ErrorAction SilentlyContinue
  throw "Provisioning firmware build/flash failed"
}

# --- 8. Wait for provisioning firmware to run ---

Write-Host ""
Write-Host ">>> Waiting for provisioning firmware to complete..."
Start-Sleep -Seconds 10

# --- 9. Clean up provision_config.h (contains private key) ---

Remove-Item $ConfigPath -Force -ErrorAction SilentlyContinue
Write-Host "provision_config.h removed"

# --- Done ---

Write-Host ""
Write-Host "=== Provisioning complete ==="
Write-Host "DEVICE_ID: $DeviceId"
Write-Host "Please reboot the device and flash the main firmware"
