# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Streams the LIVE RViz scene to the QarOS Visualizer (qar-streaming-viz.exe) through the
    qar-streaming-c SDK. LAUNCHES GUI WINDOWS (rviz2 and the QarOS Visualizer); without
    -Confirm it only prints the resolved configuration and starts nothing.

.DESCRIPTION
    The consumer is the QarOS Visualizer and the producer is RViz2 loading the
    QarStreamingPanel in QAR_PANEL_LIVE_STREAM mode (the plugin built by build_plugin.ps1).
    The panel's QarCApiSenderFactory runs the QarOS session flow: library_init ->
    runtime_create (runtime_binaries_folder_path = the SDK's bin folder) -> create_session ->
    serialize -> session_join -> peer_subscribe_updates -> invite_peer (visualizer connection
    string). The invited Visualizer then surfaces as a peer and gets a real OpenGL render
    stream.

    !!! WITHOUT -Confirm NOTHING IS STARTED, CHANGED OR STOPPED: no window, no registry write,
        no process stop, no log rotation, no Fast-DDS shared-memory clean-up. The script prints
        the resolved locations and environment and ends with exit code 0. The environment of
        the calling shell is put back when the script ends, in both modes.
        With -Confirm the script starts rviz2, writes the GPU preference of rviz2.exe to the
        registry (HKCU), stops test publishers left over from earlier runs (the Python
        processes that run publish_test_markers.py, publish_test_tf.py, publish_test_robot.py
        or publish_test_pointcloud.py, also when you started them in another window) and
        cleans up Fast-DDS shared memory of dead processes.

    PATH: the SDK's runtime DLLs (qar_streaming.dll, qar-streaming-c.dll, zenohc*.dll, the
    GStreamer stack, glib, etc.) live in <SdkRoot>\bin. The plugin does not need it on
    PATH: the adapter loads qar-streaming-c.dll by full path with flags that skip PATH; the
    runtime gets its launcher from QAR_RUNTIME_BINARIES_DIR. The script prepends it anyway.

    LOGGING IS QUIET BY DEFAULT: a low-I/O run. QarOS logs at `warn` (QAR_LOG_SEVERITY for the
    runtime/launcher/mixer, QAR_PANEL_LOG_SEVERITY for our in-process library;
    QAR_PANEL_LOG_CONSOLE=off keeps that library from also echoing every line into
    rviz_live_*.out.log), there is no zenoh logging, and none of the per-frame adapter traces
    (QAR_ADAPTER_TRACE / _TRACE_ALL / _FRAME_TRACE) is on. Our own 2-second "HB" line and the
    "AR VIDEO:" summaries stay on - they are cheap. Pass -TraceLogs for the full TRACE set a
    bug report needs. A quieter value you set yourself in the shell always wins; a LOUD one
    (trace/debug severity, zenoh or adapter tracing) is cleared in quiet mode unless
    -KeepLogEnv is given, because a leftover loud value defeats quiet mode without a sign.
    The mode is printed as "log mode".

    Before launching (-Confirm), every *.log in the QAR log directory over 256 MB
    ($env:QAR_PANEL_LOG_DIR, default %TEMP%\quaternar) is renamed to <name>.prev,
    replacing an older .prev, so a run never appends to a GB-sized file. The dry run only
    reports what it would rotate.

    The script also WARNS (it never blocks) about machine-level causes of stalls that
    hit every stream at once: a %TEMP% drive with under 10% or under 15 GB free, other
    processes using the NVIDIA GPU for compute (nvidia-smi), and less than ~4 GB of
    commit charge left (Win32_OperatingSystem.FreeVirtualMemory).

    It also reads the version of the C++ runtime next to rviz2.exe (<Ros2Root>\bin\MSVCP140.dll):
    the QarOS library needs 14.40 or newer, and the ROS 2 distribution ships 14.29. With -Confirm
    an older runtime stops the script before anything is started or changed, with the command of
    scripts\fix_old_vc_runtime.ps1 that repairs it; the dry run prints the same text as a warning.

    It also looks for <Ros2Root>\bin\tinyxml.dll, which the URDF parser of RViz needs and the ROS 2
    distribution does not ship: without it RViz ends with the exit code 0xC0000409 as soon as a
    robot description reaches the Robot model display. With -Confirm a missing file stops the
    script before anything is started or changed, with the command of
    scripts\fix_missing_tinyxml_dll.ps1 that builds it; the dry run prints the same text as a
    warning.

.PARAMETER SdkRoot       QarOS SDK install root (the folder with bin\qar-streaming-c.dll).
                         Default: the environment variable QAR_SDK_ROOT. Required.
.PARAMETER Prefix        Install prefix that holds the plugin (the folder with
                         local_setup.ps1). Default: the environment variable QAR_PREFIX.
                         Required. Without -Confirm a missing overlay is only reported.
.PARAMETER QtRoot        Qt 5.15.2 msvc2019_64 folder. Default: the environment variable
                         QAR_QT_ROOT. Required.
.PARAMETER Ros2Root      ROS 2 Humble distribution root. Default: the environment variable
                         ROS2_INSTALL_ROOT. Required.
.PARAMETER Scene         RViz config. Default: scene3_grid_marker.rviz from
                         <Prefix>\share\qar_rviz_streaming\config\test_scenes.
.PARAMETER Visualizers   How many QarOS Visualizers to invite AUTOMATICALLY at connect
                         (0-8). Default 0: connecting opens the session and invites
                         nothing; the operator spawns Visualizers on demand with the
                         panel's "Invite peer" button, and a real-device session often
                         wants none at all. Set 1-8 to invite automatically.
.PARAMETER InvitePeers   Real devices to invite as soon as the session comes up, as
                         `kind[:hostname]` entries. Kinds: visualizer, hololens, quest,
                         zed - only hololens takes (and requires) a hostname, because
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
.PARAMETER TraceLogs     Collect FULL logs (what a bug report needs): QAR_LOG_SEVERITY=trace,
                         zenoh at debug (RUST_LOG + QAR_ZENOH_LOG) and the adapter
                         breadcrumb trace (QAR_ADAPTER_TRACE, in rviz_live_*.err.log).
                         Expect GB-sized logs, and do NOT judge stalls or latency in this
                         mode: its own I/O can cause them. Without it (the default) QarOS
                         logs at `warn`, zenoh logging is off and no per-frame trace is on.
.PARAMETER KeepLogEnv    In QUIET mode (the default) loud logging values already present in
                         the shell (QAR_LOG_SEVERITY / QAR_PANEL_LOG_SEVERITY trace|debug,
                         RUST_LOG with trace|debug, QAR_ZENOH_LOG, QAR_ADAPTER_TRACE*,
                         QAR_ADAPTER_FRAME_TRACE) are CLEARED with a notice: such a value
                         silently turns the "quiet" run into a TRACE run (zenoh at debug
                         in RViz, the QarOS library at TRACE because it obeys
                         QAR_LOG_SEVERITY over the panel's own setting). -KeepLogEnv
                         keeps them as they are. Quieter values you set yourself (e.g.
                         QAR_LOG_SEVERITY=error) always win, in every mode.
.PARAMETER TimingCsv     Record per-frame stage timing for the stutter proof: sets
                         QAR_VIDEO_TIMING_DIR to a fresh <LogDir>\timing\<stamp> folder
                         (printed) and the plugin writes epoch-microsecond CSVs there -
                         video_*.csv (arrival -> decode -> capture -> try_begin -> submit
                         per AR video panel), rx_*.csv (every received message),
                         render3d*.csv (the 3D stereo stream's ready/not_ready runs and
                         submits) and gui_gaps.csv (Qt render-tick gaps) - buffered in
                         memory and flushed about once a second by a background thread,
                         never on the GUI thread. Pair it with the publisher's
                         timing_csv:=<dir>\publisher.csv and analyse with
                         scripts\analyze_video_timing.py. Independent of -TraceLogs; do not
                         combine the two when judging stalls (trace logging is itself I/O).
.PARAMETER NoDdsProfile  Do NOT export scripts\fastdds_large_data.xml as FASTRTPS_DEFAULT_PROFILES_FILE.
                         By default the launcher does, for everything it starts (rviz2 - RViz's own
                         Image display and the plugin's AR video reader are participants of that
                         one process - and the marker publisher), unless the variable is already set
                         in the shell (yours wins). The profile enlarges Fast-DDS's shared-memory
                         segment from 512 KB to 64 MB: with 6 MB video frames the default segment
                         drops fragments, and every dropped fragment is a reliable-repair round
                         trip, i.e. delivery jitter and stalls on EVERY subscriber of the topic.
                         scripts\publish_video_file.py applies the same file to itself, so the
                         publisher (the sending side, where it matters most) and rviz2 agree.
                         See the profile's own comments.
.PARAMETER LogDir        Folder for the rviz2 logs (rviz_live_<stamp>.out.log and .err.log)
                         and the -TimingCsv folders. Default: %TEMP%\qar_rviz_logs.
.PARAMETER Confirm       Must be passed explicitly to actually launch. Without it the
                         script only PRINTS the plan and the resolved env (dry run),
                         so it is safe to invoke for inspection.
#>
[CmdletBinding()]
param(
    [Alias('QarSdkRoot')]
    [string]$SdkRoot = "",
    [string]$Prefix = "",
    [Alias('QtVersionDir')]
    [string]$QtRoot = "",
    [Alias('Ros2DistroRoot', 'Ros2InstallRoot')]
    [string]$Ros2Root = "",
    [string]$Scene = "",
    [ValidateRange(0, 8)]
    [int]$Visualizers = 0,
    [string[]]$InvitePeers = @(),
    [switch]$RealPeers,
    [switch]$TraceLogs,
    [switch]$KeepLogEnv,
    [switch]$TimingCsv,
    [switch]$NoDdsProfile,
    [string]$LogDir = "",
    [switch]$Confirm
)

$ErrorActionPreference = "Stop"
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"

# The whole environment of the calling shell is saved here and put back in `finally` (dry run
# and real run). `$env:X = ...` in a script run as .\script.ps1 changes the CALLER's session,
# and ROS, Qt, DDS and logging variables left behind would colour the next run without a sign.
# The processes started below have their own copy of the environment by then, so restoring
# it afterwards changes nothing for them.
$envSnapshot = @{}
$envNow = [Environment]::GetEnvironmentVariables()
foreach ($n in $envNow.Keys) { $envSnapshot[$n] = $envNow[$n] }
function Restore-CallerEnvironment {
    $nowMap = @{}
    $now = [Environment]::GetEnvironmentVariables()
    foreach ($n in $now.Keys) { $nowMap[$n] = $now[$n] }
    foreach ($n in @($nowMap.Keys)) {
        if (-not $envSnapshot.ContainsKey($n)) { [Environment]::SetEnvironmentVariable($n, $null) }
    }
    foreach ($n in @($envSnapshot.Keys)) {
        if ($nowMap[$n] -cne $envSnapshot[$n]) { [Environment]::SetEnvironmentVariable($n, $envSnapshot[$n]) }
    }
}
try {
. (Join-Path $PSScriptRoot "qar_locations.ps1")
$SdkRoot = Resolve-QarLocation -Value $SdkRoot -Parameter 'SdkRoot' -EnvVar 'QAR_SDK_ROOT' `
    -What 'QarOS SDK' -Marker 'bin\qar-streaming-c.dll' -Section 'The QarOS SDK'
$Prefix = Resolve-QarLocation -Value $Prefix -Parameter 'Prefix' -EnvVar 'QAR_PREFIX' `
    -What 'Install prefix' -Section 'Directory layout and environment variables'
$QtRoot = Resolve-QarLocation -Value $QtRoot -Parameter 'QtRoot' -EnvVar 'QAR_QT_ROOT' `
    -What 'Qt 5.15.2' -Marker 'bin\Qt5Core.dll' -Section 'Qt 5.15.2'
$Ros2Root = Resolve-QarLocation -Value $Ros2Root -Parameter 'Ros2Root' -EnvVar 'ROS2_INSTALL_ROOT' `
    -What 'ROS 2 distribution' -Marker 'local_setup.bat' -Section 'ROS 2 Humble for Windows'
if ([string]::IsNullOrEmpty($LogDir)) { $LogDir = Join-Path $env:TEMP "qar_rviz_logs" }

$sdkBin = Join-Path $SdkRoot "bin"
$vizExe = Join-Path $sdkBin "qar-streaming-viz.exe"
$launcherExe = Join-Path $sdkBin "qar-runtime-launcher.exe"

# --- C++ runtime beside rviz2.exe: the QarOS library needs 14.40 or newer ---------
# RViz loads the msvcp140.dll that lies next to rviz2.exe, and the QarOS library then runs on
# that copy (a runtime that a process has loaded stays in use). The ROS 2 distribution ships
# 14.29, and on that copy "Connect to QarOS" ends RViz with an access violation in
# qar_library_init. Checked before anything is changed or started: -Confirm stops here, a dry
# run only warns (it starts nothing anyway). A file that cannot be read is only a warning.
$vcRuntimeFile = Join-Path $Ros2Root "bin\MSVCP140.dll"
$vcRuntimeMinimum = [version]'14.40'
$vcRuntimeVersion = $null
$vcRuntimeWarning = $null
try {
    $vcInfo = (Get-Item -LiteralPath $vcRuntimeFile -ErrorAction Stop).VersionInfo
    if (-not $vcInfo.FileVersion) { throw "the file has no version resource" }
    $vcRuntimeVersion = [version]::new($vcInfo.FileMajorPart, $vcInfo.FileMinorPart, $vcInfo.FileBuildPart, $vcInfo.FilePrivatePart)
} catch {
    $vcRuntimeWarning = "Could not read the version of $vcRuntimeFile ($($_.Exception.Message)); the C++ runtime beside rviz2.exe was not checked."
}
$vcRuntimeOld = [bool]($vcRuntimeVersion -and ($vcRuntimeVersion -lt $vcRuntimeMinimum))
if ($vcRuntimeOld) {
    $vcRuntimeWarning = "The C++ runtime beside rviz2.exe is too old: $vcRuntimeFile is version $vcRuntimeVersion.`n" +
        "The QarOS library needs $vcRuntimeMinimum or newer and runs on this copy, because RViz loads it first: RViz would end at 'Connect to QarOS' with an access violation in qar_library_init.`n" +
        "Repair the distribution, then run this script again:`n" +
        "  pwsh -NoProfile -File `"$PSScriptRoot\fix_old_vc_runtime.ps1`" -Ros2Root `"$Ros2Root`" -SdkRoot `"$SdkRoot`""
    if ($Confirm) {
        Write-Host $vcRuntimeWarning -ForegroundColor Red
        Write-Host "Nothing was started or changed." -ForegroundColor Red
        exit 1
    }
}

# --- tinyxml.dll beside rviz2.exe: the URDF parser of RViz imports it ---------------
# The Robot model display loads the URDF parser plugin (urdf_xml_parser.dll) when the first robot
# description arrives, and urdfdom_model.dll of that plugin imports tinyxml.dll. The ROS 2
# distribution does not ship the file, the plugin load fails, and nothing catches the exception:
# RViz ends with the exit code 0xC0000409. Only the file in the folder of rviz2.exe is looked
# for, not PATH: a program loads the DLLs of its own folder first. Checked before anything is
# changed or started: -Confirm stops here, a dry run only warns (it starts nothing anyway).
$tinyxmlFile = Join-Path $Ros2Root "bin\tinyxml.dll"
$tinyxmlWarning = $null
if (-not (Test-Path -LiteralPath $tinyxmlFile)) {
    $tinyxmlWarning = "tinyxml.dll is missing beside rviz2.exe: $tinyxmlFile does not exist.`n" +
        "RViz would end with the exit code 0xC0000409 as soon as a robot description reaches the Robot model display.`n" +
        "Repair the distribution, then run this script again:`n" +
        "  pwsh -NoProfile -File `"$PSScriptRoot\fix_missing_tinyxml_dll.ps1`" -Ros2Root `"$Ros2Root`""
    if ($Confirm) {
        Write-Host $tinyxmlWarning -ForegroundColor Red
        Write-Host "Nothing was started or changed." -ForegroundColor Red
        exit 1
    }
}

# --- ROS 2 distribution + Qt env, then the colcon overlay (pluginlib discovery) -----
. "$PSScriptRoot\env_setup.ps1" -Ros2Root $Ros2Root -QtRoot $QtRoot
$overlay = Join-Path $Prefix "local_setup.ps1"
$overlayFound = Test-Path $overlay
if ($overlayFound) {
    . $overlay
} elseif ($Confirm) {
    throw "Plugin overlay not found at $overlay - run: scripts\build_plugin.ps1 first."
} else {
    Write-Warning "Plugin overlay not found at $overlay (dry run, continuing) - run: scripts\build_plugin.ps1 first."
}

# --- Resolve scene config (grid + markers + the panel) ------------------------
if ([string]::IsNullOrEmpty($Scene)) {
    $Scene = Join-Path $Prefix "share\qar_rviz_streaming\config\test_scenes\scene3_grid_marker.rviz"
}

# --- Prefer the NVIDIA dGPU for rviz2 -------------------------------------------
# If RViz renders on the integrated adapter while QAR encodes on the NVIDIA one, every
# frame goes through a cross-adapter copy (slow, and a source of driver-level surprises).
# Windows' per-application GPU preference lives in the registry; 2 = "High performance"
# (= the discrete NVIDIA GPU). This is a per-user setting for rviz2.exe only, it is
# idempotent, and it is exactly what the Settings > Display > Graphics UI writes. It is
# written only with -Confirm; a dry run only reads it (see the resolved configuration).
$rvizExe = (Get-Command rviz2.exe -ErrorAction SilentlyContinue).Source
$gpuPrefKey = 'HKCU:\Software\Microsoft\DirectX\UserGpuPreferences'
if ($rvizExe -and $Confirm) {
    if (-not (Test-Path $gpuPrefKey)) { New-Item -Path $gpuPrefKey -Force | Out-Null }
    $current = (Get-ItemProperty -Path $gpuPrefKey -Name $rvizExe -ErrorAction SilentlyContinue).$rvizExe
    if ($current -ne 'GpuPreference=2;') {
        New-ItemProperty -Path $gpuPrefKey -Name $rvizExe -Value 'GpuPreference=2;' `
            -PropertyType String -Force | Out-Null
        Write-Host "  GPU preference: set High-performance (NVIDIA) for $rvizExe" -ForegroundColor Green
    } else {
        Write-Host "  GPU preference: already High-performance (NVIDIA) for $rvizExe"
    }
} elseif (-not $rvizExe) {
    Write-Warning "rviz2.exe not found on PATH - cannot set the NVIDIA GPU preference."
}

# --- SDK folder on PATH + runtime env -----------------------------------------
# <SdkRoot>\bin carries qar_streaming.dll, qar-streaming-c.dll, zenohc.dll, the GStreamer/glib
# stack, etc. The adapter loads qar-streaming-c.dll by full path with the flags
# LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR, so its imports come from
# this folder; PATH is never searched. The prepend stays for DLLs the SDK may load itself.
$env:PATH = "$sdkBin;$env:PATH"
# Tell the QAR runtime which folder to spawn app binaries (the Visualizer) from.
$env:QAR_RUNTIME_BINARIES_DIR = $sdkBin
# The panel is a live-stream producer.
$env:QAR_PANEL_LIVE_STREAM = "1"
# --- Logging: QUIET by default, the full TRACE set only with -TraceLogs --------
# Trace logging is heavy I/O: the mixer alone can write thousands of lines per second,
# and on a nearly full system drive under real-time antivirus scanning that can stall
# every QarOS stream at once. A run therefore logs quietly unless asked otherwise.
#
# One row per variable: what reads it, and its value in each mode ($null = left unset).
#   QAR_LOG_SEVERITY         QarOS itself: the runtime, plus the launcher and per-GUID
#                            mixer processes it spawns (they inherit RViz's environment) -
#                            AND the in-process library inside RViz, which reads it in
#                            PREFERENCE to the severity our panel passes in QarLibraryInit
#                            (environment=trace with the panel at error still logs;
#                            environment=error with the panel at trace does not). A stale
#                            value here therefore defeats QAR_PANEL_LOG_SEVERITY. The
#                            adapter overwrites it with the panel's decision before
#                            qar_library_init; this keeps the launcher honest as well. At
#                            TRACE the mixer writes about 70 MB in two minutes at 60 FPS
#                            (several lines per frame per topic).
#   QAR_PANEL_LOG_SEVERITY   Our in-process library init (qar_streaming_panel.cpp). Unset
#                            falls back to QAR_LOG_SEVERITY, then the factory default,
#                            TRACE, with console logging into rviz_live_*.out.log - which
#                            is why quiet mode must set it.
#   QAR_PANEL_LOG_CONSOLE    on|off: whether the in-process library also echoes every log
#                            line to RViz's stdout (rviz_live_*.out.log). Unset means on
#                            for trace/debug/info and off for warn and above; quiet mode
#                            states it so the run's log says what it asked for.
#   RUST_LOG, QAR_ZENOH_LOG  zenoh (the QAR transport) is Rust and honours RUST_LOG; its
#                            own diagnostics are the only place that says WHY a session
#                            fails to come up (qar_runtime_create_session surfaces just
#                            "MsgInternalError -4 [ZenohMessaging.cpp:340]", zenoh says
#                            "Unable to open listener ..." / "Unable to bind UDP port
#                            ..."). The Visualizer/mixer processes inherit RUST_LOG and
#                            start zenoh's logger themselves; RViz does not, so
#                            QAR_ZENOH_LOG makes the adapter call zenohc's
#                            zc_init_log_from_env_or() in-process (its value is the
#                            tracing filter).
#   QAR_ADAPTER_TRACE        Adapter breadcrumbs on stderr (run log below): peer ids, the
#                            WGL context latch, timed lines around the main C-API calls, and
#                            the per-frame submit steps (every frame up to #150, then 1 in 300).
#   QAR_ADAPTER_TRACE_ALL    Defeats that sampling: every frame, four flushed writes each.
#   QAR_ADAPTER_FRAME_TRACE  One CSV row per submitted frame (frame-timing diagnostics).
# The last three are PRESENCE flags: qar_capi_render_sender_adapter.cpp / env_config.cpp
# test getenv() != nullptr, so setting them to "0" would switch them ON. "Off" therefore
# means REMOVED from the environment, never "0".
#
# Kind says what "loud" means for a variable (Test-LoudLogValue below): 'severity' is loud
# at trace/debug, 'filter' (RUST_LOG) when it names trace/debug anywhere, 'flag' when it is
# set at all (QAR_ZENOH_LOG starts zenoh logging for any value; the adapter flags test mere
# presence), 'plain' never.
$logEnv = @(
    @{ Name = 'QAR_LOG_SEVERITY';        Quiet = 'warn'; Trace = 'trace'; Kind = 'severity' },
    @{ Name = 'QAR_PANEL_LOG_SEVERITY';  Quiet = 'warn'; Trace = $null;   Kind = 'severity' },
    @{ Name = 'QAR_PANEL_LOG_CONSOLE';   Quiet = 'off';  Trace = $null;   Kind = 'plain' },
    @{ Name = 'RUST_LOG';                Quiet = $null;  Trace = 'zenoh=debug'; Kind = 'filter' },
    @{ Name = 'QAR_ZENOH_LOG';           Quiet = $null;  Trace = 'debug'; Kind = 'flag' },
    @{ Name = 'QAR_ADAPTER_TRACE';       Quiet = $null;  Trace = '1';   Presence = $true; Kind = 'flag' },
    @{ Name = 'QAR_ADAPTER_TRACE_ALL';   Quiet = $null;  Trace = $null; Presence = $true; Kind = 'flag' },
    @{ Name = 'QAR_ADAPTER_FRAME_TRACE'; Quiet = $null;  Trace = $null; Presence = $true; Kind = 'flag' }
)
function Test-LoudLogValue {
    param($Var, [string]$Value)
    if ([string]::IsNullOrEmpty($Value)) { return $false }
    switch ($Var.Kind) {
        'severity' { return ($Value.Trim() -match '^(?i)(trace|debug)$') }
        'filter'   { return ($Value -match '(?i)(trace|debug)') }
        'flag'     { return $true }
        default    { return $false }
    }
}
# DDS transport profile for the 6 MB raw video frames (scripts\fastdds_large_data.xml; see
# .PARAMETER NoDdsProfile). Fast-DDS 2.6's default shared-memory segment is 512 KB, so a raw
# 1080p frame (~96 fragments) drops fragments and every drop is a reliable-repair round trip:
# jitter and stalls on every subscriber (measured offline: stamp->arrival p95 32 ms vs 15 ms
# with the profile, best-effort readers missing 5-8% of the frames vs ~0%). It is exported
# for everything started below (rviz2 and the marker publisher inherit it; the video
# publisher applies the same file to itself) and put back in the shell afterwards, like every
# other variable. A value already in the shell wins: it may be a profile of your own.
$ddsProfileFile = Join-Path $PSScriptRoot "fastdds_large_data.xml"
$ddsShellValue = $envSnapshot['FASTRTPS_DEFAULT_PROFILES_FILE']
if ($NoDdsProfile) {
    $ddsProfileNote = if ($ddsShellValue) { "$ddsShellValue (from your shell; -NoDdsProfile, nothing set by this launcher)" } else { "(unset: -NoDdsProfile, Fast-DDS defaults)" }
} elseif (-not [string]::IsNullOrEmpty($ddsShellValue)) {
    $ddsProfileNote = "$ddsShellValue (from your shell - yours wins)"
} elseif (Test-Path $ddsProfileFile) {
    [Environment]::SetEnvironmentVariable('FASTRTPS_DEFAULT_PROFILES_FILE', $ddsProfileFile)
    $ddsProfileNote = "$ddsProfileFile (set by this launcher)"
} else {
    $ddsProfileNote = "(unset: $ddsProfileFile NOT FOUND, Fast-DDS defaults)"
}
# Logging variables: a QUIETER value already in the shell always wins; a LOUD value in quiet
# mode is cleared (see -KeepLogEnv); every other variable is set to the mode's own value.
$logEnvKept = @()
$logEnvCleared = @()
foreach ($v in $logEnv) {
    $current = [Environment]::GetEnvironmentVariable($v.Name)
    if (-not [string]::IsNullOrEmpty($current)) {
        # A QUIETER value set in the shell always wins. A LOUD one in quiet mode is cleared:
        # kept, it silently defeats quiet mode (the QarOS library obeys QAR_LOG_SEVERITY over
        # the panel's setting; QAR_ZENOH_LOG switches zenoh debug on inside RViz). Adapter
        # presence flags spelled "off"/"0" are cleared too, because the adapter reads mere
        # presence as ON. -KeepLogEnv opts out; -TraceLogs asks for the loud values anyway.
        $loud = Test-LoudLogValue -Var $v -Value $current
        if (-not $TraceLogs -and -not $KeepLogEnv -and
            ($loud -or ($v.Presence -and $current -match '^(0|false|off|no)$'))) {
            [Environment]::SetEnvironmentVariable($v.Name, $null)
            $logEnvCleared += "$($v.Name)=$current"
        } else {
            $logEnvKept += "$($v.Name)=$current"
            continue
        }
        # cleared: fall through to the mode's own value below (quiet sets 'warn' etc.)
    }
    $want = if ($TraceLogs) { $v.Trace } else { $v.Quiet }
    [Environment]::SetEnvironmentVariable($v.Name, $want)   # $null removes the variable
}
# The adapter resolves the in-process library severity as QAR_PANEL_LOG_SEVERITY, then
# QAR_LOG_SEVERITY. In quiet mode this script supplies 'warn' for the former, which would beat a
# QAR_LOG_SEVERITY that was kept on purpose (say 'error') - so mirror that value instead and
# "your environment wins" stays true for the library inside RViz as well.
if (-not $TraceLogs -and
    @($logEnvKept | Where-Object { $_ -like 'QAR_LOG_SEVERITY=*' }).Count -gt 0 -and
    @($logEnvKept | Where-Object { $_ -like 'QAR_PANEL_LOG_SEVERITY=*' }).Count -eq 0) {
    [Environment]::SetEnvironmentVariable('QAR_PANEL_LOG_SEVERITY',
        [Environment]::GetEnvironmentVariable('QAR_LOG_SEVERITY'))
}
# -TimingCsv: a fresh folder per run under the log folder. The plugin creates it lazily
# (on its background writer thread) and nothing is written without this variable. Created
# here too on -Confirm, so the path printed below exists even if no video frame ever flows.
$timingDir = $null
if ($TimingCsv) {
    $timingDir = Join-Path $LogDir ("timing\" + $stamp)
    [Environment]::SetEnvironmentVariable('QAR_VIDEO_TIMING_DIR', $timingDir)
}
# QAR log directory: the library's own default, or where the panel was told to write.
$qarLogDir = if ($env:QAR_PANEL_LOG_DIR) { $env:QAR_PANEL_LOG_DIR } else { Join-Path $env:TEMP "quaternar" }
# Pose: QAR hands us head poses in ITS world, which is Y-up (OpenGL/AR); RViz/Ogre
# render a Z-up ROS world (REP-103). Without this the viewer arrives on its side,
# looking straight down at the scene.
# ORIENTATION ONLY: the viewer stays at XYZ = 0 so QAR's reported position and ours
# agree (the SDK has no set-initial-pose API to tell QAR about an offset).
# *_YAW_DEG / *_HEIGHT_M / *_BACK_M remain available for experimentation and are
# tunable in the shell without a rebuild.
# NOTE: 'zup' is not the library default because the capture paths and the
# pose-conversion unit tests assert the documented identity mapping.
$env:QAR_POSE_WORLD  = "zup"
if (-not $env:QAR_POSE_YAW_DEG)  { $env:QAR_POSE_YAW_DEG  = "180" }
if (-not $env:QAR_POSE_HEIGHT_M) { $env:QAR_POSE_HEIGHT_M = "0" }
if (-not $env:QAR_POSE_BACK_M)   { $env:QAR_POSE_BACK_M   = "0" }

# Depth encoding. The QarOS Visualizer expects the raw window-space z-buffer value in
# [0,1] and inverts it itself using near/far - NOT metric linear metres. With metric
# data it shows nothing up close, all colours crammed into a hairline band, then one
# saturated value; and it ignores both QarRenderFrameShow.depth_scale and
# rendered_near_far, so no producer-side linear scaling can compensate.
#
# far also matters: NDC depth is reciprocal, so a 500 m far plane squeezes a
# room-sized scene into the first per-cent of the range (everything reads "near") and
# wastes almost all depth precision.
# NOT the library default: the capture scripts still assert the documented metric
# encoding.
#
# near/far are not forced here: the panel itself defaults to 0.01 m / 150 m, sized for
# real sensor data. Setting QAR_PANEL_NEAR_M / QAR_PANEL_FAR_M in the shell still
# overrides both, which is how depth experiments are run.
if (-not $env:QAR_DEPTH_ENCODING) { $env:QAR_DEPTH_ENCODING = "ndc" }

# Scene lighting. RViz's ONLY light is a directional "MainDirectional" that
# VisualizationManager re-aims at the LOCAL view camera every frame - a headlight. Shared
# with remote peers that is simply wrong: their shading pivots around whenever the RViz
# operator orbits their own camera. The panel adds a world-fixed OMNI (point) light +
# raised ambient while streaming, and hides RViz's headlight so it stops swimming; both
# are restored when streaming stops.
#   QAR_SCENE_LIGHT            off | omni | per-peer | both (default)
#     omni     = raised ambient + FOUR directional lights on a tetrahedron, so every
#                surface normal faces at least one light and nothing is unlit, with
#                the two "from above" directions carrying the key. Directional lights
#                are position-independent, so this lights the world evenly however
#                far a peer stands from the origin.
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
#   QAR_PANEL_MAX_PEERS         ceiling on concurrent streams (default 8).
#   QAR_PANEL_PEER_ADMISSION    invited (default) | new | named | any
#     The peer subscription surfaces the runtime's INTERNAL app peers too (one invited
#     Visualizer shows up as several distinct ids), and building a sender for one of
#     those mismatches app ids, so the Visualizer's own frames get rejected.
#       invited : only ids we invited ourselves. Safe, and correct for the local
#                 Visualizer - but it REFUSES real onboarded peers, which never go
#                 through qar_session_invite_peer. Use -RealPeers for those.
#       new     : invited peers, plus anything that appears after the first few
#                 seconds (QAR_PANEL_PEER_SETTLE_MS, default 5000). The runtime's own
#                 peers are all up by then; a human onboarding a headset is not.
#                 This is the policy for a real HoloLens/Quest/remote-visualizer demo.
#       named   : + display-name match (QAR_PANEL_PEER_NAME_FILTER). Unusable in
#                 practice: names are auto-generated AND change between updates.
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

Write-Host "=== RViz -> QarOS Visualizer live stream ===" -ForegroundColor Magenta
Write-Host "  scene            : $Scene$(if (Test-Path $Scene) { '' } else { '  (NOT FOUND)' })"
Write-Host "  QarOS SDK bin    : $sdkBin"
Write-Host "  Visualizer exe   : $vizExe"
Write-Host "  runtime-launcher : $launcherExe"
Write-Host "  QAR_RUNTIME_BINARIES_DIR = $env:QAR_RUNTIME_BINARIES_DIR"
Write-Host "  QAR_PANEL_LIVE_STREAM    = 1"
Write-Host "  FASTRTPS_DEFAULT_PROFILES_FILE = $ddsProfileNote"
Write-Host "  QAR_SCENE_LIGHT          = $env:QAR_SCENE_LIGHT (headlight: $(if ($env:QAR_SCENE_LIGHT_HEADLIGHT -eq 'keep') {'kept'} else {'suppressed while streaming'}))"
Write-Host "  QAR_PANEL_VISUALIZERS    = $env:QAR_PANEL_VISUALIZERS (admission: $env:QAR_PANEL_PEER_ADMISSION)"
Write-Host "  QAR_PANEL_INVITE_PEERS   = $(if ($env:QAR_PANEL_INVITE_PEERS) { $env:QAR_PANEL_INVITE_PEERS } else { '(none - invite from the panel, or pass -InvitePeers)' })"
Write-Host "  install prefix           : $Prefix (plugin overlay: $(if ($overlayFound) { 'found' } else { 'NOT FOUND' }))"
Write-Host "  RViz logs (-LogDir)      : $LogDir"
Write-Host "  rviz2.exe                : $(if ($rvizExe) { $rvizExe } else { '(not found on PATH)' })"
$gpuNow = if ($rvizExe -and (Test-Path $gpuPrefKey)) { (Get-ItemProperty -Path $gpuPrefKey -Name $rvizExe -ErrorAction SilentlyContinue).$rvizExe } else { $null }
Write-Host "  GPU preference (HKCU)    : $(if ($gpuNow) { $gpuNow } else { '(not set)' }) - with -Confirm the launcher sets High-performance (NVIDIA) for rviz2.exe"
if ($TraceLogs) {
    Write-Host "  log mode                 : TRACE (-TraceLogs) - QarOS logs at trace, zenoh debug, adapter traces on; GB-sized logs whose I/O can stall streams, so not for latency work." -ForegroundColor Yellow
} else {
    Write-Host "  log mode                 : QUIET (default) - QarOS logs at warn, no zenoh logging, no per-frame traces; -TraceLogs collects full logs for a vendor report." -ForegroundColor Green
}
if ($logEnvCleared.Count -gt 0) {
    Write-Host "  CLEARED (quiet mode)     : $($logEnvCleared -join ', ')" -ForegroundColor Yellow
    Write-Host "                             these were already in this shell - a loud value silently defeats quiet mode. -TraceLogs asks for them on purpose, -KeepLogEnv keeps yours." -ForegroundColor Yellow
}
if ($logEnvKept.Count -gt 0) {
    Write-Host "                             your environment wins for: $($logEnvKept -join ', ')" -ForegroundColor Yellow
}
foreach ($v in $logEnv) {
    $shown = [Environment]::GetEnvironmentVariable($v.Name)
    Write-Host ("  {0,-24} = {1}" -f $v.Name, $(if ($shown) { $shown } else { '(unset)' }))
}
if ($timingDir -and $TraceLogs) {
    Write-Warning "-TimingCsv together with -TraceLogs: the trace logging is itself heavy I/O and can cause the very stalls being measured. Use -TimingCsv alone to judge them."
}
if ($timingDir) {
    Write-Host "  timing CSVs (-TimingCsv) : $timingDir" -ForegroundColor Cyan
    Write-Host "                             QAR_VIDEO_TIMING_DIR is set; the plugin writes video_*.csv, rx_*.csv, render3d*.csv and gui_gaps.csv there (~1 flush/s, off the GUI thread)."
    Write-Host "                             publisher : python scripts\publish_video_file.py <clip> --ros-args -p timing_csv:=$timingDir\publisher.csv"
    Write-Host "                             analyse   : python scripts\analyze_video_timing.py `"$timingDir`" --publisher `"$timingDir\publisher.csv`""
} elseif ($env:QAR_VIDEO_TIMING_DIR) {
    Write-Host "  QAR_VIDEO_TIMING_DIR     = $env:QAR_VIDEO_TIMING_DIR   (from your shell; -TimingCsv would use a fresh folder)" -ForegroundColor Yellow
}

if (-not (Test-Path $vizExe))       { Write-Warning "Visualizer exe not found at $vizExe" }
if (-not (Test-Path $launcherExe))  { Write-Warning "runtime-launcher exe not found at $launcherExe" }

# --- Startup environment checks: WARN only, never block -----------------------------
# A stall that hits every stream at once is rarely QarOS's own: a nearly full system
# drive under real-time antivirus, another process using the GPU, or no commit charge
# left all freeze the Visualizer, the mixer and the launcher together.
# Saying so up front keeps such a run from being mistaken for a plugin bug.
function Show-StartupEnvironmentWarnings {
    param([string[]]$Folders)
    $warned = 0

    # 1. Free space on the drive(s) the logs and temp files land on.
    $roots = @()
    foreach ($folder in $Folders) {
        try { $roots += [System.IO.Path]::GetPathRoot([System.IO.Path]::GetFullPath($folder)) } catch { }
    }
    foreach ($root in @($roots | Where-Object { $_ } | Select-Object -Unique)) {
        try {
            $drive = New-Object System.IO.DriveInfo $root
            $freeGB = $drive.AvailableFreeSpace / 1GB
            $freePct = 100.0 * $drive.AvailableFreeSpace / $drive.TotalSize
            if ($freePct -lt 10 -or $freeGB -lt 15) {
                Write-Warning ("Drive {0} is nearly full: {1:N1} GB free ({2:N1}%). Logs and temp files land there, and a nearly full drive under real-time antivirus scanning can stall writes - and every QarOS stream with them. Free some space, or point QAR_PANEL_LOG_DIR at a roomier drive." -f $root, $freeGB, $freePct)
                $warned++
            }
        } catch { }
    }

    # 2. Other processes using the NVIDIA GPU for compute (a CUDA job, say). Run with a
    #    timeout: nvidia-smi can hang on a wedged driver, and this check must not block.
    $nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if ($nvidiaSmi) {
        try {
            $psi = New-Object System.Diagnostics.ProcessStartInfo
            $psi.FileName = $nvidiaSmi.Source
            $psi.Arguments = '--query-compute-apps=pid,process_name,used_memory --format=csv,noheader'
            $psi.UseShellExecute = $false
            $psi.CreateNoWindow = $true
            $psi.RedirectStandardOutput = $true
            $psi.RedirectStandardError = $true
            $smi = [System.Diagnostics.Process]::Start($psi)
            $smiOut = $smi.StandardOutput.ReadToEndAsync()
            $smiErr = $smi.StandardError.ReadToEndAsync()   # drained so a full pipe cannot deadlock it
            if ($smi.WaitForExit(10000)) {
                $apps = @()
                if ($smi.ExitCode -eq 0) {
                    $apps = @($smiOut.Result -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ })
                }
                if ($apps.Count -gt 0) {
                    Write-Warning ("{0} other process(es) are using the NVIDIA GPU for compute (pid, name, GPU memory):`n    {1}`n  They compete with the Visualizer and the mixer for the GPU and can stall every stream together. Stop them before judging stalls or latency." -f $apps.Count, ($apps -join "`n    "))
                    $warned++
                }
            } else {
                try { $smi.Kill() } catch { }
                Write-Warning "nvidia-smi did not answer within 10 s; GPU compute users not checked (a wedged NVIDIA driver can stall the Visualizer too)."
                $warned++
            }
        } catch { }
    }

    # 3. Commit charge (RAM + page file) nearly exhausted. FreeVirtualMemory is in KB.
    try {
        $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        $freeCommitGB = $os.FreeVirtualMemory / 1MB
        if ($freeCommitGB -lt 4) {
            Write-Warning ("Only {0:N1} GB of commit charge is left (limit {1:N1} GB = RAM + page file). When it runs out allocations fail and everything stalls. Close memory-hungry applications or enlarge the page file." -f $freeCommitGB, ($os.TotalVirtualMemorySize / 1MB))
            $warned++
        }
    } catch { }

    if ($warned -eq 0) {
        Write-Host "  startup checks           : OK (drive space, GPU compute users, commit charge)"
    }
}

# --- Bounded QarOS log files ------------------------------------------------------
# The launcher and the mixers APPEND to <log dir>\*.log across runs (a launcher stdout log
# can grow to 1 GB), and the new run's writes land on top of that. Anything over the
# limit is renamed to <name>.prev - replacing an older .prev - so the run starts a fresh,
# small file. Without -Apply it only REPORTS what would be rotated (the dry run).
function Invoke-QarLogRotation {
    param(
        [Parameter(Mandatory)][string]$LogDir,
        [switch]$Apply,
        [long]$LimitBytes = 256MB
    )
    $limitMB = [int]($LimitBytes / 1MB)
    if (-not (Test-Path -LiteralPath $LogDir -PathType Container)) {
        Write-Host "  QAR log dir              : $LogDir (not created yet)"
        return
    }
    $big = @(Get-ChildItem -LiteralPath $LogDir -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -ieq '.log' -and $_.Length -gt $LimitBytes })
    if ($big.Count -eq 0) {
        Write-Host "  QAR log dir              : $LogDir (no *.log over $limitMB MB)"
        return
    }
    Write-Host "  QAR log dir              : $LogDir ($($big.Count) *.log over $limitMB MB)"
    foreach ($f in $big) {
        $prev = $f.FullName + '.prev'
        $size = '{0:N0} MB' -f ($f.Length / 1MB)
        $note = if (Test-Path -LiteralPath $prev) { ' (replacing an older .prev)' } else { '' }
        if (-not $Apply) {
            Write-Host "    would rotate : $($f.Name) ($size) -> $($f.Name).prev$note"
            continue
        }
        try {
            Move-Item -LiteralPath $f.FullName -Destination $prev -Force -ErrorAction Stop
            Write-Host "    rotated      : $($f.Name) ($size) -> $($f.Name).prev$note" -ForegroundColor Yellow
        } catch {
            Write-Warning "could not rotate $($f.FullName): $($_.Exception.Message) (a QarOS process from an earlier run may still hold it open; the run will append to it)"
        }
    }
}

Show-StartupEnvironmentWarnings -Folders @($env:TEMP, $qarLogDir, $LogDir)
Invoke-QarLogRotation -LogDir $qarLogDir -Apply:$Confirm
$vcRuntimeShown = if ($vcRuntimeVersion) { "$vcRuntimeVersion ($(if ($vcRuntimeOld) { 'TOO OLD' } else { 'ok' }), needs $vcRuntimeMinimum or newer)" } else { "(version not readable)" }
if ($vcRuntimeWarning) { Write-Host "  VC++ runtime (rviz2 dir) : $vcRuntimeShown" -ForegroundColor Yellow } else { Write-Host "  VC++ runtime (rviz2 dir) : $vcRuntimeShown" }
if ($vcRuntimeWarning) { Write-Warning $vcRuntimeWarning }   # an unreadable file; a dry run also gets here with an old runtime (-Confirm stopped above)
$tinyxmlShown = if ($tinyxmlWarning) { "MISSING - run fix_missing_tinyxml_dll.ps1" } else { "present" }
if ($tinyxmlWarning) { Write-Host "  tinyxml.dll (rviz2 dir)  : $tinyxmlShown" -ForegroundColor Yellow } else { Write-Host "  tinyxml.dll (rviz2 dir)  : $tinyxmlShown" }
if ($tinyxmlWarning) { Write-Warning $tinyxmlWarning }   # only a dry run gets here with the file missing (-Confirm stopped above)

if (-not $Confirm) {
    Write-Host ""
    Write-Host "DRY RUN (no -Confirm): environment resolved above; NOT launching any GUI (log rotation is only reported, not performed; the registry, running processes and Fast-DDS shared memory are left alone)." -ForegroundColor Yellow
    Write-Host "To actually run the interactive session, re-invoke with -Confirm (opens windows)." -ForegroundColor Yellow
    return
}


# ---------------------------------------------------------------------------
# LAUNCH (only with -Confirm).
#
# Launch rviz2 ONLY. The QAR library starts and configures qar-runtime-launcher.exe itself
# (from launcher_config_json + the runtime binaries folder) and then spawns
# qar-streaming-viz.exe in response to invite_peer. Starting the launcher by hand competes
# with it and breaks peer/consumer id coordination.
# ---------------------------------------------------------------------------

# --- Live scene content --------------------------------------------------------
# scene3_grid_marker.rviz subscribes to the test markers; without a publisher RViz
# shows nothing but the grid, and the stream would carry an empty scene on BOTH ends.
# Reap publishers leaked by earlier runs FIRST. This is not tidiness - it is load-
# bearing. Every publisher is a ROS 2 node, and on domain 0 DDS assigns each participant the next
# UDP port pair from 7410 upwards (from 7410 + 250 x domain number on any other domain). Leaked
# publishers accumulate, the ladder climbs, and once it reaches 7446, which only domain 0 can, it
# collides with zenoh's multicast scouting port: QAR then fails with the opaque "MsgInternalError
# Msg: -4 [ZenohMessaging.cpp:340]" and no session can be created at all. A publisher is a Python
# process whose command line names one of the four test publisher scripts of this folder as a
# whole file name, whatever folder it was started from: one that you started yourself in another
# window is ended as well. A Python process that has only the text publish_test_ somewhere in its
# command line is left alone.
function Test-QarTestPublisher([string]$CommandLine) {
    $name = '(?<![\w.-])publish_test_(?:markers|tf|robot|pointcloud)\.py(?![\w.-])'
    return [bool]($CommandLine -match $name)
}
$stale = Get-CimInstance Win32_Process -Filter "Name='python.exe'" -ErrorAction SilentlyContinue |
    Where-Object { Test-QarTestPublisher $_.CommandLine }
if ($stale) {
    foreach ($s in $stale) { Stop-Process -Id $s.ProcessId -Force -ErrorAction SilentlyContinue }
    Write-Host ("  reaped {0} leaked test publisher(s) from earlier runs" -f @($stale).Count) -ForegroundColor Yellow
    Start-Sleep -Milliseconds 500
}

$pyExe = (Get-Command python -ErrorAction SilentlyContinue).Source

# The forced reap above, or a crashed rviz2, leaves Fast-DDS shared-memory segments
# behind. Those zombies can wedge large-message delivery on the domain (a 6 MB video topic
# delivering nothing on domain 0 while another domain runs normally).
# `shm clean` removes only segments whose owner process is dead.
if ($pyExe -and $rvizExe) {
    $fastddsCli = Join-Path (Split-Path (Split-Path $rvizExe)) "tools\fastdds\fastdds.py"
    if (Test-Path $fastddsCli) {
        $cleaned = & $pyExe $fastddsCli shm clean 2>&1 | Select-String -Pattern "zombie"
        Write-Host ("  Fast-DDS shm clean: " + (($cleaned | ForEach-Object { $_.Line.Trim() }) -join ", "))
    }
}

if ($pyExe) {
    $pub = Start-Process -FilePath $pyExe `
        -ArgumentList ('"' + (Join-Path $PSScriptRoot "publish_test_markers.py") + '"') `
        -PassThru -WindowStyle Hidden
    Write-Host "marker publisher PID=$($pub.Id) (publish_test_markers.py, frame 'map')" -ForegroundColor Green
    Write-Host "  stopped automatically by the next run; or: Stop-Process -Id $($pub.Id)"
} else {
    Write-Warning "python not found - markers will NOT be published and the scene will be empty."
}

# Guard: zenoh scouting needs UDP 7446. If anything holds it, QAR fails to create a
# session with an error that names neither the port nor the culprit - say so up front.
$busy7446 = Get-NetUDPEndpoint -LocalPort 7446 -ErrorAction SilentlyContinue
if ($busy7446) {
    foreach ($b in $busy7446) {
        $owner = (Get-Process -Id $b.OwningProcess -ErrorAction SilentlyContinue).ProcessName
        Write-Warning ("UDP 7446 (zenoh scouting) is held by pid $($b.OwningProcess) ($owner). " +
            "QAR session creation will fail with 'MsgInternalError Msg: -4'.")
    }
}

Write-Host "=== launching rviz2 (live-stream producer) ===" -ForegroundColor Magenta
# Capture stdout/stderr to files: the adapter trace + any SEH/crash breadcrumb must
# survive the window disappearing if RViz dies.
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
if ($timingDir) {
    New-Item -ItemType Directory -Force -Path $timingDir | Out-Null
    Write-Host "timing CSVs : $timingDir (QAR_VIDEO_TIMING_DIR)" -ForegroundColor Green
}
$outLog = Join-Path $LogDir "rviz_live_$stamp.out.log"
$errLog = Join-Path $LogDir "rviz_live_$stamp.err.log"
$rviz = Start-Process -FilePath "rviz2.exe" -ArgumentList @("-d", ('"' + $Scene + '"')) -PassThru `
    -RedirectStandardOutput $outLog -RedirectStandardError $errLog
Write-Host "rviz2 PID=$($rviz.Id)."
Write-Host ""
Write-Host "In the RViz 'Qar Streaming' panel click 'Connect to QarOS' - the QAR runtime"
Write-Host "then starts its launcher and the Visualizer window appears on its own."
Write-Host ""
Write-Host "  RViz stdout : $outLog" -ForegroundColor Cyan
Write-Host "  RViz stderr : $errLog   <- [qar-panel] HB + AR VIDEO lines$(if ($env:QAR_ADAPTER_TRACE) { ', adapter trace' })" -ForegroundColor Cyan
Write-Host "  QAR logs    : $qarLogDir   (QAR_LOG_SEVERITY=$(if ($env:QAR_LOG_SEVERITY) { $env:QAR_LOG_SEVERITY } else { '(unset)' }); $(if ($TraceLogs) { 'TRACE mode' } else { 'quiet mode, -TraceLogs for full logs' }))" -ForegroundColor Cyan
if ($timingDir) {
    Write-Host "  Timing CSVs : $timingDir" -ForegroundColor Cyan
    Write-Host "     start the publisher with  --ros-args -p timing_csv:=$timingDir\publisher.csv"
    Write-Host "     after the run:            python scripts\analyze_video_timing.py `"$timingDir`" --publisher `"$timingDir\publisher.csv`""
}
} finally {
    # Put the calling shell's environment back (see $envSnapshot). rviz2 and the marker
    # publisher were started with their own copy of the environment above; the dry run and
    # a failed start get here too.
    Restore-CallerEnvironment
}
