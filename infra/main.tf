terraform {
  required_version = ">= 1.5"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }

  # S3 バックエンド（tfstate をリモート管理）
  # backend.tfbackend に設定値を記載し、terraform init -backend-config=backend.tfbackend で初期化
  backend "s3" {}
}

provider "aws" {
  region = var.aws_region

  default_tags {
    tags = {
      Project = var.project
    }
  }
}

# CloudFront 用 ACM 証明書は us-east-1 でのみ作成可能
provider "aws" {
  alias  = "us_east_1"
  region = "us-east-1"

  default_tags {
    tags = {
      Project = var.project
    }
  }
}

data "aws_iot_endpoint" "main" {
  endpoint_type = "iot:Data-ATS"
}

data "aws_caller_identity" "current" {}
