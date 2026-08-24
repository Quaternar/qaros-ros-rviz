<#
.SYNOPSIS
    Idempotently downloads and extracts the ROS 2 Humble Windows binary distro and its
    reference source archive, verifying SHA256 on each run.

.DESCRIPTION
    Downloads (to $DestinationRoot\downloads\):
      - ros2-humble-20260220-windows-release-amd64.zip  (the buildable binary distro)
      - ros2-release-humble-20260220-src.zip             (reference-only manifest repo)
    then extracts them to $DestinationRoot\ros2_humble\ and
    $DestinationRoot\ros2_src_release-humble-20260220\ respectively.

    Safe to re-run: if a target zip already exists AND its SHA256 matches the recorded value
    below, the download is skipped. Extraction is skipped if the destination already looks
    populated (pass -Force to re-extract).

    Note: on this class of machine, curl (Git Bash) fails downloading these URLs with an SSL
    error - this script uses PowerShell's Invoke-WebRequest (-UseBasicParsing), which works.

.PARAMETER DestinationRoot
    Root directory for downloads/extraction. Default: D:\development

.PARAMETER Force
    Re-extract even if the destination directory already exists and looks populated.

.EXAMPLE
    ./scripts/fetch_dependencies.ps1
    ./scripts/fetch_dependencies.ps1 -DestinationRoot "D:\development" -Force
#>
param(
    [string]$DestinationRoot = "D:\development",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$downloads = Join-Path $DestinationRoot "downloads"
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

$items = @(
    @{
        Name       = "ROS 2 Humble Windows binary distro"
        Url        = "https://github.com/ros2/ros2/releases/download/release-humble-20260220/ros2-humble-20260220-windows-release-amd64.zip"
        ZipName    = "ros2-humble-20260220-windows-release-amd64.zip"
        Sha256     = "256ABCFA37F8D97D2CB3FF2A85F53465B016A25632399C99F67DC2BEE18E89C3"
        ExtractTo  = Join-Path $DestinationRoot "ros2_humble"
        # actual layout after extraction: ros2_humble\ros2-windows\{bin,Lib,include,share,opt,...}
        SentinelPath = Join-Path $DestinationRoot "ros2_humble\ros2-windows\local_setup.bat"
    },
    @{
        Name       = "ROS 2 source manifest repo (reference only)"
        Url        = "https://github.com/ros2/ros2/archive/refs/tags/release-humble-20260220.zip"
        ZipName    = "ros2-release-humble-20260220-src.zip"
        Sha256     = "EA72D8921072A824D65E592F01E53A192766CD9AC50CA84D5EAB8F475474A28F"
        ExtractTo  = Join-Path $DestinationRoot "ros2_src_release-humble-20260220"
        SentinelPath = Join-Path $DestinationRoot "ros2_src_release-humble-20260220\ros2-release-humble-20260220\ros2.repos"
    }
)

function Test-Sha256Matches($path, $expected) {
    if (-not (Test-Path $path)) { return $false }
    $actual = (Get-FileHash $path -Algorithm SHA256).Hash
    return ($actual -eq $expected)
}

foreach ($item in $items) {
    Write-Host "=== $($item.Name) ==="
    $zipPath = Join-Path $downloads $item.ZipName

    if (Test-Sha256Matches $zipPath $item.Sha256) {
        Write-Host "Already downloaded and SHA256 verified: $zipPath"
    } else {
        Write-Host "Downloading $($item.Url)"
        Invoke-WebRequest -Uri $item.Url -OutFile $zipPath -UseBasicParsing
        if (-not (Test-Sha256Matches $zipPath $item.Sha256)) {
            $actual = (Get-FileHash $zipPath -Algorithm SHA256).Hash
            throw "SHA256 mismatch for $($item.ZipName): expected $($item.Sha256), got $actual"
        }
        Write-Host "Downloaded and verified: $zipPath"
    }

    if ((Test-Path $item.SentinelPath) -and -not $Force) {
        Write-Host "Already extracted (sentinel found: $($item.SentinelPath)). Use -Force to re-extract."
    } else {
        Write-Host "Extracting to $($item.ExtractTo)"
        New-Item -ItemType Directory -Force -Path $item.ExtractTo | Out-Null
        Expand-Archive -Path $zipPath -DestinationPath $item.ExtractTo -Force
    }
    Write-Host ""
}

Write-Host "Done. Binary distro: $($items[0].ExtractTo)\ros2-windows"
Write-Host "Reminder: the binary distro has a known packaging gap (missing spdlog.dll) - run"
Write-Host "scripts\fix_missing_spdlog_dll.ps1 after fetching, before using ros2/rviz2."
