param(
    [string]$Gcc = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force $outDir | Out-Null

if (-not $Gcc) {
    $cmd = Get-Command gcc -ErrorAction SilentlyContinue
    if ($cmd) {
        $Gcc = $cmd.Source
    } else {
        $fallback = Join-Path $root "..\.tools\w64devkit\w64devkit\bin\gcc.exe"
        if (Test-Path $fallback) {
            $Gcc = $fallback
        }
    }
}
if (-not $Gcc -or -not (Test-Path $Gcc)) {
    Write-Error "No host C compiler found. Install gcc or pass -Gcc <path>."
}

$core = Join-Path $root "firmware\components\gg_core"
$app = Join-Path $root "firmware/components/gg_app"
$pedal = Join-Path $app "src"
$exe = Join-Path $outDir "test_gg_core.exe"

$sources = @(
    (Join-Path $PSScriptRoot "test_gg_core.c"),
    (Join-Path $core "src\gg_debounce.c"),
    (Join-Path $core "src\gg_calibration.c"),
    (Join-Path $core "src\gg_aim.c"),
    (Join-Path $core "src\gg_hid_output.c"),
    (Join-Path $core "src\gg_gun_controller.c"),
    (Join-Path $core "src\gg_usb_link.c"),
    (Join-Path $pedal "pedal_keys.c")
)

& $Gcc -std=c11 -O1 -g -Wall -Wextra -Werror `
    "-I$(Join-Path $core 'include')" "-I$(Join-Path $app 'include')" "-I$(Join-Path $root 'firmware/platform/esp32/gun')" `
    $sources -o $exe -lm
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$usb = Join-Path $root "firmware/platform/esp32/components/gg_usb"
$usbExe = Join-Path $outDir "test_usb_transport.exe"
& $Gcc -std=c11 -O1 -g -Wall -Wextra -Werror `
    "-I$(Join-Path $core 'include')" "-I$(Join-Path $usb 'include')" "-I$PSScriptRoot" `
    (Join-Path $PSScriptRoot "test_usb_transport.c") (Join-Path $core "src/gg_usb_link.c") `
    -o $usbExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $usbExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The complete application compiles with ONLY portable headers.
$appExe = Join-Path $outDir "test_app_hal.exe"
& $Gcc -std=c11 -O1 -g -Wall -Wextra -Werror `
    "-I$(Join-Path $core 'include')" "-I$(Join-Path $app 'include')" `
    (Join-Path $PSScriptRoot "test_app_hal.c") (Join-Path $app "src/gg_app.c") `
    (Join-Path $app "src/pedal_keys.c") `
    (Join-Path $core "src/gg_debounce.c") (Join-Path $core "src/gg_calibration.c") `
    (Join-Path $core "src/gg_aim.c") (Join-Path $core "src/gg_hid_output.c") `
    (Join-Path $core "src/gg_gun_controller.c") -o $appExe -lm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $appExe
exit $LASTEXITCODE
