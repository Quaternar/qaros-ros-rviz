<#
.SYNOPSIS
    Materialize the stale absolute build-machine paths baked into the ROS 2 Humble
    Windows binary distro's exported CMake targets, so downstream ament plugins can
    pass CMake's generate-step existence checks.

.DESCRIPTION
    PACKAGING GAP (discovered in M3):
    The official release-humble-20260220 Windows archive was built on a pixi CI
    machine and its installed *Export.cmake files hard-code absolute paths under
        C:/pixi_ws/.pixi/envs/default/...
    into imported-target INTERFACE_INCLUDE_DIRECTORIES / link interfaces (eigen3,
    the conda "Library/include", numpy, Qt, mkspecs, python310.lib). Those paths do
    not exist on this machine, so when a downstream target (our plugin) pulls them
    into its link/generate closure, CMake fails at the generate step with
        "Imported target ... includes non-existent path ...".

    Our plugin does not actually compile against these third-party trees (it uses
    the aqt Qt via Qt5_DIR and the vendored Eigen via fix_missing_eigen.ps1), so the
    paths only need to EXIST. This script creates that directory tree, populating
    eigen3 + numpy with real headers (harmless, and future-proof for later
    milestones) and copying a real python310.lib. Idempotent.

.PARAMETER PixiRoot
    Baked CI prefix to materialize. Default: C:\pixi_ws\.pixi\envs\default
.PARAMETER Ros2DistroRoot
    Root of the extracted ROS 2 Humble Windows distro (used to find the
    eigen3_vendor headers written by fix_missing_eigen.ps1). Precedence:
    this parameter, then env ROS2_INSTALL_ROOT, then the detected default
    on the audited dev machine.
.PARAMETER NumpySourceInclude
    Path to a source '.../numpy/core/include' directory. If omitted, the script
    searches the machine's Anaconda/Miniconda installs for a numpy package.
#>
param(
    [string]$PixiRoot = "C:\pixi_ws\.pixi\envs\default",
    [string]$Ros2DistroRoot = "",
    [string]$NumpySourceInclude = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrEmpty($Ros2DistroRoot)) {
    $Ros2DistroRoot = if ($env:ROS2_INSTALL_ROOT) { $env:ROS2_INSTALL_ROOT } else { "D:\development\ros2_humble\ros2-windows" }
}

function Ensure-Dir { param([string]$Path) if (-not (Test-Path $Path)) { New-Item -ItemType Directory -Force -Path $Path | Out-Null; Write-Host "  mkdir $Path" -ForegroundColor Green } }

# --- Directories referenced as INTERFACE_INCLUDE_DIRECTORIES ----------------
$libInclude = Join-Path $PixiRoot "Library\include"
Ensure-Dir $libInclude
Ensure-Dir (Join-Path $PixiRoot "Library\mkspecs\win32-msvc")
foreach ($q in @("qt", "qt\QtWidgets", "qt\QtGui", "qt\QtCore", "qt\QtANGLE")) {
    Ensure-Dir (Join-Path $libInclude $q)
}

# eigen3 (populate with the vendored Eigen headers, if available) ------------
$eigenDst = Join-Path $libInclude "eigen3"
Ensure-Dir $eigenDst
$eigenSrc = Join-Path $Ros2DistroRoot "opt\eigen3_vendor\include\eigen3"
if ((Test-Path (Join-Path $eigenSrc "Eigen")) -and -not (Test-Path (Join-Path $eigenDst "Eigen"))) {
    Copy-Item -Path (Join-Path $eigenSrc "*") -Destination $eigenDst -Recurse -Force
    Write-Host "  populated eigen3 headers -> $eigenDst" -ForegroundColor Green
}

# numpy core/include (populate from Anaconda numpy for py310) ----------------
$numpyDst = Join-Path $PixiRoot "lib\site-packages\numpy\core\include"
Ensure-Dir $numpyDst
if (-not (Test-Path (Join-Path $numpyDst "numpy"))) {
    if ($NumpySourceInclude -and (Test-Path (Join-Path $NumpySourceInclude "numpy"))) {
        Copy-Item -Path (Join-Path $NumpySourceInclude "*") -Destination $numpyDst -Recurse -Force
        Write-Host "  populated numpy headers -> $numpyDst" -ForegroundColor Green
    } else {
        # Search common per-user Anaconda/Miniconda install roots (not hard-coded
        # to any one account or numpy build string) for a numpy package's bundled
        # C headers.
        $condaRoots = @(
            "$env:USERPROFILE\anaconda3", "$env:USERPROFILE\miniconda3",
            "C:\ProgramData\anaconda3", "C:\ProgramData\miniconda3"
        ) | Where-Object { Test-Path $_ }
        $found = $null
        foreach ($r in $condaRoots) {
            $found = Get-ChildItem -Path (Join-Path $r "pkgs") -Directory -Filter "numpy-*" -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName "Lib\site-packages\numpy\core\include" } |
                Where-Object { Test-Path (Join-Path $_ "numpy") } |
                Select-Object -First 1
            if ($found) { break }
        }
        if ($found) {
            Copy-Item -Path (Join-Path $found "*") -Destination $numpyDst -Recurse -Force
            Write-Host "  populated numpy headers -> $numpyDst (from $found)" -ForegroundColor Green
        } else {
            Write-Host "  WARNING: no numpy C headers found; pass -NumpySourceInclude if a downstream build needs them." -ForegroundColor Yellow
        }
    }
}

# --- python310.lib (a real link input referenced by *_rosidl_generator_py) --
$libsDir = Join-Path $PixiRoot "libs"
Ensure-Dir $libsDir
$py310lib = Join-Path $libsDir "python310.lib"
if (-not (Test-Path $py310lib)) {
    $srcLib = "C:\Program Files\Python310\libs\python310.lib"
    if (Test-Path $srcLib) {
        Copy-Item -Path $srcLib -Destination $py310lib -Force
        Write-Host "  copied python310.lib -> $py310lib" -ForegroundColor Green
    } else {
        Write-Host "  WARNING: system python310.lib not found; python bindings link may fail." -ForegroundColor Yellow
    }
}

# --- tinyxml.lib (urdfdom's exported link interface hard-references it) ------
# urdfdomExport.cmake lists C:/pixi_ws/.pixi/envs/default/Library/lib/tinyxml.lib
# (tinyxml v1) in urdfdom::* INTERFACE_LINK_LIBRARIES, which propagates onto our
# plugin's link line via rviz_common -> urdf -> urdfdom. The plugin references no
# tinyxml symbols (tinyxml is linked inside urdfdom.dll), so a VALID but empty
# stub archive lets the link succeed. Requires cl.exe + lib.exe on PATH (they are
# after build_plugin.ps1 imports vcvars64).
$libDir = Join-Path $PixiRoot "Library\lib"
Ensure-Dir $libDir
$tinyxmlLib = Join-Path $libDir "tinyxml.lib"
if (-not (Test-Path $tinyxmlLib)) {
    $cl = Get-Command cl -ErrorAction SilentlyContinue
    $libExe = Get-Command lib -ErrorAction SilentlyContinue
    if ($cl -and $libExe) {
        $tmp = Join-Path $env:TEMP ("qar_stub_" + [System.Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $tmp | Out-Null
        # Empty translation unit -> valid (symbol-less) obj -> valid archive.
        Set-Content -Path (Join-Path $tmp "stub.c") -Value "/* qar tinyxml stub */`r`n" -Encoding ascii
        Push-Location $tmp
        try {
            & cl /nologo /c stub.c 2>&1 | Out-Null
            & lib /nologo /out:"$tinyxmlLib" stub.obj 2>&1 | Out-Null
        } finally { Pop-Location }
        Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
        if (Test-Path $tinyxmlLib) {
            Write-Host "  created stub tinyxml.lib -> $tinyxmlLib" -ForegroundColor Green
        } else {
            Write-Host "  WARNING: failed to create stub tinyxml.lib." -ForegroundColor Yellow
        }
    } else {
        Write-Host "  WARNING: cl/lib not on PATH; cannot create stub tinyxml.lib (run from build_plugin.ps1)." -ForegroundColor Yellow
    }
}

Write-Host "Stale pixi_ws paths materialized under $PixiRoot." -ForegroundColor Green
