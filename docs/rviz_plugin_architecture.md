# qar_rviz_streaming — RViz2 Plugin Architecture (M3 draft)

`qar_rviz_streaming` is the RViz2 packaging + UI layer for the Qar streaming
integration. It is a thin **consumer** of the reusable `qar_ogre_streaming`
library: all rendering/streaming logic lives in the
library; this package only adapts RViz objects into the library's generic
interfaces and provides the Qt panel UI.

> Status: **M3 skeleton.** The panel loads in a live RViz2, obtains the
> DisplayContext + Ogre::SceneManager, wires the reusable library, and drives a
> MOCK runtime/peer state machine. No offscreen rendering yet (that is M4). There
> is **no authorization UI** (contractor answer C6) — a peer id only identifies /
> routes a stream.

## Dependency direction

```
qar_rviz_streaming (ament, this package; MAY use rviz/Qt/ament)
        |  consumes via add_subdirectory + link
        v
qar_ogre_streaming (plain-CMake, reusable; MUST NOT use rviz/Qt/ament)
        |
        v
Qar C API / Ogre
```

The `check_no_rviz_includes` CTest guards the library boundary; it scans only
`qar_ogre_streaming/**` and `examples/**`, **not** this package (verified: it
reports "scanned N file(s) under qar_ogre_streaming, examples").

## Components

| Class (`qar_rviz_streaming::`) | File | Responsibility |
|---|---|---|
| `QarStreamingPanel` | `qar_streaming_panel.{hpp,cpp}` | `rviz_common::Panel` (a QWidget) exported via pluginlib. Minimum UI: Start/Stop Qar, Add/Remove mock peer, per-peer Start/Stop stream, Resolution + FPS selectors, diagnostics text area, last-error line, context/scene status. Owns the `OgreQarStreamingManager` and drives the mock runtime. Also implements `OgreQarDiagnosticsSink` so library diagnostics land in the on-screen log. |
| `RVizDisplayContextAdapter` | `rviz_display_context_adapter.{hpp,cpp}` | Wraps `getDisplayContext()`. Exposes `Ogre::SceneManager*`, `lockRender()/unlockRender()/queueRender()`, validates non-null, and reports scene identity (name, camera count, root-node child count, frame count). The single RViz-specific seam that keeps `qar_ogre_streaming` RViz-agnostic. |
| `RVizRenderThreadExecutor` | `rviz_render_thread_executor.{hpp,cpp}` | `IRenderThreadExecutor` on the Qt main/render thread. `post_to_render_thread()` marshals work via `QMetaObject::invokeMethod(qApp, …, Qt::QueuedConnection)`; `is_render_thread()` is `QThread::currentThread() == qApp->thread()`. Ensures Qar callbacks can only *enqueue* render work, never render. |
| `RvizPeerPanelModel` | `rviz_peer_panel_model.{hpp,cpp}` | Qt-free model of the mock peer list (id, label, resolution, fps, streaming flag). Generates deterministic non-zero `QarPeerId`s so a peer can be looked up in the library's stream map. |

## How the panel wires the reusable library

On `onInitialize()` (called by RViz once the DisplayContext exists):

```
adapter_  = RVizDisplayContextAdapter(getDisplayContext())
executor_ = RVizRenderThreadExecutor()               // captures qApp thread
factory_  = <mock IQarRenderSenderFactory>           // real Qar path arrives in M10
if adapter_.isValid():
    OgreQarStreamingConfig cfg { adapter_.getSceneManager(), executor_, factory_, this(sink) }
    manager_ = OgreQarStreamingManager(cfg)
```

The panel maps UI actions onto the library:

| UI action | Library call |
|---|---|
| Start / Stop Qar | `manager_->set_streaming_active(true/false)` |
| Add mock peer | `RvizPeerPanelModel::addMockPeer()` (model only) |
| Start stream (per peer) | `manager_->create_stream(StereoStreamConfig)` + `get_stream(id)->set_active(true)` |
| Stop stream (per peer) | `get_stream(id)->set_active(false)` |
| Remove peer | `manager_->destroy_stream(id)` + model removal |

Note: in the M1 skeleton `create_stream` / the stereo-stream constructor make **no
Ogre calls** (real render targets are M4), so exercising them against the live
RViz `Ogre::SceneManager` is safe today and genuinely proves the lib↔plugin
boundary (executor, scene manager, sender factory, stream lifecycle).

## Threading model

All Ogre/OpenGL work must run on the Qt GUI/render thread.
`RVizRenderThreadExecutor` is the enforcement point: the library's scheduler calls
`is_render_thread()` and refuses to render off-thread, and any deferred work is
posted back to the GUI thread via `post_to_render_thread()`. Library diagnostics
(`on_message`) may arrive on any thread and are marshalled to the GUI thread
before touching the QPlainTextEdit.

## pluginlib registration

* `plugin_description.xml` declares `qar_rviz_streaming/QarStreamingPanel` with
  `base_class_type="rviz_common::Panel"`.
* `CMakeLists.txt` calls `pluginlib_export_plugin_description_file(rviz_common plugin_description.xml)`,
  which installs the ament resource-index marker
  `share/ament_index/resource_index/rviz_common__pluginlib__plugin/qar_rviz_streaming`.
* `qar_streaming_panel.cpp` ends with
  `PLUGINLIB_EXPORT_CLASS(qar_rviz_streaming::QarStreamingPanel, rviz_common::Panel)`.
* `Q_OBJECT` + Qt AUTOMOC provide the meta-object. `Q_PLUGIN_METADATA` is **not**
  used — pluginlib (not the Qt plugin system) loads rviz panels.
* Because the reusable library names a method `OgreQarStreamingManager::emit()`
  (an ordinary identifier), the plugin is compiled with `QT_NO_KEYWORDS` and uses
  the `Q_SLOTS`/`Q_SIGNALS`/`Q_EMIT` forms (as do the rviz_common public headers).

## Non-interactive verification (auto-dump)

Set env `QAR_PANEL_AUTODUMP=<file>` (and optional `QAR_PANEL_AUTODUMP_DELAY_MS`,
default 4000). A `QTimer` single-shot then: gathers context/scene diagnostics,
runs a mock peer add → start stream → stop stream → remove round-trip through the
real `OgreQarStreamingManager`, writes a `key=value` report to `<file>`, and calls
`QCoreApplication::quit()` so RViz exits cleanly. `scripts/run_rviz_with_plugin.ps1
-AutoDump <file>` drives this and prints PASS/FAIL from `overall_pass`.

Representative passing dump (live RViz2, OpenGL 4.6):

```
context_valid=true
scene_manager_valid=true
scene_manager_name=SceneManagerInstance1
camera_count=4
scene_node_children=5
rviz_frame_count=115
render_thread_executor_ready=true
on_render_thread=true
library_manager_ready=true
mock_peer_added=true
stream_started=true          stream_count_after_start=1
stream_stopped=true
mock_peer_removed=true       stream_count_after_remove=0
overall_pass=true
```

## What M4 adds (offscreen render)

M4 will use `RVizDisplayContextAdapter::lockRender()/unlockRender()` around
`Ogre::RenderTexture` updates driven on this same Qt render thread, cloning the
active RViz view camera into offscreen color+depth targets. The adapter and
executor built here are the seams that make that possible without further RViz
coupling.
