# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Registers a hand-built qar_rviz_streaming install prefix for QAROS, so that the Visualizer
    offers "ROS 2 Rviz" for that ROS 2 distribution and starts rviz2 with this plugin.

.DESCRIPTION
    Writes or replaces one record in the per-user file
        %APPDATA%\Quaternar\ros-plugins.json
    The record is keyed by (distro, platform, source); this script always writes source "local",
    so running it again for the same distribution replaces the earlier record. Records of other
    distributions, other platforms and prebuilt records that QAROS wrote are kept as they are.
    The file format is documented in docs/CONFIGURATION.md, section "QAROS plugin registration",
    and in docs/ros-plugins.schema.json.

    Before it writes, the script checks that
      - the prefix holds the plugin: the ament index resource
        share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming,
        bin\qar_rviz_streaming.dll, share\qar_rviz_streaming\plugin_description.xml and
        share\qar_rviz_streaming\config\qar_streaming_dev.rviz;
      - the ROS 2 root holds a setup script (local_setup.bat or setup.bat) and rviz2
        (bin\rviz2.exe or lib\rviz2\rviz2.exe).
    It never changes the ROS 2 distribution or the prefix. A distribution that is broken (rviz2
    does not start) is not detected here; docs/TROUBLESHOOTING.md lists the known cases.

    The file is written atomically (a temporary file in the same folder, then a rename), as
    UTF-8 without a byte order mark. No administrator rights are needed.

.PARAMETER Prefix
    The colcon install prefix of the plugin. Default: the environment variable QAR_PREFIX.
.PARAMETER RosRoot
    The ROS 2 distribution root (the folder with local_setup.bat and rviz2). Default: the
    environment variable ROS2_INSTALL_ROOT, else the first entry of AMENT_PREFIX_PATH that is a
    ROS 2 root.
.PARAMETER Distro
    The ROS 2 distribution name (humble, jazzy, ...). Default: read from the ROS 2 root
    (share\ros_environment), else the environment variable ROS_DISTRO.
.PARAMETER CApiHeader
    The QarOS C header qar_streaming.h the plugin was built against. Default: the hash the
    build installed (share\qar_rviz_streaming\qar_streaming_h.sha256), else the header of the
    qaros submodule of this repository (external\qaros\qar-streaming-c\include\qar_streaming.h).
.PARAMETER Unregister
    Remove the local record of this distribution and platform instead of writing one. Only the
    distribution is needed (-Distro, or a ROS 2 root to read it from).

.EXAMPLE
    .\scripts\register-qaros-plugin.ps1 -Prefix C:\qar\install -RosRoot C:\qar\deps\ros2_humble\ros2-windows
.EXAMPLE
    .\scripts\register-qaros-plugin.ps1 -Unregister -Distro humble
#>
[CmdletBinding()]
param(
    [string]$Prefix = "",
    [Alias('Ros2Root')]
    [string]$RosRoot = "",
    [string]$Distro = "",
    [string]$CApiHeader = "",
    [switch]$Unregister
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Package = 'qar_rviz_streaming'
$SchemaVersion = 1
$Platform = 'windows-x64'

function Get-FullPath([string]$Path) {
    $p = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path.Trim().Trim('"'))
    if ($p.Length -gt 3) { $p = $p.TrimEnd('\', '/') }
    return $p
}

function Test-RosRoot([string]$Root) {
    $setup = @('local_setup.bat', 'setup.bat') | Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf }
    $rviz = @('bin\rviz2.exe', 'lib\rviz2\rviz2.exe') | Where-Object { Test-Path -LiteralPath (Join-Path $Root $_) -PathType Leaf }
    return [bool]($setup -and $rviz)
}

# The distribution name that the ROS 2 root records (ros_environment's environment hook).
function Get-RootDistro([string]$Root) {
    $envDir = Join-Path $Root 'share\ros_environment\environment'
    $dsv = Join-Path $envDir '1.ros_distro.dsv'
    if (Test-Path -LiteralPath $dsv) {
        foreach ($line in [IO.File]::ReadAllLines($dsv)) {
            if ($line -match '^set;ROS_DISTRO;(\S+)\s*$') { return $matches[1] }
        }
    }
    $bat = Join-Path $envDir '1.ros_distro.bat'
    if (Test-Path -LiteralPath $bat) {
        foreach ($line in [IO.File]::ReadAllLines($bat)) {
            if ($line -match '^\s*set\s+ROS_DISTRO=(\S+)\s*$') { return $matches[1] }
        }
    }
    return $null
}

function Get-RegistryPath {
    if ([string]::IsNullOrWhiteSpace($env:APPDATA)) {
        throw 'The environment variable APPDATA is not set; cannot locate the per-user QAROS folder.'
    }
    return Join-Path (Join-Path $env:APPDATA 'Quaternar') 'ros-plugins.json'
}

# PowerShell 7 turns ISO 8601 strings into DateTime when it reads JSON; turn them back so other
# records are written as they were read.
function ConvertTo-PlainJsonValue($Value) {
    if ($null -eq $Value) { return $null }
    if ($Value -is [datetime]) { return $Value.ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'") }
    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        $o = [ordered]@{}
        foreach ($p in $Value.PSObject.Properties) { $o[$p.Name] = ConvertTo-PlainJsonValue $p.Value }
        return $o
    }
    if ($Value -is [System.Collections.IList]) {
        return , @($Value | ForEach-Object { ConvertTo-PlainJsonValue $_ })
    }
    return $Value
}

function Read-Registry([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return [ordered]@{ version = $SchemaVersion; plugins = @() }
    }
    $text = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
    if ([string]::IsNullOrWhiteSpace($text)) {
        return [ordered]@{ version = $SchemaVersion; plugins = @() }
    }
    try {
        $convert = Get-Command ConvertFrom-Json
        if ($convert.Parameters.ContainsKey('DateKind')) {
            $parsed = $text | ConvertFrom-Json -DateKind String
        } else {
            $parsed = $text | ConvertFrom-Json
        }
    } catch {
        throw "$Path is not valid JSON; fix or delete it, then run this script again. ($($_.Exception.Message))"
    }
    if ($parsed -isnot [System.Management.Automation.PSCustomObject]) {
        throw "$Path does not hold a JSON object; fix or delete it, then run this script again."
    }
    $doc = ConvertTo-PlainJsonValue $parsed
    if (-not $doc.Contains('version')) {
        throw "$Path has no ""version"" field; fix or delete it, then run this script again."
    }
    if ([int]$doc['version'] -ne $SchemaVersion) {
        throw "$Path has version $($doc['version']); this script writes version $SchemaVersion and leaves the file unchanged. Update qaros-ros-rviz."
    }
    if (-not $doc.Contains('plugins') -or $null -eq $doc['plugins']) { $doc['plugins'] = @() }
    $doc['plugins'] = @($doc['plugins'])
    return $doc
}

function Write-Registry([string]$Path, $Doc) {
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $json = ConvertTo-Json -InputObject $Doc -Depth 20
    $tmp = Join-Path $dir ("ros-plugins.json.{0}.tmp" -f [Guid]::NewGuid().ToString('N'))
    try {
        [IO.File]::WriteAllText($tmp, $json + "`n", (New-Object System.Text.UTF8Encoding($false)))
        if (Test-Path -LiteralPath $Path) {
            [IO.File]::Replace($tmp, $Path, [NullString]::Value)
        } else {
            [IO.File]::Move($tmp, $Path)
        }
    } finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    }
}

function Test-SameKey($Record, [string]$D, [string]$P, [string]$S) {
    return ($Record['distro'] -eq $D -and $Record['platform'] -eq $P -and $Record['source'] -eq $S)
}

# --- ROS 2 root and distribution -----------------------------------------------
$rootFrom = '-RosRoot'
if ([string]::IsNullOrWhiteSpace($RosRoot) -and -not [string]::IsNullOrWhiteSpace($env:ROS2_INSTALL_ROOT)) {
    $RosRoot = $env:ROS2_INSTALL_ROOT; $rootFrom = '$env:ROS2_INSTALL_ROOT'
}
$prefixFull = if (-not [string]::IsNullOrWhiteSpace($Prefix)) { Get-FullPath $Prefix }
              elseif (-not [string]::IsNullOrWhiteSpace($env:QAR_PREFIX)) { Get-FullPath $env:QAR_PREFIX }
              else { $null }
if ([string]::IsNullOrWhiteSpace($RosRoot) -and $env:AMENT_PREFIX_PATH) {
    foreach ($entry in $env:AMENT_PREFIX_PATH -split ';') {
        if ([string]::IsNullOrWhiteSpace($entry)) { continue }
        $full = Get-FullPath $entry
        if ($prefixFull -and $full -eq $prefixFull) { continue }
        if (Test-RosRoot $full) { $RosRoot = $full; $rootFrom = '$env:AMENT_PREFIX_PATH'; break }
    }
}
$rootFull = if (-not [string]::IsNullOrWhiteSpace($RosRoot)) { Get-FullPath $RosRoot } else { $null }

$rootDistro = if ($rootFull -and (Test-Path -LiteralPath $rootFull)) { Get-RootDistro $rootFull } else { $null }
if ([string]::IsNullOrWhiteSpace($Distro)) {
    if ($rootDistro) { $Distro = $rootDistro }
    elseif ($env:ROS_DISTRO) { $Distro = $env:ROS_DISTRO }
} elseif ($rootDistro -and $rootDistro -ne $Distro) {
    throw "-Distro $Distro does not match the ROS 2 root $rootFull, which is $rootDistro."
}
if ([string]::IsNullOrWhiteSpace($Distro)) {
    throw 'Cannot tell the ROS 2 distribution: pass -Distro <name> or -RosRoot <ROS 2 root>, or set ROS_DISTRO.'
}
$Distro = $Distro.Trim().ToLowerInvariant()
if ($Distro -notmatch '^[a-z][a-z0-9_]*$') { throw "Invalid distribution name '$Distro'." }

$registry = Get-RegistryPath

# --- Unregister --------------------------------------------------------------------
if ($Unregister) {
    if (-not (Test-Path -LiteralPath $registry)) {
        Write-Host "Nothing to remove: $registry does not exist."
        exit 0
    }
    $doc = Read-Registry $registry
    $before = @($doc['plugins']).Count
    $doc['plugins'] = @(@($doc['plugins']) | Where-Object { -not (Test-SameKey $_ $Distro $Platform 'local') })
    if (@($doc['plugins']).Count -eq $before) {
        Write-Host "Nothing to remove: no local record for $Distro / $Platform in $registry."
        exit 0
    }
    Write-Registry $registry $doc
    Write-Host "Removed the local record for $Distro / $Platform from $registry." -ForegroundColor Green
    exit 0
}

# --- Validate the prefix and the ROS 2 root -------------------------------------
if (-not $prefixFull) {
    throw 'Install prefix not found: pass -Prefix <folder> or set the environment variable QAR_PREFIX.'
}
if (-not (Test-Path -LiteralPath $prefixFull -PathType Container)) {
    throw "The install prefix does not exist: $prefixFull."
}
$required = @(
    "share\ament_index\resource_index\rviz_common__pluginlib__plugin\$Package",
    "bin\$Package.dll",
    "share\$Package\plugin_description.xml",
    "share\$Package\config\qar_streaming_dev.rviz"
)
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $prefixFull $_) -PathType Leaf) })
if ($missing) {
    throw ("The prefix $prefixFull does not hold an installed $Package plugin; missing: " +
        ($missing -join ', ') + '. Build and install it first (scripts\build_plugin.ps1).')
}

if (-not $rootFull) {
    throw 'ROS 2 root not found: pass -RosRoot <folder> or set the environment variable ROS2_INSTALL_ROOT.'
}
if (-not (Test-Path -LiteralPath $rootFull -PathType Container)) {
    throw "The ROS 2 root does not exist: $rootFull (from $rootFrom)."
}
if (-not (Test-RosRoot $rootFull)) {
    throw ("$rootFull (from $rootFrom) is not a ROS 2 root with RViz: it needs local_setup.bat or setup.bat, " +
        'and bin\rviz2.exe or lib\rviz2\rviz2.exe.')
}

# --- Plugin version and C API header hash -------------------------------------------
$packageXml = Join-Path $prefixFull "share\$Package\package.xml"
if (-not (Test-Path -LiteralPath $packageXml)) { $packageXml = Join-Path $RepoRoot "$Package\package.xml" }
$pluginVersion = ([xml][IO.File]::ReadAllText($packageXml)).package.version
if ([string]::IsNullOrWhiteSpace($pluginVersion)) { throw "No <version> in $packageXml." }

$hash = $null
$hashFrom = $null
if (-not [string]::IsNullOrWhiteSpace($CApiHeader)) {
    $h = Get-FullPath $CApiHeader
    if (-not (Test-Path -LiteralPath $h -PathType Leaf)) { throw "-CApiHeader: $h does not exist." }
    $hash = (Get-FileHash -LiteralPath $h -Algorithm SHA256).Hash.ToLowerInvariant()
    $hashFrom = $h
} else {
    $stamp = Join-Path $prefixFull "share\$Package\qar_streaming_h.sha256"
    if (Test-Path -LiteralPath $stamp) {
        $hash = ([IO.File]::ReadAllText($stamp)).Trim().ToLowerInvariant()
        $hashFrom = $stamp
    } else {
        $h = Join-Path $RepoRoot 'external\qaros\qar-streaming-c\include\qar_streaming.h'
        if (-not (Test-Path -LiteralPath $h)) {
            throw "Cannot find the QarOS C header: $h is missing (run 'git submodule update --init external/qaros') and the prefix has no qar_streaming_h.sha256; or pass -CApiHeader."
        }
        $hash = (Get-FileHash -LiteralPath $h -Algorithm SHA256).Hash.ToLowerInvariant()
        $hashFrom = "$h (the prefix has no qar_streaming_h.sha256; rebuild to record the header of the build)"
    }
}
if ($hash -notmatch '^[0-9a-f]{64}$') { throw "Invalid SHA-256 '$hash' from $hashFrom." }

# --- Write the record -----------------------------------------------------------------
$record = [ordered]@{
    distro              = $Distro
    platform            = $Platform
    source              = 'local'
    ros_root            = $rootFull
    prefix              = $prefixFull
    plugin_version      = $pluginVersion
    c_api_header_sha256 = $hash
    registered_at       = [DateTime]::UtcNow.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'")
}

$doc = Read-Registry $registry
$others = @(@($doc['plugins']) | Where-Object { -not (Test-SameKey $_ $Distro $Platform 'local') })
$replaced = @($doc['plugins']).Count -ne $others.Count
$doc['plugins'] = @($others) + , $record
Write-Registry $registry $doc

$verb = if ($replaced) { 'Replaced' } else { 'Added' }
Write-Host "$verb the local record for $Distro / $Platform in $registry" -ForegroundColor Green
Write-Host "  ROS 2 root     : $rootFull"
Write-Host "  prefix         : $prefixFull"
Write-Host "  plugin version : $pluginVersion"
Write-Host "  C API SHA-256  : $hash"
Write-Host "                   (from $hashFrom)"
