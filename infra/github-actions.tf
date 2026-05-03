# ─── GitHub Actions OIDC ─────────────────────────────────────────────────────
# v* タグ push のみ assume できる最小権限ロール

resource "aws_iam_openid_connect_provider" "github_actions" {
  url             = "https://token.actions.githubusercontent.com"
  client_id_list  = ["sts.amazonaws.com"]
  thumbprint_list = ["6938fd4d98bab03faadb97b34396831e3780aea1"]
}

resource "aws_iam_role" "github_actions_firmware" {
  name = "${var.project}-gha-firmware"

  assume_role_policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Effect = "Allow"
      Principal = {
        Federated = aws_iam_openid_connect_provider.github_actions.arn
      }
      Action = "sts:AssumeRoleWithWebIdentity"
      Condition = {
        StringEquals = {
          "token.actions.githubusercontent.com:aud" = "sts.amazonaws.com"
        }
        StringLike = {
          # v* タグ push のみ（ブランチ push や PR では assume 不可）
          "token.actions.githubusercontent.com:sub" = "repo:karuru6225/car-iot-services:ref:refs/tags/v*"
        }
      }
    }]
  })
}

resource "aws_iam_role_policy" "github_actions_firmware" {
  role = aws_iam_role.github_actions_firmware.id

  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [
      {
        Effect   = "Allow"
        Action   = "s3:PutObject"
        Resource = [
          "${aws_s3_bucket.firmware.arn}/firmware/*",
          "${aws_s3_bucket.firmware.arn}/jobs/*",
        ]
      },
      {
        Effect = "Allow"
        Action = [
          "iot:ListThings",
          "iot:CreateJob",
          "iot:ListJobs",
          "iot:CancelJob",
          "iot:DeleteJob",
        ]
        Resource = "*"
      },
    ]
  })
}
