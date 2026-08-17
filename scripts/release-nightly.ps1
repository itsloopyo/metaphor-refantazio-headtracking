#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Publish a rolling `dev` GitHub pre-release for MetaphorHeadTracking.
#>
[CmdletBinding()]
param([switch]$AllowDirty)
$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$manifest = Get-Content -LiteralPath (Join-Path $ProjectRoot 'launcher-manifest.json') -Raw | ConvertFrom-Json
$version = $manifest.mod_info.version

Publish-NightlyBuild `
    -ModId 'metaphor' `
    -ModName 'MetaphorHeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -BuildCommand 'pixi run build' `
    -AllowDirty:$AllowDirty
