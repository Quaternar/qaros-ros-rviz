# Testing

## There are no automated tests in this repository

That is a deliberate, and honest, statement of where the coverage is — not an
oversight to be papered over.

Everything this package does is bound to things a test process does not have: a
running `rviz2.exe`, a live Ogre scene with a real GL context, a pluginlib-loaded
Qt panel, and a QarOS runtime with a Visualizer on the other end. A unit test of
`QarStreamingPanel` would be a test of mocks.

The coverage that *does* exist lives one layer down, in the `qar_ogre_adapter`
repository: **108 automated tests** over the streaming library and the geodetic
maths, plus a dependency-direction guard. Everything in this repository that was
worth testing was moved there for exactly that reason — see the note on
`geo_anchor.{hpp,cpp}` in that repository's `docs/testing.md`. **If you add panel
logic worth testing, extract it into a free function over plain data and put it
there**, rather than adding a mock-heavy test here.

What follows is therefore the *manual* verification procedure. It is scripted where
it can be, so "manual" mostly means "you have to look at the result".

## 1. The build itself

```powershell
.\scripts\build_plugin.ps1 -Clean
```

Passes when it prints `=== BUILD OK ===` and a path to `qar_rviz_streaming.dll`.
This is a real check, not a formality: the plugin links two packages from a
separate repository through `find_package()`, so a successful build proves that
wiring is intact.

## 2. Scripted in-RViz captures (no QarOS needed)

`scripts/run_rviz_with_plugin.ps1` launches real `rviz2.exe` with the panel loaded
and drives it non-interactively, then exits with a pass/fail code and prints a
`VERIFICATION: PASS` / `FAIL` line. Each mode writes its artefacts to the path you
give it.

| Mode | What it proves |
|---|---|
| `-AutoDump <file>` | The panel loads under pluginlib, gets a valid `DisplayContext` and a real `Ogre::SceneManager`, and completes a mock peer add → stream → remove round trip. |
| `-AutoCapture <dir>` | The live RViz scene renders into an offscreen `Ogre::RenderTexture` and reads back as a PNG with real content. |
| `-AutoStereo <dir>` | Per-eye stereo targets render with the correct inter-pupillary offset. Use `-IpdMetres` / `-StereoFrames` to vary it. |
| `-AutoMulti <dir>` | Several simultaneous peer streams render and submit. `-MultiPeers` / `-MultiTicks`. |
| `-GlSpike <file>` | Native GL texture-id extraction from an Ogre render target, validated against an independent `glGetTexImage` readback. |

Example:

```powershell
.\scripts\run_rviz_with_plugin.ps1 -AutoDump .\dump.json
.\scripts\run_rviz_with_plugin.ps1 -AutoCapture .\out\capture
```

These open a real RViz window. They are not headless.

## 3. Test scenes

`scripts/capture_test_scenes.ps1` walks the five configs in
`qar_rviz_streaming/config/test_scenes/` — grid, grid+TF, grid+marker, grid+robot,
grid+point cloud — capturing each. Use it after any change to the render path to
see that all five still come out.

The scenes can also just be opened by hand:

```powershell
rviz2.exe -d qar_rviz_streaming\config\test_scenes\scene5_grid_pointcloud.rviz
```

Publishers for the synthetic data are in `scripts/publish_test_*.py`.

## 4. Rosbag playback

See [`rosbag_metadata.md`](rosbag_metadata.md) for the full detail. The short
version:

```powershell
.\scripts\play_demo_rosbag.ps1 -BagDir <bag directory>
```

**The trap to know about.** Sensor publishers are conventionally BEST_EFFORT, and a
RELIABLE subscriber receives **nothing** from one — with no error surfaced anywhere
except a terse player/DDS warning. A point-cloud display that renders nothing while
`ros2 bag info` clearly shows the messages is almost always this. The script passes
`scripts/rosbag_qos_override.yaml` for that reason; keep using it.

Two more things that make the difference between an empty view and a correct one:

- **Fixed Frame.** Set it to a frame the bag actually publishes a TF for
  (`arco/base_link` for the ARCO bags). Leaving it at `map` when the bag has no
  `map` frame renders nothing and reports only a transform warning.
- **Point size and decay.** A 32768-point Ouster frame at the default 0.01 m point
  size is a handful of near-invisible pixels. Raise it.

## 5. Live QarOS round trip

```powershell
.\scripts\run_live_rviz_stream_real.ps1
```

Opens RViz *and* the QarOS Visualizer. Then, in the panel:

1. **Connect to QarOS** — the state label goes `CONNECTING...` (orange) then
   `RUNNING (live)`. Connecting is asynchronous on purpose: it takes ~3 s, and doing
   it on the Qt thread froze RViz.
2. **Invite peer** — a Visualizer window opens and starts showing the RViz scene.
   Invite again for a second, independent instance.
3. Move the RViz camera and confirm the Visualizer tracks it.

The panel's heartbeat line, which goes to the on-screen log and to
`rviz_live_*.err.log`, is the thing to read:

```
HB dt=2.0s ticks=120 tick_fps=60.0 max_gap=18ms streams=1 batch_avg=1.2ms budget_skips=0 | 2e6ee7d8 fps=59.8 sub=120 skip=0 rend=1.1ms
```

`tick_fps` near 60 and `max_gap` low means the Qt thread is being served properly.
A per-peer `fps` around 60 means frames are actually reaching that peer.

Useful switches: `-Visualizers <n>` to auto-invite at connect (default 0),
`-InvitePeers hololens:<hostname>` for real devices, `-Scene <file>` to pick the
RViz config.

## 6. Geolocation

See [`geolocation.md`](geolocation.md), which includes a snippet for publishing a
synthetic `NavSatFix`. Success is the line
`GEO: app volume anchored at lat=... lon=... alt=...` in the panel log.

Note that this path has **never been exercised against a real GPS receiver** — no
supplied rosbag contains a GPS topic. The maths under it is unit tested; the wiring
is not.
