# Configuration

This page lists what you can set: the options of the build, the parameters of the scripts, and the environment variables that the plugin, the libraries that it loads and the scripts read, each with its default. [Installation](INSTALL.md) explains the variables of the layout, `ROS2_INSTALL_ROOT`, `QAR_QT_ROOT`, `QAR_SDK_ROOT`, `QAR_PREFIX`, `QAR_DEPS_ROOT` and `QAR_VENV`, and how they get their values. The [operator guide](OPERATOR_GUIDE.md) says what each setting does in a running session. The tables are generated from the CMake files, from the parameter blocks of the scripts and from the code, so that a default in a table is the default that the code has.

## Build options

The plugin is one ament package, `qar_rviz_streaming`, and `scripts\build_plugin.ps1` is the way to build it: the repository root is not a colcon package, so the script hands colcon the folder of the package with `--paths`. colcon installs with `--merge-install` into the prefix, which then holds `bin\qar_rviz_streaming.dll`, `share\qar_rviz_streaming\plugin_description.xml` and the overlay `local_setup.ps1`, which you dot-source so that RViz finds the plugin. The build tree and the logs of colcon are in the folder `build\colcon` of the repository.

The script is not interactive. It does these things in this order:

1. It resolves the four locations of its parameters and stops when the two upstream packages are not installed in the prefix.
2. It imports the build environment of Visual Studio 2019 (MSVC v142, x64), the toolset that the ROS 2 distribution was compiled with: the compiler `cl.exe`. The generator Ninja is not part of that environment and has to be on `PATH` from its own install.
3. It dot-sources `scripts\env_setup.ps1`, which prepares the session with the distribution, Qt and the virtual environment.
4. It runs the repairs of the distribution that `find_package(rviz_common)` needs, `scripts\fix_missing_vendor_cmake_configs.ps1`, `scripts\fix_missing_eigen.ps1` and `scripts\fix_stale_pixi_paths.ps1`; each one does nothing when its repair is already in place.
5. It runs `colcon build` with the Ninja generator and the configuration `RelWithDebInfo`, and it points CMake at the Python of the virtual environment, because the helpers of ament need `catkin_pkg`, `lark` and `empy`, which the system Python lacks.

These are the parameters of the script:

| Parameter | Alias | Type | Default | Meaning |
|---|---|---|---|---|
| `-Prefix` | - | string | empty | Install prefix. Default: the environment variable QAR_PREFIX. Required. |
| `-Ros2Root` | `-Ros2DistroRoot`, `-Ros2InstallRoot` | string | empty | ROS 2 Humble distribution root. Default: the environment variable ROS2_INSTALL_ROOT. Required. |
| `-QtRoot` | `-QtVersionDir` | string | empty | Qt 5.15.2 msvc2019_64 folder. Default: the environment variable QAR_QT_ROOT. Required. |
| `-Venv` | - | string | empty | Python 3.10 virtual environment (ament's Python helpers need catkin_pkg, lark and empy; the system Python lacks them). Default: the environment variable QAR_VENV. Required. |
| `-CMakeArgs` | - | string[] | empty list | Extra arguments for the plugin's CMake configure step, for example -CMakeArgs '-DQAR_OGRE_ADAPTER_SOURCE_DIR=C:\src\qar_ogre_adapter'. |
| `-Clean` | - | switch | off | Delete <repository>\build\colcon (colcon's build tree and logs) before building. Nothing else is deleted. |

The CMake files of the repository define the settings in the next table. A single-configuration generator builds `RelWithDebInfo` unless you name another type, because the ROS 2 and Qt binaries are release builds and a debug C runtime in the same process crashes on load.

| Name | Type | Default | Meaning | Defined in |
|---|---|---|---|---|
| `QAR_BUILD_DOC_SNIPPETS` | BOOL | `ON` | Build the compile-checked documentation snippets | `CMakeLists.txt` |
| `QAR_BUILD_EXAMPLES` | BOOL | `ON` | Build the examples under examples/ | `CMakeLists.txt` |
| `QAR_BUILD_TESTS` | BOOL | `ON` | Build the unit tests and the layering guard tests | `CMakeLists.txt` |
| `QAR_PREFIX` | STRING | `$ENV{QAR_PREFIX}` | Install prefix of the upstream repositories | `cmake/QarBuildSupport.cmake` |
| `QAR_QT_ROOT` | PATH | unset: the value of the environment variable `QAR_QT_ROOT` is used; the configure step stops when neither is set in the modules that require it | Qt 5.15.2 (msvc2019_64). The folder must contain `lib/cmake/Qt5/Qt5Config.cmake`. | `cmake/QarBuildSupport.cmake` |
| `QAR_ROS2_INSTALL_ROOT` | PATH | unset: the value of the environment variable `ROS2_INSTALL_ROOT` is used; the configure step stops when neither is set | ROS 2 Humble distribution (ros2-windows). The folder must contain `local_setup.bat`. | `cmake/QarBuildSupport.cmake` |
| `QAR_VENDOR_INCLUDE_DIR` | PATH | `${dir}/third_party/qar/include` | Folder with the vendored qar_streaming.h | `cmake/QarBuildSupport.cmake` |

The plugin finds the two upstream repositories, qar_ogre_adapter and qar_qt_streaming, as installed CMake packages in `QAR_PREFIX`, which is one folder or several folders separated by semicolons; each folder has to exist, and a `-DQAR_PREFIX` option wins over the environment variable. The script passes its `-Prefix` as `-DQAR_PREFIX`. The web libraries `qar_ui_model`, `qar_ros_ui` and `qar_web_panels` are always built from the tree of this repository, with the plugin. For development you build against checkouts instead: you pass `-DQAR_OGRE_ADAPTER_SOURCE_DIR=<checkout>` and `-DQAR_QT_STREAMING_SOURCE_DIR=<checkout>` through the parameter `-CMakeArgs`. The configure step then adds the modules that the plugin needs from the checkout, refuses a checkout whose version has another major and minor version than the one that the plugin asks for, and takes the vendored header from the checkout, which sets `QAR_VENDOR_INCLUDE_DIR`. The script skips the test for installed packages when you pass either option. In that mode the QarOS SDK folder is read from `QAR_SDK_ROOT`, and the configure step only warns when `bin\qar-streaming-c.dll` is not there, because nothing links against the SDK: the plugin loads the library at run time from the folder that `QAR_RUNTIME_BINARIES_DIR` names.

Every script that needs a folder takes it from a parameter and, when you omit the parameter, from an environment variable, and the parameter wins; no script has a built-in default for these folders. A location that is not given stops the script with a message that names the parameter and the variable, and a folder that does not exist or lacks its marker file stops it with a message that names the folder.

## Launcher parameters

Two scripts start RViz with the plugin. `scripts\run_live_rviz_stream_real.ps1` starts the live session, and without `-Confirm` it only prints the configuration that it resolved and starts nothing, as [Getting started](GETTING_STARTED.md#check-the-launch-configuration) shows. `scripts\run_rviz_with_plugin.ps1` starts the offline preview, and its scripted modes make the panel write a result and quit RViz. The parameters of the launcher:

| Parameter | Alias | Type | Default | Meaning |
|---|---|---|---|---|
| `-SdkRoot` | `-QarSdkRoot` | string | empty | QarOS SDK install root (the folder with bin\qar-streaming-c.dll). Default: the environment variable QAR_SDK_ROOT. Required. |
| `-Prefix` | - | string | empty | Install prefix that holds the plugin (the folder with local_setup.ps1). Default: the environment variable QAR_PREFIX. Required. Without -Confirm a missing overlay is only reported. |
| `-QtRoot` | `-QtVersionDir` | string | empty | Qt 5.15.2 msvc2019_64 folder. Default: the environment variable QAR_QT_ROOT. Required. |
| `-Ros2Root` | `-Ros2DistroRoot`, `-Ros2InstallRoot` | string | empty | ROS 2 Humble distribution root. Default: the environment variable ROS2_INSTALL_ROOT. Required. |
| `-Scene` | - | string | empty | RViz config. Default: scene3_grid_marker.rviz from <Prefix>\share\qar_rviz_streaming\config\test_scenes. |
| `-Visualizers` | - | int | `0` | How many QarOS Visualizers to invite AUTOMATICALLY at connect (0-8). Default 0: connecting opens the session and invites nothing; the operator spawns Visualizers on demand with the panel's "Invite peer" button, and a real-device session often wants none at all. Set 1-8 to invite automatically. Range 0 to 8. |
| `-InvitePeers` | - | string[] | empty list | Real devices to invite as soon as the session comes up, as `kind[:hostname]` entries. Kinds: visualizer, hololens, quest, zed - only hololens takes (and requires) a hostname, because it is the only device type the SDK can address by name. Example: -InvitePeers hololens:HOLOLENS-A2B3,hololens:HOLOLENS-C4D5 An invited device lands in the allow-list and is admitted by exact peer id, so -RealPeers is not needed for it. A device that fails to invite is logged and skipped, never fatal. |
| `-RealPeers` | - | switch | off | Set up for a demo with REAL onboarded peers (HoloLens, METALENSE 2, Quest, Android, or a Visualizer on another machine). Sets QAR_PANEL_PEER_ADMISSION=new, without which those peers are silently refused: they join via QAROS onboarding, never via qar_session_invite_peer, so the default invited-only allow-list does not contain them. |
| `-TraceLogs` | - | switch | off | Collect FULL logs (what a bug report needs): QAR_LOG_SEVERITY=trace, zenoh at debug (RUST_LOG + QAR_ZENOH_LOG) and the adapter breadcrumb trace (QAR_ADAPTER_TRACE, in rviz_live_*.err.log). Expect GB-sized logs, and do NOT judge stalls or latency in this mode: its own I/O can cause them. Without it (the default) QarOS logs at `warn`, zenoh logging is off and no per-frame trace is on. |
| `-KeepLogEnv` | - | switch | off | In QUIET mode (the default) loud logging values already present in the shell (QAR_LOG_SEVERITY / QAR_PANEL_LOG_SEVERITY trace\|debug, RUST_LOG with trace\|debug, QAR_ZENOH_LOG, QAR_ADAPTER_TRACE*, QAR_ADAPTER_FRAME_TRACE) are CLEARED with a notice: such a value silently turns the "quiet" run into a TRACE run (zenoh at debug in RViz, the QarOS library at TRACE because it obeys QAR_LOG_SEVERITY over the panel's own setting). -KeepLogEnv keeps them as they are. Quieter values you set yourself (e.g. QAR_LOG_SEVERITY=error) always win, in every mode. |
| `-TimingCsv` | - | switch | off | Record per-frame stage timing for the stutter proof: sets QAR_VIDEO_TIMING_DIR to a fresh <LogDir>\timing\<stamp> folder (printed) and the plugin writes epoch-microsecond CSVs there - video_*.csv (arrival -> decode -> capture -> try_begin -> submit per AR video panel), rx_*.csv (every received message), render3d*.csv (the 3D stereo stream's ready/not_ready runs and submits) and gui_gaps.csv (Qt render-tick gaps) - buffered in memory and flushed about once a second by a background thread, never on the GUI thread. Pair it with the publisher's timing_csv:=<dir>\publisher.csv and analyse with scripts\analyze_video_timing.py. Independent of -TraceLogs; do not combine the two when judging stalls (trace logging is itself I/O). |
| `-NoDdsProfile` | - | switch | off | Do NOT export scripts\fastdds_large_data.xml as FASTRTPS_DEFAULT_PROFILES_FILE. By default the launcher does, for everything it starts (rviz2 - RViz's own Image display and the plugin's AR video reader are participants of that one process - and the marker publisher), unless the variable is already set in the shell (yours wins). The profile enlarges Fast-DDS's shared-memory segment from 512 KB to 64 MB: with 6 MB video frames the default segment drops fragments, and every dropped fragment is a reliable-repair round trip, i.e. delivery jitter and stalls on EVERY subscriber of the topic. scripts\publish_video_file.py applies the same file to itself, so the publisher (the sending side, where it matters most) and rviz2 agree. See the profile's own comments. |
| `-LogDir` | - | string | empty | Folder for the rviz2 logs (rviz_live_<stamp>.out.log and .err.log) and the -TimingCsv folders. Default: %TEMP%\qar_rviz_logs. |
| `-Confirm` | - | switch | off | Must be passed explicitly to actually launch. Without it the script only PRINTS the plan and the resolved env (dry run), so it is safe to invoke for inspection. |

The parameters of the preview script:

| Parameter | Alias | Type | Default | Meaning |
|---|---|---|---|---|
| `-Config` | - | string | empty | RViz config to load. Default: qar_streaming_dev.rviz from <Prefix>\share\qar_rviz_streaming. |
| `-AutoDump` | - | string | empty | Path of the diagnostics dump file (context validity, scene manager name, camera and node counts, and a synthetic peer add/stream/remove round trip). Enables the auto-dump-and-quit mode. |
| `-AutoCapture` | - | string | empty | Output prefix of the offscreen capture mode (writes *_color.png, *_depth.png, *_depth.f32 and *_capture.txt). |
| `-AutoStereo` | - | string | empty | Output prefix of the single-peer stereo capture mode (writes *_frameA/B_left\|right_color.png, the depth images and *_autostereo.txt). |
| `-AutoMulti` | - | string | empty | Output prefix of the multi-peer batched render mode (writes *_automulti.txt and *_peerN_color.png). |
| `-MultiPeers` | - | int | `3` | Number of peers in -AutoMulti mode (default 3). |
| `-MultiTicks` | - | int | `180` | Number of paced batches in -AutoMulti mode (default 180). |
| `-IpdMetres` | - | double | `0.35` | Stereo baseline of -AutoStereo mode in metres (default 0.35, wide enough for visible parallax). |
| `-StereoFrames` | - | int | `140` | Number of frames of -AutoStereo mode (default 140). |
| `-DelayMs` | - | int | `5000` | Delay before the scripted mode starts (default 5000 ms). |
| `-OutDir` | - | string | empty | Folder for scripted-mode outputs given as a plain name. Default: %TEMP%\qar_rviz_preview. |
| `-Prefix` | - | string | empty | Install prefix that holds the plugin (the folder with local_setup.ps1). Default: the environment variable QAR_PREFIX. Required. |
| `-Ros2Root` | `-Ros2DistroRoot`, `-Ros2InstallRoot` | string | empty | ROS 2 Humble distribution root. Default: the environment variable ROS2_INSTALL_ROOT. Required. |
| `-QtRoot` | `-QtVersionDir` | string | empty | Qt 5.15.2 msvc2019_64 folder. Default: the environment variable QAR_QT_ROOT. |

Most parameters of the launcher only set an environment variable of the plugin, and a value that is already in your shell wins over the parameter. The table says which variable each parameter sets:

| Parameter | Variable | Rule |
|---|---|---|
| `-Visualizers` | `QAR_PANEL_VISUALIZERS` | Set to the number, from 0 to 8, unless the variable is already set. |
| `-InvitePeers` | `QAR_PANEL_INVITE_PEERS` | The entries are joined with commas; set only when you pass entries and the variable is not already set. |
| `-RealPeers` | `QAR_PANEL_PEER_ADMISSION` | `new` with the switch and `invited` without it, unless the variable is already set. |
| `-TimingCsv` | `QAR_VIDEO_TIMING_DIR` | Always set to a fresh folder `<LogDir>\timing\<stamp>`. |
| `-TraceLogs`, `-KeepLogEnv` | the logging variables | See the table below. |
| `-NoDdsProfile` | `FASTRTPS_DEFAULT_PROFILES_FILE` | Leaves the variable as your shell has it; see [DDS profile](#dds-profile). |
| `-LogDir` | none | The folder of the RViz logs and of the timing folders, `qar_rviz_logs` in `TEMP` by default. |
| `-Scene` | none | The RViz configuration, `scene3_grid_marker.rviz` from `share\qar_rviz_streaming\config\test_scenes` of the prefix by default. |

The launcher is quiet by default, because trace logging is heavy input and output and a nearly full system drive under a virus scanner can stall every QarOS stream at once. It treats the logging variables as one set, and the table says what each of them becomes in the two modes; "removed" means that the variable does not exist in the environment of RViz.

| Variable | Quiet mode, the default | With `-TraceLogs` |
|---|---|---|
| `QAR_LOG_SEVERITY` | `warn` | `trace` |
| `QAR_PANEL_LOG_SEVERITY` | `warn` | removed |
| `QAR_PANEL_LOG_CONSOLE` | `off` | removed |
| `RUST_LOG` | removed | `zenoh=debug` |
| `QAR_ZENOH_LOG` | removed | `debug` |
| `QAR_ADAPTER_TRACE` | removed | `1` |
| `QAR_ADAPTER_TRACE_ALL`, `QAR_ADAPTER_FRAME_TRACE` | removed | removed |

A variable that already has a value in your shell is kept, with one exception: in quiet mode a loud value is cleared with a notice, unless you pass `-KeepLogEnv`. A loud value is a severity of `trace` or `debug`, a `RUST_LOG` that names `trace` or `debug`, any value of `QAR_ZENOH_LOG` or of the three adapter flags, and a flag that is spelled `0`, `false`, `off` or `no`; such a value would turn the quiet run into a trace run without a sign. A quieter value that you set yourself always wins, and when you keep a `QAR_LOG_SEVERITY` and set no `QAR_PANEL_LOG_SEVERITY`, the launcher copies the first into the second, so that the library inside RViz follows your value too. The three variables of the adapter are presence flags, which the code tests with `getenv`, so that the value `0` would switch them on; the launcher therefore removes them and never sets them to `0`.

The preview script sets the variables of the scripted mode that you ask for, `-AutoDump`, `-AutoCapture`, `-AutoStereo` or `-AutoMulti`, with `-DelayMs` as the delay, and it removes the variables of every mode that you did not ask for, so that a variable left in your shell does not start a mode. In the mode that uses them, `-IpdMetres` becomes `QAR_STEREO_IPD_M`, `-StereoFrames` becomes `QAR_PANEL_AUTOSTEREO_FRAMES`, and `-MultiPeers` and `-MultiTicks` become `QAR_PANEL_AUTOMULTI_PEERS` and `QAR_PANEL_AUTOMULTI_TICKS`. A name without a folder goes to the folder of `-OutDir`, and the script gives the verdict of the mode as its exit code: 0 for a pass, 1 for a fail and 2 when the panel wrote no result. [Scripted checks in a window](TESTING.md#scripted-checks-in-a-window) describes the modes.

## Environment variables

Every environment variable that code of this stack reads inside the RViz process is in the first table: the panel and the web libraries of this repository, the libraries of qar_ogre_adapter, and the one variable of qar_qt_streaming that the plugin reaches, `QAR_PANEL_CLOSE_MODE`. The column "Read by" names a file of this repository by its path and a file of another repository by the repository and its module; the CMake presets and the package configuration of another repository are named by the repository followed by "(its preset)", "(its package configuration)" or "(its preset and package configuration)", with the module inside the brackets when the file belongs to one. The second table lists the variables of the build, of the scripts and of the host, which are not read inside RViz.

### Variables of the plugin process

| Variable | Read by | Default | Values and parsing | Effect |
|---|---|---|---|---|
| `QAR_PANEL_LIVE_STREAM` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | Unset: the panel runs offline against in-process animated test peers (`AnimatedMockSenderFactory`) and builds no QarOS session. | Any non-empty value turns live mode on, including `0` and `off`; an empty or unset variable leaves it off. | Makes the panel live-capable. The runtime buttons read "Connect to QarOS", "Disconnect (pause)" and "Invite peer", and the QarOS session is built only when the operator presses Connect, never while the panel loads. Set by `scripts/run_live_rviz_stream_real.ps1` (always set to 1). |
| `QAR_RUNTIME_BINARIES_DIR` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | Unset: nothing is passed on and the QarOS runtime falls back to the current directory with a warning. | A non-empty value is used verbatim as a folder; an empty value is ignored. | Folder of the QarOS SDK binaries. It becomes `runtime_binaries_folder_path` of the sender factory configuration: the folder from which the runtime starts the launcher, the mixer and the Visualizer and, with dynamic loading, the folder from which `qar-streaming-c.dll` is loaded. The loader searches this folder first for the dependencies of `qar-streaming-c.dll` and never searches `PATH`. Set by `scripts/run_live_rviz_stream_real.ps1` (set to <SdkRoot>\bin). |
| `QAR_PANEL_TICK_MS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0.1` ms. | Read with `strtod`, so a numeric prefix counts (`8ms` is 8). The value must be a finite number: no digits, `nan`, `inf`, a negative value or a value above 100000 give the default; `0` is accepted. | Initial period of the panel render timer in milliseconds. The tick selector offers 16, 8, 4, 2, 1 and 0.1 ms; any other value is added to the list. A period below 1 ms runs a zero-interval timer that is throttled to the period. |
| `QAR_ADAPTER_PRIME` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, qar_ogre_adapter (`qar_qar_adapter`) | `true`. | `1`, `true`, `on` or `yes` (any case) is true; `0`, `false`, `off` or `no` is false; anything else, including an empty value, gives the default. The value is not trimmed. | Initial value of the prime flag of a render sender adapter. When the flag is on, the wait for the next frame starts right after a frame is shown, so a frame takes one tick instead of two. `OgreQarStreamingManager` gives every stream that it creates its own flag, `true` until the host calls its `set_prime_enabled`, so the variable decides only for an adapter that no manager configures. |
| `QAR_PANEL_FLOOR_PLANE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | on. | Trimmed and lower-cased; only the word `off` disables the plane, any other value (including `0` and `false`) leaves it on. | Adds an opaque companion plane under every RViz Grid display so that the AR depth map sees a solid floor instead of the gaps between the grid lines. |
| `QAR_PANEL_GEO` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | on. | Trimmed and lower-cased; only the word `off` disables it. | Subscribes to a NavSatFix topic and georeferences the QarOS app volume from it. |
| `QAR_PANEL_GPS_TOPIC` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `/gps/fix`. | Trimmed string; an empty value gives the default. | NavSatFix topic that the geolocation subscription listens on. |
| `QAR_PANEL_HEARTBEAT_S` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `2.0` seconds. | Read as a number; a value of at least 0 is accepted, anything else is ignored and the default stays. `0` turns the heartbeat off. | Period in seconds of the `HB` diagnostics line that the panel writes to its log. |
| `QAR_PANEL_NEAR_M` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0.01` m. | Trimmed and read as a number; used only when it is greater than 0. | Near clip plane of the stereo render, and the near distance advertised to QarOS with every frame. |
| `QAR_PANEL_FAR_M` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `150` m. | Trimmed and read as a number; used only when it is greater than the near distance. | Far clip plane of the stereo render, and the far distance advertised to QarOS with every frame. Depth is reciprocal in distance, so a very wide range squashes the depth colours that the Visualizer shows. |
| `QAR_PANEL_DEPTH_SCALE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `1.0`. | Trimmed and lower-cased. `norm` means 1 divided by the far distance; any other non-empty value must be a number greater than 0, otherwise the panel records the error `bad QAR_PANEL_DEPTH_SCALE '<value>' (use a number or 'norm')` and keeps 1.0. | Depth scale sent to QarOS with every frame (`QarRenderFrameShow.depth_scale`). It converts metres into what a consumer that honours it expects; with an SDK drop earlier than that of 2026-09-28 it could not make metric depth right for the Visualizer, which needs `QAR_DEPTH_ENCODING=ndc`. |
| `QAR_PANEL_LAUNCHER_CONFIG` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `{"enableMixer": true}`. | A non-empty value is passed verbatim as the launcher configuration JSON (`QarSessionCreateInit.launcher_config_json`); the panel does not validate it. | JSON configuration of the launcher that the QarOS library starts itself, for example `{"enableMixer": false}` for the direct RViz-to-Visualizer path used when debugging. |
| `QAR_PANEL_LOG_DIR` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | `%TEMP%\quaternar`; the relative folder `quaternar` when neither `TEMP` nor `TMP` is set. | A non-empty value is used as the log folder (`log_folder_path`). | Folder of the QarOS library log files; the per-frame trace CSV is written to the same folder. Before a run the live launcher renames every `*.log` file above 256 MB in it to `<name>.prev`. |
| `QAR_PANEL_LOG_SEVERITY` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | `trace`. | Trimmed and lower-cased: `trace`, `debug`, `info`, `warn` (or `warning`), `error` or `fatal`. A value that is present but not a severity is skipped with a warning and the next input is used. | Severity of the in-process QarOS library (`QarLibraryInit.log_severity`). Precedence: this variable, then `QAR_LOG_SEVERITY`, then `trace`. Set by `scripts/run_live_rviz_stream_real.ps1` (quiet mode: warn; -TraceLogs: removed; mirrors a QAR_LOG_SEVERITY that was kept). |
| `QAR_LOG_SEVERITY` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_qar_adapter`) | Unset. | The words that `parse_log_severity` accepts: `trace`, `debug`, `info`, `warn`, `warning`, `error` and `fatal`, in any case and trimmed; numbers and other words are not accepted. The QarOS library obeys this variable over the severity in its init struct, so the factory overwrites it in the process environment with `_putenv_s`, with the severity of `Config::log_severity`, before the library starts. | QarOS-wide log severity. The runtime, and the launcher, mixer and Visualizer processes it starts, inherit it. Set by `scripts/run_live_rviz_stream_real.ps1` (quiet mode: warn; -TraceLogs: trace). |
| `QAR_PANEL_LOG_CONSOLE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | on for `trace`, `debug` and `info`; off for `warn` and above. | Trimmed and lower-cased: `1`, `true`, `on` or `yes` against `0`, `false`, `off` or `no`; any other value gives a warning and is ignored. | Whether the in-process QarOS library also echoes every log line to the standard output of RViz. Set by `scripts/run_live_rviz_stream_real.ps1` (quiet mode: off). |
| `QAR_PANEL_VISUALIZER_GUID` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `auto`: a fresh UUID per invite (`visualizer_conn_suffix`). | A non-empty value: `off` (any case) means an empty suffix, the stock single-Visualizer behaviour; anything else is used verbatim. An empty value counts as unset. | Payload appended to the Visualizer connection string. Distinct payloads give distinct Visualizer instances, which is what lets several Visualizers be invited and streamed to at once. |
| `QAR_PANEL_GL_SUBMIT` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `render-into`. | Trimmed and lower-cased: `supply-own`, `render-into` or empty (`render-into`); any other value records the error `unknown QAR_PANEL_GL_SUBMIT '<value>' (use render-into\|supply-own)`. | How GL textures are handed to QarOS. `render-into` copies the Ogre colour and depth images into the textures that QarOS owns; `supply-own` passes the plugin's own texture ids, which renders black against the real SDK. |
| `QAR_PANEL_VISUALIZERS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | `0`. | Trimmed and read as an integer; used when it is greater than 0, otherwise 0. | Number of Visualizers invited automatically at connect. Normally 0; the "Invite peer" button invites one on demand. Set by `scripts/run_live_rviz_stream_real.ps1` (from -Visualizers (default 0)). |
| `QAR_PANEL_MAX_PEERS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `8`. | Trimmed and read as an integer; used when it is greater than 0. | Upper limit of concurrently streamed peers. |
| `QAR_PANEL_APP_VOLUME` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | on. | Trimmed and lower-cased; only the word `off` disables it. | Creates the app volume at connect. Off creates none, so streams are created with a null app volume id, a path meant only for sessions that run without the mixer. |
| `QAR_PANEL_VOLUME_NAME` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `RViz Scene`. | Trimmed string; used when it is non-empty. | Display name of the app volume (`QarAppVolumeInit.display_name`). |
| `QAR_PANEL_VOLUME_SIZE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `40` m. | Trimmed and read as a number; used when it is greater than 0, for width, length and height alike. | Extent of the app volume, a cube; the stack creates its volume with this width, length and height. Nothing clips the content to the cuboid: the volume states where the application is meant to be, and gestures and layout are derived from it, so a large scene needs either a large box or a small app scale. |
| `QAR_PANEL_VOLUME_Y` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `1.0` m. | Trimmed and read as a number; any number is accepted, including 0 and negative values. | Height of the centre of the app volume above the floor. |
| `QAR_PANEL_VOLUME_SCALE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `1.0`. | Trimmed and read as a number; used when it is greater than 0. | App scale of the app volume: how many metres of the QarOS room one metre of RViz content spans. |
| `QAR_PANEL_PEER_ADMISSION` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | `invited`. | Trimmed and lower-cased: `invited`, `new`, `named` or `any`; empty means `invited`; any other value records the error `unknown QAR_PANEL_PEER_ADMISSION '<value>' (use invited\|new\|named\|any)`. | Which peers of the session subscription get a render stream: only the peers the plugin invited itself (`invited`); those plus peers that join after the settle time (`new`); those plus peers whose display name matches `QAR_PANEL_PEER_NAME_FILTER` (`named`); or every peer, the internal peers of the runtime included (`any`, a diagnostic setting). Set by `scripts/run_live_rviz_stream_real.ps1` (new with -RealPeers, otherwise invited). |
| `QAR_PANEL_PEER_SETTLE_MS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `5000`. | Trimmed and read as an integer; used when it is 0 or greater. | With admission `new`: the time after connect during which the peers that are present count as the runtime's own. A peer that appears later is treated as a real consumer and gets a stream. |
| `QAR_PANEL_PEER_NAME_FILTER` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | Empty: the policy then admits only invited peers. | Trimmed string; a case-insensitive substring of the peer display name. | Name match for admission `named`. The display names that the SDK generates change between updates, as observed in live sessions, so this rarely matches. |
| `QAR_PANEL_INVITE_PEERS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | Empty. | Trimmed, comma-separated entries `kind[:hostname]`. Kinds: `visualizer` or `viz`; `hololens`, `hl` or `hololens2`; `quest` or `metaquest`; `zed`. An unknown kind is logged and skipped. | Real devices that are invited right after connect. An invited device lands in the allow-list and is admitted by its exact peer id, so it needs no admission heuristic. Set by `scripts/run_live_rviz_stream_real.ps1` (from -InvitePeers). |
| `QAR_SCENE_LIGHT` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | `both` in live mode; off otherwise. | Trimmed; lower-case with whitespace dropped and `_` read as `-`: `off` or `none`; `omni` or `point`; `per-peer` or `peer`; `both` or `omni-and-per-peer`. An unparseable value keeps the default without an error. | Lighting rig that the plugin adds to the shared RViz scene while it streams: none, an omnipresent rig of directional lights with a raised ambient term, one light at the head of each peer, or both. Set by `scripts/run_live_rviz_stream_real.ps1` (both when unset). |
| `QAR_SCENE_LIGHT_HEADLIGHT` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, `scripts/run_live_rviz_stream_real.ps1` | The RViz headlight is hidden while streaming with a lighting mode other than off. | Trimmed and lower-cased; `keep` or `on` keeps the RViz headlight (`MainDirectional`); any other value hides it while a lighting mode is active. With lighting mode off the headlight is always kept. | Keeps or hides the camera-tracking headlight of RViz while the plugin streams. The headlight is restored when streaming stops. |
| `QAR_SCENE_LIGHT_AMBIENT` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0.30`. | Trimmed and read as one number that sets all three colour channels; an invalid value keeps the default. | Grey level of the scene ambient light. |
| `QAR_SCENE_LIGHT_INTENSITY` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0.75`. | As `QAR_SCENE_LIGHT_AMBIENT`: one number for all three channels. | Grey level of the diffuse colour of the key light. |
| `QAR_SCENE_LIGHT_FILL` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0.55`. | Trimmed and read as a number; an invalid value keeps the default. | Strength of the fill lights from below, as a fraction of the key light. |
| `QAR_SCENE_LIGHT_PEER` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0.35`. | As `QAR_SCENE_LIGHT_AMBIENT`: one number for all three channels. | Grey level of the diffuse colour of the light at the head of each peer. |
| `QAR_SCENE_LIGHT_RANGE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0`: no attenuation. | Trimmed and read as a number; an invalid value keeps the default. | Attenuation range of the lights that the plugin creates. |
| `QAR_STEREO_IPD_M` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0.064` m (the interpupillary distance of `MockQarFrameInfoSource`). | A comma is read as a decimal point; read as a number; used when it is greater than 0. | Eye distance of the in-process animated test peers that the offline mode and the scripted stereo capture use. Live peers send their own eye poses, so it has no effect on a live session. Set by `scripts/run_rviz_with_plugin.ps1` (from -IpdMetres). |
| `QAR_PANEL_CLOSE_MODE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp`, qar_qt_streaming | `destroy`. | Trimmed and lower-cased: `destroy` or `notice`; empty means unset; any other value runs the default and logs a warning. | What a closing streamed Qt window does to its AR panel. `destroy` closes the panel and destroys its video sender; `notice` leaves the panel up as a closed-window notice and keeps the video sender until the session ends. |
| `QAR_PANEL_HIDDEN_DISPLAYS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | on. | Case-insensitive words `1`, `true`, `on` or `yes` against `0`, `false`, `off` or `no`, not trimmed; any other value, or an empty value, gives the default. | Starts the hidden RViz Displays panel automatically on connect. Off still lets the operator start it from the panel list. |
| `QAR_PANEL_HIDDEN_DISPLAYS_SIZE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `480x720` pixels. | `WIDTHxHEIGHT` in decimal with `x` or `X` and optional spaces. Each side is clamped to 200..2000 with a warning; a malformed value gives the default and a warning. | Fixed size of the hidden Displays panel. It is read once, when the panel is created, because a size change would rebuild the sender. |
| `QAR_PANEL_WEB_HOST` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | The LAN address of this machine: found with a UDP connect probe, else the first usable network interface, else `127.0.0.1`. | A non-empty value becomes `WebPanelServerConfig::advertised_host`. It is only the host that is written into the page URLs; the server keeps listening on all interfaces. | Host name or address that the QarOS browser is told to fetch panel pages from. |
| `QAR_PANEL_WEB_PORT` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `0`: the operating system assigns a free port. | Converted with `QByteArray::toUShort()` without a check: a malformed value, or one above 65535, becomes 0. | TCP port of the panel web server. Pin it for a firewall rule. |
| `QAR_PANEL_WEB_SIZE` | `qar_web_panels/src/url_panel_sizing.cpp` | Unset: the size follows from the page. | `<width>x<height>` in metres with `x` or `X`. Each part must be a whole token of at most 32 characters that is a positive number. A malformed value is ignored with a note that ends in `is not <width>x<height> in metres - ignored`; an empty value is ignored without a note. It is read each time a host calls `apply_sizing_overrides`. | Pins every generated AR web panel to a fixed physical size in metres; the size limits widen to allow it. |
| `QAR_PANEL_WEB_SCALE` | `qar_web_panels/src/url_panel_sizing.cpp` | `1.0`. | A positive number as a whole token (spaces and tabs are trimmed). A malformed value is ignored with a note that ends in `is not a positive number - ignored`; an empty value is ignored without a note. It is read each time a host calls `apply_sizing_overrides`. | Multiplier on the derived size of AR web panels, applied before the bounds of the size. |
| `QAR_VIDEO_TIMING_DIR` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_ar_core`) | Unset: no timing files. | A non-empty value is a folder, created lazily by the writer thread; an unset or empty value disables the feature. | Opt-in per-frame stage timing: when the variable is set, a background writer thread writes timing CSV files into the folder. Set by `scripts/run_live_rviz_stream_real.ps1` (-TimingCsv: a fresh <LogDir>\timing\<stamp> folder). |
| `QAR_ZENOH_LOG` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_qar_adapter`) | Unset: off. | A null or empty value logs `zenoh logging: off (QAR_ZENOH_LOG not set)` and does nothing. The request is honoured only while the library severity is `trace` or `debug`; otherwise it is logged as ignored. The value is the tracing filter passed to `zc_init_log_from_env_or`. | Starts the zenoh logger inside the process that hosts the library. The zenoh log is the only place that says why a session fails to come up. Windows only. Set by `scripts/run_live_rviz_stream_real.ps1` (-TraceLogs: debug; otherwise removed). |
| `QAR_ADAPTER_TRACE` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_qar_adapter`) | Off. | Presence test (the value of `getenv` is not null): any value, including `0` and `off`, enables the trace; remove the variable to disable it. | Breadcrumb trace on standard error: peer ids; a line with the elapsed time before and after each main call of the session start-up (library load and init, runtime and session creation, join, own peer spec, app volume, peer subscription), of the creation of a sender, of an invitation, of an anchor change and of the first three begin-frame polls; and the per-frame submit steps (every frame up to the 150th, then one in 300). Set by `scripts/run_live_rviz_stream_real.ps1` (-TraceLogs: 1; otherwise removed). |
| `QAR_ADAPTER_TRACE_ALL` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_qar_adapter`) | Off. | Presence test, as `QAR_ADAPTER_TRACE`. | Defeats the one-in-300 sampling of `QAR_ADAPTER_TRACE`: every frame is traced, with four flushed writes per frame. Set by `scripts/run_live_rviz_stream_real.ps1` (always removed). |
| `QAR_ADAPTER_FRAME_TRACE` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_qar_adapter`) | Off. | Presence test, read once per process. | Writes a per-frame CSV, `qar_frame_trace_<yyyyMMdd_HHmmss>_<tag>.csv`, into the QarOS log folder; the tag is derived from the peer id. Set by `scripts/run_live_rviz_stream_real.ps1` (always removed). |
| `QAR_ADAPTER_FLIP_Y` | qar_ogre_adapter (`qar_qar_adapter`) | Off. | Presence test. | Flips Y in the copy from the Ogre texture to the QarOS texture (diagnostic). |
| `QAR_ADAPTER_PROBE_EVERY` | qar_ogre_adapter (`qar_qar_adapter`) | `600` frames: the probe of the first copied frame is always written, then one every 600 frames, six probes at most. | Read with `strtol` in base 10 without an error check, when the copy hook sets itself up: a value above 0 replaces 600; anything else, including 0 and garbage, leaves only the probe of the first copied frame. | Writes a copy and depth probe to standard error at the first copied frame and then every N copied frames, six probes at most: a `---- probe #<n> at frame <n> ----` header, a depth histogram and an ASCII depth map. |
| `QAR_POSE_WORLD` | qar_ogre_adapter (`qar_ar_core`, `qar_ogre_streaming`) | Unset: identity (the library default). A host with a ROS world needs `zup`. | Case-sensitive: exactly `zup` enables the Y-up to Z-up basis change; anything else is identity. The pose conversion reads it once per process; the spatial-pick code reads it on every call. | Conversion between the QarOS world (Y up) and the ROS world (Z up) for head poses and for spatial picks; the two readers must agree. The library default (identity) differs from the value that a ROS host needs (`zup`), so a host that does not set it sees poses in the wrong basis. Set by `scripts/run_live_rviz_stream_real.ps1` (always zup). |
| `QAR_POSE_YAW_DEG` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_ar_core`, `qar_ogre_streaming`) | `180`. | Read with `std::stof`: a numeric prefix is used, garbage gives the default, `nan` is accepted as NaN. Degrees. | Yaw about the ROS +Z axis, applied after the basis change (`zup` mode only). Set by `scripts/run_live_rviz_stream_real.ps1` (180 when unset). |
| `QAR_POSE_HEIGHT_M` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_ar_core`, `qar_ogre_streaming`) | `0`. | Read with `std::stof`; garbage gives the default. | Eye height in metres added along the ROS +Z axis (`zup` mode only). Set by `scripts/run_live_rviz_stream_real.ps1` (0 when unset). |
| `QAR_POSE_BACK_M` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_ar_core`, `qar_ogre_streaming`) | `0`. | Read with `std::stof`; garbage gives the default. | Stand-off in metres behind the facing direction; it follows the yaw (`zup` mode only). Set by `scripts/run_live_rviz_stream_real.ps1` (0 when unset). |
| `QAR_DEPTH_ENCODING` | `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_ogre_streaming`) | `metric` (the library default). The QarOS Visualizer needs `ndc`. | Case-sensitive: exactly `ndc` selects `gl_FragCoord.z`; anything else writes metric eye-space distance in metres. Read once per process. | Encoding of the depth texture. With an SDK drop earlier than that of 2026-09-28 the QarOS Visualizer expected the raw window-space depth value in the range 0 to 1 and inverted it itself, so metric depth rendered wrongly there. The library default differs from the value that the Visualizer needs, so a host that does not set it sends metric depth. Set by `scripts/run_live_rviz_stream_real.ps1` (ndc when unset). |
| `QAR_DEPTH_SHADER_SCALE` | qar_ogre_adapter (`qar_ogre_streaming`) | `1.0`. | Metric mode only: read with `std::stof` and accepted when it is greater than 0, otherwise 1.0. | Multiplier on the metric depth that the depth shader writes. |
| `QAR_PANEL_AUTODUMP` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | Unset. | A file path; a non-empty value enables the mode. | Non-interactive verification mode: dumps diagnostics, including a round trip through the in-process animated test peers, to the file after a delay and quits RViz. Set by `scripts/run_rviz_with_plugin.ps1` (from -AutoDump). |
| `QAR_PANEL_AUTODUMP_DELAY_MS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `4000`. | Read as an integer; a malformed value or a value of 0 or less gives 4000. | Delay before `QAR_PANEL_AUTODUMP` runs. Set by `scripts/run_rviz_with_plugin.ps1` (from -DelayMs). |
| `QAR_PANEL_AUTOCAPTURE` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | Unset. | A file name prefix; a non-empty value enables the mode. | Non-interactive verification mode: renders the live RViz scene into the offscreen colour and depth targets, writes PNG files and quits RViz. It is also the base name that the "Capture offscreen frame" button writes to. Set by `scripts/run_rviz_with_plugin.ps1` (from -AutoCapture). |
| `QAR_PANEL_AUTOCAPTURE_DELAY_MS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `6000`. | Read as an integer; a malformed value or a value of 0 or less gives 6000. | Delay before `QAR_PANEL_AUTOCAPTURE` runs. Set by `scripts/run_rviz_with_plugin.ps1` (from -DelayMs). |
| `QAR_PANEL_AUTOSTEREO` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | Unset. | A file name prefix; a non-empty value enables the mode. | Non-interactive verification mode: creates one animated test peer with a stereo stream, captures left and right colour and depth PNG pairs at two poses, writes metadata and parallax statistics and quits RViz. Set by `scripts/run_rviz_with_plugin.ps1` (from -AutoStereo). |
| `QAR_PANEL_AUTOSTEREO_DELAY_MS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `6000`. | Read as an integer; a malformed value or a value of 0 or less gives 6000. | Delay before `QAR_PANEL_AUTOSTEREO` runs. Set by `scripts/run_rviz_with_plugin.ps1` (from -DelayMs). |
| `QAR_PANEL_AUTOSTEREO_FRAMES` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `140`. | Read as an integer; a malformed value or a value below 2 gives 140. | Number of frames rendered between the two captured poses. Set by `scripts/run_rviz_with_plugin.ps1` (from -StereoFrames). |
| `QAR_PANEL_AUTOMULTI` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | Unset. | A file name prefix; a non-empty value enables the mode. | Non-interactive verification mode: adds several animated test peers with independent views, batch-renders them for a few seconds, writes the achieved frame rate per peer and the timing per batch, and quits RViz. Set by `scripts/run_rviz_with_plugin.ps1` (from -AutoMulti). |
| `QAR_PANEL_AUTOMULTI_DELAY_MS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `6000`. | Read as an integer; a malformed value or a value of 0 or less gives 6000. | Delay before `QAR_PANEL_AUTOMULTI` runs. Set by `scripts/run_rviz_with_plugin.ps1` (from -DelayMs). |
| `QAR_PANEL_AUTOMULTI_PEERS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `3`. | Read as an integer; a malformed value or a value below 1 gives 3; the count is limited to 8. | Number of animated test peers for `QAR_PANEL_AUTOMULTI`. Set by `scripts/run_rviz_with_plugin.ps1` (from -MultiPeers). |
| `QAR_PANEL_AUTOMULTI_TICKS` | `qar_rviz_streaming/src/qar_streaming_panel.cpp` | `180`. | Read as an integer; a malformed value or a value below 2 gives 180. | Number of batched render ticks for `QAR_PANEL_AUTOMULTI`. Set by `scripts/run_rviz_with_plugin.ps1` (from -MultiTicks). |
| `TEMP` | `scripts/fix_stale_pixi_paths.ps1`, `scripts/play_demo_rosbag.ps1`, `scripts/qar_locations.ps1`, `scripts/run_live_rviz_stream_real.ps1`, and 1 more file, qar_ogre_adapter (`qar_qar_adapter`) | None. | The adapter reads `TEMP`, then `TMP`, and uses `<folder>\quaternar` as the QarOS log folder; with neither set it uses the relative folder `quaternar`. Scripts use `$env:TEMP` for scratch files. | Default QarOS log folder and trace folder when the host passes no log folder; scratch folder of several scripts. |
| `TMP` | qar_ogre_adapter (`qar_qar_adapter`) | None. | Read only when `TEMP` is unset. | Fallback of `TEMP` for the default QarOS log folder. |
| `AMENT_PREFIX_PATH` | `cmake/QarBuildSupport.cmake`, `qar_ros_ui/src/type_support_registry.cpp`, `scripts/env_setup.ps1` | None. | A list separated by `;` (`:` on other systems). A program that loads ROS message type support reads it through `_dupenv_s` (MSVC) or `getenv` and searches it after the prefixes that the host adds explicitly. | The ament index of the ROS 2 distribution, through which the type-support libraries of ROS message packages (`<pkg>__rosidl_typesupport_*`) are found. The shared CMake support module prepends the distribution root to it for the configure process, and `scripts/env_setup.ps1` prints its value after it has imported the environment of the distribution. |

**When the plugin reads them.** The plugin reads a variable at the moment its feature starts, so a value that you change later does not reach a feature that has already started, and you start RViz again. The panel reads `QAR_PANEL_TICK_MS`, `QAR_ADAPTER_PRIME`, `QAR_PANEL_WEB_SIZE` and `QAR_PANEL_WEB_SCALE` when it builds its widgets, because they set the start values of the controls, and it reads the pose variables for the spatial picker when it is constructed. It reads `QAR_PANEL_LIVE_STREAM`, `QAR_RUNTIME_BINARIES_DIR`, the scene lighting, floor plane, geolocation and heartbeat variables and the variables of the scripted modes when RViz initialises it. It reads the variables of the session, the clip planes, the depth scale, the logging, the invitations, the admission and the app volume when you press `"Connect to QarOS"` for the first time, because that is when the session is built. `QAR_PANEL_CLOSE_MODE` is read when the first panel is streamed, `QAR_PANEL_HIDDEN_DISPLAYS` at every connect and `QAR_PANEL_HIDDEN_DISPLAYS_SIZE` when the hidden panel is created, and `QAR_PANEL_WEB_HOST` and `QAR_PANEL_WEB_PORT` when the first AR tool starts the web server. The button `"Capture offscreen frame"` reads `QAR_PANEL_AUTOCAPTURE` each time that you press it.

**Values that surprise.** The parsing rules are in the table, and a few of them are traps:

- `QAR_PANEL_LIVE_STREAM` is on for any value that is not empty, `0` and `off` included.
- `QAR_PANEL_FLOOR_PLANE`, `QAR_PANEL_GEO` and `QAR_PANEL_APP_VOLUME` are switched off only by the word `off`, in any letter case; `0` and `false` leave them on. `QAR_PANEL_HIDDEN_DISPLAYS` reads a list of words instead: `0`, `false`, `off` and `no` switch it off, and any other value, an empty one included, leaves it on.
- `QAR_POSE_WORLD` and `QAR_DEPTH_ENCODING` are case sensitive: only the exact words `zup` and `ndc` change anything.
- The adapter variables `QAR_ADAPTER_TRACE`, `QAR_ADAPTER_TRACE_ALL`, `QAR_ADAPTER_FRAME_TRACE` and `QAR_ADAPTER_FLIP_Y` are presence flags, so that a variable that is set to `0` is on.
- A value of `QAR_SCENE_LIGHT` that is not a mode keeps the default without an error, and `QAR_PANEL_WEB_PORT` turns a malformed number into 0, which means a port that the system picks. The other numeric variables of the panel use the default when the value cannot be read, and the ones that have an error text, such as `QAR_PANEL_DEPTH_SCALE`, `QAR_PANEL_GL_SUBMIT` and `QAR_PANEL_PEER_ADMISSION`, write it to `"Last error:"` and to the log.

**Remarks on single variables.** The text in the table says what a variable does for the library that reads it, and the plugin adds the following.

`QAR_ADAPTER_PRIME` is the start state of the check box `"Prime next frame"`: the panel reads it with the default `true` when it builds the box, and the box drives the manager, once when the manager is created and again at every toggle. In the plugin the variable therefore decides only where the box starts, and with the variable unset the box starts checked.

The library severity has three inputs in the plugin, in this order: `QAR_PANEL_LOG_SEVERITY`, then `QAR_LOG_SEVERITY`, then the default `trace`. A value that is present and is not a severity is skipped and reported in the log as a line that begins `LIVE: WARNING:`. The decision goes into `Config::log_severity`, and the factory exports it back as `QAR_LOG_SEVERITY`, because the QarOS library obeys that variable over the severity in its init structure. The panel writes the line `LIVE: QarOS library logging:` with the severity, where it came from, the state of the console copy and a note when the environment disagreed and was overwritten. `QAR_LOG_SEVERITY` takes the same words as `QAR_PANEL_LOG_SEVERITY`: `trace`, `debug`, `info`, `warn`, `warning`, `error` and `fatal`. The launcher sets `QAR_LOG_SEVERITY` to `warn` in quiet mode and to `trace` with `-TraceLogs`. [Reading the logs](TROUBLESHOOTING.md#reading-the-logs) explains why the variable wins.

`QAR_ZENOH_LOG` starts the logger of the zenoh transport inside `rviz2.exe`. RViz never starts that logger on its own, so the request of the plugin is the only way to get it, and the adapter honours it only while the library severity is `trace` or `debug`. The launcher sets the variable to `debug` with `-TraceLogs` and removes it otherwise.

`QAR_RUNTIME_BINARIES_DIR` is the one variable that connects the plugin to an SDK folder. The panel passes it as `runtime_binaries_folder_path`, and the adapter loads `qar-streaming-c.dll` from that folder by its full path. The loader searches the folder of the library first for its dependencies and never searches `PATH`, so that the variable is what matters, and a copy of the SDK folder on `PATH` is not. The launcher prepends the SDK `bin` folder to `PATH` as well, which the loader does not use.

`QAR_PANEL_WEB_SCALE` seeds the spin box with the prefix `size x` next to `"Re-open in AR"`, and the box is the live control: it runs from 0.25 to 4.00, it starts at the value of the variable or at 1.00, and it applies with the next `"Re-open in AR"` and with the next report of the viewport of a page. `QAR_PANEL_WEB_SIZE` pins the size in metres. The panel reads both once, when it builds its widgets, and writes a note about a value that it ignored when the web server starts, because there is no log view yet at the time of the read.

`QAR_PANEL_HIDDEN_DISPLAYS_SIZE` is the size of the hidden copy of the Displays panel, `WIDTHxHEIGHT` in logical pixels, 480 by 720 by default; each side is limited to the range from 200 to 2000 with a warning in the log, and a text that is not a size keeps the default with a warning. `QAR_PANEL_VISUALIZER_GUID` is the payload that the adapter appends to the connection string of a Visualizer: `auto`, the default, makes a new identifier for every invitation, which is what lets several Visualizers run at once, `off` restores the bare string, and any other value is used as it is. An unset variable and an empty one both mean `auto`.

`QAR_PANEL_LAUNCHER_CONFIG` replaces the configuration of the runtime launcher, which is `{"enableMixer": true}` while the variable is unset. The value `{"enableMixer": false}` selects the direct path from RViz to the Visualizer, and it is useful for debugging only.

The variables that set the depth, the pose and the lighting have a library default and a value that the launcher sets, and the next section compares them.

`QAR_PANEL_WEB_SIZE` and `QAR_PANEL_WEB_SCALE` are read by `apply_sizing_overrides` of `qar_web_panels`, each time a host calls it, through the lookup function that the host passes, and by nothing else: a host that never calls it never reads them. `QAR_PANEL_WEB_SIZE` is two positive numbers in metres that are separated by `x` or `X`, such as `0.9x1.2`, and it pins every panel to that size and skips the measurement. `QAR_PANEL_WEB_SCALE` is a positive number, such as `1.25`, that multiplies the derived size before the bounds apply. Each number has to be a whole token of at most 32 characters; spaces and tabs around it are ignored, and `0.9m` or `0.9 0.8` is not a number. A value that does not parse is ignored and is explained in the note that the function returns, whose text ends in `is not <width>x<height> in metres - ignored` or `is not a positive number - ignored`. An empty value is ignored without a note. A pinned size moves the bounds of the panel out of its way, so it is never clamped. [Panel sizing](ARCHITECTURE.md#panel-sizing) explains what the two knobs do to the size.

`AMENT_PREFIX_PATH` is read by the registry of type support in `qar_ros_ui`, as a list that is separated by `;`, and its folders are searched after the prefixes that the host added with `add_search_prefix` and before the search of the operating system, which is `PATH` on Windows. The build also sets it, and `PYTHONPATH`, for the configure process only, from `ROS2_INSTALL_ROOT`, so that the CMake files of the ROS 2 distribution run from a plain shell.

### Variables of the build, the scripts and the host

These variables are read by the CMake files, by the scripts or by Windows, and not by the plugin while it runs:

| Variable | Read by | Default | Values and parsing | Effect |
|---|---|---|---|---|
| `ROS2_INSTALL_ROOT` | `cmake/QarBuildSupport.cmake`, `scripts/build_plugin.ps1`, `scripts/env_setup.ps1`, `scripts/fix_missing_eigen.ps1`, and 12 more files, qar_ogre_adapter (its preset and package configuration) | None: a script or the configure step that needs it stops with an error that names the parameter or cache variable and this variable. | A folder. The shared CMake support module uses it when the cache variable `QAR_ROS2_INSTALL_ROOT` is unset or empty; the folder must exist and contain `local_setup.bat`. Scripts use it when the `-Ros2Root` parameter is omitted. The Ogre sample of qar_ogre_adapter takes a non-empty value as the Ogre plugin folder `<root>/opt/rviz_ogre_vendor/bin`. | Root of the ROS 2 Humble Windows distribution: Ogre through `rviz_ogre_vendor`, the ROS command-line tools and the Ogre DLL folder. |
| `QAR_QT_ROOT` | `cmake/QarBuildSupport.cmake`, `scripts/build_plugin.ps1`, `scripts/env_setup.ps1`, `scripts/run_live_rviz_stream_real.ps1`, and 2 more files, qar_qt_streaming (its preset and package configuration) | None: a script or the configure step that needs it stops with an error that names the parameter or cache variable and this variable. | A folder. The shared CMake support module uses it when the cache variable `QAR_QT_ROOT` is unset or empty; the folder must contain `lib/cmake/Qt5/Qt5Config.cmake`. Scripts use it when the `-QtRoot` parameter is omitted. | Root of the Qt 5.15.2 `msvc2019_64` installation. |
| `QAR_DEPS_ROOT` | `scripts/fetch_dependencies.ps1`, `scripts/fix_missing_eigen.ps1`, `scripts/fix_missing_openssl_dll.ps1`, `scripts/fix_missing_spdlog_dll.ps1`, and 2 more files | None: a script that needs it stops with an error that names the parameter and this variable. | A folder. Scripts use it when the `-DepsRoot` parameter is omitted. | Dependency root of the setup kit: the downloads folder and the build trees of the distribution repairs live below it. |
| `QAR_PREFIX` | `cmake/QarBuildSupport.cmake`, `scripts/build_plugin.ps1`, `scripts/run_live_rviz_stream_real.ps1`, `scripts/run_rviz_with_plugin.ps1`, and 1 more file, qar_qt_streaming (its preset) | None: the configure step continues without it, and a script that needs it stops with an error that names the parameter and this variable. | One folder, or several separated by semicolons (CMake reads the value as a list); every folder must exist. CMake prepends them to `CMAKE_PREFIX_PATH`. Scripts use it when the `-Prefix` parameter is omitted. | Install prefix of the repositories: the libraries are installed into it, and a repository that depends on them finds them through it. |
| `QAR_SDK_ROOT` | `scripts/fix_old_vc_runtime.ps1`, `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter (`qar_qar_adapter`, its preset) | None. | A folder. CMake (the adapter module) uses it when the cache variable `QAR_SDK_ROOT` is unset or empty; an unset or wrong value only gives a configure warning, because nothing links against the SDK. Scripts use it when the `-SdkRoot` parameter is omitted. | Root of the QarOS SDK install tree (`bin`, `include`). It is used by the configure-time check and by the scripts that need the SDK; at run time the DLL is found through the runtime binaries folder that the host passes (`runtime_binaries_folder_path`). |
| `QAR_VENV` | `scripts/build_plugin.ps1`, `scripts/env_setup.ps1`, `scripts/fix_missing_openssl_dll.ps1`, `scripts/fix_stale_pixi_paths.ps1` | None: a script that needs it stops with an error that names the parameter and this variable; `fix_missing_openssl_dll.ps1` tries `tar` instead; `fix_stale_pixi_paths.ps1` prints a warning and goes on without the numpy headers. | A folder. Scripts use it when the `-Venv` parameter is omitted; `fix_missing_openssl_dll.ps1` takes `Scripts\python.exe` below it as the Python that unpacks the OpenSSL package when `-Python` is omitted; `fix_stale_pixi_paths.ps1` takes the numpy headers from the numpy package below it when `-NumpySourceInclude` is omitted. | Python 3.10 virtual environment (the folder with `Scripts\python.exe`). |
| `PYTHONPATH` | `cmake/QarBuildSupport.cmake` | None. | The shared CMake support module puts `<ROS2_INSTALL_ROOT>/Lib/site-packages` in front of the existing value for the configure process when that folder exists. | Lets the ament CMake modules import `ament_package` from the Python of the ROS 2 distribution while a project is configured. |
| `QT_QPA_PLATFORM_PLUGIN_PATH` | `scripts/env_setup.ps1` | None. | Read by Qt, not by repository code; set by `scripts/env_setup.ps1`. | Directory of the Qt platform plugins, the folder `platforms`, for the programs that the shell starts. Set by `scripts/env_setup.ps1`. |
| `QT_PLUGIN_PATH` | - | None. | Read by Qt, not by repository code; set by `scripts/env_setup.ps1`. | Directory of the Qt plugins. Set by `scripts/env_setup.ps1`. |
| `FASTRTPS_DEFAULT_PROFILES_FILE` | `scripts/publish_video_file.py`, `scripts/run_live_rviz_stream_real.ps1` | `scripts/fastdds_large_data.xml` (a 64 MB shared-memory segment). | `publish_video_file.py`: an existing value wins, otherwise the `--dds-profile` file is used (`none` skips). The live launcher: an existing value wins, otherwise `scripts/fastdds_large_data.xml` is used unless `-NoDdsProfile` is given. | Fast-DDS transport profile for 6 MB raw video frames: the default 512 KB shared-memory segment drops fragments. Set by `scripts/publish_video_file.py`, `scripts/run_live_rviz_stream_real.ps1` (unless -NoDdsProfile or already set). |
| `RMW_FASTRTPS_PUBLICATION_MODE` | `scripts/publish_video_file.py` | `ASYNCHRONOUS` (the default of the `--publication-mode` option of `publish_video_file.py`). | `publish_video_file.py`: an existing value wins; `keep` leaves the RMW default (`SYNCHRONOUS`); otherwise `ASYNCHRONOUS`, `SYNCHRONOUS` or `AUTO` in any case; an unknown value gives a warning and the default stays. | Fast-RTPS publication mode of the video publisher. Set by `scripts/publish_video_file.py`. |
| `RUST_LOG` | `scripts/run_live_rviz_stream_real.ps1` | Unset in quiet mode; `zenoh=debug` with `-TraceLogs`. | Read by zenoh (Rust) inside the QarOS processes, not by repository code. The launcher treats a value that names `trace` or `debug` as loud. | zenoh log filter for the Visualizer and mixer processes. Set by `scripts/run_live_rviz_stream_real.ps1` (-TraceLogs: zenoh=debug; otherwise removed). |
| `PATH` | `scripts/env_setup.ps1`, `scripts/qar_locations.ps1`, `scripts/run_live_rviz_stream_real.ps1`, qar_ogre_adapter | None. | Read by the Windows loader. | The folders in which Windows looks for the DLLs of a program that it starts: the Qt, Ogre and ROS DLL folders must be on it for a program that imports them. The scripts prepend them. Set by `scripts/env_setup.ps1`, `scripts/qar_locations.ps1`, `scripts/run_live_rviz_stream_real.ps1`. |

The scripts of the setup read `QAR_DEPS_ROOT` and `QAR_VENV` when their parameters `-DepsRoot` and `-Venv` are omitted, and `scripts\fix_missing_openssl_dll.ps1` reads `QAR_VENV` for the Python of its parameter `-Python`. `scripts\env_setup.ps1` sets `QT_QPA_PLATFORM_PLUGIN_PATH` and `QT_PLUGIN_PATH` to the plugin folders of Qt, because RViz is linked against renamed Qt libraries and cannot find that folder the usual way, and it puts the virtual environment, Qt and the distribution on `PATH`. `scripts\fix_stale_pixi_paths.ps1` reads `QAR_VENV` for the numpy headers of the virtual environment, which are an optional source, `scripts\fix_old_vc_runtime.ps1` reads `SYSTEMROOT` to find the system folder that holds the newer C++ runtime, and `TEMP` is the folder of their scratch files and the parent of the log folders of the plugin.

## What the launcher sets

The launcher sets a few variables that the plugin needs and some that change what RViz does. The table compares each value with what the code does when the variable is not set; the code defaults are the ones in the table of the variables of the plugin process. For the first group the launcher always overwrites your value, for the second it keeps a value that you set in your shell, and the third group is not set at all.

| Variable | The launcher | Without the launcher |
|---|---|---|
| `QAR_PANEL_LIVE_STREAM` | `1`, always | unset: the offline preview with in-process test peers, and no session |
| `QAR_RUNTIME_BINARIES_DIR` | `<SdkRoot>\bin`, always | unset: the runtime falls back to the current folder with a warning |
| `QAR_POSE_WORLD` | `zup`, always | unset: the identity basis, so that a viewer arrives on its side in a Z-up world |
| `QAR_POSE_YAW_DEG` | `180` unless set | `180` |
| `QAR_POSE_HEIGHT_M`, `QAR_POSE_BACK_M` | `0` unless set | `0` |
| `QAR_DEPTH_ENCODING` | `ndc` unless set | `metric`: the Visualizer shows metric depth wrongly |
| `QAR_SCENE_LIGHT` | `both` unless set | `both` in live mode and `off` in the offline preview |
| `QAR_PANEL_VISUALIZERS` | the value of `-Visualizers`, `0`, unless set | `0` |
| `QAR_PANEL_INVITE_PEERS` | the entries of `-InvitePeers` unless set | empty |
| `QAR_PANEL_PEER_ADMISSION` | `new` with `-RealPeers`, else `invited`, unless set | `invited` |
| `QAR_LOG_SEVERITY` | `warn`, or `trace` with `-TraceLogs`, unless a quieter value is set | unset: the library severity is the one of `QAR_PANEL_LOG_SEVERITY`, else `trace` |
| `QAR_PANEL_LOG_SEVERITY` | `warn`; removed with `-TraceLogs` | unset: the severity of `QAR_LOG_SEVERITY`, else `trace` |
| `QAR_PANEL_LOG_CONSOLE` | `off`; removed with `-TraceLogs` | on for `trace`, `debug` and `info` and off for `warn` and above |
| `RUST_LOG`, `QAR_ZENOH_LOG`, `QAR_ADAPTER_TRACE`, `QAR_ADAPTER_TRACE_ALL`, `QAR_ADAPTER_FRAME_TRACE` | removed in quiet mode; see the table of the logging variables | unset: no zenoh log and no trace |
| `QAR_VIDEO_TIMING_DIR` | `<LogDir>\timing\<stamp>` with `-TimingCsv` | unset: no timing files |
| `FASTRTPS_DEFAULT_PROFILES_FILE` | `scripts\fastdds_large_data.xml` unless set or `-NoDdsProfile` | unset: the Fast-DDS defaults |
| `PATH` | the SDK `bin` folder in front | not changed |

Two defaults of the libraries differ from what a ROS host needs. The library default of `QAR_POSE_WORLD` is the identity, which the unit tests of the pose conversion assert, and a host with a Z-up ROS world needs `zup`; the library default of `QAR_DEPTH_ENCODING` is metric depth, and with an SDK drop earlier than that of 2026-09-28 the Visualizer expected the window-space depth value in the range 0 to 1 and inverted it itself. QarOS ignored `depth_scale` and `rendered_near_far` for that, so that no scaling on the side of the producer could compensate for metric depth. The launcher sets both, and a host that starts RViz without the launcher has to set them itself.

Everything else keeps the default of the code. The clip planes are 0.01 m and 150 m: the window-space depth is reciprocal in the distance, so that a far plane of 500 m squeezes a room-sized scene into the first percent of the range, and a far plane that is shorter than the range of a LiDAR clips its points away. The app volume is a cube of 40 m at the height of 1 m with the scale 1.0. The Visualizer connection uses a new identifier for each invitation. The floor plane, the geolocation and the hidden Displays panel are on.

## Running RViz without the launcher

You can start RViz yourself when you need an environment that the launcher does not allow, for example another scene or a debugger. The plugin needs three things from the environment: the distribution and Qt, which `scripts\env_setup.ps1` prepares, the overlay of the plugin, which `local_setup.ps1` in the prefix sets, and the variables of the plugin. `QAR_PANEL_LIVE_STREAM` makes the panel live-capable, `QAR_RUNTIME_BINARIES_DIR` names the SDK folder, and `QAR_POSE_WORLD` and `QAR_DEPTH_ENCODING` give a Z-up world and the window-space depth. The two logging variables keep the run quiet, because the library logs at `trace` when nothing says otherwise.

The commands set the environment in the current window and start RViz with the scene that the launcher uses. They need a window of the desktop, and RViz does not return until you close it. They run in the folder of the clone, so go there first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

```powershell
. $env:QAR_PREFIX\local_setup.ps1
$env:QAR_PANEL_LIVE_STREAM = '1'
$env:QAR_RUNTIME_BINARIES_DIR = Join-Path $env:QAR_SDK_ROOT 'bin'
$env:QAR_POSE_WORLD = 'zup'
$env:QAR_DEPTH_ENCODING = 'ndc'
$env:QAR_LOG_SEVERITY = 'warn'
$env:QAR_PANEL_LOG_SEVERITY = 'warn'
$env:QAR_PANEL_LOG_CONSOLE = 'off'
$env:FASTRTPS_DEFAULT_PROFILES_FILE = (Resolve-Path '.\scripts\fastdds_large_data.xml').Path
rviz2.exe -d (Join-Path $env:QAR_PREFIX 'share\qar_rviz_streaming\config\test_scenes\scene3_grid_marker.rviz')
```

The scene shows a grid and a display of markers, and no publisher feeds the markers. If you want content, prepare another window with the two lines below: the first goes to the folder of the clone, and the second, with the leading dot and the space, imports the environment of the ROS 2 distribution.

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

In that window run `python scripts\publish_test_markers.py`. What the launcher does with `-Confirm` beyond this, the graphics preference, the clean-up of earlier test publishers and of the shared memory of Fast-DDS, and the rotation of large QarOS logs, you have to do yourself or leave out.

## DDS profile

Large ROS messages need a profile for Fast-DDS, the middleware of the distribution. The file `scripts\fastdds_large_data.xml` defines the default participant profile `qar_large_data`: it switches the built-in transports off and registers two of its own, a shared-memory transport with a segment of 64 MB, `qar_shm_large`, and UDPv4 at its defaults, `qar_udp`. Because UDPv4 stays on, discovery, remote peers and readers that cannot use shared memory behave as with the stock profile.

The reason is the segment. The shared-memory transport sends through one segment for each sending participant, and the segment is 512 KB by default in Fast-DDS 2.6, while a raw frame of 1080p, 6.2 MB, is about 96 fragments of 64 KB. With the default segment between 5 and 8 percent of the frames lose fragments: a best-effort reader misses them, and the reliable protocol brings them back 10 to 25 ms late, at every reader of the topic, which is why the Image display of RViz and the AR panel stutter together once both are subscribed. A segment of 64 MB holds about ten whole frames and loses none. The numbers come from an offline measurement with a publisher and two readers, which the profile file records.

Fast-DDS creates every participant of a process from the default profile, so one file settles the matter for the whole process: RViz's own Image display and the video reader of the plugin are two participants of `rviz2.exe`. The launcher exports the file as `FASTRTPS_DEFAULT_PROFILES_FILE` for everything that it starts, unless the variable is already set in your shell, in which case yours wins, or you pass `-NoDdsProfile`, and it says in its output which of the three cases it found. `scripts\publish_video_file.py`, the video publisher, applies the same file to its own process before it initialises ROS, and its options `--dds-profile` and `--publication-mode` change that; a value that is already in the environment wins there too. The sending side is where the profile matters most, so the publisher and RViz have to agree.

The segment is a file in `C:\ProgramData\eprosima\fastrtps_interprocess` with the size of the segment: 67,133,984 bytes for a participant that uses the profile and 549,408 bytes for one that does not. The file is removed when the process ends cleanly, and a process that dies leaves it behind until `fastdds shm clean` removes it; the launcher runs that command at every start with `-Confirm`, and [Run-time problems](TROUBLESHOOTING.md#large-ros-messages-stall-after-a-crash) describes the symptom.

## Runtime option structs

These are the structs that a host fills in code. Every default below is the value that the struct has when you change nothing. [API](API.md) shows how a host fills them.

### The server

`WebPanelServerConfig` in `web_panel_server.hpp`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `bind_address` | `QHostAddress` | `QHostAddress::Any` | The interfaces to listen on. Every interface, IPv4 and IPv6, is what a headset needs; the loopback address makes the server reachable from this machine only. |
| `port` | `quint16` | `0` | The TCP port. 0 lets the operating system pick a free one, so that two hosts on one machine never collide; pin it for a firewall rule. |
| `advertised_host` | `std::string` | empty | The host that panel addresses name. Empty means the bind address when it is a concrete one, otherwise the address that the machine routes through. |
| `allow_any_origin` | `bool` | `false` | Admit a WebSocket handshake whose `Origin` is not this server or is absent. |
| `http` | `HttpServerOptions` | see below | The limits of the HTTP layer. |
| `session` | `PanelSessionLimits` | see below | The limits of each panel. |
| `render` | `HtmlRenderOptions` | see below | The options of every page; `websocket_url` is overwritten for each request. |

`HttpServerOptions` and `HttpParseLimits` in `http_server.hpp` and `http_message.hpp`, and `PanelSessionLimits` in `panel_session.hpp`:

| Field | Default | Meaning |
|---|---|---|
| `HttpServerOptions::max_connections` | `64` | Connections that the server serves at a time; a further one is answered with 503. |
| `HttpServerOptions::request_timeout_ms` | `15000` | A request head must arrive within this, and an idle connection is dropped after it; partial bytes do not reset it. |
| `HttpParseLimits::max_head_bytes` | `16384` | The request line and the headers; a larger head is answered with 431. |
| `HttpParseLimits::max_target_bytes` | `2048` | The request target; a longer one is answered with 414. |
| `HttpParseLimits::max_headers` | `100` | The header fields; more is answered with 431. |
| `PanelSessionLimits::max_clients` | `16` | The viewers of one panel; a further one is refused with 1008, or with 503 before the upgrade. |
| `PanelSessionLimits::max_client_backlog_bytes` | `4194304` | A viewer whose unsent data exceeds this is dropped with 1008. |
| `PanelSessionLimits::max_message_bytes` | `1048576` | An inbound frame or message larger than this closes the socket with 1009. |

### The page

`HtmlRenderOptions` in `html_renderer.hpp`.

| Field | Type | Default | Meaning |
|---|---|---|---|
| `theme` | `ArTheme` | `ArTheme::dark()` | The sizes and the colours of the page. |
| `websocket_url` | `std::string` | empty | The address that the script dials. Empty makes the page derive it from its own location. |
| `page_title` | `std::string` | empty | The title of the page. Empty uses the title of the panel. |
| `language` | `std::string` | `en` | The `lang` of the document. |
| `reconnect_ms` | `int` | `2000` | The delay before the script dials a closed socket again. |
| `ping_interval_ms` | `int` | `5000` | The script sends a ping after this much silence in both directions. |
| `stale_after_ms` | `int` | `5000` | The script shows Stale after this long without a frame. |
| `include_runtime` | `bool` | `true` | `false` writes a static page without the script, the pager and the boot diagnostic. |
| `include_csp` | `bool` | `true` | Write the `Content-Security-Policy` meta tag. |

`ArTheme` in `ar_theme.hpp`. Every size is in CSS pixels, and `scaled(k)` multiplies all of them, rounding and never going below 1 px. The defaults are the numbers of [Page generation](ARCHITECTURE.md#page-generation): the sizes do not change between `dark()` and `light()`, which differ in colour.

| Field | Default | Meaning |
|---|---|---|
| `base_font_px`, `small_font_px`, `value_font_px`, `heading_font_px`, `title_font_px` | `22`, `18`, `30`, `26`, `32` | Body text, the smallest text on the page, live readouts, section titles and the title of the panel. |
| `touch_target_px`, `touch_gap_px` | `64`, `12` | The least size of anything pressable, and the least gap between two pressable things. |
| `gap_px`, `padding_px`, `radius_px`, `focus_ring_px` | `16`, `24`, `14`, `4` | The gap between widgets, the padding of sections and of the panel, the corner radius and the focus ring. |
| `table_row_px`, `table_page_rows`, `chart_height_px` | `56`, `8`, `220` | The height of a table row, the rows of a table page, and the height of a chart. |
| `row_basis_px` | `260` | The width that a child of a row asks for before the row wraps. |
| `gap_tight_px`, `gap_micro_px`, `pad_tight_px`, `pad_micro_px` | `8`, `6`, `16`, `10` | The secondary spacings: a caption to its control, inside a component, inside fields and pills, and the vertical padding of a component. |
| `border_px`, `chip_px`, `dot_px`, `dot_small_px`, `knob_px`, `block_px` | `2`, `44`, `28`, `16`, `48`, `120` | An outline, a pill that is not pressable, a status dot, a connection dot, the thumb of a slider, and the side of a small block. |
| `background`, `surface`, `surface_raised`, `border` | `#0F141A`, `#1A222C`, `#26313D`, `#3A4756` | The colours of the page and of its boxes. |
| `text`, `text_muted`, `accent`, `accent_text` | `#F2F5F7`, `#A9B4BF`, `#4CC9F0`, `#06202B` | Text, secondary text, the accent, and text on the accent. |
| `ok`, `warning`, `error`, `stale`, `focus` | `#3DDC84`, `#FFC857`, `#FF5C5C`, `#8A97A5`, `#FFD166` | The colours of the states and of the focus ring. |
| `font_family` | the list `system-ui, -apple-system, 'Segoe UI', Roboto, 'Noto Sans', sans-serif` | System fonts only: the page may not fetch anything. |

### Sizing a URL panel

`UrlPanelSizing` in `url_panel_sizing.hpp`.

| Field | Default | Meaning |
|---|---|---|
| `meters_per_css_px` | `0.0` | Metres of one CSS pixel; 0 or less means derive it from the theme, which is what every caller should do. |
| `scale` | `1.0` | A uniform multiplier on the derived size, applied before the bounds (`QAR_PANEL_WEB_SCALE`). |
| `fixed_width_m`, `fixed_height_m` | `0.0`, `0.0` | A pinned size in metres (`QAR_PANEL_WEB_SIZE`); both above 0 skips the measurement. |
| `min_width_m`, `max_width_m` | `0.25`, `1.40` | The bounds of the width. |
| `min_height_m`, `max_height_m` | `0.25`, `1.30` | The bounds of the height. |
| `content_scale` | `1.0` | The zoom of the raster that QarOS applies; 1.0 fits the content across the whole surface. |
| `theme_scale` | `1.0` | The scale at which the page is drawn, relative to the theme that you pass; `size_url_panel_for` sets it. |
| `viewport_css_width`, `viewport_css_height`, `content_css_height` | `0.0` | The viewport that the page reported and the height of its content; 0 means that no page has reported yet. |

The constants of the arithmetic have these values:

| Name | Value | Where it acts |
|---|---|---|
| `kQtMetersPerLogicalPx` | `0.0012` | Metres of one logical pixel of a Qt widget panel, which the sizing of a page matches. |
| `kQtBodyFontPx` | `12.0` | The body font of a Qt widget panel, in logical pixels. |
| `kMaxRowColumns` | `3` | Columns that a row is assumed to keep on one line. |
| `kComfortableLineChars`, `kAverageCharAdvanceEm` | `60.0`, `0.5` | The text measure: characters of a line and the average advance in em. |
| `kPageLineHeight` | `1.4` | The line height of the body of the page. |
| `kJsonInspectorBlocks` | `2.0` | The nominal height of a JSON inspector, in blocks of the theme. |
| `kMaxThemeScale`, `kMinThemeScale` | `1.0`, `0.25` | The theme is never scaled up, and the floor of its scale when the caller names none. |
| `kThemeScaleDeadband` | `0.02` | A change of the scale below this is not worth restyling the page. |
| `ViewportFitOptions::min_scale`, `max_columns` | `0.25`, `3` | The options of `fit_page_to_viewport`. |
| `kOverflowTolerancePx` | `4.0` | Content must exceed the viewport by more than this before the pager appears. |
| `kPagerExitMarginPx` | `32.0` | The margin that content must clear before the pager turns off again. |
| `kPageOverlapFraction`, `kMinPageStepFraction` | `0.12`, `0.5` | How much of the previous page stays visible, and the least step of a page, as fractions of the viewport. |

### JSON and values

| Struct | Field | Default | Meaning |
|---|---|---|---|
| `JsonWriteOptions` | `indent`, `html_safe` | `0`, `false` | 0 writes one compact line; the HTML-safe mode escapes the characters that could end a script element. |
| `JsonParseOptions` | `max_depth` | `256` | The deepest nesting that the reader accepts. |
| `ValidateValueOptions` | `require_all_fields`, `reject_unknown_fields` | `false`, `true` | Whether a missing field is a problem, and whether an unknown key is. |

### The ROS 2 side

| Struct | Field | Default | Meaning |
|---|---|---|---|
| `ConversionLimits` | `max_array_elements`, `max_depth` | `4096`, `64` | The caps of a conversion from a message to a value; 0 means no limit. |
| `ConversionOptions` | `reject_unknown_fields`, `require_all_fields` | `true`, `false` | The rules of a conversion from a value to a message. |
| `IntrospectionOptions` | `record_defaults` | `true` | Construct the type once to read the IDL defaults. |
| `GraphDiscoveryOptions` | `include_hidden` | `false` | Keep the names with a token that starts with an underscore. |
| `TopicSubscriptionOptions` | `qos`, `depth`, `limits` | `QosPreset::Adaptive`, `10`, the defaults of `ConversionLimits` | The QoS preset, the depth of the queue and the caps of the conversion. |
| `TopicPublisherOptions` | `qos`, `depth`, `conversion` | `QosPreset::Adaptive`, `10`, the defaults of `ConversionOptions` | The same for a publisher. |
| `ServiceCallOptions` | `timeout`, `wait_for_server`, `conversion`, `limits` | `5000` ms, `true`, the defaults | The deadline of a call, whether a call waits for a server that is not up yet, and the conversion rules. |
| `ActionGoalOptions` | `response_timeout`, `wait_for_server`, `conversion`, `limits`, `on_event` | `5000` ms, `true`, the defaults, none | The deadline for the answer of the server, the same wait, the conversion rules and the event callback. The execution of a goal has no deadline. |
