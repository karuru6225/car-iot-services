# car_iot_ble

ESP32-S3ゲートウェイ（`esp32_iot_gateway`）とBLEで通信するFlutterダッシュボードアプリ。
計測値・OBD-IIデータの表示に加え、OBD-IIデータをバッファしてAWSへアップロードする機能を持つ。

## セットアップ

### 1. 依存パッケージ

```bash
flutter pub get
```

### 2. `infra/`を先にterraform applyしておく

Cognito（Google連携）・`POST /obd`エンドポイント一式が必要。`infra/README`または`infra/manage.ps1`を参照。

### 3. Google Cloud ConsoleでOAuthクライアントを作成（初回のみ・手動）

CognitoのGoogleフェデレーテッドログインに使うOAuthクライアントは、Google Cloud Console側で手動作成する必要がある（Terraform化できるが個人利用規模では手動の方が早いと判断）。

1. [Google Cloud Console](https://console.cloud.google.com/) でプロジェクトを作成（未作成の場合）
2. 「APIとサービス」→「認証情報」を開く
3. 初回は先に「OAuth同意画面」（Google Auth Platform設定）を求められる
   - User Type: 個人のGmailなら「外部」
   - アプリ名・サポートメールを入力して保存
   - 公開ステータスは「テスト」のままでよい（本番公開・審査は不要）
4. 「認証情報」→「+ 認証情報を作成」→「OAuth クライアント ID」
5. **アプリケーションの種類は「ウェブアプリケーション」を選ぶ**（「Android」ではない。CognitoがサーバーサイドでGoogleのトークンエンドポイントを叩くため、Cognito自体がconfidential clientとして振る舞う）
6. 「承認済みのリダイレクトURI」に以下を追加:

   ```text
   {cognito_domain}/oauth2/idpresponse
   ```

   `cognito_domain`は`cd infra && terraform output cognito_domain`で確認できる（例: `https://iot-monitor-xxxxxxxxxxxx.auth.ap-northeast-1.amazoncognito.com`）
7. 「作成」→ クライアントID・クライアントシークレットが発行される
8. 「OAuth同意画面」の「テストユーザー」に、ログインに使うGoogleアカウントを追加

発行されたクライアントID/シークレットは `infra/terraform.tfvars` の `google_oauth_client_id` / `google_oauth_client_secret` に設定し、`infra/manage.ps1 -Action apply` を実行する（Cognitoにフェデレーテッドプロバイダとして登録される）。

### 4. `lib/config.dart`を作成

`lib/config.dart.example`をコピーして`lib/config.dart`を作成し、`terraform output`の値を埋める（`config.dart`はgitignore対象、実値をコミットしない）。

```bash
cp lib/config.dart.example lib/config.dart
```

| フィールド | 値の取得方法 |
| --- | --- |
| `cognitoClientId` | `terraform output cognito_mobile_client_id` |
| `awsRegion` | `infra/variables.tf`の`var.aws_region`（既定は`ap-northeast-1`） |
| `cognitoUserPoolId` | `terraform output cognito_user_pool_id` |
| `apiEndpoint` | `terraform output api_endpoint`（末尾スラッシュを除く） |

`awsRegion`・`cognitoUserPoolId`からAppAuthのissuer URL（`https://cognito-idp.{region}.amazonaws.com/{user_pool_id}`）を組み立てる。**Hosted UIドメイン（`cognito_domain`）はissuerに使えない**——後述の注意点参照。

### 5. 実機（Android）で起動

```bash
flutter run
```

## ハマりどころ

### AppAuthのissuerにHosted UIドメインを使うと404になる

`flutter_appauth`はOIDC discovery（`{issuer}/.well-known/openid-configuration`）を`issuer`から取得する。CognitoのHosted UIカスタムドメイン（`{prefix}.auth.{region}.amazoncognito.com`）にはこのdiscoveryドキュメントは無く、`FileNotFoundException`になる。

discoveryドキュメントは **User Pool自体のissuer URL** `https://cognito-idp.{region}.amazonaws.com/{user_pool_id}` にあり、その中の`authorization_endpoint`/`token_endpoint`がHosted UIドメインを指す構成になっている。`AuthService`は`config.dart`の`awsRegion`・`cognitoUserPoolId`からこのissuer URLを組み立てており、`cognito_domain`（Hosted UIドメイン）は使わない。

### `AndroidManifest.xml`の`taskAffinity=""`があるとログインが「一瞬でキャンセルされる」

Flutterテンプレート標準の`android:taskAffinity=""`（MainActivityに付与）があると、ブラウザからのリダイレクトが新しいタスクとして扱われ、認可フロー中の状態を保持していた`AuthorizationManagementActivity`のインスタンスを見失う。ログに`No stored state - unable to handle response`と出て、ログイン画面が一瞬表示されただけでアプリに戻ってしまう場合はこれが原因。`MainActivity`の`taskAffinity=""`属性を削除する（`launchMode="singleTop"`は残してよい）。

### OAuthリダイレクトのカスタムURLスキームにアンダースコアは使えない

Android的にはapplicationId（例: `info.karuru.cariot_mobile`）にアンダースコアを含められるが、CognitoのApp Client `callback_urls`はRFC3986のURIスキーム構文で検証されており、アンダースコアを含むスキームは`InvalidParameterException: ... is not a valid URL`で弾かれる。そのため、OAuthリダイレクト用のスキーム文字列（`android/app/build.gradle.kts`の`appAuthRedirectScheme`・`lib/services/auth_service.dart`の`_redirectUri`・Cognito側の`callback_urls`）はapplicationIdとは別に、アンダースコア無しの文字列（例: `info.karuru.cariotmobile`）を使う。3箇所を完全一致させること。
