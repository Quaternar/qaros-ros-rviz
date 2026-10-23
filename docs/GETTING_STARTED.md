# Getting started

## What you will do

This document takes you from the installed plugin to a picture and a clickable panel in the QarOS Visualizer. You start RViz with the plugin through the launcher `scripts\run_live_rviz_stream_real.ps1` and connect the plugin to QarOS. You invite a Visualizer, which shows the scene of RViz as a stereo picture with depth. You stream a panel of RViz into AR and click it with the hand ray, you let the plugin put a copy of the Displays panel of RViz into AR, you expose a ROS 2 topic as a panel in AR and publish from it, you show a video in AR, and you pick a point in the 3D scene. A section on the HoloLens 2 follows, and the section Stop and clean up stops everything again.

The sections after [Stop and clean up](#stop-and-clean-up) run the two examples of the web libraries of this repository, which are built together with the libraries. `qar_web_panel_example` serves an AR panel that it builds by hand from a UI model, a counter and a button, and with the folder of the QarOS SDK it also opens the page in QarOS as a URL panel. `qar_ros_topic_panel_example` does the same for a panel that shows one ROS 2 topic and publishes on another one from a button.

Two programs open windows: RViz, which the launcher starts, and the QarOS Visualizer, which QarOS starts itself when the plugin invites it. You look at the Visualizer and you point and click in it with your own hand; nothing in this repository clicks, moves a window or presses a key for you. Each step below names what it needs and what it opens.

| Step | What it needs | What opens |
|---|---|---|
| [Build and install the plugin](#build-and-install-the-plugin) | the installation of this repository | nothing |
| [Check the launch configuration](#check-the-launch-configuration) | the QarOS SDK folder | nothing |
| [Launch RViz with the plugin](#launch-rviz-with-the-plugin) | the same | RViz |
| [Connect to QarOS](#connect-to-qaros) | the same | nothing |
| [Invite the Visualizer and see the scene](#invite-the-visualizer-and-see-the-scene) | the same | the Visualizer |
| [Stream a panel and click it in AR](#stream-a-panel-and-click-it-in-ar) | the same | nothing |
| [The hidden Displays panel](#the-hidden-displays-panel) | the same | nothing |
| [Expose a ROS topic as an AR tool](#expose-a-ros-topic-as-an-ar-tool) | a second PowerShell window | a dialog of RViz |
| [Video in AR](#video-in-ar) | the same | nothing |
| [Pick a point in the scene](#pick-a-point-in-the-scene) | the same | nothing |
| [Close behaviour](#close-behaviour) | the same | nothing |
| [The same on HoloLens 2](#the-same-on-hololens-2) | a HoloLens 2 | nothing |
| [Stop and clean up](#stop-and-clean-up) | nothing | nothing |

| Command | What it does | Needs | Opens a window |
|---|---|---|---|
| `qar_web_panel_example --help` | prints the options and exits | the Qt folder on `PATH` | no |
| `qar_web_panel_example --serve-seconds 60` | serves the counter panel for 60 seconds and prints the address of its page; `--loopback` keeps the server on this machine | the Qt folder on `PATH` | no |
| `qar_web_panel_example --sdk-bin DIR` | also opens the page as a URL panel in QarOS and invites a Visualizer, and serves for 60 seconds unless `--serve-seconds` says otherwise | the QarOS SDK | the Visualizer opens its own window |
| `qar_ros_topic_panel_example` with the same options | runs a ROS 2 node, shows the newest message of `/qar_example/text` in the panel and publishes on `/qar_example/pressed` from the button | a shell prepared with `env_setup.ps1`; the QarOS SDK for `--sdk-bin` | as above |


The sections after Stop and clean up follow that order. [How the pieces fit](#how-the-pieces-fit) explains what runs where, and the section Running a panel server safely lists what to check before a panel server runs on a network.

## Before you start

Finish [Installation](INSTALL.md) up to and including [Upstream repositories](INSTALL.md#upstream-repositories); the next section repeats the build of the plugin, so that this document runs from zero. The commands run in PowerShell 7 from the clone, `$env:QAR_ROOT\src\qar_rviz_plugin`, and use only the environment variables of the layout. You need the QarOS SDK of [The QarOS SDK](INSTALL.md#the-qaros-sdk): the launcher stops with a message when `QAR_SDK_ROOT` does not name a folder that holds `bin\qar-streaming-c.dll`. RViz and the Visualizer need the desktop of the machine, so work at the console of the machine, as [Machine prerequisites](INSTALL.md#machine-prerequisites) says.

For the examples, finish [Configure, build and test the units](INSTALL.md#configure-build-and-test-the-units) as well. The examples are in the build folder, at `build\vs2019-x64\examples\<example folder>\RelWithDebInfo\<program>.exe`. Go to the folder of the clone first; the paths of the commands below are relative to it:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

**UDP port 7446 must be free.** QarOS uses it for its discovery, and a QarOS session cannot be created while another program holds it. The command prints the programs that hold the port, or says that it is free; it changes nothing. [Run-time problems](TROUBLESHOOTING.md#run-time-problems) explains what to do when a program holds it.

```powershell
$holders = Get-NetUDPEndpoint -LocalPort 7446 -ErrorAction SilentlyContinue
if ($holders) {
    $holders | ForEach-Object { 'UDP 7446 is held by process {0} ({1})' -f $_.OwningProcess, (Get-Process -Id $_.OwningProcess -ErrorAction SilentlyContinue).ProcessName }
} else {
    'UDP 7446 is free'
}
```

```text
UDP 7446 is free
```

**The graphics processor.** When you start RViz with `-Confirm`, the launcher sets the Windows graphics preference of `rviz2.exe` to high performance, which selects the NVIDIA graphics processor on a machine that has one. If RViz rendered on another adapter than the one that QarOS uses, every picture would pass through a copy between the adapters. The preference is a value of your user in the registry, the launcher writes it only with `-Confirm`, and [Removing everything again](INSTALL.md#removing-everything-again) lists it with the way to remove it.

Close every RViz window before the next section, because the build replaces the plugin library that a running RViz has loaded.

The two examples, `qar_web_panel_example` and `qar_ros_topic_panel_example`, create a `QCoreApplication`, open no window of their own and need no platform plugin of Qt; they use the Qt modules Core, Network and WebSockets. Windows finds the DLLs of a program through the folders on `PATH`, so the Qt `bin` folder has to be on `PATH`. The QarOS DLL is not found through `PATH`: the examples load it from the folder that you pass in `--sdk-bin`.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
Get-ChildItem -Path "$env:QAR_QT_ROOT\bin\Qt5Core.dll", "$env:QAR_QT_ROOT\bin\Qt5Network.dll", "$env:QAR_QT_ROOT\bin\Qt5WebSockets.dll" -Name
```

```text
Qt5Core.dll
Qt5Network.dll
Qt5WebSockets.dll
```

The ROS 2 example also needs the DLLs of the ROS 2 distribution on `PATH`: `rclcpp`, the middleware and the typesupport library of a message package, here `std_msgs`, are loaded by name, and `qar_ros_ui` looks for that last library in the folders of `AMENT_PREFIX_PATH` first and then through `PATH`. Dot-source `scripts\env_setup.ps1` in each new shell in which you run it, with a leading dot and a space: it imports the environment of the distribution, which sets both variables, and puts Qt and the virtual environment in front of `PATH`. ROS 2 programs find each other when they use the same domain number, which is 0 unless you set `ROS_DOMAIN_ID`; if other ROS 2 programs run on your network, set a number of your own in the shell, for example `$env:ROS_DOMAIN_ID = '87'`, before you start the second example. The ROS 2 command line of the distribution does not start on another machine, so the commands run the script behind it with Python.

`--help` prints the options and exits with 0; an unknown option prints the same text and exits with 2.

```powershell
. .\scripts\env_setup.ps1
$web = '.\build\vs2019-x64\examples\web_panel_example\RelWithDebInfo\qar_web_panel_example.exe'
$ros = '.\build\vs2019-x64\examples\ros_topic_panel_example\RelWithDebInfo\qar_ros_topic_panel_example.exe'
& $web --help
& $ros --help
```

```text
"[rti_connext_dds_cmake_module][warning] RTI Connext DDS environment script not found (\resource\scripts\rtisetenv_x64Win64VS2017.bat). RTI Connext DDS will not be available at runtime, unless you already configured PATH manually."
...
Usage: qar_web_panel_example [--serve-seconds N] [--loopback] [--port N] [--sdk-bin DIR]
  --serve-seconds N  serve the panel for N seconds, then exit (default 60)
  --loopback         listen on 127.0.0.1 only, so that no other machine can connect (default: all interfaces)
  --port N           TCP port of the panel server (default 0: the operating system picks a free port)
  --sdk-bin DIR      bin folder of the QarOS SDK: also open the page as a URL panel and invite a Visualizer (opens a window)
Usage: qar_ros_topic_panel_example [--serve-seconds N] [--loopback] [--sdk-bin DIR]
  --serve-seconds N  run the node and serve the panel for N seconds, then exit (default 60)
  --loopback         serve the page on 127.0.0.1 only, so that no other machine can open it (default: all interfaces)
  --sdk-bin DIR      bin folder of the QarOS SDK: also open the page as a URL panel and invite a Visualizer (opens a window)
```

## Build and install the plugin

If you have just finished [Build and install qar_rviz_plugin](INSTALL.md#build-and-install-qar_rviz_plugin), skip this section. Otherwise run the two commands: the first builds the plugin with `colcon` and installs it into the prefix of the layout, the second checks without a window that the environment of ROS 2 finds the installed package.

```powershell
.\scripts\build_plugin.ps1
```
```text
=== Importing the Visual Studio 2019 x64 build environment ===
  vcvars64: C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat
...
=== Dot-sourcing env_setup.ps1 ===
...
=== Ensuring downstream ament CMake config gaps are filled ===
...
=== colcon build (Ninja, RelWithDebInfo) ===
...
  Qt5_DIR: C:\qar\deps\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5
  Python3: C:\qar\venv\Scripts\python.exe
  prefix : C:\qar\install
  colcon : C:\qar\src\qar_rviz_plugin\build\colcon
Starting >>> qar_rviz_streaming
...
Finished <<< qar_rviz_streaming [18.3s]
...
Summary: 1 package finished [18.7s]
...
=== BUILD OK ===
Plugin DLL: C:\qar\install\bin\qar_rviz_streaming.dll
plugin_description.xml installed: C:\qar\install\share\qar_rviz_streaming\plugin_description.xml
Overlay (dot-source it): C:\qar\install\local_setup.ps1
```

```powershell
. .\scripts\env_setup.ps1
. $env:QAR_PREFIX\local_setup.ps1
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" pkg prefix qar_rviz_streaming
```
```text
"[rti_connext_dds_cmake_module][warning] RTI Connext DDS environment script not found (\resource\scripts\rtisetenv_x64Win64VS2017.bat). RTI Connext DDS will not be available at runtime, unless you already configured PATH manually."
...
C:\qar\install
```

## Check the launch configuration

The launcher has two modes. Without `-Confirm` it only prints the configuration that it resolved, starts nothing and changes nothing: no window opens, no registry value is written, no process is stopped, no log file is rotated and no Fast-DDS shared memory is cleaned. With `-Confirm` it starts RViz. The environment of your shell is put back when the script ends, in both modes, so nothing that the launcher sets stays in your window. Run it without `-Confirm` first:

```powershell
.\scripts\run_live_rviz_stream_real.ps1
```
```text
...
=== RViz -> QarOS Visualizer live stream ===
  scene            : C:\qar\install\share\qar_rviz_streaming\config\test_scenes\scene3_grid_marker.rviz
  QarOS SDK bin    : C:\QarOS\install\bin
  Visualizer exe   : C:\QarOS\install\bin\qar-streaming-viz.exe
  runtime-launcher : C:\QarOS\install\bin\qar-runtime-launcher.exe
  QAR_RUNTIME_BINARIES_DIR = C:\QarOS\install\bin
  QAR_PANEL_LIVE_STREAM    = 1
  FASTRTPS_DEFAULT_PROFILES_FILE = C:\qar\src\qar_rviz_plugin\scripts\fastdds_large_data.xml (set by this launcher)
  QAR_SCENE_LIGHT          = both (headlight: suppressed while streaming)
...
  QAR_PANEL_INVITE_PEERS   = (none - invite from the panel, or pass -InvitePeers)
...
  install prefix           : C:\qar\install (plugin overlay: found)
  RViz logs (-LogDir)      : C:\Users\<user>\AppData\Local\Temp\qar_rviz_logs
  rviz2.exe                : C:\qar\deps\ros2_humble\ros2-windows\bin\rviz2.exe
  GPU preference (HKCU)    : (not set) - with -Confirm the launcher sets High-performance (NVIDIA) for rviz2.exe
  log mode                 : QUIET (default) - QarOS logs at warn, no zenoh logging, no per-frame traces; -TraceLogs collects full logs for a vendor report.
  QAR_LOG_SEVERITY         = warn
  QAR_PANEL_LOG_SEVERITY   = warn
  QAR_PANEL_LOG_CONSOLE    = off
  RUST_LOG                 = (unset)
  QAR_ZENOH_LOG            = (unset)
  QAR_ADAPTER_TRACE        = (unset)
  QAR_ADAPTER_TRACE_ALL    = (unset)
  QAR_ADAPTER_FRAME_TRACE  = (unset)
...
  QAR log dir              : C:\Users\<user>\AppData\Local\Temp\quaternar (no *.log over 256 MB)
...
  VC++ runtime (rviz2 dir) : 14.50.35719.0 (ok, needs 14.40 or newer)
  tinyxml.dll (rviz2 dir)  : present

DRY RUN (no -Confirm): environment resolved above; NOT launching any GUI (log rotation is only reported, not performed; the registry, running processes and Fast-DDS shared memory are left alone).
To actually run the interactive session, re-invoke with -Confirm (opens windows).
```

The script takes the locations from its parameters `-SdkRoot`, `-Prefix`, `-QtRoot` and `-Ros2Root` or, when you omit a parameter, from `QAR_SDK_ROOT`, `QAR_PREFIX`, `QAR_QT_ROOT` and `ROS2_INSTALL_ROOT`. It prepares the environment of the distribution and Qt with `scripts\env_setup.ps1`, sources the overlay of the plugin, and then prints what it will do, one line for each item:

| Line | What it says |
|---|---|
| **scene** | the RViz configuration that the launcher will load; by default the file `qar_rviz_streaming/config/test_scenes/scene3_grid_marker.rviz`, as the install copies it below `share` in the prefix, which holds a grid and a display of markers; `-Scene` names another file, and the line says `NOT FOUND` when the file is missing |
| **QarOS SDK bin**, **Visualizer exe**, **runtime-launcher** | the `bin` folder of the SDK, and the two programs of QarOS that the runtime starts from it; a warning follows when one of them is missing |
| `QAR_RUNTIME_BINARIES_DIR` | the folder that the plugin gives to QarOS as the place to start its programs from; the launcher also puts it in front of `PATH` |
| `QAR_PANEL_LIVE_STREAM` | `1`: the panel is live-capable, which relabels its buttons, and it connects only when you press a button |
| `FASTRTPS_DEFAULT_PROFILES_FILE` | the profile `scripts\fastdds_large_data.xml` that enlarges the shared-memory segment of Fast-DDS for large messages, unless the variable is already set in your shell, which then wins, or you pass `-NoDdsProfile` |
| `QAR_SCENE_LIGHT` | `both`: the scene is lit by a world-fixed rig and by a light at the head of each peer, and the headlight of RViz is suppressed while the plugin streams |
| `QAR_PANEL_VISUALIZERS`, `QAR_PANEL_INVITE_PEERS` | how many Visualizers the plugin invites when it connects, `0` by default, with the admission policy (`panel setting` unless `QAR_PANEL_PEER_ADMISSION` is set, as `-RealPeers` does) and a note that says whether connecting invites by itself, and the devices of `-InvitePeers` |
| `QAR_PANEL_FIXED_POSES`, `QAR_PANEL_VOLUME_Y` | where the panels hang and at which height the centre of the app volume is, each with its source: the defaults of the launcher, `ground` and `-1.6`, or `environment` when your shell already held a value |
| **test markers**, `QAR_PANEL_TEST_MARKERS_PYTHON`, `QAR_PANEL_TEST_MARKERS_SCRIPT` | whether the publisher of the test markers starts with RViz, which `-TestMarkers` asks for, and the python and the script that the button of the panel runs; the lines say `"(not exported)"` when python or the script is missing |
| **install prefix** | the prefix, and whether the overlay of the plugin was found; without `-Confirm` a missing overlay is only reported |
| **RViz logs (-LogDir)** | the folder of the RViz logs, by default `qar_rviz_logs` in `TEMP` |
| **rviz2.exe**, **GPU preference (HKCU)** | the RViz that the launcher found on `PATH`, and the graphics preference that is stored for it now |
| **log mode** | quiet by default, and the line says so: QarOS logs at the severity `info`, there is no zenoh logging and no per-frame trace; `-TraceLogs` selects the full logs that a bug report needs, which can grow to gigabytes |

Below the table the launcher prints the value of each logging variable and reports what it would rotate in the folder of the QarOS logs. It also checks the machine: it looks at the free space of the drives that hold the logs, asks `nvidia-smi` which other programs use the graphics processor for computing, and reads the free commit charge, and it only warns, because a full drive, a compute job on the graphics processor or a nearly exhausted commit charge can stall every QarOS stream at once. The line **VC++ runtime (rviz2 dir)** gives the version of the `MSVCP140.dll` in the folder of `rviz2.exe` and says whether it is 14.40 or newer, which the QarOS library needs; for an older runtime it says `TOO OLD` and a warning follows that names the file and the command that repairs it, [The QarOS library crashes while it starts](TROUBLESHOOTING.md#the-qaros-library-crashes-while-it-starts) explains why. The line **tinyxml.dll (rviz2 dir)** says `present` when the file `tinyxml.dll` lies in the folder of `rviz2.exe` and `MISSING - run fix_missing_tinyxml_dll.ps1` when it does not: without the file RViz ends with the exit code 0xC0000409 as soon as a robot description reaches the Robot model display, a warning follows that names the file and the command `scripts\fix_missing_tinyxml_dll.ps1` that builds it, and with `-Confirm` the launcher stops before it starts anything. The last lines say that this was a dry run and how to start the real one.

The line **GPU memory (nvidia-smi)** names the memory that the graphics processor has in use. The line **QAR library log copy** names the file `rviz_live_<stamp>.qar_streaming.log`, to which the launcher copies the log of the QarOS library when RViz has ended, because the library starts that log afresh in every session. The parameter `-QarLogSeverity` takes `warn`, `info` or `debug`, sets the log level of QarOS for that run only, and adds a line to the plan when you pass it; without it the level is `info`. The line **QAR process logs copy** names the files `rviz_live_<stamp>.mixer_<id>.log` and `rviz_live_<stamp>.streaming_viz_<id>.log`, to which the launcher copies the logs of the mixers and of the Visualizers of the run when RViz has ended, and the line **QAR crash reports copy** names the copies of the crash reports of the run, `rviz_live_<stamp>.` and the name of the report. The folder of QarOS holds the files of all runs, and the copies keep the files of one run together with the logs of RViz.

The launcher also sets variables that it does not print. `QAR_POSE_WORLD` is always `zup`, because QarOS reports head poses in a world with the y axis up while RViz renders a world with the z axis up, and without the conversion the viewer arrives on its side. `QAR_POSE_YAW_DEG` is `180`, and `QAR_POSE_HEIGHT_M` and `QAR_POSE_BACK_M` are `0`, so that the viewer is turned and never moved. `QAR_DEPTH_ENCODING` is `ndc`, because with an SDK drop earlier than that of 2026-09-28 the Visualizer expected the window-space depth value and not metres. For the last four a value that you set in your shell before you start the launcher wins. [What the launcher sets](CONFIGURATION.md#what-the-launcher-sets) compares them with the defaults of the plugin.

## Launch RViz with the plugin

This is the first step that opens a window. The command starts RViz and waits in its window until RViz ends. Run it in the window in which you ran the dry run, and keep that window, because it shows the paths of the logs:

```powershell
.\scripts\run_live_rviz_stream_real.ps1 -Confirm
```

With `-Confirm` the launcher does these things in order: it reads the version of the C++ runtime next to `rviz2.exe` and stops, before it changes or starts anything, when that version is older than 14.40; it writes the graphics preference of `rviz2.exe` when RViz is on `PATH` and the preference is not high performance already; it renames every QarOS log file over 256 MB to the same name with `.prev` added; it stops the test publishers that earlier runs left behind, which are the Python processes whose command line names `publish_test_markers.py`, `publish_test_tf.py`, `publish_test_robot.py` or `publish_test_pointcloud.py`, also when you started them yourself in another window; it cleans up the Fast-DDS shared memory of programs that have ended; with `-TestMarkers` it starts the publisher of the synthetic markers, `scripts\publish_test_markers.py`, in a hidden window; it warns when UDP port 7446 is held; and it starts `rviz2.exe` with the scene as its configuration. It writes the output of RViz to two files in the log folder, `rviz_live_<stamp>.out.log` and `rviz_live_<stamp>.err.log`, where `<stamp>` is the start time, and the results of its start checks to `rviz_live_<stamp>.checks.txt`. It then waits for RViz to end and copies next to them the log of the QarOS library, the logs of the mixers and of the Visualizers that were written during the run, and the crash reports of the run. The launcher starts nothing else: the QarOS runtime and the Visualizer are started by QarOS itself when you ask for them.

The marker publisher exists so that the scene is not empty: the configuration shows a grid and a display of the markers on the topic `/qar_test_markers`, and without a publisher the stream would carry nothing but the grid. The launcher starts no publisher unless you pass `-TestMarkers`; the button `"Start test markers"` on the tab `Diagnostics` starts the same publisher at any time and stops it again. The publisher is synthetic and needs no data of yours. The one that the launcher started keeps running after RViz has closed, and [Stop and clean up](#stop-and-clean-up) shows how it ends.

RViz opens with the Displays and Views panels and the panel `"Qar Streaming"`. An RViz configuration loads the panel with these two lines in its list of panels, and every configuration that you write for the plugin needs them:

```yaml
# Qar Streaming panel provided by qar_rviz_streaming.
- Class: qar_rviz_streaming/QarStreamingPanel
  Name: Qar Streaming
```

The first line is the class that the file `plugin_description.xml` registers, and the second line is the title of the panel. The configuration `qar_streaming_dev.rviz` of the plugin and the five scenes in the folder `qar_rviz_streaming/config/test_scenes` all carry the entry. If you save a configuration from RViz under the name of one of the shipped files, RViz writes the file again from its own state and drops the comments of the original, so save under a name of your own.

The panel starts in the state `"Qar runtime: STOPPED"` on its tab `Connection`, and the launcher made it live-capable: the two buttons at the top of the tab carry the labels `"Connect to QarOS"` and `"Disconnect (pause)"`, where a panel without a live session shows `"Start Qar"` and `"Stop Qar"`. The panel connects to nothing until you press the first button. [The Qar Streaming panel at a glance](OPERATOR_GUIDE.md#the-qar-streaming-panel-at-a-glance) lists every control.

Everything the panel logs goes to its `Diagnostics` view and, with a time of day in front, to the standard error of RViz: each line of the file `rviz_live_<stamp>.err.log` that starts with `[qar-panel]` is a line of the panel. The commands below read that file, the newest one in the default log folder.

## Connect to QarOS

Press `"Connect to QarOS"`. RViz stays responsive: the plugin builds the session of QarOS on a worker thread, which takes a few seconds, and adopts it on a render tick when it is ready. While it builds, the label shows `"Qar runtime: CONNECTING..."`, and when the session is open it shows `"Qar runtime: RUNNING (live)"`. If you press the button again while the session is being built, the panel only says that the connection is in progress. The plugin does not start the launcher of QarOS itself; the QarOS library starts and configures it from the folder that the launcher passed in `QAR_RUNTIME_BINARIES_DIR`.

If RViz ends as soon as you press the button, read the end of `rviz_live_<stamp>.err.log`: a line about a `SEH FAULT` in `qar_library_init` that names the `MSVCP140.dll` of the distribution means that the C++ runtime next to `rviz2.exe` is too old, and [The QarOS library crashes while it starts](TROUBLESHOOTING.md#the-qaros-library-crashes-while-it-starts) gives the repair.

The panel writes what it is about to do before it starts the worker. The first line of the connection has this form:

```text
Connecting to QarOS runtime (building session; RViz stays responsive)...
```

It follows with the range of depth that the plugin renders and advertises, the logging of the QarOS library, the application volume of the scene, and the invitation and admission settings. The depth range defaults to 0.01 m for the near plane and 150 m for the far plane, and the line has this form:

```text
LIVE: near/far = %1 / %2 m, depth_scale = %3
```

The line that states the invitation settings has this form, with the number of Visualizers that connect invites, the admission policy and the ceiling on the number of peers:

```text
LIVE: auto-invite %1, admission=%2, max_peers=%3
```

When the session is open, the panel says so, and because the launcher invites nothing by default, it adds what to do next. The line has this form, and a sentence about the next step follows it:

```text
Connected to QarOS: session open, streaming active.
```

Connecting also leads to the hidden Displays panel, which is made as soon as the first stream of the 3D scene is open and which [The hidden Displays panel](#the-hidden-displays-panel) describes. The command below prints these lines from the log:

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'Connecting to QarOS|Connected to QarOS|LIVE: ' | ForEach-Object { $_.Line }
```
```text
[qar-panel] 20:34:13.954 Connecting to QarOS runtime (building session; RViz stays responsive)...
[qar-panel] 20:34:13.955 LIVE: near/far = 0.01 / 150 m, depth_scale = 1
[qar-panel] 20:34:13.955 LIVE: QarOS library logging: severity=warn (from QAR_PANEL_LOG_SEVERITY), console duplicate to stdout=off
...
[qar-panel] 20:34:13.955 LIVE: auto-invite OFF (use 'Invite peer' to open a Visualizer), admission=invited, max_peers=8
[qar-panel] 20:34:15.764 Connected to QarOS: session open, streaming active. No peer invited — pick a device kind and click 'Invite peer' to open a Visualizer.
[qar-panel] 20:34:28.187 LIVE: invited visualizer as peer bf2bb6007d (1 invited in total). Its stream opens when the device connects.
[qar-panel] 20:34:28.191 LIVE: sender creation in flight (async) — RViz stays responsive; the stream opens as soon as the peer completes negotiation.
[qar-panel] 20:34:33.100 LIVE: opened stream for peer bf2bb6007d
[qar-panel] 20:39:49.263 Connected to QarOS: streaming active; waiting for the invited peer(s).
...
```

`"Disconnect (pause)"` does not disconnect. It pauses the streaming and keeps the session open, and the log shows the line `"QarOS: streaming paused (session kept open)."` Pressing `"Connect to QarOS"` again resumes the streaming on the open session: the panel runs the steps of the connection again without building a session, and the log shows a line `"Connected to QarOS:"` once more. The line `"QarOS: streaming resumed."` is the line of a press on `"Connect to QarOS"` while the panel is still connected, that is, a press with no pause before it; [Connect, invite and session lines](LOG_REFERENCE.md#connect-invite-and-session-lines) describes the lines. The session ends when RViz closes.

## Invite the Visualizer and see the scene

The group `"Invite a device"` on the tab `Connection` has a drop-down with the four kinds of device that QarOS can invite, `Visualizer`, `"HoloLens 2"`, `"Meta Quest"` and `"ZED camera"`, a field for the host name, and the button `"Invite peer"`, which is grey until the connection is open. Leave `Visualizer` selected and the field empty, and press `"Invite peer"`. QarOS starts the Visualizer from the `bin` folder of the SDK, and its window opens on this machine.

The invitation is a synchronous call into QarOS on the GUI thread of RViz. RViz does not repaint while the call runs, so the panel can stand still for the duration of the call. The same holds when the panel creates a QarOS panel, which [Stream a panel and click it in AR](#stream-a-panel-and-click-it-in-ar) shows; only the connection of the previous section runs on a worker thread.

The panel writes a line for the invitation that has this form:

```text
LIVE: invited %1%2 as peer %3 (%4 invited in total). Its stream opens when the device connects.
```

Each invitation of a Visualizer carries a fresh identifier by default, so that every invitation opens a Visualizer of its own, and the panel adds a note about that. The Visualizer then asks for a stream. The plugin creates the sender for the peer in the background, so that RViz stays responsive; until the receiver has finished negotiating, the sender does not exist yet, and the plugin writes one line about that. When the stream opens, the line has this form:

```text
LIVE: opened stream for peer %1
```

What you see in the Visualizer is the scene of RViz: the grid and the synthetic markers. The plugin renders the scene once for each peer, from the head pose that QarOS reports for that peer, and sends a colour picture and a depth picture for each eye, so the view in the Visualizer follows the pose of the Visualizer and not the camera of RViz; orbiting the camera of RViz changes only the picture on your desktop.

Every two seconds the panel writes a heartbeat line, which starts with the word `HB` and has one field for each stream. The line is the place to look at when you wonder whether pictures flow: the stream of the peer shows its frame rate and the number of frames submitted. The beginning of the line has this form:

```text
HB dt=%1s ticks=%2 tick_fps=%3 max_gap=%4ms
```

After it come `streams=`, the average time of a batch of renders and the number of frames that the render budget shed, and then one field for each peer that starts with the first characters of its identifier. [The heartbeat line](LOG_REFERENCE.md#the-heartbeat-line) explains every field.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'LIVE: invited|LIVE: opened stream|HB dt=' | Select-Object -First 8 | ForEach-Object { $_.Line }
```

If the Visualizer does not appear, [Nothing or the wrong picture in QarOS](TROUBLESHOOTING.md#nothing-or-the-wrong-picture-in-qaros) lists the causes.

## Stream a panel and click it in AR

The tab `"AR panels"` streams a Qt window of RViz into AR as a QarOS GUI panel. Its drop-down lists the windows that can be streamed: first `"This QAR panel"`, which is the panel of the plugin itself, then every dock of RViz, such as the Displays and Views panels. Keep `"This QAR panel"` selected and press `"Stream panel to AR"`.

The press does no call into QarOS. The plugin creates the QarOS panel on the next render tick, on the GUI thread, with synchronous calls into QarOS, which is why RViz can stand still for a moment then. The panel logs the request, and the line has this form:

```text
AR PANEL: streaming '%1' as a QarOS GUI panel. The QarOS panel itself is created on the next tick, off this click.
```

The top edge of the first panel hangs 1 m in front of the origin of the room, at the height of 1.5 m above the origin of the room or, with `ground`, which the launcher sets, 1.5 m above the RViz ground, and every further panel is placed 0.65 m further along the x axis, because no call of QarOS asks where you look: the pose of the viewer comes only with a render sender, and the plugin uses it only to place the AR control panel. Before a dock is streamed, the plugin fits it to its content, so that the AR panel shows no scroll bar, and the panel appears a moment after the press. Find the panel in the Visualizer: it shows the picture of the panel of the plugin and repaints when the panel changes. Point at the button `"Refresh diagnostics"` with the hand ray and click it. The click reaches the real Qt button, and the panel writes a line of diagnostics that has this form:

```text
Diagnostics: streaming=%1 streams=%2 render_ticks=%3 batches=%4
```

The readout `"AR panels:"` of the same tab counts what arrives. Its first line says how many pointer samples reached the library, how many pointed at no panel and how many the state machine refused; `in 0` after you clicked means that QarOS sent nothing, and a number above 0 means that the clicks arrive. For each panel the readout shows a short line with the size of the buffer, the frames sent and the words that apply to the panel.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'AR PANEL: streaming|qar-panel-adapter\] .* created:|Diagnostics: streaming' | ForEach-Object { $_.Line }
```
```text
[qar-panel] 20:45:09.407 AR PANEL: streaming 'qar_streaming_panel' as a QarOS GUI panel. The QarOS panel itself is created on the next tick, off this click.
[qar-panel-adapter] 20:45:09.520 panel 'qar_streaming_panel' created: id=d79dd4df-96d3-4948-8a7f-187c81fa08a2 stream=1f4c263c-743f-49ac-b6dd-2c8fed0e85ec buffer=382x1318 px
...
```

A click and a hover reach the real widget of RViz, which the authors of qar_rviz_plugin confirmed in earlier live sessions, held with older builds before the SDK drop of 2026-09-28, together with the lists of drop-down boxes and the dialogs. The unit tests also cover the dragging of a slider and the use of two hands, but no live session has exercised them yet, so do not rely on them; [What the live sessions showed](VERIFICATION.md#what-the-live-sessions-showed) lists the evidence.

## The hidden Displays panel

As soon as the first stream of the 3D scene is open, the plugin creates a second copy of the Displays panel of RViz and streams it into AR under the title `"Displays (AR)"`, and a start of the copy after a close in the same RViz process gives it a number after the title. The copy is shown off-screen only, so it never appears on your desktop, and it is bound to the same display tree as the real Displays panel: switching a display on or off, editing a property, or adding or removing a display in the headset changes the displays of RViz. Find it in the Visualizer, at x = -1.2 m, next to the first panel that you streamed, and switch the grid off and on; the grid on your desktop follows.

The drop-down lists, menus and completer lists that the copy opens appear in AR and never on your desktop; tooltips on Windows are not covered. A dialog is the exception: the dialog that its `Add` button opens is an ordinary window of RViz on your desktop and, at the same time, a panel of its own in AR, 0.5 m along the x axis from the copy, and the copy is blocked while the dialog is up. The panel writes one line when it creates the copy, which has this form:

```text
AR PANEL: created a hidden RViz '%1' panel (%2): %3x%4 px (%8), shown off-screen
```

The copy starts at 480 by 756 pixels and is fitted to its display tree before it streams, unless the variable `QAR_PANEL_HIDDEN_DISPLAYS_SIZE` gives a size. The variable `QAR_PANEL_HIDDEN_DISPLAYS` set to `off`, or the unticked box `"Start the hidden Displays copy when connecting"`, stops only the automatic start when the plugin connects, and then the last entry of the drop-down of the tab `"AR panels"` still starts the copy by hand. The entry has a title that starts with `"Displays (AR copy "` and ends with `"hidden in RViz)"`. [The hidden Displays panel](OPERATOR_GUIDE.md#the-hidden-displays-panel) describes the starting and the stopping.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'AR PANEL: .*hidden' | ForEach-Object { $_.Line }
```

## Expose a ROS topic as an AR tool

An AR tool is a panel that the plugin generates from a topic, a service or an action of the ROS 2 graph. It is a web page that the browser of QarOS opens as a URL panel, and its controls publish, call and send goals. The page of a topic has a part that shows the latest message and, when you allow it, a part that publishes. This section uses a velocity topic that a command publishes, so that you need no robot.

Open a second PowerShell window and prepare it with the two lines below: the first goes to the folder of the clone, and the second, with the leading dot and the space, imports the environment of the ROS 2 distribution, which the ROS 2 programs need.

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
. .\scripts\env_setup.ps1
```
```text
...
=== Qar RViz2 environment set up in this PowerShell session ===
...
python  -> C:\qar\venv\Scripts\python.exe
...
```

In that window run the command. It publishes an all-zero `Twist` on the topic `/qar_demo/cmd_vel` once a second and ends after 300 messages, which is five minutes:

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" topic pub --rate 1 --times 300 --wait-matching-subscriptions 0 /qar_demo/cmd_vel geometry_msgs/msg/Twist '{}'
```
```text
...
publisher: beginning loop
publishing #1: geometry_msgs.msg.Twist(linear=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0), angular=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0))

publishing #2: geometry_msgs.msg.Twist(linear=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0), angular=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0))
...
publishing #299: geometry_msgs.msg.Twist(linear=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0), angular=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0))

publishing #300: geometry_msgs.msg.Twist(linear=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0), angular=geometry_msgs.msg.Vector3(x=0.0, y=0.0, z=0.0))
```

On the tab `"AR tools"` press `"Add from ROS graph..."`. The dialog `"Add from the ROS graph"` lists the topics, services and actions that the graph shows, as trees by namespace, with their types; the field `Filter:` narrows the list by name or type, and `Refresh` asks the graph again. Select `/qar_demo/cmd_vel` under `Topics` and tick `"Expose as a QAR AR tool"` and `"Allow publishing from AR"`. The second box is off by default, because a publication to a topic is a command to a robot, and it is enabled only for an exposed topic. The line under the tree says what the button will do, here `"Will generate an AR panel with a publish form."`, and `Add` accepts it.

The panel writes a line when it starts the web server of the tools, which happens on the first tool and which says where the pages are served, and a line when the tool exists. The line for the tool has this form:

```text
AR TOOL: exposed %1 as '%2' (%3) at %4. The QarOS panel is created on the next tick, off this click.
```

The list of tools shows a row for the topic with its type, and the marks `"[in AR]"` after the QarOS panel exists and `"[writable]"` because you allowed publishing. The readout `"AR tools: none"` changes to a count and the address that serves the pages. In the Visualizer a panel appears with the title `/qar_demo/cmd_vel`: the plugin knows the type `Twist` and lays out a template for it, with a button `STOP` above the sliders `Forward`, `Strafe` and `Turn`, and a button `Publish`. The template is called `velocity-command`.

`STOP` publishes an all-zero command at once, with one press. The sliders only stage values in a draft: `Publish` sends the draft, and it asks for a second press within a short time, because it is the button that commands the robot. Set `Forward` to a value above zero, press `Publish` twice, and read the topic in the second window. The command prints the first message whose forward speed is not zero and ends:

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" topic echo --once --no-daemon --filter "m.linear.x != 0.0" /qar_demo/cmd_vel geometry_msgs/msg/Twist
```

An AR tool serves its page on all network interfaces, so that a headset on the network can open it, and a token in the address is the only protection. [AR tools from the ROS graph](OPERATOR_GUIDE.md#ar-tools-from-the-ros-graph) lists the rules for publishing and the buttons of the list of tools.

## Video in AR

A video reaches AR in two ways. Both need a topic with images, and this section publishes a synthetic clip, so that you need no camera and no recording. The first command makes the clip with the OpenCV of the virtual environment: a one-minute video with a circle that moves across a coloured background and a frame counter. It writes the file `qar_test_clip.mp4` into the folder `demo` of the layout root, and it needs no input:

```powershell
New-Item -ItemType Directory -Force -Path "$env:QAR_ROOT\demo" | Out-Null
& "$env:QAR_VENV\Scripts\python.exe" -c "
import sys
import cv2
import numpy as np
writer = cv2.VideoWriter(sys.argv[1], cv2.VideoWriter_fourcc(*'mp4v'), 30.0, (1280, 720))
for i in range(1800):
    frame = np.full((720, 1280, 3), (40 + i % 100, 60, 90), np.uint8)
    cv2.circle(frame, (100 + 4 * i % 1080, 360), 60, (255, 255, 255), -1)
    cv2.putText(frame, 'frame %d' % i, (40, 80), cv2.FONT_HERSHEY_SIMPLEX, 2, (255, 255, 255), 3)
    writer.write(frame)
writer.release()
" "$env:QAR_ROOT\demo\qar_test_clip.mp4"
```

The second command, in the prepared second window, publishes the clip as raw images of the type `sensor_msgs/msg/Image` on the topic `/qar_test_video/image_raw`, at the frame rate of the video. The parameter `loop` set to `false` plays the clip once: at the end of the clip the publisher logs that it stops, and the program then keeps running until you press Ctrl+C in its window; without the parameter the clip repeats until you press Ctrl+C. The publisher applies the Fast-DDS profile of the launcher to itself, so that the large messages are delivered smoothly.

```powershell
python scripts\publish_video_file.py "$env:QAR_ROOT\demo\qar_test_clip.mp4" --ros-args -p loop:=false
```
```text
[INFO] [<time>] [qar_video_file_publisher]: Streaming 'C:\qar\demo\qar_test_clip.mp4' on /qar_test_video/image_raw (raw bgr8) at 30.00 fps (loop=False, reliable KeepLast(10))
[INFO] [<time>] [qar_video_file_publisher]: DDS: Fast-DDS profile C:\qar\src\qar_rviz_plugin\scripts\fastdds_large_data.xml (64 MB shared-memory segment)
[INFO] [<time>] [qar_video_file_publisher]: DDS: publication mode ASYNCHRONOUS
[INFO] [<time>] [qar_video_file_publisher]: SCHED: process power-throttling opt-out applied (full speed, timer resolution always honoured)
[INFO] [<time>] [qar_video_file_publisher]: Actual achieved publish rate: 29.62 fps (149 frames in 5.03s) -- avg read+convert=2.0 ms, avg pub.publish()=0.7 ms
...
[INFO] [<time>] [qar_video_file_publisher]: Reached end of video, stopping (loop=false). Ctrl+C to exit.
```

**First way: the button on the page of the topic.** Press `"Add from ROS graph..."`, select `/qar_test_video/image_raw` and tick `"Expose as a QAR AR tool"`. The line under the tree says `"No RViz display will be created — the AR panel replaces it."`, because exposing an image topic must not add a display that you did not ask for. The panel of the tool shows the size, the encoding and the stride of the images, and says that the pixels are not sent over the page; it has the button `"Open video in AR"`. Press it in the Visualizer: a second panel opens with the title `/qar_test_video/image_raw` and shows the video at up to 30 frames per second. The video is received in a ROS context of its own, so that an RViz display of the same topic cannot starve it.

**Second way: a dock of RViz.** In the Displays panel of RViz press `Add`, choose By display type, select `Image`, and set the topic of the display to `/qar_test_video/image_raw`. RViz shows the images in a dock of its own, and the render window of an Image display cannot be captured as a picture of the window. Press `Refresh` on the tab `"AR panels"`, choose the dock of the Image display in the drop-down and press `"Stream panel to AR"`: the plugin finds the Image display in the dock, subscribes to its topic and paints the video into the AR panel directly, at up to 30 frames per second.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'AR VIDEO: ' | Select-Object -First 6 | ForEach-Object { $_.Line }
```
```text
[qar-panel] 19:42:59.760 AR VIDEO: video topics are received in their own ROS context (a DDS participant separate from RViz's), so RViz's own displays of the same topic cannot starve them.
[qar-panel] 19:42:59.765 AR VIDEO: opened a new video panel for '/qar_test_video/image_raw' (sensor_msgs/msg/Image).
[qar-panel] 19:42:59.886 AR VIDEO: 'video:/qar_test_video/image_raw' msgs=3 decoded=3 dropped=0 mailbox_pub=3 avg_decode_ms=1.2
[qar-panel] 19:42:59.886 AR VIDEO: 'video:/qar_test_video/image_raw' tick: captures=1 unchanged=0 not_ready=1 submitted=0 avg_capture_ms=8.8 submit_gap_max_ms=0.0 submit_gap_avg_ms=0.0 fg=0
[qar-panel] 19:43:01.887 AR VIDEO: 'video:/qar_test_video/image_raw' msgs=63 decoded=63 dropped=0 mailbox_pub=63 avg_decode_ms=1.4
[qar-panel] 19:43:01.888 AR VIDEO: 'video:/qar_test_video/image_raw' tick: captures=60 unchanged=0 not_ready=15 submitted=45 avg_capture_ms=1.9 submit_gap_max_ms=133.3 submit_gap_avg_ms=37.5 fg=0
```

When a video stutters, [ROS data problems](TROUBLESHOOTING.md#ros-data-problems) explains what to check, and [Timing CSV files](LOG_REFERENCE.md#timing-csv-files) explains how to measure which stage loses frames.

## Pick a point in the scene

The tab `Picking` turns the hand ray of the headset into ROS messages. It starts with the mode `Off`, so that a gesture can never move a robot because a mode was left on. Choose the mode `"Pick a point -> /clicked_point"` in the drop-down `Mode`. The drop-down `Target` offers `"Action Point (QarOS scene intersect)"`, which is the default, and two approximations that need a straight ray; leave it. The field `Frame` is a text field that holds `map` at the start; it names the frame in the header of every pick, and it does not follow the Fixed Frame of RViz, so change it when your scene uses another frame. Leave the check box `"Publish without confirming"` unchecked.

In the Visualizer aim the hand ray at one of the markers or at the floor and click. The point that QarOS reports is the intersection that the gesture acts on, which QuaternAR states is an intersection with the scene of the app volume (the SDK does not document which geometry is intersected), and the plugin adds an opaque floor under the grid, so that a click can hit the floor between the lines. The status label of the tab now shows the pick and says that it waits for your confirmation, and the button `"Confirm pick"` is enabled. Press it. The plugin publishes a `PointStamped` on `/clicked_point`, as the tool `Publish Point` of RViz does. In the second window the command prints that message and ends:

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" topic echo --once --no-daemon /clicked_point geometry_msgs/msg/PointStamped
```

Start the command before you press `"Confirm pick"`, because it waits for a message. The check passes when the point lies near the object or the spot of the floor that you aimed at: about 0.1 to 0.2 m at a distance of a few metres is a fair tolerance. The authors of qar_rviz_plugin have no measured figure for the accuracy of a pick.

A click that hits nothing is refused. When QarOS finds no intersection, it hands back the start point of the gesture, which is your hand, as the action point, and the plugin refuses to publish your hand as a picked point. The counters line of the status label then shows `clicks-on-nothing=` with the number of such clicks, and the reason has this form:

```text
the runtime intersected nothing (action point == start point, i.e. the hand) - nothing to pick
```

[Spatial picking](OPERATOR_GUIDE.md#spatial-picking) describes the other modes: a navigation goal, an initial pose and areas.

## Close behaviour

A window that you stream into AR follows the life of its window in RViz. In the drop-down of the tab `"AR panels"` choose Views and press `"Stream panel to AR"`, and then close the Views dock of RViz with its close button. With hiding on, which is the default, the AR panel is hidden and parked: nothing is closed in QarOS, the panel writes a line that says that the panel is parked, and pressing `"Stream panel to AR"` for Views again shows the same panel. The same happens when a dialog that was streamed closes, and when you press `"Stop streaming"` with the panel selected in the drop-down. With closing selected, which the variable `QAR_PANEL_AR_VISIBILITY` set to `close` does, the AR panel closes instead, and the panel writes a line that says the close was requested and that the QarOS panel is closed and its video sender destroyed; this is the close mode `destroy`. The variable `QAR_PANEL_CLOSE_MODE` set to `notice` keeps the AR panel after its window closes, as a notice that says `"This window has been closed in RViz, so its picture is no longer live."`, and keeps the video sender until the session ends.

You can also close a panel in AR with the controls of QarOS. The plugin notices that, skips its own call to close the panel, shows the mark `"closed in AR"` in the readout, and counts the panel in `arclosed=` of the heartbeat. The window in RViz stays open, so the plugin keeps the stream entry until that window closes; close the window and stream it again to bring the panel back. The copy of the Displays panel is the exception, because its window is never closed: pick the copy in the drop-down and press `"Stream panel to AR"` to bring it back.

With closing selected, the log of RViz holds the steps of each close, and the command below prints them; with hiding on, the panel writes the line that says that the panel is parked, which [AR panel lines](LOG_REFERENCE.md#ar-panel-lines) lists.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'AR PANEL: close|AR PANEL: .* was not streaming|qar-panel-adapter\] .* (closing|retired):' | ForEach-Object { $_.Line }
```
```text
...
[qar-panel-adapter] 20:46:06.301 panel 'dialog:AddDisplayDialog' closing: id=b66e358e-a28f-4081-b561-a4615f65c664 stream=c876632c-0b25-4480-a474-1ced9225c1b2 (operator already closed it in AR: no)
[qar-panel-adapter] 20:46:06.305 panel 'dialog:AddDisplayDialog' retired: id=b66e358e-a28f-4081-b561-a4615f65c664 stream=c876632c-0b25-4480-a474-1ced9225c1b2 took=3ms -- GUI panel closed, video sender destroyed
[qar-panel-adapter] 20:46:48.104 panel 'qar_streaming_panel' closing: id=d79dd4df-96d3-4948-8a7f-187c81fa08a2 stream=1f4c263c-743f-49ac-b6dd-2c8fed0e85ec (operator already closed it in AR: no)
[qar-panel-adapter] 20:46:48.109 panel 'qar_streaming_panel' retired: id=d79dd4df-96d3-4948-8a7f-187c81fa08a2 stream=1f4c263c-743f-49ac-b6dd-2c8fed0e85ec took=4ms -- GUI panel closed, video sender destroyed
[qar-panel] 20:46:48.109 AR PANEL: close requested for 'qar_streaming_panel' (stopped in RViz): its AR panel is closed and its video sender destroyed — the [qt_lib] 'closed' line has each step. Starting it again makes a new AR panel.
...
```

[Closing panels](OPERATOR_GUIDE.md#closing-panels) lists the cases and the steps of the close in the log.

## The same on HoloLens 2

The authors of qar_rviz_plugin state that an earlier build of the plugin streamed to a HoloLens 2 with the QarOS SDK drop of 2026-09-28, the drop that this release is verified against; no record of that run exists. The steps of this section were not run against a device while this documentation was written, because no device was available, so the output of a device is given as lines quoted from the code and not as recorded output.

A HoloLens is the one kind of device that QarOS can address by name, so the panel asks for its host name. Choose `"HoloLens 2"` in the drop-down of the group `"Invite a device"`, type the host name of the device in the field next to it, and press `"Invite peer"`. Without a host name the panel refuses, and its message has this form at the beginning:

```text
Invite peer: a HoloLens needs its device hostname
```

You can also invite devices when the plugin connects, with the launcher parameter `-InvitePeers`: entries of the form `kind[:hostname]`, separated by commas, where the kinds are `visualizer`, `hololens`, `quest` and `zed`, and only `hololens` takes, and needs, a host name. A device that fails to invite is logged and skipped, and never ends the session. The dry run shows the entry in the line `QAR_PANEL_INVITE_PEERS`; replace the host name of the example with the one of your device:

```powershell
.\scripts\run_live_rviz_stream_real.ps1 -InvitePeers hololens:HOLOLENS-A2B3
```
```text
...
=== RViz -> QarOS Visualizer live stream ===
  scene            : C:\qar\install\share\qar_rviz_streaming\config\test_scenes\scene3_grid_marker.rviz
  QarOS SDK bin    : C:\QarOS\install\bin
  Visualizer exe   : C:\QarOS\install\bin\qar-streaming-viz.exe
  runtime-launcher : C:\QarOS\install\bin\qar-runtime-launcher.exe
  QAR_RUNTIME_BINARIES_DIR = C:\QarOS\install\bin
  QAR_PANEL_LIVE_STREAM    = 1
  FASTRTPS_DEFAULT_PROFILES_FILE = C:\qar\src\qar_rviz_plugin\scripts\fastdds_large_data.xml (set by this launcher)
  QAR_SCENE_LIGHT          = both (headlight: suppressed while streaming)
...
  QAR_PANEL_INVITE_PEERS   = hololens:HOLOLENS-A2B3
...
  install prefix           : C:\qar\install (plugin overlay: found)
  RViz logs (-LogDir)      : C:\Users\<user>\AppData\Local\Temp\qar_rviz_logs
  rviz2.exe                : C:\qar\deps\ros2_humble\ros2-windows\bin\rviz2.exe
  GPU preference (HKCU)    : (not set) - with -Confirm the launcher sets High-performance (NVIDIA) for rviz2.exe
  log mode                 : QUIET (default) - QarOS logs at warn, no zenoh logging, no per-frame traces; -TraceLogs collects full logs for a vendor report.
  QAR_LOG_SEVERITY         = warn
  QAR_PANEL_LOG_SEVERITY   = warn
  QAR_PANEL_LOG_CONSOLE    = off
  RUST_LOG                 = (unset)
  QAR_ZENOH_LOG            = (unset)
  QAR_ADAPTER_TRACE        = (unset)
  QAR_ADAPTER_TRACE_ALL    = (unset)
  QAR_ADAPTER_FRAME_TRACE  = (unset)
...
  QAR log dir              : C:\Users\<user>\AppData\Local\Temp\quaternar (no *.log over 256 MB)
...

DRY RUN (no -Confirm): environment resolved above; NOT launching any GUI (log rotation is only reported, not performed; the registry, running processes and Fast-DDS shared memory are left alone).
To actually run the interactive session, re-invoke with -Confirm (opens windows).
```

With `-Confirm` added, the launcher starts RViz and the invitation goes out when you press `"Connect to QarOS"`. The device has to be reachable on the network, and what the network has to allow is in the next part.

This section lists what the network has to allow for this stack. It does not repeat the network requirements of QarOS itself, and it adds only the requirements that come from the programs of this stack.

### What QarOS needs

QarOS has its own ports, a multicast discovery and an encrypted session transport between the machine that runs it, the QarOS applications and the devices. They differ between QarOS releases, so they are listed only in QuaternAR's networking documentation for the release you run; this documentation names none of them. Everything here assumes that the machine and the devices are on one local network: QarOS discovery is for local networks only and must never be exposed to the internet.

### What this stack adds

**UDP port 7446 must be free on the machine that runs the program.** QarOS uses it for its discovery (zenoh scouting), and a QarOS session cannot be created while another program holds it. ROS 2 can take it by accident, see the next paragraph; "Run-time problems" in the troubleshooting document shows how to find the holder.

**ROS 2 uses a range of UDP ports of its own.** Each ROS 2 participant takes a pair of UDP ports in steps of two upward from 7410 + 250 x N, where N is the number of its domain, so from 7410 on domain 0, the default domain, and the participants of one domain find each other by multicast on the local network. A machine that publishes or subscribes to topics of another machine needs the same domain number as that machine and a network that passes the multicast discovery between them. A single machine needs no firewall rule for ROS 2 traffic between its own programs.

**The web panel server listens on a TCP port that you may have to open.** The server of qar_rviz_plugin serves one HTTP and WebSocket listener, and the page and its socket use the same port, so one firewall rule covers both. A panel is reached at an address of the form `http://<host>:<port>/p/<token>/`, and the page opens its socket at `ws://<host>:<port>/ws/<token>`. The traffic is not encrypted; the token in the address is a random 128-bit value, and a wrong or missing token is answered with a 404 without a hint whether the token exists.

By default the server listens on every network interface, so that a headset on the network can reach it, and on a port that the operating system picks, so that two programs on one machine never collide. The address that appears in the panel URL is the one the machine uses to reach the local network, unless you set another. If a firewall asks about the program, allow the program for the private network; if your firewall needs a fixed rule, pin the port.

A program that hosts the server pins the port with the `port` field and sets the host name in the panel URLs with the `advertised_host` field of the server configuration; the `bind_address` field restricts it to loopback when only a program on the same machine has to reach it.
The plugin pins the port with the environment variable `QAR_PANEL_WEB_PORT` and sets the host name in the panel URLs with `QAR_PANEL_WEB_HOST`; unset, it uses the defaults above.

**Admission.** A device that you invite, from the panel or with `-InvitePeers`, is admitted by the exact identifier that the invitation returned, under every policy, so it needs no timing heuristic. The default policy, `invited`, admits only those, which is right for the Visualizer that the plugin invited itself, because the subscription of QarOS also reports the internal peers of the runtime. A device that joins by itself, through the onboarding of QarOS, has never been invited by the plugin, and the default policy silently refuses it. `-RealPeers` sets the policy `new` for such a demo: the peers that are present in the first seconds after the session starts, 5 seconds by default, count as the infrastructure of the runtime, and a peer that appears later is treated as a consumer and gets a stream. Inviting is the better way, because the policy `new` depends on timing.

When the device connects, the panel writes the lines that the Visualizer produced in [Invite the Visualizer and see the scene](#invite-the-visualizer-and-see-the-scene): the invitation, the opening of the stream, and the heartbeat with a field for the new peer. The line of the invitation has this form for a device:

```text
LIVE: invited %1%2 as peer %3 (%4 invited in total).
```

The command below reads those lines. It is a live check against a device, and this document gives no output for it because no device has been available:

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'LIVE: invited|LIVE: opened stream|LIVE: closed stream|refused: already streaming' | ForEach-Object { $_.Line }
```

## Stop and clean up

Close RViz. The launcher waits for RViz in its window and ends with it, after it has saved the log of the QarOS library, the logs of the mixers and of the Visualizers and the crash reports of the run, so there is nothing to stop in that window. The session of QarOS ends when RViz closes: the panel tears down its streams and panels before it ends the session. RViz can crash while it exits; that is a known issue that the authors of qar_rviz_plugin tolerate, and [RViz and plugin problems](TROUBLESHOOTING.md#rviz-and-plugin-problems) lists it.

The one program that the launcher can start besides RViz keeps running: with `-TestMarkers`, the publisher of the synthetic markers, in a hidden window. The launcher printed its process id when it started it. The publisher that the button `"Start test markers"` started ends when you press the button again or when RViz closes. Stop it with `Stop-Process` and that number, or start the launcher again, which stops the test publishers of earlier runs by itself. The publisher of the velocity ends by itself after its 300 messages, as the command of this document is written; the publisher of the video keeps running after the end of the clip, so press Ctrl+C in its window.

A run leaves two kinds of logs. The logs of RViz are in the folder `qar_rviz_logs` of `TEMP`, together with the copies of the QarOS logs of the run, and the launcher never rotates them. The logs of the QarOS library are in the folder `quaternar` of `TEMP`, and with trace logging they grow to gigabytes; the launcher renames every file over 256 MB there to the same name with `.prev` added when you start it with `-Confirm`. The command lists the newest files of both folders with their sizes:

```powershell
Get-ChildItem -Path "$env:TEMP\qar_rviz_logs", "$env:TEMP\quaternar" -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 8 | ForEach-Object { '{0,8:N1} MB  {1}  {2}' -f ($_.Length / 1MB), $_.LastWriteTime.ToString('s'), $_.FullName }
```
```text
     0.5 MB  YYYY-MM-DDT20:53:08  C:\Users\<user>\AppData\Local\Temp\quaternar\mixer_1b797fb9-0178-4449-8eb3-e192bad32a3c_stdout.log
     0.1 MB  YYYY-MM-DDT20:53:07  C:\Users\<user>\AppData\Local\Temp\quaternar\streaming_viz_bf2bb600-7dce-4ded-81f1-9061a69591c4_stdout.log
     1.4 MB  YYYY-MM-DDT20:53:07  C:\Users\<user>\AppData\Local\Temp\quaternar\mixer_323b42fc-0f3c-4459-9ab0-9a2aad9c203b_stdout.log
     2.3 MB  YYYY-MM-DDT20:53:07  C:\Users\<user>\AppData\Local\Temp\qar_rviz_logs\rviz_live_20261011_203404.err.log
     0.0 MB  YYYY-MM-DDT20:52:56  C:\Users\<user>\AppData\Local\Temp\quaternar\streaming_viz_1b9facce-e064-4c2e-b1d2-5c0201963373_stdout.log
     0.0 MB  YYYY-MM-DDT20:50:19  C:\Users\<user>\AppData\Local\Temp\qar_rviz_logs\rviz_live_20261011_203404.out.log
     0.0 MB  YYYY-MM-DDT20:50:12  C:\Users\<user>\AppData\Local\Temp\quaternar\20261011-205012_qar-stereokit-gui_41456.crash.txt
     0.0 MB  YYYY-MM-DDT20:50:12  C:\Users\<user>\AppData\Local\Temp\quaternar\20261011-205012_qar-stereokit-gui_25548.crash.txt
```

Delete the files that you do not need. If programs ended badly, so that a large topic stops being delivered, remove the shared-memory segments of the dead programs with the command below, which only touches segments whose program has ended; the launcher does the same with `-Confirm`.

```powershell
python "$env:ROS2_INSTALL_ROOT\tools\fastdds\fastdds.py" shm clean
```
```text
...
shm.clean:
1 ports in use
1 segments in use
4 zombie ports cleaned
111 zombie segments cleaned
```

## Serve a UI-model panel

The first example builds its panel in code. A panel is a tree of widgets with the data that the widgets show at the start. The root is a `Panel`, which gives the page its title; the value display is bound to the path `count` of the data document, so it shows whatever the document holds there; and the button carries an action id, which is what the page sends back when somebody presses it. A value display writes every number with three decimals unless the widget says otherwise, so the example sets the precision to 0.

```cpp
// A widget tree and the data it starts with. The value display is bound to the path "count" of the data
// document; the button carries an action id, which the page sends back when the button is pressed.
ui::UiModel make_model()
{
  ui::UiModel model;
  model.root = ui::widgets::panel("counter_panel", "Counter");
  model.root.add(ui::widgets::value("count", "count", "Presses").with_precision(0));  // no decimals
  model.root.add(ui::widgets::button("increment", "Increment", "increment"));
  model.initial_data = ui::UiValue::object().set("count", 0);
  return model;
}
```

A control sink answers the button. When a viewer presses it, the page sends a control event; the server calls the sink on the thread that owns the session, with the id of the widget and its action id. The sink counts, puts the new count into the document with `set_value`, which applies it to the copy of the server first and then sends it to every viewer as one data frame, and acknowledges the event, which tells the page that the action was handled.

```cpp
// Receives the control events of every viewer, on the thread that owns the session.
struct CounterSink : wp::IPanelControlSink
{
  void on_control(wp::PanelSession & session, const wp::PanelControlEvent & event) override
  {
    if (event.control.action != "increment") { session.ack(event, false, "unknown action"); return; }
    session.set_value("count", ++count);  // the document of the server first, then every viewer
    session.ack(event, true);
    std::printf("example: increment: count %lld\n", static_cast<long long>(count));
  }
  std::int64_t count = 0;
};
```

The server is configured with a few fields: it binds every network interface on a port that the operating system picks, and `--loopback` and `--port` change that. `start` begins to listen. `add_panel` renders the model once and refuses it, with the first problem, when it does not validate; otherwise it returns the session of the panel, which holds the token. `page_url` builds the address of the page from the advertised host, the port and the token. The sink is borrowed: it has to outlive the session.

```cpp
CounterSink sink;  // declared before the server, so that it outlives every session
wp::WebPanelServerConfig server_config;
server_config.port = static_cast<quint16>(port);
if (loopback) { server_config.bind_address = QHostAddress::LocalHost; }
wp::WebPanelServer server(server_config);
std::string error;
if (!server.start(&error)) { std::printf("example: cannot start the panel server: %s\n", error.c_str()); return 1; }
const std::shared_ptr<wp::PanelSession> page = server.add_panel(make_model(), &error);  // renders the page once
if (!page) { std::printf("example: the panel server refused the model: %s\n", error.c_str()); return 1; }
page->set_control_sink(&sink);
const std::string url = server.page_url(*page);
std::printf("example: panel page: %s\n", url.c_str());
```

The command below starts the example in the background on this machine only, waits until it has printed the address of the page, fetches the page with `Invoke-WebRequest`, and waits for the example to end. It prints the status of the reply, the title of the page and the lines that the example wrote.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
$exe = '.\build\vs2019-x64\examples\web_panel_example\RelWithDebInfo\qar_web_panel_example.exe'
$log = Join-Path $env:TEMP 'qar_web_panel_example.log'
$example = Start-Process -FilePath $exe -ArgumentList '--serve-seconds', '20', '--loopback' -RedirectStandardOutput $log -NoNewWindow -PassThru
$urlPattern = 'example: panel page: (http://\S+/p/[0-9a-f]{32}/)'
$deadline = (Get-Date).AddSeconds(30)
while (-not (Select-String -Path $log -Pattern $urlPattern -Quiet) -and -not $example.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
$url = (Select-String -Path $log -Pattern $urlPattern).Matches[0].Groups[1].Value
$page = Invoke-WebRequest -Uri $url -UseBasicParsing
'status {0}, title {1}' -f $page.StatusCode, [regex]::Match($page.Content, '<title>(.*?)</title>').Groups[1].Value
$example.WaitForExit()
Get-Content $log
```
```text
status 200, title Counter
example: panel page: http://127.0.0.1:53861/p/90dd38bb64bf48c4a2cf86e2dd763142/
example: done
```

The page is complete: it holds its style sheet, its script, the model and the initial data, so the status is 200 and the title is the title of the panel. The example prints the address of the page on a line that has this form:

```text
example: panel page: %s
```

The address has the form `http://<host>:<port>/p/<token>/`. With `--loopback` the host is `127.0.0.1`; without it the host is the address that this machine uses to reach its network. The token is 32 characters, 128 random bits, and it is new for every run. The program turns off the buffering of its output, so that every line reaches the log at once; a reader that looks into the log while a line is being written can see half of it, so the command waits for the whole address. When the time given by `--serve-seconds` is up the example stops, prints a last line that says it is done, and exits with 0.

You can also open the address in a desktop browser on the same machine while the example runs: start the example with a larger `--serve-seconds` and paste the address that it prints.

## Open the panel in QarOS

The same program opens the page in QarOS when you give it the `bin` folder of the QarOS SDK. The example connects first, before it starts the server: the constructor of the session factory runs the whole bootstrap and blocks until the session is up, and a server that was already listening would answer nobody in the meantime. Then it invites a Visualizer; QarOS starts the Visualizer itself, from the folder that you passed, and the Visualizer opens its own window. The example sets the log severity of the QarOS library to `warn` and turns off the copy of the library log to the console; the file log stays on.

```cpp
std::shared_ptr<qqa::QarCApiSenderFactory> qaros;  // stays empty without --sdk-bin: no QarOS SDK is needed
if (!sdk_bin.empty()) {
  qqa::QarCApiSenderFactory::Config config;
  config.runtime_binaries_folder_path = sdk_bin;  // QarOS starts its launcher and Visualizer from here
  config.log_severity = QAR_LOG_SEVERITY_WARN;
  config.enable_console_logging = false;
  qaros = std::make_shared<qqa::QarCApiSenderFactory>(config);  // blocks until the session is up
  QarPeerId visualizer{};
  const std::string failed = qaros->ready() ? qaros->invite_additional_visualizer(visualizer) : qaros->last_error();
  if (!failed.empty()) { std::printf("example: QarOS is not ready: %s\n", failed.c_str()); return 1; }
  std::printf("example: connected to QarOS, Visualizer invited\n");
}
```

A URL panel is a QarOS GUI panel whose content is a URL that the browser of the runtime loads. No frames are sent: the browser fetches the page from the server and handles the pointing itself, and every interaction reaches the server over its WebSocket, through no QarOS input call. The description of the panel holds the address, a title that QarOS shows, and a placement: the pose is the top centre of the panel in room space, and the panel hangs below it, so the example puts it at x = 0, y = 1.5 m and z = -1.2 m. The size in metres comes from `size_url_panel_for`, which measures the page and gives its text the same angular size as the text of a Qt panel. The factory borrows the session of the session factory, because a panel belongs to a session.

```cpp
wp::UrlPanelSizing sizing;  // the page reports the viewport that QarOS gives it, and the size follows that
const auto fit = [&] { return wp::size_url_panel_for(page->model(), server.config().render.theme, sizing).size; };
qar_ar_core::UrlPanelDesc desc;
desc.url = url;
desc.display_name = "Counter";
desc.placement.pose.position = QarVector3{0.0f, 1.5f, -1.2f};  // the top centre of the panel, in the room
desc.placement.size = fit();
std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> ar_panel;
if (qaros) {
  qqa::QarCApiUrlPanelFactory factory(qaros.get());
  ar_panel = factory.create_url_panel(desc);
  if (!ar_panel) { std::printf("example: could not open the URL panel: %s\n", factory.last_error().c_str()); return 1; }
}
```

The panel has to commit to a size before the browser has laid anything out, and the size of the browser viewport is not derived from the metres that you declare. So the page measures the viewport that it was given and reports it to the server, and the host sizes the panel to what it hears: the panel gets the aspect of the viewport. The page reports again after every resize, and a resize of the panel causes one, so the example pushes a size to QarOS only when it changed by more than 5 mm. The report also says how much of the page is below the fold of the viewport, which the page reaches with a click pager.

```cpp
QObject::connect(page.get(), &wp::PanelSession::viewport_reported, &app, [&](const wp::PanelViewportReport & report) {
  std::printf("example: %s\n", report.describe().c_str());
  wp::use_reported_viewport(sizing, report.viewport);
  const QarGuiPanelSize size = fit();
  // The page reports again after every resize, so only a change of the size is pushed to QarOS.
  if (std::abs(size.width_meters - desc.placement.size.width_meters) +
    std::abs(size.height_meters - desc.placement.size.height_meters) > 0.005f) {
    desc.placement.size = size;
    if (ar_panel) { ar_panel->set_placement(desc.placement); }
  }
});
```

Start the example with the folder of the SDK and keep it running while you look at the Visualizer. The command copies everything that the program prints, its own lines and those of the error stream, into a file in your `TEMP` folder as well. You start the example yourself, and you look at the Visualizer window; nothing in this repository clicks or moves a window.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
& .\build\vs2019-x64\examples\web_panel_example\RelWithDebInfo\qar_web_panel_example.exe --sdk-bin "$env:QAR_SDK_ROOT\bin" --serve-seconds 120 2>&1 | Tee-Object -FilePath (Join-Path $env:TEMP 'qar_web_panel_example_ar.log')
```

When the session is up and the Visualizer is invited, the example prints a line that says so, and then the address of the page. After the page has loaded in the Visualizer, the example prints one line for each viewport that the page reports: it starts with the word `viewport`, the width and the height in CSS pixels, and the pixel ratio of the browser. Press the button in the Visualizer with the hand ray. The page shows the new count at once, the example prints one line for each press, and the line has this form:

```text
example: increment: count %lld
```

The panel opens only when the browser of QarOS can reach the address. The Visualizer of this example runs on your machine, and the address is the one that your machine uses on its network, or `127.0.0.1` with `--loopback`. A server that listens on every interface also answers a headset on the same network; `--port` pins the port, so that a firewall rule can name it, and [Running a panel server safely](#running-a-panel-server-safely) lists what the network has to allow.

## Expose a ROS topic as an AR panel

The second example puts a ROS 2 topic on a panel. It builds a small model by hand: a value display bound to the field `data` of the newest message of `/qar_example/text`, which the example keeps under the path `message` of the data document, and a button that publishes. The panels that the repository `qar_rviz_plugin` 0.1.0 generates for any topic are built from templates; the example does not use them. It opens its page as a URL panel as the first example does, but it sizes the panel from the page once and does not follow the viewport that the page reports.

```cpp
// The label is bound to the field "data" of the message, which the example keeps under the path "message" of
// the data document. The button carries the action id that the sink below answers.
ui::UiModel make_model()
{
  ui::UiModel model;
  model.root = ui::widgets::panel("topic_panel", "ROS topic");
  model.root.add(ui::widgets::value("received", "message.data", "Last message on /qar_example/text"));
  model.root.add(ui::widgets::button("publish", "Publish", "publish"));
  model.initial_data = ui::UiValue::object().set("message", ui::UiValue::object().set("data", "(none yet)"));
  return model;
}
```

The ROS 2 side starts a node and an executor and gives the node to `TopicBridge` as a set of node interfaces, which is what lets a lifecycle node or the node of another program do the same. `subscribe` and `advertise` take the name of the topic and the type as `std_msgs/msg/String`; the library loads the typesupport library of the package at run time and reads and writes the message without a generated header. The default options give a subscription and a publisher the QoS profile that matches the publishers that exist, with a depth of 10. A failure is returned as a `Result`, which the example prints.

```cpp
rclcpp::init(0, nullptr);
const auto node = std::make_shared<rclcpp::Node>("qar_ros_topic_panel_example");
rclcpp::executors::SingleThreadedExecutor executor;
executor.add_node(node);
qar_ros_ui::TopicBridge bridge(qar_ros_ui::NodeHandles::from(node));
std::shared_ptr<qar_ros_ui::TopicSubscription> subscription;
PublishSink sink;
const qar_ros_ui::Result subscribed = bridge.subscribe("/qar_example/text", "std_msgs/msg/String", {}, subscription);
const qar_ros_ui::Result advertised = bridge.advertise("/qar_example/pressed", "std_msgs/msg/String", {}, sink.publisher);
if (!subscribed.ok() || !advertised.ok()) {
  std::printf("example: ROS entities failed: %s %s\n", subscribed.message.c_str(), advertised.message.c_str());
  return 1;
}
```

Nothing here has a thread of its own. A timer of the Qt event loop fires every 10 ms and spins the executor, which runs the ROS 2 callbacks on this thread. The subscription only keeps the newest message in a mailbox; `take_latest` converts it into a value tree when the tick asks, so a topic that publishes faster than the tick costs one conversion for each tick, and a message that was overwritten before the tick is never converted. The value is put into the document of the session, which sends it to the viewers. Once a second `reconcile` derives the QoS of the entities again, so that a publisher that starts later, with another profile, is still heard. When ROS 2 has been shut down, for example by Ctrl+C, the tick ends the event loop as well.

```cpp
int ticks = 0;
QTimer timer;  // every 10 ms: run the ROS callbacks, then show the newest message
QObject::connect(&timer, &QTimer::timeout, [&] {
  executor.spin_some();
  if (!rclcpp::ok()) { app.quit(); }  // Ctrl+C has shut ROS 2 down: the loop ends too
  if (++ticks % 100 == 0) { bridge.reconcile(); }  // follow the QoS of publishers that come and go
  ui::UiValue message;
  if (subscription->take_latest(message)) {  // true once for each newest message; older ones are skipped
    page->set_value("message", message);
    const ui::UiValue * data = message.find("data");
    std::printf("example: rx /qar_example/text: %s\n", data != nullptr ? data->as_string().c_str() : "");
  }
});
timer.start(10);
```

The sink turns a press of the button into one message. The publisher converts the value tree into a `String`, serialises it and publishes it, and returns a `Result` that the sink passes to the page as the outcome of the event.

```cpp
// A press of the button becomes one message on the topic; the publisher converts the value to a String.
struct PublishSink : wp::IPanelControlSink
{
  void on_control(wp::PanelSession & session, const wp::PanelControlEvent & event) override
  {
    if (event.control.action != "publish") { session.ack(event, false, "unknown action"); return; }
    const std::string text = "pressed " + std::to_string(++presses);
    const qar_ros_ui::Result sent = publisher->publish(ui::UiValue::object().set("data", text));
    session.ack(event, sent.ok(), sent.message);
    std::printf("example: tx /qar_example/pressed: %s (%s)\n", text.c_str(), sent.ok() ? "ok" : sent.message.c_str());
  }
  std::shared_ptr<qar_ros_ui::TopicPublisher> publisher;
  int presses = 0;
};
```

The first command below starts the example in the background on this machine only, waits until it is up, and lists the topics of the ROS 2 graph with their types, so that you see the two topics of the example in the list. The commands of this section that talk to ROS 2 need a shell in which you dot-sourced `scripts\env_setup.ps1`. `--no-daemon` makes the command create its own node instead of using the ROS 2 daemon, a background process that a command starts when none runs and that keeps running after the command.

```powershell
$exe = '.\build\vs2019-x64\examples\ros_topic_panel_example\RelWithDebInfo\qar_ros_topic_panel_example.exe'
$log = Join-Path $env:TEMP 'qar_ros_topic_panel_example.log'
$example = Start-Process -FilePath $exe -ArgumentList '--serve-seconds', '30', '--loopback' -RedirectStandardOutput $log -NoNewWindow -PassThru
$deadline = (Get-Date).AddSeconds(30)
while (-not (Select-String -Path $log -Pattern 'example: panel page: http://\S+/p/[0-9a-f]{32}/' -Quiet) -and -not $example.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" topic list -t --no-daemon
$example.WaitForExit()
```
```text
...
/parameter_events [rcl_interfaces/msg/ParameterEvent]
/qar_example/pressed [std_msgs/msg/String]
/qar_example/text [std_msgs/msg/String]
/rosout [rcl_interfaces/msg/Log]
```

Right after the example has started, the list can be shorter, because ROS 2 needs a few seconds to discover the topics of another process: if only `/parameter_events` and `/rosout` appear, wait a few seconds and run the command again.

The second command starts the example again and publishes one message on `/qar_example/text` with the command line of ROS 2. `topic pub --once` waits until a subscription has matched, so it needs the example to be running, and it returns when it has published. The example prints one line for each message that it shows, which has this form:

```text
example: rx /qar_example/text: %s
```

```powershell
$exe = '.\build\vs2019-x64\examples\ros_topic_panel_example\RelWithDebInfo\qar_ros_topic_panel_example.exe'
$log = Join-Path $env:TEMP 'qar_ros_topic_panel_example.log'
$example = Start-Process -FilePath $exe -ArgumentList '--serve-seconds', '30', '--loopback' -RedirectStandardOutput $log -NoNewWindow -PassThru
$deadline = (Get-Date).AddSeconds(30)
while (-not (Select-String -Path $log -Pattern 'example: panel page: http://\S+/p/[0-9a-f]{32}/' -Quiet) -and -not $example.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" topic pub --once /qar_example/text std_msgs/msg/String '{data: hello}'
$example.WaitForExit()
Select-String -Path $log -Pattern 'example: rx' | ForEach-Object { $_.Line }
```
```text
...
publisher: beginning loop
publishing #1: std_msgs.msg.String(data='hello')

example: rx /qar_example/text: hello
```

In AR the same flow runs with a Visualizer. Start the example with the folder of the SDK, in a shell prepared with `env_setup.ps1`, and publish on `/qar_example/text` from a second shell as above: the label of the panel shows the message. Press the button in the Visualizer: the sink publishes `pressed 1`, `pressed 2` and so on, and prints a line that has this form for each press. Watch the topic from the second shell.

```text
example: tx /qar_example/pressed: %s (%s)
```

```powershell
.\build\vs2019-x64\examples\ros_topic_panel_example\RelWithDebInfo\qar_ros_topic_panel_example.exe --sdk-bin "$env:QAR_SDK_ROOT\bin" --serve-seconds 120 2>&1 | Tee-Object -FilePath (Join-Path $env:TEMP 'qar_ros_topic_panel_example_ar.log')
```

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" topic echo --no-daemon /qar_example/pressed std_msgs/msg/String
```

## How the pieces fit

The path of a panel has five steps. The host builds a `UiModel`, and `add_panel` renders it once, so that a model that cannot render is refused before anyone asks for the page. A request for `/p/<token>/` is answered with the page, which carries its style sheet, its script, the model and the initial data. The script opens the WebSocket `/ws/<token>` on the same port and the same host, and says hello with the id of the panel; the server answers with a hello that holds the whole data document, so every viewer, including one that reconnects, starts from what the others have reached. After that, the host publishes values and the page sends control events and viewport reports, each frame with its own counter.

Two rules make the exchange safe. A data frame is applied all or nothing, to the document of the server first, so a bad path changes nothing anywhere. And a control event does not change the document of the server: the page applies the change itself, and the next value that the host publishes confirms or corrects it, which is why a host that wants other viewers to see an edit at once calls `set_value` from its sink.

Everything of a session runs on the thread that owns it, which is the thread of the Qt event loop here, and nothing waits: the sockets are asynchronous, a call of the session from another thread is refused and never queued, and the ROS 2 callbacks run when the host spins the executor. A host that spins ROS 2 on a thread of its own connects to the signals of the session with a queued connection instead.

No program of this repository links the QarOS SDK: the examples link the public targets of this repository and the components `render` and `url` of `qar_ogre_adapter`, and they load the QarOS library at run time.

## Running a panel server safely

A panel server is meant for a local network, and the address of a panel is all that protects it. Check these points before a server runs on a network that other people use.

- **The traffic is not encrypted.** The server speaks HTTP and WebSocket without TLS, so everyone who can read the traffic of the network segment can read the page, its values and its control events.
- **The token is the credential.** The address holds a random 128-bit token, for the page and for the socket. A request with another token, or with none, gets the same 404, so it does not reveal whether a token exists, and the server compares tokens in constant time. There is no identity of a viewer and no rate limit on control events beyond the limits of the sockets, so whoever has the address has the full authority over the panel, including its buttons. Treat the address like a session cookie. A button that does something that you cannot take back should be marked `confirm`: the page then asks for a second press within a short window.
- **Removing a panel invalidates its address.** `remove_panel` forgets the token and closes the viewers of the panel.
- **A browser of another origin cannot open the socket.** A handshake whose `Origin` is not the server itself is refused with 403, so a page that somebody else serves cannot reach a panel through the browser of a viewer, even when the token has leaked into that browser. A client that sends no `Origin` is admitted on the token alone.
- **Keep the server on this machine when only this machine needs it.** `--loopback` of the examples, and the field `bind_address` of the server configuration, restrict the server to the loopback interface; then nothing else can connect, and a headset cannot either.

The command below shows the answers of a running server to a request with the right token, with a wrong token of the right shape, with a malformed token and for the root of the server, which names the service and lists no panel.

```powershell
$env:PATH = "$env:QAR_QT_ROOT\bin;$env:PATH"
$exe = '.\build\vs2019-x64\examples\web_panel_example\RelWithDebInfo\qar_web_panel_example.exe'
$log = Join-Path $env:TEMP 'qar_web_panel_example_probe.log'
$example = Start-Process -FilePath $exe -ArgumentList '--serve-seconds', '15', '--loopback' -RedirectStandardOutput $log -NoNewWindow -PassThru
$urlPattern = 'example: panel page: (http://\S+/p/[0-9a-f]{32}/)'
$deadline = (Get-Date).AddSeconds(30)
while (-not (Select-String -Path $log -Pattern $urlPattern -Quiet) -and -not $example.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
$url = (Select-String -Path $log -Pattern $urlPattern).Matches[0].Groups[1].Value
$base = $url -replace '/p/[0-9a-f]{32}/$', ''
$requests = [ordered]@{
    'the page of the panel'      = $url
    'a wrong token of the shape' = "$base/p/$('0' * 32)/"
    'a malformed token'          = "$base/p/short/"
    'the root of the server'     = "$base/"
}
foreach ($name in $requests.Keys) {
    $reply = Invoke-WebRequest -Uri $requests[$name] -UseBasicParsing -SkipHttpErrorCheck
    '{0,-28} {1}' -f $name, $reply.StatusCode
}
$example.WaitForExit()
```
```text
the page of the panel        200
a wrong token of the shape   404
a malformed token            404
the root of the server       200
```

The first and last requests answer 200, and the other two answer 404 with the same body, so a client cannot tell a token that does not exist from a path that does not exist.

What the network has to allow for the panel server, the firewall rule for its TCP port included, is listed in [What this stack adds](#what-this-stack-adds).

## Next steps

- [Operator guide](OPERATOR_GUIDE.md) describes every control of the panel, the peers, the AR tools, the video, the picking and the offline preview.
- [Configuration](CONFIGURATION.md) lists the launcher parameters, every environment variable and the options of the web server, the page and the sizing.
- [Log reference](LOG_REFERENCE.md) explains the lines of the log and the timing files.
- [Troubleshooting](TROUBLESHOOTING.md) helps when a step does not show what this document says; [Web panel problems](TROUBLESHOOTING.md#web-panel-problems) explains what to do when a page does not load, a panel cannot be reached from the headset or a ROS 2 type is not found.
- [Architecture](ARCHITECTURE.md) explains the three libraries, the two routes into AR, the page, the server, the threading and the ROS 2 introspection in detail.
- [API](API.md) documents the public interface of the three libraries, and [Wire protocol](WIRE_PROTOCOL.md) the frames between a page and its server.
