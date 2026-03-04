# Step 1: IoT Core -> Lambda(ingest) -> S3 test
# Run from infra/ directory

$REGION    = "ap-northeast-1"
$DEVICE_ID = "iot-monitor-gw-001"
$TOPIC     = "sensors/$DEVICE_ID/data"
$S3_BUCKET = (terraform output -raw s3_bucket)
$IOT_EP    = (terraform output -raw iot_endpoint)
$TODAY     = (Get-Date -Format "yyyy/MM/dd")

Write-Host "======================================"
Write-Host " Step 1: IoT Core -> Lambda -> S3 test"
Write-Host "======================================"
Write-Host "Device ID : $DEVICE_ID"
Write-Host "Topic     : $TOPIC"
Write-Host "S3 Bucket : $S3_BUCKET"
Write-Host "IoT EP    : $IOT_EP"
Write-Host ""

# --- voltage ---
Write-Host "[1/2] Sending voltage data..."
$p1 = '{"voltage":12.34,"id":"voltage_1"}'
aws iot-data publish --topic $TOPIC --payload $p1 --cli-binary-format raw-in-base64-out --endpoint-url "https://$IOT_EP" --region $REGION
Write-Host "      -> done"

Start-Sleep -Seconds 1

# --- SwitchBot ---
Write-Host "[2/2] Sending SwitchBot data..."
$p2 = '{"addr":"AA:BB:CC:DD:EE:FF","temp":25.1,"humidity":60,"battery":80,"rssi":-70}'
aws iot-data publish --topic $TOPIC --payload $p2 --cli-binary-format raw-in-base64-out --endpoint-url "https://$IOT_EP" --region $REGION
Write-Host "      -> done"

# --- S3 check ---
Write-Host ""
Write-Host "Waiting for S3 arrival (5s)..."
Start-Sleep -Seconds 5

Write-Host ""
Write-Host "=== Files saved in S3 (today) ==="
$files = aws s3 ls "s3://$S3_BUCKET/raw/" --recursive --region $REGION | Select-String $TODAY
if ($files) {
    $files | ForEach-Object { Write-Host $_ }
} else {
    Write-Host "  (no files found)"
}

Write-Host ""
Write-Host "--------------------------------------"
Write-Host " OK if files are listed above."
Write-Host " If not, check CloudWatch Logs:"
Write-Host "   /aws/lambda/iot-monitor-ingest"
Write-Host "   /aws/iot/iot-monitor/rule-errors"
Write-Host "--------------------------------------"
