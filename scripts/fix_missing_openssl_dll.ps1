<#
.SYNOPSIS
    Install the OpenSSL 1.1.1 x64 runtime DLLs (libssl-1_1-x64.dll, libcrypto-1_1-x64.dll) that
    Fast-DDS (fastrtps-2.6.dll) needs but the official ROS 2 Humble Windows distro omits.

.DESCRIPTION
    ROOT CAUSE (found in M0b): the default ROS 2 middleware for Humble is rmw_fastrtps_cpp, which
    loads fastrtps-2.6.dll. That Fast-DDS build links OpenSSL 1.1.1 (for DDS-Security / TLS) via
    the DLL names "libssl-1_1-x64.dll" and "libcrypto-1_1-x64.dll". Those two DLLs are NOT shipped
    anywhere in the release-humble-20260220 Windows archive (same packaging-gap class as spdlog and
    the 5 vendor DLLs). Because fastrtps-2.6.dll can't be loaded, rmw_implementation reports
    "failed to load any RMW implementations" and rcl init throws:
        rclcpp::exceptions::RCLError: Failed to initialize init options:
        failed to load any RMW implementations
    rviz2.exe calls this during VisualizerApp::init(); the exception is unhandled, std::terminate
    runs, and the process dies with exit code 0xC0000409 (fastfail) with NO visible window. It also
    breaks rclpy.init() and any rclcpp node. (NOTE: the previous agent's `import rclpy` test passed
    because merely importing rclpy does not load an RMW -- only init() does.)

    FIX: drop OpenSSL 1.1.1 x64 DLLs (named exactly libssl-1_1-x64.dll / libcrypto-1_1-x64.dll)
    into the distro's bin. Any OpenSSL 1.1.1 patch level works -- the whole 1.1.1 series is
    ABI-stable, so 1.1.1q / 1.1.1t / etc. are interchangeable for these symbols. Do NOT use OpenSSL
    3.x (different SONAME/filenames: libssl-3-x64.dll) -- Fast-DDS 2.6 imports the 1.1 names.

    Source: this script copies the pair from an existing conda/Anaconda/Miniconda install on the
    machine (conda ships them under <env>\Library\bin and <root>\DLLs with the exact -x64 name).
    On the M0b dev machine they came from
        C:\Users\venda\anaconda3\envs\opencv_py310\Library\bin\  (OpenSSL 1.1.1t)
    If no conda copy is found, obtain libssl-1_1-x64.dll + libcrypto-1_1-x64.dll from any OpenSSL
    1.1.1 Windows x64 distribution (e.g. conda-forge `openssl=1.1.1`, or slproweb Win64 OpenSSL
    1.1.1) and place both in the distro bin manually.

    Idempotent: skips if both DLLs already exist in the distro bin.

.PARAMETER Ros2DistroRoot
    Default: D:\development\ros2_humble\ros2-windows
.PARAMETER SourceDir
    Optional explicit directory containing the two DLLs. If omitted, common conda locations are
    searched and the newest matching pair is used.
#>
param(
    [string]$Ros2DistroRoot = "D:\development\ros2_humble\ros2-windows",
    [string]$SourceDir = ""
)
$ErrorActionPreference = "Stop"

$bin = Join-Path $Ros2DistroRoot "bin"
$needed = @("libssl-1_1-x64.dll", "libcrypto-1_1-x64.dll")

$haveAll = $true
foreach ($n in $needed) { if (-not (Test-Path (Join-Path $bin $n))) { $haveAll = $false } }
if ($haveAll) {
    Write-Host "OpenSSL 1.1.1 x64 DLLs already present in $bin -- nothing to do."
    return
}

function Find-SourceDir {
    param([string[]]$dlls)
    $candidates = @()
    if ($SourceDir) { $candidates += $SourceDir }
    $condaRoots = @(
        "$env:USERPROFILE\anaconda3", "$env:USERPROFILE\miniconda3",
        "C:\ProgramData\anaconda3", "C:\ProgramData\miniconda3"
    ) | Where-Object { Test-Path $_ }
    foreach ($r in $condaRoots) {
        # prefer env Library\bin (usually newer), then root Library\bin, then DLLs
        Get-ChildItem "$r\envs" -Directory -ErrorAction SilentlyContinue | ForEach-Object {
            $candidates += (Join-Path $_.FullName "Library\bin")
        }
        $candidates += (Join-Path $r "Library\bin")
        $candidates += (Join-Path $r "DLLs")
    }
    foreach ($c in $candidates) {
        if (-not $c) { continue }
        $ok = $true
        foreach ($d in $dlls) { if (-not (Test-Path (Join-Path $c $d))) { $ok = $false; break } }
        if ($ok) { return $c }
    }
    return $null
}

$src = Find-SourceDir -dlls $needed
if (-not $src) {
    throw @"
Could not locate libssl-1_1-x64.dll + libcrypto-1_1-x64.dll in any conda install.
Obtain OpenSSL 1.1.1 (x64) DLLs with those exact names (conda-forge openssl=1.1.1, or a Win64
OpenSSL 1.1.1 build) and re-run with -SourceDir <dir containing both>, or copy them into:
  $bin
"@
}

foreach ($n in $needed) {
    Copy-Item (Join-Path $src $n) $bin -Force
    $v = (Get-Item (Join-Path $bin $n)).VersionInfo.ProductVersion
    Write-Host "Installed $n (OpenSSL $v) from $src"
}
Write-Host "Done. Fast-DDS (rmw_fastrtps_cpp) can now load; rcl/rclpy init and rviz2 will start."
