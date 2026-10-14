# ESP32-P4 Flash and Test Script
# This script flashes the multi-backend firmware and runs validation tests

Write-Host ""
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host "ESP-IDF UART File Bridge - Flash and Test Multi-Backend Firmware" -ForegroundColor Cyan
Write-Host "================================================================================" -ForegroundColor Cyan
Write-Host ""

# Activate ESP-IDF environment
Write-Host "[1/4] Activating ESP-IDF v6.0.2..." -ForegroundColor Yellow
C:\esp\v6.0.2\esp-idf\export.ps1

# Flash firmware
Write-Host ""
Write-Host "[2/4] Flashing firmware to COM4..." -ForegroundColor Yellow
Write-Host "      (Make sure COM4 is not in use by Serial Monitor)" -ForegroundColor Gray
Write-Host ""

idf.py -p COM4 flash

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "❌ Flash failed! Is COM4 busy?" -ForegroundColor Red
    Write-Host "   Please close any Serial Monitor or program using COM4" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host ""
Write-Host "✅ Flash successful!" -ForegroundColor Green

# Wait for device to boot
Write-Host ""
Write-Host "[3/4] Waiting for device to boot (3 seconds)..." -ForegroundColor Yellow
Start-Sleep -Seconds 3

# Optional: Show boot log snippet
Write-Host ""
Write-Host "[4/4] Ready to test on COM13!" -ForegroundColor Yellow
Write-Host ""
Write-Host "To monitor boot log on COM4:" -ForegroundColor Cyan
Write-Host "  idf.py -p COM4 monitor" -ForegroundColor Gray
Write-Host ""
Write-Host "To run validation tests on COM13:" -ForegroundColor Cyan
Write-Host "  cd ../../tools_py" -ForegroundColor Gray
Write-Host "  python tests/test_all.py --port COM13" -ForegroundColor Gray
Write-Host ""

# Ask if user wants to run tests now
Write-Host ""
$runTests = Read-Host "Run validation tests now? (y/n)"

if ($runTests -eq 'y' -or $runTests -eq 'Y') {
    Write-Host ""
    Write-Host "Starting validation tests on COM13..." -ForegroundColor Yellow
    Write-Host ""
    
    $repoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
    $toolsPy = Join-Path $repoRoot "tools_py"
    Set-Location $toolsPy
    python tests\test_all.py --port COM13
    
    Write-Host ""
    if ($LASTEXITCODE -eq 0) {
        Write-Host "================================================================================" -ForegroundColor Green
        Write-Host "🎉 SUCCESS! All tests passed!" -ForegroundColor Green
        Write-Host "================================================================================" -ForegroundColor Green
    } else {
        Write-Host "================================================================================" -ForegroundColor Red
        Write-Host "⚠️  Some tests failed - see output above" -ForegroundColor Red
        Write-Host "================================================================================" -ForegroundColor Red
    }
} else {
    Write-Host ""
    Write-Host "Skipping tests. You can run them later with:" -ForegroundColor Yellow
    Write-Host "  cd tools_py" -ForegroundColor Gray
    Write-Host "  python tests/test_all.py --port COM13" -ForegroundColor Gray
}

Write-Host ""
Read-Host "Press Enter to exit"
