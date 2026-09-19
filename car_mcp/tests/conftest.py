import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from carmcp.config import Config  # noqa: E402


class FakeGateway:
    """AwsGatewayと同じメソッドを持つ偽物。S3はキー→本文のdict、Athenaは返す行を順に積んでおく。"""

    def __init__(self, objects: dict[str, str] | None = None, query_results: list[list[dict]] | None = None,
                 shadow: dict | None = None):
        self.objects = objects or {}
        self.query_results = list(query_results or [])
        self.shadow = shadow
        self.queries: list[str] = []

    def query(self, sql: str) -> list[dict]:
        self.queries.append(sql)
        return self.query_results.pop(0) if self.query_results else []

    def list_prefixes(self, prefix: str) -> list[str]:
        found = set()
        for key in self.objects:
            if key.startswith(prefix) and "/" in key[len(prefix):]:
                found.add(prefix + key[len(prefix):].split("/", 1)[0] + "/")
        return sorted(found)

    def list_keys(self, prefix: str) -> list[str]:
        return sorted(k for k in self.objects if k.startswith(prefix))

    def get_text(self, key: str) -> str | None:
        return self.objects.get(key)

    def get_shadow(self, thing_name: str) -> dict | None:
        return self.shadow


@pytest.fixture
def cfg() -> Config:
    return Config(
        device_id="esp32-gw-aabbccddeeff",
        obd_device_id="car-iot-aabbcc",
        s3_bucket="bucket",
        athena_database="iot_monitor",
        athena_workgroup="iot-monitor",
        iot_endpoint="https://example-ats.iot.ap-northeast-1.amazonaws.com",
        expose_location=False,
        token="t" * 32,
        allowed_hosts=("localhost:*", "127.0.0.1:*"),
        port=8000,
    )
