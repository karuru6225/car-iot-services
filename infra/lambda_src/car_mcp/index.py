"""
mnemosyne（個人用AIアシスタント）向けの、車両データ読み取り専用MCPサーバ（API Gateway → Lambda）

経路: POST /mcp（MCP Streamable HTTP、ステートレス・JSON応答）
認証: API GatewayのJWT Authorizer（mnemosyne専用のCognito M2Mクライアントのトークンだけを
      受け付ける）＋ルートのスコープ car-mcp/read。Lambda側でもスコープを確認する
      （trip_analysis/index.pyの_is_adminと同じくdefense in depth）

道具は読み取り専用のものだけを置く。車に作用する道具（Shadowの書き換え・コマンド送信）は
許可リストで隠すのではなく、そもそも作らない（HANDOFF_mnemosyne.md 1章）。
道具の名前はmnemosyne側の許可リストにそのまま書かれるので、一度決めたら変えない。

返り値は会話モデルがそのまま読むので、値には単位と意味を添え、判定できること（燃費の
妥当性、エンジン稼働の推定など）はコードで判定して渡す（中身はcarmcp/battery.py・trips.py）。
"""

import base64
import json
import logging

from botocore.exceptions import BotoCoreError, ClientError

from carmcp import battery, trips
from carmcp.aws import AthenaError, AwsGateway
from carmcp.config import load_config
from carmcp.protocol import PARSE_ERROR, INVALID_REQUEST, Tool, ToolError, error, handle

logger = logging.getLogger()
logger.setLevel(logging.INFO)

REQUIRED_SCOPE = "car-mcp/read"

INSTRUCTIONS = (
    "車（メイン/サブの2系統のバッテリーを持つ車両）の計測データを読むための道具。"
    "バッテリーの計測値は車載装置がLTE経由で通常5分ごとに送る。"
    "トリップ（走行記録）はスマートフォンが車内にあるときだけOBD-II経由で記録される。"
    "時刻はすべて日本時間（+09:00）で返す。"
)

_DATE_PROPERTY = {"type": "string", "pattern": r"^\d{4}-\d{2}-\d{2}$"}

cfg = load_config()
gw = AwsGateway(cfg)


def _run(fn, *args) -> str:
    """道具の本体を呼び、入力の誤りとデータ取得の失敗は会話モデルが読める文にする。"""
    try:
        return json.dumps(fn(gw, cfg, *args), ensure_ascii=False, indent=1)
    except ValueError as e:
        raise ToolError(str(e)) from e
    except (AthenaError, BotoCoreError, ClientError) as e:
        logger.exception("AWSからのデータ取得に失敗")
        raise ToolError(
            f"車のデータを取得できなかった（AWSへのアクセスに失敗: {type(e).__name__}）。時間をおいて再度試す"
        ) from e


TOOLS = [
    Tool(
        name="car_status",
        description=(
            "車のバッテリーの現在の状態を返す。メイン/サブバッテリーの電圧、サブバッテリーの電流・積算電荷量、"
            "最終受信時刻と経過時間、エンジン稼働中か駐車中かの推定、直近の推移、充電リレーなど装置の設定。"
        ),
        run=lambda: _run(battery.car_status),
    ),
    Tool(
        name="car_battery_history",
        description=(
            "期間を指定して、日ごとのバッテリーの記録を返す。メイン/サブバッテリーの電圧の最低・最高、"
            "サブバッテリーの充電量・放電量（Ah）、受信件数。受信が無かった日はそうと分かる形で返す。期間は最大92日。"
        ),
        properties={
            "start_date": {**_DATE_PROPERTY, "description": "開始日（日本時間の暦日、YYYY-MM-DD）"},
            "end_date": {**_DATE_PROPERTY, "description": "終了日（日本時間の暦日、YYYY-MM-DD、この日を含む）"},
        },
        run=lambda start_date, end_date: _run(battery.car_battery_history, start_date, end_date),
    ),
    Tool(
        name="car_trips",
        description=(
            "期間を指定して、走行記録（トリップ）の一覧を返す。出発/到着時刻、所要時間、走行距離、燃料消費、燃費。"
            "記録はスマートフォンが車内にあった走行だけなので、一覧に無いことは走っていないことを意味しない。期間は最大92日。"
        ),
        properties={
            "start_date": {**_DATE_PROPERTY, "description": "開始日（日本時間の暦日、YYYY-MM-DD）"},
            "end_date": {**_DATE_PROPERTY, "description": "終了日（日本時間の暦日、YYYY-MM-DD、この日を含む）"},
        },
        run=lambda start_date, end_date: _run(trips.car_trips, start_date, end_date),
    ),
    Tool(
        name="car_trip_detail",
        description=(
            "1回の走行（トリップ）の詳細を返す。car_tripsの項目に加えて、燃料補正（LTFT/STFT）の平均、"
            "触媒温度・ブースト圧の最高値、冷却水温の変化、OBDの記録件数。"
        ),
        properties={"trip_id": {"type": "string", "description": "car_trips が返したトリップID"}},
        run=lambda trip_id: _run(trips.car_trip_detail, trip_id),
    ),
]


def _http(status: int, body: dict | None = None, headers: dict | None = None) -> dict:
    return {
        "statusCode": status,
        "headers": {"Content-Type": "application/json", **(headers or {})},
        "body": "" if body is None else json.dumps(body, ensure_ascii=False),
    }


def _has_scope(event: dict) -> bool:
    try:
        jwt = event["requestContext"]["authorizer"]["jwt"]
    except (KeyError, TypeError):
        return False
    scopes = jwt.get("scopes") or jwt.get("claims", {}).get("scope", "").split()
    return REQUIRED_SCOPE in scopes


def handler(event, context):
    if not _has_scope(event):
        return _http(403, {"error": "forbidden"})

    # ステートレスでサーバから送るものが無いので、SSEのGETやセッション終了のDELETEは受けない
    # （仕様上405で「このサーバはSSEストリームを提供しない」を表す）
    if event["requestContext"]["http"]["method"] != "POST":
        return _http(405, None, {"Allow": "POST"})

    body = event.get("body") or ""
    if event.get("isBase64Encoded"):
        body = base64.b64decode(body).decode("utf-8")
    try:
        message = json.loads(body)
    except json.JSONDecodeError:
        return _http(400, error(None, PARSE_ERROR, "parse error"))
    if isinstance(message, list):
        return _http(400, error(None, INVALID_REQUEST, "batch requests are not supported"))

    response = handle(message, TOOLS, INSTRUCTIONS)
    if response is None:
        return _http(202)
    return _http(200, response)
