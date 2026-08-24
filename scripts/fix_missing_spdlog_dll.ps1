<#
.SYNOPSIS
    Builds spdlog.dll from source and installs it into the ROS 2 Humble Windows binary distro's
    bin/Lib directories, working around a packaging gap in the official release archive.

.DESCRIPTION
    The official ros2-humble-*-windows-release-amd64.zip archive ships the rcl_logging_spdlog
    and spdlog_vendor ament packages' CMake/share metadata, but NOT the actual spdlog.dll binary
    that rcl_logging_spdlog.dll links against. Without it, rcl.dll (used by BOTH rclpy and
    rclcpp/rviz2.exe) fails to load with "DLL load failed ... module could not be found", which
    blocks `ros2 --help`, `import rclpy`, and rviz2.exe alike.

    This was diagnosed at M0 time by:
      1. Using dumpbin /dependents (VS2019) recursively from rclpy's _rclpy_pybind11.pyd down to
         rcl.dll -> rcl_logging_spdlog.dll -> spdlog.dll, and confirming spdlog.dll is absent
         both in the extracted tree AND inside the original downloaded zip (`unzip -l`).
      2. Reading the exact mangled C++ symbols rcl_logging_spdlog.dll imports from spdlog.dll via
         `dumpbin /imports`. The `fmt::v8::basic_string_view<char>` parameter type in
         `logger::log(...)` proves the required spdlog bundles fmt >= 8.0 (spdlog >= 1.9.0),
         which is NEWER than the 1.8.2 pin found in ros2/spdlog_vendor's "1.3.1" tag (the pin
         recorded in this release's reference ros2.repos) - ROS 2 Humble has been patched many
         times since 2022 and the actual Windows binary build had moved its spdlog_vendor pin
         forward without the reference ros2.repos reflecting it.
      3. Building spdlog v1.9.2 (known Humble-era pin bundling fmt 8.0.1) from source with the
         same MSVC v142 toolset and RelWithDebInfo config as the rest of the distro, then
         confirming via `dumpbin /exports` that ALL required mangled symbols match exactly.

.PARAMETER Ros2DistroRoot
    Path to the extracted ROS 2 Windows distro root (containing bin/, Lib/, share/, ...).
    Default: D:\development\ros2_humble\ros2-windows

.PARAMETER BuildRoot
    Scratch directory for downloading/building spdlog. Default: D:\development\build_spdlog

.PARAMETER SpdlogVersion
    spdlog release tag to build. Default: v1.9.2 (verified ABI-compatible at M0 time - see above).
    If a future ROS2 Windows release ships a different spdlog pin, re-derive the required version
    via `dumpbin /imports` on rcl_logging_spdlog.dll (look for the fmt::vN inline namespace tag in
    the mangled `logger::log` symbol) before changing this default.

.EXAMPLE
    ./scripts/fix_missing_spdlog_dll.ps1
#>
param(
    [string]$Ros2DistroRoot = "D:\development\ros2_humble\ros2-windows",
    [string]$BuildRoot = "D:\development\build_spdlog",
    [string]$SpdlogVersion = "v1.9.2"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$targetDll = Join-Path $Ros2DistroRoot "bin\spdlog.dll"
if (Test-Path $targetDll) {
    Write-Host "spdlog.dll already present at $targetDll - nothing to do. Delete it to force a rebuild."
    exit 0
}

New-Item -ItemType Directory -Force -Path $BuildRoot | Out-Null
$zipPath = Join-Path (Split-Path $BuildRoot) "downloads\spdlog-$SpdlogVersion.zip"
New-Item -ItemType Directory -Force -Path (Split-Path $zipPath) | Out-Null

if (-not (Test-Path $zipPath)) {
    $url = "https://github.com/gabime/spdlog/archive/refs/tags/$SpdlogVersion.zip"
    Write-Host "Downloading $url"
    Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
}

$srcDir = Join-Path $BuildRoot "spdlog-$($SpdlogVersion.TrimStart('v'))"
if (-not (Test-Path $srcDir)) {
    Write-Host "Extracting to $BuildRoot"
    Expand-Archive -Path $zipPath -DestinationPath $BuildRoot -Force
}

$buildDir = Join-Path $BuildRoot "build"
Write-Host "Configuring (VS2019 x64, matching the distro's v142 toolset)"
cmake -S $srcDir -B $buildDir -G "Visual Studio 16 2019" -A x64 `
    -DSPDLOG_BUILD_SHARED=ON `
    -DSPDLOG_BUILD_EXAMPLE=OFF `
    -DSPDLOG_BUILD_TESTS=OFF `
    -DCMAKE_INSTALL_PREFIX="$BuildRoot\install"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "Building spdlog (RelWithDebInfo, matching the distro's build config)"
cmake --build $buildDir --config RelWithDebInfo --target spdlog
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

$builtDll = Join-Path $buildDir "RelWithDebInfo\spdlog.dll"
$builtLib = Join-Path $buildDir "RelWithDebInfo\spdlog.lib"

Write-Host "Verifying required symbols are present (spot-check against rcl_logging_spdlog.dll's import table)"
$dumpbinCandidates = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2019" -Recurse -Filter "dumpbin.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match "Hostx64\\x64" } | Select-Object -First 1
if ($dumpbinCandidates) {
    $exports = & $dumpbinCandidates.FullName /exports $builtDll 2>&1 | Out-String
    if ($exports -notmatch "register_logger" -or $exports -notmatch "basic_file_sink") {
        Write-Warning "Built spdlog.dll does not appear to export expected symbols - ABI mismatch is possible. Re-check SpdlogVersion."
    } else {
        Write-Host "Symbol spot-check OK (register_logger, basic_file_sink found)."
    }
} else {
    Write-Warning "dumpbin.exe not found - skipping symbol verification."
}

Write-Host "Installing into distro: $targetDll"
Copy-Item $builtDll $targetDll -Force
Copy-Item $builtLib (Join-Path $Ros2DistroRoot "Lib\spdlog.lib") -Force

$hash = Get-FileHash $targetDll -Algorithm SHA256
Write-Host "Done. spdlog.dll SHA256: $($hash.Hash)"
Write-Host "Re-run the M0 smoke test (ros2 --help / rviz2.exe) to confirm."
