$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root

# Always configure from a clean build tree.
# This prevents an old BatTweaks CMake cache/target from being reused.
$buildDir = Join-Path $root "build"
if (Test-Path $buildDir) {
    Write-Host "Removing previous build directory..." -ForegroundColor DarkGray
    Remove-Item $buildDir -Recurse -Force
}

cmake -S . -B build -A x64
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed." }

cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

Write-Host ""
Write-Host "DLL:" -ForegroundColor Green
Write-Host (Join-Path $root "build\Release\BAKTweaks.dll")
