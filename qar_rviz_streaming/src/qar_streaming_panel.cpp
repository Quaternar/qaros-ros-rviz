// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/qar_streaming_panel.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

#include <QByteArray>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

#include <pluginlib/class_list_macros.hpp>

#include "qar_ogre_streaming/ogre_gl_texture_spike.hpp"
#include "qar_ogre_streaming/ogre_opengl_texture_bridge.hpp"  // GlFenceHook (M7)
#include "qar_ogre_streaming/ogre_qar_stereo_stream.hpp"  // complete type for set_active()

#include "qar_rviz_streaming/mock_sender_factory.hpp"
#include "qar_rviz_streaming/qar_offscreen_capture.hpp"

// Live streaming: the QarOS-C-API sender factory + the geodetic anchor helpers.
#include "qar_qar_adapter/geo_anchor.hpp"
#include "qar_qar_adapter/qar_capi_render_sender_adapter.hpp"
// getDisplayContext() is declared by rviz_common::Panel, but reaching
// getRosNodeAbstraction() through it needs the complete DisplayContext type.
#include "rviz_common/display_context.hpp"
#include "rviz_common/ros_integration/ros_node_abstraction_iface.hpp"

#include "qar_rviz_streaming/rviz_display_context_adapter.hpp"
#include "qar_rviz_streaming/rviz_render_lock.hpp"
#include "qar_rviz_streaming/rviz_render_thread_executor.hpp"

namespace qar_rviz_streaming
{

QarStreamingPanel::QarStreamingPanel(QWidget * parent)
: rviz_common::Panel(parent)
{
  buildUi();
  appendLog("QarStreamingPanel constructed (M3 skeleton; mock runtime).");
}

QarStreamingPanel::~QarStreamingPanel()
{
  // Stop the render tick before the manager/adapter members are torn down so a
  // queued timeout can never call into a half-destroyed stream / DisplayContext.
  if (render_timer_ != nullptr) {
    render_timer_->stop();
    disconnect(render_timer_, nullptr, this, nullptr);
  }
  // Join a connect still in flight. Closing RViz mid-connect can cost up to the
  // ~3 s the session build takes, which is far better than unloading this DLL out
  // from under a running thread. PendingConnect is held by shared_ptr, so the
  // worker's own writes stay valid even though the panel is going away.
  if (connect_thread_.joinable()) {
    connect_thread_.join();
  }
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
void QarStreamingPanel::buildUi()
{
  auto * root = new QVBoxLayout(this);

  // --- Qar runtime (mock) --------------------------------------------------
  auto * runtime_group = new QGroupBox("Qar runtime (mock)", this);
  auto * runtime_layout = new QVBoxLayout(runtime_group);
  runtime_state_label_ = new QLabel("Qar runtime: STOPPED", runtime_group);
  auto * runtime_buttons = new QHBoxLayout();
  start_qar_button_ = new QPushButton("Start Qar", runtime_group);
  stop_qar_button_ = new QPushButton("Stop Qar", runtime_group);
  runtime_buttons->addWidget(start_qar_button_);
  runtime_buttons->addWidget(stop_qar_button_);
  runtime_layout->addWidget(runtime_state_label_);
  runtime_layout->addLayout(runtime_buttons);
  root->addWidget(runtime_group);

  // --- Stream settings -----------------------------------------------------
  auto * settings_group = new QGroupBox("Stream settings", this);
  auto * settings_layout = new QFormLayout(settings_group);
  resolution_combo_ = new QComboBox(settings_group);
  resolution_combo_->addItem("640 x 480", QSize(640, 480));
  resolution_combo_->addItem("1024 x 1024", QSize(1024, 1024));
  resolution_combo_->addItem("1280 x 720", QSize(1280, 720));
  resolution_combo_->addItem("1600 x 900", QSize(1600, 900));
  resolution_combo_->addItem("1920 x 1080", QSize(1920, 1080));
  resolution_combo_->addItem("2048 x 2048", QSize(2048, 2048));
  resolution_combo_->setCurrentIndex(2);  // 1280 x 720
  // FPS options MUST include 30 and 60 (contractor answer C8).
  fps_combo_ = new QComboBox(settings_group);
  for (const int fps : {15, 30, 60, 72, 90}) {
    fps_combo_->addItem(QString("%1 fps").arg(fps), fps);
  }
  fps_combo_->setCurrentIndex(2);  // 60 fps (optimum target per C8)
  settings_layout->addRow("Resolution:", resolution_combo_);
  settings_layout->addRow("FPS:", fps_combo_);
  root->addWidget(settings_group);

  // --- Peers (mock) --------------------------------------------------------
  auto * peers_group = new QGroupBox("Peers (mock)", this);
  auto * peers_layout = new QVBoxLayout(peers_group);
  // Device selector for LIVE mode: which kind of real peer the add button invites.
  // Hidden in mock mode (onInitialize shows it), where "add" means a mock peer.
  auto * invite_row = new QHBoxLayout();
  peer_kind_combo_ = new QComboBox(peers_group);
  peer_kind_combo_->addItem("Visualizer", "visualizer");
  peer_kind_combo_->addItem("HoloLens 2", "hololens");
  peer_kind_combo_->addItem("Meta Quest", "quest");
  peer_kind_combo_->addItem("ZED camera", "zed");
  peer_kind_combo_->setToolTip(
    "Device type to invite into the running QarOS session. Each invited peer gets its "
    "own stereo stream of the RViz scene.");
  peer_hostname_edit_ = new QLineEdit(peers_group);
  peer_hostname_edit_->setPlaceholderText("HoloLens hostname (e.g. HOLOLENS-A2B3)");
  peer_hostname_edit_->setToolTip(
    "Required for HoloLens 2 — it is the only device type the SDK can address by name. "
    "Ignored for the other kinds.");
  invite_row->addWidget(peer_kind_combo_);
  invite_row->addWidget(peer_hostname_edit_, 1);
  peer_kind_combo_->hide();
  peer_hostname_edit_->hide();

  auto * peer_buttons = new QHBoxLayout();
  add_peer_button_ = new QPushButton("Add mock peer", peers_group);
  remove_peer_button_ = new QPushButton("Remove peer", peers_group);
  peer_buttons->addWidget(add_peer_button_);
  peer_buttons->addWidget(remove_peer_button_);
  peers_layout->addLayout(invite_row);
  peer_list_ = new QListWidget(peers_group);
  auto * stream_buttons = new QHBoxLayout();
  start_stream_button_ = new QPushButton("Start stream", peers_group);
  stop_stream_button_ = new QPushButton("Stop stream", peers_group);
  stream_buttons->addWidget(start_stream_button_);
  stream_buttons->addWidget(stop_stream_button_);
  peers_layout->addLayout(peer_buttons);
  peers_layout->addWidget(peer_list_);
  peers_layout->addLayout(stream_buttons);
  root->addWidget(peers_group);

  // --- Diagnostics ---------------------------------------------------------
  auto * diag_group = new QGroupBox("Diagnostics", this);
  auto * diag_layout = new QVBoxLayout(diag_group);
  context_status_label_ = new QLabel("DisplayContext: (not initialized)", diag_group);
  context_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto * diag_buttons = new QHBoxLayout();
  refresh_button_ = new QPushButton("Refresh diagnostics", diag_group);
  capture_button_ = new QPushButton("Capture offscreen frame", diag_group);
  stereo_capture_button_ = new QPushButton("Capture stereo pair", diag_group);
  diag_buttons->addWidget(refresh_button_);
  diag_buttons->addWidget(capture_button_);
  diag_buttons->addWidget(stereo_capture_button_);
  diagnostics_view_ = new QPlainTextEdit(diag_group);
  diagnostics_view_->setReadOnly(true);
  diagnostics_view_->setMaximumBlockCount(2000);
  last_error_label_ = new QLabel("Last error: <none>", diag_group);
  last_error_label_->setWordWrap(true);
  diag_layout->addWidget(context_status_label_);
  diag_layout->addLayout(diag_buttons);
  diag_layout->addWidget(diagnostics_view_);
  diag_layout->addWidget(last_error_label_);
  root->addWidget(diag_group);

  root->addStretch(1);

  // --- Signal/slot wiring --------------------------------------------------
  connect(start_qar_button_, &QPushButton::clicked, this, &QarStreamingPanel::onStartQar);
  connect(stop_qar_button_, &QPushButton::clicked, this, &QarStreamingPanel::onStopQar);
  connect(add_peer_button_, &QPushButton::clicked, this, &QarStreamingPanel::onAddPeer);
  connect(remove_peer_button_, &QPushButton::clicked, this, &QarStreamingPanel::onRemovePeer);
  connect(start_stream_button_, &QPushButton::clicked, this, &QarStreamingPanel::onStartStream);
  connect(stop_stream_button_, &QPushButton::clicked, this, &QarStreamingPanel::onStopStream);
  connect(refresh_button_, &QPushButton::clicked, this, &QarStreamingPanel::onRefreshDiagnostics);
  connect(
    capture_button_, &QPushButton::clicked, this, &QarStreamingPanel::onCaptureOffscreenFrame);
  connect(
    stereo_capture_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onCaptureStereoPair);
}

// ---------------------------------------------------------------------------
// RViz lifecycle
// ---------------------------------------------------------------------------
void QarStreamingPanel::onInitialize()
{
  appendLog("QarStreamingPanel::onInitialize() — obtaining DisplayContext.");

  adapter_ = std::make_unique<RVizDisplayContextAdapter>(getDisplayContext());
  executor_ = std::make_shared<RVizRenderThreadExecutor>();
  render_lock_ = std::make_shared<RVizRenderLock>(adapter_.get());
  frame_source_ = std::make_shared<qar_ogre_streaming::MockQarFrameInfoSource>();
  mock_factory_ = std::make_shared<AnimatedMockSenderFactory>();
  sender_factory_ = mock_factory_;
  rebuildFrameSource();  // pushes the base config into mock_factory_

  // LIVE streaming to QarOS. When QAR_PANEL_LIVE_STREAM is set, swap in the
  // QarOS-C-API sender factory. The peers, and their head poses, then come FROM
  // the QarOS runtime; the panel renders the live RViz scene per peer and submits
  // through the QAR C API. The animated mock path is untouched.
  live_stream_mode_ = !qEnvironmentVariable("QAR_PANEL_LIVE_STREAM").isEmpty();
  if (live_stream_mode_) {
    // M10: this makes the panel LIVE-CAPABLE but does NOT connect. Capture the QAR
    // session config now; the real connection (library_init -> runtime_create ->
    // create_session -> join -> subscribe -> invite_peer) is built lazily on the
    // "Connect to QarOS" button (onStartQar). Connecting during onInitialize froze
    // RViz — it ran the whole zenoh/peer handshake on the GUI thread before the Qt
    // event loop was live, so the runtime's async peer coordination could never
    // progress. Deferring to a user click (event loop running) is both correct UX
    // and lets that coordination actually complete. Until connect, the manager uses
    // the mock factory and driveLiveStreaming() is a no-op (qar_factory_ == null).
    // Finding 11: tell the QAR runtime which folder to spawn app binaries (the
    // Visualizer) from. Honour QAR_RUNTIME_BINARIES_DIR if set, else leave empty
    // (the runtime falls back to cwd with a warning). In real-SDK mode this should
    // point at <repo>/QarOS_binary/install/bin (see run_live_rviz_stream_real.ps1).
    const QString rt_bin = qEnvironmentVariable("QAR_RUNTIME_BINARIES_DIR");
    qar_runtime_binaries_dir_ = rt_bin.isEmpty() ? std::string() : rt_bin.toStdString();
    appendLog(
      "LIVE mode: panel is live-capable. Click 'Connect to QarOS' to "
      "open the real session — deferred, NOT connected at load.");
    // Relabel the runtime controls so the live connection is discoverable (buildUi
    // runs before onInitialize, so live_stream_mode_ isn't known there).
    if (start_qar_button_ != nullptr) { start_qar_button_->setText("Connect to QarOS"); }
    if (stop_qar_button_ != nullptr) { stop_qar_button_->setText("Disconnect (pause)"); }
    // Live mode has no mock peers: "add a peer" means "add a physical client head",
    // i.e. invite a real device into the running session. Reveal the device selector.
    if (add_peer_button_ != nullptr) { add_peer_button_->setText("Invite peer"); }
    if (peer_kind_combo_ != nullptr) { peer_kind_combo_->show(); }
    if (peer_hostname_edit_ != nullptr) { peer_hostname_edit_->show(); }
  }

  if (adapter_->isValid()) {
    try {
      qar_ogre_streaming::OgreQarStreamingConfig cfg;
      cfg.scene_manager = adapter_->getSceneManager();
      cfg.render_thread_executor = executor_;
      cfg.qar_sender_factory = sender_factory_;
      cfg.diagnostics = this;
      // M10-live: RViz's only light is a DIRECTIONAL "MainDirectional" that
      // VisualizationManager re-aims at the LOCAL view camera every frame — a
      // headlight. Shared with remote peers that is wrong: their shading pivots
      // around whenever the RViz operator orbits. Add a world-fixed omni light (and
      // optionally one per peer head) while streaming. Applied on set_streaming_active
      // and fully restored on stop; OFF by default outside live mode so the M4-M6
      // scripted capture proofs keep the lighting they were recorded with.
      const qar_ogre_streaming::SceneLightingConfig light_cfg = buildSceneLightingConfig();
      scene_lighting_ = std::make_shared<qar_ogre_streaming::QarSceneLighting>(
        adapter_->getSceneManager(), light_cfg);
      cfg.scene_lighting = scene_lighting_;
      appendLog(
        QString("Scene lighting: mode=%1, RViz headlight=%2")
        .arg(qar_ogre_streaming::to_string(light_cfg.mode))
        .arg(light_cfg.suppress_light_name.empty() ? "kept" : "suppressed while streaming"));
      // C8: target 60 FPS/peer, hard floor 30, ~16 ms/tick batch time budget,
      // designed for up to 8 peers. A single batch render lock (via render_lock_)
      // wraps all peers so they share one coherent scene snapshot (§12.2 / C2).
      cfg.render_lock = render_lock_;
      cfg.render_budget.max_batch_milliseconds = 16.0;
      cfg.render_budget.floor_fps = 30;
      // M7: install the real GL fence seam. After each stream's targets render (in
      // the batch, inside lockRender), a glFenceSync is inserted and carried on the
      // submitted QarVideoFrameOpenGl so the Qar consumer can wait before sampling.
      cfg.fence_hook = std::make_shared<qar_ogre_streaming::GlFenceHook>();
      manager_ = std::make_unique<qar_ogre_streaming::OgreQarStreamingManager>(cfg);
      appendLog("Reusable qar_ogre_streaming manager constructed and wired to the RViz scene.");
      clearLastError();
      // M10: do NOT auto-start streaming in live mode. Streaming begins when the
      // user connects (onStartQar builds qar_factory_ and calls set_streaming_active).
    } catch (const std::exception & e) {
      setLastError(QString("Library manager construction failed: %1").arg(e.what()));
    }
  } else {
    setLastError("DisplayContext or Ogre::SceneManager unavailable at onInitialize().");
  }

  refreshContextStatus();
  updateRuntimeStateLabel();

  // Periodic render tick (~30 FPS) on the Qt/render thread. It renders active
  // streaming streams each tick (M5) and is a no-op when nothing is streaming, so
  // it does not interfere with the M3 auto-dump / M4 auto-capture flows.
  render_timer_ = new QTimer(this);
  render_timer_->setInterval(16);  // ~60 FPS target (C8); scheduler paces per-peer
  // Qt picks CoarseTimer by default, which may drift 5% and — on Windows — is served
  // off the ~15.6 ms system tick. A 16 ms coarse interval therefore lands on the NEXT
  // tick more often than not, i.e. ~31 ms, pinning us near 32 FPS however fast the
  // render actually is. PreciseTimer uses the multimedia timer and removes that
  // quantisation; it costs a slightly higher wakeup rate, which is what we want here.
  render_timer_->setTimerType(Qt::PreciseTimer);
  connect(render_timer_, &QTimer::timeout, this, &QarStreamingPanel::onRenderTick);
  render_timer_->start();

  // Geolocation source. Subscribing costs nothing when the topic is absent, so
  // this is armed unconditionally and simply never fires without a GPS.
  geo_enabled_ =
    qEnvironmentVariable("QAR_PANEL_GEO").trimmed().toLower() != "off";
  gps_topic_ = qEnvironmentVariable("QAR_PANEL_GPS_TOPIC").trimmed().toStdString();
  if (gps_topic_.empty()) { gps_topic_ = "/gps/fix"; }
  setupGeoSubscription();

  // Heartbeat cadence for the live debug loops; QAR_PANEL_HEARTBEAT_S=0 turns it
  // off. Left ON by default: it is one line every 2 s and it is the only record
  // that survives a run, since the panel's log widget does not.
  {
    const QString hb = qEnvironmentVariable("QAR_PANEL_HEARTBEAT_S");
    if (!hb.isEmpty()) {
      bool hb_ok = false;
      const double hb_v = hb.toDouble(&hb_ok);
      if (hb_ok && hb_v >= 0.0) {
        heartbeat_period_s_ = hb_v;
      }
    }
  }

  // Non-interactive verification hook (proven scriptable in M0b): dump + quit.
  const QString dump_path = qEnvironmentVariable("QAR_PANEL_AUTODUMP");
  if (!dump_path.isEmpty()) {
    bool ok = false;
    int delay_ms = qEnvironmentVariable("QAR_PANEL_AUTODUMP_DELAY_MS").toInt(&ok);
    if (!ok || delay_ms <= 0) {
      delay_ms = 4000;
    }
    appendLog(
      QString("AUTODUMP mode active: dump to '%1' in %2 ms, then quit RViz.")
      .arg(dump_path).arg(delay_ms));
    QTimer::singleShot(delay_ms, this, &QarStreamingPanel::runAutoDump);
  }

  // Non-interactive offscreen-capture hook (M4 Gate A): render the live scene
  // into the offscreen color+depth targets, write PNGs, then quit RViz.
  const QString capture_prefix = qEnvironmentVariable("QAR_PANEL_AUTOCAPTURE");
  if (!capture_prefix.isEmpty()) {
    bool ok = false;
    int delay_ms = qEnvironmentVariable("QAR_PANEL_AUTOCAPTURE_DELAY_MS").toInt(&ok);
    if (!ok || delay_ms <= 0) {
      delay_ms = 6000;  // give RViz time to render the scene (TF/markers to arrive)
    }
    appendLog(
      QString("AUTOCAPTURE mode active: capture to '%1*' in %2 ms, then quit RViz.")
      .arg(capture_prefix).arg(delay_ms));
    QTimer::singleShot(delay_ms, this, &QarStreamingPanel::runAutoCapture);
  }

  // Non-interactive single-peer stereo capture hook (M5): create a mock peer +
  // stereo stream, render an animated sequence, capture left/right color+depth
  // PNG pairs at two poses, write metadata + parallax stats, then quit RViz.
  const QString stereo_prefix = qEnvironmentVariable("QAR_PANEL_AUTOSTEREO");
  if (!stereo_prefix.isEmpty()) {
    bool ok = false;
    int delay_ms = qEnvironmentVariable("QAR_PANEL_AUTOSTEREO_DELAY_MS").toInt(&ok);
    if (!ok || delay_ms <= 0) {
      delay_ms = 6000;  // give RViz time to render the scene (markers to arrive)
    }
    appendLog(
      QString("AUTOSTEREO mode active: stereo capture to '%1*' in %2 ms, then quit RViz.")
      .arg(stereo_prefix).arg(delay_ms));
    QTimer::singleShot(delay_ms, this, &QarStreamingPanel::runAutoStereo);
  }

  // Non-interactive MULTI-PEER scripted proof (M6): add K mock peers with
  // independent views, batch-render them for a few seconds via manager.render_tick(),
  // dump per-peer achieved FPS + per-batch timing + served/skipped, then quit.
  const QString multi_prefix = qEnvironmentVariable("QAR_PANEL_AUTOMULTI");
  if (!multi_prefix.isEmpty()) {
    bool ok = false;
    int delay_ms = qEnvironmentVariable("QAR_PANEL_AUTOMULTI_DELAY_MS").toInt(&ok);
    if (!ok || delay_ms <= 0) {
      delay_ms = 6000;  // let RViz render the scene (markers/TF to arrive)
    }
    appendLog(
      QString("AUTOMULTI mode active: multi-peer batch proof to '%1*' in %2 ms, then quit RViz.")
      .arg(multi_prefix).arg(delay_ms));
    QTimer::singleShot(delay_ms, this, &QarStreamingPanel::runAutoMultiPeer);
  }

  // Non-interactive OpenGL zero-copy handoff spike (M7 Gate B): render the live
  // scene offscreen, extract the native GL texture id, validate it, prove an
  // independent raw-GL readback matches Ogre's readback, exercise a GL fence, then
  // write the spike log line + quit. Runs in RViz's real GL context / render thread.
  const QString glspike_prefix = qEnvironmentVariable("QAR_PANEL_GLSPIKE");
  if (!glspike_prefix.isEmpty()) {
    bool ok = false;
    int delay_ms = qEnvironmentVariable("QAR_PANEL_GLSPIKE_DELAY_MS").toInt(&ok);
    if (!ok || delay_ms <= 0) {
      delay_ms = 6000;  // let RViz render the scene at least once first
    }
    appendLog(
      QString("GLSPIKE mode active: zero-copy handoff spike to '%1*' in %2 ms, then quit RViz.")
      .arg(glspike_prefix).arg(delay_ms));
    QTimer::singleShot(delay_ms, this, &QarStreamingPanel::runGlSpike);
  }
}

// ---------------------------------------------------------------------------
// OgreQarDiagnosticsSink
// ---------------------------------------------------------------------------
void QarStreamingPanel::on_message(
  qar_ogre_streaming::DiagnosticSeverity severity,
  const std::string & category,
  const std::string & message,
  const std::string & peer_id_hex)
{
  QString line = QString("[lib:%1:%2] %3")
    .arg(qar_ogre_streaming::to_string(severity))
    .arg(QString::fromStdString(category))
    .arg(QString::fromStdString(message));
  if (!peer_id_hex.empty()) {
    line += QString(" (peer %1)").arg(QString::fromStdString(peer_id_hex));
  }

  // The library may emit from the render thread; marshal to the GUI thread.
  if (QThread::currentThread() == this->thread()) {
    appendLog(line);
  } else {
    const QString captured = line;
    QMetaObject::invokeMethod(
      this, [this, captured]() { appendLog(captured); }, Qt::QueuedConnection);
  }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void QarStreamingPanel::onStartQar()
{
  // M10 LIVE: "Start Qar" == Connect to the real QarOS runtime and begin streaming.
  // The session is built HERE (on user click, event loop running), never at load.
  if (live_stream_mode_) {
    if (!manager_) {
      setLastError("Cannot connect: manager not initialized (no DisplayContext).");
      return;
    }
    if (live_connected_) {
      // Already connected; just (re)activate streaming.
      manager_->set_streaming_active(true);
      qar_running_ = true;
      updateRuntimeStateLabel();
      appendLog("QarOS: streaming resumed.");
      return;
    }
    // NOTE (vendor call 2026-07-29): we must NOT start qar-runtime-launcher.exe
    // ourselves. The QAR library spawns and configures the launcher itself from
    // `QarSessionCreateInit.launcher_config_json` + the runtime binaries folder. An
    // externally started launcher competes with it (that produced the earlier
    // peerSpec/consumer-id chaos), so the old QAR_PANEL_SPAWN_LAUNCHER hook is gone.
    if (pending_connect_) {
      appendLog("QarOS: connect already in progress — RViz stays live; please wait.");
      return;
    }
    if (!qar_factory_) {
      appendLog("Connecting to QarOS runtime (building session; RViz stays responsive)...");
      qar_qar_adapter::QarCApiSenderFactory::Config cfg;
      // near/far are BOTH the clip planes we render with and the range we advertise to
      // QAR every show_frame (QarRenderFrameShow.rendered_near_far). That second role
      // matters: the Visualizer colour-maps depth across the range we report, so an
      // over-wide far plane squashes the whole scene into the first per-cent of the
      // ramp and every object comes out the same colour (M10-live depth symptom,
      // 2026-08-02: content measured 0.14-4.99 m while we advertised 0.1-500 m).
      // Tunable per run for exactly that experiment. 0.01 m near keeps close-up
      // content out of the near plane. Far was 50 m for the phase-1 demo scene and
      // is now 150 m: a real LiDAR bag reaches much further (an Ouster OS1 sees
      // ~120 m), and everything past the far plane is simply clipped away — which on
      // 2026-08-22 removed most of a 32768-point frame before it ever reached the
      // Visualizer. Still far short of the 500 m that cost us depth precision under
      // the reciprocal NDC encoding.
      float near_m = 0.01f;
      float far_m = 150.0f;
      bool nf_ok = false;
      const QString near_s = qEnvironmentVariable("QAR_PANEL_NEAR_M").trimmed();
      if (!near_s.isEmpty()) {
        const float v = near_s.toFloat(&nf_ok);
        if (nf_ok && v > 0.0f) { near_m = v; }
      }
      const QString far_s = qEnvironmentVariable("QAR_PANEL_FAR_M").trimmed();
      if (!far_s.isEmpty()) {
        const float v = far_s.toFloat(&nf_ok);
        if (nf_ok && v > near_m) { far_m = v; }
      }
      cfg.near_far = QarNearFar{near_m, far_m};
      // depth_scale converts our metric metres into whatever the consumer expects.
      // "norm" is shorthand for 1/far, i.e. metres -> [0,1] across the frustum, which
      // is the encoding to try if every surface past 1 m renders the same colour.
      const QString ds = qEnvironmentVariable("QAR_PANEL_DEPTH_SCALE").trimmed().toLower();
      if (ds == "norm") {
        cfg.depth_scale = 1.0f / far_m;
      } else if (!ds.isEmpty()) {
        bool ok = false;
        const float v = ds.toFloat(&ok);
        if (ok && v > 0.0f) {
          cfg.depth_scale = v;
        } else {
          setLastError(QString("bad QAR_PANEL_DEPTH_SCALE '%1' (use a number or 'norm')")
            .arg(ds));
        }
      }
      appendLog(QString("LIVE: near/far = %1 / %2 m, depth_scale = %3")
        .arg(near_m).arg(far_m).arg(cfg.depth_scale));
      if (!qar_runtime_binaries_dir_.empty()) {
        cfg.runtime_binaries_folder_path = qar_runtime_binaries_dir_;
      }
      // Vendor guidance: the session GUID is always unique+non-zero (handled inside the
      // factory). The launcher config defaults to {"enableMixer": false} (direct
      // RViz -> Visualizer path for debugging); QAR_PANEL_LAUNCHER_CONFIG overrides it
      // (e.g. '{"enableMixer": true}' once the direct path is verified).
      const QString lcfg = qEnvironmentVariable("QAR_PANEL_LAUNCHER_CONFIG");
      if (!lcfg.isEmpty()) { cfg.launcher_config_json = lcfg.toStdString(); }
      // QAR library log folder (TRACE severity is the factory default) — vendor asks
      // that full library logs accompany bug reports. Empty => %TEMP%/quaternar.
      const QString qlog = qEnvironmentVariable("QAR_PANEL_LOG_DIR");
      if (!qlog.isEmpty()) { cfg.log_folder_path = qlog.toStdString(); }
      // Library log severity. TRACE is the vendor's explicit request so that full
      // logs accompany bug reports, and stays the default — but TRACE costs real I/O
      // (tens of MB per minute once frames flow), so allow lowering it when the
      // question being investigated is throughput or stability rather than protocol.
      const QString qsev = qEnvironmentVariable("QAR_PANEL_LOG_SEVERITY").toLower();
      if (!qsev.isEmpty()) {
        if (qsev == "trace") { cfg.log_severity = QAR_LOG_SEVERITY_TRACE; }
        else if (qsev == "debug") { cfg.log_severity = QAR_LOG_SEVERITY_DEBUG; }
        else if (qsev == "info") { cfg.log_severity = QAR_LOG_SEVERITY_INFO; }
        else if (qsev == "warn" || qsev == "warning") {
          cfg.log_severity = QAR_LOG_SEVERITY_WARN;
        } else if (qsev == "error") { cfg.log_severity = QAR_LOG_SEVERITY_ERROR; }
        else if (qsev == "fatal") { cfg.log_severity = QAR_LOG_SEVERITY_FATAL; }
      }
      // Distinct payload for the Visualizer connection string. Defaults to "auto"
      // (fresh UUID per invite) because that is what allows several Visualizers to
      // be invited and streamed to at once. Set QAR_PANEL_VISUALIZER_GUID=off (or
      // empty) to fall back to stock v0.1.1, where every invite collapses onto one
      // peer; set a literal GUID to address one specific instance.
      const QString qvguid = qEnvironmentVariable("QAR_PANEL_VISUALIZER_GUID");
      if (!qvguid.isEmpty()) {
        cfg.visualizer_conn_suffix =
          (qvguid.toLower() == "off") ? std::string() : qvguid.toStdString();
      }
      // Finding 16 (RESOLVED): qar_render_sender_frame_opengl takes an OUT frame, so
      // QAR owns the textures and we must copy our Ogre color/depth INTO them. The
      // hook does that with glCopyImageSubData on the render thread. QAR_PANEL_GL_SUBMIT
      // still allows a supply-own run for comparison, but it renders black against the
      // real SDK (QAR overwrites the ids we pass).
      cfg.copy_hook = qar_qar_adapter::make_gl_copy_image_hook();
      const QString submit = qEnvironmentVariable("QAR_PANEL_GL_SUBMIT").trimmed().toLower();
      if (submit == "supply-own") {
        cfg.submit_mode = qar_qar_adapter::GlSubmitMode::kSupplyOwnTextureIds;
        appendLog("LIVE: GL submit mode = supply-own (expected BLACK against the real SDK)");
      } else if (!submit.isEmpty() && submit != "render-into") {
        setLastError(QString("unknown QAR_PANEL_GL_SUBMIT '%1' (use render-into|supply-own)")
          .arg(submit));
      }
      // --- Multi-peer (one Visualizer == one physical client head) --------------
      // Connect invites NOTHING by default: the operator spawns Visualizers on demand
      // with "Invite peer", and a real-device session often wants none at all. Set
      // QAR_PANEL_VISUALIZERS=N to opt back into auto-inviting N of them at connect.
      bool n_ok = false;
      const int n_viz = qEnvironmentVariable("QAR_PANEL_VISUALIZERS").trimmed().toInt(&n_ok);
      if (n_ok && n_viz > 0) { cfg.auto_invite_visualizers = static_cast<uint32_t>(n_viz); }
      bool max_ok = false;
      const int max_peers = qEnvironmentVariable("QAR_PANEL_MAX_PEERS").trimmed().toInt(&max_ok);
      if (max_ok && max_peers > 0) { cfg.max_peers = static_cast<uint32_t>(max_peers); }
      // --- App volume (the mixer's unit of work) --------------------------------
      // Every render stream is keyed to one app volume; with the mixer enabled a
      // stream without one lands nowhere and the Visualizer stays empty. Size and
      // scale are env-tunable because the right values depend on how big the RViz
      // scene is: the mixer masks our content to this box, so a 20 m grid needs
      // either a big box or a small app_scale to survive the mask.
      if (qEnvironmentVariable("QAR_PANEL_APP_VOLUME").trimmed().toLower() == "off") {
        cfg.create_app_volume = false;
        appendLog("LIVE: app volume DISABLED — streams get app_volume_id=NULL "
          "(mixer-off path only)");
      }
      const QString vol_name = qEnvironmentVariable("QAR_PANEL_VOLUME_NAME").trimmed();
      if (!vol_name.isEmpty()) { cfg.app_volume_name = vol_name.toStdString(); }
      bool vs_ok = false;
      const float vol_size =
        qEnvironmentVariable("QAR_PANEL_VOLUME_SIZE").trimmed().toFloat(&vs_ok);
      if (vs_ok && vol_size > 0.0f) {
        cfg.app_volume_width_m = vol_size;
        cfg.app_volume_length_m = vol_size;
        cfg.app_volume_height_m = vol_size;
      }
      bool vy_ok = false;
      const float vol_y = qEnvironmentVariable("QAR_PANEL_VOLUME_Y").trimmed().toFloat(&vy_ok);
      if (vy_ok) { cfg.app_volume_center_y_m = vol_y; }
      bool vsc_ok = false;
      const float vol_scale =
        qEnvironmentVariable("QAR_PANEL_VOLUME_SCALE").trimmed().toFloat(&vsc_ok);
      if (vsc_ok && vol_scale > 0.0f) { cfg.app_volume_scale = vol_scale; }
      if (cfg.create_app_volume) {
        appendLog(QString("LIVE: app volume '%1' %2 m cube at y=%3 m, app_scale=%4")
          .arg(QString::fromStdString(cfg.app_volume_name))
          .arg(static_cast<double>(cfg.app_volume_width_m))
          .arg(static_cast<double>(cfg.app_volume_center_y_m))
          .arg(static_cast<double>(cfg.app_volume_scale)));
      }
      // Which subscription peers may get a stream. The subscription surfaces the
      // runtime's internal app peers too (a live run saw 5 ids for 1 Visualizer), so
      // the default admits only the ones we invited ourselves. 'named' additionally
      // admits peers whose display name matches QAR_PANEL_PEER_NAME_FILTER — the
      // policy for Visualizers that join the session on their own, once a traced run
      // (QAR_ADAPTER_TRACE=1, "peer_spec" lines) shows what they call themselves.
      const QString adm = qEnvironmentVariable("QAR_PANEL_PEER_ADMISSION").trimmed().toLower();
      if (adm == "any") {
        cfg.admission = qar_qar_adapter::PeerAdmission::kAny;
        appendLog("LIVE: peer admission = ANY (diagnostic — will also admit internal "
          "runtime peers)");
      } else if (adm == "named") {
        cfg.admission = qar_qar_adapter::PeerAdmission::kInvitedOrNamed;
        appendLog("LIVE: peer admission = NAMED — note that SDK v0.1.1 display names "
          "are auto-generated and change between updates, so this rarely matches.");
      } else if (adm == "new") {
        // The policy for a demo with REAL onboarded peers (HoloLens, Quest, another
        // machine's Visualizer). They join via QAROS onboarding, never via
        // qar_session_invite_peer, so 'invited' would silently refuse them.
        cfg.admission = qar_qar_adapter::PeerAdmission::kInvitedOrNew;
        appendLog("LIVE: peer admission = NEW — peers present in the first few seconds "
          "are the runtime's own; anything joining later is treated as a real "
          "consumer and gets a stream.");
      } else if (!adm.isEmpty() && adm != "invited") {
        setLastError(QString("unknown QAR_PANEL_PEER_ADMISSION '%1' "
          "(use invited|new|named|any)").arg(adm));
      }
      bool settle_ok = false;
      const int settle = qEnvironmentVariable("QAR_PANEL_PEER_SETTLE_MS").trimmed()
        .toInt(&settle_ok);
      if (settle_ok && settle >= 0) { cfg.baseline_settle_ms = static_cast<uint32_t>(settle); }
      const QString name_filter = qEnvironmentVariable("QAR_PANEL_PEER_NAME_FILTER").trimmed();
      if (!name_filter.isEmpty()) { cfg.admission_name_filter = name_filter.toStdString(); }
      // Real devices invited right after the session comes up, e.g.
      // "hololens:HOLOLENS-A2B3,hololens:HOLOLENS-C4D5,quest". Invited devices land in
      // the allow-list, so they need no admission heuristic.
      const QString invite_peers = qEnvironmentVariable("QAR_PANEL_INVITE_PEERS").trimmed();
      if (!invite_peers.isEmpty()) {
        cfg.invite_peers = invite_peers.toStdString();
        appendLog(QString("LIVE: inviting devices at connect: %1").arg(invite_peers));
      }
      appendLog(QString("LIVE: auto-invite %1, admission=%2, max_peers=%3")
        .arg(cfg.auto_invite_visualizers == 0
          ? QString("OFF (use 'Invite peer' to open a Visualizer)")
          : QString("%1 Visualizer(s)").arg(cfg.auto_invite_visualizers))
        .arg(adm.isEmpty() ? QString("invited") : adm)
        .arg(cfg.max_peers));
      // Build the session on a worker and let the render tick collect it. The
      // constructor blocks for ~2.9 s (2.1 s of it inside
      // qar_runtime_create_session, which spawns the runtime launcher), and doing
      // that on the Qt thread is what froze RViz for the whole of it.
      auto pending = std::make_shared<PendingConnect>();
      pending_connect_ = pending;
      connect_thread_ = std::thread(
        [pending, cfg]() {
          try {
            pending->factory =
              std::make_shared<qar_qar_adapter::QarCApiSenderFactory>(cfg);
          } catch (const std::exception & e) {
            pending->error = e.what();
            pending->threw = true;
          } catch (...) {
            pending->error = "unknown exception";
            pending->threw = true;
          }
          // Release: everything written above is visible to the Qt thread once it
          // observes done, and the Qt thread owns the session exclusively after.
          pending->done.store(true, std::memory_order_release);
        });
      updateRuntimeStateLabel();
      return;  // pollPendingConnect() finishes this from the render tick
    }
    finishLiveConnect();
    return;
  }

  // Mock path (unchanged).
  qar_running_ = true;
  if (manager_) {
    manager_->set_streaming_active(true);
  }
  updateRuntimeStateLabel();
  appendLog("Qar runtime STARTED (mock).");
}

void QarStreamingPanel::setupGeoSubscription()
{
  if (!geo_enabled_ || gps_sub_) {
    return;
  }
  if (getDisplayContext() == nullptr) {
    return;
  }
  auto ros_abstraction = getDisplayContext()->getRosNodeAbstraction().lock();
  if (!ros_abstraction) {
    appendLog("GEO: no ROS node available; geolocation disabled for this session.");
    geo_enabled_ = false;
    return;
  }
  ros_node_ = ros_abstraction->get_raw_node();
  if (!ros_node_) {
    geo_enabled_ = false;
    return;
  }

  // Sensor data is conventionally BEST_EFFORT, and a RELIABLE subscriber would
  // silently receive nothing from such a publisher — the same QoS trap that made
  // the rosbag point clouds invisible. SensorDataQoS is the matching profile.
  gps_sub_ = ros_node_->create_subscription<sensor_msgs::msg::NavSatFix>(
    gps_topic_, rclcpp::SensorDataQoS(),
    [this](sensor_msgs::msg::NavSatFix::ConstSharedPtr msg) {
      // Runs on RViz's executor: STORE only, never call QAR from here.
      if (msg->status.status == sensor_msgs::msg::NavSatStatus::STATUS_NO_FIX) {
        return;
      }
      std::lock_guard<std::mutex> lk(gps_mutex_);
      gps_lat_ = msg->latitude;
      gps_lon_ = msg->longitude;
      gps_alt_ = msg->altitude;
      gps_have_fix_ = true;
      gps_fix_pending_ = true;
    });
  appendLog(QString("GEO: listening for NavSatFix on '%1' "
      "(QAR_PANEL_GPS_TOPIC; QAR_PANEL_GEO=off disables).")
    .arg(QString::fromStdString(gps_topic_)));
}

void QarStreamingPanel::applyPendingGeoFix()
{
  if (!geo_enabled_ || !qar_factory_ || !live_connected_) {
    return;
  }
  double lat = 0.0, lon = 0.0, alt = 0.0;
  {
    std::lock_guard<std::mutex> lk(gps_mutex_);
    if (!gps_fix_pending_ || !gps_have_fix_) {
      return;
    }
    gps_fix_pending_ = false;
    lat = gps_lat_;
    lon = gps_lon_;
    alt = gps_alt_;
  }

  // Re-anchoring on every 10 Hz fix would issue an SDK call per message for what
  // is usually receiver jitter, so only move the anchor when it actually moved.
  // Degrees -> metres is ~111320 m/deg of latitude; longitude shrinks with
  // cos(lat), and using latitude's scale for both is a deliberate over-estimate
  // that errs towards re-anchoring rather than missing real movement.
  if (geo_applied_ &&
    !qar_qar_adapter::geo_fix_moved(
      geo_last_lat_, geo_last_lon_, geo_last_alt_, lat, lon, alt,
      kGeoReanchorMetres))
  {
    return;
  }

  const std::string err = qar_factory_->set_geo_anchor_wgs84(lat, lon, alt);
  if (!err.empty()) {
    // Non-fatal: streaming continues un-georeferenced. Log once so a missing
    // anchor is visible without spamming a line per fix.
    if (!geo_applied_) {
      appendLog(QString("GEO: anchor NOT set — %1").arg(QString::fromStdString(err)));
      geo_enabled_ = false;
    }
    return;
  }
  geo_last_lat_ = lat;
  geo_last_lon_ = lon;
  geo_last_alt_ = alt;
  const bool first = !geo_applied_;
  geo_applied_ = true;
  if (first) {
    appendLog(QString("GEO: app volume anchored at lat=%1 lon=%2 alt=%3 m "
        "(ECEF WGS84, ENU axes).")
      .arg(lat, 0, 'f', 7).arg(lon, 0, 'f', 7).arg(alt, 0, 'f', 2));
  }
}

void QarStreamingPanel::pollPendingConnect()
{
  if (!pending_connect_ ||
    !pending_connect_->done.load(std::memory_order_acquire))
  {
    return;
  }
  const std::shared_ptr<PendingConnect> pending = pending_connect_;
  pending_connect_.reset();
  if (connect_thread_.joinable()) {
    connect_thread_.join();  // already finished; this only reaps it
  }

  if (pending->threw) {
    setLastError(
      QString("QarOS connect threw: %1").arg(QString::fromStdString(pending->error)));
    return;
  }
  if (!pending->factory || !pending->factory->ready()) {
    setLastError(QString("QarOS connect failed: %1").arg(
        pending->factory
        ? QString::fromStdString(pending->factory->last_error())
        : QString("session was not built")));
    return;
  }
  qar_factory_ = pending->factory;
  finishLiveConnect();
}

void QarStreamingPanel::finishLiveConnect()
{
  if (!qar_factory_ || !manager_) {
    return;
  }
  sender_factory_ = qar_factory_;
  manager_->set_sender_factory(qar_factory_);
  manager_->set_streaming_active(true);
  live_connected_ = true;
  qar_running_ = true;
  updateRuntimeStateLabel();
  // Connect no longer invites anything by default, so say what actually happened
  // rather than claiming we are waiting for a peer that was never asked for.
  if (qar_factory_->invited_peers().empty()) {
    appendLog("Connected to QarOS: session open, streaming active. No peer invited — "
      "pick a device kind and click 'Invite peer' to open a Visualizer.");
  } else {
    appendLog("Connected to QarOS: streaming active; waiting for the invited peer(s).");
  }
}

void QarStreamingPanel::onStopQar()
{
  // M10 LIVE: "Stop Qar" pauses streaming. The QAR session/handles are kept alive
  // (torn down in the panel dtor, in the correct order: manager_/streams before
  // qar_factory_/session) so we never leave the render tick pointing at a destroyed
  // session. Re-clicking Start resumes.
  if (live_stream_mode_ && live_connected_) {
    if (manager_) {
      manager_->set_streaming_active(false);
    }
    live_connected_ = false;
    qar_running_ = false;
    updateRuntimeStateLabel();
    appendLog("QarOS: streaming paused (session kept open).");
    return;
  }

  qar_running_ = false;
  if (manager_) {
    manager_->set_streaming_active(false);
  }
  updateRuntimeStateLabel();
  appendLog("Qar runtime STOPPED (mock).");
}

void QarStreamingPanel::onAddPeer()
{
  // In a live session "add a peer" means "add a physical client head", i.e. invite a
  // real device into the running session. Mock peers make no sense there.
  if (live_stream_mode_ && live_connected_) {
    inviteSelectedPeer();
    return;
  }
  addMockPeerInternal(nullptr);
}

void QarStreamingPanel::inviteSelectedPeer()
{
  if (!qar_factory_) {
    setLastError("Invite peer: not connected to QarOS.");
    return;
  }
  const QString kind_text = (peer_kind_combo_ != nullptr)
    ? peer_kind_combo_->currentData().toString() : QString("visualizer");
  qar_qar_adapter::PeerKind kind = qar_qar_adapter::PeerKind::kVisualizer;
  if (!qar_qar_adapter::parse_peer_kind(kind_text.toStdString(), kind)) {
    setLastError(QString("Invite peer: unknown device kind '%1'.").arg(kind_text));
    return;
  }
  const QString hostname = (peer_hostname_edit_ != nullptr)
    ? peer_hostname_edit_->text().trimmed() : QString();
  if (qar_qar_adapter::peer_kind_needs_hostname(kind) && hostname.isEmpty()) {
    setLastError(
      "Invite peer: a HoloLens needs its device hostname — type it in the field next "
      "to the device selector. It is the only device type the SDK can address by name.");
    return;
  }

  QarPeerId invited{};
  const std::string err =
    qar_factory_->invite_additional_peer(kind, hostname.toStdString(), invited);
  if (!err.empty()) {
    setLastError(QString("Invite %1 failed: %2")
      .arg(kind_text, QString::fromStdString(err)));
    return;
  }
  clearLastError();
  appendLog(
    QString("LIVE: invited %1%2 as peer %3 (%4 invited in total). Its stream opens "
      "when the device connects.")
    .arg(kind_text)
    .arg(hostname.isEmpty() ? QString() : QString(" '%1'").arg(hostname))
    .arg(QString::fromStdString(
        qar_ogre_streaming::peer_id_to_hex(invited).substr(0, 10)))
    .arg(qar_factory_->invited_peers().size()));
  if (kind == qar_qar_adapter::PeerKind::kVisualizer) {
    appendLog(
      "NOTE: the SDK's visualizer connection string names the visualizer APP, not an "
      "instance, so a bare invite is idempotent. The adapter appends a distinct GUID "
      "per invite (QAR_PANEL_VISUALIZER_GUID, default 'auto') to get an independent "
      "instance; set it to 'off' for stock SDK behaviour.");
  }
}

qar_ogre_streaming::SceneLightingConfig QarStreamingPanel::buildSceneLightingConfig() const
{
  using qar_ogre_streaming::SceneLightingMode;
  qar_ogre_streaming::SceneLightingConfig cfg;

  // Default OFF outside live mode: the M4/M5/M6 scripted capture proofs were recorded
  // under RViz's own lighting and must keep it. LIVE mode is the case the ask is about,
  // and defaults to BOTH: the omnipresent directional rig leaves no surface unlit, and
  // a modest light at each peer's own head keeps what that peer looks at well lit
  // (M10-live 2026-08-10: a single static point light left far sides/undersides dark).
  cfg.mode = live_stream_mode_
    ? SceneLightingMode::kOmniAndPerPeer : SceneLightingMode::kOff;
  const QString mode_env = qEnvironmentVariable("QAR_SCENE_LIGHT").trimmed();
  if (!mode_env.isEmpty()) {
    SceneLightingMode parsed = cfg.mode;
    if (qar_ogre_streaming::parse_scene_lighting_mode(mode_env.toStdString(), parsed)) {
      cfg.mode = parsed;
    }
    // An unparseable value keeps the default; the effective mode is logged by the
    // caller either way, so a typo shows up there rather than silently changing modes.
  }

  // Suppress RViz's camera-tracking headlight ("MainDirectional", created in
  // rviz_rendering::RenderWindowImpl::initialize and re-aimed every frame by
  // VisualizationManager::update). Adding lights alone only SOFTENS the reported
  // symptom — the headlight has to go for it to stop swimming. Restored on stop.
  // QAR_SCENE_LIGHT_HEADLIGHT=keep leaves RViz's own view lit exactly as before.
  const QString headlight = qEnvironmentVariable("QAR_SCENE_LIGHT_HEADLIGHT")
    .trimmed().toLower();
  const bool keep_headlight = (headlight == "keep" || headlight == "on" ||
    cfg.mode == SceneLightingMode::kOff);
  cfg.suppress_light_name = keep_headlight ? std::string() : std::string("MainDirectional");

  auto env_float = [](const char * name, float fallback) {
      const QString v = qEnvironmentVariable(name).trimmed();
      if (v.isEmpty()) { return fallback; }
      bool ok = false;
      const float parsed = v.toFloat(&ok);
      return ok ? parsed : fallback;
    };
  auto env_grey = [](const char * name, qar_ogre_streaming::Rgb fallback) {
      // One number sets all three channels — enough for the fill/ambient tuning this
      // needs, without inventing a colour-triple syntax nobody will remember.
      const QString v = qEnvironmentVariable(name).trimmed();
      if (v.isEmpty()) { return fallback; }
      bool ok = false;
      const float g = v.toFloat(&ok);
      return ok ? qar_ogre_streaming::Rgb{g, g, g} : fallback;
    };

  cfg.ambient = env_grey("QAR_SCENE_LIGHT_AMBIENT", cfg.ambient);
  cfg.key_diffuse = env_grey("QAR_SCENE_LIGHT_INTENSITY", cfg.key_diffuse);
  // 1.0 makes the rig perfectly uniform (and shapeless); 0 lights only from above.
  cfg.fill_ratio = env_float("QAR_SCENE_LIGHT_FILL", cfg.fill_ratio);
  cfg.peer_diffuse = env_grey("QAR_SCENE_LIGHT_PEER", cfg.peer_diffuse);
  cfg.attenuation_range = env_float("QAR_SCENE_LIGHT_RANGE", cfg.attenuation_range);
  return cfg;
}

void QarStreamingPanel::onRemovePeer()
{
  QarPeerId id{};
  if (selectedPeerId(id)) {
    removePeerInternal(id);
  } else {
    setLastError("Remove peer: no peer selected.");
  }
}

void QarStreamingPanel::onStartStream()
{
  QarPeerId id{};
  if (selectedPeerId(id)) {
    startStreamInternal(id);
  } else {
    setLastError("Start stream: no peer selected.");
  }
}

void QarStreamingPanel::onStopStream()
{
  QarPeerId id{};
  if (selectedPeerId(id)) {
    stopStreamInternal(id);
  } else {
    setLastError("Stop stream: no peer selected.");
  }
}

void QarStreamingPanel::onRefreshDiagnostics()
{
  refreshContextStatus();
  if (manager_) {
    const auto snap = manager_->diagnostics_snapshot();
    appendLog(
      QString(
        "Diagnostics: streaming=%1 streams=%2 render_ticks=%3 batches=%4 "
        "batch_last=%5ms batch_avg=%6ms served=%7 skipped=%8 budget_skips=%9")
      .arg(snap.streaming_active ? "true" : "false")
      .arg(snap.stream_count)
      .arg(static_cast<qulonglong>(stereo_render_ticks_))
      .arg(static_cast<qulonglong>(snap.batches_rendered))
      .arg(snap.last_batch_ms, 0, 'f', 2)
      .arg(snap.avg_batch_ms, 0, 'f', 2)
      .arg(snap.last_batch_streams_served)
      .arg(snap.last_batch_streams_skipped)
      .arg(static_cast<qulonglong>(snap.budget_skipped_frames)));
    // Per-stream (per-peer) live diagnostics: frames, per-eye + total timing,
    // derived achieved FPS, and this peer's current head pose (independent view).
    for (const auto & sd : snap.streams) {
      QString line = QString(
        "  peer %1 [%2x%3 @%4fps] submitted=%5 skipped=%6 fps=%7 "
        "L=%8ms R=%9ms total=%10ms avg=%11ms")
        .arg(QString::fromStdString(sd.peer_id_hex).left(8))
        .arg(sd.width).arg(sd.height).arg(sd.max_fps)
        .arg(static_cast<qulonglong>(sd.frames_submitted))
        .arg(static_cast<qulonglong>(sd.frames_skipped))
        .arg(sd.achieved_fps, 0, 'f', 1)
        .arg(sd.last_left_render_ms, 0, 'f', 2)
        .arg(sd.last_right_render_ms, 0, 'f', 2)
        .arg(sd.last_render_ms, 0, 'f', 2)
        .arg(sd.avg_render_ms, 0, 'f', 2);
      // Per-peer head pose from that peer's own (phase-offset) source => the views
      // are provably independent. Match the source by peer-id hex.
      std::shared_ptr<const qar_ogre_streaming::MockQarFrameInfoSource> src;
      if (mock_factory_) {
        for (const auto & ps : mock_factory_->sources()) {
          if (qar_ogre_streaming::peer_id_to_hex(ps.peer_id) == sd.peer_id_hex) {
            src = ps.source;
            break;
          }
        }
      }
      if (src && sd.frames_submitted > 0) {
        const QarVector3 hp = src->head_position_at(sd.frames_submitted - 1);
        line += QString(" head=(%1,%2,%3)")
          .arg(hp.x, 0, 'f', 2).arg(hp.y, 0, 'f', 2).arg(hp.z, 0, 'f', 2);
      }
      appendLog(line);
    }
  } else {
    appendLog("Diagnostics: library manager not initialized.");
  }
}

// ---------------------------------------------------------------------------
// Offscreen capture (M4 Gate A)
// ---------------------------------------------------------------------------
void QarStreamingPanel::onCaptureOffscreenFrame()
{
  // Interactive capture: write to QAR_PANEL_AUTOCAPTURE (if set) else the CWD,
  // with a timestamped prefix.
  QString base = qEnvironmentVariable("QAR_PANEL_AUTOCAPTURE");
  if (base.isEmpty()) {
    const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    base = QDir(QDir::currentPath()).filePath("qar_offscreen_" + stamp);
  }
  QString summary;
  const bool ok = captureOffscreenToPrefix(base, summary);
  if (!ok) {
    setLastError("Offscreen capture failed: " + summary);
  } else {
    clearLastError();
  }
}

void QarStreamingPanel::runAutoCapture()
{
  const QString prefix = qEnvironmentVariable("QAR_PANEL_AUTOCAPTURE");
  appendLog("AUTOCAPTURE: running non-interactive offscreen capture sequence.");
  QString summary;
  const bool ok = captureOffscreenToPrefix(prefix, summary);
  appendLog(QString("AUTOCAPTURE result: %1 — %2").arg(ok ? "OK" : "FAIL").arg(summary));
  // Flush pending events, then quit RViz cleanly (scriptable).
  QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

bool QarStreamingPanel::captureOffscreenToPrefix(const QString & prefix, QString & summary)
{
  if (prefix.isEmpty()) {
    summary = "empty output prefix";
    return false;
  }
  if (!adapter_ || !adapter_->isValid()) {
    summary = "no valid DisplayContext / SceneManager";
    return false;
  }
  if (executor_ && !executor_->is_render_thread()) {
    // All Ogre/GL work must run on the render thread. Our call
    // sites (button click / QTimer) are already on it; refuse otherwise.
    summary = "capture must be invoked on the render thread";
    return false;
  }

  Ogre::SceneManager * sm = adapter_->getSceneManager();
  Ogre::Camera * src = adapter_->getViewCamera();
  if (src == nullptr) {
    summary = "no active RViz view camera (getViewManager()->getCurrent()->getCamera())";
    return false;
  }

  const RenderSettings s = currentSettings();

  if (!offscreen_rt_) {
    try {
      offscreen_rt_ =
        std::make_unique<qar_ogre_streaming::OgreStereoRenderTarget>(sm, "qar_m4_offscreen");
    } catch (const std::exception & e) {
      summary = QString("render target ctor failed: %1").arg(e.what());
      return false;
    }
  }

  std::vector<std::uint8_t> rgba;
  std::vector<float> depth;
  std::uint32_t w = s.width;
  std::uint32_t h = s.height;

  bool ok = true;
  QString err;

  // --- All Ogre/GL work inside the RViz render lock -------------------------
  adapter_->lockRender();
  if (!offscreen_rt_->is_created()) {
    const auto r = offscreen_rt_->create(
      w, h, QAR_PIXEL_FORMAT_R8G8B8A8, QAR_PIXEL_FORMAT_D32_FLOAT, /*with_depth=*/true);
    if (!r.ok()) {
      ok = false;
      err = QString::fromStdString(r.message);
    }
  } else {
    const auto r = offscreen_rt_->resize(w, h);
    if (!r.ok()) {
      ok = false;
      err = QString::fromStdString(r.message);
    }
  }
  if (ok) {
    offscreen_rt_->cloneCameraFrom(src);
    const auto rr = offscreen_rt_->render();
    if (!rr.ok()) {
      ok = false;
      err = QString::fromStdString(rr.message);
    }
  }
  if (ok) {
    w = offscreen_rt_->width();
    h = offscreen_rt_->height();
    const auto cr = offscreen_rt_->readColorRgba(rgba);
    if (!cr.ok()) {
      ok = false;
      err = QString::fromStdString(cr.message);
    }
    const auto dr = offscreen_rt_->readDepthMetric(depth);
    if (!dr.ok()) {
      appendLog("AUTOCAPTURE: depth readback failed: " + QString::fromStdString(dr.message));
      depth.clear();
    }
  }
  adapter_->unlockRender();

  if (!ok) {
    summary = "offscreen render failed: " + err;
    return false;
  }

  // --- CPU-side PNG / raw dump (outside the render lock) --------------------
  QFileInfo fi(prefix);
  if (!fi.absolutePath().isEmpty()) {
    QDir().mkpath(fi.absolutePath());
  }
  const std::string color_path = (prefix + "_color.png").toStdString();
  const std::string depth_png_path = (prefix + "_depth.png").toStdString();
  const std::string depth_raw_path = (prefix + "_depth.f32").toStdString();

  const bool color_ok = writeColorPng(color_path, w, h, rgba);
  const bool depth_png_ok = !depth.empty() && writeDepthVisualizationPng(depth_png_path, w, h, depth);
  const bool depth_raw_ok = !depth.empty() && writeDepthRawF32(depth_raw_path, w, h, depth);
  const CaptureStats st = computeCaptureStats(w, h, rgba, depth);

  const bool content_ok = st.color_non_background_pixels > 0;
  const bool depth_ok = st.depth_nonzero_pixels > 0 && st.depth_max > st.depth_min;
  const bool overall = color_ok && content_ok && depth_ok;

  // Stats file for scriptable verification.
  QString report;
  QTextStream ts(&report);
  ts << "# Qar RViz2 offscreen capture (M4)\n";
  ts << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
  ts << "color_png=" << QString::fromStdString(color_path) << "\n";
  ts << "depth_png=" << QString::fromStdString(depth_png_path) << "\n";
  ts << "depth_raw=" << QString::fromStdString(depth_raw_path) << "\n";
  ts << "width=" << w << "\n";
  ts << "height=" << h << "\n";
  ts << "color_written=" << (color_ok ? "true" : "false") << "\n";
  ts << "depth_png_written=" << (depth_png_ok ? "true" : "false") << "\n";
  ts << "depth_raw_written=" << (depth_raw_ok ? "true" : "false") << "\n";
  ts << "color_content_pixels=" << static_cast<qulonglong>(st.color_non_background_pixels) << "\n";
  ts << "depth_nonzero_pixels=" << static_cast<qulonglong>(st.depth_nonzero_pixels) << "\n";
  ts << "depth_min=" << st.depth_min << "\n";
  ts << "depth_max=" << st.depth_max << "\n";
  ts << "overall_capture_pass=" << (overall ? "true" : "false") << "\n";
  ts.flush();

  QFile sf(prefix + "_capture.txt");
  if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
    sf.write(report.toUtf8());
    sf.close();
  }

  summary = QString(
    "%1x%2 color_content_px=%3 depth_nonzero_px=%4 depth=[%5..%6]m pass=%7 -> %8")
    .arg(w).arg(h)
    .arg(static_cast<qulonglong>(st.color_non_background_pixels))
    .arg(static_cast<qulonglong>(st.depth_nonzero_pixels))
    .arg(st.depth_min, 0, 'f', 3).arg(st.depth_max, 0, 'f', 3)
    .arg(overall ? "true" : "false")
    .arg(QString::fromStdString(color_path));
  appendLog(summary);
  return overall;
}

// ---------------------------------------------------------------------------
// Single-peer stereo streaming (M5)
// ---------------------------------------------------------------------------
void QarStreamingPanel::rebuildFrameSource()
{
  if (!frame_source_) {
    return;
  }
  const RenderSettings s = currentSettings();
  qar_ogre_streaming::MockQarFrameInfoSource::Config cfg;  // library defaults (IPD 64 mm)
  cfg.aspect = (s.height > 0)
    ? static_cast<float>(s.width) / static_cast<float>(s.height) : cfg.aspect;
  // IPD is configurable (default 64 mm). The scripted stereo capture sets a wider
  // baseline via QAR_STEREO_IPD_M so the parallax is unmistakable to the eye;
  // the metadata always records the actual value used.
  bool ipd_ok = false;
  // Accept both '.' and ',' decimal separators (robust to the shell locale that
  // set the env var); QString::toDouble itself is C-locale only.
  QString ipd_str = qEnvironmentVariable("QAR_STEREO_IPD_M");
  ipd_str.replace(',', '.');
  const double ipd_env = ipd_str.toDouble(&ipd_ok);
  if (ipd_ok && ipd_env > 0.0) {
    cfg.ipd_metres = static_cast<float>(ipd_env);
  }
  frame_source_->set_config(cfg);
  // Push the same base config into the factory so every per-peer stream is built
  // from it (each peer offset in orbit phase for an independent view; M6).
  if (mock_factory_) {
    mock_factory_->set_base_config(cfg);
  }
}

void QarStreamingPanel::driveLiveStreaming()
{
  if (!qar_factory_ || !manager_ || !adapter_ || !adapter_->isValid()) {
    return;
  }

  // 0) Latch the Ogre/WGL context while it is current on this thread. QAR needs it
  //    (QarStreamParamsOpenGl) when a sender is created, but this tick is a QTimer
  //    slot that can land BETWEEN Ogre frames — so we capture opportunistically
  //    every tick and create_sender() uses the latched handles.
  const bool gl_ready = qar_factory_->capture_gl_context();

  // 1) Open a stereo stream for every peer that requested one (the QAR
  //    render-request path). create_stream() only bookkeeps; the scheduler lazily
  //    creates the Ogre eye targets under the render lock during render_tick().
  std::vector<QarPeerId> new_peers;
  qar_factory_->poll_new_peer_requests(new_peers);
  const RenderSettings s = currentSettings();
  for (const QarPeerId & id : new_peers) {
    if (manager_->has_stream(id)) {
      continue;
    }
    qar_ogre_streaming::StereoStreamConfig sc;
    sc.peer_id = id;
    sc.width = s.width;
    sc.height = s.height;
    sc.max_fps = s.fps;
    const auto cr = manager_->create_stream(sc);
    if (cr.ok()) {
      if (auto * st = manager_->get_stream(id)) {
        st->set_active(true);
      }
      appendLog(QString("LIVE: opened stream for peer %1")
        .arg(QString::fromStdString(qar_ogre_streaming::peer_id_to_hex(id).substr(0, 10))));
    } else if (cr.status != qar_ogre_streaming::Status::AlreadyExists) {
      // Re-queue and retry next tick rather than dropping the peer. A null sender is
      // now the NORMAL case for the first few ticks: sender creation is asynchronous
      // (qar_render_sender_create_async), so it reports "not yet" until the receiver
      // has finished negotiating. The other benign case is the startup race where
      // RViz has not drawn a frame yet, so no GL context could be chained.
      //
      // Deliberately not an error: the old synchronous path made this a hard failure
      // AND blocked the GUI thread until it resolved. Now the tick keeps running.
      qar_factory_->on_render_request(id);
      if (gl_ready && !live_sender_pending_logged_) {
        live_sender_pending_logged_ = true;
        appendLog("LIVE: sender creation in flight (async) — RViz stays responsive; "
          "the stream opens as soon as the peer completes negotiation.");
      }
    }
  }

  // 2) Render + submit every active stream. The scheduler renders the LIVE RViz
  //    scene from each peer's head pose (reported by begin_frame) and submits it
  //    through the QAR C API.
  const qar_ogre_streaming::Result res = manager_->render_tick();
  if (res.ok()) {
    ++stereo_render_ticks_;
    adapter_->queueRender();
  } else {
    const QString msg = QString::fromStdString(res.message);
    if (msg != last_error_text_) {
      setLastError("live render_tick: " + msg);
    }
  }

  // 3) Close streams that went away (begin_frame reported STREAM_CLOSED).
  std::vector<QarPeerId> closed;
  qar_factory_->poll_closed_peers(closed);
  for (const QarPeerId & id : closed) {
    manager_->destroy_stream(id);
    qar_factory_->forget(id);
    appendLog(QString("LIVE: closed stream for peer %1")
      .arg(QString::fromStdString(qar_ogre_streaming::peer_id_to_hex(id).substr(0, 10))));
  }

}

void QarStreamingPanel::onRenderTick()
{
  // Tick accounting runs FIRST, ahead of every early-out: the heartbeat has to
  // record that the Qt timer is still being served even when there is nothing to
  // render, and the inter-tick gap is what exposes a GUI-thread stall after the
  // fact. Measuring only inside the streaming path would go quiet during exactly
  // the states we most need to tell apart (idle vs. blocked).
  const auto tick_now = std::chrono::steady_clock::now();
  ++render_timer_fires_;
  if (tick_gap_valid_) {
    const double gap_ms =
      std::chrono::duration<double, std::milli>(tick_now - tick_last_).count();
    if (gap_ms > tick_max_gap_ms_) {
      tick_max_gap_ms_ = gap_ms;
    }
  }
  tick_last_ = tick_now;
  tick_gap_valid_ = true;
  emitHeartbeat();

  if (!manager_ || !adapter_ || !adapter_->isValid()) {
    return;
  }
  if (live_stream_mode_) {
    // Adopt a background connect the moment it lands. This runs before the
    // live_connected_ gate because that is exactly the window it covers.
    pollPendingConnect();
    if (live_connected_) {
      // Both of these call into QAR, so they belong on this thread, not on the
      // ROS executor that delivered the fix.
      applyPendingGeoFix();
      driveLiveStreaming();
    }
    return;
  }
  if (!manager_->streaming_active()) {
    return;
  }
  // Batched multi-peer render on the Qt render thread. The scheduler collects the
  // streams due this tick (FPS + time budget), then renders them ALL inside ONE
  // DisplayContext render lock (via render_lock_) so every peer samples the same
  // scene snapshot. Callbacks never render — only this tick.
  const qar_ogre_streaming::Result res = manager_->render_tick();
  if (res.ok()) {
    ++stereo_render_ticks_;
    adapter_->queueRender();
  } else {
    const QString msg = QString::fromStdString(res.message);
    if (msg != last_error_text_) {
      setLastError("render_tick: " + msg);
    }
  }
}

bool QarStreamingPanel::renderActiveStreamsOnce()
{
  if (!manager_ || !adapter_ || !adapter_->isValid()) {
    return false;
  }
  if (executor_ && !executor_->is_render_thread()) {
    return false;  // never touch Ogre off the render thread
  }

  bool any_rendered = false;
  QString err;
  adapter_->lockRender();
  for (const RvizPeerEntry & entry : peer_model_.peers()) {
    if (!entry.streaming) {
      continue;
    }
    auto * stream = manager_->get_stream(entry.id);
    if (stream == nullptr || !stream->active()) {
      continue;
    }
    if (!stream->targets_ready()) {
      const auto ir = stream->initialize_targets();
      if (!ir.ok()) {
        err = QString::fromStdString(ir.message);
        continue;
      }
    }
    const auto rr = stream->render_and_submit();
    if (!rr.ok()) {
      err = QString::fromStdString(rr.message);
      continue;
    }
    any_rendered = true;
  }
  adapter_->unlockRender();

  if (any_rendered) {
    ++stereo_render_ticks_;
    adapter_->queueRender();
  }
  // Surface render errors only when the message changes (avoid per-tick spam).
  if (!err.isEmpty() && err != last_error_text_) {
    setLastError("stereo render: " + err);
  }
  return any_rendered;
}

bool QarStreamingPanel::captureStereoPair(
  const QarPeerId & peer_id, const QString & prefix, QString & summary)
{
  if (prefix.isEmpty()) {
    summary = "empty output prefix";
    return false;
  }
  if (!manager_ || !adapter_ || !adapter_->isValid()) {
    summary = "no valid manager / DisplayContext";
    return false;
  }
  if (executor_ && !executor_->is_render_thread()) {
    summary = "stereo capture must be invoked on the render thread";
    return false;
  }
  auto * stream = manager_->get_stream(peer_id);
  if (stream == nullptr) {
    summary = "no stream for peer";
    return false;
  }

  std::vector<std::uint8_t> l_rgba;
  std::vector<std::uint8_t> r_rgba;
  std::vector<float> l_depth;
  std::vector<float> r_depth;
  std::uint32_t w = 0;
  std::uint32_t h = 0;
  bool ok = true;
  QString err;

  adapter_->lockRender();
  if (!stream->targets_ready()) {
    const auto ir = stream->initialize_targets();
    if (!ir.ok()) {
      ok = false;
      err = QString::fromStdString(ir.message);
    }
  }
  qar_ogre_streaming::OgreStereoRenderTarget * lt =
    ok ? stream->left_target() : nullptr;
  qar_ogre_streaming::OgreStereoRenderTarget * rt =
    ok ? stream->right_target() : nullptr;
  if (ok && lt != nullptr && rt != nullptr && lt->is_created() && rt->is_created()) {
    w = lt->width();
    h = lt->height();
    lt->readColorRgba(l_rgba);
    lt->readDepthMetric(l_depth);
    rt->readColorRgba(r_rgba);
    rt->readDepthMetric(r_depth);
  } else if (ok) {
    ok = false;
    err = "eye targets not created (render at least one frame first)";
  }
  adapter_->unlockRender();

  if (!ok) {
    summary = "stereo readback failed: " + err;
    return false;
  }

  // --- CPU-side PNG / raw dump + metadata (outside the render lock) ----------
  QFileInfo fi(prefix);
  if (!fi.absolutePath().isEmpty()) {
    QDir().mkpath(fi.absolutePath());
  }
  const bool lc = writeColorPng((prefix + "_left_color.png").toStdString(), w, h, l_rgba);
  const bool rc = writeColorPng((prefix + "_right_color.png").toStdString(), w, h, r_rgba);
  writeDepthVisualizationPng((prefix + "_left_depth.png").toStdString(), w, h, l_depth);
  writeDepthVisualizationPng((prefix + "_right_depth.png").toStdString(), w, h, r_depth);
  writeDepthRawF32((prefix + "_left_depth.f32").toStdString(), w, h, l_depth);
  writeDepthRawF32((prefix + "_right_depth.f32").toStdString(), w, h, r_depth);

  const CaptureStats ls = computeCaptureStats(w, h, l_rgba, l_depth);
  const CaptureStats rs = computeCaptureStats(w, h, r_rgba, r_depth);
  const StereoParallaxStats sp = computeStereoParallax(w, h, l_rgba, r_rgba, l_depth, r_depth);

  // Per-eye pose/FOV metadata: reproduce the exact frame the stream just rendered
  // (deterministic from the source and the stream's submitted-frame count).
  const auto diag = stream->diagnostics();
  const std::uint64_t last_idx = diag.frames_submitted > 0 ? diag.frames_submitted - 1 : 0;
  const qar_ogre_streaming::QarFrameInfo info = frame_source_->frame_at(last_idx);

  const bool both_content = ls.color_non_background_pixels > 0 && rs.color_non_background_pixels > 0;
  const bool depth_ok =
    ls.depth_nonzero_pixels > 0 && ls.depth_max > ls.depth_min &&
    rs.depth_nonzero_pixels > 0 && rs.depth_max > rs.depth_min;
  const bool differ = sp.image_mean_abs_diff > 0.5;  // eyes are not identical
  const bool overall = lc && rc && both_content && depth_ok && differ && sp.nearer_shifts_more;

  QString report;
  QTextStream ts(&report);
  ts << "# Qar RViz2 single-peer stereo capture (M5)\n";
  ts << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
  ts << "peer_id=" << QString::fromStdString(qar_ogre_streaming::peer_id_to_hex(peer_id)) << "\n";
  ts << "layout=SEPARATED_TEXTURES\n";
  ts << "width=" << w << "\n";
  ts << "height=" << h << "\n";
  ts << "source_frame_index=" << static_cast<qulonglong>(info.frame_index) << "\n";
  ts << "ipd_metres=" << frame_source_->config().ipd_metres << "\n";
  auto write_eye = [&ts](const char * label, const qar_ogre_streaming::EyeView & e) {
    ts << label << "_eye_pos=" << e.pose.position.x << "," << e.pose.position.y
       << "," << e.pose.position.z << "\n";
    ts << label << "_eye_quat_xyzw=" << e.pose.orientation.x << "," << e.pose.orientation.y
       << "," << e.pose.orientation.z << "," << e.pose.orientation.w << "\n";
    ts << label << "_eye_fov_lrud=" << e.fov.angle_left << "," << e.fov.angle_right
       << "," << e.fov.angle_up << "," << e.fov.angle_down << "\n";
  };
  write_eye("left", info.left_eye);
  write_eye("right", info.right_eye);
  ts << "left_color_content_px=" << static_cast<qulonglong>(ls.color_non_background_pixels) << "\n";
  ts << "right_color_content_px=" << static_cast<qulonglong>(rs.color_non_background_pixels) << "\n";
  ts << "left_depth_range_m=" << ls.depth_min << ".." << ls.depth_max << "\n";
  ts << "right_depth_range_m=" << rs.depth_min << ".." << rs.depth_max << "\n";
  ts << "image_mean_abs_diff=" << sp.image_mean_abs_diff << "\n";
  ts << "near_band_m=" << sp.near_band_lo << ".." << sp.near_band_hi << "\n";
  ts << "far_band_m=" << sp.far_band_lo << ".." << sp.far_band_hi << "\n";
  ts << "near_disparity_px=" << sp.near_disparity_px << "\n";
  ts << "far_disparity_px=" << sp.far_disparity_px << "\n";
  ts << "nearer_shifts_more=" << (sp.nearer_shifts_more ? "true" : "false") << "\n";
  ts << "overall_stereo_pass=" << (overall ? "true" : "false") << "\n";
  ts.flush();

  QFile sf(prefix + "_stereo.txt");
  if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
    sf.write(report.toUtf8());
    sf.close();
  }

  summary = QString(
    "%1x%2 L_px=%3 R_px=%4 diff=%5 near_disp=%6px far_disp=%7px nearer_more=%8 pass=%9")
    .arg(w).arg(h)
    .arg(static_cast<qulonglong>(ls.color_non_background_pixels))
    .arg(static_cast<qulonglong>(rs.color_non_background_pixels))
    .arg(sp.image_mean_abs_diff, 0, 'f', 2)
    .arg(sp.near_disparity_px, 0, 'f', 2)
    .arg(sp.far_disparity_px, 0, 'f', 2)
    .arg(sp.nearer_shifts_more ? "true" : "false")
    .arg(overall ? "true" : "false");
  appendLog("stereo capture: " + summary);
  return overall;
}

void QarStreamingPanel::onCaptureStereoPair()
{
  // Find the first streaming peer (M5 is single-peer). Ensure it has rendered.
  const RvizPeerEntry * target = nullptr;
  for (const RvizPeerEntry & e : peer_model_.peers()) {
    if (e.streaming) {
      target = &e;
      break;
    }
  }
  if (target == nullptr) {
    setLastError("Capture stereo pair: start a stream first (no streaming peer).");
    return;
  }
  renderActiveStreamsOnce();  // ensure the eye targets hold a fresh frame

  const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
  const QString base = QDir(QDir::currentPath()).filePath("qar_stereo_" + stamp);
  QString summary;
  const bool ok = captureStereoPair(target->id, base, summary);
  if (!ok) {
    setLastError("Stereo capture failed: " + summary);
  } else {
    clearLastError();
  }
}

void QarStreamingPanel::runAutoStereo()
{
  const QString prefix = qEnvironmentVariable("QAR_PANEL_AUTOSTEREO");
  appendLog("AUTOSTEREO: running non-interactive single-peer stereo capture.");

  // Drive the pipeline deterministically (stop the async timer so only this
  // loop advances the animation).
  if (render_timer_ != nullptr) {
    render_timer_->stop();
  }
  rebuildFrameSource();

  onStartQar();
  bool add_ok = false;
  const QarPeerId id = addMockPeerInternal(&add_ok);
  const bool start_ok = startStreamInternal(id);

  bool ok = true;
  int frames = qEnvironmentVariable("QAR_PANEL_AUTOSTEREO_FRAMES").toInt(&ok);
  if (!ok || frames < 2) {
    frames = 140;
  }
  const int cap_a = 0;
  const int cap_b = frames - 1;  // a clearly different pose (orbit advanced)

  QString summary_a;
  QString summary_b;
  bool pass_a = false;
  bool pass_b = false;
  if (add_ok && start_ok) {
    for (int i = 0; i < frames; ++i) {
      renderActiveStreamsOnce();  // renders + submits source frame i (animates)
      if (i == cap_a) {
        pass_a = captureStereoPair(id, prefix + "_frameA", summary_a);
      }
      if (i == cap_b) {
        pass_b = captureStereoPair(id, prefix + "_frameB", summary_b);
      }
    }
  }

  // Confirm the two captured poses actually differ (changing pose changes view).
  const qar_ogre_streaming::QarFrameInfo fa = frame_source_->frame_at(cap_a);
  const qar_ogre_streaming::QarFrameInfo fb = frame_source_->frame_at(cap_b);
  const float dx = fa.left_eye.pose.position.x - fb.left_eye.pose.position.x;
  const float dy = fa.left_eye.pose.position.y - fb.left_eye.pose.position.y;
  const float dz = fa.left_eye.pose.position.z - fb.left_eye.pose.position.z;
  const double pose_delta = std::sqrt(
    static_cast<double>(dx) * dx + static_cast<double>(dy) * dy + static_cast<double>(dz) * dz);
  const bool pose_changed = pose_delta > 0.5;

  const auto diag = manager_ ? manager_->diagnostics_snapshot() : qar_ogre_streaming::DiagnosticsSnapshot{};
  std::uint64_t submitted = 0;
  double avg_ms = 0.0;
  for (const auto & sd : diag.streams) {
    submitted += sd.frames_submitted;
    avg_ms = sd.avg_render_ms;
  }

  const bool overall = add_ok && start_ok && pass_a && pass_b && pose_changed;

  QString report;
  QTextStream ts(&report);
  ts << "# Qar RViz2 single-peer stereo AUTOSTEREO summary (M5)\n";
  ts << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
  ts << "peer_added=" << (add_ok ? "true" : "false") << "\n";
  ts << "stream_started=" << (start_ok ? "true" : "false") << "\n";
  ts << "frames_rendered=" << frames << "\n";
  ts << "frames_submitted=" << static_cast<qulonglong>(submitted) << "\n";
  ts << "avg_render_ms=" << avg_ms << "\n";
  ts << "frameA_prefix=" << prefix << "_frameA\n";
  ts << "frameB_prefix=" << prefix << "_frameB\n";
  ts << "frameA_pass=" << (pass_a ? "true" : "false") << "  (" << summary_a << ")\n";
  ts << "frameB_pass=" << (pass_b ? "true" : "false") << "  (" << summary_b << ")\n";
  ts << "pose_delta_m=" << pose_delta << "\n";
  ts << "pose_changed=" << (pose_changed ? "true" : "false") << "\n";
  ts << "overall_pass=" << (overall ? "true" : "false") << "\n";
  ts.flush();
  appendLog(report);

  QFile file(prefix + "_autostereo.txt");
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    file.write(report.toUtf8());
    file.close();
    appendLog(QString("AUTOSTEREO summary written to %1_autostereo.txt").arg(prefix));
  }

  QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

// ---------------------------------------------------------------------------
// Multi-peer batched render scheduler proof (M6)
// ---------------------------------------------------------------------------
bool QarStreamingPanel::capturePeerColorPng(
  const QarPeerId & peer_id, const QString & path, QString & summary)
{
  if (!manager_ || !adapter_ || !adapter_->isValid()) {
    summary = "no valid manager / DisplayContext";
    return false;
  }
  if (executor_ && !executor_->is_render_thread()) {
    summary = "must run on the render thread";
    return false;
  }
  auto * stream = manager_->get_stream(peer_id);
  if (stream == nullptr) {
    summary = "no stream for peer";
    return false;
  }

  std::vector<std::uint8_t> rgba;
  std::uint32_t w = 0;
  std::uint32_t h = 0;
  bool ok = true;
  QString err;

  adapter_->lockRender();
  qar_ogre_streaming::OgreStereoRenderTarget * lt = stream->left_target();
  if (lt != nullptr && lt->is_created()) {
    w = lt->width();
    h = lt->height();
    lt->readColorRgba(rgba);
  } else {
    ok = false;
    err = "left target not created (render at least one frame first)";
  }
  adapter_->unlockRender();

  if (!ok) {
    summary = err;
    return false;
  }
  const bool wrote = writeColorPng(path.toStdString(), w, h, rgba);
  const CaptureStats st = computeCaptureStats(w, h, rgba, std::vector<float>{});
  summary = QString("%1x%2 content_px=%3")
    .arg(w).arg(h).arg(static_cast<qulonglong>(st.color_non_background_pixels));
  return wrote && st.color_non_background_pixels > 0;
}

void QarStreamingPanel::runAutoMultiPeer()
{
  const QString prefix = qEnvironmentVariable("QAR_PANEL_AUTOMULTI");
  appendLog("AUTOMULTI: running non-interactive multi-peer batched render proof.");

  bool ok = false;
  int peers = qEnvironmentVariable("QAR_PANEL_AUTOMULTI_PEERS").toInt(&ok);
  if (!ok || peers < 1) {
    peers = 3;  // C8: validate with 3-4 peers
  }
  if (peers > 8) {
    peers = 8;  // C8: designed for up to 8
  }
  int ticks = qEnvironmentVariable("QAR_PANEL_AUTOMULTI_TICKS").toInt(&ok);
  if (!ok || ticks < 2) {
    ticks = 180;  // ~3 s at ~60 Hz
  }

  // Drive the pipeline deterministically: stop the async timer so only this loop
  // advances the batch.
  if (render_timer_ != nullptr) {
    render_timer_->stop();
  }
  rebuildFrameSource();  // push base config into the factory (per-peer offsets)

  onStartQar();  // manager.set_streaming_active(true)

  std::vector<QarPeerId> ids;
  for (int i = 0; i < peers; ++i) {
    bool add_ok = false;
    const QarPeerId id = addMockPeerInternal(&add_ok);
    if (add_ok && startStreamInternal(id)) {
      ids.push_back(id);  // startStreamInternal creates the stream + its own source
    }
  }

  // Batch-render for `ticks`, pacing the DRIVER to ~120 Hz so the per-peer
  // RenderBudget (target 60 FPS/peer) is the real limiter — i.e. the scheduler's
  // FPS pacing, not the driver, determines the achieved rate. Each
  // manager.render_tick() renders ALL due peers inside ONE render lock (the
  // scheduler's batch). A precise BUSY-WAIT is used instead of sleep_for because
  // Windows' ~15.6 ms sleep granularity would otherwise alias the cadence down.
  const auto driver_period = std::chrono::duration<double>(1.0 / 120.0);
  std::uint32_t max_served = 0;
  int full_batch_ticks = 0;  // ticks where every active peer rendered in one batch
  const auto run_start = std::chrono::steady_clock::now();
  auto next_tick = std::chrono::steady_clock::now();
  for (int i = 0; i < ticks; ++i) {
    const qar_ogre_streaming::Result r = manager_->render_tick();
    if (!r.ok()) {
      appendLog("AUTOMULTI: render_tick error: " + QString::fromStdString(r.message));
    }
    const auto snap_served = manager_->diagnostics_snapshot().last_batch_streams_served;
    if (snap_served > max_served) {
      max_served = snap_served;
    }
    if (snap_served == static_cast<std::uint32_t>(ids.size()) && !ids.empty()) {
      ++full_batch_ticks;
    }
    // Pump RViz's event loop periodically (not every tick): processEvents() drives
    // RViz's OWN main render (~tens of ms), which would otherwise dominate the loop
    // and mask the scheduler's throughput. ~every 30th tick keeps RViz responsive
    // while the offscreen batch runs freely between pumps (the peer targets do not
    // need the main window repainted).
    if ((i % 30) == 0) {
      adapter_->queueRender();
      QCoreApplication::processEvents();
    }
    // Precise busy-wait to the next 120 Hz driver tick (skip if already overran,
    // e.g. right after a processEvents pump).
    next_tick += std::chrono::duration_cast<std::chrono::steady_clock::duration>(driver_period);
    const auto now_tp = std::chrono::steady_clock::now();
    if (next_tick < now_tp) {
      next_tick = now_tp;  // fell behind (pump tick): don't try to catch up
    } else {
      while (std::chrono::steady_clock::now() < next_tick) {
        std::this_thread::yield();
      }
    }
  }
  const double elapsed_s =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - run_start).count();

  // Per-peer color PNGs (proof of independent views from the same scene).
  for (std::size_t i = 0; i < ids.size(); ++i) {
    QString png_summary;
    const QString png_path = QString("%1_peer%2_color.png").arg(prefix).arg(i);
    capturePeerColorPng(ids[i], png_path, png_summary);
  }

  // Assemble the report from the coherent diagnostics snapshot.
  const auto snap = manager_->diagnostics_snapshot();
  bool all_peers_min_fps = !snap.streams.empty();
  bool all_peers_submitted = !snap.streams.empty();
  double min_fps = 1e9;
  for (const auto & sd : snap.streams) {
    if (sd.achieved_fps < 30.0) {
      all_peers_min_fps = false;
    }
    if (sd.frames_submitted == 0) {
      all_peers_submitted = false;
    }
    if (sd.achieved_fps < min_fps) {
      min_fps = sd.achieved_fps;
    }
  }
  const bool same_batch = full_batch_ticks > 0;  // all peers rendered in one tick
  const bool overall =
    static_cast<int>(ids.size()) == peers && all_peers_submitted &&
    all_peers_min_fps && same_batch;

  QString report;
  QTextStream ts(&report);
  ts << "# Qar RViz2 multi-peer batched render scheduler proof (M6)\n";
  ts << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
  ts << "peers_requested=" << peers << "\n";
  ts << "peers_started=" << static_cast<int>(ids.size()) << "\n";
  ts << "ticks_driven=" << ticks << "\n";
  ts << "elapsed_seconds=" << elapsed_s << "\n";
  ts << "batches_rendered=" << static_cast<qulonglong>(snap.batches_rendered) << "\n";
  ts << "last_batch_ms=" << snap.last_batch_ms << "\n";
  ts << "avg_batch_ms=" << snap.avg_batch_ms << "\n";
  // Intrinsic scheduler headroom: how many full N-peer batches/s the render loop
  // could sustain if not throttled by RViz's own main-window redraw during this
  // synchronous scripted drive (i.e. the per-peer FPS ceiling the batch supports).
  ts << "batch_capacity_fps_per_peer="
     << (snap.avg_batch_ms > 0.0 ? 1000.0 / snap.avg_batch_ms : 0.0) << "\n";
  ts << "last_batch_streams_served=" << snap.last_batch_streams_served << "\n";
  ts << "last_batch_streams_skipped=" << snap.last_batch_streams_skipped << "\n";
  ts << "max_streams_served_in_one_batch=" << max_served << "\n";
  ts << "full_batch_ticks=" << full_batch_ticks
     << "  (ticks where ALL active peers rendered in ONE batch)\n";
  ts << "budget_skipped_frames=" << static_cast<qulonglong>(snap.budget_skipped_frames) << "\n";
  ts << "min_achieved_fps=" << min_fps << "\n";
  for (const auto & sd : snap.streams) {
    // Per-peer head pose from that peer's own offset source => independent view.
    QString head;
    if (mock_factory_) {
      for (const auto & ps : mock_factory_->sources()) {
        if (qar_ogre_streaming::peer_id_to_hex(ps.peer_id) == sd.peer_id_hex &&
          sd.frames_submitted > 0)
        {
          const QarVector3 hp = ps.source->head_position_at(sd.frames_submitted - 1);
          head = QString(" head=(%1,%2,%3)")
            .arg(hp.x, 0, 'f', 2).arg(hp.y, 0, 'f', 2).arg(hp.z, 0, 'f', 2);
          break;
        }
      }
    }
    ts << "peer " << QString::fromStdString(sd.peer_id_hex).left(8)
       << " " << sd.width << "x" << sd.height << " @" << sd.max_fps << "fps"
       << " submitted=" << static_cast<qulonglong>(sd.frames_submitted)
       << " skipped=" << static_cast<qulonglong>(sd.frames_skipped)
       << " achieved_fps=" << sd.achieved_fps
       << " L=" << sd.last_left_render_ms << "ms R=" << sd.last_right_render_ms << "ms"
       << " total=" << sd.last_render_ms << "ms avg=" << sd.avg_render_ms << "ms"
       << head << "\n";
  }
  ts << "overall_pass=" << (overall ? "true" : "false") << "\n";
  ts.flush();
  appendLog(report);

  QFile file(prefix + "_automulti.txt");
  if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    file.write(report.toUtf8());
    file.close();
    appendLog(QString("AUTOMULTI summary written to %1_automulti.txt").arg(prefix));
  }

  QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

// ---------------------------------------------------------------------------
// OpenGL zero-copy texture-handoff spike (M7 Gate B)
// ---------------------------------------------------------------------------
bool QarStreamingPanel::runGlSpikeToPrefix(const QString & prefix, QString & summary)
{
  if (prefix.isEmpty()) {
    summary = "empty output prefix";
    return false;
  }
  if (!adapter_ || !adapter_->isValid()) {
    summary = "no valid DisplayContext / SceneManager";
    return false;
  }
  if (executor_ && !executor_->is_render_thread()) {
    summary = "GL spike must run on the render thread";
    return false;
  }
  Ogre::SceneManager * sm = adapter_->getSceneManager();
  Ogre::Camera * src = adapter_->getViewCamera();
  if (src == nullptr) {
    summary = "no active RViz view camera";
    return false;
  }

  const RenderSettings s = currentSettings();
  if (!offscreen_rt_) {
    try {
      offscreen_rt_ =
        std::make_unique<qar_ogre_streaming::OgreStereoRenderTarget>(sm, "qar_m7_glspike");
    } catch (const std::exception & e) {
      summary = QString("render target ctor failed: %1").arg(e.what());
      return false;
    }
  }

  qar_ogre_streaming::GlTextureRoundtripResult result;
  bool prep_ok = true;
  QString err;

  // --- ALL GL/Ogre work inside RViz's render lock, on the render thread -------
  adapter_->lockRender();
  if (!offscreen_rt_->is_created()) {
    const auto r = offscreen_rt_->create(
      s.width, s.height, QAR_PIXEL_FORMAT_R8G8B8A8, QAR_PIXEL_FORMAT_D32_FLOAT, /*with_depth=*/true);
    if (!r.ok()) {
      prep_ok = false;
      err = QString::fromStdString(r.message);
    }
  } else {
    const auto r = offscreen_rt_->resize(s.width, s.height);
    if (!r.ok()) {
      prep_ok = false;
      err = QString::fromStdString(r.message);
    }
  }
  if (prep_ok) {
    offscreen_rt_->cloneCameraFrom(src);
    const auto rr = offscreen_rt_->render();
    if (!rr.ok()) {
      prep_ok = false;
      err = QString::fromStdString(rr.message);
    }
  }
  if (prep_ok) {
    // The crux: run inside RViz's real GL context, on the render thread (C2).
    result = qar_ogre_streaming::run_color_texture_roundtrip_spike(*offscreen_rt_);
  }
  adapter_->unlockRender();

  if (!prep_ok) {
    summary = "offscreen render failed: " + err;
    return false;
  }

  const QString log_line = QString::fromStdString(qar_ogre_streaming::format_spike_log_line(result));
  appendLog("GLSPIKE: " + log_line);

  const bool pass = result.ran && result.gl_is_texture && result.dims_match &&
    result.roundtrip_match && result.fence_created && result.fence_signaled &&
    result.opengl_frame_filled;

  // Stats file for scriptable verification.
  QString report;
  QTextStream ts(&report);
  ts << "# Qar RViz2 OpenGL zero-copy texture-handoff spike (M7, Gate B)\n";
  ts << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
  ts << "ogre_render_system=" << QString::fromStdString(result.render_system_name) << "\n";
  ts << "on_render_thread=" << ((executor_ && executor_->is_render_thread()) ? "true" : "false")
     << "\n";
  ts << "width=" << result.expected_width << "\n";
  ts << "height=" << result.expected_height << "\n";
  ts << "texture_id=" << result.texture_id << "\n";
  ts << "glIsTexture=" << (result.gl_is_texture ? "1" : "0") << "\n";
  ts << "queried_dims=" << result.queried_width << "x" << result.queried_height << "\n";
  ts << "dims_match=" << (result.dims_match ? "1" : "0") << "\n";
  ts << "ogre_bytes=" << static_cast<qulonglong>(result.ogre_byte_count) << "\n";
  ts << "raw_gl_bytes=" << static_cast<qulonglong>(result.raw_byte_count) << "\n";
  ts << "total_pixels=" << static_cast<qulonglong>(result.total_pixels) << "\n";
  ts << "matching_pixels_direct=" << static_cast<qulonglong>(result.matching_pixels_direct) << "\n";
  ts << "matching_pixels_flipped=" << static_cast<qulonglong>(result.matching_pixels_flipped)
     << "\n";
  ts << "match_fraction=" << result.match_fraction << "\n";
  ts << "flipped=" << (result.flipped ? "1" : "0") << "\n";
  ts << "roundtrip_match=" << (result.roundtrip_match ? "1" : "0") << "\n";
  ts << "fence_created=" << (result.fence_created ? "1" : "0") << "\n";
  ts << "fence_wait=" << qar_ogre_streaming::gl_interop::to_string(result.fence_wait) << "\n";
  ts << "fence_signaled=" << (result.fence_signaled ? "1" : "0") << "\n";
  ts << "opengl_frame_filled=" << (result.opengl_frame_filled ? "1" : "0") << "\n";
  ts << "spike_log=" << log_line << "\n";
  ts << "message=" << QString::fromStdString(result.message) << "\n";
  ts << "overall_pass=" << (pass ? "true" : "false") << "\n";
  ts.flush();

  QFileInfo fi(prefix);
  if (!fi.absolutePath().isEmpty()) {
    QDir().mkpath(fi.absolutePath());
  }
  QFile sf(prefix + "_glspike.txt");
  if (sf.open(QIODevice::WriteOnly | QIODevice::Text)) {
    sf.write(report.toUtf8());
    sf.close();
  }

  summary = log_line;
  return pass;
}

void QarStreamingPanel::runGlSpike()
{
  const QString prefix = qEnvironmentVariable("QAR_PANEL_GLSPIKE");
  appendLog("GLSPIKE: running non-interactive OpenGL zero-copy handoff spike.");
  QString summary;
  const bool pass = runGlSpikeToPrefix(prefix, summary);
  appendLog(QString("GLSPIKE result: %1 — %2").arg(pass ? "PASS" : "FAIL").arg(summary));
  QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

// ---------------------------------------------------------------------------
// Internal operations (shared by slots and the auto-dump verification)
// ---------------------------------------------------------------------------
QarStreamingPanel::RenderSettings QarStreamingPanel::currentSettings() const
{
  RenderSettings s;
  if (resolution_combo_ != nullptr) {
    const QSize size = resolution_combo_->currentData().toSize();
    if (size.isValid()) {
      s.width = static_cast<std::uint32_t>(size.width());
      s.height = static_cast<std::uint32_t>(size.height());
    }
  }
  if (fps_combo_ != nullptr) {
    bool ok = false;
    const int fps = fps_combo_->currentData().toInt(&ok);
    if (ok && fps > 0) {
      s.fps = static_cast<std::uint32_t>(fps);
    }
  }
  return s;
}

QarPeerId QarStreamingPanel::addMockPeerInternal(bool * ok)
{
  const RenderSettings s = currentSettings();
  const RvizPeerEntry & entry = peer_model_.addMockPeer(s.width, s.height, s.fps);
  const QarPeerId id = entry.id;
  appendLog(
    QString("Added %1 (%2), %3x%4 @ %5fps")
    .arg(QString::fromStdString(entry.label))
    .arg(QString::fromStdString(qar_ogre_streaming::peer_id_to_hex(id)))
    .arg(entry.width).arg(entry.height).arg(entry.fps));
  refreshPeerList();
  if (ok != nullptr) {
    *ok = true;
  }
  return id;
}

bool QarStreamingPanel::startStreamInternal(const QarPeerId & id)
{
  RvizPeerEntry * entry = peer_model_.find(id);
  if (entry == nullptr) {
    setLastError("Start stream: peer not found in model.");
    return false;
  }
  if (!manager_) {
    setLastError("Start stream: library manager not initialized.");
    return false;
  }

  qar_ogre_streaming::StereoStreamConfig cfg;
  cfg.peer_id = id;
  cfg.width = entry->width;
  cfg.height = entry->height;
  cfg.max_fps = entry->fps;

  const auto res = manager_->create_stream(cfg);
  if (!res.ok() && res.status != qar_ogre_streaming::Status::AlreadyExists) {
    setLastError(
      QString("create_stream failed: %1").arg(QString::fromStdString(res.message)));
    return false;
  }

  if (auto * stream = manager_->get_stream(id)) {
    stream->set_active(true);
  }
  entry->streaming = true;
  appendLog(QString("Stream started for %1.").arg(QString::fromStdString(entry->label)));
  refreshPeerList();
  clearLastError();
  return true;
}

bool QarStreamingPanel::stopStreamInternal(const QarPeerId & id)
{
  RvizPeerEntry * entry = peer_model_.find(id);
  if (entry == nullptr) {
    setLastError("Stop stream: peer not found in model.");
    return false;
  }
  if (manager_) {
    if (auto * stream = manager_->get_stream(id)) {
      stream->set_active(false);
    }
  }
  entry->streaming = false;
  appendLog(QString("Stream stopped for %1.").arg(QString::fromStdString(entry->label)));
  refreshPeerList();
  return true;
}

bool QarStreamingPanel::removePeerInternal(const QarPeerId & id)
{
  const bool existed = peer_model_.find(id) != nullptr;
  if (manager_ && manager_->has_stream(id)) {
    manager_->destroy_stream(id);
  }
  const bool removed = peer_model_.removePeer(id);
  refreshPeerList();
  if (removed) {
    appendLog(
      QString("Removed peer %1.")
      .arg(QString::fromStdString(qar_ogre_streaming::peer_id_to_hex(id))));
  }
  return removed && existed;
}

// ---------------------------------------------------------------------------
// Non-interactive verification: dump diagnostics + quit RViz
// ---------------------------------------------------------------------------
void QarStreamingPanel::runAutoDump()
{
  appendLog("AUTODUMP: running non-interactive verification sequence.");

  const bool ctx_valid = adapter_ && adapter_->isValid();
  const bool scene_valid = adapter_ && adapter_->getSceneManager() != nullptr;
  const QString sm_name =
    adapter_ ? QString::fromStdString(adapter_->sceneManagerName()) : "<no-adapter>";
  const std::size_t camera_count = adapter_ ? adapter_->cameraCount() : 0;
  const std::size_t node_count = adapter_ ? adapter_->sceneNodeChildCount() : 0;
  const unsigned long long frame_count = adapter_ ? adapter_->frameCount() : 0;
  const bool executor_ready = executor_ != nullptr;
  const bool on_render_thread = executor_ ? executor_->is_render_thread() : false;
  const bool manager_ready = manager_ != nullptr;

  // Drive the mock runtime + reusable library end to end.
  onStartQar();
  bool add_ok = false;
  const QarPeerId id = addMockPeerInternal(&add_ok);
  const QString peer_hex = QString::fromStdString(qar_ogre_streaming::peer_id_to_hex(id));
  const bool stream_start_ok = startStreamInternal(id);
  const std::size_t streams_after_start = manager_ ? manager_->stream_count() : 0;
  const bool stream_stop_ok = stopStreamInternal(id);
  const bool remove_ok = removePeerInternal(id);
  const std::size_t streams_after_remove = manager_ ? manager_->stream_count() : 0;

  const bool overall_pass = ctx_valid && scene_valid && manager_ready && add_ok &&
    stream_start_ok && stream_stop_ok && remove_ok && streams_after_remove == 0;

  QString report;
  QTextStream ts(&report);
  ts << "# Qar RViz2 streaming panel - auto diagnostics dump (M3)\n";
  ts << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
  ts << "panel_loaded=true\n";
  ts << "context_valid=" << (ctx_valid ? "true" : "false") << "\n";
  ts << "scene_manager_valid=" << (scene_valid ? "true" : "false") << "\n";
  ts << "scene_manager_name=" << sm_name << "\n";
  ts << "camera_count=" << static_cast<qulonglong>(camera_count) << "\n";
  ts << "scene_node_children=" << static_cast<qulonglong>(node_count) << "\n";
  ts << "rviz_frame_count=" << frame_count << "\n";
  ts << "render_thread_executor_ready=" << (executor_ready ? "true" : "false") << "\n";
  ts << "on_render_thread=" << (on_render_thread ? "true" : "false") << "\n";
  ts << "library_manager_ready=" << (manager_ready ? "true" : "false") << "\n";
  ts << "qar_runtime_started=" << (qar_running_ ? "true" : "false") << "\n";
  ts << "mock_peer_added=" << (add_ok ? "true" : "false") << "\n";
  ts << "mock_peer_id=" << peer_hex << "\n";
  ts << "stream_started=" << (stream_start_ok ? "true" : "false") << "\n";
  ts << "stream_count_after_start=" << static_cast<qulonglong>(streams_after_start) << "\n";
  ts << "stream_stopped=" << (stream_stop_ok ? "true" : "false") << "\n";
  ts << "mock_peer_removed=" << (remove_ok ? "true" : "false") << "\n";
  ts << "stream_count_after_remove=" << static_cast<qulonglong>(streams_after_remove) << "\n";
  ts << "last_error=" << (last_error_text_.isEmpty() ? QString("<none>") : last_error_text_)
     << "\n";
  ts << "overall_pass=" << (overall_pass ? "true" : "false") << "\n";
  ts.flush();

  appendLog(report);

  const QString path = qEnvironmentVariable("QAR_PANEL_AUTODUMP");
  if (!path.isEmpty()) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      file.write(report.toUtf8());
      file.close();
      appendLog(QString("AUTODUMP written to %1").arg(path));
    } else {
      appendLog(QString("AUTODUMP FAILED to open %1 for writing").arg(path));
    }
  }

  // Flush pending events, then quit RViz cleanly.
  QTimer::singleShot(250, QCoreApplication::instance(), &QCoreApplication::quit);
}

// ---------------------------------------------------------------------------
// Small UI helpers
// ---------------------------------------------------------------------------
void QarStreamingPanel::emitHeartbeat()
{
  if (heartbeat_period_s_ <= 0.0) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (heartbeat_last_.time_since_epoch().count() == 0) {
    heartbeat_last_ = now;
    heartbeat_last_fires_ = render_timer_fires_;
    return;
  }
  const double elapsed_s =
    std::chrono::duration<double>(now - heartbeat_last_).count();
  if (elapsed_s < heartbeat_period_s_) {
    return;
  }

  const std::uint64_t fires = render_timer_fires_ - heartbeat_last_fires_;
  // Ticks actually served per second of WALL time. This is the number the 60 FPS
  // question turns on: it is immune to how fast one render is, and a timer that is
  // merely being under-served (Qt::CoarseTimer landing on the ~15.6 ms Windows
  // system tick) reads here as ~32 even when each batch costs almost nothing.
  const double tick_fps =
    elapsed_s > 0.0 ? static_cast<double>(fires) / elapsed_s : 0.0;

  QString line = QString("HB dt=%1s ticks=%2 tick_fps=%3 max_gap=%4ms")
    .arg(elapsed_s, 0, 'f', 2)
    .arg(static_cast<qulonglong>(fires))
    .arg(tick_fps, 0, 'f', 1)
    .arg(tick_max_gap_ms_, 0, 'f', 1);

  if (manager_ != nullptr) {
    const auto snap = manager_->diagnostics_snapshot();
    line += QString(" streams=%1 batch_avg=%2ms budget_skips=%3")
      .arg(snap.stream_count)
      .arg(snap.avg_batch_ms, 0, 'f', 2)
      .arg(static_cast<qulonglong>(snap.budget_skipped_frames));
    for (const auto & sd : snap.streams) {
      line += QString(" | %1 fps=%2 sub=%3 skip=%4 rend=%5ms")
        .arg(QString::fromStdString(sd.peer_id_hex).left(8))
        .arg(sd.achieved_fps, 0, 'f', 1)
        .arg(static_cast<qulonglong>(sd.frames_submitted))
        .arg(static_cast<qulonglong>(sd.frames_skipped))
        .arg(sd.avg_render_ms, 0, 'f', 2);
    }
  }
  appendLog(line);

  heartbeat_last_ = now;
  heartbeat_last_fires_ = render_timer_fires_;
  tick_max_gap_ms_ = 0.0;
}

void QarStreamingPanel::appendLog(const QString & line)
{
  const QString stamped =
    QDateTime::currentDateTime().toString("HH:mm:ss.zzz ") + line;
  if (diagnostics_view_ != nullptr) {
    diagnostics_view_->appendPlainText(stamped);
  }
  // Mirror every panel log line to stderr so the run script's rviz_live_*.err.log
  // carries the same record. Without this the panel's diagnostics live only in the
  // GUI text box: unreadable after the fact, impossible to diff between runs, and
  // absent from exactly the file the live-debug loops are supposed to be read from.
  std::fprintf(stderr, "[qar-panel] %s\n", stamped.toUtf8().constData());
  std::fflush(stderr);
}

void QarStreamingPanel::setLastError(const QString & error)
{
  last_error_text_ = error;
  if (last_error_label_ != nullptr) {
    last_error_label_->setText("Last error: " + error);
    last_error_label_->setStyleSheet("color: #c0392b;");
  }
  appendLog("ERROR: " + error);
}

void QarStreamingPanel::clearLastError()
{
  last_error_text_.clear();
  if (last_error_label_ != nullptr) {
    last_error_label_->setText("Last error: <none>");
    last_error_label_->setStyleSheet(QString());
  }
}

void QarStreamingPanel::refreshContextStatus()
{
  if (context_status_label_ == nullptr) {
    return;
  }
  if (adapter_) {
    context_status_label_->setText(QString::fromStdString(adapter_->statusSummary()));
  } else {
    context_status_label_->setText("DisplayContext: (adapter not created)");
  }
}

void QarStreamingPanel::updateRuntimeStateLabel()
{
  if (runtime_state_label_ == nullptr) {
    return;
  }
  // CONNECTING is its own state: the session is being built on a worker thread and
  // the panel stays interactive throughout, so reporting STOPPED for those ~3 s
  // would read as "the click did nothing".
  if (pending_connect_) {
    runtime_state_label_->setText("Qar runtime: CONNECTING...");
    runtime_state_label_->setStyleSheet("font-weight: bold; color: #d35400;");
    return;
  }
  runtime_state_label_->setText(
    qar_running_
    ? (live_connected_ ? "Qar runtime: RUNNING (live)" : "Qar runtime: RUNNING (mock)")
    : "Qar runtime: STOPPED");
  runtime_state_label_->setStyleSheet(
    qar_running_ ? "font-weight: bold; color: #27ae60;" : "font-weight: bold;");
}

void QarStreamingPanel::refreshPeerList()
{
  if (peer_list_ == nullptr) {
    return;
  }

  QarPeerId previous{};
  const bool had_selection = selectedPeerId(previous);

  peer_list_->clear();
  for (const RvizPeerEntry & entry : peer_model_.peers()) {
    QString text = QString::fromStdString(entry.label) +
      QString("  [%1x%2 @ %3fps]").arg(entry.width).arg(entry.height).arg(entry.fps);
    if (entry.streaming) {
      text += "  STREAMING";
    }
    auto * item = new QListWidgetItem(text, peer_list_);
    const QByteArray id_bytes(
      reinterpret_cast<const char *>(entry.id.data), static_cast<int>(sizeof(entry.id.data)));
    item->setData(Qt::UserRole, id_bytes);
    if (had_selection && qar_ogre_streaming::peer_id_equal(entry.id, previous)) {
      peer_list_->setCurrentItem(item);
    }
  }
}

bool QarStreamingPanel::selectedPeerId(QarPeerId & out_id) const
{
  if (peer_list_ == nullptr) {
    return false;
  }
  QListWidgetItem * item = peer_list_->currentItem();
  if (item == nullptr) {
    return false;
  }
  const QByteArray bytes = item->data(Qt::UserRole).toByteArray();
  if (bytes.size() != static_cast<int>(sizeof(out_id.data))) {
    return false;
  }
  std::memcpy(out_id.data, bytes.constData(), sizeof(out_id.data));
  return true;
}

}  // namespace qar_rviz_streaming

PLUGINLIB_EXPORT_CLASS(qar_rviz_streaming::QarStreamingPanel, rviz_common::Panel)
