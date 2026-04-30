#Requires -Version 5.1
param(
  [string]$Profile = ''
)

# Usage:
#   .\gen_certs.ps1
#   .\gen_certs.ps1 -Profile myprofile
#
# Run after terraform apply.
# Fetches certificate and private key from Secrets Manager
# and generates m5atom_iot_gateway/src/certs.h

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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

$InfraDir    = Resolve-Path "$PSScriptRoot\..\infra"
$BackendFile = "$InfraDir\backend.tfbackend"
$TfvarsFile  = "$InfraDir\terraform.tfvars"

if (-not (Test-Path $BackendFile)) {
  Write-Error "backend.tfbackend not found: $BackendFile"
  exit 1
}
if (-not (Test-Path $TfvarsFile)) {
  Write-Error "terraform.tfvars not found: $TfvarsFile"
  exit 1
}

Push-Location $InfraDir
Write-Host '==> terraform init'
terraform init -backend-config $BackendFile -reconfigure | Out-Null
if ($LASTEXITCODE -ne 0) { Pop-Location; exit $LASTEXITCODE }

Write-Host '==> Getting terraform outputs'
$MqttHost  = terraform output -raw iot_endpoint
$SecretArn = terraform output -raw secrets_manager_arn
Pop-Location

if (-not $MqttHost -or -not $SecretArn) {
  Write-Error "Failed to get terraform outputs. Make sure terraform apply has completed."
  exit 1
}

$DeviceId = 'iot-monitor-gw-001'
$TfvarsContent = Get-Content $TfvarsFile -Raw
if ($TfvarsContent -match 'device_id\s*=\s*"([^"]+)"') {
  $DeviceId = $Matches[1]
}

Write-Host '==> Fetching certificate from Secrets Manager'
$SecretJson = aws secretsmanager get-secret-value `
  --secret-id $SecretArn `
  --query SecretString `
  --output text
if ($LASTEXITCODE -ne 0) {
  Write-Error "Failed to get secret from Secrets Manager."
  exit 1
}

$Secret  = $SecretJson | ConvertFrom-Json
$CertPem = $Secret.certificate_pem.Trim()
$PrivKey = $Secret.private_key.Trim()

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

$Lines = @(
  '#pragma once'
  '// AWS IoT Core certificate'
  '// Regenerate with: ops/gen_certs.ps1 (after terraform apply)'
  ''
  "#define MQTT_HOST `"$MqttHost`""
  '#define MQTT_PORT 8883'
  "#define MQTT_CLIENT_ID `"$DeviceId`""
  "#define MQTT_TOPIC_DATA `"sensors/$DeviceId/data`""
  ''
  'static const char AWS_ROOT_CA[] PROGMEM = R"EOF(-----BEGIN CERTIFICATE-----'
  $RootCa
  '-----END CERTIFICATE-----)EOF";'
  ''
  'static const char DEVICE_CERT[] PROGMEM = R"EOF('
  $CertPem
  ')EOF";'
  ''
  'static const char DEVICE_KEY[] PROGMEM = R"EOF('
  $PrivKey
  ')EOF";'
)

$OutPath = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\m5atom_iot_gateway\src\certs.h'))
[System.IO.File]::WriteAllLines($OutPath, $Lines, [System.Text.UTF8Encoding]::new($false))
Write-Host "==> Generated: $OutPath"
