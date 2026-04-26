#!/usr/bin/env bash
# provision_device.sh - ESP32 デバイスの初回プロビジョニング
#
# 使い方:
#   ./provision_device.sh <PORT>
#   例: ./provision_device.sh COM3
#
# 必要なもの:
#   - AWS CLI（設定済み）
#   - PlatformIO（pio コマンド）
#   - esp32_iot_gateway ディレクトリで実行すること

set -euo pipefail

PORT="${1:?使い方: $0 <PORT>}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../esp32_iot_gateway" && pwd)"
PYTHON="$HOME/.platformio/penv/Scripts/python.exe"

# ─── Terraform outputs から設定を取得 ────────────────────────────────────────

cd "$SCRIPT_DIR"
MQTT_HOST=$(terraform output -raw iot_endpoint)
POLICY_NAME=$(terraform output -raw iot_policy_name)
cd "$PROJECT_DIR"

echo "=== ESP32 プロビジョニング ==="
echo "PORT:        $PORT"
echo "MQTT_HOST:   $MQTT_HOST"
echo "POLICY_NAME: $POLICY_NAME"
echo ""

# ─── 1. MAC アドレスから device ID を生成 ─────────────────────────────────────

echo ">>> MAC アドレスを読み取り中..."
MAC_RAW=$("$PYTHON" -m esptool --port "$PORT" read_mac 2>&1 | grep "MAC:" | awk '{print $2}')
if [ -z "$MAC_RAW" ]; then
  echo "エラー: MAC アドレスの読み取りに失敗しました"
  exit 1
fi

DEVICE_ID="esp32-gw-$(echo "$MAC_RAW" | tr -d ':')"
echo "DEVICE_ID:   $DEVICE_ID"
echo ""

# ─── 2. AWS IoT Thing を作成 ──────────────────────────────────────────────────

echo ">>> AWS IoT Thing を作成中..."
aws iot create-thing --thing-name "$DEVICE_ID" > /dev/null
echo "Thing 作成: $DEVICE_ID"

# ─── 3. 証明書を発行・有効化 ──────────────────────────────────────────────────

echo ">>> 証明書を発行中..."
mkdir -p "$PROJECT_DIR/data/certs"
CERT_JSON=$(aws iot create-keys-and-certificate --set-as-active)
CERT_ARN=$(echo "$CERT_JSON" | python3 -c "import sys,json; print(json.load(sys.stdin)['certificateArn'])")
echo "$CERT_JSON" | python3 -c "import sys,json; print(json.load(sys.stdin)['certificatePem'])"        > "$PROJECT_DIR/data/certs/device.crt"
echo "$CERT_JSON" | python3 -c "import sys,json; print(json.load(sys.stdin)['keyPair']['PrivateKey'])" > "$PROJECT_DIR/data/certs/device.key"
echo "証明書 ARN: $CERT_ARN"

# ─── 4. Amazon Root CA を取得 ────────────────────────────────────────────────

echo ">>> Amazon Root CA を取得中..."
curl -sf "https://www.amazontrust.com/repository/AmazonRootCA1.pem" \
  -o "$PROJECT_DIR/data/certs/ca.crt"
echo "ca.crt を取得しました"

# ─── 5. ポリシーと証明書を Thing にアタッチ ───────────────────────────────────

echo ">>> ポリシーをアタッチ中..."
aws iot attach-policy --policy-name "$POLICY_NAME" --target "$CERT_ARN"
aws iot attach-thing-principal --thing-name "$DEVICE_ID" --principal "$CERT_ARN"
echo "ポリシー・証明書をアタッチしました"

# ─── 6. SPIFFS に証明書を書き込む ────────────────────────────────────────────

echo ">>> SPIFFS に証明書を書き込み中..."
cd "$PROJECT_DIR"
pio run -t uploadfs --upload-port "$PORT"
echo "SPIFFS 書き込み完了"

# ─── 7. NVS に mqtt_host を書き込む ──────────────────────────────────────────

echo ">>> NVS に mqtt_host を書き込み中..."
NVS_PARTITION_GEN="$HOME/.platformio/packages/framework-espidf/tools/nvs_flash/nvs_partition_generator/nvs_partition_generator.py"
NVS_SIZE=0x5000  # partitions_two_ota.csv の nvs サイズ
TMP_CSV=$(mktemp /tmp/nvs_XXXX.csv)
TMP_BIN=$(mktemp /tmp/nvs_XXXX.bin)

cat > "$TMP_CSV" <<EOF
key,type,encoding,value
device,namespace,,
mqtt_host,data,string,$MQTT_HOST
EOF

"$PYTHON" "$NVS_PARTITION_GEN" generate "$TMP_CSV" "$TMP_BIN" $NVS_SIZE
"$PYTHON" -m esptool --port "$PORT" write_flash 0x9000 "$TMP_BIN"
rm -f "$TMP_CSV" "$TMP_BIN"
echo "NVS 書き込み完了"

# ─── 8. 一時ファイルを削除 ────────────────────────────────────────────────────

echo ">>> 証明書ファイルを削除中..."
rm -f "$PROJECT_DIR/data/certs/device.crt" \
      "$PROJECT_DIR/data/certs/device.key" \
      "$PROJECT_DIR/data/certs/ca.crt"
echo "証明書ファイルを削除しました（SPIFFS に書き込み済み）"

# ─── 完了 ─────────────────────────────────────────────────────────────────────

echo ""
echo "=== プロビジョニング完了 ==="
echo "DEVICE_ID: $DEVICE_ID"
echo "デバイスを再起動してください"
