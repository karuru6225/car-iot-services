# mnemosyneのdocker composeから `build: <このリポジトリ>/car_mcp` で使う想定
FROM python:3.12-slim AS base

WORKDIR /app
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY carmcp ./carmcp

# ─── テスト用（docker build --target test）───────────────────────────────────
FROM base AS test
COPY requirements-test.txt pytest.ini ./
RUN pip install --no-cache-dir -r requirements-test.txt
COPY tests ./tests
CMD ["pytest", "-v"]

# ─── 本番（既定のターゲット）─────────────────────────────────────────────────
FROM base AS runtime
RUN useradd --create-home --uid 10001 carmcp
USER carmcp
EXPOSE 8000
# slimイメージにはcurlが無いので、Python標準ライブラリで /healthz を叩く
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
  CMD ["python", "-c", "import os,urllib.request; urllib.request.urlopen('http://127.0.0.1:%s/healthz' % os.environ.get('CAR_MCP_PORT', '8000'), timeout=2)"]
CMD ["python", "-m", "carmcp.server"]
