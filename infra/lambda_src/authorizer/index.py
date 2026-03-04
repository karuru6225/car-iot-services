"""
API Gateway Lambda Authorizer（シンプルレスポンス形式）

x-api-key ヘッダーを環境変数 API_KEY と照合する。
将来は Cognito JWT 検証に差し替える箇所。
"""

import os


def handler(event, context):
    token = (event.get("headers") or {}).get("x-api-key", "")
    expected = os.environ.get("API_KEY", "")
    if token and expected and token == expected:
        return {"isAuthorized": True}
    return {"isAuthorized": False}
