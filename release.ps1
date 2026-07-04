# release.ps1 — bump the firmware version, build it, commit + tag + push,
# and publish a GitHub Release with the compiled firmware.bin attached.
#
# Usage:
#   .\release.ps1 1.3.0
#
# Requires a GitHub token with "Contents: Read and write" on this repo.
# Get one (fine-grained, scoped to just dzdylanzd/WaitTimeDisplay) at:
#   https://github.com/settings/tokens?type=beta
# Either set it once per session:
#   $env:GITHUB_TOKEN = "github_pat_..."
# or the script will prompt for it if it's not set.

param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$repo       = "dzdylanzd/WaitTimeDisplay"
$configPath = "src/config.h"
$tag        = "v$Version"
$binPath    = ".pio/build/esp32-c6-devkitc-1/firmware.bin"

# ---------------------------------------------------------------------------
# 0. Preflight
# ---------------------------------------------------------------------------
if (-not $env:GITHUB_TOKEN) {
    $secure = Read-Host "GitHub token (Contents: Read/write on $repo)" -AsSecureString
    $env:GITHUB_TOKEN = [Runtime.InteropServices.Marshal]::PtrToStringAuto(
        [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
}

# Only tracked-file changes matter here -- untracked files/dirs (e.g. other
# in-progress work sitting in the repo) aren't this script's concern and
# won't be touched by it.
$status = git status --porcelain --untracked-files=no
if ($status) {
    Write-Error "Working tree has uncommitted changes -- commit or stash first:`n$status"
    exit 1
}

$existingTag = git tag -l $tag
if ($existingTag) {
    Write-Error "Tag $tag already exists locally. Pick a different version or delete it first."
    exit 1
}

# ---------------------------------------------------------------------------
# 1. Bump FIRMWARE_VERSION in config.h
# ---------------------------------------------------------------------------
$content = Get-Content $configPath -Raw
$pattern = 'constexpr const char\* FIRMWARE_VERSION = "[^"]*";'
if ($content -notmatch $pattern) {
    Write-Error "Couldn't find the FIRMWARE_VERSION line in $configPath"
    exit 1
}
$newContent = $content -replace $pattern, "constexpr const char* FIRMWARE_VERSION = `"$Version`";"
Set-Content -Path $configPath -Value $newContent -NoNewline -Encoding utf8
Write-Host "Bumped FIRMWARE_VERSION to $Version" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# 2. Build
# ---------------------------------------------------------------------------
Write-Host "Building firmware..." -ForegroundColor Cyan
pio run
if ($LASTEXITCODE -ne 0) {
    git checkout -- $configPath
    Write-Error "Build failed -- reverted $configPath. Fix the build and try again."
    exit 1
}
if (-not (Test-Path $binPath)) {
    git checkout -- $configPath
    Write-Error "Build succeeded but $binPath wasn't found -- reverted $configPath."
    exit 1
}

# ---------------------------------------------------------------------------
# 3. Commit, tag, push
# ---------------------------------------------------------------------------
git add $configPath
git commit -m "Bump firmware version to $Version"
git tag $tag
git push
git push origin $tag
Write-Host "Pushed commit and tag $tag" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# 4. Create the GitHub release + upload firmware.bin
# ---------------------------------------------------------------------------
$headers = @{ Authorization = "token $env:GITHUB_TOKEN"; Accept = "application/vnd.github+json" }
$releaseBody = @{ tag_name = $tag; name = $tag; draft = $false; prerelease = $false } | ConvertTo-Json

try {
    $release = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$repo/releases" `
        -Headers $headers -Body $releaseBody
} catch {
    Write-Error "Failed to create the GitHub release: $_`nThe commit/tag were already pushed -- fix the token/permissions and create the release manually if needed."
    exit 1
}
Write-Host "Created release $tag (id $($release.id))" -ForegroundColor Cyan

$uploadUri = "https://uploads.github.com/repos/$repo/releases/$($release.id)/assets?name=firmware.bin"
$uploadHeaders = @{ Authorization = "token $env:GITHUB_TOKEN"; "Content-Type" = "application/octet-stream" }
try {
    Invoke-RestMethod -Method Post -Uri $uploadUri -Headers $uploadHeaders -InFile $binPath | Out-Null
} catch {
    Write-Error "Release $tag was created but the firmware.bin upload failed: $_`nUpload it manually at $($release.html_url)"
    exit 1
}

Write-Host "`nDone! $($release.html_url)" -ForegroundColor Green
