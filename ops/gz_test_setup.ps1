# gz_test_setup.ps1
# Create a temporary public S3 bucket for gz compression test.
# Anyone can PUT/GET - use only for short-term testing, then delete the bucket.
#
# Usage:
#   cd ops
#   .\gz_test_setup.ps1
#   .\gz_test_setup.ps1 -Profile myprofile -Region ap-northeast-1

param(
    [string]$Profile = '',
    [string]$Region  = 'ap-northeast-1'
)

$ErrorActionPreference = "Stop"

if ($Profile) {
    $env:AWS_PROFILE = $Profile
    $credEnv = aws configure export-credentials --profile $Profile --format powershell
    if ($LASTEXITCODE -ne 0) { Write-Error "Failed to get credentials."; exit 1 }
    Invoke-Expression ($credEnv -join "`n")
}

$Python  = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$Rnd     = Get-Random -Maximum 99999
$Bucket  = "gz-test-esp32-$Rnd"
$GetKey  = "test_data.gz"
$PutKey  = "upload.gz"
$TmpGz   = [System.IO.Path]::GetTempFileName() + ".gz"

Write-Host "==> Creating bucket: $Bucket"
aws s3 mb "s3://$Bucket" --region $Region
if ($LASTEXITCODE -ne 0) { Write-Error "s3 mb failed."; exit 1 }

Write-Host "==> Disabling public access block..."
aws s3api put-public-access-block --bucket $Bucket --public-access-block-configuration "BlockPublicAcls=false,IgnorePublicAcls=false,BlockPublicPolicy=false,RestrictPublicBuckets=false"
if ($LASTEXITCODE -ne 0) { Write-Error "put-public-access-block failed."; exit 1 }

Write-Host "==> Applying public GET+PUT bucket policy..."
$Policy = '{"Version":"2012-10-17","Statement":[{"Effect":"Allow","Principal":"*","Action":["s3:GetObject","s3:PutObject"],"Resource":"arn:aws:s3:::' + $Bucket + '/*"}]}'
$TmpPolicy = [System.IO.Path]::GetTempFileName()
$Policy | Set-Content -Path $TmpPolicy -Encoding ascii
aws s3api put-bucket-policy --bucket $Bucket --policy "file://$TmpPolicy"
if ($LASTEXITCODE -ne 0) { Write-Error "put-bucket-policy failed."; exit 1 }
Remove-Item $TmpPolicy

Write-Host "==> Creating test data (256 bytes: 0x00-0xff) and compressing..."
& $Python -c "
import gzip, sys
data = bytes(range(256))
with gzip.open(sys.argv[1], 'wb') as f:
    f.write(data)
print('  compressed:', len(open(sys.argv[1],'rb').read()), 'bytes')
" $TmpGz
if ($LASTEXITCODE -ne 0) { Write-Error "Python gzip failed."; exit 1 }

Write-Host "==> Uploading test_data.gz to s3://$Bucket/$GetKey ..."
aws s3 cp $TmpGz "s3://$Bucket/$GetKey" --content-type application/gzip --no-sign-request
if ($LASTEXITCODE -ne 0) {
    # fallback: signed upload
    aws s3 cp $TmpGz "s3://$Bucket/$GetKey" --content-type application/gzip
    if ($LASTEXITCODE -ne 0) { Write-Error "s3 cp failed."; exit 1 }
}
Remove-Item $TmpGz

# S3 path-style URL: AT+SHCONF の URL 最大 64 バイト制限を回避
$Base   = "https://s3.$Region.amazonaws.com"
$GetUrl = "$Base/$Bucket/$GetKey"
$PutUrl = "$Base/$Bucket/$PutKey"

Write-Host ""
Write-Host "============================================================"
Write-Host "Bucket created. Delete it after testing:"
Write-Host "  aws s3 rb s3://$Bucket --force"
Write-Host "============================================================"
Write-Host ""
Write-Host "[PUT URL] paste when device shows 'PUT URL:':"
Write-Host $PutUrl
Write-Host ""
Write-Host "[GET URL] paste when device shows 'GET URL:':"
Write-Host $GetUrl
Write-Host ""
Write-Host "Expected test data: 256 bytes (0x00..0xff)"
