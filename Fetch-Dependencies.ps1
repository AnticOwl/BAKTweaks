$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$third = Join-Path $root "third_party"
New-Item -ItemType Directory -Force -Path $third | Out-Null

function Download-And-Extract($url, $dest, $expectedFolder) {
    $zip = Join-Path $env:TEMP (([Guid]::NewGuid().ToString()) + ".zip")
    $tmp = Join-Path $env:TEMP (([Guid]::NewGuid().ToString()))

    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing

    New-Item -ItemType Directory -Force -Path $tmp | Out-Null
    Expand-Archive -Path $zip -DestinationPath $tmp -Force

    $source = Join-Path $tmp $expectedFolder
    if (-not (Test-Path $source)) {
        throw "Expected folder not found after extraction: $source"
    }

    if (Test-Path $dest) {
        Remove-Item -Recurse -Force $dest
    }
    Move-Item -Path $source -Destination $dest

    Remove-Item -Force $zip
    Remove-Item -Recurse -Force $tmp
}

Download-And-Extract "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip" (Join-Path $third "imgui") "imgui-1.91.9b"
Download-And-Extract "https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.zip" (Join-Path $third "minhook") "minhook-1.3.4"

Write-Host "Dependencies ready. No Git required."
