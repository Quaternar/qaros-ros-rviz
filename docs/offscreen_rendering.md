# Offscreen RViz scene rendering (M4) — Gate A

This documents how the `qar_rviz_streaming` plugin renders the **live RViz Ogre
scene** into an offscreen `Ogre::RenderTexture` (color + metric depth) from a
plugin, without forking RViz2.

## GATE A DECISION: **PASS** → continue plugin-first.

The plugin renders the live RViz scene into offscreen Ogre RenderTextures on
Windows / GL3+ (OpenGL 4.6), entirely from the plugin, with no RViz fork and no
RViz source changes. Color output contains real scene content matching the main
view's pose; the metric depth output is non-uniform and encodes true 3D
structure. Proof captures for Grid, TF, Marker, and PointCloud2 scenes are below.

## What was proven (captured PNGs)

Captured non-interactively with `scripts/capture_test_scenes.ps1` (each scene
starts its data publisher, launches `rviz2` with the panel, which renders the
live scene offscreen and writes PNGs, then quits). Committed proof images live in
`docs/img/m4/`; regenerate into `build/m4_captures/`.

| Scene | Displays | color content px | depth non-zero px | depth range (m) | Result |
|---|---|---|---|---|---|
| 1 | Grid | 13021 | 13021 | 5.69 – 16.20 | **PASS** |
| 2 | Grid + TF | 15438 | 17233 | 5.02 – 14.80 | **PASS** |
| 3 | Grid + MarkerArray | 58658 | 58658 | 6.62 – 16.29 | **PASS** |
| 5 | Grid + PointCloud2 | 95421 | 10363 | 8.01 – 18.68 | **PASS** |
| 4 | Grid + RobotModel | — | — | — | **not captured** (see below) |

- `docs/img/m4/scene3_grid_marker_color.png` — red cube, green sphere, blue
  cylinder, yellow ring on the grid, correctly coloured (RGBA channel order
  verified) and lit. Its depth `..._depth.png` shows near-bright/far-dark, with
  the cube nearest (brightest) and the grid receding — genuine metric depth.
- `docs/img/m4/scene1_grid_*` — the grid in the Orbit perspective; depth is a
  smooth near→far gradient.
- `docs/img/m4/scene2_grid_tf_color.png` — TF axes (R/G/B), frame labels
  (map/base_link/lidar/camera_link) and arrows.
- `docs/img/m4/scene5_grid_pointcloud_color.png` — RGB sine-wave point cloud.

These are genuine **offscreen** renders, not screen grabs: the RTT uses our own
clear colour, a 1280×720 aspect that differs from the RViz window, and the depth
is shader-computed metric eye-space distance that no screen grab could produce.

## How the offscreen path works

1. **Seam (M3).** `RVizDisplayContextAdapter` wraps `getDisplayContext()` and
   exposes exactly what the reusable library needs: `getSceneManager()`,
   `lockRender()/unlockRender()/queueRender()`, and (new in M4) `getViewCamera()`
   = `getViewManager()->getCurrent()->getCamera()`.
2. **Render target (library, Ogre-only).** `qar_ogre_streaming::OgreStereoRenderTarget`
   owns, for one eye: a color RTT (`Ogre::PF_R8G8B8A8`, `TU_RENDERTARGET`) and a
   metric depth RTT (`Ogre::PF_FLOAT32_R`), each with its own viewport on a shared
   offscreen `Ogre::Camera`. Both render targets are `setAutoUpdated(false)` so
   RViz's own render loop never drives them — the plugin updates them explicitly.
3. **Clone the live view each tick.** `cloneCameraFrom(src)` copies the RViz view
   camera's **world** pose (`getDerivedPosition/Orientation`) and projection
   (`getProjectionType`, `getFOVy`/ortho window, near/far) onto the offscreen
   camera, overriding only the aspect ratio to match the RTT. Pose matches the
   main viewport exactly.
4. **Render + readback on the render thread, under lock.** The panel's capture
   runs on the Qt GUI thread (== the RViz render thread; asserted via the
   `RVizRenderThreadExecutor::is_render_thread()`), inside
   `lockRender()`/`unlockRender()`: `render()` calls `RenderTexture::update(false)`
   for color then depth; then `readColorRgba()` / `readDepthMetric()` blit via
   `HardwarePixelBuffer::blitToMemory` into CPU buffers. PNG encoding (stb) happens
   after the lock is released.

## Depth: the C3 re-check and the shader-scheme implementation

The full re-check is in `docs/api_gaps.md` in the `qar_ogre_adapter` repository. Summary: **Ogre 1.12 GL3+ does not
expose a metric, sampleable depth texture through its public API** (the color
RTT's depth attachment is a pooled renderbuffer, not a readable `Ogre::Texture`;
a `PF_DEPTH*` RTT holds non-linear window-space depth and isn't portably
readable). Per contractor answer C3, depth is therefore a **shader-written
`PF_FLOAT32_R` RTT holding linear eye-space distance in metres**:

- A `MaterialManager::Listener` supplies, for the `"qar_linear_depth"` viewport
  material scheme, one technique for **every** renderable, whose GLSL 1.50
  fragment shader outputs `-(worldView * vertex).z` (metres; the camera looks down
  its local −Z). Because the substitution is scheme-wide, a plain second render of
  the scene through that scheme yields depth for the whole RViz scene (grid,
  markers, TF, point cloud all captured correctly).
- The produced/transmitted depth format is `QAR_PIXEL_FORMAT_R32_FLOAT`, tagged
  with the `QAR_VIDEO_FRAME_VIEW_TYPE_DEPTH` view semantic (not the hardware
  `QAR_PIXEL_FORMAT_D32_FLOAT`).

## Which displays render correctly

| Display | Offscreen result |
|---|---|
| Grid (`rviz_default_plugins/Grid`) | ✅ color + depth |
| TF (`rviz_default_plugins/TF`) — axes, arrows, frame labels | ✅ color + depth |
| Marker / MarkerArray — cube/sphere/cylinder/line-strip, coloured + lit | ✅ color + depth |
| PointCloud2 — RGB, "Boxes" style (geometry-shader points) | ✅ color + depth |
| RobotModel | ❌ crashes RViz on URDF load — see below |

### RobotModel (scene 4) — known distro limitation, NOT an offscreen-path issue

`rviz2` crashes with `STATUS_STACK_BUFFER_OVERRUN` (0xC0000409) **when the
RobotModel display parses the URDF** (`/robot_description`). This was isolated:
- Scene 4 with the RobotModel display but **no** `/robot_description` published →
  runs clean (auto-dump `overall_pass=true`, exit 0).
- Scene 4 **with** the URDF published → crash, in **both** auto-capture and
  plain auto-dump mode.

Because it reproduces in auto-dump mode (which creates **no** offscreen render
target and calls no M4 code), the crash is inside RViz's RobotModel/urdfdom/assimp
loading on this Windows binary distro (M0 rebuilt several of those vendor DLLs) —
independent of M4's offscreen rendering. The scene config
(`scene4_grid_robot.rviz`) and publisher (`publish_test_robot.py`) are provided;
capturing it is blocked on that distro crash, not on the offscreen path. (Follow-up
belongs with the distro packaging work, not M4.)

## Threading / lock discipline

All Ogre/GL work (texture/camera/viewport creation, `RenderTarget::update`,
readback) runs on the Qt render thread, guarded by
`lockRender()`/`unlockRender()`. The capture entry points (panel button, the
`QAR_PANEL_AUTOCAPTURE` timer) are already on that thread; `captureOffscreenToPrefix`
refuses to run off it. This matches the batched render-tick discipline the
scheduler will enforce in M6.

## GL3+ quirks encountered

- **Depth as texture** — the central C3 finding above (no metric sampleable depth
  → shader scheme).
- **Delay-load in unit tests** — once the library emits real Ogre symbols, the
  Ogre-free unit-test executable had to `/DELAYLOAD:OgreMain.dll` (the tests never
  call Ogre), and one imported *data* symbol
  (`ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME`, which blocks delay-load)
  was replaced with its literal value `"General"`.
- **Readback byte order** — blitting into a `PF_BYTE_RGBA` `PixelBox` yields
  canonical R,G,B,A bytes on little-endian Windows, matching stb directly (marker
  colours verified correct).
- Ogre already ships a `Codec_STBI` plugin, but we vendor `stb_image_write.h`
  ourselves for full control (depth normalization, raw F32 dump).

## Capture workflow (reproduce)

```powershell
# Build the plugin (runs the M3 distro-gap fix scripts):
powershell -File scripts\build_plugin.ps1

# One scene (grid), scriptable auto-capture + quit:
powershell -File scripts\run_rviz_with_plugin.ps1 `
  -Config qar_rviz_streaming\config\test_scenes\scene1_grid.rviz `
  -AutoCapture build\m4_captures\scene1_grid -DelayMs 8000

# All scenes with their publishers:
powershell -File scripts\capture_test_scenes.ps1 -Scene all
```

Each capture writes `<prefix>_color.png`, `<prefix>_depth.png` (8-bit
visualization), `<prefix>_depth.f32` (raw metric float32), and `<prefix>_capture.txt`
(stats + `overall_capture_pass`). Interactively, the panel's **"Capture offscreen
frame"** button does the same.

## Lib unit tests

`OgreStereoRenderTarget`'s real create/render/readback needs a GL context, so it
is validated by the in-RViz captures above rather than headless. The Ogre-free
part (the C3 depth-format decision, `depth_output_qar_format`) is covered by
`render_target_support_tests.cpp`. Full suite: **41/41 green**, include-checker
green, from a plain shell (no distro env).
