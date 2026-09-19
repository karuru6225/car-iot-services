"""car_mcpのテスト用の偽物。conftest.pyに置くと、他のLambdaのconftest.pyと
--import-mode=importlibの下で名前が衝突して import できないため別モジュールにする。"""


class FakeGateway:
    """AwsGatewayと同じメソッドを持つ偽物。S3はキー→本文のdict、Athenaは返す行を順に積んでおく。"""

    def __init__(self, objects: dict[str, str] | None = None, query_results: list[list[dict]] | None = None,
                 shadow: dict | None = None):
        self.objects = objects or {}
        self.query_results = list(query_results or [])
        self.shadow = shadow
        self.queries: list[str] = []

    def query(self, sql: str) -> list[dict]:
        self.queries.append(sql)
        return self.query_results.pop(0) if self.query_results else []

    def list_prefixes(self, prefix: str) -> list[str]:
        found = set()
        for key in self.objects:
            if key.startswith(prefix) and "/" in key[len(prefix):]:
                found.add(prefix + key[len(prefix):].split("/", 1)[0] + "/")
        return sorted(found)

    def list_keys(self, prefix: str) -> list[str]:
        return sorted(k for k in self.objects if k.startswith(prefix))

    def get_text(self, key: str) -> str | None:
        return self.objects.get(key)

    def get_shadow(self, thing_name: str) -> dict | None:
        return self.shadow
