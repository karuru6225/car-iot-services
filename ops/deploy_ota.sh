#!/usr/bin/env bash
# deploy_ota.sh - OTA ファームウェアをビルド・アップロード・Job 作成
#
# 使い方:
#   ./deploy_ota.sh <VERSION> [THING_NAME]
#   例: ./deploy_ota.sh 1.2.0
#       ./deploy_ota.sh 1.2.0 esp32-gw-aabbccddeeff
#
# THING_NAME を省略すると esp32-gw-* に一致する全 Thing を対象にする
# ops/ ディレクトリで実行すること

set -euo pipefail

VERSION="${1:?使い方: $0 <VERSION> [THING_NAME]}"
THING_NAME="${2:-}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../esp32_iot_gateway" && pwd)"
BUILD_DIR="$PROJECT_DIR/.pio/build/esp32-s3-devkitc-1"
FIRMWARE_BIN="$BUILD_DIR/firmware.bin"

# ─── Terraform outputs から設定を取得 ────────────────────────────────────────

cd "$SCRIPT_DIR/../infra"
BUCKET=$(terraform output -raw firmware_bucket)
BASE_URL=$(terraform output -raw firmware_base_url)
REGION=$(terraform output -raw iot_endpoint | grep -o 'ap-[a-z0-9-]*' | head -1 || echo "ap-northeast-1")
cd "$PROJECT_DIR"

FIRMWARE_KEY="firmware/v${VERSION}.bin"
FIRMWARE_URL="${BASE_URL}/${FIRMWARE_KEY}"
JOB_DOC_KEY="jobs/v${VERSION}.json"
JOB_ID="ota-v${VERSION}"

echo "=== OTA デプロイ ==="
echo "VERSION:  $VERSION"
echo "BUCKET:   $BUCKET"
echo "JOB_ID:   $JOB_ID"
echo ""

# ─── 1. ビルド ────────────────────────────────────────────────────────────────

echo ">>> ファームウェアをビルド中..."
pio run
echo "ビルド完了: $FIRMWARE_BIN"

# ─── 2. firmware.bin を S3 にアップロード ────────────────────────────────────

echo ">>> firmware.bin を S3 にアップロード中..."
aws s3 cp "$FIRMWARE_BIN" "s3://${BUCKET}/${FIRMWARE_KEY}"
echo "アップロード完了: $FIRMWARE_URL"

# ─── 3. ジョブドキュメントを生成・アップロード ───────────────────────────────

echo ">>> ジョブドキュメントを生成中..."
TMP_JSON=$(mktemp /tmp/job_XXXX.json)
cat > "$TMP_JSON" <<EOF
{
  "operation": "ota",
  "version": "${VERSION}",
  "url": "${FIRMWARE_URL}"
}
EOF
aws s3 cp "$TMP_JSON" "s3://${BUCKET}/${JOB_DOC_KEY}"
rm -f "$TMP_JSON"
echo "ジョブドキュメントをアップロードしました"

# ─── 4. 対象 Thing を決定 ─────────────────────────────────────────────────────

if [ -n "$THING_NAME" ]; then
  ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
  TARGETS="arn:aws:iot:${REGION}:${ACCOUNT_ID}:thing/${THING_NAME}"
  echo "ターゲット: $TARGETS"
else
  echo ">>> esp32-gw-* に一致する Thing を検索中..."
  ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
  THINGS=$(aws iot list-things --query "things[?starts_with(thingName, 'esp32-gw-')].thingName" --output text)
  if [ -z "$THINGS" ]; then
    echo "エラー: esp32-gw-* に一致する Thing が見つかりません"
    exit 1
  fi
  echo "対象 Thing: $THINGS"
  TARGETS=$(echo "$THINGS" | tr '\t' '\n' | \
    sed "s|.*|arn:aws:iot:${REGION}:${ACCOUNT_ID}:thing/&|" | \
    paste -sd ',' -)
fi

# ─── 5. IoT Job を作成 ────────────────────────────────────────────────────────

echo ">>> IoT Job を作成中..."
DOC_URL="https://s3.${REGION}.amazonaws.com/${BUCKET}/${JOB_DOC_KEY}"
aws iot create-job \
  --job-id "$JOB_ID" \
  --targets "$TARGETS" \
  --document-source "$DOC_URL" \
  --timeout-config inProgressTimeoutInMinutes=90

echo ""
echo "=== デプロイ完了 ==="
echo "Job ID:       $JOB_ID"
echo "Firmware URL: $FIRMWARE_URL"
echo "デバイスの次回起動時に OTA が適用されます"
