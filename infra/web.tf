# ─── Route53 ホストゾーン ──────────────────────────────────────────────────────

data "aws_route53_zone" "main" {
  zone_id = var.hosted_zone_id
}

# ─── ローカル変数 ──────────────────────────────────────────────────────────────

locals {
  web_domain = "${var.web_subdomain}.${trimsuffix(data.aws_route53_zone.main.name, ".")}"

  cognito_domain_base = "https://${aws_cognito_user_pool_domain.main.domain}.auth.${var.aws_region}.amazoncognito.com"

  # Terraform の replace() でプレースホルダーを実際の値に置換
  index_html_rendered = replace(
    replace(
      replace(
        replace(
          file("${path.module}/../web/index.html"),
          "__TMPL_API_ENDPOINT__",
          aws_apigatewayv2_stage.main.invoke_url
        ),
        "__TMPL_COGNITO_DOMAIN__",
        local.cognito_domain_base
      ),
      "__TMPL_COGNITO_CLIENT_ID__",
      aws_cognito_user_pool_client.web.id
    ),
    "__TMPL_WEB_URL__",
    "https://${local.web_domain}"
  )
}

# ─── ACM 証明書（CloudFront は us-east-1 必須） ───────────────────────────────

resource "aws_acm_certificate" "web" {
  provider          = aws.us_east_1
  domain_name       = local.web_domain
  validation_method = "DNS"

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_route53_record" "cert_validation" {
  for_each = {
    for dvo in aws_acm_certificate.web.domain_validation_options : dvo.domain_name => {
      name   = dvo.resource_record_name
      record = dvo.resource_record_value
      type   = dvo.resource_record_type
    }
  }

  zone_id = var.hosted_zone_id
  name    = each.value.name
  type    = each.value.type
  records = [each.value.record]
  ttl     = 60
}

resource "aws_acm_certificate_validation" "web" {
  provider                = aws.us_east_1
  certificate_arn         = aws_acm_certificate.web.arn
  validation_record_fqdns = [for r in aws_route53_record.cert_validation : r.fqdn]
}

# ─── S3 Web ホスティング用バケット ───────────────────────────────────────────

resource "aws_s3_bucket" "web" {
  bucket = "${var.project}-web-${data.aws_caller_identity.current.account_id}"
}

resource "aws_s3_bucket_public_access_block" "web" {
  bucket                  = aws_s3_bucket.web.id
  block_public_acls       = true
  block_public_policy     = true
  ignore_public_acls      = true
  restrict_public_buckets = true
}

# ─── CloudFront OAC ───────────────────────────────────────────────────────────

resource "aws_cloudfront_origin_access_control" "web" {
  name                              = "${var.project}-web"
  origin_access_control_origin_type = "s3"
  signing_behavior                  = "always"
  signing_protocol                  = "sigv4"
}

# ─── CloudFront Distribution ──────────────────────────────────────────────────

resource "aws_cloudfront_distribution" "web" {
  enabled             = true
  default_root_object = "index.html"
  price_class         = "PriceClass_100"
  aliases             = [local.web_domain]

  origin {
    domain_name              = aws_s3_bucket.web.bucket_regional_domain_name
    origin_id                = "s3-web"
    origin_access_control_id = aws_cloudfront_origin_access_control.web.id
  }

  default_cache_behavior {
    target_origin_id       = "s3-web"
    viewer_protocol_policy = "redirect-to-https"
    allowed_methods        = ["GET", "HEAD"]
    cached_methods         = ["GET", "HEAD"]
    compress               = true

    forwarded_values {
      query_string = false
      cookies { forward = "none" }
    }

    min_ttl     = 0
    default_ttl = 300
    max_ttl     = 3600
  }

  # SPA: 403/404 → index.html
  custom_error_response {
    error_code         = 403
    response_code      = 200
    response_page_path = "/index.html"
  }

  custom_error_response {
    error_code         = 404
    response_code      = 200
    response_page_path = "/index.html"
  }

  restrictions {
    geo_restriction { restriction_type = "none" }
  }

  viewer_certificate {
    acm_certificate_arn      = aws_acm_certificate_validation.web.certificate_arn
    ssl_support_method       = "sni-only"
    minimum_protocol_version = "TLSv1.2_2021"
  }
}

# ─── Route53 A レコード（カスタムドメイン → CloudFront） ─────────────────────

resource "aws_route53_record" "web" {
  zone_id = var.hosted_zone_id
  name    = local.web_domain
  type    = "A"

  alias {
    name                   = aws_cloudfront_distribution.web.domain_name
    zone_id                = aws_cloudfront_distribution.web.hosted_zone_id
    evaluate_target_health = false
  }
}

# ─── index.html アップロード（API エンドポイント・Cognito 設定を埋め込み） ──

resource "aws_s3_object" "index_html" {
  bucket       = aws_s3_bucket.web.id
  key          = "index.html"
  content      = local.index_html_rendered
  content_type = "text/html"
  etag         = md5(local.index_html_rendered)
}

# ─── Bucket Policy: CloudFront OAC のみ許可 ──────────────────────────────────

resource "aws_s3_bucket_policy" "web" {
  bucket = aws_s3_bucket.web.id
  policy = jsonencode({
    Version = "2012-10-17"
    Statement = [{
      Sid    = "AllowCloudFront"
      Effect = "Allow"
      Principal = {
        Service = "cloudfront.amazonaws.com"
      }
      Action   = "s3:GetObject"
      Resource = "${aws_s3_bucket.web.arn}/*"
      Condition = {
        StringEquals = {
          "AWS:SourceArn" = aws_cloudfront_distribution.web.arn
        }
      }
    }]
  })
}
