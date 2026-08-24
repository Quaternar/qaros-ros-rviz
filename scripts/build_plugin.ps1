<#
.SYNOPSIS
    Build the qar_rviz_streaming RViz2 panel plugin as a colcon overlay on the
    ROS 2 Humble Windows binary distro.

.DESCRIPTION
    This repository is used as its own colcon workspace: the single ament package
    `qar_rviz_streaming/` is discovered with `colcon build --paths qar_rviz_streaming`
    (no src/ junction needed), and colcon writes build/ install/ log/ into the repo
    root (all gitignored).

    The plugin depends on TWO packages from the separate qar_ogre_adapter
    repository — qar_ogre_streaming and qar_qar_adapter — resolved with
    find_package(). Build that repository first and point this script at its
    install prefix with -AdapterInstall, or build both repositories together in one
    workspace (see README.md, which describes both layouts).

    This script is NON-INTERACTIVE. It:
      1. Imports the VS2019 (MSVC v142) x64 dev environment (cl.exe + ninja),
         matching the distro's compiled toolset, via vswhere + vcvars64.bat.
      2. Dot-sources scripts/env_setup.ps1 (ROS 2 distro env, Qt, venv/colcon).
      3. Applies the idempotent distro CMake-config gap fixes.
      4. Runs `colcon build` with the Ninja generator, RelWithDebInfo.

    The QarOS SDK is NOT needed to build. The adapter loads the QarOS runtime
    dynamically at run time, so only the vendored header (which ships inside
    qar_ogre_streaming) is required here. The SDK binaries are needed to RUN —
    see scripts/run_live_rviz_stream_real.ps1.

.PARAMETER AdapterInstall
    Install prefix of the qar_ogre_adapter repository. Accepts either a single
    prefix (an umbrella `cmake --install` tree) or a colcon install root that
    contains one prefix per package. Default: ..\qar_ogre_adapter\install
    (the side-by-side clone layout).
.PARAMETER QtVersionDir
    Root of the aqt Qt install. Default: D:\development\Qt\5.15.2\msvc2019_64
.PARAMETER Clean
    Remove build/ install/ log/ before building.
#>
param(
    [string]$AdapterInstall = "",
    [string]$QtVersionDir = "D:\development\Qt\5.15.2\msvc2019_64",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path

if ([string]::IsNullOrEmpty($AdapterInstall)) {
    $AdapterInstall = Join-Path (Split-Path -Parent $RepoRoot) "qar_ogre_adapter\install"
}

# ---------------------------------------------------------------------------
# Resolve the adapter repository's prefixes. Both layouts are accepted:
#   * one umbrella prefix           -> <prefix>/share/qar_ogre_streaming/cmake
#   * a colcon install root         -> <root>/qar_ogre_streaming/share/...
# ---------------------------------------------------------------------------
function Test-QarPrefix { param([string]$p)
    return (Test-Path (Join-Path $p "share\qar_ogre_streaming\cmake\qar_ogre_streamingConfig.cmake")) -or
           (Test-Path (Join-Path $p "share\qar_qar_adapter\cmake\qar_qar_adapterConfig.cmake"))
}

$prefixes = @()
if (Test-Path $AdapterInstall) {
    if (Test-QarPrefix $AdapterInstall) { $prefixes += (Resolve-Path $AdapterInstall).Path }
    foreach ($d in Get-ChildItem -Path $AdapterInstall -Directory -ErrorAction SilentlyContinue) {
        if (Test-QarPrefix $d.FullName) { $prefixes += $d.FullName }
    }
}
if ($prefixes.Count -eq 0) {
    throw @"
The qar_ogre_adapter packages were not found under: $AdapterInstall

Build that repository first, for example:

    cd ..\qar_ogre_adapter
    colcon build --cmake-args -DQAR_ROS2_INSTALL_ROOT=<distro>\ros2-windows

then re-run this script (or pass -AdapterInstall <prefix>). See README.md.
"@
}
Write-Host "=== qar_ogre_adapter prefixes ===" -ForegroundColor Cyan
$prefixes | ForEach-Object { Write-Host "  $_" }

function Import-BatchEnvironment {
    param([string]$BatchFile, [string]$BatchArgs = "")
    if (-not (Test-Path $BatchFile)) { throw "Batch file not found: $BatchFile" }

    # vcvars64.bat (and similar toolchain batch files) fail with "The input line is
    # too long" / "The syntax of the command is incorrect" when the CURRENT PATH is
    # already large: their own internal `set PATH=<additions>;%PATH%` line exceeds
    # cmd.exe's ~8191-character line-length limit. That is a function of how much
    # the calling shell/profile has already accumulated on PATH, not of anything
    # this script does. Run the batch file against a short, machine-baseline PATH,
    # then merge the caller's original PATH back in afterward (after what the batch
    # file just set, so its own additions -- e.g. cl.exe -- still take precedence),
    # so nothing already on it is permanently lost.
    $savedPath = $env:PATH
    $env:PATH = [Environment]::GetEnvironmentVariable('PATH', 'Machine')

    $dump = Join-Path $env:TEMP ("qar_env_dump_{0}.txt" -f ([System.Guid]::NewGuid().ToString('N')))
    cmd /c "call `"$BatchFile`" $BatchArgs && set > `"$dump`"" | Out-Null
    if (-not (Test-Path $dump)) {
        $env:PATH = $savedPath
        throw "Failed to capture environment from $BatchFile"
    }
    Get-Content $dump -Encoding UTF8 | ForEach-Object {
        if ($_ -match '^([A-Za-z_][A-Za-z0-9_\(\)]*)=(.*)$') {
            try { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue } catch {}
        }
    }
    Remove-Item $dump -ErrorAction SilentlyContinue
    $env:PATH = "$env:PATH;$savedPath"
}

# --- 1. VS2019 (v142) x64 dev environment (cl.exe + ninja) ------------------
$vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere not found at $vswhere" }
$vsPath = & $vswhere -version "[16.0,17.0)" -property installationPath -latest | Select-Object -First 1
if (-not $vsPath) { throw "Visual Studio 2019 (v142) not found via vswhere." }
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
Write-Host "=== Importing VS2019 x64 dev environment ===" -ForegroundColor Cyan
Write-Host "  vcvars64: $vcvars"
Import-BatchEnvironment -BatchFile $vcvars
# Some ROS/ament CMake modules invoke `vswhere` by bare name; make sure the VS
# Installer directory (where vswhere.exe lives) is on PATH.
$vswhereDir = Split-Path -Parent $vswhere
if ($env:PATH -notlike "*$vswhereDir*") { $env:PATH = "$vswhereDir;$env:PATH" }

# --- 2. ROS 2 distro + Qt + venv/colcon environment ------------------------
Write-Host "=== Dot-sourcing env_setup.ps1 ===" -ForegroundColor Cyan
. "$PSScriptRoot\env_setup.ps1" -QtVersionDir $QtVersionDir

# --- 2b. Self-heal the distro's missing vendor CMake configs ----------------
# The official Windows archive ships several vendored third-party runtime DLLs
# without their CMake CONFIG files, which breaks find_package(rviz_common) for any
# downstream ament plugin. These fix scripts are idempotent (no-op if present).
Write-Host "=== Ensuring downstream ament CMake config gaps are filled ===" -ForegroundColor Cyan
& "$PSScriptRoot\fix_missing_vendor_cmake_configs.ps1"
& "$PSScriptRoot\fix_missing_eigen.ps1"
& "$PSScriptRoot\fix_stale_pixi_paths.ps1"

# --- 3. colcon build -------------------------------------------------------
Set-Location $RepoRoot

if ($Clean) {
    Write-Host "=== Clean: removing build/ install/ log/ ===" -ForegroundColor Yellow
    Remove-Item -Recurse -Force "build", "install", "log" -ErrorAction SilentlyContinue
}

$qtCmakeDir = Join-Path $QtVersionDir "lib\cmake\Qt5"
if (-not (Test-Path (Join-Path $qtCmakeDir "Qt5Config.cmake"))) {
    throw "Qt5 CMake config not found at $qtCmakeDir (install aqt Qt 5.15.2 msvc2019_64)."
}

$ninja = (Get-Command ninja -ErrorAction SilentlyContinue)
if (-not $ninja) { throw "ninja not found on PATH after vcvars import." }

# ament_cmake runs Python helper scripts (package_xml_2_cmake.py) that need
# catkin_pkg/lark/empy. CMake's find_package(Python3) otherwise picks the system
# Python (which lacks them); force the repo venv Python (3.10, has them).
$venvPython = Join-Path $RepoRoot ".venv\Scripts\python.exe"
if (-not (Test-Path $venvPython)) {
    throw "venv Python not found at $venvPython. Create it: `"C:\Program Files\Python310\python.exe`" -m venv .venv; .venv\Scripts\pip install -r requirements.txt"
}

# Hand the adapter prefixes to CMake. Semicolon-separated, forward slashes so the
# list is not mangled by CMake's escaping rules.
$prefixArg = ($prefixes | ForEach-Object { $_.Replace('\','/') }) -join ';'

Write-Host "=== colcon build (Ninja, RelWithDebInfo) ===" -ForegroundColor Cyan
Write-Host "  cl.exe : $((Get-Command cl -ErrorAction SilentlyContinue).Source)"
Write-Host "  ninja  : $($ninja.Source)"
Write-Host "  Qt5_DIR: $qtCmakeDir"
Write-Host "  Python3: $venvPython"

colcon build `
    --paths qar_rviz_streaming `
    --event-handlers console_direct+ `
    --cmake-args `
        -G Ninja `
        "-DCMAKE_BUILD_TYPE=RelWithDebInfo" `
        "-DCMAKE_PREFIX_PATH=$prefixArg" `
        "-DQt5_DIR=$qtCmakeDir" `
        "-DPython3_EXECUTABLE=$venvPython" `
        "-DPYTHON_EXECUTABLE=$venvPython" `
        --no-warn-unused-cli

$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host "colcon build FAILED (exit $code)." -ForegroundColor Red
    exit $code
}

Write-Host ""
Write-Host "=== BUILD OK ===" -ForegroundColor Green
$dll = Get-ChildItem -Path "$RepoRoot\install\qar_rviz_streaming" -Recurse -Filter "qar_rviz_streaming.dll" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($dll) {
    Write-Host "Plugin DLL: $($dll.FullName)" -ForegroundColor Green
} else {
    Write-Host "WARNING: qar_rviz_streaming.dll not found under install/." -ForegroundColor Yellow
}
$desc = Get-ChildItem -Path "$RepoRoot\install\qar_rviz_streaming" -Recurse -Filter "plugin_description.xml" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($desc) { Write-Host "plugin_description.xml installed: $($desc.FullName)" -ForegroundColor Green }
