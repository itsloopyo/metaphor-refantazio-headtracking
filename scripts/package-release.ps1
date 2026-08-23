#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Build the offline installer ZIP for MetaphorHeadTracking.
.DESCRIPTION
    Consumes only files committed to the repo plus the compiled plugin from
    build/Release. Never touches the network. Stages release/artifact-contents/
    so the launcher-manifest.json sits at the ZIP root and the source paths in
    the manifest (vendor/..., plugins/...) resolve to real files in the ZIP.
#>
$ErrorActionPreference = 'Stop'

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
$ManifestPath = Join-Path $RepoRoot 'launcher-manifest.json'

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$version = $manifest.mod_info.version
if ([string]::IsNullOrWhiteSpace($version)) {
    throw "Could not read mod_info.version from $ManifestPath"
}

$AsiSource = Join-Path $RepoRoot 'build\Release\MetaphorHeadTracking.asi'
if (-not (Test-Path -LiteralPath $AsiSource)) {
    throw "Compiled plugin not found at $AsiSource. Run `pixi run build` first."
}

$ReleaseDir = Join-Path $RepoRoot 'release'
$StageDir = Join-Path $ReleaseDir 'artifact-contents'
if (Test-Path -LiteralPath $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

function Copy-Into {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$RelTarget
    )
    $src = Join-Path $RepoRoot $Source
    if (-not (Test-Path -LiteralPath $src)) {
        throw "Required package file missing: $Source"
    }
    $dst = Join-Path $StageDir $RelTarget
    $dstDir = Split-Path $dst -Parent
    if (-not (Test-Path -LiteralPath $dstDir)) {
        New-Item -ItemType Directory -Path $dstDir -Force | Out-Null
    }
    Copy-Item -LiteralPath $src -Destination $dst -Force
}

# Installer scripts. The launcher can deploy straight from launcher-manifest.json
# (manifest mode), but install.cmd / uninstall.cmd ship too for the manual
# `pixi run install` path and standalone installs, so the find-game shim they
# call (shared\find-game.ps1 + its module + games.json) must ship alongside them.
Copy-Into 'scripts\install.cmd'              'install.cmd'
Copy-Into 'scripts\uninstall.cmd'            'uninstall.cmd'
Copy-Into 'scripts\shared\find-game.ps1'         'shared\find-game.ps1'
Copy-Into 'scripts\shared\GamePathDetection.psm1' 'shared\GamePathDetection.psm1'
Copy-Into 'scripts\shared\games.json'            'shared\games.json'

# Compiled plugin (manifest source path: plugins/MetaphorHeadTracking.asi)
$pluginDst = Join-Path $StageDir 'plugins\MetaphorHeadTracking.asi'
New-Item -ItemType Directory -Path (Split-Path $pluginDst -Parent) -Force | Out-Null
Copy-Item -LiteralPath $AsiSource -Destination $pluginDst -Force

# Vendored ASI loader (manifest source path: vendor/ultimate-asi-loader/dinput8.dll)
Copy-Into 'vendor\ultimate-asi-loader\dinput8.dll' 'vendor\ultimate-asi-loader\dinput8.dll'
Copy-Into 'vendor\ultimate-asi-loader\LICENSE'     'vendor\ultimate-asi-loader\LICENSE'
Copy-Into 'vendor\ultimate-asi-loader\README.md'   'vendor\ultimate-asi-loader\README.md'

# Launcher manifest, re-stamped to the resolved version.
$manifest.mod_info.version = $version
$manifestJson = $manifest | ConvertTo-Json -Depth 10
Set-Content -LiteralPath (Join-Path $StageDir 'launcher-manifest.json') -Value $manifestJson -Encoding utf8

# Top-level docs
Copy-Into 'README.md'                'README.md'
Copy-Into 'LICENSE'                  'LICENSE'
Copy-Into 'CHANGELOG.md'             'CHANGELOG.md'
Copy-Into 'THIRD-PARTY-NOTICES.md'   'THIRD-PARTY-NOTICES.md'

$ZipPath = Join-Path $ReleaseDir "MetaphorHeadTracking-v$version-installer.zip"
if (Test-Path -LiteralPath $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -Path (Join-Path $StageDir '*') -DestinationPath $ZipPath -Force

Write-Host "Installer ZIP created:" -ForegroundColor Green
Write-Host "  $ZipPath"

# Nexus ZIP: extract-to-game-folder. The deploy subtree (the .asi that lands
# beside the game exe) plus the notice files below. Nexus users manage their own
# ASI loader, so no vendored loader and no install scripts.
$NexusStage = Join-Path $ReleaseDir 'nexus-contents'
if (Test-Path -LiteralPath $NexusStage) {
    Remove-Item -LiteralPath $NexusStage -Recurse -Force
}
New-Item -ItemType Directory -Path $NexusStage -Force | Out-Null
Copy-Item -LiteralPath $AsiSource -Destination (Join-Path $NexusStage 'MetaphorHeadTracking.asi') -Force

$NexusZip = Join-Path $ReleaseDir "MetaphorHeadTracking-v$version-nexus.zip"
if (Test-Path -LiteralPath $NexusZip) {
    Remove-Item -LiteralPath $NexusZip -Force
}
# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $RepoRoot $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $NexusStage -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Compress-Archive -Path (Join-Path $NexusStage '*') -DestinationPath $NexusZip -Force

Write-Host "Nexus ZIP created:" -ForegroundColor Green
Write-Host "  $NexusZip"
exit 0
