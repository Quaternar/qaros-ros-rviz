# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Builds the qar_rviz_streaming RViz2 panel plugin with colcon against the ROS 2 Humble
    Windows binary distribution and installs it into the install prefix.

.DESCRIPTION
    The plugin is the single ament package qar_rviz_streaming/ of this repository; colcon finds
    it with `--paths` (the repository root is not a colcon package, so this script is the only
    supported way to build it).

    The libraries of qar_ogre_adapter and qar_qt_streaming come from their installed CMake
    packages under -Prefix (the plugin is configured with -DQAR_PREFIX=<Prefix>): install them
    there first; the web libraries are built from the tree of this repository. A developer can
    build the two from source checkouts instead by passing the -DQAR_OGRE_ADAPTER_SOURCE_DIR and
    -DQAR_QT_STREAMING_SOURCE_DIR options through -CMakeArgs (see cmake/QarBuildSupport.cmake);
    that mode reads the QarOS SDK folder from the environment variable QAR_SDK_ROOT.

    colcon installs with --merge-install into the prefix, which therefore gets
        <Prefix>\bin\qar_rviz_streaming.dll
        <Prefix>\share\qar_rviz_streaming\plugin_description.xml
        <Prefix>\local_setup.ps1           (the overlay: dot-source it so RViz finds the plugin)
    Use a prefix that was not created with colcon's isolated layout. The prefix is never
    deleted or rewritten as a whole.

    colcon's build tree and logs are kept in <repository>\build\colcon.

    The script is non-interactive. It
      1. checks the location parameters and that the two upstream packages are installed;
      2. imports the Visual Studio 2019 (MSVC v142) x64 build environment (cl.exe; Ninja comes
         from its own install), the toolset the distribution was compiled with;
      3. dot-sources env_setup.ps1 (distribution, Qt, virtual environment);
      4. runs the idempotent distribution repairs that find_package(rviz_common) needs
         (fix_missing_vendor_cmake_configs.ps1, fix_missing_eigen.ps1,
         fix_stale_pixi_paths.ps1);
      5. runs `colcon build` with the Ninja generator and RelWithDebInfo.

.PARAMETER Prefix
    Install prefix. Default: the environment variable QAR_PREFIX. Required.
.PARAMETER Ros2Root
    ROS 2 Humble distribution root. Default: the environment variable ROS2_INSTALL_ROOT.
    Required.
.PARAMETER QtRoot
    Qt 5.15.2 msvc2019_64 folder. Default: the environment variable QAR_QT_ROOT. Required.
.PARAMETER Venv
    Python 3.10 virtual environment (ament's Python helpers need catkin_pkg, lark and empy; the
    system Python lacks them). Default: the environment variable QAR_VENV. Required.
.PARAMETER CMakeArgs
    Extra arguments for the plugin's CMake configure step, for example
    -CMakeArgs '-DQAR_OGRE_ADAPTER_SOURCE_DIR=C:\src\qar_ogre_adapter'.
.PARAMETER Clean
    Delete <repository>\build\colcon (colcon's build tree and logs) before building. Nothing
    else is deleted.

.EXAMPLE
    ./scripts/build_plugin.ps1 -Prefix C:\qar\install
#>
[CmdletBinding()]
param(
    [string]$Prefix = "",
    [Alias('Ros2DistroRoot', 'Ros2InstallRoot')]
    [string]$Ros2Root = "",
    [Alias('QtVersionDir')]
    [string]$QtRoot = "",
    [string]$Venv = "",
    [string[]]$CMakeArgs = @(),
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path

# --- 1. Locations and the upstream packages ------------------------------------
. (Join-Path $PSScriptRoot "qar_locations.ps1")
$Prefix = Resolve-QarLocation -Value $Prefix -Parameter 'Prefix' -EnvVar 'QAR_PREFIX' `
    -What 'Install prefix' -Section 'Directory layout and environment variables' -AllowMissing
$Ros2Root = Resolve-QarLocation -Value $Ros2Root -Parameter 'Ros2Root' -EnvVar 'ROS2_INSTALL_ROOT' `
    -What 'ROS 2 distribution' -Marker 'local_setup.bat' -Section 'ROS 2 Humble for Windows'
$QtRoot = Resolve-QarLocation -Value $QtRoot -Parameter 'QtRoot' -EnvVar 'QAR_QT_ROOT' `
    -What 'Qt 5.15.2' -Marker 'bin\Qt5Core.dll' -Section 'Qt 5.15.2'
$Venv = Resolve-QarLocation -Value $Venv -Parameter 'Venv' -EnvVar 'QAR_VENV' `
    -What 'Python virtual environment' -Marker 'Scripts\python.exe' -Section 'Python environment'

$sourceMode = @($CMakeArgs | Where-Object { $_ -match '^-DQAR_\w+_SOURCE_DIR=' }).Count -gt 0
if (-not $sourceMode) {
    $missing = @('qar_ogre_adapter', 'qar_qt_streaming') | Where-Object {
        -not (Test-Path (Join-Path $Prefix "share\$_\cmake\$($_)Config.cmake"))
    }
    if ($missing) {
        throw ("Cannot build the plugin: install qar_ogre_adapter and qar_qt_streaming into " +
            "$Prefix first (no CMake package found for: $($missing -join ', ')). " +
            "See docs/INSTALL.md, section 'Upstream repositories'. To build them from source " +
            "checkouts instead, pass -CMakeArgs -DQAR_<PACKAGE>_SOURCE_DIR=<checkout>.")
    }
}

# --- 2. Visual Studio 2019 (v142) x64 build environment (cl.exe + ninja) -----------
Import-QarVsDevEnvironment

# --- 3. ROS 2 distribution + Qt + venv environment ---------------------------------
Write-Host "=== Dot-sourcing env_setup.ps1 ===" -ForegroundColor Cyan
. "$PSScriptRoot\env_setup.ps1" -Ros2Root $Ros2Root -QtRoot $QtRoot -Venv $Venv

# --- 4. Fill the distribution's missing CMake configurations -----------------------
# The official Windows archive ships several vendored third-party runtime DLLs without
# their CMake CONFIG files, which breaks find_package(rviz_common) for any downstream
# ament plugin. These repair scripts are idempotent (no-op if present).
Write-Host "=== Ensuring downstream ament CMake config gaps are filled ===" -ForegroundColor Cyan
& "$PSScriptRoot\fix_missing_vendor_cmake_configs.ps1" -Ros2Root $Ros2Root
& "$PSScriptRoot\fix_missing_eigen.ps1" -Ros2Root $Ros2Root
& "$PSScriptRoot\fix_stale_pixi_paths.ps1" -Ros2Root $Ros2Root

# --- 5. colcon build ---------------------------------------------------------------
$colconRoot = Join-Path $RepoRoot "build\colcon"
if ($Clean) {
    Write-Host "=== Clean: removing $colconRoot ===" -ForegroundColor Yellow
    Remove-Item -Recurse -Force $colconRoot -ErrorAction SilentlyContinue
}

$qtCmakeDir = Join-Path $QtRoot "lib\cmake\Qt5"
if (-not (Test-Path (Join-Path $qtCmakeDir "Qt5Config.cmake"))) {
    throw "Qt5 CMake config not found at $qtCmakeDir (install Qt 5.15.2 msvc2019_64)."
}

$ninja = (Get-Command ninja -ErrorAction SilentlyContinue)
if (-not $ninja) { throw "ninja not found on PATH after the Visual Studio environment import." }

# ament_cmake runs Python helper scripts (package_xml_2_cmake.py) that need
# catkin_pkg/lark/empy. CMake's find_package(Python3) otherwise picks the system
# Python (which lacks them); force the venv Python (3.10, has them).
$venvPython = Join-Path $Venv "Scripts\python.exe"

Write-Host "=== colcon build (Ninja, RelWithDebInfo) ===" -ForegroundColor Cyan
Write-Host "  cl.exe : $((Get-Command cl -ErrorAction SilentlyContinue).Source)"
Write-Host "  ninja  : $($ninja.Source)"
Write-Host "  Qt5_DIR: $qtCmakeDir"
Write-Host "  Python3: $venvPython"
Write-Host "  prefix : $Prefix"
Write-Host "  colcon : $colconRoot"

colcon --log-base (Join-Path $colconRoot "log") build `
    --paths (Join-Path $RepoRoot "qar_rviz_streaming") `
    --build-base (Join-Path $colconRoot "build") `
    --install-base $Prefix `
    --merge-install `
    --event-handlers console_direct+ `
    --cmake-args `
        -G Ninja `
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo" `
        "-DQt5_DIR=$qtCmakeDir" `
        "-DQAR_PREFIX=$Prefix" `
        "-DPython3_EXECUTABLE=$venvPython" `
        "-DPYTHON_EXECUTABLE=$venvPython" `
        @CMakeArgs `
        --no-warn-unused-cli

$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host "colcon build FAILED (exit $code)." -ForegroundColor Red
    exit $code
}

Write-Host ""
Write-Host "=== BUILD OK ===" -ForegroundColor Green
$dll = Join-Path $Prefix "bin\qar_rviz_streaming.dll"
if (Test-Path $dll) {
    Write-Host "Plugin DLL: $dll" -ForegroundColor Green
} else {
    Write-Host "WARNING: $dll not found." -ForegroundColor Yellow
}
$desc = Join-Path $Prefix "share\qar_rviz_streaming\plugin_description.xml"
if (Test-Path $desc) { Write-Host "plugin_description.xml installed: $desc" -ForegroundColor Green }
$overlay = Join-Path $Prefix "local_setup.ps1"
if (Test-Path $overlay) { Write-Host "Overlay (dot-source it): $overlay" -ForegroundColor Green }
