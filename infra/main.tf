terraform {
  required_version = ">= 1.5"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
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
