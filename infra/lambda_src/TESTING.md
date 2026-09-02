# Lambda ユニットテスト

`lambda_src/*/index.py` のユニットテストを、ホストに Python を入れずに Docker で実行する。
S3 依存部分は [moto](https://github.com/getmoto/moto) でモックし、実 AWS には一切繋がない。

## 実行方法

`infra/lambda_src/` ディレクトリで実行する。

```bash
# 全Lambda分のテストを実行（ビルド込み）
docker compose -f docker-compose.test.yml run --build --rm test

# 特定のLambdaだけ実行（pytestへの引数をそのまま渡せる）
docker compose -f docker-compose.test.yml run --build --rm test pytest compact/tests -v
```

`--build` を付けることで、`requirements-test.txt` やソースの変更を毎回反映してから実行する
（pip install 部分は Docker のレイヤーキャッシュが効くので、依存関係が変わらない限り速い）。

## 構成

```text
lambda_src/
├── Dockerfile.test          python3.12-slim（Lambdaランタイムと同じ）+ requirements-test.txt
├── docker-compose.test.yml  docker compose run --build 用の定義
├── requirements-test.txt    pytest / moto[s3] / boto3
├── pytest.ini               --import-mode=importlib（後述）
└── {lambda名}/
    ├── index.py
    └── tests/
        ├── conftest.py       fixtureでmotoモック+index.pyのロードを行う
        └── test_index.py
```

新しい Lambda にテストを追加する場合は、既存の `tests/conftest.py` を1つコピーして
バケット名・環境変数・fixture名を対象の Lambda に合わせて書き換えるのが早い。

## 設計上のポイント（ハマりどころ）

### 1. `index.py` は毎回 `spec_from_file_location` で一意な名前としてロードする

全 Lambda 分のテストが同一 pytest プロセス内で動くため、素朴に `sys.path` を通して
`import index` すると、`sys.modules["index"]` が Lambda 間で衝突し、片方のテストが
別の Lambda の `index.py` を見てしまう（実際に発生した）。

`conftest.py` の fixture では `importlib.util.spec_from_file_location("<lambda名>_index", ...)`
で毎回フレッシュなモジュールオブジェクトとしてロードする。これにより:

- Lambda 間の名前衝突を回避
- `s3 = boto3.client("s3")` のようなモジュールレベルのクライアント生成を、
  `mock_aws()` コンテキストの中で確実に発生させられる（先に一度でも素の状態でロードされて
  `sys.modules` にキャッシュされてしまう心配がない）

### 2. `pytest.ini` で `--import-mode=importlib` を指定する

Lambda ごとに同名の `test_index.py` が並ぶため、pytest のデフォルトの import 方式だと
ファイルパスの違いを無視して同一モジュールとして衝突する
（`import file mismatch` エラーになる）。`__init__.py` を追加してパッケージ化する方法もあるが、
Lambda の zip に不要なファイルが混ざるため、`--import-mode=importlib` で回避している。

### 3. moto の `iot-data`（Device Shadow）は一部未実装

`update_thing_shadow` は moto でサポートされておらず、`get_thing_shadow` も安定して
動かせなかった（admin/status/shadow_guard Lambda で遭遇）。この2つの API を使う箇所は
`mock_aws()` に頼らず、`monkeypatch.setattr(module.iot_data, "get_thing_shadow", ...)` の
ように個別に差し替える。`mock_aws()` 自体は「本物の AWS に誤って繋がらないための安全網」
としてそのまま残す。

### 4. Athena も実 SQL 実行まではモックされない

moto は `start_query_execution` / `get_query_execution` 等の API 呼び出し自体はモックするが、
投げた SQL を実際に実行して結果を返すことはしない。`delete`/`query` Lambda では
`_get_partitions` や `athena.get_query_results` を `monkeypatch` で差し替え、クエリの
**組み立てロジック**（WHERE句・UNION ALLの構成等）と、**結果のパース処理**を分けてテストする。

### 5. `datetime.now()` に依存する関数は `_FrozenDateTime` サブクラスで固定する

`freezegun` 等の追加ライブラリは入れず、次のパターンで十分:

```python
class _FrozenDateTime(datetime):
    @classmethod
    def now(cls, tz=None):
        return datetime(2026, 3, 1, 10, 0, tzinfo=tz)

def test_xxx(module, monkeypatch):
    monkeypatch.setattr(module, "datetime", _FrozenDateTime)
    ...
```

対象モジュールが `from datetime import datetime` で束縛した名前を直接差し替えるので、
モジュール内の `datetime.now(...)` 呼び出し全てに効く。
