// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__QAR_STREAMING_PANEL_HPP_
#define QAR_RVIZ_STREAMING__QAR_STREAMING_PANEL_HPP_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include <QString>

#include "rviz_common/display.hpp"
#include "rviz_common/display_group.hpp"
#include "rviz_common/panel.hpp"

#include "qar_ogre_streaming/diagnostics.hpp"
#include "qar_ogre_streaming/mock_qar_frame_info_source.hpp"
#include "qar_ogre_streaming/ogre_qar_streaming_manager.hpp"
#include "qar_ogre_streaming/ogre_stereo_render_target.hpp"
#include "qar_ogre_streaming/qar_render_sender_adapter.hpp"
#include "qar_ogre_streaming/qar_scene_lighting.hpp"
#include "qar_ogre_streaming/types.hpp"

#include <QPointer>

#include <utility>

#include "qar_qt_streaming/qt_panel_stream_manager.hpp"
#include "qar_qt_streaming/video_panel_widget.hpp"

// The Windows SDK defines `interface` as a macro for `struct` (combaseapi.h,
// reached here through rclcpp -> rcutils -> windows.h). Both qar_ui_model's
// TypeNameParts and qar_ros_ui's TypeSupportRegistry have a field named
// `interface` — the ROS word for msg/srv/action — so the macro has to go before
// their headers are read. Nothing in this plugin uses the COM alias, and
// the SDK headers included later are free to define it again.
#ifdef interface
#undef interface
#endif

// The ROS graph -> generated AR panel path. The three reusable libraries
// join HERE and nowhere else: qar_ros_ui produces/consumes UiValues, qar_ui_model
// is the model both speak, and qar_web_panels serves the page QarOS's browser
// loads. All three are Qt/ROS-boundary-checked libraries this package consumes
// through their public APIs only.
#include "qar_ros_ui/action_bridge.hpp"
#include "qar_ros_ui/graph_discovery.hpp"
#include "qar_ros_ui/service_bridge.hpp"
#include "qar_ros_ui/topic_bridge.hpp"

#include "qar_web_panels/panel_session.hpp"
#include "qar_ar_core/qar_url_panel.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"
#include "qar_web_panels/web_panel_server.hpp"

#include "qar_rviz_streaming/add_topic_dialog.hpp"
#include "qar_rviz_streaming/ar_control_router.hpp"
#include "qar_rviz_streaming/ar_exposure_registry.hpp"
#include "qar_rviz_streaming/ar_panel_generator.hpp"
#include "qar_rviz_streaming/display_class_catalog.hpp"
#include "qar_rviz_streaming/grid_floor_plane.hpp"
#include "qar_rviz_streaming/ros_graph_tree.hpp"
#include "qar_rviz_streaming/ros_image_frame_source.hpp"
#include "qar_rviz_streaming/ros_topic_tree.hpp"
#include "qar_rviz_streaming/rviz_peer_panel_model.hpp"
#include "qar_rviz_streaming/spatial_interaction.hpp"
#include "qar_rviz_streaming/video_source_executor.hpp"
#include "qar_rviz_streaming/windows_scheduling.hpp"

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTimer;

namespace qar_qar_adapter
{
class QarCApiSenderFactory;
class QarCApiPanelSenderFactory;
class QarCApiPanelInputSource;
class QarCApiSpatialGestureSource;
class QarCApiUrlPanelFactory;
}  // namespace qar_qar_adapter

namespace qar_rviz_streaming
{

class RVizDisplayContextAdapter;
class RVizRenderThreadExecutor;
class RVizRenderLock;
class AnimatedMockSenderFactory;

/// @brief The Qar Streaming RViz2 panel.
///
/// It is an rviz_common::Panel (a QWidget) exported through
/// pluginlib. On load it obtains the DisplayContext + Ogre::SceneManager via
/// RVizDisplayContextAdapter and wires the reusable qar_ogre_streaming library
/// (OgreQarStreamingManager) with an RVizRenderThreadExecutor. Without
/// QAR_PANEL_LIVE_STREAM the panel is an offline preview: a simulated Qar
/// sender factory and a simulated runtime/peer state machine driven from the UI.
/// With it, the panel streams to the QarOS runtime (see driveLiveStreaming()).
///
/// There is intentionally NO authorization UI (approve/deny/denylist/audit).
/// The peer id only routes/identifies a stream.
///
/// The panel also implements OgreQarDiagnosticsSink so library diagnostics land
/// in the on-screen log (marshalled onto the GUI thread when emitted off it).
///
/// Non-interactive verification: if the QAR_PANEL_AUTODUMP environment variable
/// names a file, the panel — a few seconds after load — writes a diagnostics
/// snapshot (context validity, scene manager name, camera/node counts, and a
/// simulated peer add/stream/remove round-trip result) to that file and then quits
/// RViz cleanly. This gives scriptable proof without a human clicking.
class QarStreamingPanel : public rviz_common::Panel,
  public qar_ogre_streaming::OgreQarDiagnosticsSink,
  public qar_ar_core::PanelDiagnosticsSink,
  public qar_web_panels::IPanelControlSink,
  public qar_ros_ui::RosUiDiagnosticsSink
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

  // --- PanelDiagnosticsSink ----------------------------------------------
  /// @brief The Qt panel manager's diagnostics (a panel added, a dialog promoted,
  /// a panel closed, a stream that never served a frame), attached through
  /// `QtPanelStreamingConfig::diagnostics` and forwarded to @ref appendLog and so
  /// to the run script's `.err.log`.
  void on_message(
    qar_ar_core::DiagnosticSeverity severity,
    const std::string & category,
    const std::string & message,
    const std::string & panel_key) override;

  // --- RosUiDiagnosticsSink ------------------------------------------------
  void on_message(
    qar_ros_ui::DiagnosticSeverity severity,
    const std::string & category,
    const std::string & message) override;

  // --- Spatial AR interaction -----------------------------------------------
  /// @brief Hand one AR pointer sample to the spatial picker.
  ///
  /// THE entry point for 3D interaction, and deliberately the same
  /// `PanelPointerSample` the panels' input path uses: whatever produces samples
  /// — the QarOS app-volume gesture callback, a hand-ray source, or a test —
  /// pushes here and the armed mode does the rest. Thread-safe: the sample is
  /// queued and drained on the Qt thread by the render tick.
  void submitSpatialSample(const qar_ar_core::PanelPointerSample & sample);

  /// @brief The armed spatial mode, for a host that wants to drive it directly.
  SpatialInteractionController & spatialInteraction() { return spatial_; }

  // --- IPanelControlSink ---------------------------------------------------
  /// A control event from a browser in the headset. Runs on the session's
  /// thread, which is this panel's Qt GUI thread: the router turns it into a
  /// ROS operation, the bridges execute it, and the viewer is acked either way.
  void on_control(
    qar_web_panels::PanelSession & session,
    const qar_web_panels::PanelControlEvent & event) override;

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
  /// @brief Render-tick combo changed (QAR_PANEL_TICK_MS): applies the new
  /// period via applyTickPeriodMs().
  void onTickComboChanged(int index);
  /// @brief "Prime next frame" checkbox toggled (QAR_ADAPTER_PRIME): forward
  /// to the manager; applies at each stream's next show_frame.
  void onPrimeCheckboxToggled(bool checked);
  void runAutoDump();
  void runAutoCapture();
  void runAutoStereo();
  void runAutoMultiPeer();

  // --- ROS topic browser + AR GUI panels -----------------------------------
  void onAddTopic();
  void onRemoveWatchedTopic();

  // --- Exposed AR tools ----------------------------------------------------
  void onRemoveExposure();
  void onReopenExposure();
  void onCopyExposureUrl();
  void onStreamSelectedPanel();
  void onStopSelectedPanel();
  void onRefreshStreamablePanels();

  // --- Spatial AR interaction ----------------------------------------------
  void onSpatialModeChanged(int index);
  void onSpatialTargetChanged(int index);
  void onSpatialConfirm();
  void onSpatialCancel();
  void onSpatialClosePolygon();

private:
  void buildUi();

  // --- ROS topic browser, AR GUI panel and AR tool helpers -----------------
  /// @brief Topics, services AND actions, from qar_ros_ui::GraphDiscovery.
  /// Actions are not a graph entity and have to be recovered from their
  /// protocol, which is exactly what that component does (and cross-checks
  /// against rcl_action).
  std::vector<RosGraphEntry> queryRosGraph();

  /// @brief Create the ROS node handles + the three bridges, once.
  /// @return empty on success, else a human-readable reason.
  QString ensureRosBridges();

  /// @brief Start the HTTP/WebSocket server that serves generated panels, once.
  QString ensureWebPanelServer();

  /// @brief Introspect, generate, serve and (later) open one AR tool.
  /// Everything QarOS-facing is deferred to the render tick, so this never
  /// blocks the Qt thread — the same rule the asynchronous connect follows.
  QString exposeEntity(const AddTopicSelection & selection);

  /// @brief Tear one exposure down: AR panel, session, subscription, registry row.
  void retireExposure(RosEntityKind kind, const QString & name);

  /// @brief Mirror the registry into exposure_list_.
  void refreshExposureList();

  /// @brief Per-tick work for the generated panels: open the QarOS panels that
  /// are still pending, push new ROS values at each panel's own cadence, and
  /// give the service/action bridges their timeout tick.
  void tickArToolPanels();

  /// @brief Currently selected row of exposure_list_, or nullptr.
  ExposedEntity * selectedExposure();

  /// @brief Build the message-type -> Display-class index once, from every
  /// rviz_common::Display plugin manifest pluginlib knows about.
  void ensureDisplayCatalog();

  /// @brief Instantiate the RViz Display for @p selection and point it at the
  /// topic. @return empty on success, else a human-readable reason.
  QString createDisplayForTopic(const AddTopicSelection & selection);

  /// @brief Add @p selection to the watch list, ignoring duplicates.
  void pinWatchedTopic(const AddTopicSelection & selection);

  /// @brief Bring up the Qt->QarOS panel manager once the QAR session is live.
  /// @return empty on success, else a human-readable reason.
  QString ensurePanelManager();

  /// @brief Repopulate the streamable-panel list: this panel first, then every
  /// other dock widget RViz currently hosts.
  void refreshStreamablePanels();

  /// @brief THE close for a panel the OPERATOR chose to stream — the "Stop streaming"
  /// button, the sweep for a destroyed widget and a closed RViz dock all end here, so the
  /// manager's close mode applies to each of them alike.
  ///
  /// Forgets the host's own bookkeeping (@ref streamed_panels_, the dock hook),
  /// then asks the manager to close the panel (QtPanelStreamManager::close_panel):
  /// by default the AR panel is closed and its video sender destroyed, under
  /// `QAR_PANEL_CLOSE_MODE=notice` it becomes the closed-window notice. Logs the
  /// outcome in words that say which one happened. Dialog panels are not closed
  /// here — the manager sweeps those itself.
  /// @param short_reason the phrase the manager's close line carries ("closed in
  ///        RViz", "stopped in RViz").
  /// @param notice_text what the notice says in notice mode (a full sentence).
  /// @return false if the manager was not streaming @p manager_key.
  bool closeStreamedPanel(
    const std::string & manager_key, const QString & short_reason, const QString & notice_text);

  /// @brief When @p widget lives in an RViz dock, close its AR panel when that
  /// dock is closed (its X button). RViz hosts every panel in an
  /// `rviz_common::PanelDockWidget` — an exported class whose `closed()` signal is
  /// emitted from its `closeEvent` — and without this hook the AR panel of a closed
  /// dock would go on streaming a blank rectangle. Re-showing the dock does NOT
  /// restart the stream: the operator streams it again with "Stream panel to AR".
  ///
  /// A widget with no such ancestor (an "Open video in AR" panel, which is
  /// parentless) is left alone. Replaces any hook already on @p manager_key.
  void watchDockClose(const std::string & manager_key, QWidget * widget);

  /// @brief Drop the dock hook for @p manager_key, if there is one.
  void unwatchDockClose(const std::string & manager_key);

  /// @brief Choose the display name a NEW QarOS panel is created under, and write it to
  /// `options.display_name`. Called right before `panel_manager_->add_panel()`, and not for a
  /// start that only revives a retired slot, which creates no QarOS panel. The first panel of
  /// this process under a name keeps it; every later one gets `<name> (2)`, `<name> (3)`, ...
  /// The reason, and what the numbering does not prevent, is in panel_display_names.hpp. One
  /// "AR PANEL:" log line when the name changes.
  /// @param key the panel's manager key: for that log line only, it never changes.
  /// @param options the options the panel is about to be added with.
  void applyUniqueDisplayName(
    const std::string & key, qar_qt_streaming::PanelStreamOptions & options);

  /// @brief Create the hidden RViz Displays panel (once) and stream it into AR as
  /// kHiddenDisplaysKey. Idempotent: calling it while that stream is live does
  /// nothing, and calling it after the stream is gone - "Stop streaming", or the operator
  /// closing the AR panel - brings it back.
  ///
  /// The widget is a NEW instance of RViz's Displays panel (class `rviz_common/Displays`,
  /// made through the main window's panel factory - see hidden_rviz_panel.hpp), bound to
  /// the same display tree, shown OFF-SCREEN at a fixed size (QAR_PANEL_HIDDEN_DISPLAYS_SIZE,
  /// default 480x720) so it can be captured and clicked but never appears on the desktop.
  /// Checkbox toggles, edits and Add/Remove/Rename made through it act on the real
  /// displays. It is never added to streamed_panels_ and stopping its stream never hides,
  /// closes or deletes the widget.
  ///
  /// Every failure is logged and the plugin carries on without the feature; nothing here
  /// can throw out.
  /// @return empty on success (including "already streaming"), else the reason.
  QString startHiddenDisplaysStream();

  /// @brief What finishLiveConnect() calls: startHiddenDisplaysStream() unless
  /// QAR_PANEL_HIDDEN_DISPLAYS says off (then it says so, once, and does nothing). The
  /// variable only switches the AUTOMATIC start off; "Stream panel to AR" on the entry in
  /// the panel list still works.
  void startHiddenDisplaysStreamOnConnect();

  /// @brief Show the hidden panel again if something hid it, and say so once. Called
  /// from tickArPanels(). A hidden Qt widget is not painted, so a hidden Displays copy
  /// would stream a blank rectangle.
  void keepHiddenDisplaysShown();

  /// @brief One capture/submit cycle for every streamed AR panel. Also retires
  /// streams whose source widget has been destroyed.
  void tickArPanels();

  /// Ask for one AR-panel input pump from a POSTED event, coalescing repeats.
  ///
  /// NOT called inline. Delivering an AR click runs arbitrary widget code, and
  /// RViz's own "Add" buttons answer with `QDialog::exec()` — a nested event
  /// loop that does not return until the dialog closes. Entered from inside
  /// onRenderTick() (a QTimerEvent), that loop makes Qt refuse to re-deliver
  /// THAT timer for as long as it runs, and the whole render tick dies with it:
  /// no panel capture, no stereo submit, a frozen headset. Entered from a posted
  /// event instead, the timer is not on the stack, keeps firing inside the
  /// nested loop, and the stream survives the dialog: the heartbeat shows no gap
  /// while a dialog is open. See qar_qt_streaming docs/ARCHITECTURE.md, "Popups
  /// and dialogs".
  void scheduleArPanelInputPump();

  /// @brief Refresh the AR-panel status line from the manager's diagnostics.
  void updateArPanelStatus();
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

  // --- Live streaming to the QarOS runtime ----------------------------------
  /// When QAR_PANEL_LIVE_STREAM is set, the panel is a PRODUCER for the QarOS
  /// runtime: it uses the QAR C API (QarCApiSenderFactory) as its sender factory
  /// instead of the simulated-peer factory. Each render tick it opens streams for
  /// the peers that requested one (rendering the LIVE RViz scene from each peer's
  /// head pose) and closes the ones the runtime closed. A no-op until connected.
  void driveLiveStreaming();

  // Internal operations (used both by the slots and by runAutoDump so results
  // can be captured for the diagnostics dump).
  QarPeerId addMockPeerInternal(bool * ok);
  bool startStreamInternal(const QarPeerId & id);
  bool stopStreamInternal(const QarPeerId & id);
  bool removePeerInternal(const QarPeerId & id);

  // --- Offscreen capture --------------------------------------------------
  /// Render the live RViz view into the offscreen color+depth targets and write
  /// PNGs (color, depth visualization) + raw F32 depth + a stats file, all with
  /// the given path prefix. MUST end up running on the Qt render thread; this
  /// helper marshals via the executor if called off it. Returns true on success
  /// and fills @p summary with a human-readable one-line result.
  bool captureOffscreenToPrefix(const QString & prefix, QString & summary);

  // --- Single-peer stereo streaming ---------------------------------------
  /// Build/refresh the deterministic animated stereo frame source from the
  /// current render settings + IPD (env QAR_STEREO_IPD_M overrides the default).
  void rebuildFrameSource();

  // --- Scene lighting -----------------------------------------------------
  /// Assemble the QAR lighting rig's config from the QAR_SCENE_LIGHT* env vars.
  /// Defaults differ by mode: LIVE streaming gets the omni rig with RViz's
  /// camera-tracking headlight suppressed (that headlight is what made a remote
  /// peer's shading swing around as the local operator orbited); the offline
  /// preview and the scripted captures default to OFF so they keep RViz's own
  /// lighting.
  qar_ogre_streaming::SceneLightingConfig buildSceneLightingConfig() const;

  /// Invite the device selected in the peer-kind combo into the running QarOS
  /// session — one invited device per physical client head. Uses the hostname field
  /// for HoloLens, the only device type the SDK can address by name. Logs the
  /// outcome; no-op unless live and connected.
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

  /// @brief Apply a new render-tick period (ms; fractional). Stops/reconfigures/
  /// restarts render_timer_ (Qt::PreciseTimer for ms >= 1; a zero-interval timer for
  /// ms < 1, since QTimer cannot represent a sub-1ms period directly), resets the
  /// zero-interval throttle, forwards the value to the manager (informational, for
  /// the frame-trace CSV) and logs the change. Called both from onInitialize() (the
  /// initial value) and onTickComboChanged() (a live change), so both paths share
  /// exactly one code path.
  void applyTickPeriodMs(double ms);

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
  /// Render-tick period selector (QAR_PANEL_TICK_MS): 16/8/4/2/1/0.1 ms
  /// (default 0.1 ms), plus a non-standard env-supplied value appended if one was
  /// given at startup. See applyTickPeriodMs() for the sub-1ms (0.1 ms) handling.
  QComboBox * tick_combo_ = nullptr;
  /// "Prime next frame" (QAR_ADAPTER_PRIME); default ON (checked).
  QCheckBox * prime_checkbox_ = nullptr;
  /// LIVE mode only (hidden otherwise): which device type "Invite peer" invites, and
  /// the hostname HoloLens needs — it is the only device type the SDK can address
  /// by name.
  QComboBox * peer_kind_combo_ = nullptr;
  QLineEdit * peer_hostname_edit_ = nullptr;
  QListWidget * peer_list_ = nullptr;

  // --- ROS topic browser widgets -------------------------------------------
  QListWidget * topic_watch_list_ = nullptr;
  QPushButton * add_topic_button_ = nullptr;
  QPushButton * remove_topic_button_ = nullptr;

  // --- Exposed AR tool widgets ---------------------------------------------
  QListWidget * exposure_list_ = nullptr;
  QPushButton * remove_exposure_button_ = nullptr;
  QPushButton * reopen_exposure_button_ = nullptr;
  QPushButton * copy_url_button_ = nullptr;
  /// Live size trim for AR web panels: a multiplier on the size derived from
  /// the page's own measurement (url_panel_sizing.hpp). Seeded from
  /// QAR_PANEL_WEB_SCALE and applied by "Re-open in AR", so the operator can
  /// tune a panel in a running session without a rebuild or a restart.
  QDoubleSpinBox * ar_panel_scale_spin_ = nullptr;
  QLabel * exposure_status_label_ = nullptr;
  QComboBox * ar_panel_combo_ = nullptr;
  QPushButton * refresh_panels_button_ = nullptr;
  QPushButton * stream_panel_button_ = nullptr;
  QPushButton * stop_panel_button_ = nullptr;
  /// The live "AR panels:" readout, and the viewport it is confined to.
  ///
  /// It lives in a scroll area for a reason that is not cosmetic. This label is
  /// inside the very widget that is streamed to AR, and it is rewritten several
  /// times a second with counters whose width and line count change. A plain
  /// QLabel would push its new size hint into the dock's layout, the dock would
  /// change width, QtPanelStream would see a new buffer size and rebuild the
  /// whole AR panel, and the rebuild would rewrite the label again -- an endless
  /// feedback loop. The scroll area pins the height and the horizontal size
  /// policy is Ignored, so no text this label ever holds can move the layout by
  /// a pixel; the text stays fully readable because the viewport scrolls instead
  /// of clipping.
  QScrollArea * ar_panel_status_scroll_ = nullptr;
  QLabel * ar_panel_status_label_ = nullptr;

  // --- Spatial interaction widgets -----------------------------------------
  QComboBox * spatial_mode_combo_ = nullptr;
  QComboBox * spatial_target_combo_ = nullptr;
  QLineEdit * spatial_frame_edit_ = nullptr;
  QCheckBox * spatial_auto_confirm_ = nullptr;
  QPushButton * spatial_confirm_button_ = nullptr;
  QPushButton * spatial_cancel_button_ = nullptr;
  QPushButton * spatial_close_polygon_button_ = nullptr;
  QLabel * spatial_status_label_ = nullptr;

  QLabel * context_status_label_ = nullptr;
  QPlainTextEdit * diagnostics_view_ = nullptr;
  QLabel * last_error_label_ = nullptr;
  QString last_error_text_;

  // --- Spatial AR interaction ----------------------------------------------
  /// The armed mode + the pick sessions. Every rule lives in this pure unit;
  /// the widgets above only set it and render its status.
  SpatialInteractionController spatial_;
  /// The one lock on the spatial input path, at the seam rather than in the
  /// core — the same shape the panels' input path has. A producer on the QarOS
  /// callback thread pushes; the render tick drains on the Qt thread.
  qar_ar_core::QueuedPanelInputSource spatial_input_;
  std::vector<qar_ar_core::PanelPointerSample> spatial_drain_buffer_;
  /// Publisher for the topic the current mode targets, created on first use and
  /// re-created when the destination changes.
  std::shared_ptr<qar_ros_ui::TopicPublisher> spatial_publisher_;
  std::string spatial_publisher_topic_;
  std::string spatial_publisher_type_;

  /// @brief Subscribe the session's app volume to AR pointer gestures.
  /// Idempotent: re-running it replaces any existing subscription.
  void startSpatialGestureSource();

  /// @brief Re-read where QarOS is displaying our scene, into @ref scene_placement_.
  ///
  /// The app volume's `app_pose` and `app_scale` stop being ours the moment the
  /// volume is created: our gesture configuration keeps the runtime's
  /// grab-to-move and pinch-to-zoom rules, which write exactly those two
  /// fields. So they are re-read live rather than assumed from what we passed
  /// at create.
  ///
  /// **This does NOT feed the picker's transform, and must not start to.**
  /// Gesture action points, the view pose and the hand joints are reported in
  /// APP space (the arm-length invariant shows it), so a pick needs the
  /// room->ROS basis change and no placement term. Applying the placement's
  /// inverse puts picks metres away from the target. The reasoning is written
  /// out at the call site and in qar_ar_core's `spatial_frames.hpp`.
  ///
  /// A failed read leaves the previous value in force — "we could not ask" is
  /// not "the scene is life size".
  void refreshScenePlacement();

  /// Per-tick: drain the queue into the controller, publish anything the
  /// operator has already authorized, and refresh the status line.
  void tickSpatialInteraction();
  void refreshSpatialUi();
  /// @brief "rx=.. queued=.. drop=..": the one line that says whether AR pointer
  /// gestures are reaching us at all, and where they stop if they are not.
  ///
  /// Shown in the Spatial status label AND in the heartbeat, because the two
  /// answer different questions: the label is what an operator reads while
  /// trying a gesture, the heartbeat is what a log shows afterwards.
  QString spatialCountersLine() const;
  /// Advertise if needed and publish. @return empty on success, else the reason.
  QString publishSpatialPick(const SpatialPublishRequest & request);

  // --- Library wiring (declaration order matters for destruction) ----------
  std::unique_ptr<RVizDisplayContextAdapter> adapter_;
  std::shared_ptr<RVizRenderThreadExecutor> executor_;
  // Render lock (IRenderLock) forwarding to the DisplayContext; the scheduler
  // holds a copy, so it must be constructed after adapter_ and outlive manager_.
  std::shared_ptr<RVizRenderLock> render_lock_;
  // The concrete simulated-peer factory (typed handle so the panel can set the base
  // config and query per-peer sources); also passed to the manager as the base factory.
  std::shared_ptr<AnimatedMockSenderFactory> mock_factory_;
  // Sender factory backed by the real QAR C API. Non-null only in
  // QAR_PANEL_LIVE_STREAM mode; then it (not mock_factory_) is the manager's
  // sender factory.
  std::shared_ptr<qar_qar_adapter::QarCApiSenderFactory> qar_factory_;
  // live_stream_mode_: this is a live-capable panel (QAR_PANEL_LIVE_STREAM set in a
  // real-SDK build). It does NOT mean connected — the panel connects to the QarOS
  // runtime only when the operator clicks "Connect to QarOS" (onStartQar), never at
  // load (connecting during onInitialize would freeze RViz and run the zenoh/peer
  // handshake before the Qt event loop is live). qar_factory_ stays null until
  // connected, so driveLiveStreaming() is a no-op meanwhile.
  bool live_stream_mode_ = false;
  bool live_connected_ = false;

  // --- Asynchronous connect -----------------------------------------------
  // Building the session is a straight line of BLOCKING SDK calls, and the two
  // that matter cannot be moved off the click: of about 2.9 s in total,
  // qar_runtime_create_session takes about 2.1 s (it spawns the runtime launcher
  // and waits for it) and qar_session_join about 0.7 s. Everything else together
  // is a few ms, so there is nothing worth hoisting to panel construction.
  //
  // The SDK offers no async bootstrap, so the build runs on a worker thread and
  // the render tick polls for it. The handoff is strictly sequential — the
  // worker publishes with release, the Qt thread acquires and from then on is
  // the ONLY thread that calls into the session — so the runtime is never used
  // concurrently, which the SDK's API conventions caution against.
  struct PendingConnect
  {
    std::atomic<bool> done{false};
    std::shared_ptr<qar_qar_adapter::QarCApiSenderFactory> factory;
    std::string error;
    bool threw = false;
  };
  std::shared_ptr<PendingConnect> pending_connect_;
  std::thread connect_thread_;

  // --- Geolocation -> app volume world anchor -------------------------------
  // QarOS georeferences an app volume with a QarGeoAnchorFrame: an ECEF WGS84
  // origin plus the local frame's axes. ROS carries that as sensor_msgs/NavSatFix
  // (lat/lon/alt), which the adapter converts.
  //
  // The subscription callback runs on RViz's executor, so it only STORES the fix;
  // the render tick applies it. That keeps every QAR call on the Qt thread, which
  // is what the SDK's API conventions ask for ("single-threaded use per runtime").
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
  // Runtime-binaries dir captured at onInitialize; it builds the
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

  // Offscreen render target for the offscreen capture path. Declared AFTER adapter_/
  // manager_ so it is destroyed first (its dtor releases Ogre resources while the
  // RViz scene manager is still valid).
  std::unique_ptr<qar_ogre_streaming::OgreStereoRenderTarget> offscreen_rt_;

  // Floor-plane feature: mirrors every rviz_default_plugins Grid
  // display with an opaque companion box, so the AR depth map sees a solid
  // floor instead of the gaps between grid lines. Same ordering reasoning as
  // offscreen_rt_ above: declared AFTER adapter_ so it is destroyed BEFORE it,
  // while the Ogre SceneManager/DisplayContext it holds raw pointers to is
  // still valid. Null when QAR_PANEL_FLOOR_PLANE=off or construction failed.
  std::unique_ptr<GridFloorPlaneManager> floor_plane_;

  // --- Stereo streaming state ---------------------------------------------
  // The deterministic animated per-eye pose/FOV source shared with the
  // simulated-peer sender factory. Drives every simulated peer's stereo stream.
  std::shared_ptr<qar_ogre_streaming::MockQarFrameInfoSource> frame_source_;
  // --- ROS topic browser + AR GUI panels -----------------------------------
  /// Message type -> RViz Display class, rebuilt from pluginlib manifests once.
  DisplayClassCatalog display_catalog_;
  bool display_catalog_ready_ = false;
  /// Topics pinned by the Add dialog; mirrored into topic_watch_list_.
  std::vector<AddTopicSelection> watched_topics_;

  /// The Qt->QarOS GUI-panel path. All created lazily, only once a live QAR
  /// session exists, because the panel sender must borrow that session.
  ///
  /// DECLARATION ORDER IS LOAD-BEARING, and all three sit AFTER qar_factory_ so
  /// they are destroyed before the session they borrow. Within the group,
  /// panel_input_source_ is declared FIRST and therefore destroyed LAST:
  /// closing panel_manager_'s streams is what unsubscribes it, and a source
  /// torn down before the panels it serves would be the one way a QarOS
  /// callback could find a dead object (the adapters also hold a reference of
  /// their own, so this is belt and braces).
  ///
  /// panel_input_source_ is the click/hover half — the panels' input.
  std::shared_ptr<qar_qar_adapter::QarCApiPanelInputSource> panel_input_source_;
  std::shared_ptr<qar_qar_adapter::QarCApiPanelSenderFactory> panel_sender_factory_;

  /// The dedicated node + executor `acquireFrameSource()` hands every
  /// `RosImageFrameSource` instead of RViz's own node — see
  /// video_source_executor.hpp for why (RViz only spins its own node with
  /// `spin_some` when the Qt GUI thread is idle, which is what starved the
  /// video subscriptions under load; and a second, slow reader of the same
  /// topic inside RViz's DDS participant — its Image display — starved them
  /// too, which is why this node lives in a private rclcpp context of its
  /// own). Lazily created on first use.
  ///
  /// Declared BEFORE video_panels_ and video_regions_ (and therefore
  /// destroyed AFTER both — members die in reverse declaration order, same
  /// rule video_panels_'s own comment below relies on): every
  /// RosImageFrameSource's subscription must already be gone before the
  /// executor spinning its node is stopped, and the executor must stop
  /// spinning before its node is released. Reverse-order destruction gives
  /// exactly that: sources -> executor -> node.
  std::unique_ptr<VideoSourceExecutor> video_executor_;

  /// One "Open video in AR" panel per ROS Image/CompressedImage topic. Keyed
  /// `"video:" + topic`, independent of ar_tools_ (the web tool whose button
  /// opened it keeps running unaffected by this panel's lifecycle).
  struct VideoPanel
  {
    std::shared_ptr<RosImageFrameSource> source;
    std::unique_ptr<qar_qt_streaming::VideoPanelWidget> widget;
  };
  /// Declared BEFORE panel_manager_ (and therefore destroyed AFTER it — members
  /// die in reverse declaration order, the same rule panel_input_source_'s own
  /// comment above relies on) so every VideoPanelWidget here is torn down only
  /// once panel_manager_ has already retired its streams — the "a streamed
  /// widget must outlive its stream" rule from qt_panel_stream_manager.hpp,
  /// satisfied automatically at session/plugin teardown with no extra code in
  /// the destructor.
  std::map<std::string, VideoPanel> video_panels_;

  /// @brief Return the shared `RosImageFrameSource` for @p topic, reusing the
  /// live one from variant (a) (`video_panels_`) or variant (b)
  /// (`video_regions_`) if either is already subscribed to it, rather than
  /// minting a second subscription + decode worker for the same topic.
  ///
  /// Without the sharing, opening a topic's "Open video in AR" panel AND
  /// streaming an Image/Camera dock for the SAME topic would construct two
  /// independent `RosImageFrameSource`s, each with its own `SensorDataQoS`
  /// subscription and worker thread decoding the same multi-megabyte frames —
  /// doubled network/CPU load fighting over the same best-effort delivery,
  /// worsening the frame loss that starves both. `video_frame_sources_` is a
  /// cache of weak_ptr, not an owner: it neither keeps a source alive on its
  /// own (an entry both variants drop is simply gone next time `.lock()` is
  /// tried) nor needs any special destruction-order treatment, because ownership
  /// stays in `video_panels_` / `video_regions_`.
  std::shared_ptr<RosImageFrameSource> acquireFrameSource(
    const std::string & topic, ImageTransport transport);
  std::map<std::string, std::weak_ptr<RosImageFrameSource>> video_frame_sources_;

  /// One RViz Image/Camera display's render-panel region, substituted with
  /// its ROS topic because `QWidget::render()` cannot capture a native Ogre
  /// child window (variant (b) — see attachVideoRegions()).
  ///
  /// `display` and `region` (== `display->getAssociatedWidget()`) are
  /// QPointer because RViz owns both, and either can go away out from under
  /// this panel: the display removed in the Displays tree, or (less likely)
  /// just its render window. `source` is reset — the entry itself kept, not
  /// erased — while the panel is tombstoned, so the ROS subscription is freed
  /// without losing track of the attachment; see maintainVideoRegions().
  struct VideoRegionAttachment
  {
    QPointer<rviz_common::Display> display;
    QPointer<QWidget> region;
    std::string topic;
    std::shared_ptr<RosImageFrameSource> source;
  };
  /// Every video-display region currently substituted (the entries of a
  /// tombstoned panel are kept), keyed by the same manager key as streamed_panels_ /
  /// video_panels_. A vector per key because one dock can host more than one
  /// matching display (e.g. an Image and a Camera in a split layout).
  ///
  /// Declared BEFORE panel_manager_ (and therefore destroyed AFTER it —
  /// members die in reverse declaration order), for the same reason
  /// video_panels_ above is: each RosImageFrameSource here feeds a region
  /// panel_manager_ may still paint into on its own teardown, so it must
  /// outlive that teardown rather than being torn down first out from under
  /// it.
  std::map<std::string, std::vector<VideoRegionAttachment>> video_regions_;
  /// Throttle for maintainVideoRegions(). Epoch (time_since_epoch() == 0)
  /// means "run unconditionally" — the same sentinel convention
  /// heartbeat_last_ below uses.
  std::chrono::steady_clock::time_point video_regions_last_maintained_{};

  /// @brief Rate-limited (~2 s) "AR VIDEO:" line per video panel (video_panels_)
  /// and per region source (each video_regions_ attachment with a live
  /// source), covering the whole path from ROS message to submitted frame:
  /// RosImageFrameSource's received/decoded/avg_decode_ms, the mailbox's
  /// publish sequence, and the owning QtPanelStream's frames_captured /
  /// frames_skipped_unchanged / frames_skipped_not_ready / frames_submitted /
  /// avg_capture_ms (qt_panel_stream_manager.hpp's PanelDiagnosticsSnapshot —
  /// already-maintained counters, read only, nothing new computed on the GUI
  /// thread). It shows exactly which stage a slow panel is losing frames at.
  /// Called from tickArPanels(), after panel_manager_->tick() so this tick's
  /// counters are already current.
  void logVideoDiagnostics();
  std::chrono::steady_clock::time_point video_diagnostics_last_logged_{};

  /// Each of these says something once; see startHiddenDisplaysStreamOnConnect() and
  /// keepHiddenDisplaysShown(). Declared ahead of hidden_displays_ only so that member
  /// stays the one declared IMMEDIATELY before panel_manager_.
  bool hidden_displays_disabled_logged_ = false;
  bool hidden_displays_reshown_logged_ = false;

  /// The hidden RViz Displays panel: a SECOND instance of RViz's own Displays panel,
  /// bound to the same live display tree but never shown on the desktop, streamed into
  /// AR under kHiddenDisplaysKey. See startHiddenDisplaysStream(). Null until it has
  /// been created (the first QarOS connect, or the first "Stream panel to AR"), and
  /// again if creating it failed.
  ///
  /// **Declared IMMEDIATELY BEFORE panel_manager_, and so destroyed immediately AFTER
  /// it.** That order is the contract: the manager (destroyed first) closes this panel's
  /// stream - input subscriptions, QarOS panel, video sender - while the widget it
  /// captured is still alive, exactly as video_panels_ above is ordered for the same
  /// reason. The widget is parentless (nothing in Qt owns it), so this pointer is its
  /// only owner, and it is NOT registered in streamed_panels_: that list's
  /// destroyed-widget sweep and dock hooks are for panels the operator picked from RViz's
  /// own docks, and this one has neither a dock nor an RViz close to follow.
  ///
  /// It views the VisualizationManager's display tree model, which RViz destroys BEFORE
  /// it destroys the panels (VisualizationFrame::~VisualizationFrame deletes manager_
  /// first). That is safe: the Qt view is told when its model dies, and nothing here
  /// touches getDisplayContext() once teardown has begun.
  std::unique_ptr<rviz_common::Panel> hidden_displays_;

  std::unique_ptr<qar_qt_streaming::QtPanelStreamManager> panel_manager_;
  /// The room-space half — app-volume gestures feeding the spatial picker.
  /// Subscribed once the session has an app volume to subscribe to.
  std::unique_ptr<qar_qar_adapter::QarCApiSpatialGestureSource> spatial_gesture_source_;
  /// Whether startSpatialGestureSource() has ever run, and what it concluded.
  /// A status line that only ever says "off" cannot distinguish "not armed"
  /// from "never subscribed".
  bool spatial_subscription_attempted_ = false;
  QString spatial_subscription_note_;
  /// Samples drained from the gesture source, and the rate-limited per-sample
  /// trace counter that turns them into log lines (first 20, then one in 50).
  std::uint64_t spatial_samples_drained_ = 0;
  /// Where QarOS last said it is displaying our scene (@ref refreshScenePlacement).
  ///
  /// A DIAGNOSTIC. It is logged with every traced gesture sample and whenever it
  /// moves, and it is deliberately NOT a field of the picker's transform: it
  /// lives here, on the panel, so that "read it back and log it" and "apply it
  /// to a pick" cannot be confused for one operation.
  qar_ar_core::ScenePlacement scene_placement_;
  /// Key under which THIS panel streams itself; also the AR panel's title.
  static constexpr const char * kSelfPanelKey = "qar_streaming_panel";
  /// Key of the hidden Displays panel's stream (see hidden_displays_). Its dialogs - the
  /// Add-display box - become panels of their own beside it, keyed "dialog:<title>".
  static constexpr const char * kHiddenDisplaysKey = "hidden:Displays";

  // --- The ROS graph as AR tools --------------------------------------------
  /// One per exposed entity: the served session, the router that decides what a
  /// control event means, and whichever ROS entities the panel needs. Held by
  /// unique_ptr in a vector so the address stays stable while callbacks are in
  /// flight (a service response arrives later and looks its tool up again).
  struct ArToolPanel
  {
    std::string panel_id;
    RosEntityRef entity;
    bool writable = false;
    std::shared_ptr<qar_web_panels::PanelSession> session;
    std::unique_ptr<ArControlRouter> router;
    std::shared_ptr<qar_ros_ui::TopicSubscription> subscription;
    std::shared_ptr<qar_ros_ui::TopicPublisher> publisher;
    std::shared_ptr<qar_ros_ui::ActionGoalHandle> goal;
    std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> ar_panel;
    /// The QarOS panel is created on a render tick, never on the click.
    bool ar_panel_pending = true;
    bool pending_logged = false;
    /// What the live panel was last given, so a viewport report can resize it
    /// without recomputing where it hangs in the room.
    qar_ar_core::UrlPanelPlacement placement;
    /// The CSS viewport the page reported measuring itself into, once it has.
    /// Empty until then: a panel opens before its page has ever been laid out,
    /// so the first size is always the estimate and the report corrects it.
    std::optional<qar_ui_model::ViewportBody> viewport;
    /// Coalescing clock: a 500 Hz topic still costs one conversion per refresh.
    std::chrono::steady_clock::time_point last_push{};
    double refresh_hz = 5.0;
    std::uint64_t last_status_published = 0;
    bool result_published = false;
  };
  ArToolPanel * findArTool(const std::string & panel_id);
  /// A page told us the CSS viewport it was actually laid out into. Logs it,
  /// re-sizes the live AR panel to that aspect, and shows it in the status
  /// line. See qar_web_panels/url_panel_sizing.hpp for what "re-sizes" means
  /// and why matching the reported aspect is the fix for the squeeze.
  void onArToolViewport(
    const std::string & panel_id, const qar_web_panels::PanelViewportReport & report);
  /// Open the QarOS URL panel for @p tool. Empty on success, else the reason
  /// (which leaves the tool pending so the next tick retries).
  QString openArPanelFor(ArToolPanel & tool);
  /// Push whatever ROS has produced since the last push, at the tool's cadence.
  void pushArToolData(ArToolPanel & tool, std::chrono::steady_clock::time_point now);
  /// Run one routed control intent against the bridges and ack the viewer.
  void executeControlIntent(
    ArToolPanel & tool, const ControlIntent & intent,
    const qar_web_panels::PanelControlEvent & event);

  /// @brief `RosActionKind::OpenVideoPanel`: open (or revive) the streamed AR
  /// video panel for @p tool's Image/CompressedImage topic, and ack the viewer
  /// either way. Key is `"video:" + tool.entity.name`.
  ///
  /// A key already live (has_panel && !is_tombstoned) is a no-op ack. Anything
  /// else — never opened, or tombstoned — gets a `RosImageFrameSource` (shared
  /// with variant (b) for the same topic via @ref acquireFrameSource, or a new
  /// subscription if none exists yet) + a BRAND NEW `VideoPanelWidget` (a
  /// tombstoned slot cannot be revived onto its old widget: reviving means a
  /// fresh `VideoPanelWidget`, since its `LatestVideoFrame` is fixed at
  /// construction — the frame SOURCE may still be reused), added via
  /// `panel_manager_->add_panel`, which itself revives a tombstoned key onto the
  /// new widget rather than minting a second sender.
  void openVideoPanel(ArToolPanel & tool, const qar_web_panels::PanelControlEvent & event);

  /// @brief Stop @p key's ROS subscription (freeing CPU) the first time its
  /// panel is observed tombstoned, and sync_to_frame() every panel still live.
  /// Called from tickArPanels(), just before panel_manager_->tick().
  void tickVideoPanels();

  /// @brief Variant (b): substitute ROS video for every RViz Image/Camera
  /// display whose render window lives inside @p widget, the dock just
  /// (re)streamed as @p manager_key.
  ///
  /// Walks `getDisplayContext()->getRootDisplayGroup()` for every Display
  /// with `is_video_display_class(getClassId())` and an associated widget
  /// equal to (or a descendant of) @p widget, acquires a `RosImageFrameSource`
  /// per match (@ref acquireFrameSource — shared with variant (a) or another
  /// region already on that topic), and registers it with
  /// `panel_manager_->set_panel_region_source`. A @p widget with no such
  /// display (the common case) is a cheap no-op. Overwrites any previous
  /// entry for @p manager_key — the natural full-rebuild path both a first
  /// stream and a revive take, since either way the caller just succeeded at
  /// `add_panel(manager_key, widget, ...)`.
  ///
  /// Called after EVERY successful `add_panel()` (onStreamSelectedPanel's
  /// combo-driven path and openVideoPanel's own video widget); harmless on a
  /// widget that hosts no RViz render window.
  void attachVideoRegions(const std::string & manager_key, QWidget * widget);

  /// @brief Recursive collect (DisplayGroup nesting) of every Display under
  /// @p group that passes is_video_display_class() and whose associated
  /// widget is @p widget itself or a descendant of it. Mirrors
  /// GridFloorPlaneManager::collectGridDisplays's walk.
  void collectVideoDisplays(
    rviz_common::DisplayGroup * group, QWidget * widget,
    std::vector<rviz_common::Display *> & out) const;

  /// @brief Heartbeat field for every RViz Image/Camera display, wherever it
  /// lives (diagnostic only; empty when there is none): per display, how many
  /// publishers RViz's OWN node sees on its topic and what the display's Status
  /// "Topic" row says, worded by format_video_display_health(). It exists because
  /// "No Image" in such a display left no trace in any log, so a live failure
  /// could not be told apart as "RViz never saw the publisher" / "saw it but
  /// received nothing" / "received frames but did not paint". GUI thread only.
  QString videoDisplayHealthField() const;

  /// @brief Throttled (~0.5 s) upkeep for every attachVideoRegions() entry:
  /// a display/its render window destroyed drops that region (re-registering
  /// the survivors — clear_panel_region_sources has no single-region form); a
  /// changed Topic property (or a fresh revive) rebuilds the source and
  /// re-registers it; a tombstoned panel drops its sources to free the ROS
  /// subscriptions but keeps the entries so the next "Stream panel to AR"
  /// (attachVideoRegions) finds and repopulates them; a panel gone from panel_manager_
  /// entirely drops everything under its key. Called from the same per-tick hook as
  /// tickVideoPanels() (tickArPanels()).
  void maintainVideoRegions();

  std::unique_ptr<qar_ros_ui::GraphDiscovery> graph_discovery_;
  std::unique_ptr<qar_ros_ui::TopicBridge> topic_bridge_;
  std::unique_ptr<qar_ros_ui::ServiceBridge> service_bridge_;
  std::unique_ptr<qar_ros_ui::ActionBridge> action_bridge_;
  /// Serves every generated panel on one port; QarOS's browser fetches from it.
  std::unique_ptr<qar_web_panels::WebPanelServer> web_panel_server_;
  /// Mints QarOS URL panels against the render path's session.
  std::shared_ptr<qar_qar_adapter::QarCApiUrlPanelFactory> url_panel_factory_;
  /// Bounds and overrides for web-panel physical size. Read from the
  /// QAR_PANEL_WEB_* environment once, then trimmed by ar_panel_scale_spin_.
  qar_web_panels::UrlPanelSizing url_panel_sizing_;
  bool url_panel_sizing_loaded_ = false;
  /// What the environment override said, held until there is a log to say it in.
  std::string url_panel_sizing_note_;
  /// The most recent viewport any page reported, for the AR-tools status line.
  std::string ar_viewport_note_;
  /// Resolve the sizing config, reading the environment on first use.
  const qar_web_panels::UrlPanelSizing & urlPanelSizing();
  /// The panel templates: the semantic layouts first, the generic form as the
  /// fallback.
  PanelTemplateRegistry panel_templates_;
  ExposureRegistry exposures_;
  /// Adaptive QoS has to be re-derived as publishers come and go; once a second
  /// is plenty and costs nothing when nothing changed.
  std::chrono::steady_clock::time_point last_qos_reconcile_{};

  /// Declared LAST of the AR tool members so it is destroyed FIRST: a tool borrows
  /// the bridges' entities, the server's session and the render path's QarOS
  /// session, and every one of those must outlive it.
  std::vector<std::unique_ptr<ArToolPanel>> ar_tools_;

  /// Candidates shown in ar_panel_combo_, in the same order. QPointer because a
  /// dock panel can be closed and destroyed while its entry is still listed.
  std::vector<std::pair<QString, QPointer<QWidget>>> streamable_panels_;
  /// Live AR panel streams: manager key -> the widget being captured. Also
  /// QPointer: QtPanelStream holds a RAW widget pointer, so a destroyed panel
  /// must retire its stream before the next capture touches it.
  std::vector<std::pair<std::string, QPointer<QWidget>>> streamed_panels_;
  /// The `closed()` connection on the RViz dock that hosts each streamed dock
  /// panel, keyed like @ref streamed_panels_; see @ref watchDockClose. The
  /// receiver context is this panel, so every connection dies with it.
  std::map<std::string, QMetaObject::Connection> dock_close_connections_;
  /// When the AR-panel status line was last rebuilt. The line is a LIVE readout
  /// (frames, input counters), so it is refreshed from tickArPanels() rather
  /// than only from the one-shot places that change it; this paces that to a
  /// few Hz so a 60 Hz tick does not relayout a QLabel every frame.
  std::chrono::steady_clock::time_point ar_status_last_{};
  /// One posted AR input pump is already in flight; see scheduleArPanelInputPump.
  bool ar_input_pump_pending_ = false;

  /// Opts RViz out of Windows' occlusion/power throttling (applied once in
  /// onInitialize, released with the panel) — see windows_scheduling.hpp. Declared
  /// before render_timer_ only for grouping; its release is explicit in the
  /// destructor, after the tick has been stopped.
  WindowsSchedulingGuard scheduling_guard_;

  /// Periodic render tick on the Qt/render thread, at tick_period_ms_. It renders
  /// the active streaming streams (no-op unless streaming is active with an
  /// active stream).
  QTimer * render_timer_ = nullptr;
  std::uint64_t stereo_render_ticks_ = 0;

  // --- Render-tick period (QAR_PANEL_TICK_MS) ------------------------------
  /// Nominal configured period in ms (fractional); the source of truth for display/
  /// logging/the manager, since render_timer_->interval() alone cannot represent
  /// the 0.1 ms zero-interval case (its underlying QTimer interval is 0 there).
  double tick_period_ms_ = 0.1;
  /// True when tick_period_ms_ < 1 ms: render_timer_ runs as a zero-interval timer
  /// and onRenderTick() throttles the actual tick body to tick_throttle_interval_us_.
  bool zero_interval_mode_ = false;
  double tick_throttle_interval_us_ = 1000.0;
  /// Epoch (time_since_epoch().count() == 0) means "run the next tick unconditionally"
  /// — same sentinel convention as heartbeat_last_ below.
  std::chrono::steady_clock::time_point last_tick_body_run_{};

  // --- Live tick heartbeat --------------------------------------------------
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
  /// Per-frame stage timing (QAR_VIDEO_TIMING_DIR; qar_ar_core/timing_log.hpp): every
  /// gap between two render ticks longer than kGuiGapRecordMs, as gui_gaps.csv. A gap here
  /// means the Qt thread itself did not run - which separates "RViz's GUI thread was blocked"
  /// from "only the QarOS streams stalled". Null when timing is off.
  std::shared_ptr<qar_ar_core::TimingSink> gui_gap_timing_;
  static constexpr double kGuiGapRecordMs = 25.0;

  RvizPeerPanelModel peer_model_;
  bool qar_running_ = false;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__QAR_STREAMING_PANEL_HPP_
