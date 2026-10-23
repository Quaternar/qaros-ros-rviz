# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Launches RViz2 with the qar_rviz_streaming panel pre-loaded, either interactively or in one
    of the non-interactive "auto" verification modes.

.DESCRIPTION
    Sources the ROS 2 distribution environment (env_setup.ps1), then the plugin's colcon overlay
    (<Prefix>\local_setup.ps1) so that pluginlib finds the panel, then runs rviz2 with
    config/qar_streaming_dev.rviz (which pre-loads the QarStreamingPanel).

    VERIFICATION MODES (-AutoDump, -AutoCapture, -AutoStereo, -AutoMulti): each one sets
    environment variables that make the panel, a few seconds after it has loaded, write a result
    (a diagnostics snapshot, or captured images with a summary) and then quit RViz cleanly. That
    gives scriptable proof with no human clicking. The modes need the local console session and a
    distribution on which fix_qt_conda_imports.ps1 and fix_missing_openssl_dll.ps1 have been run.
    A mode's output is written to the given path; a plain name without a folder part goes into
    -OutDir. The stereo and multi-peer modes delete older files that start with the given
    prefix in its folder first.

.PARAMETER Config
    RViz config to load. Default: qar_streaming_dev.rviz from <Prefix>\share\qar_rviz_streaming.
.PARAMETER AutoDump
    Path of the diagnostics dump file (context validity, scene manager name, camera and node
    counts, and a synthetic peer add/stream/remove round trip). Enables the auto-dump-and-quit
    mode.
.PARAMETER AutoCapture
    Output prefix of the offscreen capture mode (writes *_color.png, *_depth.png, *_depth.f32 and
    *_capture.txt).
.PARAMETER AutoStereo
    Output prefix of the single-peer stereo capture mode (writes *_frameA/B_left|right_color.png,
    the depth images and *_autostereo.txt).
.PARAMETER AutoMulti
    Output prefix of the multi-peer batched render mode (writes *_automulti.txt and
    *_peerN_color.png).
.PARAMETER MultiPeers
    Number of peers in -AutoMulti mode (default 3).
.PARAMETER MultiTicks
    Number of paced batches in -AutoMulti mode (default 180).
.PARAMETER IpdMetres
    Stereo baseline of -AutoStereo mode in metres (default 0.35, wide enough for visible parallax).
.PARAMETER StereoFrames
    Number of frames of -AutoStereo mode (default 140).
.PARAMETER DelayMs
    Delay before the scripted mode starts (default 5000 ms).
.PARAMETER OutDir
    Folder for scripted-mode outputs given as a plain name. Default: %TEMP%\qar_rviz_preview.
.PARAMETER Prefix
    Install prefix that holds the plugin (the folder with local_setup.ps1). Default: the
    environment variable QAR_PREFIX. Required.
.PARAMETER Ros2Root
    ROS 2 Humble distribution root. Default: the environment variable ROS2_INSTALL_ROOT.
    Required.
.PARAMETER QtRoot
    Qt 5.15.2 msvc2019_64 folder. Default: the environment variable QAR_QT_ROOT.
.PARAMETER TestMarkers
    Start publish_test_markers.py before RViz and stop it when RViz exits. It publishes a small
    MarkerArray on /qar_test_markers, which test_scenes\scene3_grid_marker.rviz shows. With or
    without the switch the script exports QAR_PANEL_TEST_MARKERS_PYTHON (the python of this
    shell) and QAR_PANEL_TEST_MARKERS_SCRIPT (the full path of publish_test_markers.py) to RViz
    when both exist, so that the "Test markers" button of the panel's Diagnostics tab can start
    and stop the publisher at any time. When python or the script is missing, neither variable
    is exported (a value from the shell is dropped) and the script says why.
#>
param(
    [string]$Config = "",
    [string]$AutoDump = "",
    [string]$AutoCapture = "",
    [string]$AutoStereo = "",
    [string]$AutoMulti = "",
    [int]$MultiPeers = 3,
    [int]$MultiTicks = 180,
    [double]$IpdMetres = 0.35,
    [int]$StereoFrames = 140,
    [int]$DelayMs = 5000,
    [string]$OutDir = "",
    [string]$Prefix = "",
    [Alias('Ros2DistroRoot', 'Ros2InstallRoot')]
    [string]$Ros2Root = "",
    [Alias('QtVersionDir')]
    [string]$QtRoot = "",
    [switch]$TestMarkers
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path

. "$PSScriptRoot\qar_locations.ps1"
$Prefix = Resolve-QarLocation -Value $Prefix -Parameter 'Prefix' -EnvVar 'QAR_PREFIX' `
    -What 'Install prefix' -Section 'Directory layout and environment variables'
$Ros2Root = Resolve-QarLocation -Value $Ros2Root -Parameter 'Ros2Root' -EnvVar 'ROS2_INSTALL_ROOT' `
    -What 'ROS 2 distribution' -Marker 'local_setup.bat' -Section 'ROS 2 Humble for Windows'
$QtRoot = Resolve-QarLocation -Value $QtRoot -Parameter 'QtRoot' -EnvVar 'QAR_QT_ROOT' `
    -What 'Qt 5.15.2' -Marker 'bin\Qt5Core.dll' -Section 'Qt 5.15.2' -Optional

# --- 1. Base ROS 2 distribution + Qt + venv environment --------------------
. "$PSScriptRoot\env_setup.ps1" -Ros2Root $Ros2Root -QtRoot $QtRoot

# --- 2. Overlay: this package's colcon install -----------------------------
$overlay = Join-Path $Prefix "local_setup.ps1"
if (-not (Test-Path $overlay)) {
    throw "Overlay not found at $overlay - run scripts\build_plugin.ps1 first."
}
Write-Host "=== Sourcing overlay: $overlay ===" -ForegroundColor Cyan
. $overlay

# --- 2b. Output folder of the scripted modes -----------------------------------
# A plain name without a folder part goes into -OutDir (default: under %TEMP%).
if ([string]::IsNullOrEmpty($OutDir)) { $OutDir = Join-Path $env:TEMP "qar_rviz_preview" }
function Resolve-PreviewOutput([string]$Name) {
    if ([string]::IsNullOrEmpty($Name) -or -not [string]::IsNullOrEmpty([System.IO.Path]::GetDirectoryName($Name))) { return $Name }
    return Join-Path $OutDir $Name
}
$AutoDump = Resolve-PreviewOutput $AutoDump
$AutoCapture = Resolve-PreviewOutput $AutoCapture
$AutoStereo = Resolve-PreviewOutput $AutoStereo
$AutoMulti = Resolve-PreviewOutput $AutoMulti

# --- 3. Resolve the dev config ---------------------------------------------
if (-not $Config) {
    $installed = Get-ChildItem -Path "$Prefix\share\qar_rviz_streaming" -Recurse -Filter "qar_streaming_dev.rviz" -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($installed) {
        $Config = $installed.FullName
    } else {
        $Config = Join-Path $RepoRoot "qar_rviz_streaming\config\qar_streaming_dev.rviz"
    }
}
Write-Host "RViz config: $Config"

# --- 4. Verification (auto-dump) env ---------------------------------------
if ($AutoDump) {
    $env:QAR_PANEL_AUTODUMP = $AutoDump
    $env:QAR_PANEL_AUTODUMP_DELAY_MS = "$DelayMs"
    Write-Host "=== AUTO-DUMP verification mode ===" -ForegroundColor Yellow
    Write-Host "  Dump file : $AutoDump"
    Write-Host "  Delay     : $DelayMs ms (then RViz quits)"
    $dumpParent = Split-Path -Parent $AutoDump
    if ($dumpParent -and -not (Test-Path $dumpParent)) { New-Item -ItemType Directory -Force -Path $dumpParent | Out-Null }
    if (Test-Path $AutoDump) { Remove-Item $AutoDump -Force }
} else {
    Remove-Item Env:QAR_PANEL_AUTODUMP -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTODUMP_DELAY_MS -ErrorAction SilentlyContinue
}

# --- 4b. Offscreen-capture env ---------------------------------------------
if ($AutoCapture) {
    $env:QAR_PANEL_AUTOCAPTURE = $AutoCapture
    $env:QAR_PANEL_AUTOCAPTURE_DELAY_MS = "$DelayMs"
    Write-Host "=== AUTO-CAPTURE (offscreen) mode ===" -ForegroundColor Yellow
    Write-Host "  Prefix : $AutoCapture (writes *_color.png, *_depth.png, *_depth.f32, *_capture.txt)"
    Write-Host "  Delay  : $DelayMs ms (then RViz quits)"
    $capParent = Split-Path -Parent $AutoCapture
    if ($capParent -and -not (Test-Path $capParent)) { New-Item -ItemType Directory -Force -Path $capParent | Out-Null }
    foreach ($suffix in @("_color.png", "_depth.png", "_depth.f32", "_capture.txt")) {
        $p = "$AutoCapture$suffix"
        if (Test-Path $p) { Remove-Item $p -Force }
    }
} else {
    Remove-Item Env:QAR_PANEL_AUTOCAPTURE -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTOCAPTURE_DELAY_MS -ErrorAction SilentlyContinue
}

# --- 4c. Single-peer stereo capture env ------------------------------------
if ($AutoStereo) {
    $env:QAR_PANEL_AUTOSTEREO = $AutoStereo
    $env:QAR_PANEL_AUTOSTEREO_DELAY_MS = "$DelayMs"
    $env:QAR_PANEL_AUTOSTEREO_FRAMES = "$StereoFrames"
    # Format with the invariant culture so a comma-decimal locale (e.g. cs-CZ)
    # does not emit "0,35", which the plugin's C-locale parse would reject.
    $env:QAR_STEREO_IPD_M = $IpdMetres.ToString([System.Globalization.CultureInfo]::InvariantCulture)
    Write-Host "=== AUTO-STEREO (single-peer stereo) mode ===" -ForegroundColor Yellow
    Write-Host "  Prefix : $AutoStereo (writes *_frameA/B_left|right_color.png + _depth + _stereo.txt)"
    Write-Host "  IPD    : $IpdMetres m (wide baseline for visible parallax)"
    Write-Host "  Frames : $StereoFrames (captures pose A at 0, pose B at last)"
    Write-Host "  Delay  : $DelayMs ms (then RViz quits)"
    $stParent = Split-Path -Parent $AutoStereo
    if ($stParent -and -not (Test-Path $stParent)) { New-Item -ItemType Directory -Force -Path $stParent | Out-Null }
    Get-ChildItem -Path (Split-Path -Parent $AutoStereo) -Filter ((Split-Path -Leaf $AutoStereo) + "*") -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:QAR_PANEL_AUTOSTEREO -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTOSTEREO_DELAY_MS -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTOSTEREO_FRAMES -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_STEREO_IPD_M -ErrorAction SilentlyContinue
}

# --- 4d. Multi-peer batched scheduler proof --------------------------------
if ($AutoMulti) {
    $env:QAR_PANEL_AUTOMULTI = $AutoMulti
    $env:QAR_PANEL_AUTOMULTI_DELAY_MS = "$DelayMs"
    $env:QAR_PANEL_AUTOMULTI_PEERS = "$MultiPeers"
    $env:QAR_PANEL_AUTOMULTI_TICKS = "$MultiTicks"
    Write-Host "=== AUTO-MULTI (multi-peer batched render scheduler) mode ===" -ForegroundColor Yellow
    Write-Host "  Prefix : $AutoMulti (writes *_automulti.txt + *_peerN_color.png)"
    Write-Host "  Peers  : $MultiPeers (independent views; 3-4 validated, up to 8)"
    Write-Host "  Ticks  : $MultiTicks (~60 Hz paced batches)"
    Write-Host "  Delay  : $DelayMs ms (then the batch run starts; RViz quits after)"
    $mParent = Split-Path -Parent $AutoMulti
    if ($mParent -and -not (Test-Path $mParent)) { New-Item -ItemType Directory -Force -Path $mParent | Out-Null }
    Get-ChildItem -Path (Split-Path -Parent $AutoMulti) -Filter ((Split-Path -Leaf $AutoMulti) + "*") -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
} else {
    Remove-Item Env:QAR_PANEL_AUTOMULTI -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTOMULTI_DELAY_MS -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTOMULTI_PEERS -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTOMULTI_TICKS -ErrorAction SilentlyContinue
}

# --- 4e. Test markers ------------------------------------------------------
# publish_test_markers.py publishes the markers that test_scenes\scene3_grid_marker.rviz shows.
# -TestMarkers starts it before RViz (below) and stops it when RViz exits. With or without the
# switch, the python of this shell and the full path of the script are exported to RViz whenever
# both exist, so that the "Test markers" button of the panel's Diagnostics tab can start and stop
# the publisher on demand. When either is missing nothing is exported (a value from the shell is
# dropped) and the line below says why.
$pyExe = (Get-Command python -ErrorAction SilentlyContinue).Source
$markersScript = Join-Path $PSScriptRoot "publish_test_markers.py"
$markersMissing = $null
if (-not $pyExe) {
    $markersMissing = "python not found"
} elseif (-not (Test-Path -LiteralPath $markersScript -PathType Leaf)) {
    $markersMissing = "$markersScript not found"
}
if ($markersMissing) {
    Remove-Item Env:QAR_PANEL_TEST_MARKERS_PYTHON -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_TEST_MARKERS_SCRIPT -ErrorAction SilentlyContinue
    $markersNote = "nothing exported"
    if ($TestMarkers) { $markersNote = "-TestMarkers cannot start them, nothing exported" }
    Write-Host "test markers: unavailable ($markersMissing; $markersNote)" -ForegroundColor Yellow
} else {
    $env:QAR_PANEL_TEST_MARKERS_PYTHON = $pyExe
    $env:QAR_PANEL_TEST_MARKERS_SCRIPT = $markersScript
    if ($TestMarkers) {
        Write-Host "test markers: on ($pyExe $markersScript)"
    } else {
        Write-Host "test markers: off (start them with -TestMarkers or from the Diagnostics tab)"
    }
    Write-Host "  QAR_PANEL_TEST_MARKERS_PYTHON = $pyExe"
    Write-Host "  QAR_PANEL_TEST_MARKERS_SCRIPT = $markersScript"
}

# --- 5. Launch RViz2 -------------------------------------------------------
$markerPublisher = $null
if ($TestMarkers -and -not $markersMissing) {
    $markerPublisher = Start-Process -FilePath $pyExe -ArgumentList ('"' + $markersScript + '"') `
        -PassThru -WindowStyle Hidden
    Write-Host "marker publisher PID=$($markerPublisher.Id) (publish_test_markers.py)" `
        -ForegroundColor Green
} elseif ($TestMarkers) {
    Write-Warning "$markersMissing - markers will NOT be published and the scene will be empty."
}
Write-Host "=== Launching rviz2 ===" -ForegroundColor Cyan
try {
    & rviz2.exe -d "$Config"
    $code = $LASTEXITCODE
} finally {
    # The publisher this script started ends with RViz, also when RViz fails to start or the
    # script is interrupted.
    if ($markerPublisher) {
        Stop-Process -Id $markerPublisher.Id -Force -ErrorAction SilentlyContinue
        Write-Host "marker publisher PID=$($markerPublisher.Id) stopped"
    }
}
Write-Host "rviz2 exited with code $code"

if ($AutoDump) {
    if (Test-Path $AutoDump) {
        Write-Host "=== AUTO-DUMP result ($AutoDump) ===" -ForegroundColor Green
        Get-Content $AutoDump | Write-Host
        $pass = (Get-Content $AutoDump | Select-String -Pattern '^overall_pass=true' -Quiet)
        if ($pass) {
            Write-Host "VERIFICATION: PASS" -ForegroundColor Green
            exit 0
        } else {
            Write-Host "VERIFICATION: FAIL (see dump above)" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "VERIFICATION: NO DUMP FILE PRODUCED (panel may not have loaded)." -ForegroundColor Red
        exit 2
    }
}

if ($AutoCapture) {
    $statsFile = "$AutoCapture" + "_capture.txt"
    if (Test-Path $statsFile) {
        Write-Host "=== AUTO-CAPTURE result ($statsFile) ===" -ForegroundColor Green
        Get-Content $statsFile | Write-Host
        foreach ($suffix in @("_color.png", "_depth.png", "_depth.f32")) {
            $p = "$AutoCapture$suffix"
            if (Test-Path $p) {
                $sz = (Get-Item $p).Length
                Write-Host ("  produced: {0} ({1} bytes)" -f $p, $sz)
            }
        }
        $pass = (Get-Content $statsFile | Select-String -Pattern '^overall_capture_pass=true' -Quiet)
        if ($pass) {
            Write-Host "CAPTURE: PASS (scene content + non-uniform depth)" -ForegroundColor Green
            exit 0
        } else {
            Write-Host "CAPTURE: FAIL (see stats above)" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "CAPTURE: NO STATS FILE PRODUCED (panel may not have loaded / capture failed)." -ForegroundColor Red
        exit 2
    }
}

if ($AutoStereo) {
    $summaryFile = "$AutoStereo" + "_autostereo.txt"
    if (Test-Path $summaryFile) {
        Write-Host "=== AUTO-STEREO result ($summaryFile) ===" -ForegroundColor Green
        Get-Content $summaryFile | Write-Host
        Get-ChildItem -Path (Split-Path -Parent $AutoStereo) -Filter ((Split-Path -Leaf $AutoStereo) + "*.png") -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host ("  produced: {0} ({1} bytes)" -f $_.FullName, $_.Length)
        }
        $pass = (Get-Content $summaryFile | Select-String -Pattern '^overall_pass=true' -Quiet)
        if ($pass) {
            Write-Host "STEREO: PASS (left/right parallax; near shifts more than far)" -ForegroundColor Green
            exit 0
        } else {
            Write-Host "STEREO: FAIL (see summary above)" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "STEREO: NO SUMMARY FILE PRODUCED (panel may not have loaded / capture failed)." -ForegroundColor Red
        exit 2
    }
}

if ($AutoMulti) {
    $summaryFile = "$AutoMulti" + "_automulti.txt"
    if (Test-Path $summaryFile) {
        Write-Host "=== AUTO-MULTI result ($summaryFile) ===" -ForegroundColor Green
        Get-Content $summaryFile | Write-Host
        Get-ChildItem -Path (Split-Path -Parent $AutoMulti) -Filter ((Split-Path -Leaf $AutoMulti) + "*.png") -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host ("  produced: {0} ({1} bytes)" -f $_.FullName, $_.Length)
        }
        $pass = (Get-Content $summaryFile | Select-String -Pattern '^overall_pass=true' -Quiet)
        if ($pass) {
            Write-Host "MULTI-PEER: PASS (all peers >=30 FPS, one coherent batch/tick)" -ForegroundColor Green
            exit 0
        } else {
            Write-Host "MULTI-PEER: FAIL (see summary above)" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "MULTI-PEER: NO SUMMARY FILE PRODUCED (panel may not have loaded / run failed)." -ForegroundColor Red
        exit 2
    }
}
exit $code
