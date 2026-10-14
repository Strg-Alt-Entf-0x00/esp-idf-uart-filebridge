# ESP-IDF UART File Bridge - Flash and Test Script
# Adjust the variables below to match your setup.

param(
    [string]$FlashPort = "COM4",
    [string]$TestPort  = "COM13"
)

Write-Host ""
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "ESP-IDF UART File Bridge - Flash and Test Multi-Backend Firmware" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host ""

# Flash firmware
Write-Host "[1/3] Flashing firmware to $FlashPort..." -ForegroundColor Yellow
Write-Host "      (Make sure $FlashPort is not in use by Serial Monitor)" -ForegroundColor Gray
Write-Host ""

idf.py -p $FlashPort flash

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[FAIL] Flash failed! Is $FlashPort busy?" -ForegroundColor Red
    Write-Host "       Please close any Serial Monitor or program using $FlashPort" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host ""
Write-Host "[OK] Flash successful!" -ForegroundColor Green

# Wait for device to boot
Write-Host ""
Write-Host "[2/3] Waiting for device to boot (3 seconds)..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

# Show next steps
Write-Host ""
Write-Host "[3/3] Ready to test on $TestPort!" -ForegroundColor Yellow
Write-Host ""
Write-Host "To monitor boot log on ${FlashPort}:" -ForegroundColor Cyan
Write-Host "  idf.py -p $FlashPort monitor" -ForegroundColor Gray
Write-Host ""
Write-Host "To run validation tests on ${TestPort}:" -ForegroundColor Cyan
Write-Host "  cd ../../tools_py" -ForegroundColor Gray
Write-Host "  python tests/test_all.py --port $TestPort" -ForegroundColor Gray
Write-Host ""

# Ask if user wants to run tests now
Write-Host ""
$runTests = Read-Host "Run validation tests now? (y/n)"

if ($runTests -eq 'y' -or $runTests -eq 'Y') {
    Write-Host ""
    Write-Host "Starting validation tests on $TestPort..." -ForegroundColor Yellow
    Write-Host ""

    $repoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
    $toolsPy = Join-Path $repoRoot "tools_py"
    Set-Location $toolsPy
    python tests\test_all.py --port $TestPort

    Write-Host ""
    if ($LASTEXITCODE -eq 0) {
        Write-Host "================================================================================" -ForegroundColor Green
        Write-Host "SUCCESS - All tests passed!" -ForegroundColor Green
        Write-Host "================================================================================" -ForegroundColor Green
    } else {
        Write-Host "================================================================================" -ForegroundColor Red
        Write-Host "FAILED - Some tests failed, see output above" -ForegroundColor Red
        Write-Host "================================================================================" -ForegroundColor Red
    }
} else {
    Write-Host ""
    Write-Host "Skipping tests. You can run them later with:" -ForegroundColor Yellow
    Write-Host "  cd tools_py" -ForegroundColor Gray
    Write-Host "  python tests/test_all.py --port $TestPort" -ForegroundColor Gray
}

Write-Host ""
Read-Host "Press Enter to exit"
