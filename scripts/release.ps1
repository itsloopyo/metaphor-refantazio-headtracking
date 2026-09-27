#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Release entry point. Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
.DESCRIPTION
    Fully unattended. The command-line invocation is the authorization - there
    are no prompts. Deterministic preconditions (on main, clean tree, tag
    absent, valid semver) fail-fast with a non-zero exit instead of asking.
    Never destructive: no force push, no amend, no tag overwrite.

    Canonical version source is launcher-manifest.json (mod_info.version);
    src/MetaphorHeadTracking/version.h and the CMakeLists.txt project version
    are kept in sync so the compiled .asi reports the released version.
#>
param(
    [Parameter(Mandatory = $true)][string]$Version
)
$ErrorActionPreference = 'Stop'

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
$noticesRoot = Split-Path -Parent $PSScriptRoot
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) { throw "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run." }
& (Join-Path $noticesRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $noticesRoot
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $noticesRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $noticesRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

$usage = 'Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force

$ManifestPath = Join-Path $ProjectRoot 'launcher-manifest.json'
$VersionHeader = Join-Path $ProjectRoot 'src\MetaphorHeadTracking\version.h'
$CMakeLists = Join-Path $ProjectRoot 'CMakeLists.txt'
$InstallCmdPath = Join-Path $ProjectRoot 'scripts\install.cmd'

$manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
$currentVersion = $manifest.mod_info.version

# 1. Resolve + validate semver.
try {
    $newVersion = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
} catch {
    Write-Host $usage
    Write-Host $_.Exception.Message
    exit 2
}
if (-not (Test-SemanticVersion -Version $newVersion)) {
    Write-Host $usage
    Write-Host "Resolved version '$newVersion' is not X.Y.Z."
    exit 2
}

# launcher-manifest.json names the first version that reads CameraUnlock.ini, and nothing on the
# release path otherwise holds the version to it before the tag is pushed.
Assert-ReleaseNotBelowCanonicalSince -RepoRoot $ProjectRoot -Version $newVersion

# 2. Preconditions: on main, clean tree, tag absent.
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Host "Refusing to release: on branch '$branch', not 'main'."
    exit 1
}
if (-not (Test-CleanGitStatus)) {
    Write-Host "Refusing to release: working tree is dirty. Commit or stash first."
    exit 1
}
$tag = "v$newVersion"
if (Test-GitTagExists -Tag $tag) {
    Write-Host "Refusing to release: tag '$tag' already exists."
    exit 1
}

Write-Host "Releasing $currentVersion -> $newVersion (tag $tag)" -ForegroundColor Cyan

# 3. Update the canonical version source + the derived copies.
# mod_info.version is the only version key here, and Update-ManifestVersion
# only reaches a top-level one. UTF-8 without a BOM: PowerShell 5.1's
# -Encoding UTF8 prefixes EF BB BF, which the launcher's parser rejects.
$manifest.mod_info.version = $newVersion
[System.IO.File]::WriteAllText($ManifestPath, ($manifest | ConvertTo-Json -Depth 10),
                               (New-Object System.Text.UTF8Encoding $false))

$headerContent = Get-Content -LiteralPath $VersionHeader -Raw
if ($headerContent -notmatch '#define METAPHOR_HT_VERSION "[^"]+"') {
    throw "No METAPHOR_HT_VERSION define found in $VersionHeader"
}
$headerContent = $headerContent -replace '#define METAPHOR_HT_VERSION "[^"]+"', "#define METAPHOR_HT_VERSION `"$newVersion`""
Set-Content -LiteralPath $VersionHeader -Value $headerContent -NoNewline

$cmakeContent = Get-Content -LiteralPath $CMakeLists -Raw
if ($cmakeContent -notmatch 'project\(MetaphorHeadTracking VERSION \d+\.\d+\.\d+') {
    throw "No 'project(MetaphorHeadTracking VERSION X.Y.Z' found in $CMakeLists"
}
$cmakeContent = $cmakeContent -replace '(project\(MetaphorHeadTracking VERSION )\d+\.\d+\.\d+', "`${1}$newVersion"
Set-Content -LiteralPath $CMakeLists -Value $cmakeContent -NoNewline

# install.cmd's MOD_VERSION is what the install writes into the launcher's
# state file, which is where the launcher looks to spot a stale install.
$installCmdContent = Get-Content -LiteralPath $InstallCmdPath -Raw
if ($installCmdContent -notmatch 'set "MOD_VERSION=[^"]+"') {
    throw "No MOD_VERSION line found in $InstallCmdPath"
}
$installCmdContent = $installCmdContent -replace 'set "MOD_VERSION=[^"]+"', "set `"MOD_VERSION=$newVersion`""
Set-Content -LiteralPath $InstallCmdPath -Value $installCmdContent -NoNewline

# 4. Release build. Abort on failure.
Push-Location $ProjectRoot
try {
    pixi run build
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed (exit $LASTEXITCODE). Aborting release."
        exit 1
    }
} finally {
    Pop-Location
}

# 5. Generate the changelog from commits since the last tag.
New-ChangelogFromCommits -ChangelogPath (Join-Path $ProjectRoot 'CHANGELOG.md') -Version $newVersion | Out-Null

# 6. Commit the version bump + changelog.
$null = Invoke-VersionCommit -Version $newVersion -Files @(
    $ManifestPath,
    $VersionHeader,
    $CMakeLists,
    $InstallCmdPath,
    (Join-Path $ProjectRoot 'CHANGELOG.md')
)

# 7-8. Annotated tag + push commits and tag (triggers release.yml).
New-ReleaseTag -Version $newVersion -Message "Release $tag" -Branch 'main'

Write-Host "Released $tag." -ForegroundColor Green
exit 0
