#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Refresh the vendored Ultimate ASI Loader (manual dev action).
.DESCRIPTION
    Downloads the latest v9.x Ultimate-ASI-Loader_x64.zip, extracts the single
    dinput8.dll, and commits it as vendor/ultimate-asi-loader/dinput8.dll. The
    install/package scripts expect the raw DLL, not the zip, so the zip is
    removed afterwards. Review the diff and commit. Never runs in CI/build.
#>
$ErrorActionPreference = 'Stop'

$projectDir = Resolve-Path (Join-Path $PSScriptRoot '..')
$vendorDir = Join-Path $projectDir 'vendor\ultimate-asi-loader'

Import-Module (Join-Path $projectDir 'cameraunlock-core\powershell\ModLoaderSetup.psm1') -Force

Update-VendoredLoader `
    -Name 'ultimate-asi-loader' `
    -OutputDir $vendorDir `
    -OutputFileName 'Ultimate-ASI-Loader_x64.zip' `
    -Owner 'ThirteenAG' `
    -Repo 'Ultimate-ASI-Loader' `
    -VersionPrefix 'v9.' `
    -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$' | Out-Null

$zipPath = Join-Path $vendorDir 'Ultimate-ASI-Loader_x64.zip'
if (-not (Test-Path -LiteralPath $zipPath)) {
    throw "Expected downloaded zip not found at $zipPath"
}

$dllPath = Join-Path $vendorDir 'dinput8.dll'

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $entry = $archive.Entries | Where-Object { $_.Name -ieq 'dinput8.dll' } | Select-Object -First 1
    if (-not $entry) {
        throw "dinput8.dll not found inside $zipPath"
    }
    if (Test-Path -LiteralPath $dllPath) {
        Remove-Item -LiteralPath $dllPath -Force
    }
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $dllPath, $true)
} finally {
    $archive.Dispose()
}

Remove-Item -LiteralPath $zipPath -Force

$hash = (Get-FileHash -LiteralPath $dllPath -Algorithm SHA256).Hash
Write-Host "Vendored dinput8.dll updated." -ForegroundColor Green
Write-Host "  $dllPath"
Write-Host "  SHA-256: $hash"
exit 0
