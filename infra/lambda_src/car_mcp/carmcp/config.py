"""環境変数（infra/car_mcp.tfで設定）からの設定読み込み。"""

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


def _require_id(name: str, value: str) -> str:
    if not _ID_RE.match(value):
        raise RuntimeError(f"環境変数 {name} に使えない文字が含まれている: {value!r}")
    return value


def load_config(env: Mapping[str, str] = os.environ) -> Config:
    device_id = _require_id("CAR_DEVICE_ID", _require(env, "CAR_DEVICE_ID"))
    return Config(
        device_id=device_id,
        obd_device_id=_require_id("CAR_DEVICE_ID", derive_obd_device_id(device_id)),
        s3_bucket=_require(env, "S3_BUCKET"),
        athena_database=_require_id("ATHENA_DATABASE", _require(env, "ATHENA_DATABASE")),
        athena_workgroup=_require(env, "ATHENA_WORKGROUP"),
        iot_endpoint=_require(env, "IOT_ENDPOINT"),
        expose_location=env.get("EXPOSE_LOCATION", "false").strip().lower() == "true",
    )
