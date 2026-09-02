# ─── モバイルアプリ用 Cognito 認証（Google連携） ───────────────────────────────
# Web管理画面（implicit flow, Cognito直接ログイン）とは別に、モバイルアプリ用の
# App Clientを用意し、GoogleをフェデレーテッドIdPとして連携する。

resource "aws_cognito_identity_provider" "google" {
  user_pool_id  = aws_cognito_user_pool.main.id
  provider_name = "Google"
  provider_type = "Google"

  provider_details = {
    client_id        = var.google_oauth_client_id
    client_secret    = var.google_oauth_client_secret
    authorize_scopes = "email openid profile"
  }

  attribute_mapping = {
    email    = "email"
    username = "sub"
  }

  # AWS側がGoogle用のOAuthエンドポイント（authorize_url/token_url/oidc_issuer等）を
  # 勝手にデフォルト値で補完して返してくるため、宣言していないこれらの項目との差分が
  # terraform planのたびに「削除」として出続ける（実際には何も壊れない既知の挙動）。
  # provider_detailsの差分検出自体を無視して、この無限diffを止める。
  lifecycle {
    ignore_changes = [provider_details]
  }
}

# ─── App Client（モバイル用、Authorization Code + PKCE、シークレット無し） ────

resource "aws_cognito_user_pool_client" "mobile" {
  name         = "${var.project}-mobile"
  user_pool_id = aws_cognito_user_pool.main.id

  generate_secret = false

  allowed_oauth_flows                  = ["code"]
  allowed_oauth_scopes                 = ["openid", "email", "profile"]
  allowed_oauth_flows_user_pool_client = true

  # URIスキームはRFC3986でアンダースコア不可のため、applicationId（info.karuru.cariot_mobile）とは
  # 別にハイフン・アンダースコア無しの文字列を使う
  callback_urls = ["info.karuru.cariotmobile://oauthredirect"]
  logout_urls   = ["info.karuru.cariotmobile://oauthredirect"]

  supported_identity_providers = ["Google"]

  # supported_identity_providers は文字列参照のためTerraformの暗黙依存が働かない → 明示
  depends_on = [aws_cognito_identity_provider.google]

  token_validity_units {
    access_token  = "hours"
    id_token      = "hours"
    refresh_token = "days"
  }
  access_token_validity  = 1
  id_token_validity      = 1
  refresh_token_validity = 30

  prevent_user_existence_errors = "ENABLED"
}
