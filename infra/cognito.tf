# ─── Cognito User Pool ────────────────────────────────────────────────────────

resource "aws_cognito_user_pool" "main" {
  name = var.project

  # 管理者のみユーザーを作成可能（自己サインアップ無効）
  admin_create_user_config {
    allow_admin_create_user_only = true
  }

  username_attributes      = ["email"]
  auto_verified_attributes = ["email"]

  password_policy {
    minimum_length    = 8
    require_lowercase = true
    require_uppercase = true
    require_numbers   = true
    require_symbols   = false
  }
}

# ─── App Client（Web SPA 用） ──────────────────────────────────────────────────

resource "aws_cognito_user_pool_client" "web" {
  name         = "${var.project}-web"
  user_pool_id = aws_cognito_user_pool.main.id

  generate_secret = false

  allowed_oauth_flows                  = ["implicit"]
  allowed_oauth_scopes                 = ["openid", "email", "profile"]
  allowed_oauth_flows_user_pool_client = true

  callback_urls = ["https://${local.web_domain}"]
  logout_urls   = ["https://${local.web_domain}"]

  supported_identity_providers = ["COGNITO"]
}

# ─── グループ ─────────────────────────────────────────────────────────────────

resource "aws_cognito_user_group" "admin" {
  name         = "admin"
  user_pool_id = aws_cognito_user_pool.main.id
  description  = "管理者: 閲覧・削除が可能"
}

resource "aws_cognito_user_group" "viewer" {
  name         = "viewer"
  user_pool_id = aws_cognito_user_pool.main.id
  description  = "閲覧者: 削除不可"
}

# ─── Hosted UI ドメイン ────────────────────────────────────────────────────────

resource "aws_cognito_user_pool_domain" "main" {
  domain       = "${var.project}-${data.aws_caller_identity.current.account_id}"
  user_pool_id = aws_cognito_user_pool.main.id
}
