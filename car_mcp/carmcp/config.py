"""環境変数からの設定読み込み。必須項目が欠けていたら起動時に落とす
（道具が中途半端に動く状態でmnemosyneに繋がるより、起動失敗で気づける方がよい）。"""

import os
import re
from collections.abc import Mapping
from dataclasses import dataclass

# SQLに埋め込むため、英数字・ハイフン・アンダースコアだけを許す
# （trip_analysis/index.py・obd_ingest/index.pyと同じパターン）
_ID_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


@dataclass(frozen=True)
class Config:
    device_id: str  # IoT Thing名（esp32-gw-{MAC12桁}）。sensor_data・Shadowの識別子
    obd_device_id: str  # OBDデータ・トリップ分析の識別子（car-iot-{MAC上位6桁}）
    s3_bucket: str
    athena_database: str
    athena_workgroup: str
    iot_endpoint: str  # https://xxxx-ats.iot.{region}.amazonaws.com
    expose_location: bool  # トリップの開始/終了地点名を会話モデルへ渡すか（既定: 渡さない）
    token: str  # mnemosyneからの呼び出しを認証するBearerトークン
    allowed_hosts: tuple[str, ...]  # Hostヘッダの許可リスト（DNSリバインディング対策）
    port: int


def derive_obd_device_id(device_id: str) -> str:
    """IoT Thing名からOBDのdevice_idを導出する。モバイルアプリはESP32のBLEアドバタイズ名
    （car-iot-{MAC上位3バイト6桁}）をそのままOBDのdevice_idに使い、ESP32はThing名
    （esp32-gw-{MAC12桁}）の先頭6桁からそのBLE名を作る（web/trip-analysis.htmlの
    deriveObdDeviceIdと同じ規則）。"""
    mac = device_id.removeprefix("esp32-gw-")
    return f"car-iot-{mac[:6]}"


def _require(env: Mapping[str, str], name: str) -> str:
    value = env.get(name, "").strip()
    if not value:
        raise RuntimeError(f"環境変数 {name} が設定されていない")
    return value


def _require_id(env: Mapping[str, str], name: str, value: str) -> str:
    if not _ID_RE.match(value):
        raise RuntimeError(f"環境変数 {name} に使えない文字が含まれている: {value!r}")
    return value


def load_config(env: Mapping[str, str] = os.environ) -> Config:
    device_id = _require_id(env, "CAR_DEVICE_ID", _require(env, "CAR_DEVICE_ID"))
    obd_device_id = env.get("CAR_OBD_DEVICE_ID", "").strip() or derive_obd_device_id(device_id)
    _require_id(env, "CAR_OBD_DEVICE_ID", obd_device_id)

    token = _require(env, "CAR_MCP_TOKEN")
    if len(token) < 32:
        raise RuntimeError("CAR_MCP_TOKEN は32文字以上にする（推測されにくい長さを保証するため）")

    iot_endpoint = _require(env, "CAR_IOT_ENDPOINT")
    if not iot_endpoint.startswith("https://"):
        iot_endpoint = f"https://{iot_endpoint}"

    hosts = env.get("CAR_MCP_ALLOWED_HOSTS", "car-mcp:*,localhost:*,127.0.0.1:*")

    return Config(
        device_id=device_id,
        obd_device_id=obd_device_id,
        s3_bucket=_require(env, "CAR_S3_BUCKET"),
        athena_database=_require_id(env, "CAR_ATHENA_DATABASE", env.get("CAR_ATHENA_DATABASE", "iot_monitor")),
        athena_workgroup=env.get("CAR_ATHENA_WORKGROUP", "iot-monitor"),
        iot_endpoint=iot_endpoint,
        expose_location=env.get("CAR_EXPOSE_LOCATION", "false").strip().lower() == "true",
        token=token,
        allowed_hosts=tuple(h.strip() for h in hosts.split(",") if h.strip()),
        port=int(env.get("CAR_MCP_PORT", "8000")),
    )
