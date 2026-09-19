"""MCPサーバの組み立てと起動。

- トランスポート: Streamable HTTP（/mcp）。mnemosyneのorchestratorは
  @modelcontextprotocol/sdkのStreamableHTTPClientTransportで繋ぐ
- 認証: Authorization: Bearer <CAR_MCP_TOKEN>。/healthz だけは認証なし（composeのhealthcheck用）
- ステートレス: サーバが再起動してもmnemosyne側のセッションが無効にならないよう、
  セッションを持たないモードで動かす（mnemosyneは接続時に一度だけ道具の一覧を取る）

道具は読み取り専用のものだけを置く。車に作用する道具（Shadowの書き換え・コマンド送信）は
許可リストで隠すのではなく、そもそも作らない（HANDOFF_mnemosyne.md 1章）。
道具の名前はmnemosyne側の許可リストにそのまま書かれるので、一度決めたら変えない。"""

import hmac
import json
import logging
from collections.abc import Callable

import uvicorn
from botocore.exceptions import BotoCoreError, ClientError
from mcp.server.mcpserver import MCPServer
from mcp.server.mcpserver.exceptions import ToolError
from mcp.server.transport_security import TransportSecuritySettings
from mcp.types import ToolAnnotations
from starlette.types import ASGIApp, Receive, Scope, Send

from . import battery, trips
from .aws import AthenaError, AwsGateway
from .config import Config, load_config

logger = logging.getLogger("carmcp")

INSTRUCTIONS = (
    "車（メイン/サブの2系統のバッテリーを持つ車両）の計測データを読むための道具。"
    "バッテリーの計測値は車載装置がLTE経由で通常5分ごとに送る。"
    "トリップ（走行記録）はスマートフォンが車内にあるときだけOBD-II経由で記録される。"
    "時刻はすべて日本時間（+09:00）で返す。"
)

_READ_ONLY = ToolAnnotations(readOnlyHint=True, destructiveHint=False, idempotentHint=True, openWorldHint=False)


def _to_text(result: dict) -> str:
    return json.dumps(result, ensure_ascii=False, indent=1)


def _run(fn: Callable[[], dict]) -> str:
    """道具の本体を呼び、入力の誤りとデータ取得の失敗は会話モデルが読める文でエラーとして返す
    （SDKの既定では想定外の例外は "Error executing tool ..." だけになり、理由が伝わらない）。"""
    try:
        return _to_text(fn())
    except ValueError as e:
        raise ToolError(str(e)) from e
    except (AthenaError, BotoCoreError, ClientError) as e:
        logger.exception("AWSからのデータ取得に失敗")
        raise ToolError(f"車のデータを取得できなかった（AWSへのアクセスに失敗: {type(e).__name__}）。時間をおいて再度試す") from e


def build_server(cfg: Config, gw: AwsGateway) -> MCPServer:
    server = MCPServer(name="car", instructions=INSTRUCTIONS)

    @server.tool(annotations=_READ_ONLY, structured_output=False)
    def car_status() -> str:
        """車のバッテリーの現在の状態を返す。メイン/サブバッテリーの電圧、サブバッテリーの電流・積算電荷量、
        最終受信時刻と経過時間、エンジン稼働中か駐車中かの推定、直近の推移、充電リレーなど装置の設定。"""
        return _run(lambda: battery.car_status(gw, cfg))

    @server.tool(annotations=_READ_ONLY, structured_output=False)
    def car_battery_history(start_date: str, end_date: str) -> str:
        """期間を指定して、日ごとのバッテリーの記録を返す。メイン/サブバッテリーの電圧の最低・最高、
        サブバッテリーの充電量・放電量（Ah）、受信件数。受信が無かった日はそうと分かる形で返す。

        Args:
            start_date: 開始日（日本時間の暦日、YYYY-MM-DD）
            end_date: 終了日（日本時間の暦日、YYYY-MM-DD、この日を含む）。期間は最大92日
        """
        return _run(lambda: battery.car_battery_history(gw, cfg, start_date, end_date))

    @server.tool(annotations=_READ_ONLY, structured_output=False)
    def car_trips(start_date: str, end_date: str) -> str:
        """期間を指定して、走行記録（トリップ）の一覧を返す。出発/到着時刻、所要時間、走行距離、燃料消費、燃費。
        記録はスマートフォンが車内にあった走行だけなので、一覧に無いことは走っていないことを意味しない。

        Args:
            start_date: 開始日（日本時間の暦日、YYYY-MM-DD）
            end_date: 終了日（日本時間の暦日、YYYY-MM-DD、この日を含む）。期間は最大92日
        """
        return _run(lambda: trips.car_trips(gw, cfg, start_date, end_date))

    @server.tool(annotations=_READ_ONLY, structured_output=False)
    def car_trip_detail(trip_id: str) -> str:
        """1回の走行（トリップ）の詳細を返す。car_tripsの項目に加えて、燃料補正（LTFT/STFT）の平均、
        触媒温度・ブースト圧の最高値、冷却水温の変化、OBDの記録件数。

        Args:
            trip_id: car_trips が返したトリップID
        """
        return _run(lambda: trips.car_trip_detail(gw, cfg, trip_id))

    return server


class BearerAuthMiddleware:
    """/healthz 以外のHTTPリクエストにBearerトークンを要求するASGIミドルウェア。
    Streamable HTTPの応答はストリームになりうるため、BaseHTTPMiddlewareではなく素のASGIで書く。
    lifespan等のHTTP以外のイベントはそのまま通す（MCPのセッションマネージャの起動に要る）。"""

    def __init__(self, app: ASGIApp, token: str):
        self._app = app
        self._expected = f"Bearer {token}".encode()

    async def __call__(self, scope: Scope, receive: Receive, send: Send) -> None:
        if scope["type"] != "http":
            await self._app(scope, receive, send)
            return
        if scope["path"] == "/healthz":
            await _respond(send, 200, b"ok")
            return
        auth = dict(scope["headers"]).get(b"authorization", b"")
        if not hmac.compare_digest(auth, self._expected):
            await _respond(send, 401, b"unauthorized", [(b"www-authenticate", b"Bearer")])
            return
        await self._app(scope, receive, send)


async def _respond(send: Send, status: int, body: bytes, extra_headers: list | None = None) -> None:
    headers = [(b"content-type", b"text/plain"), (b"content-length", str(len(body)).encode())]
    await send({"type": "http.response.start", "status": status, "headers": headers + (extra_headers or [])})
    await send({"type": "http.response.body", "body": body})


def build_app(cfg: Config, gw: AwsGateway) -> ASGIApp:
    server = build_server(cfg, gw)
    app = server.streamable_http_app(
        streamable_http_path="/mcp",
        stateless_http=True,
        json_response=True,
        # SDKの既定（127.0.0.1で待ち受けるときの自動設定）ではcomposeのサービス名で来た
        # リクエストがHost不一致で弾かれるため、許可するHostを明示する
        transport_security=TransportSecuritySettings(
            enable_dns_rebinding_protection=True,
            allowed_hosts=list(cfg.allowed_hosts),
        ),
        host="0.0.0.0",
    )
    return BearerAuthMiddleware(app, cfg.token)


def main() -> None:
    logging.basicConfig(level=logging.INFO)
    cfg = load_config()
    app = build_app(cfg, AwsGateway(cfg))
    logger.info("car_mcp: device=%s obd_device=%s expose_location=%s", cfg.device_id, cfg.obd_device_id, cfg.expose_location)
    uvicorn.run(app, host="0.0.0.0", port=cfg.port)


if __name__ == "__main__":
    main()
