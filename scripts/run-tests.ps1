#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Configure, build and run the host-side unit tests.
.DESCRIPTION
    The test targets are behind METAPHOR_BUILD_TESTS, which is OFF in the build tree
    `pixi run build` uses, so they get their own tree in build-tests/ and the shipped .asi in
    build/ is never built out of a tree configured for tests.

    Non-interactive: exits 0 when every test passes, non-zero with ctest's own output on the
    first failure.
.NOTES
    Run via: pixi run test
#>
param(
    # Build the test binaries without running them (pixi run build-tests).
    [switch]$BuildOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build-tests'

# The differential test is only as good as its claim about what it compiled. SHA256 through
# .NET, because Get-FileHash is not found when a runner's pwsh runs this under Windows
# PowerShell.
$provenance = Join-Path $projectRoot 'tests/config_differential/provenance.txt'
$sha256 = [System.Security.Cryptography.SHA256]::Create()
foreach ($line in Get-Content $provenance) {
    if ($line -match '^\s*(#|$)') { continue }
    $hash, $path = ($line -split '\s+', 3)[0, 1]
    $bytes = [System.IO.File]::ReadAllBytes((Join-Path $projectRoot $path))
    $actual = -join ($sha256.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') })
    if ($actual -ne $hash) { throw "$path has changed: sha256 $actual, provenance.txt records $hash" }
}

& cmake -S $projectRoot -B $buildDir -G 'Visual Studio 18 2026' -A x64 -DMETAPHOR_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { Write-Host 'ERROR: cmake configure failed' -ForegroundColor Red; exit 1 }

& cmake --build $buildDir --config Release
if ($LASTEXITCODE -ne 0) { Write-Host 'ERROR: test build failed' -ForegroundColor Red; exit 1 }
if ($BuildOnly) { exit 0 }

& ctest --test-dir $buildDir --build-config Release --output-on-failure
if ($LASTEXITCODE -ne 0) { Write-Host 'ERROR: tests failed' -ForegroundColor Red; exit 1 }

Write-Host 'All tests passed.' -ForegroundColor Green
