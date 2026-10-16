# Verification

## Status

This section gives the result of each level. The results of Level 0 and Level 1 are those of this release, and each comes from a recorded run of the commands that the level names. The result of Level 2 comes from live sessions with earlier builds, as its line says.

- Level 0, automated checks: passed. Every test of the repository ran and none failed ([Run all tests](#run-all-tests) shows the totals), the layering guard, its self-test and the self-test of the timing analyzer reported OK ([The layering guard and the analyzer](#the-layering-guard-and-the-analyzer)), the plugin built with colcon and installed into the prefix ([Build the plugin and install it](#build-the-plugin-and-install-it)), and the install smoke test found the installed package and its three files ([Install smoke test](#install-smoke-test)).
- Level 1, headless checks: passed with an SDK that has the header of the verified drop of 2026-09-28 ([SDK compatibility](SDK_COMPATIBILITY.md)): the default dry run, the dry run with trace logs and the dry run that invites a device printed the lines that the checks name, exited with 0 and changed nothing ([The default dry run](#the-default-dry-run), [The dry run with trace logs](#the-dry-run-with-trace-logs), [The dry run that invites a device](#the-dry-run-that-invites-a-device)), and, without an SDK, the web example served its page with the status 200 and answered a wrong token with 404 and the root of the server with 200, and the ROS 2 example advertised its topics and printed the message that was published to it ([A page served and fetched](#a-page-served-and-fetched), [The token gate](#the-token-gate), [The ROS 2 bridge](#the-ros-2-bridge)).
- Level 2, desktop checks: the authors of qar_rviz_plugin ran two live sessions in RViz against a QarOS Visualizer, with builds that were made before the last changes of the code, so no live session has run the code of this release. The logs of the sessions show the connection, the invitation of one and of two Visualizers, a steady stream, the pause and the resume, streamed panels with their dialogs, AR tools of the ROS graph with 12 of the 17 templates and the generic form, hover and click events with the ray of the runtime and 21 published picks, video by both routes, a geographic anchor from a synthetic fix and a recorded bag; in those two sessions the authors reported that the three commands of the README for the recorded bag worked and gave no verdict step by step on the rest, and the confirmations that this document reports come from earlier live sessions, held with older builds before the SDK drop of 2026-09-28; [What the live sessions showed](#what-the-live-sessions-showed) says which. The Visualizer crashes when the hidden Displays panel is started again, which is a defect of the Visualizer ([Known issues](#known-issues)), and the examples of the web libraries were not run against a Visualizer.
- Level 3, device checks: not run for this release; [Device checks (Level 3)](#device-checks-level-3) says why.

## Verification levels

Every check of this document belongs to one of four levels. The level says what a check needs and who can run it, and it is the level of the documented commands that the check uses.

- **Level 0, automated checks.** They need neither the QarOS SDK nor a window and run in a plain PowerShell 7 window: configure, build, run the tests, install, the layering guard and the install smoke test. A script or a person runs them with the commands of this document, and every command has a recorded output.
- **Level 1, headless checks.** They run the real programs of this stack without a window, without a Visualizer, without inviting a peer and without a click. Most of them need the QarOS SDK: they open a real QarOS session and end again, which can leave QarOS background processes running.
The checks of a web server or of a ROS 2 bridge need only the programs of this stack.
A script or a person runs them.
- **Level 2, desktop checks.** They run against a real QarOS Visualizer on the desktop of the machine. A person starts the programs, points and clicks in the Visualizer and judges what it shows, and the logs of the run give the numbers. Nothing in this stack clicks, moves a window or presses a key.
- **Level 3, device checks.** They run against a real device, such as a HoloLens 2. Where no device is available, the command is run in its dry-run form and the outcome on a device is described from the code, with the log lines quoted from it.

A check of a higher level assumes that the lower levels passed. All checks run on Windows 11 on x64. A check that needs the QarOS SDK uses the folder that `QAR_SDK_ROOT` names, as [The QarOS SDK](INSTALL.md#the-qaros-sdk) describes, and a check that leaves QarOS background processes running does not stop them: if a later check cannot create a QarOS session, look for a program that holds UDP port 7446, as [Run-time problems](TROUBLESHOOTING.md#run-time-problems) explains.

## Automated checks (Level 0)

The automated checks need neither the QarOS SDK nor a window. Run them in PowerShell 7 from the clone, once the installation has reached [Build and install qar_rviz_plugin](INSTALL.md#build-and-install-qar_rviz_plugin). The commands are those of that section, of [Testing](TESTING.md) and of [Install smoke test](INSTALL.md#install-smoke-test), in the order of a full check. Level 0 passes when every command ends without an error and the test run reports no failed test. Four of the seven test executables, `qar_ros_ui_tests` and three of the plugin, are live ROS tests that use ROS 2 for real; [Live ROS tests and the re-run rule](TESTING.md#live-ros-tests-and-the-re-run-rule) says what to do when only those fail.

Go to the folder of the clone first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

### Configure, build and test the units

The plain CMake project of the repository builds the three web libraries, the two examples, the pure units of the plugin and their tests. It does not build the file that holds the panel, because the plugin builds only under `colcon`; that build is the next step. [Configure, build and test the units](INSTALL.md#configure-build-and-test-the-units) has the details.

```powershell
cmake --preset vs2019-x64
```

```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_PREFIX="C:\qar\install"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"
  QAR_ROS2_INSTALL_ROOT="C:\qar\deps\ros2_humble\ros2-windows"
...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
...
-- qar_ogre_adapter 0.1.0: C:/qar/install/share/qar_ogre_adapter/cmake
-- qar_qt_streaming 0.1.0: C:/qar/install/share/qar_qt_streaming/cmake
...
-- Configuring done (9.6s)
-- Generating done (0.4s)
-- Build files have been written to: C:/qar/src/qar_rviz_plugin/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```

```text
...
  qar_rviz_streaming_ros_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_ros_tests.exe
...
  qar_rviz_streaming_video_source_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_video_source_tests.exe
...
  qar_rviz_plugin_doc_snippets.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_rviz_plugin_doc_snippets.exe
...
  qar_rviz_streaming_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_tests.exe
...
  qar_rviz_streaming_image_source_tests.vcxproj -> C:\qar\src\qar_rviz_plugin\build\vs2019-x64\qar_rviz_streaming\test\RelWithDebInfo\qar_rviz_streaming_image_source_tests.exe
...
```

### Run all tests

The test run is the core of Level 0. It runs every test of the repository through the test preset. A pass is a run in which every test passes: the last lines of the output give the number of tests that ran and say that none failed. A test that is reported as skipped does not count as a failure.

```powershell
ctest --preset vs2019-x64
```

```text
Test project C:/qar/src/qar_rviz_plugin/build/vs2019-x64
        Start   1: check_layering_qar_ui_model
  1/636 Test   #1: check_layering_qar_ui_model ..................................................................   Passed    0.12 sec
        Start   2: check_layering_qar_web_panels
  2/636 Test   #2: check_layering_qar_web_panels ................................................................   Passed    0.07 sec
        Start   3: check_layering_qar_ros_ui
  3/636 Test   #3: check_layering_qar_ros_ui ....................................................................   Passed    0.10 sec
        Start   4: check_layering_qar_rviz_streaming
  4/636 Test   #4: check_layering_qar_rviz_streaming ............................................................   Passed    0.19 sec
...
100% tests passed, 0 tests failed out of 636

Total Test time (real) =  52.12 sec
```

[Testing](TESTING.md#what-the-automated-tests-do-not-cover) lists what these tests leave out: the panel in RViz, every call into QarOS, the JavaScript of the pages, the accuracy of a pick and a real network. Of the checks of the next levels, only those of Level 1 were run for this release: the dry runs of the launcher, which start nothing, and the two examples of the web libraries over the loopback interface. The panel in RViz and the calls into QarOS were exercised only in the two live sessions of Level 2, which ran with builds that were made before the last changes of the code, and no accuracy of a pick has been measured.

### The layering guard and the analyzer

The guard checks the include rules of the four modules and of the examples, and its self-test checks the guard itself. The analyzer of the timing files has a self-test of its own that CTest does not register: it writes four fabricated recordings and checks that the analyzer names the cause of each. [The layering guard](TESTING.md#the-layering-guard) and [The analyzer of the timing files](TESTING.md#the-analyzer-of-the-timing-files) describe them.

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --all
```

```text
check_layering examples: OK (2 files)
check_layering qar_ui_model: OK (36 files)
check_layering qar_web_panels: OK (18 files)
check_layering qar_ros_ui: OK (35 files)
check_layering qar_rviz_streaming: OK (69 files)
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --self-test
```

```text
check_layering self-test: OK
```

```powershell
python scripts\analyze_video_timing.py --self-test
```

```text
SELF-TEST PASSED: 4 fabricated recordings (source stall, GUI-thread stall, QarOS back-pressure, clean) attributed correctly; latency tables and the 15 s period recovered; idle keep-alive submits at the end and at the start of the video ignored, the stalls beside them still found.
```

### Build the plugin and install it

The plugin itself is built with `colcon` into the prefix of the layout. The build is the test of the code that needs RViz, so a change of the panel needs this step as well as the tests. Close every RViz window first, because the build replaces the library that a running RViz has loaded. A pass is a build that ends with the report that it is OK and names the plugin library, the description of the plugin and the overlay.

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

### Install smoke test

The two commands check without a window that the environment of ROS 2 finds the installed package, and that the three files that RViz needs to load the plugin are in the prefix. A pass is the prefix of the layout from the first command and three lines that say True from the second.

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

```powershell
foreach ($path in 'bin\qar_rviz_streaming.dll', 'share\qar_rviz_streaming\plugin_description.xml', 'share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming') {
    '{0,-6} {1}' -f (Test-Path -LiteralPath (Join-Path $env:QAR_PREFIX $path)), $path
}
```

```text
True   bin\qar_rviz_streaming.dll
True   share\qar_rviz_streaming\plugin_description.xml
True   share\ament_index\resource_index\rviz_common__pluginlib__plugin\qar_rviz_streaming
```

## Headless checks (Level 1)

The headless checks of the plugin run the launcher without `-Confirm`. In that mode it prints the configuration that it resolved and changes nothing: no window opens, no registry value is written, no process is stopped and no log file is rotated. A pass is an exit code of 0 and the lines that each check names. The checks need the QarOS SDK that `QAR_SDK_ROOT` names, because the launcher looks for the programs of QarOS in it. [Check the launch configuration](GETTING_STARTED.md#check-the-launch-configuration) explains every line.

The headless checks of the web libraries start the two examples of this repository without a window and without a Visualizer, and probe them over real sockets and a real ROS 2 graph. They need neither the QarOS SDK nor a browser, and each example ends by itself when its time is up. [Getting started](GETTING_STARTED.md) explains the programs; the checks below reuse its commands.

### The default dry run

The default run shows the scene, the programs of QarOS, the overlay of the plugin, the log mode and the variables that the launcher would set. A pass is a plugin overlay that is found, the log mode QUIET and the closing lines of a dry run.

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
  QAR_PANEL_VISUALIZERS    = 0 (admission: invited)
  QAR_PANEL_INVITE_PEERS   = (none - invite from the panel, or pass -InvitePeers)
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
  VC++ runtime (rviz2 dir) : 14.50.35719.0 (ok, needs 14.40 or newer)
  tinyxml.dll (rviz2 dir)  : present

DRY RUN (no -Confirm): environment resolved above; NOT launching any GUI (log rotation is only reported, not performed; the registry, running processes and Fast-DDS shared memory are left alone).
To actually run the interactive session, re-invoke with -Confirm (opens windows).
```

### The dry run with trace logs

With `-TraceLogs` the launcher selects the full logs that a bug report needs: QarOS logs at trace, the zenoh transport logs at debug and the traces of the adapter are on. These logs can grow to gigabytes, so use the mode only for a report. A pass is the log mode TRACE and the closing lines of a dry run.

```powershell
.\scripts\run_live_rviz_stream_real.ps1 -TraceLogs
```

```text
  log mode                 : TRACE (-TraceLogs) - QarOS logs at trace, zenoh debug, adapter traces on; GB-sized logs whose I/O can stall streams, so not for latency work.
  QAR_LOG_SEVERITY         = trace
  QAR_PANEL_LOG_SEVERITY   = (unset)
  QAR_PANEL_LOG_CONSOLE    = (unset)
  RUST_LOG                 = zenoh=debug
  QAR_ZENOH_LOG            = debug
  QAR_ADAPTER_TRACE        = 1
  QAR_ADAPTER_TRACE_ALL    = (unset)
  QAR_ADAPTER_FRAME_TRACE  = (unset)
...
DRY RUN (no -Confirm): environment resolved above; NOT launching any GUI (log rotation is only reported, not performed; the registry, running processes and Fast-DDS shared memory are left alone).
To actually run the interactive session, re-invoke with -Confirm (opens windows).
```

### The dry run that invites a device

The parameter `-InvitePeers` takes entries of the form kind and host name, separated by commas. The dry run shows the entries that the plugin would invite when it connects, and starts nothing. A pass is the entry in the line for the invited peers and the closing lines of a dry run. This is the dry-run form of the device check of [Device checks (Level 3)](#device-checks-level-3).

```powershell
.\scripts\run_live_rviz_stream_real.ps1 -InvitePeers hololens:HOLOLENS-A2B3
```

```text
...
=== RViz -> QarOS Visualizer live stream ===
...
  QAR_PANEL_INVITE_PEERS   = hololens:HOLOLENS-A2B3
...
DRY RUN (no -Confirm): environment resolved above; NOT launching any GUI (log rotation is only reported, not performed; the registry, running processes and Fast-DDS shared memory are left alone).
To actually run the interactive session, re-invoke with -Confirm (opens windows).
```

### A page served and fetched

The web example serves one page on the loopback interface, the command fetches it, and the example prints its address and ends. A pass is the status 200 for the page, the title of the panel, and the last line of the example. [Serve a UI-model panel](GETTING_STARTED.md#serve-a-ui-model-panel) explains the program.

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

### The token gate

The address of a panel holds a random token, and a wrong or missing token is answered with a 404 that does not say whether the token exists. The command asks the server for the page, for a wrong token of the right shape, for a malformed token and for the root of the server. A pass is 200, 404, 404 and 200, in this order. [Running a panel server safely](GETTING_STARTED.md#running-a-panel-server-safely) lists the rules of the server.

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

### The ROS 2 bridge

The ROS 2 example subscribes to the topic `/qar_example/text` and advertises `/qar_example/pressed`. The first command starts it and lists the topics of the graph with their types; a pass is a list that holds both topics. The second command publishes one message on `/qar_example/text` with the command line of ROS 2; a pass is a line of the example that shows the message. Both commands need a shell that was prepared with `scripts\env_setup.ps1`. [Expose a ROS topic as an AR panel](GETTING_STARTED.md#expose-a-ros-topic-as-an-ar-panel) explains the program.

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
publishing #1: std_msgs.msg.String(data='hello')
...
example: rx /qar_example/text: hello
```

## Desktop Visualizer checks (Level 2)

The desktop checks run the plugin in RViz against a real QarOS Visualizer. You start RViz with the launcher, you look at the Visualizer, you point and click in it and you judge what you see; nothing in this repository clicks, moves a window or presses a key for you. The log of RViz gives the numbers: the commands read the newest file `rviz_live_<stamp>.err.log` in the log folder `qar_rviz_logs` of `TEMP`. Before a session, work at the console of the machine, make sure that UDP port 7446 is free and that no RViz of an earlier run is open; [Before you start](GETTING_STARTED.md#before-you-start) has the commands. A check can leave a line that looks like an error; [Known issues](#known-issues) lists them.

Two more checks run the examples of the web libraries against a real Visualizer: you start the example with the folder of the SDK, you look at the Visualizer and you press the button of the panel with the hand ray.

Where a check has a command, the command is the one of [Getting started](GETTING_STARTED.md), [Testing](TESTING.md), [Operator guide](OPERATOR_GUIDE.md) or [Log reference](LOG_REFERENCE.md) that the check names, and it runs as it is written there. Six checks have their own commands below.

- **The web panel in QarOS.** [Open the panel in QarOS](GETTING_STARTED.md#open-the-panel-in-qaros) starts the web example with `--sdk-bin`. A pass is a Visualizer that opens by itself and shows the panel with the title of the example, a line of the example that says that the session is up and the Visualizer invited, and one line for each viewport that the page reports. Press the button: the page shows the new count at once and the example prints one line for each press.
- **The ROS 2 panel in QarOS.** [Expose a ROS topic as an AR panel](GETTING_STARTED.md#expose-a-ros-topic-as-an-ar-panel) starts the ROS 2 example with `--sdk-bin`, in a shell prepared with `scripts\env_setup.ps1`. A pass is a panel whose label shows each message that you publish on `/qar_example/text` from a second shell, and a button whose press makes the example publish a message on `/qar_example/pressed`, which the echo of that topic in a second shell shows, with one line of the example for each press.

### What the live sessions showed

The authors of qar_rviz_plugin ran two live sessions of the plugin in RViz against a QarOS Visualizer, one with a single launch of RViz and one with three. The SDK of both sessions had the header of the verified drop of 2026-09-28. Both ran with builds of the three repositories that were made before the last changes of the code, so no live session has run the code of this release. The evidence is of two kinds: the log of RViz, `rviz_live_<stamp>.err.log`, which the commands of this section read, and what the authors said.

**The logs of the two sessions show:**

- the connection of the session within 2 seconds, the invitation of a Visualizer and its open stream about 5 seconds later, and a second Visualizer, with two streams that ran at a median of about 52 frames a second each;
- a 3D stream at a median of 60 frames a second in every launch that streamed, the pause of the stream, and the return to 60 frames a second within 3 seconds of the resume;
- the hidden Displays panel, docks and dialogs streamed as panels, each with the lines of its creation and its close;
- 15 AR tools exposed from the ROS graph in the second session, with 12 of the 17 templates and the generic form for a topic, a service and an action, and for each of them the report of the browser of QarOS that loaded its page; the template `tf` appears in the log of one earlier run only;
- one message published from AR in the second session, a velocity command;
- hover and click events with the ray of the runtime: in one launch 78,315 hovers and 53 clicks, 1,587 sampled points with the ray and the point where it met the scene, 21 points published on `/clicked_point`, and 32 clicks that the runtime reported without an intersection and that the plugin did not publish;
- one geographic anchor, from a synthetic fix;
- a video at a median of 28 frames a second by both routes, from a publisher that sent 30 a second;
- a recorded bag of a real LiDAR played into RViz: the fixed frame `arco/base_link` of the bag, the 3D stream at 60 frames a second and no line about dropped messages; the cloud itself leaves no line in the log.

**The authors of qar_rviz_plugin confirmed** in earlier live sessions, held with older builds before the SDK drop of 2026-09-28, that a click and a hover reach the real widget of RViz, with the lists of drop-down boxes and the dialogs; that two Visualizers stream at the same time; and that the page of a velocity command opens in AR, pages and publishes. In the two sessions of this section they reported that the three commands of the README for the recorded bag worked. In those two sessions they gave no verdict step by step. How the pictures looked in the Visualizer (depth, shading, the colour of the floor), whether the pages of the other AR tools were readable and how near the picks landed to the points that were aimed at were not judged in those two sessions, and no accuracy has been measured.

**Has not been checked in a live session:** geographic anchoring against a live GPS source, real multi-user devices, and the drag of a slider, the use of two hands, the tap count and the long press. **Not confirmed:** the result of a service call and of an action goal from AR, because the plugin logs neither (the pages of a service and an action of the demo servers of the distribution loaded, and the authors of qar_rviz_plugin reported no result), and the templates `diagnostics`, `imu`, `robot-service` and `navigation-goal`, which no log of a live run shows. The synthetic demo bag failed in the second session: its script wrote no index, and RViz ended when the robot description reached it because `tinyxml.dll` was missing. Both are repaired in this release and have not been run in a live session since. The examples of the web libraries were not run against a Visualizer.

### Without QarOS: the offline preview

The scripted modes of [Scripted checks in a window](TESTING.md#scripted-checks-in-a-window) start RViz with the plugin and no QarOS, make the panel work through one proof each, write their result files and close RViz by themselves. A pass is a PASS line from each of the four modes and an exit code of 0; the windows flash and you do not click into them. [Offline preview mode](OPERATOR_GUIDE.md#offline-preview-mode) describes the interactive form of the same preview.

### Launch, connect, invite

1. **Launch RViz with the plugin.** [Launch RViz with the plugin](GETTING_STARTED.md#launch-rviz-with-the-plugin) starts the launcher with `-Confirm`. A pass is an RViz that opens with the panels Displays and Views and the panel `"Qar Streaming"`, in the state `"Qar runtime: STOPPED"`, with the buttons `"Connect to QarOS"` and `"Disconnect (pause)"`.
2. **Connect.** [Connect to QarOS](GETTING_STARTED.md#connect-to-qaros): press `"Connect to QarOS"` and orbit the 3D view of RViz at once. A pass is a view that keeps orbiting smoothly, the label `"Qar runtime: CONNECTING..."` and then `"Qar runtime: RUNNING (live)"` within a few seconds, and the lines of the connection in the log.
3. **Invite the Visualizer.** [Invite the Visualizer and see the scene](GETTING_STARTED.md#invite-the-visualizer-and-see-the-scene): leave the device `Visualizer` selected and press `"Invite peer"`. A pass is a Visualizer window that opens by itself and shows the grid and the synthetic markers upright and in colour, the line of the invitation and the line of the opened stream, and heartbeat lines for the stream.

### A steady stream

Put the Visualizer beside RViz so that it does not cover RViz, touch nothing for 60 seconds, and print the newest heartbeat lines. The heartbeat is written every 2 seconds. A pass is a stream whose frame rate in the field `fps` stays close to the 60 frames a second that the frame rate list of the panel starts at, and a `max_gap` that is far below a second.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'HB dt=' | Select-Object -Last 5 | ForEach-Object { $_.Line }
```

```text
[qar-panel] 20:35:34.014 HB dt=2.00s ticks=15357 tick_fps=7678.4 max_gap=3.4ms streams=1 batch_avg=1.70ms budget_skips=0 | bf2bb600 fps=60.0 sub=3629 skip=6844 rend=0.85ms why=sender_not_ready[nr=6844 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=1) tick=0.1 prime=1 || panels=1 live=1 closed=0 notice=0 | hidden:Displays sub=61 || AR gestures: rx=0 (click=0 hover=0 other=0) queued=0 dropped=0 | picker: seen=0 used=0 picks=0 ignored=0
[qar-panel] 20:35:36.014 HB dt=2.00s ticks=15384 tick_fps=7691.9 max_gap=5.0ms streams=1 batch_avg=1.46ms budget_skips=0 | bf2bb600 fps=60.0 sub=3749 skip=6868 rend=0.85ms why=sender_not_ready[nr=6868 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=4) tick=0.1 prime=1 || panels=1 live=1 closed=0 notice=0 | hidden:Displays sub=63 || AR gestures: rx=0 (click=0 hover=0 other=0) queued=0 dropped=0 | picker: seen=0 used=0 picks=0 ignored=0
[qar-panel] 20:35:38.015 HB dt=2.00s ticks=14759 tick_fps=7375.6 max_gap=23.3ms streams=1 batch_avg=0.53ms budget_skips=0 | bf2bb600 fps=59.9 sub=3869 skip=7170 rend=0.85ms why=sender_not_ready[nr=7170 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=32) tick=0.1 prime=1 || panels=1 live=1 closed=0 notice=0 | hidden:Displays sub=65 || AR gestures: rx=0 (click=0 hover=0 other=0) queued=0 dropped=0 | picker: seen=0 used=0 picks=0 ignored=0
[qar-panel] 20:35:40.015 HB dt=2.00s ticks=15435 tick_fps=7717.1 max_gap=4.5ms streams=1 batch_avg=1.19ms budget_skips=0 | bf2bb600 fps=60.0 sub=3989 skip=7209 rend=0.84ms why=sender_not_ready[nr=7209 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=9) tick=0.1 prime=1 || panels=1 live=1 closed=0 notice=0 | hidden:Displays sub=67 || AR gestures: rx=0 (click=0 hover=0 other=0) queued=0 dropped=0 | picker: seen=0 used=0 picks=0 ignored=0
[qar-panel] 20:35:42.015 HB dt=2.00s ticks=15318 tick_fps=7658.8 max_gap=3.9ms streams=1 batch_avg=1.47ms budget_skips=0 | bf2bb600 fps=60.0 sub=4109 skip=7231 rend=0.84ms why=sender_not_ready[nr=7231 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=3) tick=0.1 prime=1 || panels=1 live=1 closed=0 notice=0 | hidden:Displays sub=69 || AR gestures: rx=0 (click=0 hover=0 other=0) queued=0 dropped=0 | picker: seen=0 used=0 picks=0 ignored=0
```

### Depth, lighting and the floor

Look at the scene in the Visualizer. A pass is a marker that hides the grid lines behind it, a solid grey plane under the grid lines with the lines visible on top of it, and a shading of the markers that does not change while you orbit the camera of RViz. The depth is the window-space value that the Visualizer reads, the light is a rig that is fixed to the world, and the headlight of RViz is suppressed while the plugin streams. The command prints the lines of the log that name these settings.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'Scene lighting:|Floor plane:|LIVE: near/far' | ForEach-Object { $_.Line }
```

```text
[qar-panel] 20:34:09.988 Scene lighting: mode=both, RViz headlight=suppressed while streaming
[qar-panel] 20:34:09.991 Floor plane: frame='map' plane=XY 10x10 cells @ 1 m -> 10x10 m plane, offset=(0, 0, 0) m, 1mm drop, depth_bias(constant=16, slopeScale=4).
[qar-panel] 20:34:13.955 LIVE: near/far = 0.01 / 150 m, depth_scale = 1
```

### Pause and resume

Press `"Disconnect (pause)"`, switch the grid off in the Displays panel of RViz, and look at the Visualizer: it still shows the grid, because the streaming is paused while the session stays open. Press `"Connect to QarOS"`: the Visualizer catches up and the grid disappears there too. A pass is a pause that does not close the session, a resume that works, and an RViz that stays responsive. The log shows the pause with the line `"QarOS: streaming paused (session kept open)."` The press on `"Connect to QarOS"` after the pause runs the steps of the connection again on the open session, so a line `"Connected to QarOS:"` after the pause line shows the resume, and the line `"QarOS: streaming resumed."` does not appear, because only a press with no pause before it writes that line ([Connect, invite and session lines](LOG_REFERENCE.md#connect-invite-and-session-lines) describes the lines). The command prints the pause line and the lines `"Connected to QarOS:"` in the order of the log: the first of them is the connection of the session, and the one after the pause line is the resume.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'QarOS: streaming paused|Connected to QarOS:' | ForEach-Object { $_.Line }
```

```text
[qar-panel] 20:34:15.764 Connected to QarOS: session open, streaming active. No peer invited — pick a device kind and click 'Invite peer' to open a Visualizer.
[qar-panel] 20:39:35.412 QarOS: streaming paused (session kept open).
[qar-panel] 20:39:49.263 Connected to QarOS: streaming active; waiting for the invited peer(s).
```

### Panels in AR

1. **Stream a panel and click it.** [Stream a panel and click it in AR](GETTING_STARTED.md#stream-a-panel-and-click-it-in-ar): stream `"This QAR panel"`, find it in the Visualizer, hover and click a button, open a drop-down list and press `"Refresh diagnostics"`. A pass is a readable panel that repaints, a list that opens inside the AR panel, a change that shows in RViz and the line of the diagnostics.
2. **The hidden Displays panel.** [The hidden Displays panel](GETTING_STARTED.md#the-hidden-displays-panel): nothing new opens on the desktop, and a click on the checkbox of Grid in the panel `"Displays (AR)"` switches the grid of RViz off and on. A pass is a toggle that follows each click and the line that reports the creation of the copy.
3. **A dialog and the close of a panel.** [Close behaviour](GETTING_STARTED.md#close-behaviour): press `Add` in the AR copy of the Displays panel, choose a display and confirm, then stop the streaming of a panel and close a streamed dock of RViz. A pass is a dialog that opens as a panel of its own and goes away with the dialog, AR panels that close with their windows, and the lines of the close.

### Several Visualizers

Press `"Invite peer"` a second time with the device `Visualizer`: a second window opens with a view of its own. Watch both for 30 seconds, then close the first window and watch the other while you orbit RViz. A pass is a heartbeat with two streams that each keep a steady frame rate, a second stream that keeps running after the first window closed, and an RViz that stays responsive. Each invitation of a Visualizer carries a fresh identifier, so each opens a Visualizer of its own.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'LIVE: invited|LIVE: opened stream|LIVE: closed stream' | ForEach-Object { $_.Line }
Select-String -Path $log.FullName -Pattern 'HB dt=.* streams=2' | Select-Object -First 3 | ForEach-Object { $_.Line }
```

```text
[qar-panel] 20:34:28.187 LIVE: invited visualizer as peer bf2bb6007d (1 invited in total). Its stream opens when the device connects.
[qar-panel] 20:34:33.100 LIVE: opened stream for peer bf2bb6007d
[qar-panel] 20:47:54.432 LIVE: invited visualizer as peer 1b9faccee0 (2 invited in total). Its stream opens when the device connects.
[qar-panel] 20:48:00.131 LIVE: opened stream for peer 1b9faccee0
[qar-panel] 20:48:00.350 HB dt=2.00s ticks=2283 tick_fps=1141.5 max_gap=9.2ms streams=2 batch_avg=0.03ms budget_skips=0 | 1b9facce fps=0.0 sub=1 skip=1127 rend=1.14ms why=sender_not_ready[nr=1127 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=575) | bf2bb600 fps=40.8 sub=45625 skip=1149144 rend=0.90ms why=sender_not_ready[nr=1149144 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=86) tick=0.1 prime=1 || panels=1 live=1 closed=2 notice=0 | hidden:Displays sub=822 || AR gestures: rx=12993 (click=0 hover=12984 other=9) queued=12984 dropped=9 last=not a pointer gesture (a manipulation kind we did not subscribe to) | picker: seen=12984 used=0 picks=0 ignored=12984 [no pick mode armed (Mode is Off)]
[qar-panel] 20:48:02.349 HB dt=2.00s ticks=8418 tick_fps=4208.9 max_gap=12.5ms streams=2 batch_avg=0.06ms budget_skips=0 | 1b9facce fps=48.1 sub=102 skip=9421 rend=1.17ms why=sender_not_ready[nr=9421 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=51) | bf2bb600 fps=49.3 sub=45727 skip=1157006 rend=0.90ms why=sender_not_ready[nr=1157006 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=14) tick=0.1 prime=1 || panels=1 live=1 closed=2 notice=0 | hidden:Displays sub=824 || AR gestures: rx=13121 (click=0 hover=13112 other=9) queued=13112 dropped=9 last=not a pointer gesture (a manipulation kind we did not subscribe to) | picker: seen=13112 used=0 picks=0 ignored=13112 [no pick mode armed (Mode is Off)]
[qar-panel] 20:48:04.350 HB dt=2.00s ticks=9274 tick_fps=4636.9 max_gap=10.5ms streams=2 batch_avg=0.02ms budget_skips=0 | 1b9facce fps=52.6 sub=207 skip=18583 rend=1.08ms why=sender_not_ready[nr=18583 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=23) | bf2bb600 fps=45.3 sub=45816 skip=1163888 rend=0.90ms why=sender_not_ready[nr=1163888 err=0 tgt=0 bud=0] (begin-frame pending (no render request from the viewer); consecutive=149) tick=0.1 prime=1 || panels=1 live=1 closed=2 notice=0 | hidden:Displays sub=826 || AR gestures: rx=13241 (click=0 hover=13232 other=9) queued=13232 dropped=9 last=not a pointer gesture (a manipulation kind we did not subscribe to) | picker: seen=13232 used=0 picks=0 ignored=13232 [no pick mode armed (Mode is Off)]
```

### Stop and start the hidden Displays panel

Select the entry of the copy of the Displays panel in the drop-down of the group `"AR GUI panels"` and press `"Stop streaming"`, wait about three seconds, and press `"Stream panel to AR"`; do this three times and watch the Visualizer for 20 seconds after each start. Note for each start when the window of the Visualizer came back and whether it shows its scene. A pass is a panel that comes back every time under the next number after its title, `Displays (AR) (2)` and so on, with one line with `gets the QarOS panel name` in the log for each start, and a Visualizer that QarOS starts again with its scene. A crash file of the Visualizer in the QarOS log folder at a start is the known defect that [The Visualizer crashes when the hidden Displays panel is started again](TROUBLESHOOTING.md#the-visualizer-crashes-when-the-hidden-displays-panel-is-started-again) describes, and not a failure of this repository.

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern "closed 'hidden:Displays'|added panel stream \(panel 'hidden:Displays'\)|'hidden:Displays' gets the QarOS panel name" | ForEach-Object { $_.Line }
Get-ChildItem -Path "$env:TEMP\quaternar" -Filter '*_qar-stereokit-gui_*.crash.txt' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 3 | ForEach-Object { '{0}  {1}' -f $_.LastWriteTime.ToString('s'), $_.Name }
```

```text
[qar-panel] 00:52:30.373 [qt_lib:INFO:panel] added panel stream (panel 'hidden:Displays')
[qar-panel] 00:52:49.455 [qt_lib:INFO:panel] closed 'hidden:Displays' (stream e8ed1b96-2680-4f30-b7d4-60065773b1d2) after 11 frame(s) (reason: stopped in RViz). Teardown: input subscriptions cancelled, close_panel ran, video sender destroyed. The stream entry is gone (no closed-window notice); opening the window again is a fresh add on a new sender. (panel 'hidden:Displays')
[qar-panel] 00:52:56.266 AR PANEL: 'hidden:Displays' gets the QarOS panel name 'Displays (AR) (2)', because an earlier QarOS panel of this process already used 'Displays (AR)'.
[qar-panel] 00:52:56.267 [qt_lib:INFO:panel] added panel stream (panel 'hidden:Displays')
[qar-panel] 00:53:17.339 [qt_lib:INFO:panel] closed 'hidden:Displays' (stream 49e237d8-170f-4bb2-905a-20965871b0a9) after 14 frame(s) (reason: stopped in RViz). Teardown: input subscriptions cancelled, close_panel ran, video sender destroyed. The stream entry is gone (no closed-window notice); opening the window again is a fresh add on a new sender. (panel 'hidden:Displays')
[qar-panel] 00:53:25.866 AR PANEL: 'hidden:Displays' gets the QarOS panel name 'Displays (AR) (3)', because an earlier QarOS panel of this process already used 'Displays (AR)'.
[qar-panel] 00:53:25.867 [qt_lib:INFO:panel] added panel stream (panel 'hidden:Displays')
YYYY-MM-DDT00:53:26  20261012-005326_qar-stereokit-gui_15096.crash.txt
YYYY-MM-DDT00:52:56  20261012-005256_qar-stereokit-gui_33004.crash.txt
```

### ROS tools, video, picking and geolocation

These checks need a second PowerShell window and the synthetic publishers of the repository. Prepare the window with the two lines below: the first goes to the folder of the clone, and the second, with the leading dot and the space, imports the environment of the ROS 2 distribution.

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

When the launcher starts with `-Confirm`, it stops the Python processes that run `publish_test_markers.py`, `publish_test_tf.py`, `publish_test_robot.py` or `publish_test_pointcloud.py`, also when you started them in another window, so start every publisher after the launcher. [What the live sessions showed](#what-the-live-sessions-showed) says which generated tools the logs of the live sessions show and which the authors of qar_rviz_plugin confirmed; the templates and the calls that no live session covers are covered by unit tests and by the live ROS tests.

1. **A velocity command as an AR tool.** [Expose a ROS topic as an AR tool](GETTING_STARTED.md#expose-a-ros-topic-as-an-ar-tool): expose `/qar_demo/cmd_vel` with publishing allowed, set `Forward` and press `Publish` twice. A pass is a page of the template `velocity-command` that opens in AR, a message with a forward speed above zero on the topic, and no message for a single press.
2. **Video, both routes.** [Video in AR](GETTING_STARTED.md#video-in-ar): make the clip, publish it, open it from the page of the topic and stream the Image dock of RViz. A pass is a video that moves in both AR panels and the lines of the video in the log. Cover RViz with the Visualizer for 60 seconds to see that the video does not depend on RViz being visible.
3. **Picking.** [Pick a point in the scene](GETTING_STARTED.md#pick-a-point-in-the-scene): choose the mode that picks a point, aim at a marker and at the floor, click and press `"Confirm pick"`. A pass is a point on `/clicked_point` near the aimed object or floor spot, about 0.1 to 0.2 m at a few metres, and a refusal, with nothing published, for a click that hits nothing. There is no measured figure for the accuracy of a pick.
4. **Geolocation.** [Geolocation](OPERATOR_GUIDE.md#geolocation): publish a synthetic fix while the panel is connected. A pass is one line that reports the anchor, with the latitude, the longitude and the altitude that you published; a static fix does not anchor again. The anchor has never been checked against a live GPS source.
5. **Timing.** Start the launcher with `-TimingCsv`, run a video as above, and close RViz. The launcher makes a fresh folder for each run below the log folder, and the analyzer reads that folder and prints one verdict for each stall. The time after the video has ended is no stall: the panel only submits its last picture again once a second, and the analyzer reports those gaps as ignored. A pass is a folder that holds the CSV files and an analyzer that prints its verdict; a stall that the analyzer attributes is a finding. [Timing CSV files](LOG_REFERENCE.md#timing-csv-files) describes the files.

```powershell
$timing = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs\timing" -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
python scripts\analyze_video_timing.py $timing.FullName
```

```text
--- panel qar_test_video_image_raw (rx source: qar_test_video_image_raw) ---
  submitted 10144 frames in 1908.5 s; active 324.9 s at 26.4 fps; submit gap in the active part p50 33.6 / p95 44.0 / p99 78.6 / max 7809.1 ms; gaps >100 ms: 70, >250 ms: 3, >500 ms: 3
  idle keep-alive gaps ignored: 1562 (1575.9 s)
  keep-alive gaps with no frame waiting but a tick not ready or failing: 2 (7.7 s), outside the active part; episodes when over 100 ms
...
=== verdict ===
133 episode(s): 16 OUR-PIPELINE, 117 QAROS-BACKPRESSURE
16 episode(s) started in OUR pipeline - see the details above.
```

### The end of a session

Close the Visualizer windows and then RViz. RViz can crash while it closes, which [Known issues](#known-issues) describes. [Stop and clean up](GETTING_STARTED.md#stop-and-clean-up) lists the logs of the run and how big they are, and [Where the logs go](LOG_REFERENCE.md#where-the-logs-go) lists the log files that QarOS wrote for each program. A pass is the pair of logs of the run in the log folder, a log of RViz without per-frame trace lines in the quiet mode, and the marker publisher that the launcher started, which you stop yourself.

## Device checks (Level 3)

Not run for this release: no device was available. The authors of qar_rviz_plugin state that an earlier build of this stack worked on a HoloLens 2 with the SDK drop of 2026-09-28; that is their statement, and no record of that run exists.

A device check runs the plugin against a device that QarOS can address: a HoloLens 2 by the host name of the device, or a Meta Quest or a ZED camera that joins by itself. It checks four things. The invitation goes out, from the panel or with the launcher parameter `-InvitePeers`. The admission policy lets the device in. The scene arrives on the device. When the device sends hand joints, the pointing ray of the plugin follows the hand.

Without a device the invitation is checked in its dry-run form, the third launcher run of [Headless checks (Level 1)](#headless-checks-level-1): it prints the devices that the launcher would invite and starts nothing. What a device run shows is given from the code. When the launcher invites devices at connect, the panel writes a line that has this form:

```text
LIVE: inviting devices at connect: %1
```

When the invitation of a device succeeds, the panel writes a line that has this form, and the stream opens when the device connects, with the lines of [Invite the Visualizer and see the scene](GETTING_STARTED.md#invite-the-visualizer-and-see-the-scene):

```text
LIVE: invited %1%2 as peer %3 (%4 invited in total). Its stream opens when the device connects.
```

A HoloLens has to be named. Without a host name the panel refuses the invitation, and its message has this form at its start:

```text
Invite peer: a HoloLens needs its device hostname
```

When the device sends hand joints, the adapter writes a line that has this form, and the pointing ray of the gestures is live from then on:

```text
[qar-adapter] hands: the device IS sending hand joints - AR pointing ray is live
```

A device that would exceed the ceiling on the number of peers is refused, with a line that has this form:

```text
[qar-adapter] *** peer %s refused: already streaming to %zu peers (max_peers=%u)
```

## Feature coverage

The table lists what this repository provides, the automated suites that exercise it, and the levels whose checks in this document cover it. A suite is the name of a group of tests, the part before the first dot of a test name, and [List and select tests](TESTING.md#list-and-select-tests) shows how to list every test. The table gives names and no counts; the test counts are in the output of [Run all tests](#run-all-tests). Level 0 is the suites of the middle column, and a feature without a suite has only the checks of the higher levels. An entry for Level 2 says what the two live sessions covered and no more: they ran with builds that were made before the last changes of the code, so no live session has run the code of this release, and [What the live sessions showed](#what-the-live-sessions-showed) lists what their logs and the authors of qar_rviz_plugin showed. Level 3 has not been run. The suites of the libraries of qar_ogre_adapter and qar_qt_streaming are in the documents of those repositories.

| Feature | Automated suites (Level 0) | Covered by the checks of levels |
|---|---|---|
| Topic tree, filter and the dialog that adds from the ROS graph | `TopicTree`, `TopicFilter`, `Catalog`, `ParseManifest`, `NormalizeMessageType`, `RosGraphTreeTest` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| AR tools: exposure, registry and generated panels | `ArPanelGeneratorTest`, `ArExposureRegistryTest`, `ArPanelIdTest`, `ArDocumentSchemaTest`, `ArPanelTemplateRegistryTest`, `ArRosEntityKindTest` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Controls of an AR tool: publishing to a topic, calling a service, sending an action goal | `ArControlRouterTest` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds), publishing to a topic only |
| Semantic templates and the generic form | `ArSemanticCatalogTest`, `ArTwistTemplateTest`, `ArPoseTemplateTest`, `ArImageTemplateTest`, `ArSemanticFallbackTest`, `ArSemanticRosTest` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds), 12 of the 17 templates |
| Grey floor plane under the grid | `GridFloorPlaneGeometry`, `ParseGridPlaneKind`, `IsValidGridExtent` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds), the log line and not the look |
| The hidden Displays panel: the guard of the panel factory | `RttiGuardTypeName`, `RttiGuardCast` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Spatial picking and the messages that it publishes | `SpatialInteraction`, `SpatialPointPublish`, `SpatialPosePublish`, `SpatialNavGoalPublish`, `SpatialAreaPublish`, `SpatialTargetOptions` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds), picks of points only |
| Video in AR: display classes, transports and the sources of images | `IsVideoDisplayClass`, `ShouldSkipDisplayForExposedTopic`, `ImageTopicTransportTest`, `VideoSourceExecutor`, `RosImageFrameSourceTiming`, `WindowsScheduling` | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Health readout of a video display | `FormatVideoDisplayHealth` | 0 |
| Build, install and load of the plugin | none | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| The live launcher, its dry runs and its logging modes | none | [1](#headless-checks-level-1); [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| The timing analyzer | none | 0; [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Connect, pause and resume without blocking RViz | none | [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Several Visualizers and the stream of each | none | [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Invitation and admission of devices | none | [1](#headless-checks-level-1), the dry run only; [3](#device-checks-level-3) (not run for this release) |
| Geographic anchor from a GPS fix | none | [2](#desktop-visualizer-checks-level-2) (earlier builds), a synthetic fix |
| Test scenes and synthetic data | none | [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Offline preview and its scripted modes | none | [2](#desktop-visualizer-checks-level-2) (no run is recorded) |
| Values, JSON and HTML of the UI model | `UiValue`, `JsonParse`, `JsonWrite`, `JsonRoundTrip`, `HtmlEscape`, `HtmlRenderer`, `Utf8` | 0 |
| Description of ROS messages as fields, widgets and defaults | `FieldKinds`, `MessageDescription`, `FieldPath`, `ResolveFieldPath`, `DefaultValues`, `ValidateValue`, `TypeSupportRegistry`, `Introspection` | 0 |
| Pages that fit the viewport, with paging and size knobs | `Viewport`, `ViewportFit`, `PageMetrics`, `ScrollPaging`, `UrlPanelSizing`, `ReportedViewport`, `ViewportFitSizing` | 0 |
| The theme of an AR page | `ArTheme`, `ArThemeCss`, `ThemeCss` | 0 |
| The wire protocol and the patches of a page | `Protocol`, `ApplyPatch` | 0 |
| The token-gated HTTP and WebSocket server | `HttpParse`, `HttpMessage`, `PanelToken`, `HttpServerTest`, `WebPanelServerTest`, `WebPanelServerHosts`, `WebPanelServerBind` | 0; [1](#headless-checks-level-1), the page and the token gate; [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| Discovery of topics, services and actions in the ROS 2 graph | `GraphDiscovery`, `GraphDiscoveryLive` | 0 |
| The topic bridge: subscribe, publish and quality of service | `DynamicMessage`, `QosSelection`, `TopicBridgeLive` | 0; [1](#headless-checks-level-1); [2](#desktop-visualizer-checks-level-2) (earlier builds) |
| The service and action bridges | `ServiceBridgeLive`, `ActionBridge`, `ActionBridgeLive` | 0 |
| The layering of the modules | `check_layering.py` | 0 |











## Known issues

**RViz can crash when it closes.** When you close RViz, Windows can report that `rviz2.exe` stopped working. The authors of qar_rviz_plugin know the crash and tolerate it, and most of the runs that they recorded ended with a crash report. A run is judged by its heartbeat lines and not by a clean exit. [RViz crashes when it is closed](TROUBLESHOOTING.md#rviz-crashes-when-it-is-closed) describes it.

**The Visualizer crashes when the hidden Displays panel is started again.** With a Visualizer attached, the window of the QarOS Visualizer disappeared within a second of the start in each of the 25 cases that the authors of qar_rviz_plugin observed, with the hidden Displays panel stopped and started again, and QarOS started a Visualizer again after several seconds. It also disappeared five more times in one longer session, each within a second of the close or the creation of another panel. The defect is the Visualizer's. The plugin gives a panel that is started again a number after its title as a mitigation of unproven value, which does not prevent the crash. [The Visualizer crashes when the hidden Displays panel is started again](TROUBLESHOOTING.md#the-visualizer-crashes-when-the-hidden-displays-panel-is-started-again) describes it, and the check [Stop and start the hidden Displays panel](#stop-and-start-the-hidden-displays-panel) repeats the sequence.

Some lines of a run look like errors and are expected. None of them is a failure by itself, and a run is judged by its heartbeat lines and by the checks of this document, and not by the absence of these lines.

**At the end of a run.** When the last Visualizer has closed, or while RViz closes, the panel can still try to render for a stream whose sender is gone, and the error line has this form at its end:

```text
submit_phase: sender rejected the frame
```

The adapter can then fail to make the OpenGL context of a sender current, and it writes a line that has this form:

```text
[qar-adapter] *** wglMakeCurrent FAILED (err=%lu)
```

RViz can also crash while it exits, as [RViz crashes when it is closed](TROUBLESHOOTING.md#rviz-crashes-when-it-is-closed) describes. Judge such a run by its heartbeat lines.

**While a stream is set up.** The creation of a sender for a peer can take several seconds, because the receiver has to finish its negotiation first, and the adapter then writes a line that has this form:

```text
[qar-adapter] create_sender: peer %s still creating after %.1f s
```

In the first seconds after an invitation, or while a QarOS panel is created, the heartbeat marks a panel that QarOS asks for and that is not served yet with a field that has this form:

```text
UNSERVED %1s nr=%2 stream=%3
```

The panel manager warns about the same case with a line that has this form:

```text
QarOS has a panel from us it is not being served:
```

A panel that stays unserved after the first Visualizer stream is open is a finding, not a tolerated line.

## Reporting a problem

When a check fails, report it with the facts that let someone repeat it. [What to send us](TROUBLESHOOTING.md#what-to-send-us) lists what every report needs: the facts of your machine, the newest QarOS logs and the steps that lead to the problem. A failed check of this document adds the following.

- **The check.** Its heading in this document, and the command exactly as you ran it, with its complete output.
- **The text of the console** in which the check ran, when a program printed to it.
- **The logs of the run.** The launcher writes `rviz_live_<stamp>.out.log` and `rviz_live_<stamp>.err.log` into its log folder, and a run with timing writes a folder of CSV files below it.
- **The QarOS logs** that were written during the run, newest first, and every crash report that appeared: the files of the QarOS log folder whose names end in crash.txt, with their dumps, and the report that Windows shows for a program that stops.
- **Pictures.** A screenshot of the Visualizer, and of the window of RViz where there is one, at the moment of the failure, when the failure is visible.
- **What you did,** step by step, and what you expected to see.
