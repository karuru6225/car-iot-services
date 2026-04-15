import { Hono } from 'hono';
import { handle, streamHandle } from 'hono/aws-lambda';
import { DynamoDBClient } from '@aws-sdk/client-dynamodb';
import { DynamoDBDocumentClient, PutCommand, QueryCommand, DeleteCommand } from '@aws-sdk/lib-dynamodb';
import { BedrockAgentRuntimeClient, InvokeAgentCommand } from '@aws-sdk/client-bedrock-agent-runtime';
import { streamSSE } from 'hono/streaming';

const app = new Hono();

const dynamo = DynamoDBDocumentClient.from(new DynamoDBClient({}));
const bedrockAgentRuntime = new BedrockAgentRuntimeClient({});

const API_KEY = process.env.API_KEY ?? '';
const HISTORY_TABLE = process.env.HISTORY_TABLE ?? '';
const AGENTCORE_RUNTIME_ID = process.env.AGENTCORE_RUNTIME_ID ?? '';

// ─── API キー検証ミドルウェア ──────────────────────────────────────────────

app.use('*', async (c, next) => {
  const auth = c.req.header('Authorization');
  if (!auth || auth !== `Bearer ${API_KEY}`) {
    return c.json({ error: 'Unauthorized' }, 401);
  }
  await next();
});

// ─── POST /api/invoke — AgentCore をストリーミング呼び出し ─────────────────

app.post('/api/invoke', async (c) => {
  const { message, userId, sessionId } = await c.req.json<{
    message: string;
    userId: string;
    sessionId?: string;
  }>();

  // 会話履歴に送信メッセージを保存
  const timestamp = new Date().toISOString();
  await dynamo.send(new PutCommand({
    TableName: HISTORY_TABLE,
    Item: {
      userId,
      timestamp,
      role: 'user',
      content: message,
      sessionId: sessionId ?? timestamp,
    },
  }));

  return streamSSE(c, async (stream) => {
    let fullResponse = '';

    // ノート: AgentCore の呼び出し方法（SDK API）は要確認
    // 現時点では bedrock-agent-runtime の InvokeAgent を暫定使用
    // AgentCore GA 後の正式 SDK が揃い次第修正すること
    const command = new InvokeAgentCommand({
      agentId: AGENTCORE_RUNTIME_ID,
      agentAliasId: 'TSTALIASID',
      sessionId: sessionId ?? timestamp,
      inputText: message,
    });

    const response = await bedrockAgentRuntime.send(command);

    if (response.completion) {
      for await (const chunk of response.completion) {
        if (chunk.chunk?.bytes) {
          const text = new TextDecoder().decode(chunk.chunk.bytes);
          fullResponse += text;
          await stream.writeSSE({ data: text });
        }
      }
    }

    // 応答を履歴に保存
    await dynamo.send(new PutCommand({
      TableName: HISTORY_TABLE,
      Item: {
        userId,
        timestamp: new Date().toISOString(),
        role: 'assistant',
        content: fullResponse,
        sessionId: sessionId ?? timestamp,
      },
    }));

    await stream.writeSSE({ data: '[DONE]' });
  });
});

// ─── GET /api/history — 会話履歴を取得 ───────────────────────────────────

app.get('/api/history', async (c) => {
  const userId = c.req.query('userId');
  if (!userId) {
    return c.json({ error: 'userId is required' }, 400);
  }

  const result = await dynamo.send(new QueryCommand({
    TableName: HISTORY_TABLE,
    KeyConditionExpression: 'userId = :uid',
    ExpressionAttributeValues: { ':uid': userId },
    ScanIndexForward: false,
    Limit: 100,
  }));

  return c.json({ items: result.Items ?? [] });
});

// ─── DELETE /api/history/:timestamp — 履歴を1件削除 ──────────────────────

app.delete('/api/history/:timestamp', async (c) => {
  const userId = c.req.query('userId');
  const timestamp = c.req.param('timestamp');

  if (!userId) {
    return c.json({ error: 'userId is required' }, 400);
  }

  await dynamo.send(new DeleteCommand({
    TableName: HISTORY_TABLE,
    Key: { userId, timestamp },
  }));

  return c.json({ success: true });
});

// Lambda ハンドラー（ストリーミング対応）
export const handler = streamHandle(app);
