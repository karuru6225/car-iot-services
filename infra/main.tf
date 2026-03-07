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

data "aws_iot_endpoint" "main" {
  endpoint_type = "iot:Data-ATS"
}

data "aws_caller_identity" "current" {}
