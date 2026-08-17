#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Dev convenience: deploy the freshly built plugin + ASI loader to the game.
.DESCRIPTION
    Copies build/Release/MetaphorHeadTracking.asi into the game's exe directory
    and, if no winmm.dll is present, drops the vendored Ultimate ASI Loader as
    winmm.dll. Game detection mirrors install.cmd: explicit -GamePath wins,
    otherwise GamePathDetection.psm1 resolves it (env var -> Steam registry ->
    games.json). No prompts; exits non-zero on any failure.
#>
param(
    [string]$GamePath
)
$ErrorActionPreference = 'Stop'

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$GameId = 'metaphor'

Import-Module (Join-Path $RepoRoot 'cameraunlock-core\powershell\GamePathDetection.psm1') -Force

$AsiSource = Join-Path $RepoRoot 'build\Release\MetaphorHeadTracking.asi'
if (-not (Test-Path -LiteralPath $AsiSource)) {
    throw "Compiled plugin not found at $AsiSource. Run `pixi run build` first."
}

if (-not $GamePath) {
    $GamePath = Find-GamePath -GameId $GameId
}
if (-not $GamePath) {
    throw "Could not locate the game. Pass -GamePath 'C:\path\to\METAPHOR' or set METAPHOR_PATH."
}
if (-not (Test-Path -LiteralPath $GamePath -PathType Container)) {
    throw "Game path does not exist: $GamePath"
}

$config = (Get-GameConfigs)[$GameId]
$exeRelPath = $config.Executable
$ExeDir = Split-Path (Join-Path $GamePath $exeRelPath) -Parent

Write-Host "Game directory: $GamePath" -ForegroundColor Cyan
Write-Host "Exe directory : $ExeDir"

Copy-Item -LiteralPath $AsiSource -Destination (Join-Path $ExeDir 'MetaphorHeadTracking.asi') -Force
Write-Host "  Deployed MetaphorHeadTracking.asi"

$LoaderTarget = Join-Path $ExeDir 'winmm.dll'
if (Test-Path -LiteralPath $LoaderTarget) {
    Write-Host "  winmm.dll already present - leaving it alone"
} else {
    $VendorDll = Join-Path $RepoRoot 'vendor\ultimate-asi-loader\dinput8.dll'
    if (-not (Test-Path -LiteralPath $VendorDll)) {
        throw "Vendored ASI loader missing at $VendorDll. Run `pixi run update-deps`."
    }
    Copy-Item -LiteralPath $VendorDll -Destination $LoaderTarget -Force
    Write-Host "  Installed Ultimate ASI Loader as winmm.dll"
}

Write-Host "Deploy complete." -ForegroundColor Green
exit 0
