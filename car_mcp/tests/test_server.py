"""実際にuvicornで立ててHTTPで叩く。認証・Hostの検査・道具の一覧と呼び出しが
mnemosyneから繋いだときと同じ経路で通ることを確かめる。"""

import json
import socket
import threading
import time
import urllib.error
import urllib.request

import pytest
import uvicorn

from carmcp.server import build_app
from conftest import FakeGateway

TOKEN = "t" * 32
EXPECTED_TOOLS = {"car_status", "car_battery_history", "car_trips", "car_trip_detail"}


@pytest.fixture(scope="module")
def base_url(request):
    from carmcp.config import Config

    cfg = Config(
        device_id="esp32-gw-aabbccddeeff", obd_device_id="car-iot-aabbcc", s3_bucket="b",
        athena_database="iot_monitor", athena_workgroup="iot-monitor", iot_endpoint="https://x",
        expose_location=False, token=TOKEN, allowed_hosts=("127.0.0.1:*",), port=0,
    )
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    server = uvicorn.Server(uvicorn.Config(build_app(cfg, FakeGateway()), host="127.0.0.1", port=port, log_level="warning"))
    thread = threading.Thread(target=server.run, daemon=True)
    thread.start()
    deadline = time.time() + 10
    while not server.started:
        if time.time() > deadline:
            raise RuntimeError("uvicornが起動しなかった")
        time.sleep(0.05)
    yield f"http://127.0.0.1:{port}"
    server.should_exit = True
    thread.join(timeout=5)


def _post(url, payload, token=TOKEN, host=None):
    headers = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
               "MCP-Protocol-Version": "2025-06-18"}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if host:
        headers["Host"] = host
    req = urllib.request.Request(f"{url}/mcp", data=json.dumps(payload).encode(), headers=headers, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, json.loads(resp.read() or b"null")
    except urllib.error.HTTPError as e:
        return e.code, e.read()


def _rpc(method, params=None, id_=1):
    return {"jsonrpc": "2.0", "id": id_, "method": method, "params": params or {}}


def test_healthz_は認証なしで応答する(base_url):
    with urllib.request.urlopen(f"{base_url}/healthz", timeout=5) as resp:
        assert resp.status == 200


@pytest.mark.parametrize("token", [None, "wrong-token-wrong-token-wrong-tok"])
def test_トークンが無いか違えば401(base_url, token):
    status, _ = _post(base_url, _rpc("tools/list"), token=token)
    assert status == 401


def test_許可していないHostは拒否する(base_url):
    status, _ = _post(base_url, _rpc("tools/list"), host="evil.example:8000")
    assert status == 421


def test_道具は読み取り専用の4つだけ(base_url):
    init = {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "test", "version": "0"}}
    status, body = _post(base_url, _rpc("initialize", init))
    assert status == 200, body

    status, body = _post(base_url, _rpc("tools/list", id_=2))
    assert status == 200, body
    tools = {t["name"]: t for t in body["result"]["tools"]}
    assert set(tools) == EXPECTED_TOOLS
    assert all(t["annotations"]["readOnlyHint"] for t in tools.values())
    assert set(tools["car_trips"]["inputSchema"]["required"]) == {"start_date", "end_date"}


def test_入力の誤りは会話モデルが読める文のエラーで返る(base_url):
    params = {"name": "car_trips", "arguments": {"start_date": "2026/09/01", "end_date": "2026-09-02"}}
    status, body = _post(base_url, _rpc("tools/call", params, id_=3))
    assert status == 200, body
    result = body["result"]
    assert result["isError"] is True
    assert "YYYY-MM-DD" in result["content"][0]["text"]


def test_AWSの失敗も理由が分かる文で返る(base_url, monkeypatch):
    from carmcp import battery
    from carmcp.aws import AthenaError

    def boom(*_args, **_kwargs):
        raise AthenaError("Athenaのクエリが60秒で終わらなかった")

    monkeypatch.setattr(battery, "car_status", boom)
    status, body = _post(base_url, _rpc("tools/call", {"name": "car_status", "arguments": {}}, id_=4))
    assert status == 200, body
    assert body["result"]["isError"] is True
    assert "車のデータを取得できなかった" in body["result"]["content"][0]["text"]
