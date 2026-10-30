# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT

<#
.SYNOPSIS
    Streams the LIVE RViz scene to the targets of the installed QAROS Hub (a headset, or a QAROS
    Visualizer window) with the RViz2 plugin. LAUNCHES A GUI WINDOW (rviz2); without -Confirm it
    only reads the machine, prints the resolved configuration and what you will have to do, and
    starts nothing.

.DESCRIPTION
    The producer is RViz2 loading the QarStreamingPanel in QAR_PANEL_LIVE_STREAM mode (the plugin
    built by build_plugin.ps1). The panel joins the QAROS Hub when you press Connect in its
    Connection tab. The plugin loads the installed QAROS library itself (qar_library_load(NULL):
    the installation that the registry names, or the folder of QAROS_RUNTIME_DIR) and joins one of
    two Hubs:
      - local: the Hub of this PC. QAROS asks once, in its own window, whether rviz2 may join
        (click Allow). If no Hub is running, the plugin starts QAROS when you press Connect.
      - remote: the Hub of another PC. You type its host, its onboarding port and the pairing code
        that its Visualizer shows; the pairing is stored for the next start.
    -HubMode auto (the default) takes remote when a pairing with another Hub is stored, else local
    when QAROS is installed on this PC. The script starts neither the Hub nor a Visualizer: open a
    Visualizer window from QAROS yourself, and the panel streams to every target that joins.

    !!! WITHOUT -Confirm NOTHING IS STARTED, CHANGED OR STOPPED: no window, no registry write,
        no process stop, no log rotation, no Fast-DDS shared-memory clean-up. The script prints
        the resolved locations and environment and ends with exit code 0. The environment of
        the calling shell is put back when the script ends, in both modes.
        With -Confirm the script starts rviz2, writes the GPU preference of rviz2.exe to the
        registry (HKCU), stops test publishers left over from earlier runs (the Python
        processes that run publish_test_markers.py, publish_test_tf.py, publish_test_robot.py
        or publish_test_pointcloud.py, also when you started them in another window) and
        cleans up Fast-DDS shared memory of dead processes.

    PATH: nothing is added to it. The plugin loads the installed QAROS library by its full path with
    flags that take the library's own dependencies from its folder, so the QAROS folder is not
    needed on PATH, and a folder of another QarOS version on PATH could not join this Hub anyway.

    LOGGING IS QUIET BY DEFAULT: a low-I/O run. QarOS logs at `info` (QAR_LOG_SEVERITY for the
    runtime/launcher/mixer, QAR_PANEL_LOG_SEVERITY for our in-process library;
    QAR_PANEL_LOG_CONSOLE=off keeps that library from also echoing every line into
    rviz_live_*.out.log), there is no zenoh logging, and none of the per-frame adapter traces
    (QAR_ADAPTER_TRACE / _TRACE_ALL / _FRAME_TRACE) is on. Our own 2-second "HB" line and the
    "AR VIDEO:" summaries stay on - they are cheap. Pass -TraceLogs for the full TRACE set a
    bug report needs. A quieter value you set yourself in the shell always wins; a LOUD one
    (trace/debug severity, zenoh or adapter tracing) is cleared in quiet mode unless
    -KeepLogEnv is given, because a leftover loud value defeats quiet mode without a sign.
    The mode is printed as "log mode".

    START POSITION: the RViz it starts gets QAR_PANEL_FIXED_POSES=ground (the panels with a
    fixed place hang from the RViz ground) and QAR_PANEL_VOLUME_Y=-1.6 (the room height of the
    centre of the app volume, in metres) when the variable is not set in the shell. A value in
    the shell wins. The plan prints each value and where it comes from: "launcher default" or
    "environment".

    Before launching (-Confirm), every *.log in the QAR log directory over 256 MB
    ($env:QAR_PANEL_LOG_DIR, default %TEMP%\quaternar) is renamed to <name>.prev,
    replacing an older .prev, so a run never appends to a GB-sized file. The dry run only
    reports what it would rotate.

    The library of QarOS inside RViz writes its log to <QAR log dir>\qar_streaming.log and
    starts that file afresh in every session. When rviz2 has exited, the launcher copies it to
    <LogDir>\rviz_live_<stamp>.qar_streaming.log, next to the RViz logs of the run. With
    -Confirm the launcher therefore stays running until rviz2 exits (Ctrl+C stops the wait and
    saves the log as it is). A file that is older than the run is not copied, and a failed copy
    is only a warning. The dry run prints the name of the copy.

    The processes that QAROS starts write their logs to %TEMP%\quaternar\logs: the launcher to
    qar-launcher-default\<session id>\ (one folder per session) and the Hub service to
    qaros-hub-service\, whose crash reports are in qaros-hub-service\crashes (QAROS empties that
    folder when the Hub restarts). A process of this plugin's run that crashes leaves a *.crash.txt
    report in %TEMP%\quaternar\crashes. These files are not started afresh for the next run, so at
    the same moment the launcher copies the *.log files of the newest launcher session folder that
    were written at or after the start of the run to <LogDir>\rviz_live_<stamp>.launcher_<name>,
    and every crash report of the three folders to <LogDir>\rviz_live_<stamp>.<name of the crash
    report>, and prints one line per copy. The .dmp files are not copied. A missing folder or file
    is only a note, and a failed copy only a warning. The dry run prints the plan of these copies.

    The script also WARNS (it never blocks) about machine-level causes of stalls that
    hit every stream at once: a %TEMP% drive with under 10% or under 15 GB free, other
    processes using the NVIDIA GPU for compute (nvidia-smi), and less than ~4 GB of
    commit charge left (Win32_OperatingSystem.FreeVirtualMemory). The plan names the memory
    use of the GPU (nvidia-smi --query-gpu), and with -Confirm the results of all these
    checks are written next to the RViz logs, to rviz_live_<stamp>.checks.txt (the stamp of
    the .out.log and .err.log of the run), so that a run whose scene came out black can be
    compared with what else was using the GPU at its start.

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

.PARAMETER SdkRoot       Folder of the installed QAROS (the folder with bin\qar-streaming-c.dll).
                         Default: the InstallFolder that the QAROS installer records in
                         HKLM\SOFTWARE\Quaternar\QAROS, else the environment variable QAR_SDK_ROOT.
                         Used for the checks and the plan only: the plugin loads the installed QAROS
                         itself. Optional: without an installation only a Hub of another PC can be
                         joined.
.PARAMETER Prefix        Install prefix that holds the plugin (the folder with
                         local_setup.ps1). Default: the environment variable QAR_PREFIX.
                         Required. Without -Confirm a missing overlay is only reported.
.PARAMETER QtRoot        Qt 5.15.2 msvc2019_64 folder. Default: the environment variable
                         QAR_QT_ROOT. Required.
.PARAMETER Ros2Root      ROS 2 Humble distribution root. Default: the environment variable
                         ROS2_INSTALL_ROOT. Required.
.PARAMETER Scene         RViz config. Default: scene3_grid_marker.rviz from
                         <Prefix>\share\qar_rviz_streaming\config\test_scenes.
.PARAMETER InvitePeers   Devices to invite as soon as the panel has joined the Hub of this PC, as
                         `kind:hostname` entries. Kinds: hololens, headset. Both need a hostname.
                         Example:
                           -InvitePeers hololens:HOLOLENS-A2B3,headset:HEADSET-C4D5
                         Only the Hub of this PC (local mode) invites devices; a Hub of another PC
                         refuses, and the panel greys the invites out. A device that fails to
                         invite is logged and skipped, never fatal. Exported as
                         QAR_PANEL_INVITE_PEERS.
.PARAMETER HubMode       Which Hub the panel joins: auto (a stored pairing with another Hub, else the
                         installed QAROS of this PC), local (this PC) or remote (another PC).
                         Exported as QAR_PANEL_HUB_MODE, which wins over the panel setting "Hub
                         Connection". Without the parameter the variable is left as it is.
.PARAMETER HubHost       Host name or address of the Hub of another PC, prefilled in the panel
                         (QAR_PANEL_HUB_HOST). The pairing code is never a parameter: type it in
                         the panel.
.PARAMETER HubPort       Onboarding port of that Hub, prefilled in the panel (QAR_PANEL_HUB_PORT).
                         The Visualizer of that PC shows it with the pairing code.
.PARAMETER RuntimeDir    Folder that holds another QAROS runtime for the library lookup. Exported as
                         QAROS_RUNTIME_DIR. Not set unless you pass it.
.PARAMETER TraceLogs     Collect FULL logs (what a bug report needs): QAR_LOG_SEVERITY=trace,
                         zenoh at debug (RUST_LOG + QAR_ZENOH_LOG) and the adapter
                         breadcrumb trace (QAR_ADAPTER_TRACE, in rviz_live_*.err.log).
                         Expect GB-sized logs, and do NOT judge stalls or latency in this
                         mode: its own I/O can cause them. Without it (the default) QarOS
                         logs at `info`, zenoh logging is off and no per-frame trace is on.
.PARAMETER KeepLogEnv    In QUIET mode (the default) loud logging values already present in
                         the shell (QAR_LOG_SEVERITY / QAR_PANEL_LOG_SEVERITY trace|debug,
                         RUST_LOG with trace|debug, QAR_ZENOH_LOG, QAR_ADAPTER_TRACE*,
                         QAR_ADAPTER_FRAME_TRACE) are CLEARED with a notice: such a value
                         silently turns the "quiet" run into a TRACE run (zenoh at debug
                         in RViz, the QarOS library at TRACE because it obeys
                         QAR_LOG_SEVERITY over the panel's own setting). -KeepLogEnv
                         keeps them as they are. Quieter values you set yourself (e.g.
                         QAR_LOG_SEVERITY=error) always win, in every mode.
.PARAMETER QarLogSeverity  Log level of QarOS for this run only: warn, info or debug. It sets
                         QAR_LOG_SEVERITY (the runtime, the launcher and the mixers it starts,
                         and the library inside RViz) and QAR_PANEL_LOG_SEVERITY, in quiet mode
                         as well, and, when you pass it, wins over the level of -TraceLogs and
                         over a value of your shell. The variables of your shell are put back
                         when the script ends.
                         The library writes its log to qar_streaming.log, which the next session
                         overwrites, so the launcher saves a copy of that file (see above).
                         Without the switch the level is the mode's own (info, or trace with
                         -TraceLogs), and a value of your shell that is not trace or debug wins.
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
.PARAMETER TestMarkers   Start publish_test_markers.py with the launch. It publishes a small
                         MarkerArray on /qar_test_markers, which the default scene
                         (scene3_grid_marker.rviz) shows; without the switch nothing is started
                         and that scene shows only the grid. With or without the switch the
                         launcher exports QAR_PANEL_TEST_MARKERS_PYTHON (the python of this
                         shell) and QAR_PANEL_TEST_MARKERS_SCRIPT (the full path of
                         publish_test_markers.py) to RViz when both exist, so that the
                         "Test markers" button of the panel's Diagnostics tab can start and stop
                         the publisher at any time. When python or the script is missing,
                         neither variable is exported (a value from the shell is dropped) and
                         the dry run says why.
.PARAMETER LogDir        Folder for the rviz2 logs (rviz_live_<stamp>.out.log and .err.log),
                         the record of the start checks of the run (rviz_live_<stamp>.checks.txt,
                         written with -Confirm) and the -TimingCsv folders. Default:
                         %TEMP%\qar_rviz_logs.
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
    [string[]]$InvitePeers = @(),
    [ValidateSet('auto', 'local', 'remote')]
    [string]$HubMode = "",
    [string]$HubHost = "",
    [ValidateRange(0, 65535)]
    [int]$HubPort = 0,
    [string]$RuntimeDir = "",
    [switch]$TraceLogs,
    [switch]$KeepLogEnv,
    [ValidateSet('warn', 'info', 'debug')]
    [string]$QarLogSeverity = "info",
    [switch]$TimingCsv,
    [switch]$NoDdsProfile,
    [switch]$TestMarkers,
    [string]$LogDir = "",
    [switch]$Confirm
)

$ErrorActionPreference = "Stop"
$runStarted = Get-Date
# True when the caller passed -QarLogSeverity: only then does it beat the shell and -TraceLogs.
$qarLogSeverityGiven = $PSBoundParameters.ContainsKey('QarLogSeverity')
$stamp = $runStarted.ToString("yyyyMMdd_HHmmss")

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
# The installed QAROS is recorded in the registry by its installer (machine scope). -SdkRoot wins,
# then the registry, then QAR_SDK_ROOT. The folder is used for the checks and the plan only: the
# plugin loads the installed QAROS library itself. No installation is not an error here, because a
# Hub of another PC can be joined without one.
$qarosKey = 'HKLM:\SOFTWARE\Quaternar\QAROS'
function Get-QarosInstall {
    try {
        $k = Get-ItemProperty -LiteralPath $qarosKey -ErrorAction Stop
        if ([string]::IsNullOrWhiteSpace($k.InstallFolder)) { return $null }
        return [pscustomobject]@{
            InstallFolder = [string]$k.InstallFolder; Version = [string]$k.Version
            Flavor = [string]$k.Flavor; CApi = [string]$k.CApi; Scope = [string]$k.Scope
        }
    } catch { return $null }
}
$qarosInstall = Get-QarosInstall
$sdkRootSource = if (-not [string]::IsNullOrWhiteSpace($SdkRoot)) { '-SdkRoot' } else { $null }
if (-not $sdkRootSource -and $qarosInstall) {
    $SdkRoot = $qarosInstall.InstallFolder
    $sdkRootSource = 'registry'
} elseif (-not $sdkRootSource -and $env:QAR_SDK_ROOT) {
    $sdkRootSource = 'QAR_SDK_ROOT'
}
$sdkProblem = $null
try {
    $SdkRoot = Resolve-QarLocation -Value $SdkRoot -Parameter 'SdkRoot' -EnvVar 'QAR_SDK_ROOT' `
        -What 'QAROS install folder' -Marker 'bin\qar-streaming-c.dll' -Section 'The QarOS SDK' -Optional
} catch {
    $sdkProblem = $_.Exception.Message
    $SdkRoot = $null
}
$Prefix = Resolve-QarLocation -Value $Prefix -Parameter 'Prefix' -EnvVar 'QAR_PREFIX' `
    -What 'Install prefix' -Section 'Directory layout and environment variables'
$QtRoot = Resolve-QarLocation -Value $QtRoot -Parameter 'QtRoot' -EnvVar 'QAR_QT_ROOT' `
    -What 'Qt 5.15.2' -Marker 'bin\Qt5Core.dll' -Section 'Qt 5.15.2'
$Ros2Root = Resolve-QarLocation -Value $Ros2Root -Parameter 'Ros2Root' -EnvVar 'ROS2_INSTALL_ROOT' `
    -What 'ROS 2 distribution' -Marker 'local_setup.bat' -Section 'ROS 2 Humble for Windows'
if ([string]::IsNullOrEmpty($LogDir)) { $LogDir = Join-Path $env:TEMP "qar_rviz_logs" }
# The record of the start checks shares the stamp of the run's RViz logs (rviz_live_<stamp>.out.log
# and .err.log, named where RViz is started).
$checksFile = Join-Path $LogDir "rviz_live_$stamp.checks.txt"
# The copy of the QarOS library log of this run (see Save-QarStreamingLog), with the same stamp.
$qarLogCopy = Join-Path $LogDir "rviz_live_$stamp.qar_streaming.log"

$sdkBin = if ($SdkRoot) { Join-Path $SdkRoot "bin" } else { $null }
# The folders of the processes that QAROS starts write their logs to (see Save-QarRunLogs).
$qarosLogRoot = Join-Path $env:TEMP "quaternar\logs"

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

# --- QAROS: nothing is put on PATH -----------------------------------------------
# The plugin loads the installed QAROS library itself (qar_library_load(NULL): the installation that
# the registry names, or the folder of QAROS_RUNTIME_DIR when it is set) with flags that take the
# library's own dependencies from its folder. PATH is not touched; a folder of an older QarOS drop
# on PATH could not join this Hub anyway.
$runtimeDirFull = $null
if (-not [string]::IsNullOrWhiteSpace($RuntimeDir)) {
    $runtimeDirFull = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($RuntimeDir.Trim().Trim('"'))
    if (-not (Test-Path -LiteralPath $runtimeDirFull -PathType Container)) {
        throw "-RuntimeDir folder does not exist: $runtimeDirFull"
    }
    $env:QAROS_RUNTIME_DIR = $runtimeDirFull
}
# The Hub the panel joins. The parameters are exported only when they are given; the panel setting
# "Hub" of the QAROS Hub group decides otherwise, and a variable wins over that setting.
if ($HubMode) { $env:QAR_PANEL_HUB_MODE = $HubMode }
if ($HubHost) { $env:QAR_PANEL_HUB_HOST = $HubHost }
if ($HubPort -gt 0) { $env:QAR_PANEL_HUB_PORT = "$HubPort" }
# Variables that this QAROS does not read: the panel logs each one once.
foreach ($removedVar in 'QAR_RUNTIME_BINARIES_DIR', 'QAR_PANEL_VISUALIZERS', 'QAR_PANEL_VISUALIZER_GUID',
        'QAR_PANEL_LAUNCHER_CONFIG', 'QAR_PANEL_PEER_ADMISSION', 'QAR_PANEL_PEER_SETTLE_MS',
        'QAR_PANEL_PEER_NAME_FILTER') {
    if ([Environment]::GetEnvironmentVariable($removedVar)) {
        Write-Warning "$removedVar is set in this shell and has no effect with this QAROS."
    }
}
# The panel is a live-stream producer.
$env:QAR_PANEL_LIVE_STREAM = "1"
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
$logLevel = $QarLogSeverity   # the level of quiet mode: info, or the one that was asked for
$logEnv = @(
    @{ Name = 'QAR_LOG_SEVERITY';       Quiet = $logLevel; Trace = 'trace'; Kind = 'severity' },
    @{ Name = 'QAR_PANEL_LOG_SEVERITY'; Quiet = $logLevel; Trace = $null;   Kind = 'severity' },
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
# QAR_LOG_SEVERITY. In quiet mode this script supplies 'info' for the former, which would beat a
# QAR_LOG_SEVERITY that was kept on purpose (say 'error') - so mirror that value instead and
# "your environment wins" stays true for the library inside RViz as well.
if (-not $TraceLogs -and
    @($logEnvKept | Where-Object { $_ -like 'QAR_LOG_SEVERITY=*' }).Count -gt 0 -and
    @($logEnvKept | Where-Object { $_ -like 'QAR_PANEL_LOG_SEVERITY=*' }).Count -eq 0) {
    [Environment]::SetEnvironmentVariable('QAR_PANEL_LOG_SEVERITY',
        [Environment]::GetEnvironmentVariable('QAR_LOG_SEVERITY'))
}
# -QarLogSeverity is an explicit request, so it is applied after the mode's own values: it wins
# over -TraceLogs, over a value of the shell and over the clearing of loud values in quiet mode.
# Both variables are set because the library inside RViz reads QAR_PANEL_LOG_SEVERITY first.
# The caller's environment is put back when the script ends, so the level holds for this run only.
if ($qarLogSeverityGiven) {
    [Environment]::SetEnvironmentVariable('QAR_LOG_SEVERITY', $QarLogSeverity)
    [Environment]::SetEnvironmentVariable('QAR_PANEL_LOG_SEVERITY', $QarLogSeverity)
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

# Targets. The panel streams to every TARGET of the Hub (a headset, or a Visualizer window that you
# open in QAROS), up to QAR_PANEL_MAX_PEERS streams (default 8). Nothing here starts a Visualizer.
#   QAR_PANEL_INVITE_PEERS  devices invited right after the panel has joined, e.g.
#     "hololens:HOLOLENS-A2B3,headset:HEADSET-C4D5". Only the Hub of this PC (local mode) invites
#     devices; the Hub of another PC refuses.
$InvitePeers = @($InvitePeers | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($entry in $InvitePeers) {
    $inviteKind, $inviteHost = $entry -split ':', 2
    if (@('hololens', 'headset') -notcontains $inviteKind.ToLowerInvariant() -or [string]::IsNullOrWhiteSpace($inviteHost)) {
        throw "-InvitePeers entry '$entry' is not kind:hostname with a kind of hololens or headset."
    }
}
if ($InvitePeers.Count -gt 0 -and -not $env:QAR_PANEL_INVITE_PEERS) {
    $env:QAR_PANEL_INVITE_PEERS = ($InvitePeers -join ',')
}

# Which Hub the panel will join (the same rule as the panel's own "auto": a stored pairing with
# another Hub first, then an installed QAROS, else a Hub of another PC). The identity file only
# exists after a pairing with another PC's Hub; only its host and port are read, never its id.
$identityBase = if ($env:QAR_PANEL_IDENTITY_DIR) { $env:QAR_PANEL_IDENTITY_DIR } else { Join-Path $env:LOCALAPPDATA 'Quaternar\QarOS-ROS' }
$identityLeaf = 'rviz2-plugin'
if ($env:QAR_PANEL_INSTANCE) { $identityLeaf += '.' + ($env:QAR_PANEL_INSTANCE -replace '[^A-Za-z0-9_-]', '-') }
$identityFile = Join-Path (Join-Path $identityBase $identityLeaf) 'identity.txt'
$identityPresent = Test-Path -LiteralPath $identityFile -PathType Leaf
$identityHub = $null
if ($identityPresent) {
    $idHost = $null
    $idPort = $null
    foreach ($idLine in @(Get-Content -LiteralPath $identityFile -ErrorAction SilentlyContinue)) {
        if ($idLine -match '^hub_host=(.+)$') { $idHost = $Matches[1].Trim() }
        elseif ($idLine -match '^hub_port=(\d+)$') { $idPort = $Matches[1] }
    }
    $identityHub = if ($idHost -and $idPort) { "${idHost}:$idPort" } else { '(file unreadable)' }
}
$hubModeAsked = 'auto'
$hubModeAskedBy = 'default'
if ($HubMode) { $hubModeAsked = $HubMode; $hubModeAskedBy = '-HubMode' }
elseif ($env:QAR_PANEL_HUB_MODE -match '^(auto|local|remote)$') { $hubModeAsked = $Matches[1]; $hubModeAskedBy = 'environment' }
if ($hubModeAsked -ne 'auto') {
    $hubModeResolved = $hubModeAsked
    $hubModeWhy = "asked for by $hubModeAskedBy"
} elseif ($identityPresent) {
    $hubModeResolved = 'remote'
    $hubModeWhy = 'auto: a pairing with the Hub of another PC is stored'
} elseif ($qarosInstall) {
    $hubModeResolved = 'local'
    $hubModeWhy = 'auto: QAROS is installed on this PC'
} else {
    $hubModeResolved = 'remote'
    $hubModeWhy = 'auto: QAROS is not installed on this PC'
}

# Start position for the desktop Visualizer.
#   QAR_PANEL_FIXED_POSES  room | ground. The docks, the panel itself, the video panels and the
#                          Displays copy have a fixed place: `room` hangs their top edge at a fixed
#                          height of the room, `ground` hangs it 1.5 m above the RViz ground,
#                          wherever the scene has been moved to. The panel's own default is room.
#   QAR_PANEL_VOLUME_Y     The room height of the centre of the app volume, in metres.
# This launcher starts the RViz with ground and -1.6, the position from which the desktop Visualizer
# shows the scene and the panels well at its start. A value that is already in the environment wins,
# and the plan names the source of each value.
$fixedPosesSource = 'environment'
if ([string]::IsNullOrWhiteSpace($env:QAR_PANEL_FIXED_POSES)) {
    $env:QAR_PANEL_FIXED_POSES = 'ground'
    $fixedPosesSource = 'launcher default'
}
$volumeYSource = 'environment'
if ([string]::IsNullOrWhiteSpace($env:QAR_PANEL_VOLUME_Y)) {
    $env:QAR_PANEL_VOLUME_Y = '-1.6'
    $volumeYSource = 'launcher default'
}

# Test markers. scene3_grid_marker.rviz shows the markers of publish_test_markers.py, a small rclpy
# node. -TestMarkers starts it with the launch (below). With or without the switch, the python of
# this shell and the full path of the script are exported to RViz whenever both exist, so that the
# "Test markers" button of the panel's Diagnostics tab can start and stop the publisher on demand.
# When either is missing nothing is exported (a value from the shell is dropped), and the dry run
# says why. python is looked up here, after the ROS 2 environment is set up, and used again below.
$pyExe = (Get-Command python -ErrorAction SilentlyContinue).Source
$markersScript = Join-Path $PSScriptRoot "publish_test_markers.py"
$markersMissing = $null
if (-not $pyExe) {
    $markersMissing = "python not found"
} elseif (-not (Test-Path -LiteralPath $markersScript -PathType Leaf)) {
    $markersMissing = "$markersScript not found"
}
[Environment]::SetEnvironmentVariable('QAR_PANEL_TEST_MARKERS_PYTHON',
    $(if ($markersMissing) { $null } else { $pyExe }))
[Environment]::SetEnvironmentVariable('QAR_PANEL_TEST_MARKERS_SCRIPT',
    $(if ($markersMissing) { $null } else { $markersScript }))

Write-Host "=== RViz -> QAROS Hub live stream ===" -ForegroundColor Magenta
Write-Host "  scene            : $Scene$(if (Test-Path $Scene) { '' } else { '  (NOT FOUND)' })"
$sdkShown = if ($SdkRoot) { "$SdkRoot (from $sdkRootSource)" } else { '(none found: only the Hub of another PC can be joined)' }
Write-Host "  QAROS install    : $sdkShown"
if ($sdkProblem) { Write-Host "  QAROS install note: $sdkProblem" -ForegroundColor Yellow }
$runtimeDirShown = if ($runtimeDirFull) { $runtimeDirFull } else { '(not set: the plugin uses the installed QAROS)' }
Write-Host "  QAROS_RUNTIME_DIR        = $runtimeDirShown"
Write-Host "  QAR_PANEL_LIVE_STREAM    = 1"
Write-Host "  FASTRTPS_DEFAULT_PROFILES_FILE = $ddsProfileNote"
Write-Host "  QAR_SCENE_LIGHT          = $env:QAR_SCENE_LIGHT (headlight: $(if ($env:QAR_SCENE_LIGHT_HEADLIGHT -eq 'keep') {'kept'} else {'suppressed while streaming'}))"
$hubVarShown = if ($env:QAR_PANEL_HUB_MODE) { $env:QAR_PANEL_HUB_MODE } else { '(not set: the panel setting "Hub" of the QAROS Hub group decides, Automatic by default)' }
Write-Host "  QAR_PANEL_HUB_MODE       = $hubVarShown"
$hubHostShown = if ($env:QAR_PANEL_HUB_HOST) { $env:QAR_PANEL_HUB_HOST } else { '(not set)' }
$hubPortShown = if ($env:QAR_PANEL_HUB_PORT) { $env:QAR_PANEL_HUB_PORT } else { '(not set)' }
Write-Host "  QAR_PANEL_HUB_HOST / _PORT = $hubHostShown / $hubPortShown"
Write-Host "  QAR_PANEL_INVITE_PEERS   = $(if ($env:QAR_PANEL_INVITE_PEERS) { $env:QAR_PANEL_INVITE_PEERS } else { '(none - pass -InvitePeers to invite devices)' })"
Write-Host "  QAR_PANEL_FIXED_POSES    = $env:QAR_PANEL_FIXED_POSES ($fixedPosesSource)"
Write-Host "  QAR_PANEL_VOLUME_Y       = $env:QAR_PANEL_VOLUME_Y ($volumeYSource)"
if ($markersMissing) {
    $markersNote = "nothing exported"
    if ($TestMarkers) { $markersNote = "-TestMarkers cannot start them, nothing exported" }
    Write-Host "  test markers: unavailable ($markersMissing; $markersNote)" -ForegroundColor Yellow
} elseif ($TestMarkers) {
    Write-Host "  test markers: on ($pyExe $markersScript)"
} else {
    Write-Host "  test markers: off (start them with -TestMarkers or from the Diagnostics tab)"
}
foreach ($markersVar in 'QAR_PANEL_TEST_MARKERS_PYTHON', 'QAR_PANEL_TEST_MARKERS_SCRIPT') {
    $markersShown = [Environment]::GetEnvironmentVariable($markersVar)
    if (-not $markersShown) { $markersShown = '(not exported)' }
    Write-Host ("  {0,-29} = {1}" -f $markersVar, $markersShown)
}
Write-Host "  install prefix           : $Prefix (plugin overlay: $(if ($overlayFound) { 'found' } else { 'NOT FOUND' }))"
Write-Host "  RViz logs (-LogDir)      : $LogDir"
Write-Host "  rviz2.exe                : $(if ($rvizExe) { $rvizExe } else { '(not found on PATH)' })"
$gpuNow = if ($rvizExe -and (Test-Path $gpuPrefKey)) { (Get-ItemProperty -Path $gpuPrefKey -Name $rvizExe -ErrorAction SilentlyContinue).$rvizExe } else { $null }
Write-Host "  GPU preference (HKCU)    : $(if ($gpuNow) { $gpuNow } else { '(not set)' }) - with -Confirm the launcher sets High-performance (NVIDIA) for rviz2.exe"
if ($TraceLogs) {
    Write-Host "  log mode                 : TRACE (-TraceLogs) - QarOS logs at trace, zenoh debug, adapter traces on; GB-sized logs whose I/O can stall streams, so not for latency work." -ForegroundColor Yellow
} else {
    Write-Host "  log mode                 : QUIET (default) - QarOS logs at $QarLogSeverity$(if ($qarLogSeverityGiven) { ' (-QarLogSeverity)' } else { ' (default)' }), no zenoh logging, no per-frame traces; -TraceLogs collects full logs for a vendor report." -ForegroundColor Green
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


# --- Startup environment checks: WARN only, never block -----------------------------
# A stall that hits every stream at once is rarely QarOS's own: a nearly full system
# drive under real-time antivirus, another process using the GPU, or no commit charge
# left all freeze the Visualizer, the mixer and the launcher together.
# Saying so up front keeps such a run from being mistaken for a plugin bug.
# Every check also leaves a line in $script:StartCheckLines; with -Confirm they are written next
# to the RViz logs of the run (Write-StartChecksRecord), so that a run whose scene came out black
# can be compared with what else was using the GPU at its start.
$script:StartCheckLines = New-Object 'System.Collections.Generic.List[string]'
$script:StartChecksTime = ''

function Add-StartCheck([string]$Line) { $script:StartCheckLines.Add($Line) }

# One nvidia-smi query with a 10 s timeout: nvidia-smi can hang on a wedged driver, and the start
# checks must not block. Found = nvidia-smi exists; TimedOut = it did not answer in time (it is
# ended); Lines = the output lines that are not blank, when it answered with the exit code 0.
function Invoke-NvidiaSmiQuery {
    param([Parameter(Mandatory)][string]$Arguments)
    $answer = [pscustomobject]@{ Found = $false; TimedOut = $false; Lines = @() }
    $nvidiaSmi = Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if (-not $nvidiaSmi) { return $answer }
    $answer.Found = $true
    try {
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $nvidiaSmi.Source
        $psi.Arguments = $Arguments
        $psi.UseShellExecute = $false
        $psi.CreateNoWindow = $true
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $true
        $smi = [System.Diagnostics.Process]::Start($psi)
        $smiOut = $smi.StandardOutput.ReadToEndAsync()
        $smiErr = $smi.StandardError.ReadToEndAsync()   # drained so a full pipe cannot deadlock it
        if ($smi.WaitForExit(10000)) {
            if ($smi.ExitCode -eq 0) {
                $rows = $smiOut.Result -split "`r?`n"
                $answer.Lines = @($rows | ForEach-Object { $_.Trim() } | Where-Object { $_ })
            }
        } else {
            try { $smi.Kill() } catch { }
            $answer.TimedOut = $true
        }
    } catch { }
    return $answer
}

function Show-StartupEnvironmentWarnings {
    param([string[]]$Folders)
    $warned = 0
    $invariant = [System.Globalization.CultureInfo]::InvariantCulture
    $script:StartChecksTime = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'

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
            $driveLine = "drive space of ${root}: " +
                "$($freeGB.ToString('N1', $invariant)) GB free " +
                "($($freePct.ToString('N1', $invariant)) %)"
            if ($freePct -lt 10 -or $freeGB -lt 15) {
                Write-Warning ("Drive {0} is nearly full: {1:N1} GB free ({2:N1}%). Logs and temp files land there, and a nearly full drive under real-time antivirus scanning can stall writes - and every QarOS stream with them. Free some space, or point QAR_PANEL_LOG_DIR at a roomier drive." -f $root, $freeGB, $freePct)
                $warned++
                $driveLine += ' - WARNING: nearly full (under 10 % or under 15 GB free)'
            } else {
                $driveLine += ' - OK'
            }
            Add-StartCheck $driveLine
        } catch { }
    }

    # 2. Other processes using the NVIDIA GPU for compute (a CUDA job, say).
    $computeQuery = '--query-compute-apps=pid,process_name,used_memory ' +
        '--format=csv,noheader'
    $compute = Invoke-NvidiaSmiQuery $computeQuery
    if (-not $compute.Found) {
        Add-StartCheck 'GPU compute users: not checked, nvidia-smi not found'
    } elseif ($compute.TimedOut) {
        Write-Warning ("nvidia-smi did not answer within 10 s; GPU compute users not checked " +
            "(a wedged NVIDIA driver can stall the Visualizer too).")
        $warned++
        Add-StartCheck 'GPU compute users: not checked - WARNING: nvidia-smi: no answer in 10 s'
    } else {
        # The processes of the installed QAROS are the ones that stream: they are not "other" users.
        $qarosOwn = @($compute.Lines | Where-Object {
            $SdkRoot -and $_.IndexOf($SdkRoot, [System.StringComparison]::OrdinalIgnoreCase) -ge 0 })
        $apps = @($compute.Lines | Where-Object { $qarosOwn -notcontains $_ })
        if ($qarosOwn.Count -gt 0) {
            Add-StartCheck "GPU compute users of the installed QAROS: $($qarosOwn.Count) (not counted as other users)"
        }
        if ($apps.Count -gt 0) {
            $usersFormat = "{0} other process(es) are using the NVIDIA GPU for compute " +
                "(pid, name, GPU memory):`n    {1}`n  They compete with QAROS for the GPU and " +
                "can stall every stream together. Stop them before judging stalls or latency."
            Write-Warning ($usersFormat -f $apps.Count, ($apps -join "`n    "))
            $warned++
            Add-StartCheck "GPU compute users: $($apps.Count) other process(es) - WARNING:"
            Add-StartCheck '    (pid, name, GPU memory)'
            foreach ($app in $apps) { Add-StartCheck "    $app" }
        } else {
            Add-StartCheck 'GPU compute users: none - OK'
        }
    }

    # 2b. How much of the GPU is taken now: the plan names it, the record keeps the rows as
    #     nvidia-smi gave them (name, memory used, memory total, utilization).
    $gpuQuery = '--query-gpu=name,memory.used,memory.total,utilization.gpu --format=csv,noheader'
    $gpu = Invoke-NvidiaSmiQuery $gpuQuery
    $gpuTitle = "GPU memory (nvidia-smi ${gpuQuery})"
    if (-not $gpu.Found) {
        $gpuShown = 'not available (nvidia-smi not found)'
        Add-StartCheck "${gpuTitle}: $gpuShown"
    } elseif ($gpu.TimedOut) {
        $gpuShown = 'not available (nvidia-smi did not answer within 10 s)'
        Add-StartCheck "${gpuTitle}: $gpuShown"
    } elseif (@($gpu.Lines).Count -eq 0) {
        $gpuShown = 'not available (nvidia-smi gave no GPU rows)'
        Add-StartCheck "${gpuTitle}: $gpuShown"
    } else {
        $gpuRows = @()
        foreach ($row in @($gpu.Lines)) {
            $f = @($row -split ',\s*')
            if ($f.Count -eq 4) {
                $gpuRows += "$($f[0]): $($f[1]) used of $($f[2]), $($f[3]) busy"
            } else {
                $gpuRows += $row
            }
        }
        $gpuShown = $gpuRows -join ' | '
        Add-StartCheck "${gpuTitle}:"
        foreach ($row in @($gpu.Lines)) { Add-StartCheck "    $row" }
    }
    Write-Host ("  GPU memory (nvidia-smi)  : {0}" -f $gpuShown)

    # 3. Commit charge (RAM + page file) nearly exhausted. FreeVirtualMemory is in KB.
    try {
        $os = Get-CimInstance Win32_OperatingSystem -ErrorAction Stop
        $freeCommitGB = $os.FreeVirtualMemory / 1MB
        $totalCommitGB = $os.TotalVirtualMemorySize / 1MB
        $commitLine = "commit charge: $($freeCommitGB.ToString('N1', $invariant)) GB free of " +
            "$($totalCommitGB.ToString('N1', $invariant)) GB (RAM + page file)"
        if ($freeCommitGB -lt 4) {
            Write-Warning ("Only {0:N1} GB of commit charge is left (limit {1:N1} GB = RAM + page file). When it runs out allocations fail and everything stalls. Close memory-hungry applications or enlarge the page file." -f $freeCommitGB, ($os.TotalVirtualMemorySize / 1MB))
            $warned++
            $commitLine += ' - WARNING: under 4 GB free'
        } else {
            $commitLine += ' - OK'
        }
        Add-StartCheck $commitLine
    } catch {
        Add-StartCheck 'commit charge: not read'
    }

    if ($warned -eq 0) {
        Write-Host "  startup checks           : OK (drive space, GPU compute users, commit charge)"
    }
}

# Writes what the start checks found to $Path, next to the RViz logs of the run. Only a run with
# -Confirm calls it (the dry run names the file and writes nothing). A file that cannot be written
# is a warning: the checks never block a run.
function Write-StartChecksRecord {
    param([Parameter(Mandatory)][string]$Path)
    $lines = @(
        "start checks of the run rviz_live_$stamp",
        "checks run at: $($script:StartChecksTime) (local time)"
    ) + @($script:StartCheckLines)
    try {
        $utf8 = New-Object System.Text.UTF8Encoding($false)
        [System.IO.File]::WriteAllLines($Path, [string[]]$lines, $utf8)
    } catch {
        Write-Warning "could not write the start checks record to ${Path}: $($_.Exception.Message)"
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

# The library of QarOS inside RViz writes its log to <QAR log dir>\qar_streaming.log and starts the
# file afresh in every session, so the log of a run is gone as soon as the next run starts. When
# rviz2 has exited, the launcher copies it next to the RViz logs of the run. $RunStart is the
# start of the run: a file older than that belongs to an earlier session (QarOS never started in
# this one) and is not copied under this run's name. Nothing here is fatal: every failure is a
# warning. @return true when the copy was made.
function Save-QarStreamingLog {
    param(
        [Parameter(Mandatory)][string]$Source,
        [Parameter(Mandatory)][string]$Destination,
        [Parameter(Mandatory)][datetime]$RunStart
    )
    $timeFormat = 'yyyy-MM-dd HH:mm:ss'
    try {
        if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
            Write-Warning "no QarOS library log at $Source; nothing saved as $Destination"
            return $false
        }
        $written = (Get-Item -LiteralPath $Source -ErrorAction Stop).LastWriteTime
        if ($written -lt $RunStart) {
            $writtenText = $written.ToString($timeFormat)
            $startText = $RunStart.ToString($timeFormat)
            Write-Warning ("$Source was last written at $writtenText, before this run " +
                "started ($startText): it belongs to an earlier session, so it is not " +
                "saved as $Destination")
            return $false
        }
        Copy-Item -LiteralPath $Source -Destination $Destination -Force -ErrorAction Stop
        $size = (Get-Item -LiteralPath $Destination).Length
        Write-Host "  QAR library log saved    : $Destination ($size bytes)" `
            -ForegroundColor Green
        return $true
    } catch {
        $reason = $_.Exception.Message
        Write-Warning ("could not save the QarOS library log $Source as " +
            "${Destination}: $reason")
        return $false
    }
}

# The processes that QAROS starts write their logs below %TEMP%\quaternar\logs, and the files are
# not started afresh for each run: the launcher's <session id>\*.log, the crash reports of the Hub
# service and the *.crash.txt reports of a crashed process in <QAR log dir>\crashes. Copies every
# file of $SourceDir that matches $Filter and was last written at or after $RunStart to $LogDir,
# as rviz_live_<stamp>. followed by
# $NameFormat applied to the capture `id` of $NameRegex, and prints one line per copy. A missing
# folder, or no file of this run, is a note; a failed copy is a warning; nothing here is fatal.
# The .dmp files are never matched. @return the number of files copied.
function Save-QarRunFiles {
    param(
        [Parameter(Mandatory)][string]$SourceDir,
        [Parameter(Mandatory)][string]$Filter,
        [Parameter(Mandatory)][string]$NameRegex,
        [Parameter(Mandatory)][string]$NameFormat,
        [Parameter(Mandatory)][string]$Kind,
        [Parameter(Mandatory)][datetime]$RunStart
    )
    $copied = 0
    try {
        if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
            Write-Host "  ${Kind} copy: no folder $SourceDir; nothing to save"
            return 0
        }
        $files = @(Get-ChildItem -LiteralPath $SourceDir -File -Filter $Filter `
            -ErrorAction Stop | Where-Object { $_.LastWriteTime -ge $RunStart })
        if ($files.Count -eq 0) {
            Write-Host "  ${Kind} copy: no $Filter in $SourceDir written during this run"
            return 0
        }
        foreach ($f in $files) {
            if ($f.Name -notmatch $NameRegex) { continue }
            $target = Join-Path $LogDir ("rviz_live_$stamp." + ($NameFormat -f $Matches['id']))
            try {
                Copy-Item -LiteralPath $f.FullName -Destination $target -Force -ErrorAction Stop
                $size = (Get-Item -LiteralPath $target).Length
                Write-Host "  ${Kind} copy: $target ($size bytes, from $($f.Name))" `
                    -ForegroundColor Green
                $copied++
            } catch {
                Write-Warning "could not save $($f.FullName) as ${target}: $($_.Exception.Message)"
            }
        }
    } catch {
        Write-Warning "could not look for $Filter in ${SourceDir}: $($_.Exception.Message)"
    }
    return $copied
}

# The newest session folder of the launcher: <logs>\qar-launcher-default\<session id>. Null when
# there is none.
function Get-QarLauncherSessionDir {
    param([Parameter(Mandatory)][string]$QarosLogRoot)
    $launcherLogs = Join-Path $QarosLogRoot 'qar-launcher-default'
    if (-not (Test-Path -LiteralPath $launcherLogs -PathType Container)) { return $null }
    return Get-ChildItem -LiteralPath $launcherLogs -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

# All the copies of Save-QarRunFiles for the run that ends now. QAROS may still be closing the
# logs of its processes when rviz2 has exited, so the copies wait two seconds.
function Save-QarRunLogs {
    param(
        [Parameter(Mandatory)][string]$QarLogDir,
        [Parameter(Mandatory)][string]$QarosLogRoot,
        [Parameter(Mandatory)][datetime]$RunStart
    )
    Start-Sleep -Seconds 2
    $session = Get-QarLauncherSessionDir -QarosLogRoot $QarosLogRoot
    if ($session) {
        [void](Save-QarRunFiles -SourceDir $session.FullName -Filter '*.log' `
            -NameRegex '^(?<id>.+\.log)$' -NameFormat 'launcher_{0}' `
            -Kind 'QAROS launcher log' -RunStart $RunStart)
    } else {
        Write-Host "  QAROS launcher log copy: no session folder in $QarosLogRoot\qar-launcher-default; nothing to save"
    }
    foreach ($crashDir in @($QarLogDir, (Join-Path $QarLogDir 'crashes'),
            (Join-Path $QarosLogRoot 'qaros-hub-service\crashes'))) {
        [void](Save-QarRunFiles -SourceDir $crashDir -Filter '*.crash.txt' `
            -NameRegex '^(?<id>.+\.crash\.txt)$' -NameFormat '{0}' `
            -Kind 'QAROS crash report' -RunStart $RunStart)
    }
}

# What the machine says about QAROS, read only: the registry, the process list and the endpoint
# file of the Hub. Nothing is started, stopped or written, and the trust key in the endpoint file is
# never printed.
function Get-QarosProcessText([string]$Name) {
    $found = @(Get-Process -Name $Name -ErrorAction SilentlyContinue)
    if ($found.Count -eq 0) { return 'not running' }
    $since = ''
    try { $since = ", since $($found[0].StartTime.ToString('HH:mm:ss'))" } catch { }
    return "running (pid $(($found | ForEach-Object { $_.Id }) -join ', ')$since)"
}

function Show-QarosState {
    Write-Host "  --- QAROS on this PC (read only) ---"
    if ($qarosInstall) {
        Write-Host ("  installed QAROS          : {0} {1} (C API {2}, scope {3}) at {4}" -f $qarosInstall.Version,
            $qarosInstall.Flavor, $qarosInstall.CApi, $qarosInstall.Scope, $qarosInstall.InstallFolder)
    } else {
        Write-Host "  installed QAROS          : none ($qarosKey has no InstallFolder): only the Hub of another PC can be joined" -ForegroundColor Yellow
    }
    foreach ($name in 'qaros-hub-service', 'qar-runtime-launcher', 'qar-streaming-viz') {
        Write-Host ("  {0,-24} : {1}" -f $name, (Get-QarosProcessText $name))
    }
    $hubRunning = @(Get-Process -Name 'qaros-hub-service' -ErrorAction SilentlyContinue).Count -gt 0
    $endpointFile = Join-Path $env:LOCALAPPDATA 'Quaternar\Qaros\hub-endpoint.json'
    if (Test-Path -LiteralPath $endpointFile -PathType Leaf) {
        try {
            $endpoint = Get-Content -LiteralPath $endpointFile -Raw -ErrorAction Stop | ConvertFrom-Json
            Write-Host ("  Hub endpoint file        : host {0}, onboarding port {1}, router port {2}" -f
                $endpoint.onboardingHost, $endpoint.onboardingPort, $endpoint.routerPort)
            Write-Host "                             $endpointFile (the trust key in it is never shown)"
            if (-not $hubRunning) {
                Write-Host "                             no Hub service runs, so these values can be left over from an earlier start"
            }
        } catch {
            Write-Host "  Hub endpoint file        : $endpointFile could not be read ($($_.Exception.Message))" -ForegroundColor Yellow
        }
    } else {
        Write-Host "  Hub endpoint file        : not found ($endpointFile): no Hub has started on this PC"
    }
    Write-Host "  Hub mode (resolved)      : $hubModeResolved ($hubModeWhy; with no variable and no parameter the panel setting 'Hub' of the QAROS Hub group decides)"
    $identityShown = if ($identityPresent) {
        "present, paired with the Hub at $identityHub ($identityFile)"
    } else {
        "not present ($identityFile): no pairing with the Hub of another PC is stored"
    }
    Write-Host "  identity file            : $identityShown"
    Write-Host "  --- what you will have to do ---"
    if ($hubModeResolved -eq 'local') {
        Write-Host "  Press Connect in the Connection tab of the 'Qar Streaming' panel. The first time, QAROS asks in its own window whether rviz2 may join: click Allow."
        if (-not $hubRunning) {
            Write-Host "  No Hub service runs now: the plugin will start QAROS when you press Connect."
        }
        Write-Host "  Open a Visualizer window from QAROS (Start menu), or put on a headset: the panel streams to every target that joins."
    } else {
        $remoteHub = if ($identityPresent) { $identityHub } elseif ($env:QAR_PANEL_HUB_HOST) { "$($env:QAR_PANEL_HUB_HOST):$($env:QAR_PANEL_HUB_PORT)" } else { 'the Hub of the other PC' }
        Write-Host "  Press Connect in the Connection tab of the 'Qar Streaming' panel, then type the host, the onboarding port and the pairing code that $remoteHub shows"
        Write-Host "  (Connection panel of its Visualizer: Hubs > Pair another Hub > Let another hub join us). The code lives about 10 seconds and works once;"
        Write-Host "  the pairing is stored, so the next start joins without a code."
        if ($InvitePeers.Count -gt 0) {
            Write-Host "  -InvitePeers is set, but only the Hub of this PC (local mode) invites devices: a Hub of another PC refuses." -ForegroundColor Yellow
        }
    }
}

Show-QarosState
Show-StartupEnvironmentWarnings -Folders @($env:TEMP, $qarLogDir, $LogDir)
Write-Host "  start checks record      : $checksFile (written with -Confirm)"
Invoke-QarLogRotation -LogDir $qarLogDir -Apply:$Confirm
if ($qarLogSeverityGiven) {
    Write-Host ("  -QarLogSeverity          : $QarLogSeverity (QAR_LOG_SEVERITY and " +
        "QAR_PANEL_LOG_SEVERITY for this run only, also in quiet mode)")
}
$qarLogSource = Join-Path $qarLogDir 'qar_streaming.log'
Write-Host ("  QAR library log copy     : $qarLogCopy (copy of $qarLogSource made when " +
    "rviz2 exits; with -Confirm the launcher waits for rviz2)")
$launcherSession = Get-QarLauncherSessionDir -QarosLogRoot $qarosLogRoot
$launcherSessionShown = if ($launcherSession) { "newest session folder now: $($launcherSession.Name)" } else { "no session folder yet" }
Write-Host ("  QAROS launcher logs copy : when rviz2 exits, the *.log files of the newest session folder of " +
    "$qarosLogRoot\qar-launcher-default ($launcherSessionShown) written during the run are copied to " +
    "${LogDir}: rviz_live_$stamp.launcher_<name>")
Write-Host ("  QAROS crash reports copy : *.crash.txt of $qarosLogRoot\qaros-hub-service\crashes, " +
    "$qarLogDir and $qarLogDir\crashes written during the run, as rviz_live_$stamp.<report> (no .dmp files)")
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
# Launch rviz2 ONLY. The plugin joins the installed QAROS Hub itself when you press Connect in the
# panel, and starts QAROS if no Hub runs and the mode is local. This script starts neither the Hub,
# nor a launcher, nor a Visualizer, and stops none of them.
# ---------------------------------------------------------------------------

# --- Live scene content --------------------------------------------------------
# scene3_grid_marker.rviz subscribes to the test markers; without a publisher RViz
# shows nothing but the grid, and the stream would carry an empty scene on BOTH ends.
# -TestMarkers starts the publisher below; the panel's "Test markers" button starts it later.
# Reap publishers leaked by earlier runs FIRST. Every publisher is a ROS 2 node, and on domain 0 DDS
# assigns each participant the next UDP port pair from 7410 upwards (from 7410 + 250 x domain
# number on any other domain): leaked publishers accumulate and the ladder of ports climbs. A publisher is a Python
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

if ($TestMarkers) {
    if (-not $markersMissing) {
        $pub = Start-Process -FilePath $pyExe `
            -ArgumentList ('"' + $markersScript + '"') `
            -PassThru -WindowStyle Hidden
        Write-Host "marker publisher PID=$($pub.Id) (publish_test_markers.py, frame 'map')" `
            -ForegroundColor Green
        Write-Host "  stopped automatically by the next run; or: Stop-Process -Id $($pub.Id)"
    } else {
        Write-Warning "$markersMissing - markers will NOT be published and the scene will be empty."
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
# The start checks of this run, next to its RViz logs and with their stamp ($checksFile).
Write-StartChecksRecord -Path $checksFile
$rviz = Start-Process -FilePath "rviz2.exe" -ArgumentList @("-d", ('"' + $Scene + '"')) -PassThru `
    -RedirectStandardOutput $outLog -RedirectStandardError $errLog
$rvizStarted = $true
Write-Host "rviz2 PID=$($rviz.Id)."
Write-Host ""
Write-Host "In the RViz 'Qar Streaming' panel open the Connection tab and press Connect."
if ($hubModeResolved -eq 'local') {
    Write-Host "Local Hub: click Allow in the QAROS window if it asks. Open a Visualizer window from QAROS, or put on a headset."
} else {
    Write-Host "Remote Hub: type the host, the onboarding port and the pairing code that Hub shows."
}
Write-Host ""
Write-Host "  RViz stdout : $outLog" -ForegroundColor Cyan
Write-Host "  RViz stderr : $errLog   <- [qar-panel] HB + AR VIDEO lines$(if ($env:QAR_ADAPTER_TRACE) { ', adapter trace' })" -ForegroundColor Cyan
Write-Host "  Start checks: $checksFile   <- drive space, GPU users and memory, commit charge" `
    -ForegroundColor Cyan
Write-Host "  QAR logs    : $qarLogDir   (QAR_LOG_SEVERITY=$(if ($env:QAR_LOG_SEVERITY) { $env:QAR_LOG_SEVERITY } else { '(unset)' }); $(if ($TraceLogs) { 'TRACE mode' } else { 'quiet mode, -TraceLogs for full logs' }))" -ForegroundColor Cyan
if ($timingDir) {
    Write-Host "  Timing CSVs : $timingDir" -ForegroundColor Cyan
    Write-Host "     start the publisher with  --ros-args -p timing_csv:=$timingDir\publisher.csv"
    Write-Host "     after the run:            python scripts\analyze_video_timing.py `"$timingDir`" --publisher `"$timingDir\publisher.csv`""
}
# Wait for rviz2, so that the library log of the run can be saved the moment it ends (below, in
# `finally`, which also runs when Ctrl+C stops the wait).
Write-Host ""
Write-Host ("Waiting for rviz2 to exit; the QAROS library log is then saved as " +
    "$qarLogCopy, with the launcher logs and the crash reports of the run " +
    "(Ctrl+C stops the wait and saves them as they are).") -ForegroundColor Cyan
$rviz.WaitForExit()
} finally {
    # The QarOS library log, the mixer and Visualizer logs and the crash reports of the run, when
    # rviz2 was started: also after a Ctrl+C, then as far as the logs have come. A failure is only
    # a warning.
    if ($rvizStarted) {
        [void](Save-QarStreamingLog -Source (Join-Path $qarLogDir 'qar_streaming.log') `
            -Destination $qarLogCopy -RunStart $runStarted)
        Save-QarRunLogs -QarLogDir $qarLogDir -QarosLogRoot $qarosLogRoot -RunStart $runStarted
    }
    # Put the calling shell's environment back (see $envSnapshot). rviz2 and the marker
    # publisher were started with their own copy of the environment above; the dry run and
    # a failed start get here too.
    Restore-CallerEnvironment
}
