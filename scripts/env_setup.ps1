<#
.SYNOPSIS
    Dot-source this script to set up a PowerShell session for building/running against the
    ROS 2 Humble Windows binary distro, Qt, and this repo's Python venv.

.DESCRIPTION
    Usage (from the repo root, or anywhere - paths below are absolute):

        . .\scripts\env_setup.ps1

    You MUST dot-source it (leading ". "), not just run it - PowerShell tool/script
    invocations do not share environment state with their caller unless dot-sourced (or unless
    you are literally pasting these commands into your own interactive shell, which behaves the
    same way as dot-sourcing since there's no separate child process involved).

    What this does:
      1. Runs the ROS 2 distro's local_setup.bat (via cmd.exe, since it's a .bat) and imports
         the resulting environment (PATH, AMENT_PREFIX_PATH, CMAKE_PREFIX_PATH, PYTHONPATH, etc.)
         into the CURRENT PowerShell session.
      2. Prepends this repo's .venv\Scripts (Python 3.10 - see docs/environment_lock.md for why
         3.10 and not the officially-documented 3.8) and the aqtinstall Qt 5.15.2 bin directory
         to PATH.
      3. Sets QT_QPA_PLATFORM_PLUGIN_PATH / QT_PLUGIN_PATH to the aqt Qt install's plugins dir -
         required because the distro's RViz binaries were built against a conda Qt and cannot
         auto-derive their plugin directory the normal Qt way (relative to their own DLL path).
         See fix_qt_conda_imports.ps1.
      4. Prints a summary of what was set, and the known packaging-gap DLLs it assumes have
         already been installed (spdlog, tinyxml2, console_bridge, yaml-cpp, assimp, libcurl,
         Qt import patching) - run fetch_dependencies.ps1 + fix_missing_spdlog_dll.ps1 +
         fix_missing_vendor_dlls.ps1 + fix_qt_conda_imports.ps1 first if you haven't.

    TWO STRUCTURAL DEFECTS in the official Windows archive have to be fixed once before
    rviz2.exe will even start. Both fixes are deterministic, not workarounds:
      1. Two Qt copies. rviz2.exe/rviz_common/rviz_rendering/rviz_default_plugins were built
         against a conda Qt and import Qt5Core_conda.dll etc. Supplying those names as copies of
         the aqt Qt DLLs does NOT work: the copies' own import tables still reference the PLAIN
         names, so both a plain Qt5Core.dll and a Qt5Core_conda.dll load into one process.
         QApplication then sets its static in one copy while rviz_common reads
         QCoreApplication::arguments() from the other (null) copy -> "Please instantiate the
         QApplication object first". FIX: scripts/fix_qt_conda_imports.ps1 binary-patches those
         four binaries to import the plain Qt5 names, so exactly one Qt copy loads.
      2. Missing OpenSSL. Fast-DDS (fastrtps-2.6.dll, the default rmw_fastrtps_cpp middleware) needs
         libssl-1_1-x64.dll + libcrypto-1_1-x64.dll, absent from the distro -> the RMW fails to
         load -> rcl init throws rclcpp::exceptions::RCLError -> unhandled -> 0xC0000409, no window.
         FIX: scripts/fix_missing_openssl_dll.ps1 installs the OpenSSL 1.1.1 x64 pair.
    Neither is caused by RDP: both reproduce identically on the local console.

.PARAMETER Ros2DistroRoot
    Default: D:\development\ros2_humble\ros2-windows
.PARAMETER QtBinDir
    Default: D:\development\Qt\5.15.2\msvc2019_64\bin
.PARAMETER RepoRoot
    Default: the repo root (two levels up from this script's directory)
#>
param(
    [string]$Ros2DistroRoot = "D:\development\ros2_humble\ros2-windows",
    [string]$QtVersionDir = "D:\development\Qt\5.15.2\msvc2019_64",
    [string]$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path
)

$ErrorActionPreference = "Stop"

$venvScripts = Join-Path $RepoRoot ".venv\Scripts"
$qtBin = Join-Path $QtVersionDir "bin"
$qtPlugins = Join-Path $QtVersionDir "plugins"

if (-not (Test-Path (Join-Path $Ros2DistroRoot "local_setup.bat"))) {
    throw "ROS2 distro not found at $Ros2DistroRoot - run scripts\fetch_dependencies.ps1 first."
}
if (-not (Test-Path $venvScripts)) {
    Write-Warning ".venv not found at $venvScripts - create it first: `"C:\Program Files\Python310\python.exe`" -m venv `"$RepoRoot\.venv`"; then pip install -r requirements.txt"
}
if (-not (Test-Path $qtBin)) {
    Write-Warning "Qt not found at $qtBin - install via: .venv\Scripts\python.exe -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --outputdir D:\development\Qt"
}

# Put venv Python + Qt bin on PATH FIRST (so `python` resolves to the venv's 3.10, matching the
# distro's compiled ABI, before local_setup.bat's own PATH additions).
#
# local_setup.bat does its own PATH-prepending internally; if the CALLER's PATH is
# already large (e.g. after importing the VS dev environment, or just a naturally
# accumulated user PATH), that internal `set PATH=...;%PATH%` line can exceed
# cmd.exe's ~8191-character line-length limit ("The input line is too long. / The
# syntax of the command is incorrect."). Seed the batch file's PATH from a short,
# machine-baseline value instead of the live (potentially huge) %PATH%, then merge
# the caller's real PATH back in afterward so nothing on it is lost -- just kept
# out of what this one batch file has to parse.
$callerPath = $env:PATH
$machineBasePath = [Environment]::GetEnvironmentVariable('PATH', 'Machine')

$tempEnvDump = Join-Path $env:TEMP "qar_ros2_env_dump.txt"
$batch = @"
@echo off
set PATH=$venvScripts;$qtBin;$machineBasePath
call "$Ros2DistroRoot\local_setup.bat"
set > "$tempEnvDump"
"@
$batchPath = Join-Path $env:TEMP "qar_dump_env.bat"
$batch | Out-File -Encoding ascii $batchPath
cmd /c $batchPath | Out-Null

Get-Content $tempEnvDump -Encoding UTF8 | ForEach-Object {
    if ($_ -match '^([A-Za-z_][A-Za-z0-9_\(\)]*)=(.*)$') {
        try { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] -ErrorAction SilentlyContinue } catch {}
    }
}
$env:PATH = "$env:PATH;$callerPath"

# Qt plugin discovery for the patched RViz binaries (see fix_qt_conda_imports.ps1).
$env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $qtPlugins "platforms"
$env:QT_PLUGIN_PATH = $qtPlugins

Write-Host "=== Qar RViz2 environment set up in this PowerShell session ==="
Write-Host "PATH prepended with: venv Scripts, Qt bin, ROS2 distro bin dirs (via local_setup.bat)"
Write-Host "AMENT_PREFIX_PATH: $env:AMENT_PREFIX_PATH"
Write-Host "QT_QPA_PLATFORM_PLUGIN_PATH: $env:QT_QPA_PLATFORM_PLUGIN_PATH"
Write-Host ""
Write-Host "python  -> $(Get-Command python -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source)"
Write-Host ""
Write-Host "REMINDER: 'ros2' the CLI wrapper exe has a broken shebang (build-machine path)." -ForegroundColor Yellow
Write-Host "  Use: python `"$Ros2DistroRoot\Scripts\ros2-script.py`" <args>   (NOT ros2.exe / ros2 directly)" -ForegroundColor Yellow
Write-Host "rviz2.exe works: launch it directly (rviz2.exe) or via 'ros2 run rviz2 rviz2'." -ForegroundColor Green
Write-Host "  Requires fix_qt_conda_imports.ps1 + fix_missing_openssl_dll.ps1 to have been run once" -ForegroundColor Green
Write-Host "  (in addition to the spdlog/vendor DLL fixes). See docs/environment_lock.md." -ForegroundColor Green
