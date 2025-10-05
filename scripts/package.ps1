# DQX Utility Portable Package Builder

param(
    [switch]$Clean,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

Write-Host "=== DQX Utility Portable Package Builder ===" -ForegroundColor Cyan

if (-not $IsWindows -and $PSVersionTable.PSVersion.Major -ge 6) {
    Write-Host "Error: Packaging is only supported on Windows" -ForegroundColor Red
    exit 1
}

if ($Clean) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    if (Test-Path "out/windows-msvc-app-release") {
        Remove-Item -Path "out/windows-msvc-app-release" -Recurse -Force
    }
}

Write-Host "`nConfiguring project..." -ForegroundColor Green
cmake --preset windows-msvc-app-release
if ($LASTEXITCODE -ne 0) {
    Write-Host "Configuration failed!" -ForegroundColor Red
    exit 1
}

if (-not $SkipBuild) {
    Write-Host "`nBuilding project..." -ForegroundColor Green
    cmake --build --preset windows-msvc-app-release
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }
}

Write-Host "`nCreating portable package..." -ForegroundColor Green
cpack --preset windows-portable
if ($LASTEXITCODE -ne 0) {
    Write-Host "Packaging failed!" -ForegroundColor Red
    exit 1
}

$packagePath = Get-ChildItem -Path "out/windows-msvc-app-release" -Filter "dqx-utility-*.zip" | Select-Object -First 1

if ($packagePath) {
    Write-Host "`n=== SUCCESS ===" -ForegroundColor Green
    Write-Host "Portable package created:" -ForegroundColor Cyan
    Write-Host "  $($packagePath.FullName)" -ForegroundColor White
    Write-Host "`nPackage size: $([math]::Round($packagePath.Length / 1MB, 2)) MB" -ForegroundColor Gray
    Write-Host "`nContents:" -ForegroundColor Cyan
    Write-Host "  - dqx-utility.exe (main application)" -ForegroundColor White
    Write-Host "  - dqxclarity-cpp.exe (helper tool)" -ForegroundColor White
    Write-Host "  - SDL3.dll (required library)" -ForegroundColor White
} else {
    Write-Host "`nWarning: Package file not found" -ForegroundColor Yellow
}

Write-Host "`nTo distribute:" -ForegroundColor Cyan
Write-Host "  1. Extract the ZIP anywhere" -ForegroundColor White
Write-Host "  2. Double-click dqx-utility.exe" -ForegroundColor White
Write-Host "  3. All data stored in same folder (fully portable)" -ForegroundColor White
