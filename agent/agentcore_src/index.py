"""
AgentCore ランタイムに載せるエージェントコード。
Bedrock Claude モデルに対してメッセージを送り、ストリーミングで返す。

ノート:
  AgentCore の Python ランタイムが期待するハンドラー形式は
  2026年3月時点で公式ドキュメントが薄いため、
  デプロイ時のエラーに応じてシグネチャを修正すること。
"""

import json
import os
import boto3

MODEL_ID = os.environ.get('MODEL_ID', 'anthropic.claude-3-5-sonnet-20241022-v2:0')

bedrock = boto3.client('bedrock-runtime')


def handler(event: dict, context: object) -> dict:
    """
    AgentCore ランタイムから呼び出されるハンドラー。
    event の形式は AgentCore の仕様に依存するため要確認。
    """
    # AgentCore から渡されるメッセージを取得
    # 形式は公式ドキュメントで確認すること
    messages = event.get('messages', [])
    user_input = event.get('inputText', '')

    if not messages and user_input:
        messages = [{'role': 'user', 'content': user_input}]

    # Bedrock Claude 呼び出し
    body = {
        'anthropic_version': 'bedrock-2023-05-31',
        'max_tokens': 4096,
        'messages': messages,
    }

    response = bedrock.invoke_model_with_response_stream(
        modelId=MODEL_ID,
        body=json.dumps(body),
        contentType='application/json',
        accept='application/json',
    )

    # ストリーミングレスポンスを結合して返す
    full_text = ''
    for event_chunk in response['body']:
        chunk = json.loads(event_chunk['chunk']['bytes'])
        if chunk.get('type') == 'content_block_delta':
            delta = chunk.get('delta', {})
            if delta.get('type') == 'text_delta':
                full_text += delta.get('text', '')

    return {
        'response': full_text,
        'messages': messages + [{'role': 'assistant', 'content': full_text}],
    }
