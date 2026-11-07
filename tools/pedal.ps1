param(
    [ValidateSet("build", "flash", "test", "doctor")][string]$Action = "build",
    [string]$Environment = "f103c8",
    [string]$ToolRoot = "",
    [string]$HostGcc = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
if (-not $ToolRoot) { $ToolRoot = Join-Path $root "../.tools/stm32" }
$env:PLATFORMIO_CORE_DIR = Join-Path $ToolRoot "pio"
$pio = Join-Path $ToolRoot "venv/Scripts/pio.exe"
if (-not (Test-Path $pio)) {
    throw "Install Python venv + platformio==6.1.18 under $ToolRoot. See docs/STM32_PEDAL.md."
}
if ($Action -eq "doctor") {
    & $pio --version
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $pio pkg list -d (Join-Path $root "firmware/pedal")
    exit $LASTEXITCODE
}
if ($Action -eq "test") {
    if (-not $HostGcc) { $HostGcc = Join-Path $root "../.tools/w64devkit/w64devkit/bin/gcc.exe" }
    $sdk = Join-Path $env:PLATFORMIO_CORE_DIR "packages/framework-libopencm3/include"
    if (-not (Test-Path $sdk)) { throw "Run build first to install pinned USB headers." }
    $outputDir = Join-Path $root "tests/build"
    New-Item -ItemType Directory -Force $outputDir | Out-Null
    $exe = Join-Path $outputDir "test_stm32_usb.exe"
    & $HostGcc -std=c11 -Wall -Wextra -Werror "-I$sdk" "-I$root/firmware/pedal/include" `
        "-I$root/firmware/components/gg_core/include" "-I$root/firmware/components/gg_app/include" `
        "$root/tests/test_stm32_usb.c" "$root/firmware/components/gg_core/src/gg_usb_link.c" -o $exe
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $exe
    exit $LASTEXITCODE
}
$arguments = @("run", "-d", (Join-Path $root "firmware/pedal"), "-e", $Environment)
if ($Action -eq "flash") { $arguments += @("-t", "upload") }
& $pio @arguments
exit $LASTEXITCODE
