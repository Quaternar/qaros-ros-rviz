# Environment Lock — M0

This document records the exact versions, paths, checksums, and environment recipe established
during M0 (environment provisioning). **Status: COMPLETE. The rviz2.exe smoke test PASSES — the
RViz2 main window launches and renders (OpenGL 4.6, "RViz is ready.") from the local console
session, verified by screenshot in M0b.** See "rviz2.exe smoke test: root causes and fix" below
for the two issues that were resolved.

## Versions

| Component | Version | Notes |
|---|---|---|
| ROS 2 | Humble, release tag `release-humble-20260220` | Windows binary distro |
| rviz2 (`external/rviz` submodule) | commit `ba6201f27724c4b287665d3f7e2711e11386e02e` | branch `humble`, tip as of 2026-06-24 |
| Ogre (via `rviz_ogre_vendor`) | 1.12.1 ("Rhagorthua") | confirmed via `OgreBuildSettings.h` and `OGREConfigVersion.cmake` |
| Qt | 5.15.2, `win64_msvc2019_64` kit | installed via aqtinstall (no Qt account needed) |
| Python (ROS-compatible) | 3.10.11 | **Not 3.8** as originally assumed — see below |
| Python (Anaconda base, NOT used) | 3.9.13 | confirmed incompatible |
| VS toolchain | VS2019 Community, MSVC 19.29.30151.0 (v142) | matches distro's compiled toolset |
| OpenSSL | not needed | `libcurl` was rebuilt using Schannel (Windows-native TLS), avoiding an OpenSSL dependency entirely |
| Qar API header (`qar_streaming.h`) | qaros commit `37eb9841097555a1c757ff0bc6be45bcb1cc24de` | vendored, MIT license |

## Critical deviation from plan: Python 3.10, not 3.8

The plan (§0/§6) assumed the ROS 2 Humble Windows binary distro was compiled against Python 3.8.
Inspection of `Lib\site-packages\**\*.pyd` files (e.g. `rclpy\_rclpy_pybind11.cp310-win_amd64.pyd`)
in the actual `release-humble-20260220` archive shows it is compiled against **Python 3.10**
instead. No `python38.dll` or embedded Python interpreter is shipped in the distro — an external
Python 3.10 matching that exact ABI tag must be on `PATH`.

Installed via: `winget install Python.Python.3.10 --scope machine --source winget --accept-package-agreements --accept-source-agreements`
(the `--source winget` flag was required — the default multi-source search fails against the
`msstore` source with a certificate error on this machine).

Location: `C:\Program Files\Python310\python.exe`

## SHA256 checksums

| File | SHA256 |
|---|---|
| `ros2-humble-20260220-windows-release-amd64.zip` (395,014,297 bytes) | `256ABCFA37F8D97D2CB3FF2A85F53465B016A25632399C99F67DC2BEE18E89C3` |
| `ros2-release-humble-20260220-src.zip` | `EA72D8921072A824D65E592F01E53A192766CD9AC50CA84D5EAB8F475474A28F` |

Both reproducible via `scripts/fetch_dependencies.ps1`.

## Install paths (large artifacts — outside the repo, per project rules)

| What | Path |
|---|---|
| ROS 2 binary distro (extracted) | `D:\development\ros2_humble\ros2-windows\` |
| ROS 2 source manifest repo (reference only) | `D:\development\ros2_src_release-humble-20260220\ros2-release-humble-20260220\` |
| Qt 5.15.2 | `D:\development\Qt\5.15.2\msvc2019_64\` |
| Downloaded zips/tarballs | `D:\development\downloads\` |
| Vendor DLL build trees (not needed after install, kept for rebuild convenience) | `D:\development\build_spdlog\`, `D:\development\build_vendor_dlls\` |

## Packaging gaps found and fixed in the official Windows release archive

The official `release-humble-20260220` Windows binary archive is missing **6 third-party runtime
DLLs** entirely (confirmed absent both in the extracted tree and inside the original zip's file
listing via `unzip -l` — not a download/extraction problem). Without these, `rcl.dll` (used by
both `rclpy` and `rclcpp`/`rviz2.exe`) fails to load at all with `STATUS_DLL_NOT_FOUND`
(0xC0000135), which blocked `ros2 --help`, `import rclpy`, and `rviz2.exe` alike:

| Missing DLL | Required by | Rebuilt from | Script |
|---|---|---|---|
| `spdlog.dll` | `rcl_logging_spdlog.dll` -> `rcl.dll` | spdlog v1.9.2 (fmt 8.x ABI, confirmed via mangled symbol comparison) | `scripts/fix_missing_spdlog_dll.ps1` |
| `console_bridge.dll` | `rviz_common.dll`, `tf2.dll`, `class_loader.dll` | ros/console_bridge tag 1.0.2 | `scripts/fix_missing_vendor_dlls.ps1` |
| `tinyxml2.dll` | `rviz_common.dll` | leethomason/tinyxml2 tag 9.0.0 | ″ |
| `yaml-cpp.dll` | `rviz_common.dll` | jbeder/yaml-cpp tag yaml-cpp-0.7.0 | ″ |
| `assimp-vc141-mt.dll` | `rviz_rendering.dll` | assimp/assimp tag v4.1.0 (renamed from assimp's own output name `assimp-vc140-mt.dll`) | ″ |
| `libcurl.dll` | `resource_retriever.dll` | curl/curl tag curl-7_81_0, built with Schannel TLS | ″ |
| `libssl-1_1-x64.dll` + `libcrypto-1_1-x64.dll` | `fastrtps-2.6.dll` (Fast-DDS, the default `rmw_fastrtps_cpp` middleware) | OpenSSL **1.1.1** x64 (any 1.1.1 patch level; the 1.1.1 series is ABI-stable). NOT built from source — copied from a conda OpenSSL 1.1.1 install. | `scripts/fix_missing_openssl_dll.ps1` |

(The OpenSSL pair was the M0b discovery — without it the RMW cannot load, which is what actually
broke rviz2 at runtime; see the smoke-test section below. Do NOT use OpenSSL 3.x: Fast-DDS 2.6
imports the `libssl-1_1` / `libcrypto-1_1` names, not `libssl-3`.)

All versions were traced from each ROS2 "vendor" ament package's `package.xml` dependency name to
its pinned upstream source via the vendor package's `CMakeLists.txt` on GitHub at its installed
tag (readable from each package's `share\<pkg>\cmake\*Config.cmake` "Found X: version" message).
Each rebuilt DLL's exported symbols were spot-verified via `dumpbin /exports` against the actual
mangled C++ symbols the consuming DLL imports (via `dumpbin /imports`), to catch ABI mismatches.

Additionally, `rviz2.exe`/`rviz_common.dll`/`rviz_rendering.dll`/`rviz_default_plugins.dll` link
against **`Qt5Core_conda.dll`, `Qt5Gui_conda.dll`, `Qt5Widgets_conda.dll`** — a "_conda"-suffixed
naming convention from the conda/pixi Qt package the official CI build machine used (evidenced by
`local_setup.bat`'s RTI Connext warning path and `ros2-script.py`'s shebang, both pointing at
`C:\pixi_ws\...`). aqtinstall's standard Qt build uses plain names. **`scripts/fix_qt_conda_imports.ps1`**
binary-patches those 4 rviz binaries so they import the plain `Qt5Core.dll`/`Qt5Gui.dll`/`Qt5Widgets.dll`
names (length-preserving edit, no RVA shifts), which resolves to the single aqt Qt on `PATH`.
See the smoke-test section below for why the older alias-copy approach
(byte-identical `_conda`-named copies of the Qt DLLs) was broken.

The 6 rebuilt DLLs + the OpenSSL pair are installed into `D:\development\ros2_humble\ros2-windows\bin\`.

## Plugin-dev prerequisites (validates plan decision §1.1)

Confirmed present for all 5 required packages — headers, `.lib` import libraries, and CMake
config packages:

| Package | Headers | `.lib` | CMake config |
|---|---|---|---|
| `rviz_common` | `include\rviz_common` | `Lib\rviz_common.lib` (696,588 bytes) | `share\rviz_common\cmake\rviz_commonConfig.cmake` |
| `rviz_rendering` | `include\rviz_rendering` | `Lib\rviz_rendering.lib` (148,186 bytes) | `share\rviz_rendering\cmake\rviz_renderingConfig.cmake` |
| `rviz_ogre_vendor` | `opt\rviz_ogre_vendor\include\OGRE` (nested prefix, not top-level!) | `opt\rviz_ogre_vendor\lib\OgreMain.lib` etc. | `share\rviz_ogre_vendor\cmake\rviz_ogre_vendorConfig.cmake` — defines proper CMake `IMPORTED` targets `rviz_ogre_vendor::OgreMain`, `::OgreOverlay`, `::OgreGLSupport` with correct include dirs |
| `pluginlib` | `include\pluginlib` | (header-only-ish; no separate `.lib` expected) | `share\pluginlib\cmake\pluginlibConfig.cmake` |
| `rclcpp` | `include\rclcpp` | `Lib\rclcpp.lib` (605,200 bytes) + `rclcpp_action.lib`, `rclcpp_lifecycle.lib` | `share\rclcpp\cmake\rclcppConfig.cmake` |

**Conclusion: the binary-distro-only build strategy (plan §1.1) is validated for build/link time.**
M1's plain-CMake library should `find_package(rviz_ogre_vendor REQUIRED)` and link
`rviz_ogre_vendor::OgreMain` directly — no manual include/lib path hacking needed. This is
independent of the rviz2.exe runtime issue below (that's an executable startup bug, not a
build/link gap).

## rviz2.exe smoke test: root causes and fix (M0b — SMOKE TEST PASSES)

The M0 smoke test failure (`rviz2.exe` exiting immediately, a GUI dialog showing only `:`) was
tracked down in M0b to **two independent, deterministic bugs**, both now fixed. RDP was ruled out
first: the failure reproduced identically on the local console session, so remote-desktop / GL
context was NOT the cause. Diagnosis used `dumpbin` import-table inspection and a procdump full
crash dump (the dump's unhandled-exception record named the exact C++ exception type).

**Bug 1 — two Qt5Core copies in one process (the `:` / "Please instantiate the QApplication
object first" failure).** The old approach made byte-identical COPIES
of the aqt Qt DLLs under the `_conda` names. But `dumpbin /dependents` shows those copies' own
import tables still reference the PLAIN names (`Qt5Widgets_conda.dll` -> `Qt5Gui.dll`,
`Qt5Core.dll`). So BOTH `Qt5Core.dll` and `Qt5Core_conda.dll` loaded. `QApplication` (built via
the `_conda` copy chain) set `QCoreApplication::self` in one physical `Qt5Core`, while
`rviz_common.dll`'s `VisualizerApp::init()` read `QCoreApplication::arguments()` from the OTHER
`Qt5Core_conda.dll` (whose static was still null) -> the "instantiate the QApplication object
first" warning and exit code 1. **Fix:** `scripts/fix_qt_conda_imports.ps1` binary-patches the
4 consumer binaries (`rviz2.exe`, `rviz_common.dll`, `rviz_rendering.dll`,
`rviz_default_plugins.dll`) to import the plain `Qt5*.dll` names — a single Qt5Core loads.
(The earlier M0 "cmd.exe vs Start-Process" theory was a red herring; launch method is irrelevant.)

**Bug 2 — missing OpenSSL breaks the RMW (the real crash after Bug 1 was fixed).** With Bug 1
fixed, Qt initialized fully (platform plugin, windowsvista style all loaded) but the process then
died with exit code `0xC0000409` and no window. The procdump crash dump identified the unhandled
exception as **`rclcpp::exceptions::RCLError`**, reproduced verbatim via `rclpy.init()`:
`"Failed to initialize init options: failed to load any RMW implementations"`. Cause: the default
middleware `rmw_fastrtps_cpp` -> `fastrtps-2.6.dll` links **OpenSSL 1.1.1**
(`libssl-1_1-x64.dll` + `libcrypto-1_1-x64.dll`), which the distro omits, so the RMW failed to
load. **Fix:** `scripts/fix_missing_openssl_dll.ps1` installs the OpenSSL 1.1.1 x64 pair.
(NOTE: the M0 `import rclpy` test passed because importing rclpy does not load an RMW — only
`init()` does; this is why the gap was missed until M0b.)

After both fixes, `rviz2.exe` launches and the RViz main window renders: 3D grid viewport,
Displays/Views panels, `OpenGl version: 4.6 (GLSL 4.6)`, "RViz is ready." Verified by screenshot
across two independent launches from the local console session.

## Environment variables / how to reproduce

Dot-source `scripts/env_setup.ps1` from a PowerShell session:
```powershell
. .\scripts\env_setup.ps1
```
This chains the distro's `local_setup.bat` environment into the current session, prepends the
repo's `.venv\Scripts` and the aqt Qt `bin` directory to `PATH`, and sets
`QT_QPA_PLATFORM_PLUGIN_PATH`/`QT_PLUGIN_PATH`. **Must be dot-sourced** — plain execution runs in
a child process and none of the environment changes persist into your shell.

Run `ros2` via: `python "D:\development\ros2_humble\ros2-windows\Scripts\ros2-script.py" <args>`
— **not** `ros2.exe`/`ros2` directly (its shebang is hardcoded to the CI build machine's path and
fails with "failed to create process" otherwise).

To launch RViz once the environment is set up: `rviz2.exe` (or `ros2 run rviz2 rviz2`). It must be
run from the **local console session** (verified there; not required to be, but that is where the
smoke test passed).

## Reproduction scripts (all committed, all idempotent)

1. `scripts/fetch_dependencies.ps1` — downloads + SHA256-verifies + extracts both ROS2 zips.
2. `scripts/fix_missing_spdlog_dll.ps1` — rebuilds + installs `spdlog.dll`.
3. `scripts/fix_missing_vendor_dlls.ps1` — rebuilds + installs the other 5 missing DLLs.
4. `scripts/fix_missing_openssl_dll.ps1` — installs the OpenSSL 1.1.1 x64 pair the RMW needs.
5. `scripts/fix_qt_conda_imports.ps1` — patches the 4 rviz binaries to import plain Qt5 names.
   (Replaces the broken alias-copy approach, described for history.)
7. `scripts/env_setup.ps1` — dot-source this to set up a working PowerShell session (see above).

Order: 1, then create `.venv` + `pip install -r requirements.txt`, then install Qt via aqt, then
run 2, 3, 4, 5 (any order among themselves), then dot-source 7. rviz2.exe should then launch and
render the main window.

## Downstream ament-plugin build gaps (discovered in M3)

The fixes above make **rviz2.exe run**, but building a downstream **ament plugin**
that `find_package(rviz_common)` (first done in M3) exposed a second class of gaps:
the official archive omits CMake **config files / dev headers / import libs** for
several third-party packages whose *runtime DLLs* M0 rebuilt. `find_package(rviz_common)`
pulls these transitively, failing at configure, generate, compile, or link. Also,
the distro's exported CMake targets hard-code the pixi CI machine's absolute paths
(`C:\pixi_ws\.pixi\envs\default\…`). Three new idempotent scripts fix all of this
(run automatically by `scripts/build_plugin.ps1` before colcon):

8.  `scripts/fix_missing_vendor_cmake_configs.ps1` — shim CMake configs for **assimp,
    curl, yaml-cpp, TinyXML2, spdlog, orocos_kdl, console_bridge** (creating the
    `assimp::assimp` / `tinyxml2::tinyxml2` / `spdlog::spdlog` / `orocos-kdl` /
    `console_bridge::console_bridge` targets the exported rviz targets reference).
    console_bridge additionally needs real **headers** (`console_bridge/console.h`
    is `#include`d by class_loader) and its **import lib** (`console_bridge::log` is
    pulled in by `PLUGINLIB_EXPORT_CLASS`) — both vendored from the M0
    `build_vendor_dlls` tree.
9.  `scripts/fix_missing_eigen.ps1` — vendors Eigen 3.4.0 headers (from Anaconda) +
    writes `Eigen3Config.cmake`.
10. `scripts/fix_stale_pixi_paths.ps1` — materializes the baked `C:\pixi_ws\…` paths
    (eigen3/`Library/include`/Qt/mkspecs include dirs, numpy headers, `python310.lib`,
    and a valid empty stub `Library/lib/tinyxml.lib`) so CMake's generate-step
    path-existence checks pass. The plugin does not compile/link against these; they
    only need to exist.

These are consumed by the M3 `qar_rviz_streaming` plugin build; see
`qar_rviz_streaming/docs/build_windows.md` for the colcon workflow. They will be
needed by every later ament plugin milestone (M4+).
