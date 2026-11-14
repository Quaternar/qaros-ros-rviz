# Changelog

What changes for users of qar_rviz_plugin: the RViz2 plugin `qar_rviz_streaming` and its settings,
the launchers and scripts, the release assets, the libraries `qar_ui_model`, `qar_web_panels`,
`qar_ros_ui` and `qar_capi_panels`, and the examples. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) with a **Breaking** section first, and
versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html): a minor version adds
features (anything that breaks a user is listed under Breaking with its migration), a patch version
only fixes. The version is the file [VERSION](VERSION) and the release tag `vX.Y.Z`; how entries are
written and how a version is released is in [AGENTS.md](AGENTS.md).

## [Unreleased]

## [0.2.1] - 2026-11-14

### Breaking

- The panel joins only the QAROS Hub of this PC, as a local app: to reach the Hub of another PC, make the Hub of this PC a member of it in QAROS ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The group `"QAROS Hub"` loses the Hub choice, the fields `"Host:"`, `"Port:"`, `"Code:"` and the buttons `Pair` and `"Forget pairing"`; `Cancel`, the identity and state lines and the targets stay ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- Settings are written as version 4: a saved `"Hub Mode"` `remote` loads as `local` with a log line and its `"Hub Host"` and `"Hub Port"` are dropped; `"Hub Mode"` accepts `auto` and `local`, which both join the Hub of this PC ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- `QAR_PANEL_HUB_HOST` and `QAR_PANEL_HUB_PORT` only log that they have no effect, and `QAR_PANEL_IDENTITY_DIR` is gone ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The launcher parameters `-HubHost` and `-HubPort`, the value `remote` of `-HubMode`, and the example options `--hub`, `--code` and `--forget` are removed ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The QarOS C and C++ headers come from the public qaros repository, the git submodule `external/qaros`: clone with `--recurse-submodules`; configuring stops when an installed upstream package carries headers of another qaros commit (`QAR_QAROS_HEADER_CHECK`) ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The plugin builds against qar_ogre_adapter 0.2.1 and qar_qt_streaming 0.2.1: install those versions of qaros-ogre and qaros-qt ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- Release tags are the plugin's own version `vX.Y.Z` (was the QAROS version), assets are named `qar_rviz_streaming-<plugin version>-<distro>-<platform>.zip`, and `manifest.json` names each zip's minimum QAROS version in `min_qaros_version` ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).

### Features

- `scripts/register-qaros-plugin.ps1` and `scripts/register-qaros-plugin.sh` register a hand-built install prefix for QAROS in the per-user `ros-plugins.json`, and remove it with `-Unregister` / `--unregister` ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The release workflow builds a prebuilt plugin zip and `manifest.json` (SHA-256, C API header hash, minimum QAROS version, plugin version) for every `vX.Y.Z` tag; Humble on Windows x64 only for now, the Linux builds do not compile yet ([#6](https://github.com/Quaternar/qaros-ros-rviz/issues/6)).
- `scripts/package-release.ps1` and `scripts/package-release.sh` package the plugin of an install prefix as a release zip with its manifest entry ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- `scripts/build_plugin.sh` builds the plugin on Linux with colcon; unverified, it does not compile yet with gcc or against Jazzy ([#6](https://github.com/Quaternar/qaros-ros-rviz/issues/6)).
- The plugin installs `share/qar_rviz_streaming/qar_streaming_h.sha256` (the hash of the QarOS C header it was compiled against), `plugin_version` and `qaros_version` ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The version comes from the file `VERSION` and git tags (`X.Y.Z`, `X.Y.Z-rc.N`, `X.Y.Z-dev.N+<sha>`): the panel logs it and tells it to the QAROS Hub as its app version, and the examples print it with `--version` ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).

### Enhancements

- `scripts/build_plugin.ps1` prints the register command at its end and has the switch `-SkipRepairs` ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The environment variable `QAR_VS_VERSION` (`2019`, the default, or `2022`) selects the Visual Studio of the scripts and the repairs ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- Troubleshooting section "A broken ROS 2 distribution" maps the known gaps of the Windows Humble binary release to the repair scripts ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- The ROS 2 root check accepts `setup.sh` (Linux) besides `local_setup.bat`, and `QAR_QT_ROOT` is optional on Linux ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).

### Bug fixes

- The repair scripts (`fix_missing_eigen.ps1`, `fix_missing_openssl_dll.ps1`, `fix_missing_tinyxml_dll.ps1`, `fix_missing_vendor_dlls.ps1`) unpack with the tar of Windows, so a GNU tar first on `PATH` (the one of Git) no longer hangs on a `C:\` archive path ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).
- `build_plugin.ps1` builds with MSVC (`cl`) even when a MinGW `c++` is on `PATH`; it used to pick MinGW, which cannot compile the plugin ([#5](https://github.com/Quaternar/qaros-ros-rviz/pull/5)).

## [0.2.0] - 2026-11-13

Verified with QAROS 0.9.0-dev.3008+65dc9aeb, ROS 2 Humble (`release-humble-20260220`), Qt 5.15.2, MSVC v142, CMake 3.22 or newer, qar_ogre_adapter and qar_qt_streaming 0.2.0; not tagged. Known limitations: [SDK compatibility](docs/SDK_COMPATIBILITY.md#known-sdk-limitations).

### Breaking

- The plugin builds against qar_ogre_adapter 0.2.0 (components `qar_ar_core`, `qar_ogre_streaming`, `capi`, `session`, `render`, `url`, `gestures`) and qar_qt_streaming 0.2.0 (`seam`, `qar_qt_streaming`) ([#4](https://github.com/Quaternar/qaros-ros-rviz/pull/4)).
- The Python packages of the ROS 2 build, `colcon` among them, moved from `requirements.txt` to `requirements-ros.txt`: install both into the same virtual environment ([#4](https://github.com/Quaternar/qaros-ros-rviz/pull/4)).

### Features

- Module `qar_capi_panels` (`qar_rviz_plugin::qar_capi_panels`): the QarOS C API behind the panel interfaces of `qar_qt_streaming::seam`, with the panel sender, its factory and the panel input source ([#4](https://github.com/Quaternar/qaros-ros-rviz/pull/4)).

### Enhancements

- The Qt widget panels use the interfaces and values of `seam` of qar_qt_streaming; they behave as before ([#4](https://github.com/Quaternar/qaros-ros-rviz/pull/4)).
- The pairing code texts name no Hub card and no code lifetime ([#4](https://github.com/Quaternar/qaros-ros-rviz/pull/4)).

## [0.1.0] - 2026-10-26

The first version. Verified with QAROS 0.9.0-dev.3008+65dc9aeb, ROS 2 Humble (`release-humble-20260220`), Qt 5.15.2, MSVC v142, CMake 3.22 or newer, qar_ogre_adapter and qar_qt_streaming 0.1.0; not tagged.

### Features

- The RViz2 panel `"Qar Streaming"`: joins the QAROS Hub through a session owner on its own thread, streams to every target the Hub lists, invites a HoloLens or headset, and keeps fourteen settings in the RViz2 configuration ([#3](https://github.com/Quaternar/qaros-ros-rviz/pull/3)).
- Live stereo scene per peer from the head pose QarOS reports, within a 16 ms budget, at least 30 frames a second and up to 8 peers ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Opaque floor under the Grid display, a world-fixed lighting rig and a geographic anchor from a `NavSatFix` topic ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- AR GUI panels of the RViz2 docks and dialogs, the hidden Displays panel, hiding and parking instead of closing, and the close modes `destroy` and `notice` ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- The AR control panel in front of each user: switches AR panels and tools, arms and confirms a pick, shows the connection state ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Placement of AR panels above the RViz ground, fixed poses, sizing for the QarOS frame, a 10 m app volume, and docks fitted to their content ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Diagnostics for black or frozen panels: frame brightness, missing destinations and per-panel state lines ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- AR tools from the ROS 2 graph: 17 semantic templates and a generic form, write rules and confirmation of robot commands, and a test publisher ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Video in AR from image tools and Image or Camera docks, the Fast-DDS large-frame profile and timing CSV files with their analyzer ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Spatial picking of a point, a navigation goal, an initial pose and an area on the RViz2 tool topics, with confirmation ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Launchers `run_live_rviz_stream_real.ps1` and `run_rviz_with_plugin.ps1`, the offline preview, five test scenes, the demo bag and its player ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Repairs `fix_old_vc_runtime.ps1` and `fix_missing_tinyxml_dll.ps1` with launcher checks that stop before RViz starts ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- Libraries `qar_ui_model` (UI model and page renderer), `qar_web_panels` (HTTP and WebSocket panel server) and `qar_ros_ui` (graph discovery, introspection, topic, service and action bridges) ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- URL panel sizing from the measured page and a fixed design viewport, the resize guard and the click pager ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
- The examples `qar_web_panel_example` and `qar_ros_topic_panel_example` ([#2](https://github.com/Quaternar/qaros-ros-rviz/pull/2)).
