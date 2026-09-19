"""API Gatewayから来るイベントでhandlerを呼び、MCPのやり取りと認証の確認を通しで確かめる。"""

import base64
import json

import pytest

from carmcp.aws import AthenaError

EXPECTED_TOOLS = {"car_status", "car_battery_history", "car_trips", "car_trip_detail"}


def _event(body=None, method="POST", scopes=("car-mcp/read",), b64=False):
    raw = "" if body is None else json.dumps(body)
    event = {
        "requestContext": {"http": {"method": method}},
        "body": base64.b64encode(raw.encode()).decode() if b64 else raw,
        "isBase64Encoded": b64,
    }
    if scopes is not None:
        event["requestContext"]["authorizer"] = {"jwt": {"claims": {}, "scopes": list(scopes)}}
    return event


def _rpc(method, params=None, id_=1):
    return {"jsonrpc": "2.0", "id": id_, "method": method, "params": params or {}}


def _call(module, event):
    resp = module.handler(event, None)
    return resp["statusCode"], (json.loads(resp["body"]) if resp["body"] else None)


@pytest.mark.parametrize("scopes", [None, (), ("aws.cognito.signin.user.admin",)])
def test_スコープが無ければ403(car_mcp, scopes):
    status, _ = _call(car_mcp, _event(_rpc("tools/list"), scopes=scopes))
    assert status == 403


def test_スコープはclaimsのscopeからも読む(car_mcp):
    event = _event(_rpc("ping"), scopes=None)
    event["requestContext"]["authorizer"] = {"jwt": {"claims": {"scope": "car-mcp/read"}}}
    assert _call(car_mcp, event)[0] == 200


@pytest.mark.parametrize("method", ["GET", "DELETE"])
def test_POST以外は405(car_mcp, method):
    resp = car_mcp.handler(_event(method=method), None)
    assert resp["statusCode"] == 405
    assert resp["headers"]["Allow"] == "POST"


@pytest.mark.parametrize(("requested", "expected"), [("2025-11-25", "2025-11-25"), ("2025-06-18", "2025-06-18"),
                                                     ("2099-01-01", "2025-11-25")])
def test_initializeはプロトコル版を交渉する(car_mcp, requested, expected):
    params = {"protocolVersion": requested, "capabilities": {}, "clientInfo": {"name": "t", "version": "0"}}
    status, body = _call(car_mcp, _event(_rpc("initialize", params)))
    assert status == 200
    assert body["result"]["protocolVersion"] == expected
    assert body["result"]["capabilities"] == {"tools": {"listChanged": False}}
    assert "日本時間" in body["result"]["instructions"]


def test_通知には202で本文なし(car_mcp):
    status, body = _call(car_mcp, _event({"jsonrpc": "2.0", "method": "notifications/initialized"}))
    assert (status, body) == (202, None)


def test_base64の本文も受け付ける(car_mcp):
    assert _call(car_mcp, _event(_rpc("ping"), b64=True)) == (200, {"jsonrpc": "2.0", "id": 1, "result": {}})


def test_道具は読み取り専用の4つだけ(car_mcp):
    _, body = _call(car_mcp, _event(_rpc("tools/list")))
    tools = {t["name"]: t for t in body["result"]["tools"]}
    assert set(tools) == EXPECTED_TOOLS
    assert all(t["annotations"]["readOnlyHint"] and not t["annotations"]["destructiveHint"] for t in tools.values())
    assert tools["car_trips"]["inputSchema"]["required"] == ["start_date", "end_date"]
    assert tools["car_status"]["inputSchema"]["properties"] == {}


def test_道具を呼ぶと単位付きの日本語で返る(car_mcp):
    car_mcp.gw.query_results = [[{"ts": "2026-09-19T02:55:00Z", "main": 12.45, "sub": 13.1, "current": -0.07,
                                  "power": -0.9, "temp": 31.2, "ah": 210.5}]]
    _, body = _call(car_mcp, _event(_rpc("tools/call", {"name": "car_status", "arguments": {}})))
    result = body["result"]
    assert result["isError"] is False
    status = json.loads(result["content"][0]["text"])
    assert status["最新の計測"]["メインバッテリー電圧（車両の始動用バッテリー）"] == "12.45 V"


@pytest.mark.parametrize(
    ("arguments", "message"),
    [
        ({"start_date": "2026/09/01", "end_date": "2026-09-02"}, "YYYY-MM-DD"),
        ({"start_date": "2026-09-01"}, "必須の引数が無い: end_date"),
        ({"start_date": "2026-09-01", "end_date": "2026-09-02", "limit": "3"}, "知らない引数: limit"),
        ({"start_date": 20260901, "end_date": "2026-09-02"}, "文字列で渡す引数: start_date"),
    ],
)
def test_引数の誤りは会話モデルが読める文で返る(car_mcp, arguments, message):
    _, body = _call(car_mcp, _event(_rpc("tools/call", {"name": "car_trips", "arguments": arguments})))
    assert body["result"]["isError"] is True
    assert message in body["result"]["content"][0]["text"]


def test_AWSの失敗も理由が分かる文で返る(car_mcp):
    def boom(_sql):
        raise AthenaError("Athenaのクエリが20秒で終わらなかった")

    car_mcp.gw.query = boom
    _, body = _call(car_mcp, _event(_rpc("tools/call", {"name": "car_status", "arguments": {}})))
    assert body["result"]["isError"] is True
    assert "車のデータを取得できなかった（AWSへのアクセスに失敗: AthenaError）" in body["result"]["content"][0]["text"]


def test_知らない道具とメソッドはJSON_RPCのエラー(car_mcp):
    _, body = _call(car_mcp, _event(_rpc("tools/call", {"name": "car_shadow_update", "arguments": {}})))
    assert body["error"]["code"] == -32602
    _, body = _call(car_mcp, _event(_rpc("resources/list")))
    assert body["error"]["code"] == -32601


@pytest.mark.parametrize("raw", ["{not json", json.dumps([{"jsonrpc": "2.0", "id": 1, "method": "ping"}])])
def test_壊れた本文とバッチは400(car_mcp, raw):
    event = _event()
    event["body"] = raw
    assert car_mcp.handler(event, None)["statusCode"] == 400
