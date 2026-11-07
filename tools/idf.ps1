[CmdletBinding()]
param(
    [ValidateSet('gun-controller', 'pedal-controller')]
    [string]$Project = 'gun-controller',
    [ValidateSet('doctor', 'build', 'flash', 'monitor', 'menuconfig')]
    [string]$Action = 'doctor',
    [ValidateSet('hid', 'jtag')]
    [string]$Mode = 'hid',
    [string]$Port,
    [switch]$DryRun
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$projectDir = Join-Path $repoRoot "firmware/$Project"
$localFile = Join-Path $PSScriptRoot 'local.json'
$idfRoot = $env:IDF_PATH
if (Test-Path -LiteralPath $localFile) {
    $localSettings = Get-Content -Raw -LiteralPath $localFile | ConvertFrom-Json
    if ($localSettings.idfPath) { $idfRoot = $localSettings.idfPath }
}
$bundledRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '../.tools/esp32'))
if (-not $idfRoot -and (Test-Path -LiteralPath "$bundledRoot/esp-idf/export.ps1")) {
    $idfRoot = Join-Path $bundledRoot 'esp-idf'
}
if ($idfRoot -and [IO.Path]::GetFullPath($idfRoot) -eq [IO.Path]::GetFullPath("$bundledRoot/esp-idf")) {
    $env:IDF_TOOLS_PATH = Join-Path $bundledRoot 'espressif'
    $env:IDF_PYTHON_ENV_PATH = Join-Path $env:IDF_TOOLS_PATH 'python_env/idf5.4_py3.11_env'
    $env:PATH = "$bundledRoot/Python311;$env:PATH"
}
if ($Action -eq 'doctor') {
    Write-Host "Project: $projectDir"
    Write-Host "IDF_PATH/local.json: $idfRoot"
    Write-Host "UART ports: $([string]::Join(', ', [System.IO.Ports.SerialPort]::GetPortNames()))"
    if (-not $idfRoot -or -not (Test-Path -LiteralPath (Join-Path $idfRoot 'export.ps1'))) {
        throw 'ESP-IDF not configured. Install ESP-IDF and set tools/local.json idfPath or IDF_PATH.'
    }
}
if (($Action -eq 'flash' -or $Action -eq 'monitor') -and $Port -notmatch '^COM[1-9][0-9]*$') {
    throw 'Specify the UART bridge port, for example -Port COM5.'
}
$buildDir = Join-Path $projectDir "build-$Mode"
$sdkconfig = Join-Path $projectDir "sdkconfig.$Mode"
$defaults = "$(Join-Path $projectDir 'sdkconfig.defaults');$(Join-Path $projectDir "sdkconfig.defaults.$Mode")"
$idfArgs = @('-C', $projectDir, '-B', $buildDir, '-D', 'IDF_TARGET=esp32s3', '-D', "SDKCONFIG=$sdkconfig", '-D', "SDKCONFIG_DEFAULTS=$defaults")
if ($Port) { $idfArgs += @('-p', $Port) }
if ($Action -eq 'doctor') { $idfArgs = @('--version') } else { $idfArgs += $Action }
if ($DryRun) {
    # JSON is only a diagnostic display, never executed as a shell command.
    Write-Output (ConvertTo-Json -InputObject $idfArgs -Compress)
    exit 0
}
if (-not $idfRoot -or -not (Test-Path -LiteralPath (Join-Path $idfRoot 'export.ps1'))) {
    throw 'ESP-IDF not configured. Run GG: Doctor and follow docs/DEBUGGING.md.'
}
# Import the environment in this process; do not invoke export.bat from PowerShell.
. (Join-Path $idfRoot 'export.ps1')
if ($env:IDF_PYTHON_ENV_PATH) {
    $idfPython = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'
} else {
    $idfPython = (Get-Command python -ErrorAction Stop).Source
}
if (-not (Test-Path -LiteralPath $idfPython)) { throw "IDF Python missing: $idfPython" }
& $idfPython (Join-Path $idfRoot 'tools/idf.py') @idfArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
