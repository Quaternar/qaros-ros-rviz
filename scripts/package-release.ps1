# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Packages an installed qar_rviz_streaming plugin as a release asset: a zip with an overlay
    prefix, and its entry in manifest.json.

.DESCRIPTION
    Takes the plugin's files out of a colcon install prefix (which may also hold other packages,
    such as the installed upstream libraries) and writes
        <OutDir>\qar_rviz_streaming-<Version>-<Distro>-<Platform>.zip
        <OutDir>\qar_rviz_streaming-<Version>-<Distro>-<Platform>.json   (the manifest entry)
        <OutDir>\manifest.json                                            (all entries in OutDir)
    The zip holds the prefix layout at its root, so it unpacks into a folder that is itself an
    overlay prefix:
        bin\qar_rviz_streaming.dll                       (Windows)
        lib\libqar_rviz_streaming.so                     (Linux)
        share\ament_index\resource_index\<type>\qar_rviz_streaming   (not parent_prefix_path,
                                                         which names folders of the build machine)
        share\colcon-core\packages\qar_rviz_streaming
        share\qar_rviz_streaming\...                     (plugin_description.xml, config,
                                                         package.xml, environment hooks,
                                                         qar_streaming_h.sha256, plugin_version,
                                                         qaros_version, LICENSE)
        local_setup.* and _local_setup_util_*.py         (colcon's prefix scripts)
    setup.* of the prefix are left out: they chain to the underlays of the build machine.

    The format of manifest.json is described in docs/CONFIGURATION.md, section
    "Release assets and manifest.json". The script only reads the prefix.

.PARAMETER Prefix
    The colcon install prefix that holds the plugin. Default: the environment variable QAR_PREFIX.
.PARAMETER Version
    The release version, the plugin's own version without a leading v (a leading v is removed):
    X.Y.Z for a release, X.Y.Z-rc.N for a candidate, X.Y.Z-pr.N for a pull request build. Its
    X.Y.Z should be the version of the build (VERSION, package.xml); the script warns otherwise.
    Default: the file VERSION of the repository.
.PARAMETER Distro
    The ROS 2 distribution the plugin was built for. Default: from -RosRoot, else ROS_DISTRO.
.PARAMETER RosRoot
    The ROS 2 root the plugin was built against, only to read the distribution name. Default:
    the environment variable ROS2_INSTALL_ROOT.
.PARAMETER Platform
    windows-x64 or linux-x64. Default: windows-x64.
.PARAMETER QtMajor
    The Qt major version the plugin was built with. Default: 5.
.PARAMETER CApiHeader
    The QarOS C header the plugin was built against. Default: the prefix's
    share\qar_rviz_streaming\qar_streaming_h.sha256, else the header of the submodule
    external\qaros.
.PARAMETER MinQarosVersion
    The QAROS version whose C API header the plugin was built against, written to the manifest as
    min_qaros_version. Default: the prefix's share\qar_rviz_streaming\qaros_version, else the
    file VERSION of the submodule external\qaros.
.PARAMETER OutDir
    The output folder; created when missing. An existing zip of the same name is replaced.

.EXAMPLE
    .\scripts\package-release.ps1 -Prefix C:\qar\install -Distro humble -OutDir dist
#>
[CmdletBinding()]
param(
    [string]$Prefix = "",
    [string]$Version = "",
    [string]$Distro = "",
    [Alias('Ros2Root')][string]$RosRoot = "",
    [ValidateSet('windows-x64', 'linux-x64', 'linux-arm64')][string]$Platform = 'windows-x64',
    [ValidateSet(5, 6)][int]$QtMajor = 5,
    [string]$CApiHeader = "",
    [string]$MinQarosVersion = "",
    [Parameter(Mandatory = $true)][string]$OutDir
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = 'qar_rviz_streaming'

function Get-FullPath([string]$Path) {
    $p = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path.Trim().Trim('"'))
    if ($p.Length -gt 3) { $p = $p.TrimEnd('\', '/') }
    return $p
}
function Get-Sha256([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

# --- Inputs ----------------------------------------------------------------------------------
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = ([IO.File]::ReadAllLines((Join-Path $RepoRoot 'VERSION')) | Select-Object -First 1)
}
$Version = $Version.Trim()
if ($Version.StartsWith('v')) { $Version = $Version.Substring(1) }
if ($Version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.+-]+)?$') {
    throw "-Version '$Version' is not a version like 0.9.0 or 0.9.0-rc.1."
}
if ([string]::IsNullOrWhiteSpace($Prefix)) { $Prefix = $env:QAR_PREFIX }
if ([string]::IsNullOrWhiteSpace($Prefix)) { throw 'Pass -Prefix <folder> or set QAR_PREFIX.' }
$Prefix = Get-FullPath $Prefix
if (-not (Test-Path -LiteralPath $Prefix -PathType Container)) { throw "The prefix does not exist: $Prefix." }

if ([string]::IsNullOrWhiteSpace($Distro)) {
    if ([string]::IsNullOrWhiteSpace($RosRoot)) { $RosRoot = $env:ROS2_INSTALL_ROOT }
    if (-not [string]::IsNullOrWhiteSpace($RosRoot)) {
        $dsv = Join-Path (Get-FullPath $RosRoot) 'share\ros_environment\environment\1.ros_distro.dsv'
        if (Test-Path -LiteralPath $dsv) {
            foreach ($line in [IO.File]::ReadAllLines($dsv)) {
                if ($line -match '^set;ROS_DISTRO;(\S+)\s*$') { $Distro = $matches[1] }
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace($Distro)) { $Distro = $env:ROS_DISTRO }
}
if ([string]::IsNullOrWhiteSpace($Distro)) { throw 'Cannot tell the ROS 2 distribution: pass -Distro or -RosRoot.' }
$Distro = $Distro.Trim().ToLowerInvariant()

$isWindowsTarget = $Platform -like 'windows-*'
$library = if ($isWindowsTarget) { "bin/$Package.dll" } else { "lib/lib$Package.so" }

# --- Select the files ----------------------------------------------------------------------------
$required = @(
    $library,
    "share/ament_index/resource_index/packages/$Package",
    "share/ament_index/resource_index/rviz_common__pluginlib__plugin/$Package",
    "share/$Package/plugin_description.xml",
    "share/$Package/package.xml",
    "share/$Package/config/qar_streaming_dev.rviz"
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Prefix $_) -PathType Leaf) })
if ($missing) { throw "The prefix $Prefix does not hold an installed $Package for $Platform; missing: $($missing -join ', ')." }

# Map of the zip entry name (forward slashes) to the source file.
$entries = [ordered]@{}
function Add-Entry([string]$Relative) {
    $src = Join-Path $Prefix ($Relative -replace '/', '\')
    if (Test-Path -LiteralPath $src -PathType Leaf) { $script:entries[$Relative] = $src }
}
Add-Entry $library
foreach ($typeDir in Get-ChildItem -LiteralPath (Join-Path $Prefix 'share\ament_index\resource_index') -Directory) {
    if ($typeDir.Name -eq 'parent_prefix_path') { continue }
    Add-Entry "share/ament_index/resource_index/$($typeDir.Name)/$Package"
}
Add-Entry "share/colcon-core/packages/$Package"
$shareRoot = Join-Path $Prefix "share\$Package"
# -Name gives paths relative to the folder (FullName may spell a short 8.3 folder name long).
foreach ($rel in Get-ChildItem -LiteralPath $shareRoot -Recurse -File -Name) {
    $entries["share/$Package/$($rel -replace '\\', '/')"] = Join-Path $shareRoot $rel
}
foreach ($f in Get-ChildItem -LiteralPath $Prefix -File) {
    if ($f.Name -like 'local_setup.*' -or $f.Name -like '_local_setup_util_*.py') { Add-Entry $f.Name }
}

# The header hash: given, from the build's stamp, or from the submodule.
$stampRel = "share/$Package/qar_streaming_h.sha256"
$generated = @{}
if (-not [string]::IsNullOrWhiteSpace($CApiHeader)) {
    $hash = Get-Sha256 (Get-FullPath $CApiHeader)
    $hashFrom = $CApiHeader
} elseif ($entries.Contains($stampRel)) {
    $hash = ([IO.File]::ReadAllText($entries[$stampRel])).Trim().ToLowerInvariant()
    $hashFrom = $entries[$stampRel]
} else {
    $h = Join-Path $RepoRoot 'external\qaros\qar-streaming-c\include\qar_streaming.h'
    if (-not (Test-Path -LiteralPath $h)) { throw "No qar_streaming_h.sha256 in the prefix and no $h; pass -CApiHeader." }
    $hash = Get-Sha256 $h
    $hashFrom = "$h (the prefix has no qar_streaming_h.sha256)"
}
if ($hash -notmatch '^[0-9a-f]{64}$') { throw "Invalid SHA-256 '$hash' from $hashFrom." }
if ($entries.Contains($stampRel)) { $entries.Remove($stampRel) }
$generated[$stampRel] = "$hash`n"
if (-not $entries.Contains("share/$Package/LICENSE")) { $entries["share/$Package/LICENSE"] = Join-Path $RepoRoot 'LICENSE' }

# The minimum QAROS version: given, from the build's stamp, or from the submodule.
$qarosStampRel = "share/$Package/qaros_version"
if (-not [string]::IsNullOrWhiteSpace($MinQarosVersion)) {
    $minQaros = $MinQarosVersion.Trim()
    $minQarosFrom = '-MinQarosVersion'
} elseif ($entries.Contains($qarosStampRel)) {
    $minQaros = ([IO.File]::ReadAllText($entries[$qarosStampRel])).Trim()
    $minQarosFrom = $entries[$qarosStampRel]
} else {
    $v = Join-Path $RepoRoot 'external\qaros\VERSION'
    if (-not (Test-Path -LiteralPath $v)) { throw "No qaros_version in the prefix and no $v; pass -MinQarosVersion." }
    $minQaros = ([IO.File]::ReadAllLines($v) | Select-Object -First 1).Trim()
    $minQarosFrom = "$v (the prefix has no qaros_version)"
}
if ($minQaros -notmatch '^[0-9]+\.[0-9]+\.[0-9]+$') { throw "Invalid QAROS version '$minQaros' from $minQarosFrom." }
if ($entries.Contains($qarosStampRel)) { $entries.Remove($qarosStampRel) }
$generated[$qarosStampRel] = "$minQaros`n"

# The plugin version of the release is the release version; the build's package.xml should agree.
$pluginVersion = $Version
$packageVersion = ([xml][IO.File]::ReadAllText((Join-Path $Prefix "share\$Package\package.xml"))).package.version
$releaseCore = ($Version -split '[-+]')[0]
if ($packageVersion -ne $releaseCore) {
    Write-Warning "The prefix holds $Package $packageVersion (package.xml), the release version is $Version."
}
$versionStampRel = "share/$Package/plugin_version"
if ($entries.Contains($versionStampRel)) { $entries.Remove($versionStampRel) }
$generated[$versionStampRel] = "$pluginVersion`n"

# --- Write the zip ------------------------------------------------------------------------------
$OutDir = Get-FullPath $OutDir
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$baseName = "$Package-$Version-$Distro-$Platform"
$zipPath = Join-Path $OutDir "$baseName.zip"
$tmpZip = "$zipPath.tmp"
if (Test-Path -LiteralPath $tmpZip) { Remove-Item -LiteralPath $tmpZip -Force }

$utf8 = New-Object System.Text.UTF8Encoding($false)
$stream = [IO.File]::Open($tmpZip, 'CreateNew')
try {
    $zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        $names = @($entries.Keys) + @($generated.Keys) | Sort-Object -Unique
        foreach ($name in $names) {
            $entry = $zip.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
            $w = $entry.Open()
            try {
                if ($generated.ContainsKey($name)) {
                    $bytes = $utf8.GetBytes($generated[$name])
                    $w.Write($bytes, 0, $bytes.Length)
                } else {
                    $r = [IO.File]::OpenRead($entries[$name])
                    try { $r.CopyTo($w) } finally { $r.Dispose() }
                }
            } finally { $w.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $stream.Dispose() }
Move-Item -LiteralPath $tmpZip -Destination $zipPath -Force

# --- Manifest entry and manifest.json --------------------------------------------------------------
$asset = [ordered]@{
    name                = "$baseName.zip"
    sha256              = Get-Sha256 $zipPath
    size                = (Get-Item -LiteralPath $zipPath).Length
    distro              = $Distro
    platform            = $Platform
    qt_major            = $QtMajor
    plugin_version      = $pluginVersion
    c_api_header_sha256 = $hash
    min_qaros_version   = $minQaros
}
$entryPath = Join-Path $OutDir "$baseName.json"
[IO.File]::WriteAllText($entryPath, (ConvertTo-Json -InputObject $asset -Depth 5) + "`n", $utf8)

# manifest.json lists every entry file of this release version in OutDir.
$assets = @()
foreach ($f in Get-ChildItem -LiteralPath $OutDir -Filter "$Package-$Version-*.json" | Sort-Object Name) {
    $assets += , ([IO.File]::ReadAllText($f.FullName) | ConvertFrom-Json)
}
$manifest = [ordered]@{ version = 1; release = $Version; plugin_version = $Version; package = $Package; assets = $assets }
[IO.File]::WriteAllText((Join-Path $OutDir 'manifest.json'), (ConvertTo-Json -InputObject $manifest -Depth 6) + "`n", $utf8)

Write-Host "Packaged $($names.Count) files from $Prefix" -ForegroundColor Green
Write-Host "  zip      : $zipPath"
Write-Host "  sha256   : $($asset.sha256)"
Write-Host "  version  : $pluginVersion"
Write-Host "  C API    : $hash (from $hashFrom)"
Write-Host "  QAROS    : $minQaros or newer (from $minQarosFrom)"
Write-Host "  manifest : $(Join-Path $OutDir 'manifest.json')"
