# qar_rviz_plugin

An RViz2 panel that streams the **live RViz 3D scene** to the **QarOS** AR runtime
as a per-eye stereo pair, so someone in a headset sees the robot's data rendered by
the same RViz everyone else is looking at. Peers are managed from the panel, the
scene can be placed at a real position on Earth from a GPS fix, and the whole thing
runs inside stock `rviz2.exe` — no RViz fork.

The rendering and QarOS plumbing live in a separate repository,
`qar_ogre_adapter`; this one is the RViz2 integration.

---

## The QarOS SDK is not included

The QarOS runtime — `qar-streaming-c.dll`, `qar_streaming.dll`, the zenoh and
GStreamer stacks, `qar-streaming-viz.exe` (the Visualizer) — is **Quaternar's
proprietary binary distribution and is not redistributed here**. Obtain it from
Quaternar and extract it somewhere local, e.g. `C:\QarOS\install`.

You do **not** need it to build. The adapter resolves the SDK dynamically at run
time, so only the vendored header (which ships inside `qar_ogre_adapter`) is needed
to compile. You need it to *stream*.

---

## What is in here

```
qar_rviz_streaming/     The ament package: the pluginlib panel.
  include/ src/           QarStreamingPanel, the RViz render-lock/executor/context
                          adapters, the peer model, offscreen PNG capture.
  config/                 A dev RViz config + five test scenes.
  third_party/stb/        Single-header PNG writer, for the debug capture path.
docs/                    Plugin architecture, build, testing, rosbag, geolocation.
scripts/                 Fresh-machine setup, build, run and rosbag tooling.
```

---

## Setting up a fresh Windows machine

Verified toolchain (full detail and reasoning in
[`docs/environment_lock.md`](docs/environment_lock.md)):

| Component | Version |
|---|---|
| ROS 2 | Humble, Windows binary distro, release tag `release-humble-20260220` |
| Ogre (via `rviz_ogre_vendor`) | 1.12.1 |
| Qt | 5.15.2, `win64_msvc2019_64` kit (via aqtinstall — no Qt account needed) |
| Python | **3.10.11** — not 3.8; the distro's `.pyd` files are `cp310` |
| VS toolchain | VS2019 Community, MSVC 19.29.30151.0 (v142) |

### 1. Clone both repositories to a short path

```powershell
git clone <adapter-url> C:\dev\qar_ogre_adapter
git clone <plugin-url>  C:\dev\qar_rviz_plugin
```

Short paths matter: GoogleTest's configure-time fetch in the adapter repository
creates paths deep enough to overflow the Windows 260-character `MAX_PATH` limit
from a nested clone.

### 2. Fetch the ROS 2 Humble Windows distro

```powershell
cd C:\dev\qar_rviz_plugin
powershell -File scripts\fetch_dependencies.ps1
```

Downloads, SHA256-verifies and extracts the official `release-humble-20260220`
archive to `D:\development\ros2_humble\ros2-windows\`. Override the destination
with the script's parameters; downstream scripts then take that path via
`-Ros2DistroRoot` / the `ROS2_INSTALL_ROOT` environment variable rather than
assuming a default.

### 3. Python venv

```powershell
winget install Python.Python.3.10 --scope machine --source winget --accept-package-agreements --accept-source-agreements
"C:\Program Files\Python310\python.exe" -m venv .venv
.venv\Scripts\pip install -r requirements.txt
```

### 4. Qt 5.15.2

```powershell
.venv\Scripts\python.exe -m aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --outputdir D:\development\Qt
```

### 5. Patch the official archive — required, not optional

The official Windows release ships with real defects: several missing runtime DLLs
and a Qt DLL-naming mismatch. Without these fixes `rviz2.exe` does not start at
all. Run once, in this order (all idempotent):

```powershell
scripts\fix_missing_spdlog_dll.ps1
scripts\fix_missing_vendor_dlls.ps1        # console_bridge, tinyxml2, yaml-cpp, assimp, libcurl
scripts\fix_missing_openssl_dll.ps1        # libssl-1_1-x64 + libcrypto-1_1-x64, needed by Fast-DDS
scripts\fix_qt_conda_imports.ps1           # patches rviz2 + rviz_common/rviz_rendering/rviz_default_plugins
```

Two failures these prevent, both of which look like something else entirely:

- **`0xC0000409` on launch, no window.** Fast-DDS needs OpenSSL 1.1.1, which the
  archive omits, so the RMW fails to load and `rcl` init throws.
- **"Please instantiate the QApplication object first".** The RViz binaries were
  built against a conda Qt and import `Qt5Core_conda.dll` etc. Supplying those as
  *copies* of the real Qt DLLs makes it worse, not better: the copies' own import
  tables still name the plain DLLs, so two Qt copies load into one process and
  `QApplication` sets its static in the one `rviz_common` isn't reading.
  `fix_qt_conda_imports.ps1` binary-patches the four binaries to import the plain
  names, so exactly one Qt loads.

Neither is caused by RDP; both reproduce on the local console.

### 6. Patch the downstream ament/CMake gaps

`find_package(rviz_common)` needs several third-party CMake configs and dev headers
the runtime-DLL fixes above don't provide, and some distro-exported CMake targets
hard-code the original CI machine's `C:\pixi_ws\...` paths.
`scripts\build_plugin.ps1` runs these automatically, so you rarely need them by
hand:

```powershell
scripts\fix_missing_vendor_cmake_configs.ps1
scripts\fix_missing_eigen.ps1
scripts\fix_stale_pixi_paths.ps1
```

### 7. Every new shell session

```powershell
. .\scripts\env_setup.ps1
```

**Must be dot-sourced** (leading `. `). A plain invocation runs in a child process
and none of the environment changes survive.

---

## Building

### Both repositories in one workspace (recommended)

```
ws/
  src/
    qar_ogre_adapter/
    qar_rviz_plugin/
```

```powershell
cd ws
colcon build --cmake-args -DQAR_ROS2_INSTALL_ROOT=<distro>\ros2-windows
```

colcon reads the `package.xml` dependencies and builds
`qar_ogre_streaming` → `qar_qar_adapter` → `qar_rviz_streaming` in that order.

**On Windows, run this from a shell with the Visual Studio developer environment
already imported** — an "x64 Native Tools Command Prompt for VS 2019" / "Developer
PowerShell for VS 2019" (Start menu), or `vcvars64.bat` dot-sourced by hand.
Otherwise colcon's CMake task fails immediately with `VisualStudioVersion is not
set, please run within a Visual Studio Command Prompt`, regardless of which CMake
generator is used — this is a colcon requirement on Windows, not specific to this
repository. `qar_ogre_adapter/scripts/build_adapter.ps1` does this automatically
when building that repository alone.

### This repository alone

Build the adapter repository first, then:

```powershell
.\scripts\build_plugin.ps1 -Clean
```

It imports the VS2019 x64 environment, sources the ROS 2 environment, applies the
CMake-gap fixes and runs colcon. It finds the adapter at
`..\qar_ogre_adapter\install` by default; override with
`-AdapterInstall <prefix>`.

Success prints `=== BUILD OK ===` and the path to `qar_rviz_streaming.dll`.

---

## Running it

```powershell
.\scripts\run_live_rviz_stream_real.ps1
```

Opens RViz with the panel loaded, with the QarOS runtime on `PATH`. In the panel:

1. **Connect to QarOS** — the state label goes `CONNECTING...` then
   `RUNNING (live)`. It is asynchronous on purpose; the handshake takes ~3 s and
   running it on the Qt thread froze RViz.
2. **Invite peer** — a QarOS Visualizer opens and shows the RViz scene. Invite
   again for a second, independent instance.
3. Move the RViz camera; the Visualizer follows.

Point it at the SDK with `-QarSdkRoot <path>` if it is not at the default.
`-Visualizers <n>` auto-invites at connect (default 0);
`-InvitePeers hololens:<hostname>` invites a real device.

To run the panel **without** QarOS at all — useful for checking the plugin loads —
use `scripts\run_rviz_with_plugin.ps1`, which drives it with mock peers.

---

## Testing

**There are no automated tests in this repository, and that is deliberate.**
Everything here is bound to a running `rviz2.exe`, a live GL context, a
pluginlib-loaded Qt panel and a QarOS runtime; a unit test of the panel would be a
test of mocks. The automated coverage — **108 tests** — lives one layer down in
`qar_ogre_adapter`, and logic worth testing gets moved there rather than mocked
here.

What this repository has instead is scripted verification that drives real RViz and
reports pass/fail. See [`docs/testing.md`](docs/testing.md) for the full procedure;
in brief:

```powershell
.\scripts\run_rviz_with_plugin.ps1 -AutoDump .\dump.json      # panel loads, real SceneManager, peer round trip
.\scripts\run_rviz_with_plugin.ps1 -AutoCapture .\out         # live scene renders offscreen to PNG
.\scripts\run_rviz_with_plugin.ps1 -AutoStereo .\out          # per-eye stereo with correct IPD
.\scripts\run_rviz_with_plugin.ps1 -GlSpike .\gl.json         # native GL texture-id extraction
.\scripts\capture_test_scenes.ps1                             # all five test scenes
```

### Rosbag playback

```powershell
.\scripts\play_demo_rosbag.ps1 -BagDir <bag directory>
```

Then in RViz add a PointCloud2 / TF / Marker display and stream it as usual. Full
detail in [`docs/rosbag_metadata.md`](docs/rosbag_metadata.md).

**Three things account for almost every "the bag plays but I see nothing":**

1. **QoS.** Sensor publishers are conventionally BEST_EFFORT, and a RELIABLE
   subscriber receives **nothing** from one — silently, with no error anywhere.
   `scripts\rosbag_qos_override.yaml` fixes this and the script passes it; keep
   using it.
2. **Fixed Frame.** Set it to a frame the bag actually publishes a TF for
   (`arco/base_link` for the ARCO bags). Leaving it at `map` when the bag has no
   `map` frame renders nothing and warns only about a transform.
3. **Point size.** A 32768-point Ouster frame at the default 0.01 m is a few nearly
   invisible pixels. Raise it.

---

## Geolocation

The panel subscribes to a `sensor_msgs/NavSatFix` topic (default `/gps/fix`) and
uses the fix to anchor the streamed scene to a real position on Earth, so an AR
client standing in the right place sees the robot's data where the robot actually
is. The fix is converted to an ECEF origin plus a local East/North/Up basis and
installed on the QarOS **app volume** — which means disabling the app volume
disables geolocation entirely.

The subscription callback never calls into QarOS: it stores the fix, and the
Qt-thread render tick applies it, keeping every SDK call on one thread.
Re-anchoring is throttled to 2 m so a 10 Hz receiver doesn't drive an SDK call per
message. The QoS is `SensorDataQoS` (BEST_EFFORT) for the reason in point 1 above.

| Variable | Default | Meaning |
|---|---|---|
| `QAR_PANEL_GPS_TOPIC` | `/gps/fix` | Topic to subscribe to. |
| `QAR_PANEL_GEO` | on | `off` disables the path. |
| `QAR_PANEL_APP_VOLUME` | on | `off` removes the volume, and with it any anchor. |

**Status: built and unit tested, never exercised against a live GPS receiver** — no
supplied rosbag contains a GPS topic. Full detail, including why RViz's own
`FrameManager` is the wrong source (float32 across a 6.4 × 10⁶ m ECEF radius
quantises to about half a metre): [`docs/geolocation.md`](docs/geolocation.md).

---

## Documentation

| Document | Contents |
|---|---|
| [`docs/rviz_plugin_architecture.md`](docs/rviz_plugin_architecture.md) | How the panel is put together and how it reaches RViz's render thread. |
| [`docs/build_windows.md`](docs/build_windows.md) | The full setup sequence end to end, plus a troubleshooting table. |
| [`docs/testing.md`](docs/testing.md) | Every verification mode, and why there are no unit tests here. |
| [`docs/offscreen_rendering.md`](docs/offscreen_rendering.md) | How the live RViz scene is rendered into offscreen stereo targets. |
| [`docs/rosbag_metadata.md`](docs/rosbag_metadata.md) | Playing rosbags into the plugin, and the QoS trap. |
| [`docs/geolocation.md`](docs/geolocation.md) | GPS → QarOS app-volume anchoring. |
| [`docs/environment_lock.md`](docs/environment_lock.md) | Exact versions, paths and the environment recipe. |

## License

MIT (see individual file headers). `third_party/stb/` carries its own license. The
QarOS runtime is proprietary and separately licensed by Quaternar.
