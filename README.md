# qar_rviz_plugin - the RViz2 plugin and the web AR panels that bring RViz and the ROS 2 graph into QarOS

qar_rviz_plugin 0.1.0 streams RViz2 into QarOS. A panel inside RViz2 connects to a QarOS session, renders the live 3D scene from the head pose of each peer and sends it as stereo video, streams the Qt windows of RViz2 and tools that it generates from the ROS 2 graph into AR as GUI panels, and turns the pointer of the headset into ROS 2 messages. It is an RViz2 plugin for ROS 2 Humble, written in C++17 for Windows 11 and MSVC, that colcon installs as the ament package `qar_rviz_streaming`, together with a plain CMake project that builds the pure units of the plugin, the web libraries and the examples of this repository, and runs their tests. The AR tools are web pages that three C++17 static libraries of this repository generate, serve and connect to the ROS 2 graph, `qar_ui_model`, `qar_web_panels` and `qar_ros_ui`; a program of your own can use them without RViz, as the two examples of the repository do. It builds on the two repositories `qar_ogre_adapter` and `qar_qt_streaming`, and at run time it needs the QarOS SDK, which this repository does not contain.

## Who this is for

This repository is for QuaternAR engineers who integrate with QarOS. It assumes that you know QarOS and its C API, and it never explains them: where QarOS appears, the documentation states only what this stack relies on and how it uses the C API.

What it does explain, where it is used, is the part of the stack that is not QarOS:

- how a UI model becomes a web page that QarOS opens as a URL panel, and how the ROS 2 graph is read and written from it;
- RViz2 and how a plugin lives inside it, ROS 2 Humble on Windows, Ogre 1.12.1 and Qt 5.15.2 in the extent that the plugin and its web libraries use them;
- colcon, the build tool of ROS 2, for the one package of this repository that is built with it;
- CMake and the Microsoft C++ toolchain, with the exact versions and the reason for each.

Everything that is not QarOS is installed from a fresh Windows 11 machine, step by step, in the installation document.

## Features

- **A panel in RViz2 that connects to QarOS.** The plugin adds the panel `"Qar Streaming"` to RViz2. The session of QarOS is built when you press `"Connect to QarOS"`, on a worker thread, so that RViz2 stays responsive, and nothing connects when the panel loads. The panel can invite a Visualizer or a device, a HoloLens 2, a Meta Quest or a ZED camera, into the session, and by default only the peers that it invited get a stream.
- **The live scene for every peer.** For every admitted peer the plugin renders the real scene of RViz2 from the head pose that QarOS reports and sends a colour picture and a depth picture for each eye. The streams that are due are rendered in one batch under one render lock, with a budget of about 16 ms for a batch and a floor of 30 frames per second for each peer, and up to 8 peers are served at once. An opaque floor under the Grid display, a lighting rig that is fixed in the world, and an optional geographic anchor from a `NavSatFix` topic make the scene readable and placed in AR.
- **RViz2 panels as AR GUI panels.** Any dock of RViz2, and the panel itself, can be streamed into AR as a GUI panel. A click and a hover in AR reach the real widget, and a dialog that the widget opens becomes a panel of its own. When the session opens, a hidden copy of the Displays panel goes up, so that the headset can toggle, edit, add and remove the displays of RViz2. A window that closes in RViz2 has its AR panel closed and, by default, its video sender destroyed.
- **ROS 2 graph tools in AR.** The panel lists the topics, services and actions of the ROS 2 graph and generates an AR panel for any of them: one of 17 layouts for common types, such as velocity commands with a stop button, poses, batteries, diagnostics, odometry, maps, scans, images, point clouds, IMU data and GPS fixes, or a generic form for any other type. A web server in RViz2 serves the page, and QarOS opens it as a URL panel. Publishing from AR is off until you allow it, and the buttons that command a robot ask for a second press.
- **Video in AR.** A ROS 2 image topic reaches AR as a video panel, by the button `"Open video in AR"` of an image tool or by a dock that holds an Image or Camera display. The video is received in a private ROS 2 context, decoded off the GUI thread and sent at up to 30 frames per second. A Fast-DDS profile for large frames, a publisher that plays a video file as a topic, and an analyzer of per-frame timing files ship with the plugin.
- **Picking in AR.** The pointer of the headset publishes a point, a navigation goal, an initial pose or an area on the topics, and with the message types, of the tools of RViz2: `/clicked_point`, `/goal_pose`, `/initialpose` and `/qar/selected_area`. A pick waits for your confirmation unless you switch that off, and a click that hits nothing is refused and not published.
- **Two launchers and an offline preview.** `scripts\run_live_rviz_stream_real.ps1` prepares the environment and starts RViz2 with the live panel, and without `-Confirm` it only prints what it would do. `scripts\run_rviz_with_plugin.ps1` starts the offline preview, in which simulated peers replace QarOS, and four scripted modes that write a result file and quit RViz2.
- **Logs and diagnostics.** The panel writes a heartbeat line every 2 s and its log goes to the file `rviz_live_<stamp>.err.log`; its groups show the state of the connection, of the AR panels, of the AR tools and of the pointer; and the launcher parameter `-TimingCsv` records the timing of every video stage in CSV files.
- **Tests.** A plain CMake project builds the pure units of the plugin, among them the generator of AR tools, the semantic templates, the logic of spatial picking, the tree of topics and the catalog of displays, with the web libraries and the examples, and seven test executables test the units and the libraries. The panel itself, which links RViz2, builds only under colcon.
- **A UI model that becomes a page.** `qar_ui_model` describes a panel as data: a tree of twenty kinds of widget (containers, read-outs, inputs, buttons, tables, gauges, status lights, charts, images and a JSON inspector). A widget that shows or edits a value names it by a dotted path into one data document, and a button carries an action id. `render_html_page` turns the model into one self-contained page, and the same model with the same options gives the same bytes. The library needs nothing beyond the C++ standard library.
- **A page made for reading and pointing in the air.** The theme of the page keeps every pressable element at 64 by 64 pixels or more with a gap of 12 pixels between neighbours, keeps all text at 18 pixels or more, uses dark colours, and has no hover rule, because a hand ray jitters and has no reliable hover state. The script of the page is written in ES5 syntax for the browser of QarOS, and a probe at boot writes the name of a missing browser feature on the page instead of leaving a blank rectangle. The page carries a Content-Security-Policy, escapes every string that reaches its markup, and embeds its data so that no string can end the script element.
- **A server that QarOS can open.** `qar_web_panels` serves the page and its WebSocket from one HTTP listener, so that one port and one firewall rule cover both. The listener binds every network interface on a port that the operating system picks, unless you set others. Every panel has its own 128-bit token in its address, and a wrong token is answered with a 404; a WebSocket handshake from another origin is refused; and limits on the size of a request and of a message, on the time that a request may take, on the viewers of a panel and on the unsent backlog of a viewer bound what one slow or hostile connection can cost. The limit on connections is one count for the whole server and not one for each address, so a peer that keeps opening connections can use up all of them, and the other viewers then get the answer 503. Nothing in the server waits.
- **Panels sized from the page.** A URL panel needs a size in metres before the browser has laid anything out. `size_url_panel_for` measures the page and gives the panel the same legibility as a Qt widget panel of `qar_qt_streaming`; the page reports the viewport that QarOS gave it, and the size follows that report. A page that does not fit its viewport shrinks its theme and drops columns, and what is still below the fold is reached with a click pager.
- **A ROS 2 side that needs no generated code.** `qar_ros_ui` lists the topics, services and actions of the ROS 2 graph with their types, reads the layout of any message type from its typesupport library at run time, and subscribes, publishes, calls services and sends goals with the same value tree that the page uses. Its QoS profiles follow the publishers that exist. A host drives it from its own event loop; no call waits, and an error is a returned `Result`, never an exception.
- **Two examples and tests.** `qar_web_panel_example` serves a counter panel from a hand-built model, and `qar_ros_topic_panel_example` shows one ROS 2 topic in a panel and publishes on another from a button. Each library has its own test executable, and the tests need no QarOS SDK and open no window.
- **Known limitations.** The code that calls the QarOS C API, the session bootstrap, the render sender, the panel sender, the input sources and the URL adapter, has no automated test, and only live sessions exercise it. The logs of the two live sessions show the hover and click events with the ray of the runtime and 21 published picks, 12 of the 17 templates of the AR tools with the page of each loaded in the browser of QarOS, and a velocity command published from AR. In those sessions the maintainers gave no verdict on how near a pick lands to the aimed point or on how the pages look, and no live session has run the code of this release. Real multi-user devices and geographic anchoring against a live GPS source have not been checked in a live session, the maintainers reported no result of a service call or of an action goal from AR, and the templates `diagnostics`, `imu`, `robot-service` and `navigation-goal` appear in no log. [What the live sessions showed](docs/VERIFICATION.md#what-the-live-sessions-showed) lists the evidence. The invitation of a peer and the calls that create, change or close a panel are synchronous calls of the SDK on the GUI thread of RViz2, which can stand still for a moment. [SDK compatibility](docs/SDK_COMPATIBILITY.md#known-sdk-limitations) lists what the SDK does not document, and [Troubleshooting](docs/TROUBLESHOOTING.md#rviz-and-plugin-problems) the symptoms that you may meet. The page is served as plain HTTP and WebSocket, so the token in the address is the only protection. The browser of QarOS gives a page a viewport of fixed size that no header field describes, so a page taller than the viewport is reached with the click pager. What is known about that browser is in [The QarOS in-AR browser](docs/SDK_COMPATIBILITY.md#the-qaros-in-ar-browser), and the symptoms that you may meet are in [Web panel problems](docs/TROUBLESHOOTING.md#web-panel-problems).

## Where this fits

This repository is one of three that together stream an Ogre scene, Qt widgets, web pages and RViz into QarOS. Each repository has the version 0.1.0, and this documentation describes version 0.1.0 of all three: a repository asks for that version of the repositories it builds on, so the three have to be at matching versions. The table lists the three in the order in which they are installed: `qar_ogre_adapter` first, then `qar_qt_streaming`, then `qar_rviz_plugin`.

| Repository | Version | What it provides | Depends on | Also needs |
|---|---|---|---|---|
| qar_ogre_adapter | 0.1.0 | The QarOS integration layer: renders an Ogre scene for each eye and streams it to QarOS; the adapter for the QarOS C API; the vendored C header | none | The ROS 2 Humble distribution, for Ogre only |
| qar_qt_streaming | 0.1.0 | Qt widgets as QarOS AR GUI panels | qar_ogre_adapter | Qt 5.15.2 |
| **qar_rviz_plugin** (this repository) | 0.1.0 | The RViz2 plugin that streams RViz into QarOS, and web AR panels from a UI model and from the ROS 2 graph | qar_ogre_adapter, qar_qt_streaming | The ROS 2 Humble distribution with its repairs, Qt 5.15.2, colcon |

The installation document of each repository installs the repositories it depends on, with the commands for each, so you can start from any of them.

## Quick start

These ten steps take a Windows 11 machine that has the toolchain of [Installation](docs/INSTALL.md#toolchain) installed to RViz2 with the live panel. Every command is explained, with its output, in the document that the step names. Run the steps in one PowerShell 7 window, in this order. Steps 1 to 8 do not need the QarOS SDK; steps 9 and 10 need it in the folder `QAR_SDK_ROOT` ([The QarOS SDK is not included](#the-qaros-sdk-is-not-included)).

1. Create the folders and set the seven variables that all commands use ([Directory layout and environment variables](docs/INSTALL.md#directory-layout-and-environment-variables)).

```powershell
$layout = [ordered]@{
    QAR_ROOT          = 'C:\qar'
    QAR_DEPS_ROOT     = 'C:\qar\deps'
    ROS2_INSTALL_ROOT = 'C:\qar\deps\ros2_humble\ros2-windows'
    QAR_QT_ROOT       = 'C:\qar\deps\Qt\5.15.2\msvc2019_64'
    QAR_VENV          = 'C:\qar\venv'
    QAR_PREFIX        = 'C:\qar\install'
    QAR_SDK_ROOT      = 'C:\QarOS\install'
}
foreach ($name in $layout.Keys) {
    [Environment]::SetEnvironmentVariable($name, $layout[$name], 'User')
    Set-Item -Path "Env:$name" -Value $layout[$name]
}
New-Item -ItemType Directory -Force -Path $layout.QAR_ROOT, $layout.QAR_DEPS_ROOT, $layout.QAR_PREFIX, (Split-Path $layout.QAR_SDK_ROOT) | Out-Null
```

> Success: the block prints nothing and ends without an error. Afterwards the seven variables are set for your Windows user and in the current window, and these folders exist: `C:\qar`, `C:\qar\deps`, `C:\qar\install` and `C:\QarOS`.
>
> This command changes your machine, so no output is shown here.

2. Clone this repository and go to the folder of the clone; the next steps run there until a step names another folder ([Python environment](docs/INSTALL.md#python-environment)).

```powershell
git clone https://github.com/Quaternar/qaros-ros-rviz "$env:QAR_ROOT\src\qar_rviz_plugin"
```
```text
Cloning into 'C:\qar\src\qar_rviz_plugin'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

3. Create the Python environment, which holds the installer of Qt and colcon, and fill it from the pinned list of the repository.

```powershell
& "$env:ProgramFiles\Python310\python.exe" -m venv $env:QAR_VENV
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m pip install --disable-pip-version-check -r requirements.txt
```
```text
...
Successfully installed PyYAML-6.0.3 Pygments-2.20.0 aqtinstall-3.3.0 backports.zstd-1.6.0 beautifulsoup4-4.15.0 brotli-1.2.0 bs4-0.0.2 catkin-pkg-1.1.0 certifi-2026.6.17 charset-normalizer-3.4.9 colcon-cmake-0.2.30 colcon-common-extensions-0.3.0 colcon-core-0.21.0 colcon-defaults-0.2.9 colcon-devtools-0.3.0 colcon-library-path-0.2.1 colcon-metadata-0.2.5 colcon-notification-0.3.1 colcon-output-0.2.14 colcon-package-information-0.4.1 colcon-package-selection-0.2.10 colcon-parallel-executor-0.4.0 colcon-pkg-config-0.1.0 colcon-powershell-0.5.0 colcon-python-setup-py-0.2.9 colcon-recursive-crawl-0.2.3 colcon-ros-0.5.0 colcon-test-result-0.3.8 colorama-0.4.6 coloredlogs-15.0.1 coverage-7.15.0 defusedxml-0.7.1 distlib-0.4.3 docutils-0.23 empy-4.2.1 exceptiongroup-1.3.1 humanfriendly-10.0 humanize-4.16.0 idna-3.18 inflate64-1.0.4 iniconfig-2.3.0 lark-1.3.1 multivolumefile-0.2.3 netifaces-0.11.0 numpy-2.2.6 opencv-python-5.0.0.93 packaging-26.2 patch-ng-1.19.1 pluggy-1.6.0 psutil-7.2.2 py7zr-1.1.3 pybcj-1.0.7 pycryptodomex-3.23.0 pyparsing-3.3.2 pyppmd-1.3.1 pyreadline3-3.5.6 pytest-9.1.1 pytest-cov-7.1.0 pytest-repeat-0.9.4 pytest-rerunfailures-16.4 python-dateutil-2.9.0.post0 pywin32-312 requests-2.34.2 semantic-version-2.10.0 six-1.17.0 soupsieve-2.8.4 texttable-1.7.0 tomli-2.4.1 typing_extensions-4.16.0 urllib3-2.7.0 vcstool-0.3.0
```

4. Download and unpack the ROS 2 Humble distribution, which provides the ROS 2 client libraries and RViz2, and repair it: its missing runtime libraries, the C++ runtime next to `rviz2.exe`, which is too old for the QarOS library, and the missing `tinyxml.dll` that the URDF parser of RViz needs ([ROS 2 Humble for Windows](docs/INSTALL.md#ros-2-humble-for-windows), [Repairs to the ROS 2 Windows distribution](docs/INSTALL.md#repairs-to-the-ros-2-windows-distribution)).

```powershell
.\scripts\fetch_dependencies.ps1
```
```text
=== ROS 2 Humble Windows binary distro ===
Downloading https://github.com/ros2/ros2/releases/download/release-humble-20260220/ros2-humble-20260220-windows-release-amd64.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-humble-20260220-windows-release-amd64.zip
Extracting to C:\qar\deps\ros2_humble

=== ROS 2 source manifest repo (reference only) ===
Downloading https://github.com/ros2/ros2/archive/refs/tags/release-humble-20260220.zip
Downloaded and verified: C:\qar\deps\downloads\ros2-release-humble-20260220-src.zip
Extracting to C:\qar\deps\ros2_src_release-humble-20260220

Done. Binary distro: C:\qar\deps\ros2_humble\ros2-windows
Reminder: the binary distro has packaging gaps - run the repair scripts next:
fix_missing_spdlog_dll.ps1, fix_missing_vendor_dlls.ps1, fix_missing_openssl_dll.ps1, fix_qt_conda_imports.ps1.
```

```powershell
.\scripts\fix_missing_spdlog_dll.ps1
.\scripts\fix_missing_vendor_dlls.ps1
.\scripts\fix_missing_openssl_dll.ps1
.\scripts\fix_qt_conda_imports.ps1
.\scripts\fix_missing_vendor_cmake_configs.ps1
.\scripts\fix_missing_eigen.ps1
.\scripts\fix_stale_pixi_paths.ps1
```
```text
...
Done. spdlog.dll SHA256: BC272567EDB344A4EACDD42902271879B890C1DFB05C45618957064A4EE46D11
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\console_bridge.dll  (from C:\qar\deps\build_vendor_dlls\console_bridge-1.0.2\_build\bin\RelWithDebInfo\console_bridge.dll)
Installed: console_bridge headers and import library under C:\qar\deps\ros2_humble\ros2-windows\opt\console_bridge_vendor
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\tinyxml2.dll  (from C:\qar\deps\build_vendor_dlls\tinyxml2-9.0.0\_build\RelWithDebInfo\tinyxml2.dll)
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\yaml-cpp.dll  (from C:\qar\deps\build_vendor_dlls\yaml-cpp-yaml-cpp-0.7.0\_build\RelWithDebInfo\yaml-cpp.dll)
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\assimp-vc141-mt.dll  (from C:\qar\deps\build_vendor_dlls\assimp-4.1.0\_build\code\RelWithDebInfo\assimp-vc140-mt.dll)
...
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\libcurl.dll  (from C:\qar\deps\build_vendor_dlls\curl-7.81.0\_build\lib\RelWithDebInfo\libcurl.dll)

Done. Verify with ros2 --help (through ros2-script.py) or rviz2.exe: no DLL load errors should remain.
Downloading https://conda.anaconda.org/conda-forge/win-64/openssl-1.1.1w-hcfcfb64_0.conda
Downloaded and verified: C:\qar\deps\downloads\openssl-1.1.1w-hcfcfb64_0.conda
Extracting openssl-1.1.1w-hcfcfb64_0.conda
Installed libssl-1_1-x64.dll (OpenSSL 1.1.1w) from C:\qar\deps\build_openssl\unpacked
Installed libcrypto-1_1-x64.dll (OpenSSL 1.1.1w) from C:\qar\deps\build_openssl\unpacked
Done. Fast-DDS (rmw_fastrtps_cpp) can now load; rcl/rclpy init and rviz2 will start.
Patched rviz2.exe : 1 '_conda' import name(s) -> plain Qt5 names.
Patched rviz_common.dll : 3 '_conda' import name(s) -> plain Qt5 names.
Patched rviz_rendering.dll : 2 '_conda' import name(s) -> plain Qt5 names.
Patched rviz_default_plugins.dll : 3 '_conda' import name(s) -> plain Qt5 names.

Done. rviz2 binaries now import plain Qt5Core/Qt5Gui/Qt5Widgets DLLs (single Qt copy).
Ensure the Qt bin folder is on PATH (scripts/env_setup.ps1 handles this).
assimp:
  wrote C:\qar\deps\ros2_humble\ros2-windows\opt\rviz_assimp_vendor\lib\cmake\assimp-4.1\assimpConfig.cmake
curl:
  wrote C:\qar\deps\ros2_humble\ros2-windows\opt\libcurl_vendor\lib\CMake\curlConfig.cmake
yaml-cpp:
  wrote C:\qar\deps\ros2_humble\ros2-windows\opt\yaml_cpp_vendor\CMake\yaml-cpp-config.cmake
console_bridge:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\console_bridge\cmake\console_bridgeConfig.cmake
TinyXML2:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\tinyxml2\cmake\TinyXML2Config.cmake
spdlog:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\spdlog\cmake\spdlogConfig.cmake
orocos_kdl:
  wrote C:\qar\deps\ros2_humble\ros2-windows\share\orocos_kdl\cmake\orocos_kdlConfig.cmake

All vendor CMake config shims written. find_package(rviz_common) will now configure.
(Eigen3 headers+config are handled separately by fix_missing_eigen.ps1.)
Downloading https://deb.debian.org/debian/pool/main/e/eigen3/eigen3_3.4.0.orig.tar.bz2
Downloaded and verified: C:\qar\deps\downloads\eigen-3.4.0.tar.bz2
Extracting eigen-3.4.0.tar.bz2
Eigen source: C:\qar\deps\build_eigen\eigen-3.4.0
Copied Eigen headers -> C:\qar\deps\ros2_humble\ros2-windows\opt\eigen3_vendor\include\eigen3
Wrote Eigen3Config.cmake -> C:\qar\deps\ros2_humble\ros2-windows\share\eigen3\cmake
find_package(Eigen3) will now succeed for downstream ament plugin builds.
Stale pixi_ws paths materialized under C:\pixi_ws\.pixi\envs\default.
```

The first run of `scripts\fix_stale_pixi_paths.ps1` on a machine that has no `C:\pixi_ws` prints a `mkdir` line for each folder that it creates and a line for each set of files that it fills in. In a shell without the Visual Studio environment, such as the shell in which you run these commands, it then prints the warning `WARNING: cl/lib not on PATH; cannot create stub tinyxml.lib (run from build_plugin.ps1)`. The warning is expected: the stub `tinyxml.lib` is a file that the linker needs and that the script builds with `cl` and `lib`, and `scripts\build_plugin.ps1` loads the Visual Studio environment before it runs the script again, which completes the stub.

The eighth repair replaces the C++ runtime files next to `rviz2.exe`, version 14.29, with the files of a runtime of version 14.40 or newer from the system folder of Windows; the QarOS library needs that version, and RViz would end at `"Connect to QarOS"` without it. It needs the current Microsoft Visual C++ Redistributable for x64, which the [toolchain](docs/INSTALL.md#microsoft-visual-c-redistributable) installs. The SDK may not be unpacked yet at this point, because steps 1 to 8 do not need it; if the system folder holds no runtime of version 14.40 or newer, the script says so and changes nothing, and you run it again with `-SdkRoot $env:QAR_SDK_ROOT` once the SDK is there. Close RViz first, and start it again after the repair.

```powershell
.\scripts\fix_old_vc_runtime.ps1
```
```text
8 Visual C++ runtime file(s) older than 14.40 below C:\qar\deps\ros2_humble\ros2-windows; source C:\WINDOWS\System32 (msvcp140.dll 14.50.35719.0).
  replaced bin\concrt140.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_1.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_2.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_atomic_wait.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\msvcp140_codecvt_ids.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\vcruntime140.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)
  replaced bin\vcruntime140_1.dll  14.29.30157.0 -> 14.50.35719.0  (from C:\WINDOWS\System32)

Done. Replaced 8 of 8 file(s); each original is kept as <name>.vcrt.orig next to it.
Start rviz2 again: a running process keeps the runtime it loaded.
```

The ninth and last repair builds the `tinyxml.dll` that the URDF parser of RViz needs. The release ships no such file, and without it RViz ends with the exit code 0xC0000409 as soon as a robot description reaches the Robot model display. The script downloads the source archive of TinyXML 2.6.2 and checks its SHA-256, builds the library with Visual Studio 2019 and CMake, copies only `tinyxml.dll` into the `bin` folder of the distribution, and proves with `check_urdf.exe` of the distribution that the file works; run it after the vendor DLL script, as [Repairs to the ROS 2 Windows distribution](docs/INSTALL.md#repairs-to-the-ros-2-windows-distribution) describes.

```powershell
.\scripts\fix_missing_tinyxml_dll.ps1
```
```text
Visual Studio 2019 tools: C:\Program Files (x86)\Microsoft Visual Studio\2019\Community
Downloading https://deb.debian.org/debian/pool/main/t/tinyxml/tinyxml_2.6.2.orig.tar.gz
Downloaded and verified: C:\qar\deps\downloads\tinyxml_2.6.2.orig.tar.gz
Extracting tinyxml_2.6.2.orig.tar.gz
Configuring (VS2019 x64, matching the distro's v142 toolset)
...
Building tinyxml (RelWithDebInfo, matching the distro's build config)
...
  tinyxml.vcxproj -> C:\qar\deps\build_tinyxml\tinyxml\_build\RelWithDebInfo\tinyxml.dll
Installed: C:\qar\deps\ros2_humble\ros2-windows\bin\tinyxml.dll  (from C:\qar\deps\build_tinyxml\tinyxml\_build\RelWithDebInfo\tinyxml.dll)
Check: check_urdf.exe parses a robot with the new tinyxml.dll.

Done. tinyxml.dll SHA256: 9F1EAF765712B58FCAF6924D96E59554D91834C46D68B99F5DAEC68B8FAF9365
Start RViz again; its Robot model display can load a robot description now.
```

5. Install Qt ([Qt 5.15.2](docs/INSTALL.md#qt-5152)).

```powershell
& "$env:QAR_VENV\Scripts\python.exe" -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --outputdir "$env:QAR_DEPS_ROOT\Qt"
```
```text
INFO    : aqtinstall(aqt) v3.3.0 on Python 3.10.11 [CPython MSC v.1929 64 bit (AMD64)]
...
INFO    : Patching C:\qar\deps\Qt\5.15.2\msvc2019_64\bin\qmake.exe
...
INFO    : Finished installation
INFO    : Time elapsed: 15.21180160 second
```

6. Clone the upstream repository `qar_ogre_adapter`, go to the folder of its clone, build it and install it into `QAR_PREFIX` ([Upstream repositories](docs/INSTALL.md#upstream-repositories)).

```powershell
git clone https://github.com/Quaternar/qaros-ogre "$env:QAR_ROOT\src\qar_ogre_adapter"
```
```text
Cloning into 'C:\qar\src\qar_ogre_adapter'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_ogre_adapter"
```

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_ROS2_INSTALL_ROOT="C:\qar\deps\ros2_humble\ros2-windows"
  QAR_SDK_ROOT="C:\QarOS\install"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
-- Found rviz_ogre_vendor: 11.2.25 (C:/qar/deps/ros2_humble/ros2-windows/share/rviz_ogre_vendor/cmake)
...
-- qar_qar_adapter: QarOS SDK backend, DYNAMIC loading of C:\QarOS\install/bin/qar-streaming-c.dll (no import lib linked)
...
-- Configuring done (7.7s)
-- Generating done (0.1s)
-- Build files have been written to: C:/qar/src/qar_ogre_adapter/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```
```text
...
  qar_ar_core.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ar_core\RelWithDebInfo\qar_ar_core.lib
...
  qar_ogre_streaming.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_ogre_streaming\RelWithDebInfo\qar_ogre_streaming.lib
...
  qar_qar_adapter_render.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\qar_qar_adapter\RelWithDebInfo\qar_qar_adapter_render.lib
...
  qar_ogre_standalone_streaming_sample.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\ogre_standalone_streaming_sample\RelWithDebInfo\qar_ogre_standalone_streaming_sample.exe
...
  qar_ogre_live_stream_example.vcxproj -> C:\qar\src\qar_ogre_adapter\build\vs2019-x64\examples\qaros_live_stream\RelWithDebInfo\qar_ogre_live_stream_example.exe
...
```

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_ar_core.lib
-- Installing: C:\qar\install/lib/qar_ogre_streaming.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_capi.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_render.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_panels.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_url.lib
-- Installing: C:\qar\install/lib/qar_qar_adapter_gestures.lib
-- Installing: C:\qar\install/include
...
-- Installing: C:\qar\install/include/qar_streaming.h
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE.qar_streaming_header
-- Installing: C:\qar\install/share/qar_ogre_adapter/LICENSE
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfig.cmake
-- Installing: C:\qar\install/share/qar_ogre_adapter/cmake/qar_ogre_adapterConfigVersion.cmake
```

7. Do the same for `qar_qt_streaming`, which finds `qar_ogre_adapter` in `QAR_PREFIX`.

```powershell
git clone https://github.com/Quaternar/qaros-qt "$env:QAR_ROOT\src\qar_qt_streaming"
```
```text
Cloning into 'C:\qar\src\qar_qt_streaming'...
done.
```

```powershell
Set-Location "$env:QAR_ROOT\src\qar_qt_streaming"
```

```powershell
cmake --preset vs2019-x64
```
```text
Preset CMake variables:

  QAR_BUILD_TESTS="ON"
  QAR_PREFIX="C:\qar\install"
  QAR_QT_ROOT="C:\qar\deps\Qt\5.15.2\msvc2019_64"

...
-- The CXX compiler identification is MSVC 19.29.30151.0
...
-- ROS 2 distribution: C:/qar/deps/ros2_humble/ros2-windows
...
-- qar_ogre_adapter 0.1.0: C:/qar/install/share/qar_ogre_adapter/cmake
...
-- Configuring done (8.0s)
-- Generating done (0.1s)
-- Build files have been written to: C:/qar/src/qar_qt_streaming/build/vs2019-x64
```

```powershell
cmake --build --preset vs2019-x64
```
```text
...
  qar_qt_streaming.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\RelWithDebInfo\qar_qt_streaming.lib
...
  qar_qt_panel_example.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\examples\qt_panel_example\RelWithDebInfo\qar_qt_panel_example.exe
...
  qar_qt_streaming_doc_snippets.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\docs\snippets\RelWithDebInfo\qar_qt_streaming_doc_snippets.exe
...
  qar_qt_streaming_tests.vcxproj -> C:\qar\src\qar_qt_streaming\build\vs2019-x64\qar_qt_streaming\test\RelWithDebInfo\qar_qt_streaming_tests.exe
...
```

```powershell
cmake --install build\vs2019-x64 --config RelWithDebInfo --prefix $env:QAR_PREFIX
```
```text
-- Installing: C:\qar\install/lib/qar_qt_streaming.lib
-- Up-to-date: C:\qar\install/include
-- Installing: C:\qar\install/include/qar_qt_streaming
...
-- Installing: C:\qar\install/share/qar_qt_streaming/LICENSE
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingTargets-relwithdebinfo.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfig.cmake
-- Installing: C:\qar\install/share/qar_qt_streaming/cmake/qar_qt_streamingConfigVersion.cmake
```

8. Go back to the folder of the clone of this repository, then build the plugin with colcon and install it into `QAR_PREFIX`. Close every RViz2 window first, because the install replaces the library of the plugin ([Build and install qar_rviz_plugin](docs/INSTALL.md#build-and-install-qar_rviz_plugin)).

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

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

9. Check the launch configuration. Without `-Confirm` the launcher prints what it would do and starts nothing ([Check the launch configuration](docs/GETTING_STARTED.md#check-the-launch-configuration)).

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

10. Start RViz2 with the live panel ([Launch RViz with the plugin](docs/GETTING_STARTED.md#launch-rviz-with-the-plugin)). The panel connects only when you press `"Connect to QarOS"`.

```powershell
.\scripts\run_live_rviz_stream_real.ps1 -Confirm
```

[Getting started](docs/GETTING_STARTED.md) goes on from here: it connects to QarOS, invites the Visualizer, streams a panel and clicks it in AR, exposes a ROS 2 topic as an AR tool, shows a video, picks a point and explains how to stop and clean up. [Try the examples](#try-the-examples) runs a synthetic point cloud, a video and two recorded bags in the session that you have open by then.

## Try the examples

These recipes start where the Quick start ends: RViz is open with the live panel and, to see a result in AR, the panel is connected and has invited a Visualizer ([Connect to QarOS](docs/GETTING_STARTED.md#connect-to-qaros), [Invite the Visualizer and see the scene](docs/GETTING_STARTED.md#invite-the-visualizer-and-see-the-scene)). Every command runs in PowerShell 7 with the folder of this repository, `$env:QAR_ROOT\src\qar_rviz_plugin`, as the current directory. A command that needs no ROS 2 runs in any such window. A program that talks to ROS 2, or that keeps running until you stop it, gets a window of its own, which you prepare with the two lines below: the first goes to that folder, and the second, with the leading dot and the space, imports the environment of the ROS 2 distribution.

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

`local_setup.bat` of the ROS 2 distribution prints a warning about RTI Connext every time `scripts\env_setup.ps1` runs, before the banner of the script. RTI Connext is a DDS product that this stack does not use, because it uses Fast-DDS, so you can ignore the warning.

ROS 2 programs find each other when they use the same domain number, which is 0 unless you set `ROS_DOMAIN_ID`; if other ROS 2 programs run on your network, set a number of your own in every window, including the one that starts RViz.

### A synthetic point cloud

The script `scripts\publish_test_pointcloud.py` publishes a coloured, wavy surface of 3600 points on the topic `/qar_test_points`, in the frame `map`, four times a second, at heights between 0.4 m and 1.6 m. Start it in a window of its own, prepared as above:

```powershell
python scripts\publish_test_pointcloud.py
```

The script logs one line, `Publishing synthetic PointCloud2 on /qar_test_points`, and runs until you press Ctrl+C in its window. In the panel of RViz open the group `"AR GUI panels"`, press `"Add from ROS graph..."`, select `/qar_test_points` under `Topics` and press `Add`: RViz creates a display of the class PointCloud2 for the topic. The Fixed Frame of the scene that the launcher loads is `map`, the frame of the cloud, so the surface appears above the grid in RViz, and the Visualizer shows it with the rest of the scene.

### A video

Make the clip in any window; it needs no ROS 2. The clip is one minute long, 1280 by 720 pixels at 30 frames per second, with a circle that moves across a coloured background and a frame counter, and it is written to the file `qar_test_clip.mp4` in the folder `demo` of the layout root.

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

Publish the clip in a prepared window of its own. The publisher sends the clip once as raw images of the type `sensor_msgs/msg/Image` on `/qar_test_video/image_raw`, at the frame rate of the clip; at the end it logs that it stops and keeps running until you press Ctrl+C.

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

Two ways show the video in AR. In the panel press `"Add from ROS graph..."`, select `/qar_test_video/image_raw`, tick `"Expose as a QAR AR tool"` and press `Add`; no RViz display is created, and the panel of the tool has the button `"Open video in AR"`, which opens a second panel, titled with the topic, that shows the video at up to 30 frames per second. Or add an Image display for the topic in the Displays panel of RViz, press `Refresh` in the group `"AR GUI panels"`, choose the dock of that display in the drop-down and press `"Stream panel to AR"`; the plugin then paints the video into the AR panel directly, at up to 30 frames per second. `"Stop streaming"` closes a video panel. [Video in AR](docs/GETTING_STARTED.md#video-in-ar) has both ways in full.

### A recorded bag of synthetic data

The script `scripts\play_demo_rosbag.ps1` plays a small bag of the synthetic publishers. The first run records the bag for 12 seconds into the folder `qar_demo_rosbag` of `TEMP`, writes the index that the recorder does not write, and plays the bag; a later run plays the same bag again. The script prepares its own environment from the variables of step 1 of the Quick start, and it plays the bag again and again until you press Ctrl+C, so it gets a window of its own.

```powershell
.\scripts\play_demo_rosbag.ps1
```

Among other topics the bag holds `/qar_test_markers` and `/qar_test_points`, with the messages of the live publishers. Add the cloud to RViz as in the first recipe, after you have stopped the publisher of that recipe: the bag takes its place.

### A recorded bag of a real LiDAR

The bag `TrafficMonitoring` is a recording of 122 seconds of traffic, made with the Ouster LiDAR and the other sensors of a robot that stood next to a road. It belongs to the dataset of the Service Robotics Lab of the Pablo de Olavide University, which [the page of the dataset](https://robotics.upo.es/datasets/ArcoDataset/main.html) describes. The authors publish the data under the Creative Commons license Attribution-NonCommercial-ShareAlike 3.0, which excludes commercial use, so read its terms on that page before you use the bag; this repository does not contain the data. The server `robotics.upo.es` offers the bag as a ZIP archive of 731 MiB, which holds one file, the database `rosbag2_2024_05_23-15_55_45_0.db3` of 1.8 GiB, and no `metadata.yaml`, the index that every rosbag2 command needs. Download the archive and unpack it, in any window:

```powershell
New-Item -ItemType Directory -Force -Path "$env:QAR_ROOT\demo" | Out-Null
Invoke-WebRequest -Uri 'https://robotics.upo.es/datasets/ArcoDataset/bags/TrafficMonitoring.zip' -OutFile "$env:QAR_ROOT\demo\TrafficMonitoring.zip"
```

```powershell
Expand-Archive -Path "$env:QAR_ROOT\demo\TrafficMonitoring.zip" -DestinationPath "$env:QAR_ROOT\demo"
```

Write the index in a prepared window: `ros2 bag reindex` reads the database and writes `metadata.yaml`, and `ros2 bag info` lists what the bag holds.

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" bag reindex "$env:QAR_ROOT\demo\TrafficMonitoring" -s sqlite3
```
```text
...
[INFO] [<time>] [rosbag2_storage]: Opened database 'C:\qar\demo\TrafficMonitoring\rosbag2_2024_05_23-15_55_45_0.db3' for READ_ONLY.
[INFO] [<time>] [rosbag2_cpp]: Reindexing complete.
```

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" bag info "$env:QAR_ROOT\demo\TrafficMonitoring"
```
```text
...
Files:             rosbag2_2024_05_23-15_55_45_0.db3
Bag size:          1.8 GiB
Storage id:        sqlite3
Duration:          121.769497554s
...
Messages:          40337
Topic information: Topic: /DetectionList | Type: ars548_messages/msg/DetectionList | Count: 2436 | Serialization Format: cdr
...
                   Topic: /ouster/points | Type: sensor_msgs/msg/PointCloud2 | Count: 1117 | Serialization Format: cdr
                   Topic: /tf | Type: tf2_msgs/msg/TFMessage | Count: 4073 | Serialization Format: cdr
                   Topic: /tf_static | Type: tf2_msgs/msg/TFMessage | Count: 6 | Serialization Format: cdr
```

Two things stand between the bag and the display. First, the bag holds two trees of transforms that are not connected: the tree of the robot, `arco/base_link` with `arco/os1_sensor` below it, and the tree of the sensors, `os_sensor` with `os_lidar` below it, in which the clouds are stamped. `static_transform_publisher` of the package `tf2_ros` joins them with a transform that moves nothing and has to keep running for the whole playback, so it gets a prepared window of its own; it prints a warning about old-style arguments, which you can ignore.

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" run tf2_ros static_transform_publisher 0 0 0 0 0 0 arco/os1_sensor os_sensor
```

Second, the Ouster topics were recorded as BEST_EFFORT, the PointCloud2 display of RViz asks for RELIABLE, and a BEST_EFFORT publisher cannot serve a RELIABLE subscriber, so the player must offer them as RELIABLE. The file `scripts\rosbag_qos_override.yaml` does that, and it keeps `/tf_static` latched with a history depth of 100, because the six static transforms of the bag must all reach a subscriber that joins late. The player gets a prepared window of its own, too. It plays the bag again and again with `--loop` until you press Ctrl+C, and it prints a warning for each of the three topics of the package `ars548_messages`, which the ROS 2 distribution does not have; it skips those topics and plays the others.

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" bag play "$env:QAR_ROOT\demo\TrafficMonitoring" --loop --qos-profile-overrides-path scripts\rosbag_qos_override.yaml
```

In the Displays panel of RViz set the property Fixed Frame, under Global Options, to `arco/base_link`. Then press `"Add from ROS graph..."` as in the first recipe, select `/ouster/points` and press `Add`. The cloud of the LiDAR appears. If it does not, read the standard error of the player first: [A recorded bag plays and RViz shows nothing](docs/TROUBLESHOOTING.md#a-recorded-bag-plays-and-rviz-shows-nothing) and the two entries after it explain the causes. Press Ctrl+C in the window of the player and in the window of the transform to stop them; the folder `demo` of the layout root keeps the archive and the unpacked bag, together about 2.5 GiB, until you delete them.

### More to try

- A velocity topic as an AR tool, with a stop button, and no robot: [Expose a ROS topic as an AR tool](docs/GETTING_STARTED.md#expose-a-ros-topic-as-an-ar-tool).
- The semantic templates of the AR tools, one topic for each of ten of them, published by `scripts\publish_template_topics.py`: [AR tools from the ROS graph](docs/OPERATOR_GUIDE.md#ar-tools-from-the-ros-graph).
- Picking: the hand ray of the headset publishes a point on `/clicked_point`: [Pick a point in the scene](docs/GETTING_STARTED.md#pick-a-point-in-the-scene); a goal, an initial pose and an area: [Spatial picking](docs/OPERATOR_GUIDE.md#spatial-picking).
- Geolocation: a synthetic `NavSatFix` on `/gps/fix` sets the geographic anchor of the scene; the path is unit-tested and has never been checked against a live GPS source: [Geolocation](docs/OPERATOR_GUIDE.md#geolocation).

## Documentation

| Document | What it covers |
|---|---|
| [Installation](docs/INSTALL.md) | The machine, the toolchain, the dependencies and this repository, step by step, with the commands |
| [Getting started](docs/GETTING_STARTED.md) | The first program that you run, and what you see |
| [Architecture](docs/ARCHITECTURE.md) | How the code is structured, which part owns what, and why |
| [API](docs/API.md) | The public interface, with examples that are compiled with the repository |
| [Configuration](docs/CONFIGURATION.md) | Every environment variable and build option |
| [Testing](docs/TESTING.md) | How to run the tests, what they cover and what they do not |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Symptoms, causes and fixes |
| [SDK compatibility](docs/SDK_COMPATIBILITY.md) | The supported QarOS SDK drops and the QarOS behaviour this repository relies on |
| [Developing](docs/DEVELOPING.md) | The layout of the repository and the conventions for changing it |
| [Verification](docs/VERIFICATION.md) | The checks of each level, how to run them, and their status |
| [Wire protocol](docs/WIRE_PROTOCOL.md) | The protocol between the panel page and the server |
| [Operator guide](docs/OPERATOR_GUIDE.md) | Using the plugin in RViz: the panel, peers, tools and modes |
| [Log reference](docs/LOG_REFERENCE.md) | The log lines of the plugin and what they mean |
| [Changelog](CHANGELOG.md) | What each version contains |
| [License](LICENSE) | The license of this repository |

## The QarOS SDK is not included

The QarOS runtime, which consists of `qar-streaming-c.dll`, `qar_streaming.dll`, the zenoh and GStreamer stacks and the Visualizer `qar-streaming-viz.exe`, is QuaternAR's proprietary binary distribution and is not part of this repository. You obtain a drop of it from QuaternAR. The crash reports of the Visualizer carry the name of a second program of the same SDK folder, `qar-stereokit-gui.exe`.

No part of the SDK is copied into this repository. The C header of the QarOS C API is vendored by the repository qar_ogre_adapter and installed with its libraries.

You do not need the SDK to build this repository or to run its tests. Nothing links against the SDK: the library `qar-streaming-c.dll` is loaded at run time from the folder that a program passes in, and configuring this repository does not look at the SDK. You need the SDK to connect to QarOS.

Point the plugin at the SDK with the environment variable `QAR_RUNTIME_BINARIES_DIR`, which holds the `bin` folder of the SDK; the live launcher sets it from its `-SdkRoot` parameter.

This stack was verified against the QarOS SDK drop of 2026-09-28 and needs at least the drop of 2026-09-21; the [SDK compatibility](docs/SDK_COMPATIBILITY.md) document says how to identify the drop you have and what changed between drops. At the time of writing, the public QarOS repository <https://github.com/Quaternar/qaros> publishes a later generation of the C API. This stack does not build against it: the session set-up (create, serialise, join and invite), the hand of a gesture, the time points and the init structures of volumes and panels would have to be ported.

## Platform support

The stack runs on Windows 11, 64-bit, and is built with the Microsoft C++ toolset v142 (Visual Studio 2019). It is run from a local desktop session. Linux is not built or tested.

## License

This repository is licensed under the MIT license; the text is in [LICENSE](LICENSE), and every C++ source and header carries the identifier of the license and the copyright line of QuaternAR.

Third-party material in this repository:

- `third_party/stb` holds the single-header PNG writer `stb_image_write.h`; its README names the origin.
- GoogleTest is downloaded when you configure, only for the tests; it is not part of this repository and is not installed with it.
