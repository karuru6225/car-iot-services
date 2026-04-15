"""
API Lambda — /api/invoke と /api/history を処理する。
API Gateway HTTP API (payload format 2.0) を想定。
"""

import json
import os
import boto3
from datetime import datetime, timezone

API_KEY = os.environ['API_KEY']
HISTORY_TABLE = os.environ['HISTORY_TABLE']
AGENTCORE_RUNTIME_ARN = os.environ['AGENTCORE_RUNTIME_ARN']

dynamo = boto3.resource('dynamodb').Table(HISTORY_TABLE)
# ノート: AgentCore SDK のクライアント名は要確認。デプロイ時のエラーで修正する
agentcore = boto3.client('bedrock-agentcore')


def handler(event: dict, context: object) -> dict:
    # API キー検証
    auth = (event.get('headers') or {}).get('authorization', '')
    if auth != f'Bearer {API_KEY}':
        return _resp(401, {'error': 'Unauthorized'})

    method = event.get('requestContext', {}).get('http', {}).get('method', '')
    path = event.get('rawPath', '')

    if path == '/api/invoke' and method == 'POST':
        return handle_invoke(event)
    elif path == '/api/history' and method == 'GET':
        return handle_history_get(event)
    elif path.startswith('/api/history/') and method == 'DELETE':
        return handle_history_delete(event)
    else:
        return _resp(404, {'error': 'Not Found'})


def handle_invoke(event: dict) -> dict:
    body = json.loads(event.get('body') or '{}')
    message = body.get('message', '')
    user_id = body.get('userId', 'anonymous')
    session_id = body.get('sessionId') or datetime.now(timezone.utc).isoformat()

    # 送信メッセージを履歴に保存
    timestamp = datetime.now(timezone.utc).isoformat()
    dynamo.put_item(Item={
        'userId': user_id,
        'timestamp': timestamp,
        'role': 'user',
        'content': message,
        'sessionId': session_id,
    })

    # AgentCore 呼び出し
    payload = json.dumps({
        'messages': [{'role': 'user', 'content': message}]
    }).encode('utf-8')

    response = agentcore.invoke_agent_runtime(
        agentRuntimeArn=AGENTCORE_RUNTIME_ARN,
        runtimeSessionId=session_id,
        payload=payload,
    )

    # レスポンスを結合
    result_text = ''
    body = response.get('response') or response.get('body') or ''
    if hasattr(body, 'read'):
        result_text = body.read().decode('utf-8')
    elif isinstance(body, (bytes, bytearray)):
        result_text = body.decode('utf-8')
    elif isinstance(body, str):
        result_text = body

    # 応答を履歴に保存
    dynamo.put_item(Item={
        'userId': user_id,
        'timestamp': datetime.now(timezone.utc).isoformat(),
        'role': 'assistant',
        'content': result_text,
        'sessionId': session_id,
    })

    return _resp(200, {'response': result_text, 'sessionId': session_id})


def handle_history_get(event: dict) -> dict:
    params = event.get('queryStringParameters') or {}
    user_id = params.get('userId')
    if not user_id:
        return _resp(400, {'error': 'userId is required'})

    result = dynamo.query(
        KeyConditionExpression='userId = :uid',
        ExpressionAttributeValues={':uid': user_id},
        ScanIndexForward=False,
        Limit=100,
    )
    return _resp(200, {'items': result.get('Items', [])})


def handle_history_delete(event: dict) -> dict:
    params = event.get('queryStringParameters') or {}
    user_id = params.get('userId')
    path = event.get('rawPath', '')
    timestamp = path.split('/')[-1]

    if not user_id:
        return _resp(400, {'error': 'userId is required'})

    dynamo.delete_item(Key={'userId': user_id, 'timestamp': timestamp})
    return _resp(200, {'success': True})


def _resp(status: int, body: dict) -> dict:
    return {
        'statusCode': status,
        'headers': {'Content-Type': 'application/json'},
        'body': json.dumps(body, ensure_ascii=False),
    }
