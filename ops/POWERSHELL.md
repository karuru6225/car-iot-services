# PowerShell スクリプト作成メモ

このプロジェクトの ops スクリプトを書くときにハマりやすい点をまとめる。

---

## 外部コマンドの引数に `--` が含まれる場合

PowerShell は `--` を単項演算子として解釈しようとするため、
バッククォート（`` ` ``）による行継続と組み合わせるとパースエラーになる。

```powershell
# NG: パースエラー
$result = (aws s3api generate-presigned-url `
  --bucket $bucket `   # ← ここでエラー
  --key $key)

# OK: 1行にまとめる
$result = (aws s3api generate-presigned-url --bucket $bucket --key $key --expires-in 600)
```

**対策: `aws` コマンドは原則1行で書く。** どうしても分割したい場合は配列引数を使う。

```powershell
$args = @("s3api", "generate-presigned-url", "--bucket", $bucket, "--key", $key)
$result = (aws @args)
```

---

## バッククォート行継続の注意点

行継続の `` ` `` の**直後にスペースや文字があると機能しない**（サイレントに無視される）。

```powershell
# NG: バッククォートの後にスペースがある
$result = aws sts get-caller-identity ` 
  --query Account

# OK
$result = aws sts get-caller-identity `
  --query Account
```

エディタの末尾スペース自動削除が有効だと気づきにくい。

---

## S3 presigned URL の生成

`aws s3 presign` は **GET 専用**。PUT 用の presigned URL を生成するには `aws s3api` を使う。

```powershell
# GET 用（ダウンロード）
$url = (aws s3 presign "s3://bucket/key" --expires-in 600)

# PUT 用（アップロード）
# --output text は不要（付けると逆に空になる）。デフォルトで URL 文字列を直接返す
$url = (aws s3api generate-presigned-url --bucket $bucket --key $key --expires-in 600 --http-method PUT --region $region)
```

---

## AWS CLI の認証情報の扱い

プロファイルを使う場合、`aws configure export-credentials` で環境変数に展開してから後続コマンドに渡す。

```powershell
if ($Profile) {
  $env:AWS_PROFILE = $Profile
  $credEnv = aws configure export-credentials --profile $Profile --format powershell
  if ($LASTEXITCODE -ne 0) { Write-Error "Failed to get credentials."; exit 1 }
  Invoke-Expression ($credEnv -join "`n")
}
```

---

## IoT Jobs のジョブドキュメントをファイル経由で渡す

JSON 文字列を直接 `--document` に渡すとエスケープやクォートが壊れることがある。
一時ファイルに書き出して `file://` で渡す。

```powershell
$doc = '{"operation":"foo","param":' + $value + '}'
$tmpFile = [System.IO.Path]::GetTempFileName()
$doc | Set-Content -Path $tmpFile -Encoding ascii
aws iot create-job --job-id $jobId --targets $thingArn --document "file://$tmpFile"
Remove-Item $tmpFile -ErrorAction SilentlyContinue
```

---

## ps1 ファイルの文字列は英語で書く

ps1 ファイルに日本語の文字列リテラルを含めると、エンコーディング（BOM なし UTF-8 vs ANSI）の違いで文字化けし、クォートのパースエラーになることがある。
エラーメッセージ・コメント以外の文字列は英語で書く。コメントは化けても実害がないが、`Write-Error` や `Write-Host` に日本語を入れると実行時にパースエラーになる場合がある。

```powershell
# NG
if (-not $url) { Write-Error "URLが空です"; exit 1 }

# OK
if (-not $url) { Write-Error "URL is empty."; exit 1 }
```

## `$LASTEXITCODE` によるエラーチェック

PowerShell では外部コマンドのエラーは例外にならないため、
`$ErrorActionPreference = "Stop"` だけでは捕捉できない。
各コマンドの後に明示的にチェックする。

```powershell
$ErrorActionPreference = "Stop"  # PowerShell 自身のエラーは止まる

$result = (aws sts get-caller-identity --output text)
if ($LASTEXITCODE -ne 0) { Write-Error "failed"; exit 1 }  # 外部コマンドはこれが必要
```
