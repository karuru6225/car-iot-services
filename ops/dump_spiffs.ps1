# dump_spiffs.ps1
# Download SPIFFS from device via USB and extract files.
#
# Usage:
#   cd ops
#   .\dump_spiffs.ps1 -Port COM6
#   .\dump_spiffs.ps1 -Port COM6 -OutputDir C:\tmp\spiffs_out

param(
    [Parameter(Mandatory=$true)]
    [string]$Port,
    [string]$OutputDir = "$PSScriptRoot\spiffs_extracted"
)

$ErrorActionPreference = "Stop"

$Python   = "$HOME\.platformio\penv\Scripts\python.exe"
$Esptool  = "$HOME\.platformio\packages\tool-esptoolpy\esptool.py"
$Mkspiffs = "$HOME\.platformio\packages\tool-mkspiffs\mkspiffs_espressif32_arduino.exe"

# Temp bin stored next to this script
$SpiffsBin = "$PSScriptRoot\spiffs_dump.bin"

# SPIFFS partition from partitions_two_ota.csv
$SpiffsOffset = 0x790000
$SpiffsSize   = 0x70000
$PageSize     = 256
$BlockSize    = 4096

Write-Host "[1/3] Reading SPIFFS from device ($Port)..."
& $Python $Esptool --chip esp32s3 --port $Port read_flash $SpiffsOffset $SpiffsSize $SpiffsBin
if ($LASTEXITCODE -ne 0) { Write-Error "esptool read_flash failed."; exit 1 }

Write-Host "[2/3] Extracting files -> $OutputDir"
if (Test-Path $OutputDir) { Remove-Item $OutputDir -Recurse -Force }
New-Item $OutputDir -ItemType Directory | Out-Null

# mkspiffs cannot handle absolute Windows paths; use Push-Location + relative path
$OutputLeaf   = Split-Path $OutputDir -Leaf
$OutputParent = Split-Path $OutputDir -Parent

Push-Location $OutputParent
try {
    & $Mkspiffs -u $OutputLeaf -p $PageSize -b $BlockSize -s $SpiffsSize $SpiffsBin
    if ($LASTEXITCODE -ne 0) { Write-Error "mkspiffs extraction failed."; exit 1 }
}
finally {
    Pop-Location
}

Remove-Item $SpiffsBin -ErrorAction SilentlyContinue

Write-Host "[3/3] Done."
Write-Host ""

$files = Get-ChildItem $OutputDir -Recurse -File | Sort-Object FullName
if ($files.Count -eq 0) {
    Write-Host "  (no files)"
}
else {
    foreach ($f in $files) {
        $rel  = $f.FullName.Substring((Resolve-Path $OutputDir).Path.Length)
        $size = "{0,8} bytes" -f $f.Length
        Write-Host "  $size  $rel"
    }
}

Write-Host ""
Write-Host "To view logs:"
$hint = "Get-Content " + $OutputDir + "\log_*.txt | Sort-Object"
Write-Host "  $hint"
