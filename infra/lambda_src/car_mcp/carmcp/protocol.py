"""MCP（Model Context Protocol）のJSON-RPCメッセージ処理。

Streamable HTTPのうち、セッションを持たず（ステートレス）、応答をSSEではなく
application/jsonで返す形だけを実装する。サーバから能動的に送るもの（通知・サンプリング等）は
無く、道具を提供するだけなので、扱うのは initialize / ping / tools/list / tools/call と通知のみ。

公式SDK（Python の mcp パッケージ）を使わないのは、Lambdaのzipにpydantic_core等の
ネイティブ依存をLinux向けにビルドして同梱する必要が生じ、このリポジトリの他のLambdaと
同じarchive_fileだけのデプロイができなくなるため。上の範囲なら手書きでも小さく収まる。
仕様: https://modelcontextprotocol.io/specification/2025-11-25"""

from collections.abc import Callable
from dataclasses import dataclass, field

# 新しい順。クライアントが要求した版に対応していればそれを返し、
# 対応していなければ最新を返す（判断はクライアントに委ねる。仕様のバージョン交渉の規則）
SUPPORTED_PROTOCOL_VERSIONS = ("2025-11-25", "2025-06-18", "2025-03-26")

SERVER_INFO = {"name": "car", "version": "1.0.0"}

# JSON-RPCのエラーコード
PARSE_ERROR = -32700
INVALID_REQUEST = -32600
METHOD_NOT_FOUND = -32601
INVALID_PARAMS = -32602


class ToolError(Exception):
    """道具の実行に失敗したことを、会話モデルが読める文で伝えるための例外。
    JSON-RPCのエラーではなく、isError=trueの道具の結果として返す（仕様: 道具の実行エラーは
    モデルが見て対処できるよう結果に含める）。"""


@dataclass(frozen=True)
class Tool:
    name: str
    description: str
    properties: dict = field(default_factory=dict)  # 引数名 → JSON Schema
    run: Callable[..., str] = lambda **_: ""

    def definition(self) -> dict:
        return {
            "name": self.name,
            "description": self.description,
            "inputSchema": {
                "type": "object",
                "properties": self.properties,
                "required": list(self.properties),
                "additionalProperties": False,
            },
            "annotations": {
                "readOnlyHint": True,
                "destructiveHint": False,
                "idempotentHint": True,
                "openWorldHint": False,
            },
        }


def _result(msg_id, result: dict) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "result": result}


def error(msg_id, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": msg_id, "error": {"code": code, "message": message}}


def _tool_result(text: str, is_error: bool = False) -> dict:
    return {"content": [{"type": "text", "text": text}], "isError": is_error}


def _call_tool(tools: dict[str, Tool], params: dict) -> dict:
    tool = tools.get(params.get("name"))
    if tool is None:
        raise KeyError(params.get("name"))
    args = params.get("arguments") or {}
    if not isinstance(args, dict):
        return _tool_result("arguments はオブジェクトで渡す", is_error=True)

    missing = [name for name in tool.properties if name not in args]
    unknown = [name for name in args if name not in tool.properties]
    if missing or unknown:
        parts = []
        if missing:
            parts.append(f"必須の引数が無い: {', '.join(missing)}")
        if unknown:
            parts.append(f"知らない引数: {', '.join(unknown)}")
        return _tool_result("。".join(parts), is_error=True)
    wrong_type = [name for name, value in args.items() if not isinstance(value, str)]
    if wrong_type:
        return _tool_result(f"文字列で渡す引数: {', '.join(wrong_type)}", is_error=True)

    try:
        return _tool_result(tool.run(**args))
    except ToolError as e:
        return _tool_result(str(e), is_error=True)


def handle(message, tools: list[Tool], instructions: str) -> dict | None:
    """JSON-RPCメッセージ1件を処理し、応答を返す。通知（idの無いメッセージ）ならNone。"""
    if not isinstance(message, dict) or message.get("jsonrpc") != "2.0" or not isinstance(message.get("method"), str):
        return error(message.get("id") if isinstance(message, dict) else None, INVALID_REQUEST, "invalid JSON-RPC request")

    if "id" not in message:
        return None  # notifications/initialized など。応答しない

    msg_id = message["id"]
    method = message["method"]
    params = message.get("params") or {}
    if not isinstance(params, dict):
        return error(msg_id, INVALID_PARAMS, "params must be an object")

    if method == "initialize":
        requested = params.get("protocolVersion")
        version = requested if requested in SUPPORTED_PROTOCOL_VERSIONS else SUPPORTED_PROTOCOL_VERSIONS[0]
        return _result(msg_id, {
            "protocolVersion": version,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": SERVER_INFO,
            "instructions": instructions,
        })
    if method == "ping":
        return _result(msg_id, {})
    if method == "tools/list":
        return _result(msg_id, {"tools": [t.definition() for t in tools]})
    if method == "tools/call":
        try:
            return _result(msg_id, _call_tool({t.name: t for t in tools}, params))
        except KeyError:
            return error(msg_id, INVALID_PARAMS, f"unknown tool: {params.get('name')}")
    return error(msg_id, METHOD_NOT_FOUND, f"method not found: {method}")
