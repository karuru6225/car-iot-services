# provision_ha_bridge.ps1 - Provision AWS IoT credentials for the
# home_assistant repo's car_iot_bridge (subscribe-only local MQTT bridge).
#
# Usage:
#   .\provision_ha_bridge.ps1
#   .\provision_ha_bridge.ps1 -Profile myprofile
#   .\provision_ha_bridge.ps1 -OutDir "D:\other\path\certs"
#
# Requirements:
#   - AWS CLI (configured)
#   - terraform apply already run (needs aws_iot_policy.ha_bridge from iot.tf
#     and the iot_endpoint / iot_policy_name_ha_bridge outputs)
#   - Run from ops/ directory
#
# What it does:
#   - Creates (or reuses) an IoT Thing named "ha-bridge"
#   - Deactivates/detaches any existing certificates on it
#   - Issues a new certificate, attaches the ha_bridge policy (subscribe-only)
#   - Writes AmazonRootCA1.pem / device.pem.crt / private.pem.key into
#     ../../home_assistant/home_assistant/car_iot_bridge/certs/
#     (sibling repo; override with -OutDir if your layout differs)
#
# NOTE: this only prepares files on THIS machine. If home_assistant's
# docker-compose actually runs on the NAS, you still need to copy the
# certs/ directory (and fill in .env) over there yourself.

param(
  [string]$Profile = '',
  [string]$OutDir = ''
)

$ErrorActionPreference = "Stop"

if ($Profile) {
  $env:AWS_PROFILE = $Profile
  Write-Host "AWS profile: $Profile"

  $credEnv = aws configure export-credentials --profile $Profile --format powershell
  if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to get credentials. Run 'aws login' first and try again."
    exit 1
  }
  Invoke-Expression ($credEnv -join "`n")
}

$ScriptDir = $PSScriptRoot
$ThingName = "ha-bridge"

if (-not $OutDir) {
  $OutDir = Join-Path $ScriptDir "..\..\home_assistant\home_assistant\car_iot_bridge\certs"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
Write-Host "Output directory: $OutDir"

# --- Get config from Terraform outputs ---

Push-Location "$ScriptDir\..\infra"
$MqttHost = terraform output -raw iot_endpoint
if ($LASTEXITCODE -ne 0) { Pop-Location; Write-Error "Failed to read iot_endpoint output."; exit 1 }
$PolicyName = terraform output -raw iot_policy_name_ha_bridge
if ($LASTEXITCODE -ne 0) { Pop-Location; Write-Error "Failed to read iot_policy_name_ha_bridge output. Did you run terraform apply after adding aws_iot_policy.ha_bridge?"; exit 1 }
Pop-Location

Write-Host "=== ha-bridge Provisioning ==="
Write-Host "THING_NAME:  $ThingName"
Write-Host "MQTT_HOST:   $MqttHost"
Write-Host "POLICY_NAME: $PolicyName"
Write-Host ""

# --- 1. Create AWS IoT Thing (idempotent) ---

Write-Host ">>> Creating AWS IoT Thing..."
$thingResult = aws iot create-thing --thing-name $ThingName 2>&1
if ($LASTEXITCODE -ne 0 -and ($thingResult -notmatch 'ResourceAlreadyExistsException')) {
  throw "create-thing failed: $thingResult"
}
Write-Host "Thing: $ThingName"

# --- 2. Deactivate and detach existing certificates ---

Write-Host ">>> Deactivating existing certificates..."
$principals = aws iot list-thing-principals --thing-name $ThingName |
  ConvertFrom-Json | Select-Object -ExpandProperty principals
foreach ($arn in $principals) {
  $certId = ($arn -replace '.*/cert/', '').Trim()
  aws iot detach-thing-principal --thing-name $ThingName --principal $arn
  aws iot update-certificate --certificate-id $certId --new-status INACTIVE
  Write-Host "  Deactivated: $certId"
}
Write-Host "Done ($($principals.Count) certs)"

# --- 3. Issue and activate new certificate ---

Write-Host ">>> Issuing certificate..."
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$CertJson = aws iot create-keys-and-certificate --set-as-active | ConvertFrom-Json

if ($CertJson.certificatePem.Length -lt 100) { throw "certificatePem is empty or missing" }
$CertArn = $CertJson.certificateArn

# --- 4. Attach policy and certificate to Thing ---

Write-Host ">>> Attaching policy..."
aws iot attach-policy --policy-name $PolicyName --target $CertArn
aws iot attach-thing-principal --thing-name $ThingName --principal $CertArn
Write-Host "Policy and certificate attached"

# --- 5. Write cert/key/CA files for car_iot_bridge ---

Write-Host ">>> Writing certificate files..."

# Amazon Root CA 1 (same one embedded in gen_certs.ps1)
$RootCa = 'MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF' + [Environment]::NewLine +
          'ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6' + [Environment]::NewLine +
          'b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL' + [Environment]::NewLine +
          'MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv' + [Environment]::NewLine +
          'b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj' + [Environment]::NewLine +
          'ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM' + [Environment]::NewLine +
          '9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw' + [Environment]::NewLine +
          'IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6' + [Environment]::NewLine +
          'VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L' + [Environment]::NewLine +
          '93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm' + [Environment]::NewLine +
          'jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC' + [Environment]::NewLine +
          'AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA' + [Environment]::NewLine +
          'A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI' + [Environment]::NewLine +
          'U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs' + [Environment]::NewLine +
          'N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv' + [Environment]::NewLine +
          'o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU' + [Environment]::NewLine +
          '5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy' + [Environment]::NewLine +
          'rqXRfboQnoZsG4q5WTP468SQvvG5'

$RootCaPem = "-----BEGIN CERTIFICATE-----`n$RootCa`n-----END CERTIFICATE-----`n"

Set-Content -Path (Join-Path $OutDir "AmazonRootCA1.pem") -Value $RootCaPem -NoNewline -Encoding ascii
Set-Content -Path (Join-Path $OutDir "device.pem.crt") -Value $CertJson.certificatePem -NoNewline -Encoding ascii
Set-Content -Path (Join-Path $OutDir "private.pem.key") -Value $CertJson.keyPair.PrivateKey -NoNewline -Encoding ascii

Write-Host "Written: $OutDir\AmazonRootCA1.pem"
Write-Host "Written: $OutDir\device.pem.crt"
Write-Host "Written: $OutDir\private.pem.key (KEEP SECRET - already gitignored)"

# --- Done ---

Write-Host ""
Write-Host "=== Provisioning complete ==="
Write-Host "Set CAR_IOT_AWS_ENDPOINT=$MqttHost in home_assistant/.env"
Write-Host "If the docker-compose actually runs on the NAS (not this machine)," 
Write-Host "copy $OutDir there too, and set CAR_IOT_AWS_ENDPOINT / CAR_IOT_MQTT_LOCAL_PASSWORD in its .env."
