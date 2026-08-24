# Windows build & run — detailed reference

[`README.md`](../README.md) has the short setup path. This page is the longer
reference: what each step actually does, and a troubleshooting table for the
failures the official ROS 2 Humble Windows archive produces.

**One-time vs per-checkout vs per-shell.** Steps 1–6 fix the *machine* (ROS 2
distro, Qt, and a handful of packaging gaps in the official archive) and run
**once per machine**, not once per clone. Step 7 is per-shell. Everything after
that is per-checkout.

## 0. Prerequisites

| Tool | Version used | Notes |
|---|---|---|
| Windows | 11 Pro | |
| Visual Studio | 2019 Community, MSVC v142 (`19.29.30151.0`) | Must match the ROS 2 Humble Windows distro's compiled toolset. VS 2022+ may coexist but is not used for this build. |
| CMake | ≥ 3.21 | |
| Ninja | any recent | used by colcon |
| Git | any recent | |
| Python | 3.10.x — **not** the officially documented 3.8 (step 3) | |

## 1. Clone

```powershell
git clone <adapter-url> C:\dev\qar_ogre_adapter
git clone <plugin-url>  C:\dev\qar_rviz_plugin
```

**Clone to a short path.** The adapter repository fetches GoogleTest at configure
time, and its sub-build creates paths deep enough to overflow the Windows
`MAX_PATH` (260 character) limit from a deeply nested clone. The failure appears as
`ninja: error: Stat(...): Filename longer than 260 characters` during the
googletest populate step, and mentions nothing about your clone path.

(Alternative: enable long paths with `git config --global core.longpaths true`
**and** the Windows `LongPathsEnabled` registry policy — a machine-wide policy
change, so this project's scripts do not apply it for you.)

## 2. Fetch the ROS 2 Humble Windows binary distro

```powershell
powershell -File scripts\fetch_dependencies.ps1
```

Downloads, SHA256-verifies and extracts the official `release-humble-20260220`
Windows binary release to `D:\development\ros2_humble\ros2-windows\`. Large
artifacts intentionally live outside the repository; override the destination via
the script's parameters, and downstream scripts then take that path via
`-Ros2DistroRoot` / the `ROS2_INSTALL_ROOT` environment variable rather than
assuming the default. Exact checksums and the source-zip pointer are in
[`environment_lock.md`](environment_lock.md).

## 3. Python venv

The distro is compiled against **Python 3.10**, not the officially documented 3.8
— its `.pyd` files carry the `cp310` ABI tag. See
[`environment_lock.md`](environment_lock.md) for how that was established.

```powershell
winget install Python.Python.3.10 --scope machine --source winget --accept-package-agreements --accept-source-agreements
"C:\Program Files\Python310\python.exe" -m venv .venv
.venv\Scripts\pip install -r requirements.txt
```

## 4. Qt 5.15.2 (msvc2019_64) via aqtinstall

```powershell
.venv\Scripts\python.exe -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --outputdir D:\development\Qt
```

No Qt account needed, unlike the official Qt installer.

## 5. Fix the archive's runtime packaging gaps

Required — `rviz2.exe` does not start without these. Run in this order; all are
idempotent.

```powershell
scripts\fix_missing_spdlog_dll.ps1
scripts\fix_missing_vendor_dlls.ps1        # console_bridge, tinyxml2, yaml-cpp, assimp, libcurl
scripts\fix_missing_openssl_dll.ps1        # libssl-1_1-x64 + libcrypto-1_1-x64, for Fast-DDS/RMW
scripts\fix_qt_conda_imports.ps1           # patches rviz2 + rviz_common/rviz_rendering/rviz_default_plugins
```

Without them `rcl.dll` fails to load (`STATUS_DLL_NOT_FOUND`), and `rviz2.exe`
either cannot `import rclpy` or crashes with `0xC0000409` inside
`rclcpp::exceptions::RCLError` ("failed to load any RMW implementations"). Full
root-cause narratives — dumpbin evidence, exact symbol names, version provenance —
are in [`environment_lock.md`](environment_lock.md).

## 6. Fix the downstream ament/CMake gaps

`find_package(rviz_common)`, first exercised when building this plugin, pulls in
third-party CMake configs, dev headers and import libs that the *runtime*-DLL fixes
above do not provide. Some distro-exported CMake targets also hard-code the
original CI machine's `C:\pixi_ws\...` paths. `scripts\build_plugin.ps1` runs these
automatically, so you rarely need them by hand:

```powershell
scripts\fix_missing_vendor_cmake_configs.ps1   # assimp, curl, yaml-cpp, TinyXML2, spdlog, orocos_kdl, console_bridge
scripts\fix_missing_eigen.ps1                  # vendors Eigen 3.4.0 headers + writes Eigen3Config.cmake
scripts\fix_stale_pixi_paths.ps1               # materializes the stale C:\pixi_ws\... paths so CMake's existence checks pass
```

`fix_missing_eigen.ps1` and `fix_stale_pixi_paths.ps1` look for an existing
Anaconda/Miniconda install (any account, via `$env:USERPROFILE` / `C:\ProgramData`)
as a source for Eigen/numpy headers. Pass `-EigenSourceInclude` /
`-NumpySourceInclude` explicitly if your machine has neither. The plugin does not
compile against them — the scripts' own doc comments explain why those paths only
need to *exist*.

## 7. Every new shell session

```powershell
. .\scripts\env_setup.ps1
```

**Must be dot-sourced** (leading `. `). A plain invocation runs in a child process
and none of the environment changes — `PATH`, `AMENT_PREFIX_PATH`, the Qt plugin
path — survive into your shell. It chains the distro's `local_setup.bat`, then
prepends the repository's `.venv\Scripts` and the aqt Qt `bin` directory to `PATH`.

## 8. Build

Either build both repositories in one colcon workspace:

```powershell
cd ws          # with src/qar_ogre_adapter and src/qar_rviz_plugin
colcon build --cmake-args -DQAR_ROS2_INSTALL_ROOT=D:\development\ros2_humble\ros2-windows
```

Run this from a shell with the Visual Studio developer environment already
imported (an "x64 Native Tools Command Prompt for VS 2019" from the Start menu, or
`vcvars64.bat` dot-sourced by hand) — colcon's CMake task on Windows fails with
`VisualStudioVersion is not set, please run within a Visual Studio Command Prompt`
otherwise, regardless of generator. `qar_ogre_adapter\scripts\build_adapter.ps1`
imports it automatically when building that repository on its own; there is no
equivalent single-command wrapper for the combined two-repository workspace, since
its layout is up to you.

or build this one against an already-installed adapter:

```powershell
powershell -File scripts\build_plugin.ps1
```

which imports the VS2019 x64 dev environment, dot-sources `env_setup.ps1`, runs the
step-6 fixes, then runs `colcon build --paths qar_rviz_streaming` with Ninja and
**RelWithDebInfo**. Never Debug: a Debug plugin mixes CRT and Qt runtimes with the
Release `rviz2.exe` and fails to load.

The plugin resolves `qar_ogre_streaming` and `qar_qar_adapter` with
`find_package()`. `build_plugin.ps1` looks for them at
`..\qar_ogre_adapter\install` by default; override with `-AdapterInstall <prefix>`.

## 9. Run

```powershell
powershell -File scripts\run_rviz_with_plugin.ps1          # mock peers, no QarOS needed
powershell -File scripts\run_live_rviz_stream_real.ps1     # live QarOS round trip
```

The first opens RViz2 with `qar_rviz_streaming/config/qar_streaming_dev.rviz`,
which pre-loads `QarStreamingPanel` (or add it via **Panels → Add New Panel →
qar_rviz_streaming → QarStreamingPanel**). **Run from the local console session**,
not RDP, for GL-heavy interactive work.

See [`testing.md`](testing.md) for the non-interactive verification modes.

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `rviz2.exe` exits immediately; dialog shows only `:` or "Please instantiate the QApplication object first" | Two Qt5Core copies loaded — `_conda`-named copies still import the plain Qt names | `scripts\fix_qt_conda_imports.ps1` |
| `rviz2.exe` starts Qt fully then dies with exit `0xC0000409`, no window | Missing OpenSSL 1.1.1 x64 → Fast-DDS (`rmw_fastrtps_cpp`) can't load → unhandled `rclcpp::exceptions::RCLError` | `scripts\fix_missing_openssl_dll.ps1` |
| `ros2` / `ros2.exe` CLI fails with "failed to create process" | The wrapper's shebang hardcodes the original CI build machine's path | Run `python "<distro>\Scripts\ros2-script.py" <args>` instead |
| `find_package(rviz_common)` fails — "Could NOT find Eigen3" / assimp / curl / yaml-cpp / tinyxml2 / spdlog / console_bridge / orocos_kdl | The archive ships those packages' runtime DLLs but not their CMake configs, dev headers or import libs | `scripts\build_plugin.ps1` runs the three step-6 scripts automatically |
| CMake generate fails: "Imported target ... includes non-existent path ..." under `C:\pixi_ws\...` | The distro's exported CMake targets hard-code the original CI machine's absolute paths | `scripts\fix_stale_pixi_paths.ps1` |
| `find_package(qar_ogre_streaming)` fails | The adapter repository is not built, or its prefix is not on `CMAKE_PREFIX_PATH` | Build it first; pass `-AdapterInstall <prefix>`, or build both repositories in one colcon workspace |
| `ninja: error: Stat(...): Filename longer than 260 characters` while fetching googletest | Clone path too deep (Windows `MAX_PATH`) | Clone to a short path — see step 1 |
| **Scene 4 (RobotModel) crashes `rviz2.exe`** (`0xC0000409`) when the URDF is published | A distro/urdfdom-side bug parsing the URDF. Reproduces in plain auto-dump mode with **no** offscreen or plugin rendering involved | Not this project's bug. Scenes 1/2/3/5 are unaffected. |
| colcon/CMake picks the wrong Python and fails importing `catkin_pkg`/`lark` | The system Python lacks them; the repository venv has them | `build_plugin.ps1` passes `-DPython3_EXECUTABLE=<repo>\.venv\Scripts\python.exe` explicitly |
| A `QAR_STEREO_IPD_M` value like `0.35` is silently rejected or parsed wrong | A comma-decimal locale (e.g. `cs-CZ`) formats it as `0,35`, which the plugin's C-locale float parse rejects | Already handled: the launch script formats it with `InvariantCulture`. If you set the variable yourself, use a `.` decimal point |
| The rosbag plays but nothing renders | Almost always QoS (a RELIABLE subscriber gets nothing from a BEST_EFFORT publisher), a Fixed Frame with no TF, or a point size too small to see | See [`rosbag_metadata.md`](rosbag_metadata.md) |
| Interactive RViz2 renders oddly over Remote Desktop | Not reproduced as a real bug here — the actual causes were the two DLL bugs above, which reproduce identically on the local console | Still recommended: run from the local console session for GL-heavy interactive work |
