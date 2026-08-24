// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__QAR_STREAMING_PANEL_HPP_
#define QAR_RVIZ_STREAMING__QAR_STREAMING_PANEL_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include <QString>

#include "rviz_common/panel.hpp"

#include "qar_ogre_streaming/diagnostics.hpp"
#include "qar_ogre_streaming/mock_qar_frame_info_source.hpp"
#include "qar_ogre_streaming/ogre_qar_streaming_manager.hpp"
#include "qar_ogre_streaming/ogre_stereo_render_target.hpp"
#include "qar_ogre_streaming/qar_render_sender_adapter.hpp"
#include "qar_ogre_streaming/qar_scene_lighting.hpp"
#include "qar_ogre_streaming/types.hpp"

#include "qar_rviz_streaming/rviz_peer_panel_model.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTimer;

namespace qar_qar_adapter
{
class QarCApiSenderFactory;
}  // namespace qar_qar_adapter

namespace qar_rviz_streaming
{

class RVizDisplayContextAdapter;
class RVizRenderThreadExecutor;
class RVizRenderLock;
class AnimatedMockSenderFactory;

/// @brief The Qar Streaming RViz2 panel.
///
/// M3 skeleton. It is an rviz_common::Panel (a QWidget) exported through
/// pluginlib. On load it obtains the DisplayContext + Ogre::SceneManager via
/// RVizDisplayContextAdapter, wires the reusable qar_ogre_streaming library
/// (OgreQarStreamingManager) with an RVizRenderThreadExecutor and a mock Qar
/// sender factory, and drives a MOCK runtime/peer state machine from the UI.
///
/// Per contractor answer C6 there is intentionally NO authorization UI
/// (approve/deny/denylist/audit). The peer id only routes/identifies a stream.
///
/// The panel also implements OgreQarDiagnosticsSink so library diagnostics land
/// in the on-screen log (marshalled onto the GUI thread when emitted off it).
///
/// Non-interactive verification: if the QAR_PANEL_AUTODUMP environment variable
/// names a file, the panel — a few seconds after load — writes a diagnostics
/// snapshot (context validity, scene manager name, camera/node counts, and a
/// mock peer add/stream/remove round-trip result) to that file and then quits
/// RViz cleanly. This gives scriptable proof without a human clicking.
class QarStreamingPanel : public rviz_common::Panel,
  public qar_ogre_streaming::OgreQarDiagnosticsSink
{
  Q_OBJECT

public:
  explicit QarStreamingPanel(QWidget * parent = nullptr);
  ~QarStreamingPanel() override;

  /// @brief Called by RViz once the DisplayContext is available.
  void onInitialize() override;

  // --- OgreQarDiagnosticsSink ---------------------------------------------
  void on_message(
    qar_ogre_streaming::DiagnosticSeverity severity,
    const std::string & category,
    const std::string & message,
    const std::string & peer_id_hex) override;

private Q_SLOTS:
  void onStartQar();
  void onStopQar();
  void onAddPeer();
  void onRemovePeer();
  void onStartStream();
  void onStopStream();
  void onRefreshDiagnostics();
  void onCaptureOffscreenFrame();
  void onCaptureStereoPair();
  void onRenderTick();
  void runAutoDump();
  void runAutoCapture();
  void runAutoStereo();
  void runAutoMultiPeer();
  void runGlSpike();

private:
  void buildUi();
  void appendLog(const QString & line);
  /// @brief Emit the periodic tick/FPS heartbeat if the period has elapsed.
  void emitHeartbeat();
  /// @brief Adopt a finished background connect, if one has completed.
  void pollPendingConnect();
  /// @brief Qt-thread wiring once qar_factory_ exists (manager, state, logs).
  void finishLiveConnect();
  void setLastError(const QString & error);
  void clearLastError();
  void refreshContextStatus();
  void refreshPeerList();
  void updateRuntimeStateLabel();

  // Selected-peer helpers.
  bool selectedPeerId(QarPeerId & out_id) const;

  // --- LIVE streaming to the QarOS runtime ---------------------------------
  /// When QAR_PANEL_LIVE_STREAM is set, the panel is a PRODUCER for QarOS: it uses
  /// the QAR C API (QarCApiSenderFactory) as its sender factory instead of the
  /// animated mock factory. Each render tick it opens streams for peers that
  /// requested one (rendering the LIVE RViz scene from each peer's head pose) and
  /// closes streams the runtime reported as gone.
  void driveLiveStreaming();

  // Internal operations (used both by the slots and by runAutoDump so results
  // can be captured for the diagnostics dump).
  QarPeerId addMockPeerInternal(bool * ok);
  bool startStreamInternal(const QarPeerId & id);
  bool stopStreamInternal(const QarPeerId & id);
  bool removePeerInternal(const QarPeerId & id);

  // --- Offscreen capture (M4) ---------------------------------------------
  /// Render the live RViz view into the offscreen color+depth targets and write
  /// PNGs (color, depth visualization) + raw F32 depth + a stats file, all with
  /// the given path prefix. MUST end up running on the Qt render thread; this
  /// helper marshals via the executor if called off it. Returns true on success
  /// and fills @p summary with a human-readable one-line result.
  bool captureOffscreenToPrefix(const QString & prefix, QString & summary);

  // --- Single-peer stereo streaming (M5) ----------------------------------
  /// Build/refresh the deterministic animated stereo frame source from the
  /// current render settings + IPD (env QAR_STEREO_IPD_M overrides the default).
  void rebuildFrameSource();

  // --- Scene lighting (M10-live) ------------------------------------------
  /// Assemble the QAR lighting rig's config from the QAR_SCENE_LIGHT* env vars.
  /// Defaults differ by mode: LIVE streaming gets the omni rig with RViz's
  /// camera-tracking headlight suppressed (that headlight is what made a remote
  /// peer's shading swing around as the local operator orbited); the mock/scripted
  /// paths default to OFF so the M4-M6 capture proofs keep their existing lighting.
  qar_ogre_streaming::SceneLightingConfig buildSceneLightingConfig() const;

  /// Invite the device selected in the peer-kind combo into the live session — one
  /// invited device per physical client head. Uses the hostname field for HoloLens,
  /// the only device type SDK v0.1.1 can address by name. Logs the outcome; no-op
  /// unless live and connected.
  void inviteSelectedPeer();

  /// Render one tick of every active streaming stream (initialize targets on
  /// first use), on the render thread under the RViz render lock. @return true if
  /// at least one stream rendered+submitted this tick.
  bool renderActiveStreamsOnce();

  /// Read back the peer's left+right eye color+depth, write the pair of PNGs
  /// (+ raw depth) under @p prefix, and a metadata/parallax stats file. Must be
  /// invoked on the render thread. @return true if both eyes wrote content and
  /// the near depth band shows more horizontal parallax than the far band.
  bool captureStereoPair(
    const QarPeerId & peer_id, const QString & prefix, QString & summary);

  /// Read back one peer's LEFT-eye color and write it as a PNG under @p prefix.
  /// Proof-of-independent-views helper for the multi-peer scripted run. Must be
  /// invoked on the render thread (targets already rendered this batch).
  bool capturePeerColorPng(
    const QarPeerId & peer_id, const QString & path, QString & summary);

  // --- OpenGL zero-copy texture-handoff spike (M7 Gate B) -----------------
  /// Render the live RViz view into the offscreen color target, then run the
  /// in-process zero-copy spike (native id extraction + glIsTexture/dims +
  /// independent raw-GL readback vs Ogre readback + GL fence create/wait) on the
  /// live RViz GL context, on the render thread, inside lockRender. Writes the
  /// greppable spike log line + a stats file under @p prefix. @return the spike's
  /// roundtrip_match (zero-copy proven).
  bool runGlSpikeToPrefix(const QString & prefix, QString & summary);

  struct RenderSettings
  {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t fps = 60;
  };
  RenderSettings currentSettings() const;

  // --- UI widgets (owned by the Qt parent/child tree) ----------------------
  QLabel * runtime_state_label_ = nullptr;
  QPushButton * start_qar_button_ = nullptr;
  QPushButton * stop_qar_button_ = nullptr;
  QPushButton * add_peer_button_ = nullptr;
  QPushButton * remove_peer_button_ = nullptr;
  QPushButton * start_stream_button_ = nullptr;
  QPushButton * stop_stream_button_ = nullptr;
  QPushButton * refresh_button_ = nullptr;
  QPushButton * capture_button_ = nullptr;
  QPushButton * stereo_capture_button_ = nullptr;
  QComboBox * resolution_combo_ = nullptr;
  QComboBox * fps_combo_ = nullptr;
  /// LIVE mode only (hidden otherwise): which device type "Invite peer" invites, and
  /// the hostname HoloLens needs — it is the only device type SDK v0.1.1 can address
  /// by name.
  QComboBox * peer_kind_combo_ = nullptr;
  QLineEdit * peer_hostname_edit_ = nullptr;
  QListWidget * peer_list_ = nullptr;
  QLabel * context_status_label_ = nullptr;
  QPlainTextEdit * diagnostics_view_ = nullptr;
  QLabel * last_error_label_ = nullptr;
  QString last_error_text_;

  // --- Library wiring (declaration order matters for destruction) ----------
  std::unique_ptr<RVizDisplayContextAdapter> adapter_;
  std::shared_ptr<RVizRenderThreadExecutor> executor_;
  // Render lock (IRenderLock) forwarding to the DisplayContext; the scheduler
  // holds a copy, so it must be constructed after adapter_ and outlive manager_.
  std::shared_ptr<RVizRenderLock> render_lock_;
  // The concrete mock factory (typed handle so the panel can set the base config
  // and query per-peer sources); also passed to the manager as the base factory.
  std::shared_ptr<AnimatedMockSenderFactory> mock_factory_;
  // Live-stream sender factory backed by the QarOS streaming C API.
  // Non-null only in QAR_PANEL_LIVE_STREAM mode; then it (not mock_factory_) is the
  // manager's sender factory.
  std::shared_ptr<qar_qar_adapter::QarCApiSenderFactory> qar_factory_;
  // live_stream_mode_: this is a live-capable panel (QAR_PANEL_LIVE_STREAM set in a
  // real-SDK build). It does NOT mean connected — the panel connects to the QarOS
  // runtime only when the user clicks "Connect to QarOS" (onStartQar), never at load
  // (M10: connecting during onInitialize froze RViz and ran the zenoh/peer handshake
  // before the Qt event loop was live). qar_factory_ stays null until connected, so
  // driveLiveStreaming() is a no-op meanwhile.
  bool live_stream_mode_ = false;
  bool live_connected_ = false;

  // --- Asynchronous connect (M10) -----------------------------------------
  // Building the session is a straight line of BLOCKING SDK calls, and the two
  // that matter cannot be moved off the click: measured 2026-08-22, of 2884 ms
  // total, qar_runtime_create_session took 2071 ms (it spawns the runtime
  // launcher and waits for it) and qar_session_join 689 ms. Everything else
  // together is 19 ms, so there is nothing worth hoisting to panel construction.
  //
  // The SDK offers no async bootstrap, so the build runs on a worker thread and
  // the render tick polls for it. The handoff is strictly sequential — the
  // worker publishes with release, the Qt thread acquires and from then on is
  // the ONLY thread that calls into the session — so we never use the runtime
  // concurrently, which is what api-conventions.mdx cautions against.
  struct PendingConnect
  {
    std::atomic<bool> done{false};
    std::shared_ptr<qar_qar_adapter::QarCApiSenderFactory> factory;
    std::string error;
    bool threw = false;
  };
  std::shared_ptr<PendingConnect> pending_connect_;
  std::thread connect_thread_;

  // --- Geolocation -> app volume world anchor (M10) ------------------------
  // QarOS georeferences an app volume with a QarGeoAnchorFrame: an ECEF WGS84
  // origin plus the local frame's axes. ROS carries that as sensor_msgs/NavSatFix
  // (lat/lon/alt), which the adapter converts.
  //
  // The subscription callback runs on RViz's executor, so it only STORES the fix;
  // the render tick applies it. That keeps every QAR call on the Qt thread, which
  // is what api-conventions.mdx asks for ("single-threaded use per runtime").
  //
  // Deliberately NOT read from RViz's FrameManager, even though a REP-105
  // earth->map transform would be ECEF already: FrameManager hands back Ogre
  // float poses relative to the CURRENT fixed frame, and float32 across a ~6.4e6 m
  // ECEF radius quantises to roughly half a metre.
  rclcpp::Node::SharedPtr ros_node_;
  rclcpp::Subscription<sensor_msgs::msg::NavSatFix>::SharedPtr gps_sub_;
  std::mutex gps_mutex_;
  double gps_lat_ = 0.0;
  double gps_lon_ = 0.0;
  double gps_alt_ = 0.0;
  bool gps_have_fix_ = false;      ///< guarded by gps_mutex_
  bool gps_fix_pending_ = false;   ///< a new fix awaits the render tick
  bool geo_enabled_ = true;        ///< QAR_PANEL_GEO=off disables the whole path
  bool geo_applied_ = false;       ///< an anchor has been pushed at least once
  double geo_last_lat_ = 0.0;      ///< fix the current anchor was built from
  double geo_last_lon_ = 0.0;
  double geo_last_alt_ = 0.0;
  std::string gps_topic_;          ///< QAR_PANEL_GPS_TOPIC, default /gps/fix
  /// Re-anchor only when the fix moves more than this, so a 10 Hz receiver does
  /// not drive an SDK call per message for sub-metre jitter.
  static constexpr double kGeoReanchorMetres = 2.0;
  void setupGeoSubscription();
  void applyPendingGeoFix();
  /// Sender creation is asynchronous, so "no sender yet" happens on most early ticks.
  /// Log that once per session instead of once per tick.
  bool live_sender_pending_logged_ = false;
  // Runtime-binaries dir captured at onInitialize (Finding 11); used to build the
  // QarCApiSenderFactory::Config lazily on connect. Stored as a plain string so the
  // panel header need not include the QAR C API (QarCApiSenderFactory stays a
  // forward declaration; the full type is only used in the .cpp).
  std::string qar_runtime_binaries_dir_;
  std::shared_ptr<qar_ogre_streaming::IQarRenderSenderFactory> sender_factory_;
  // QAR lighting rig for the shared RViz scene. Declared BEFORE manager_ so it is
  // destroyed AFTER it (members die in reverse declaration order) — the manager's
  // dtor already calls shutdown() on it, and this ordering keeps that valid.
  std::shared_ptr<qar_ogre_streaming::QarSceneLighting> scene_lighting_;
  std::unique_ptr<qar_ogre_streaming::OgreQarStreamingManager> manager_;

  // Offscreen render target for the M4 capture path. Declared AFTER adapter_/
  // manager_ so it is destroyed first (its dtor releases Ogre resources while the
  // RViz scene manager is still valid).
  std::unique_ptr<qar_ogre_streaming::OgreStereoRenderTarget> offscreen_rt_;

  // --- M5 stereo streaming state ------------------------------------------
  // The deterministic animated per-eye pose/FOV source shared with the mock
  // sender factory. Drives every mock peer's stereo stream.
  std::shared_ptr<qar_ogre_streaming::MockQarFrameInfoSource> frame_source_;
  // Periodic render tick (~30 FPS) on the Qt/render thread; renders active
  // streaming streams (no-op unless streaming is active with an active stream).
  QTimer * render_timer_ = nullptr;
  std::uint64_t stereo_render_ticks_ = 0;

  // --- Live tick heartbeat (M10 debug loops) -------------------------------
  // Periodic one-line health record. `tick_max_gap_ms_` is the freeze detector:
  // any stall of the Qt thread shows up as an inter-tick gap far above the 16 ms
  // interval, recorded whether or not anyone was watching the window at the time.
  // That is the objective evidence a "did it freeze?" test needs — a snapshot
  // taken before and after an event cannot prove nothing stalled in between.
  double heartbeat_period_s_ = 2.0;  ///< 0 disables; QAR_PANEL_HEARTBEAT_S.
  std::chrono::steady_clock::time_point heartbeat_last_{};
  std::chrono::steady_clock::time_point tick_last_{};
  std::uint64_t heartbeat_last_fires_ = 0;
  std::uint64_t render_timer_fires_ = 0;  ///< Raw timer fires, incl. no-op ticks.
  double tick_max_gap_ms_ = 0.0;
  bool tick_gap_valid_ = false;

  RvizPeerPanelModel peer_model_;
  bool qar_running_ = false;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__QAR_STREAMING_PANEL_HPP_
