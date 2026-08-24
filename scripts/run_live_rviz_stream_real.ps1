<#
.SYNOPSIS
    Live round-trip (LAUNCHES GUI WINDOWS). Stream the live RViz scene to the QarOS
    Visualizer (qar-streaming-viz.exe) through the qar-streaming-c SDK.

.DESCRIPTION
    The consumer is the QarOS Visualizer; the producer is RViz2 loading
    QarStreamingPanel in QAR_PANEL_LIVE_STREAM mode. The panel's QarCApiSenderFactory
    runs the session flow: library_init -> runtime_create (runtime_binaries_folder_path
    = QarOS bin) -> create_session -> serialize -> session_join ->
    peer_subscribe_updates -> invite_peer(visualizer connection string). The invited
    Visualizer then surfaces as a peer and gets an OpenGL render stream.

    !!! THIS SCRIPT OPENS GUI WINDOWS (rviz2 + the QarOS Visualizer). Run it when you
        can watch it; it is not part of a headless build/verify pipeline.

    PATH: the real SDK's transitive runtime DLLs (qar_streaming.dll, qar-streaming-c.dll,
    zenohc*.dll, the GStreamer stack, glib, etc.) all live in QarOS_binary/install/bin.
    That directory MUST be on PATH for both the plugin DLL to resolve its imports and
    for the runtime to spawn app binaries. This script prepends it.

.PARAMETER Scene         RViz config (default: installed scene3_grid_marker.rviz).
.PARAMETER QarSdkRoot    QarOS SDK install root (bin/include/lib/share). This
                         repository does not vendor the SDK (see README.md), so
                         there is no in-repo default -- resolution order is this
                         parameter, then the QAR_SDK_ROOT environment variable.
                         The script fails fast, before launching anything, if
                         neither resolves to a real qar-streaming-c.dll.
.PARAMETER QtVersionDir  aqt Qt root (default D:\development\Qt\5.15.2\msvc2019_64).
.PARAMETER Visualizers   How many QarOS Visualizers to invite AUTOMATICALLY at connect.
                         Default 0 (2026-08-14): connecting opens the session and
                         invites nothing; the operator spawns Visualizers on demand
                         with the panel's "Invite peer" button, and a real-device
                         session often wants none at all. Set 1-8 to opt back in.
.PARAMETER InvitePeers   Real devices to invite as soon as the session comes up, as
                         `kind[:hostname]` entries. Kinds: visualizer, hololens, quest,
                         zed — only hololens takes (and requires) a hostname, because
                         it is the only device type the SDK can address by name.
                         Example:
                           -InvitePeers hololens:HOLOLENS-A2B3,hololens:HOLOLENS-C4D5
                         An invited device lands in the allow-list and is admitted by
                         exact peer id, so -RealPeers is not needed for it. A device
                         that fails to invite is logged and skipped, never fatal.
.PARAMETER RealPeers     Set up for a demo with REAL onboarded peers (HoloLens,
                         METALENSE 2, Quest, Android, or a Visualizer on another
                         machine). Sets QAR_PANEL_PEER_ADMISSION=new, without which
                         those peers are silently refused: they join via QAROS
                         onboarding, never via qar_session_invite_peer, so the default
                         invited-only allow-list does not contain them.
.PARAMETER LaunchLauncherFirst
                         Start qar-runtime-launcher.exe before rviz2 (Finding 11
                         branch: needed if the in-proc runtime does NOT itself spawn
                         the visualizer). Off by default — try auto-spawn first.
.PARAMETER Confirm       Must be passed explicitly to actually launch. Without it the
                         script only PRINTS the plan and the resolved env (dry run),
                         so it is safe to invoke for inspection.
#>
param(
    [string]$Scene = "",
    [string]$QarSdkRoot = "",
    [string]$QtVersionDir = "D:\development\Qt\5.15.2\msvc2019_64",
    [ValidateRange(1, 8)]
    [int]$Visualizers = 0,
    [string[]]$InvitePeers = @(),
    [switch]$RealPeers,
    [switch]$LaunchLauncherFirst,
    [switch]$Confirm
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..").Path

# This repository does not vendor the QarOS SDK (it's a separate proprietary
# binary drop -- see README.md), so there is no in-repo default to fall back to.
if ([string]::IsNullOrEmpty($QarSdkRoot)) {
    if (-not [string]::IsNullOrEmpty($env:QAR_SDK_ROOT)) {
        $QarSdkRoot = $env:QAR_SDK_ROOT
    } else {
        throw @"
No QarOS SDK root given. Pass -QarSdkRoot <path to the SDK's install tree>, or
set the QAR_SDK_ROOT environment variable. The tree must contain bin\qar-streaming-c.dll.
See README.md ("The QarOS SDK is not included") for where to obtain it.
"@
    }
}

$sdkBin = Join-Path $QarSdkRoot "bin"
$vizExe = Join-Path $sdkBin "qar-streaming-viz.exe"
$launcherExe = Join-Path $sdkBin "qar-runtime-launcher.exe"

# Fail now, before touching the ROS 2/Qt environment or launching anything, rather
# than deep inside a live RViz session -- a missing/wrong SDK path used to surface
# only when qar_library_load() failed on "Connect to QarOS", by which point RViz
# was already running.
$sdkDll = Join-Path $sdkBin "qar-streaming-c.dll"
if (-not (Test-Path $sdkDll)) {
    throw "QarOS SDK DLL not found at $sdkDll. Pass -QarSdkRoot <QarOS install> (or set QAR_SDK_ROOT) pointing at the SDK's install tree."
}

# --- ROS 2 distro + Qt env, then the colcon overlay (pluginlib discovery) -----
. "$PSScriptRoot\env_setup.ps1" -QtVersionDir $QtVersionDir
$overlay = Join-Path $RepoRoot "install\local_setup.ps1"
if (-not (Test-Path $overlay)) {
    throw "Overlay not found at $overlay - run: scripts\build_plugin.ps1 -Real   first."
}
. $overlay

# --- Resolve scene config (grid + markers + the panel) ------------------------
if ([string]::IsNullOrEmpty($Scene)) {
    $installed = Get-ChildItem -Path "$RepoRoot\install\qar_rviz_streaming" -Recurse -Filter "scene3_grid_marker.rviz" -ErrorAction SilentlyContinue | Select-Object -First 1
    $Scene = if ($installed) { $installed.FullName } else { Join-Path $RepoRoot "qar_rviz_streaming\config\test_scenes\scene3_grid_marker.rviz" }
}

# --- Prefer the NVIDIA dGPU for rviz2 (vendor call 2026-07-29) -----------------
# If RViz renders on the integrated adapter while QAR encodes on the NVIDIA one, every
# frame goes through a cross-adapter copy (slow, and a source of driver-level surprises).
# Windows' per-application GPU preference lives in the registry; 2 = "High performance"
# (= the discrete NVIDIA GPU). This is a per-user setting for rviz2.exe only, it is
# idempotent, and it is exactly what the Settings > Display > Graphics UI writes.
$rvizExe = (Get-Command rviz2.exe -ErrorAction SilentlyContinue).Source
if ($rvizExe) {
    $gpuPrefKey = 'HKCU:\Software\Microsoft\DirectX\UserGpuPreferences'
    if (-not (Test-Path $gpuPrefKey)) { New-Item -Path $gpuPrefKey -Force | Out-Null }
    $current = (Get-ItemProperty -Path $gpuPrefKey -Name $rvizExe -ErrorAction SilentlyContinue).$rvizExe
    if ($current -ne 'GpuPreference=2;') {
        New-ItemProperty -Path $gpuPrefKey -Name $rvizExe -Value 'GpuPreference=2;' `
            -PropertyType String -Force | Out-Null
        Write-Host "  GPU preference: set High-performance (NVIDIA) for $rvizExe" -ForegroundColor Green
    } else {
        Write-Host "  GPU preference: already High-performance (NVIDIA) for $rvizExe"
    }
} else {
    Write-Warning "rviz2.exe not found on PATH - cannot set the NVIDIA GPU preference."
}

# --- Transitive-DLL PATH + runtime env ----------------------------------------
# QarOS_binary/install/bin carries qar_streaming.dll, qar-streaming-c.dll, zenohc.dll,
# the GStreamer/glib stack, etc. The QAR DLL is now loaded DYNAMICALLY by the adapter
# (no import lib), but its own transitive dependencies still resolve via this PATH, and
# the runtime spawns its launcher/visualizer from here.
$env:PATH = "$sdkBin;$env:PATH"
# Tell the QAR runtime which folder to spawn app binaries (the Visualizer) from.
$env:QAR_RUNTIME_BINARIES_DIR = $sdkBin
# The panel is a live-stream producer.
$env:QAR_PANEL_LIVE_STREAM = "1"
# Adapter breadcrumbs (peer ids, the WGL context latch, each raw C-API call). These go
# to stderr and are captured to the run log below — they are the primary evidence when
# a live run misbehaves, so they are ON for this (already interactive) script.
$env:QAR_ADAPTER_TRACE = "1"
# Zenoh (the QAR transport) is Rust and honours RUST_LOG; its own diagnostics are the
# only place that says WHY a session fails to come up. qar_runtime_create_session
# surfaces just "MsgInternalError -4 [ZenohMessaging.cpp:340]", while zenoh itself
# reports the actual cause ("Unable to open listener ...", "Unable to join multicast
# group ... on interface ...", "Unable to bind UDP port ..."). Captured into the
# stderr log below. Set RUST_LOG yourself to override/quieten.
# RUST_LOG alone is NOT enough: zenoh only reads it once the host initialises its
# logger, which RViz does not. QAR_ZENOH_LOG makes the adapter call zenohc's
# zc_init_log_from_env_or() for us; its value is the tracing filter.
if (-not $env:RUST_LOG) { $env:RUST_LOG = "zenoh=debug" }
if (-not $env:QAR_ZENOH_LOG) { $env:QAR_ZENOH_LOG = "debug" }
# QAR's own C++ logging severity, read by the runtime AND inherited by the launcher
# and the per-GUID mixer processes it spawns. The vendor asked for TRACE so that full
# logs accompany bug reports, so TRACE stays the default here.
#
# Be aware of what it costs once frames actually flow: at 60 FPS the mixer wrote
# ~71 MB in two minutes (mixerAndTargetApp_<guid>_stdout.log), several trace lines
# per frame per transfer topic. When investigating throughput or a Visualizer crash
# rather than a protocol problem, re-run with QAR_LOG_SEVERITY=warn to take that I/O
# out of the picture; QAR_PANEL_LOG_SEVERITY does the same for our in-process library.
if (-not $env:QAR_LOG_SEVERITY) { $env:QAR_LOG_SEVERITY = "trace" }
# Pose: QAR hands us head poses in ITS world, which is Y-up (OpenGL/AR); RViz/Ogre
# render a Z-up ROS world (REP-103). Without this the viewer arrives on its side,
# looking straight down at the scene. Confirmed live 2026-07-31.
# ORIENTATION ONLY: the viewer stays at XYZ = 0 so QAR's reported position and ours
# agree (we cannot tell QAR about an offset — v0.1.1 has no set-initial-pose API).
# *_YAW_DEG / *_HEIGHT_M / *_BACK_M remain available for experimentation and are
# tunable in the shell without a rebuild.
# NOTE: 'zup' is not yet the library default because the mock/capture paths and the
# pose-conversion unit tests still assert the documented identity mapping.
$env:QAR_POSE_WORLD  = "zup"
if (-not $env:QAR_POSE_YAW_DEG)  { $env:QAR_POSE_YAW_DEG  = "180" }
if (-not $env:QAR_POSE_HEIGHT_M) { $env:QAR_POSE_HEIGHT_M = "0" }
if (-not $env:QAR_POSE_BACK_M)   { $env:QAR_POSE_BACK_M   = "0" }

# Depth encoding (confirmed live 2026-08-02). The QarOS Visualizer expects the raw
# window-space z-buffer value in [0,1] and inverts it itself using near/far — NOT the
# metric linear metres of the M4/C3b design. With metric data it shows nothing up
# close, all colours crammed into a hairline band, then one saturated value; and it
# ignores both QarRenderFrameShow.depth_scale and rendered_near_far, so no producer-
# side linear scaling can compensate.
#
# far also matters now: NDC depth is reciprocal, so a 500 m far plane squeezes a
# room-sized scene into the first per-cent of the range (everything reads "near") and
# wastes almost all depth precision.
# NOT the library default yet: the mock round-trip and capture scripts still assert
# the documented metric encoding.
#
# near/far are NO LONGER forced here (2026-08-14): the panel itself now defaults to
# 0.01 m / 150 m, sized for real sensor data rather than the phase-1 demo scene.
# Setting QAR_PANEL_NEAR_M / QAR_PANEL_FAR_M in the shell still overrides both, which
# is how the depth experiments are run.
if (-not $env:QAR_DEPTH_ENCODING) { $env:QAR_DEPTH_ENCODING = "ndc" }

# Scene lighting (M10-live, 2026-08-05). RViz's ONLY light is a directional
# "MainDirectional" that VisualizationManager re-aims at the LOCAL view camera every
# frame — a headlight. Shared with remote peers that is simply wrong: their shading
# pivots around whenever the RViz operator orbits their own camera. The panel adds a
# world-fixed OMNI (point) light + raised ambient while streaming, and hides RViz's
# headlight so it stops swimming; both are restored when streaming stops.
#   QAR_SCENE_LIGHT            off | omni | per-peer | both (default)
#     omni     = raised ambient + FOUR directional lights on a tetrahedron, so every
#                surface normal faces at least one light and nothing is unlit, with
#                the two "from above" directions carrying the key. Directional lights
#                are position-independent, so this lights the world evenly however
#                far a peer stands from the origin. (2026-08-10: a single static
#                point light left far sides and undersides too dark.)
#     per-peer = one point light at EACH connected peer's head, updated every frame
#                inside the batch render lock.
#     both     = the default: even base + a modest personal highlight per peer.
#   QAR_SCENE_LIGHT_HEADLIGHT  keep => leave RViz's headlight on (adds our lights on
#     top; the swimming highlight remains, so this is for comparison only).
#   QAR_SCENE_LIGHT_AMBIENT    single grey value 0..1 (default 0.30)
#   QAR_SCENE_LIGHT_INTENSITY  key brightness (default 0.75)
#   QAR_SCENE_LIGHT_FILL       "from below" fills as a fraction of the key (default
#                              0.55). 1.0 = perfectly uniform and shapeless; 0 = lit
#                              from above only, undersides fall back to ambient.
#   QAR_SCENE_LIGHT_PEER       per-peer head-light brightness (default 0.35)
#   QAR_SCENE_LIGHT_RANGE      attenuation range; 0 = no distance falloff
if (-not $env:QAR_SCENE_LIGHT) { $env:QAR_SCENE_LIGHT = "both" }

# Multi-peer. One QarOS Visualizer == one physical client head (a pair of eye
# cameras), so N simultaneous users means N Visualizers, each with its own stereo
# stream. Each invite spawns another Visualizer window on this machine.
#   QAR_PANEL_VISUALIZERS       how many to AUTO-invite at connect (default 0 = none).
#                               Visualizers are normally opened on demand with the
#                               panel's "Invite peer" button instead.
#   QAR_PANEL_MAX_PEERS         ceiling on concurrent streams (default 8, per C8).
#   QAR_PANEL_PEER_ADMISSION    invited (default) | new | named | any
#     The peer subscription surfaces the runtime's INTERNAL app peers too — a live run
#     saw 5 distinct ids for 1 invited Visualizer — and building a sender for one of
#     those mismatches app ids, so the Visualizer's own frames get rejected.
#       invited : only ids we invited ourselves. Safe, and correct for the local
#                 Visualizer — but it REFUSES real onboarded peers, which never go
#                 through qar_session_invite_peer. Use -RealPeers for those.
#       new     : invited peers, plus anything that appears after the first few
#                 seconds (QAR_PANEL_PEER_SETTLE_MS, default 5000). The runtime's own
#                 peers are all up by then; a human onboarding a headset is not.
#                 This is the policy for a real HoloLens/Quest/remote-visualizer demo.
#       named   : + display-name match (QAR_PANEL_PEER_NAME_FILTER). Proven unusable
#                 on v0.1.1 — names are auto-generated AND change between updates.
#       any     : everything except ourselves. Diagnostic only.
#   QAR_PANEL_INVITE_PEERS      real devices invited at connect, e.g.
#     "hololens:HOLOLENS-A2B3,quest". Preferred over waiting for a device to onboard
#     itself: the invite returns the peer id, so the device is admitted by EXACT id
#     under any policy and needs no timing heuristic.
if (-not $env:QAR_PANEL_VISUALIZERS)    { $env:QAR_PANEL_VISUALIZERS    = "$Visualizers" }
if ($InvitePeers.Count -gt 0 -and -not $env:QAR_PANEL_INVITE_PEERS) {
    $env:QAR_PANEL_INVITE_PEERS = ($InvitePeers -join ',')
}
if (-not $env:QAR_PANEL_PEER_ADMISSION) {
    $env:QAR_PANEL_PEER_ADMISSION = if ($RealPeers) { "new" } else { "invited" }
}

Write-Host "=== M10 REAL RViz -> QarOS Visualizer round-trip ===" -ForegroundColor Magenta
Write-Host "  scene            : $Scene"
Write-Host "  QarOS SDK bin    : $sdkBin"
Write-Host "  Visualizer exe   : $vizExe"
Write-Host "  runtime-launcher : $launcherExe"
Write-Host "  QAR_RUNTIME_BINARIES_DIR = $env:QAR_RUNTIME_BINARIES_DIR"
Write-Host "  QAR_PANEL_LIVE_STREAM    = 1"
Write-Host "  QAR_SCENE_LIGHT          = $env:QAR_SCENE_LIGHT (headlight: $(if ($env:QAR_SCENE_LIGHT_HEADLIGHT -eq 'keep') {'kept'} else {'suppressed while streaming'}))"
Write-Host "  QAR_PANEL_VISUALIZERS    = $env:QAR_PANEL_VISUALIZERS (admission: $env:QAR_PANEL_PEER_ADMISSION)"
Write-Host "  QAR_PANEL_INVITE_PEERS   = $(if ($env:QAR_PANEL_INVITE_PEERS) { $env:QAR_PANEL_INVITE_PEERS } else { '(none - invite from the panel, or pass -InvitePeers)' })"
Write-Host "  LaunchLauncherFirst      = $LaunchLauncherFirst"

if (-not (Test-Path $vizExe))       { Write-Warning "Visualizer exe not found at $vizExe" }
if (-not (Test-Path $launcherExe))  { Write-Warning "runtime-launcher exe not found at $launcherExe" }

if (-not $Confirm) {
    Write-Host ""
    Write-Host "DRY RUN (no -Confirm): environment resolved above; NOT launching any GUI." -ForegroundColor Yellow
    Write-Host "To actually run the interactive session, re-invoke with -Confirm (opens windows)." -ForegroundColor Yellow
    Write-Host "See README.md (Manual verification) for the step-by-step, branch-by-branch flow." -ForegroundColor Yellow
    return
}

# ---------------------------------------------------------------------------
# LAUNCH (only with -Confirm).
#
# Launch rviz2 ONLY. Per the vendor call (2026-07-29) the QAR library starts and
# configures qar-runtime-launcher.exe itself (from launcher_config_json + the runtime
# binaries folder) and then spawns qar-streaming-viz.exe in response to invite_peer.
# Starting the launcher by hand competes with it and breaks peer/consumer id
# coordination, so -LaunchLauncherFirst is intentionally NOT honoured any more.
# ---------------------------------------------------------------------------
if ($LaunchLauncherFirst) {
    Write-Warning ("-LaunchLauncherFirst is obsolete and is IGNORED: the QAR library " +
        "starts qar-runtime-launcher.exe itself. Starting it manually breaks peer/consumer " +
        "id coordination (vendor call 2026-07-29).")
}

# --- Live scene content --------------------------------------------------------
# scene3_grid_marker.rviz subscribes to the test markers; without a publisher RViz
# shows nothing but the grid, and we would be streaming an empty scene (M10-live:
# this is why the first corrected run looked "empty" on BOTH ends). The mock
# round-trip script has always started this; the real one must too.
# Reap publishers leaked by earlier runs FIRST. This is not tidiness — it is load-
# bearing. Every publisher is a ROS 2 node, and DDS assigns each participant the next
# UDP port pair from 7410 upwards. Leaked publishers accumulate, the ladder climbs,
# and once it reaches 7446 it collides with zenoh's multicast scouting port: QAR then
# fails with the opaque "MsgInternalError Msg: -4 [ZenohMessaging.cpp:340]" and no
# session can be created at all. That is exactly what happened on 2026-08-02 after
# ~19 runs had leaked 36 processes. Match on the command line so only OUR publishers
# are touched, never an unrelated python.
$stale = Get-CimInstance Win32_Process -Filter "Name='python.exe'" -ErrorAction SilentlyContinue |
    Where-Object { $_.CommandLine -match 'publish_test_' }
if ($stale) {
    foreach ($s in $stale) { Stop-Process -Id $s.ProcessId -Force -ErrorAction SilentlyContinue }
    Write-Host ("  reaped {0} leaked test publisher(s) from earlier runs" -f @($stale).Count) -ForegroundColor Yellow
    Start-Sleep -Milliseconds 500
}

$pyExe = (Get-Command python -ErrorAction SilentlyContinue).Source
if ($pyExe) {
    $pub = Start-Process -FilePath $pyExe `
        -ArgumentList (Join-Path $PSScriptRoot "publish_test_markers.py") `
        -PassThru -WindowStyle Hidden
    Write-Host "marker publisher PID=$($pub.Id) (publish_test_markers.py, frame 'map')" -ForegroundColor Green
    Write-Host "  stopped automatically by the next run; or: Stop-Process -Id $($pub.Id)"
} else {
    Write-Warning "python not found - markers will NOT be published and the scene will be empty."
}

# Guard: zenoh scouting needs UDP 7446. If anything holds it, QAR fails to create a
# session with an error that names neither the port nor the culprit — say so up front.
$busy7446 = Get-NetUDPEndpoint -LocalPort 7446 -ErrorAction SilentlyContinue
if ($busy7446) {
    foreach ($b in $busy7446) {
        $owner = (Get-Process -Id $b.OwningProcess -ErrorAction SilentlyContinue).ProcessName
        Write-Warning ("UDP 7446 (zenoh scouting) is held by pid $($b.OwningProcess) ($owner). " +
            "QAR session creation will fail with 'MsgInternalError Msg: -4'.")
    }
}

Write-Host "=== launching rviz2 (REAL live-stream producer) ===" -ForegroundColor Magenta
# Capture stdout/stderr to files: the adapter trace + any SEH/crash breadcrumb must
# survive the window disappearing if RViz dies (which is exactly the case we debug).
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$outLog = Join-Path $RepoRoot "rviz_live_$stamp.out.log"
$errLog = Join-Path $RepoRoot "rviz_live_$stamp.err.log"
$rviz = Start-Process -FilePath "rviz2.exe" -ArgumentList @("-d", "$Scene") -PassThru `
    -RedirectStandardOutput $outLog -RedirectStandardError $errLog
Write-Host "rviz2 PID=$($rviz.Id)."
Write-Host ""
Write-Host "In the RViz 'Qar Streaming' panel click 'Connect to QarOS' — the QAR runtime"
Write-Host "then starts its launcher and the Visualizer window appears on its own."
Write-Host ""
Write-Host "  RViz stdout : $outLog" -ForegroundColor Cyan
Write-Host "  RViz stderr : $errLog   <- adapter trace lives here" -ForegroundColor Cyan
Write-Host "  QAR TRACE   : $env:TEMP\quaternar" -ForegroundColor Cyan
