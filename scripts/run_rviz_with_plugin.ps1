<#
.SYNOPSIS
    Launch RViz2 with the qar_rviz_streaming panel pre-loaded, either interactively
    or in a non-interactive "auto-dump-and-quit" verification mode.

.DESCRIPTION
    Sources the base ROS 2 distro environment (env_setup.ps1) and then the colcon
    overlay (install/local_setup.ps1) so pluginlib discovers the panel, then runs
    rviz2 with config/qar_streaming_dev.rviz (which pre-loads the QarStreamingPanel).

    VERIFICATION MODE (-AutoDump): sets QAR_PANEL_AUTODUMP so the panel, a few
    seconds after load, writes a diagnostics snapshot (context validity, scene
    manager name, camera/node counts, and a mock peer add/stream/remove round-trip)
    to the given file and then quits RViz cleanly. This gives scriptable proof with
    no human clicking. Requires the local console session + the M0b environment
    fixes (fix_qt_conda_imports.ps1 + fix_missing_openssl_dll.ps1 already applied).

.PARAMETER Config
    RViz config to load. Default: the installed qar_streaming_dev.rviz.
.PARAMETER AutoDump
    Path to the diagnostics dump file. When set, enables auto-dump-and-quit mode.
.PARAMETER DelayMs
    Delay before the auto-dump fires (default 5000 ms).
.PARAMETER QtVersionDir
    aqt Qt root. Default: D:\development\Qt\5.15.2\msvc2019_64
#>
param(
    [string]$Config = "",
    [string]$AutoDump = "",
    [string]$AutoCapture = "",
    [string]$AutoStereo = "",
    [string]$AutoMulti = "",
    [string]$GlSpike = "",
    [int]$MultiPeers = 3,
    [int]$MultiTicks = 180,
    [double]$IpdMetres = 0.35,
    [int]$StereoFrames = 140,
    [int]$DelayMs = 5000,
    [string]$QtVersionDir = "D:\development\Qt\5.15.2\msvc2019_64"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path

# --- 1. Base ROS 2 distro + Qt + venv environment --------------------------
. "$PSScriptRoot\env_setup.ps1" -QtVersionDir $QtVersionDir

# --- 2. Overlay: this package's colcon install -----------------------------
$overlay = Join-Path $RepoRoot "install\local_setup.ps1"
if (-not (Test-Path $overlay)) {
    throw "Overlay not found at $overlay - run scripts\build_plugin.ps1 first."
}
Write-Host "=== Sourcing overlay: $overlay ===" -ForegroundColor Cyan
. $overlay

# --- 3. Resolve the dev config ---------------------------------------------
if (-not $Config) {
    $installed = Get-ChildItem -Path "$RepoRoot\install\qar_rviz_streaming" -Recurse -Filter "qar_streaming_dev.rviz" -ErrorAction SilentlyContinue | Select-Object -First 1
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
    if (Test-Path $AutoDump) { Remove-Item $AutoDump -Force }
} else {
    Remove-Item Env:QAR_PANEL_AUTODUMP -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_AUTODUMP_DELAY_MS -ErrorAction SilentlyContinue
}

# --- 4b. Offscreen-capture (M4 Gate A) env ---------------------------------
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

# --- 4c. Single-peer stereo capture (M5) env -------------------------------
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

# --- 4d. Multi-peer batched scheduler proof (M6) ---------------------------
if ($AutoMulti) {
    $env:QAR_PANEL_AUTOMULTI = $AutoMulti
    $env:QAR_PANEL_AUTOMULTI_DELAY_MS = "$DelayMs"
    $env:QAR_PANEL_AUTOMULTI_PEERS = "$MultiPeers"
    $env:QAR_PANEL_AUTOMULTI_TICKS = "$MultiTicks"
    Write-Host "=== AUTO-MULTI (multi-peer batched render scheduler) mode ===" -ForegroundColor Yellow
    Write-Host "  Prefix : $AutoMulti (writes *_automulti.txt + *_peerN_color.png)"
    Write-Host "  Peers  : $MultiPeers (independent views; C8 validate 3-4, up to 8)"
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

# --- 4e. OpenGL zero-copy texture-handoff spike (M7 Gate B) ----------------
if ($GlSpike) {
    $env:QAR_PANEL_GLSPIKE = $GlSpike
    $env:QAR_PANEL_GLSPIKE_DELAY_MS = "$DelayMs"
    Write-Host "=== GL-SPIKE (OpenGL zero-copy handoff, Gate B) mode ===" -ForegroundColor Yellow
    Write-Host "  Prefix : $GlSpike (writes *_glspike.txt)"
    Write-Host "  Delay  : $DelayMs ms (then the spike runs; RViz quits after)"
    $gsParent = Split-Path -Parent $GlSpike
    if ($gsParent -and -not (Test-Path $gsParent)) { New-Item -ItemType Directory -Force -Path $gsParent | Out-Null }
    $gsp = "$GlSpike" + "_glspike.txt"
    if (Test-Path $gsp) { Remove-Item $gsp -Force }
} else {
    Remove-Item Env:QAR_PANEL_GLSPIKE -ErrorAction SilentlyContinue
    Remove-Item Env:QAR_PANEL_GLSPIKE_DELAY_MS -ErrorAction SilentlyContinue
}

# --- 5. Launch RViz2 -------------------------------------------------------
Write-Host "=== Launching rviz2 ===" -ForegroundColor Cyan
& rviz2.exe -d "$Config"
$code = $LASTEXITCODE
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
if ($GlSpike) {
    $summaryFile = "$GlSpike" + "_glspike.txt"
    if (Test-Path $summaryFile) {
        Write-Host "=== GL-SPIKE result ($summaryFile) ===" -ForegroundColor Green
        Get-Content $summaryFile | Write-Host
        $pass = (Get-Content $summaryFile | Select-String -Pattern '^overall_pass=true' -Quiet)
        if ($pass) {
            Write-Host "GL-SPIKE: PASS (zero-copy handoff proven; id valid, roundtrip match, fence signaled)" -ForegroundColor Green
            exit 0
        } else {
            Write-Host "GL-SPIKE: FAIL (see stats above)" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "GL-SPIKE: NO STATS FILE PRODUCED (panel may not have loaded / spike failed)." -ForegroundColor Red
        exit 2
    }
}
exit $code
