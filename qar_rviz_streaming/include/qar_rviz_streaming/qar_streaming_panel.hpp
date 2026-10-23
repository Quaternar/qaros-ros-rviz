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
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include <QByteArray>
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
#include "qar_rviz_streaming/ar_control_panel_model.hpp"
#include "qar_rviz_streaming/ar_control_panel_placement.hpp"
#include "qar_rviz_streaming/ar_control_panel_router.hpp"
#include "qar_rviz_streaming/ar_control_panel_state.hpp"
#include "qar_rviz_streaming/ar_control_router.hpp"
#include "qar_rviz_streaming/ar_exposure_registry.hpp"
#include "qar_rviz_streaming/ar_panel_generator.hpp"
#include "qar_rviz_streaming/display_class_catalog.hpp"
#include "qar_rviz_streaming/grid_floor_plane.hpp"
#include "qar_rviz_streaming/panel_ground_rule.hpp"
#include "qar_rviz_streaming/panel_settings.hpp"
#include "qar_rviz_streaming/ros_graph_tree.hpp"
#include "qar_rviz_streaming/ros_image_frame_source.hpp"
#include "qar_rviz_streaming/ros_topic_tree.hpp"
#include "qar_rviz_streaming/rviz_peer_panel_model.hpp"
#include "qar_rviz_streaming/spatial_interaction.hpp"
#include "qar_rviz_streaming/stable_panel_layout.hpp"
#include "qar_rviz_streaming/video_source_executor.hpp"
#include "qar_rviz_streaming/windows_scheduling.hpp"

class QComboBox;
class QCheckBox;
class QDockWidget;
class QDoubleSpinBox;
class QFrame;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QResizeEvent;
class QScrollArea;
class QShowEvent;
class QTabWidget;
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
class ElidedLabel;

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

  /// @brief The size hints of the panel. Both are constants, measured once at the end of
  /// buildUi() (compute_stable_hints()) and never again: this panel streams itself to AR, and a
  /// change of its size rebuilds the AR panel, so no page may be able to move what the dock asks
  /// the panel for. See stable_panel_layout.hpp.
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

  /// @brief Reads the panel's settings from the RViz configuration (base class first), applies
  /// them to the controls and re-resolves them against the environment. Works whether RViz calls
  /// it before or after onInitialize(). See panel_settings.hpp for the keys and the precedence
  /// rule (environment variable, then the saved value, then the default).
  void load(const rviz_common::Config & config) override;

  /// @brief Writes the panel's settings into the RViz configuration (base class first): the
  /// version key and the operator's own values. A value that only the environment supplied is
  /// never written.
  void save(rviz_common::Config config) const override;

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

protected:
  /// The first show after onInitialize() schedules the one-time "PANEL SIZE:" report.
  void showEvent(QShowEvent * event) override;
  /// Logs a change of the panel's size ("PANEL SIZE: panel resized"), at most once a second.
  void resizeEvent(QResizeEvent * event) override;

private Q_SLOTS:
  void onStartQar();
  void onStopQar();
  /// @brief "Invite peer" (Connection tab). Enabled only while live and connected.
  void onInvitePeer();
  /// @brief "Add simulated peer" (Diagnostics tab, Simulated peers): the offline preview's peer.
  void onAddSimulatedPeer();
  void onRemovePeer();
  void onStartStream();
  void onStopStream();
  void onRefreshDiagnostics();
  void onCaptureOffscreenFrame();
  void onCaptureStereoPair();
  void onRenderTick();
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

  // --- AR control panel (Connection tab) -----------------------------------
  /// "Show AR control panel": the first press asks QarOS to show the panel again, a second press
  /// within ten seconds of a refusal creates it again. Only records the press; the QarOS call is
  /// made by the next render tick.
  void onShowArControlPanel();
  /// "Copy page URL": puts the address of the control panel's page on the clipboard.
  void onCopyArControlUrl();

private:
  void buildUi();

  // --- Persisted settings (panel_settings.hpp) ------------------------------
  /// @brief Reads the environment variables of the settings, once, into env_snapshot_. Called
  /// from the constructor; the environment is not read again.
  void snapshotEnvironment();

  /// @brief Resolves user_settings_ against env_snapshot_ into effective_settings_ (the one rule:
  /// an acceptable environment variable, else the saved value, else the default). What the rule
  /// refused is kept in settings_warnings_; reportSettings() logs it.
  void resolveSettings();

  /// @brief Logs the "SETTINGS:" lines of the last resolve: each sentence the rule refused with,
  /// and each setting that an environment variable decides. A sentence is logged once.
  void reportSettings();

  /// @brief Shows effective_settings_ in the controls, under a QSignalBlocker, then does what the
  /// signals would have done (the pick target and the frame go to the controller). A control
  /// whose environment variable decides shows that value, is disabled, and says so in its tool
  /// tip. Safe to call whether or not onInitialize() has run.
  void applySettingsToUi();

  /// @brief An edit of a persisted control: records the operator's own value for @p key
  /// (@p text, validated), re-resolves, and emits configChanged() so RViz knows the
  /// configuration is no longer saved. A text the setting refuses is logged and not kept.
  void onSettingEdited(const char * key, const QString & text);

  /// @brief What the variable of the setting @p key said when the rule refused it: its text,
  /// trimmed and lower-cased. Empty when the variable is unset, empty or was accepted.
  QString refusedEnvironmentText(const char * key) const;

  /// @brief Logs "SETTINGS: <text>" unless that exact text was logged before.
  void logSettingsLine(const QString & text);

  // --- Tabs, status strip and the size log ----------------------------------
  /// @brief Shows the Advanced group when "Show advanced options" is ticked, and the simulated
  /// peers (Diagnostics) when the panel is the offline preview or that box is ticked.
  void refreshAdvancedVisibility();

  /// @brief Keeps "Closed AR panels:" in step with the settings in force. The choice acts only
  /// while "Hide panels in AR instead of closing them" is off (hiding parks every closed window),
  /// so the control is disabled while hiding is on and says why in its tool tip. A control whose
  /// environment variable decides stays disabled, and its tool tip then names the variable first.
  /// Only the enabled state and the tool tip change, never the size of the panel.
  void refreshCloseModeControl();

  /// @brief Once, from a queued call after the first show that follows onInitialize(): logs the
  /// "PANEL SIZE:" report. Idempotent.
  void scheduleSizeReport();
  void reportPanelSizes();

  /// @brief "PANEL SIZE: tab '...' selected; panel WxH unchanged" or "... CHANGED from WxH".
  void logTabSelected(const QString & tab_name);

  /// @brief Logs a pending change of the panel's size that resizeEvent() held back to keep to
  /// one line a second.
  void flushResizeLog();

  // --- Default dock size ----------------------------------------------------------------------
  /// @brief Gives the dock that hosts this panel the default size, once. load() starts
  /// default_dock_timer_ when the saved configuration asks for it (wants_default_dock_size()); the
  /// delay lets RViz restore its window state first. A docked panel is resized through its main
  /// window, a floating one directly; no dock or no main window leaves the size alone, and so does
  /// an AR panel that is already streaming (a resize would rebuild it). The hint that sizes the
  /// dock is the panel's plus what the dock adds around it (dockChrome()), so that every page fits
  /// the panel. A dock whose column is shared with other docks is held at its height by a temporary
  /// minimum height, the docks streamed to AR keep their height, and a main window whose other
  /// docks cannot give way grows (giveDockHeight()); default_dock_release_timer_ takes the limits
  /// away again about 300 ms later (releaseDefaultDockHold()). Says what it did, with the size the
  /// dock really got.
  void applyDefaultDockSize();

  /// @brief Takes the temporary limits off the docks that applyDefaultDockSize() held, and says
  /// what size the dock has.
  void releaseDefaultDockHold();

  /// @brief Gives @p dock the height @p height (set_dock_height()), keeping the height of every
  /// other dock whose content is streamed to AR (streamedDocksExcept()), and logs what the main
  /// window did: "PANEL SIZE: RViz window grown from <h1> to <h2> px so that '<label>' gets <h>
  /// px." or "PANEL SIZE: RViz window cannot grow (<why>); '<label>' stays <h> px.". The limits
  /// that were set are recorded in @p hold, which the caller releases when the layout has settled.
  DockHeightResult giveDockHeight(
    QDockWidget & dock, const QString & label, int height, DockHold & hold);

  /// @brief The docks other than @p dock whose content is streamed to AR: those whose manager key
  /// has a panel there, and the dock of this panel when it is streamed. Their size must not
  /// change: a size change rebuilds the AR panel.
  std::vector<QDockWidget *> streamedDocksExcept(const QDockWidget * dock) const;

  /// @brief What @p dock adds around this panel (its title bar and frame), as far as it can be
  /// measured now: the size of the dock less the size of the panel, each side kept between 0 and
  /// 120 pixels so that a dock that has not been laid out cannot give a wild number.
  QSize dockChrome(const QDockWidget & dock) const;

  /// @brief The line "PANEL SIZE: default dock size applied: ..." for @p dock and the size it was
  /// asked for.
  void logDefaultDockApplied(const QDockWidget & dock, const QSize & asked);

  // --- A dock is fitted to its content before it is streamed -----------------------------------
  /// One start of a dock stream that waits for the layout to settle: its dock is fitted by a
  /// SettledHeightFit, one look per period of dock_start_timer_.
  struct PendingDockStart
  {
    explicit PendingDockStart(const FitPolicy & policy)
    : fit(policy) {}

    std::string key;                ///< the manager key
    QPointer<QDockWidget> dock;     ///< null once the dock is gone
    DockHold hold;                  ///< the limits that keep the dock and its neighbours
    SettledHeightFit fit;           ///< the looks at the content of the dock, and what they found
    /// What the window said to the latest growth of the dock: why it cannot be made higher.
    WindowGrowth window = WindowGrowth::kNotNeeded;
    /// The hold on the docks was released at a look that found nothing to remove: a dock that is
    /// low again at a later look lost its height to the layout.
    bool hold_released = false;
    /// The fit does not log its looks one by one; its caller says the result once (the fit of
    /// This QAR panel at start, startFit()).
    bool quiet = false;
    /// The height of the dock at the first look.
    int first_height = 0;
    /// Set when the fit ends: its last words (describe_fit_result()) and whether two looks agreed
    /// that no bar is left.
    std::string result;
    bool confirmed = false;
  };

  /// What one look at a pending dock start came to.
  enum class DockStartStep
  {
    Waiting,   ///< look again at the next period
    Finished,  ///< the fit is over and said so; the stream is due
  };

  /// @brief The most height a dock may be asked for: the room of its screen less the clearance.
  int dockRoomHeight(const QDockWidget & dock) const;

  /// @brief When @p widget lives in a dock (an RViz dock or this panel) that is on show: fits the
  /// dock to its content with a SettledHeightFit, so that the AR copy shows no scroll bar and its
  /// QarOS panel is made once, at the final size. The first look is taken at once; the dock is
  /// grown under a hold (giveDockHeight()) when it shows a bar, and the start waits for
  /// dock_start_timer_ (startPendingDockStreams()) for the next looks. The stream is added when
  /// two looks in a row agree after the hold was released.
  /// @return true when the start is queued, false when the stream can be added now.
  bool growDockBeforeStreaming(const QString & key, QWidget * widget);

  /// @brief Takes one look at the content of the dock of @p start and does what the fit asks for:
  /// grows the dock under a hold, releases the hold at the first look that finds no bar, or ends
  /// the fit. One line per look (`fit round`) and one at the end (`fitted to`), in the shapes of
  /// describe_fit_round() and describe_fit_result(); no line may claim that there is no scroll bar
  /// unless two looks agreed. A fit that gives up because the dock did not grow says why with
  /// dock_growth_reason(): what the window said, or that the layout took the height back after
  /// the hold was released.
  DockStartStep advanceDockStart(PendingDockStart & start);

  /// @brief The start of a dock stream is due: takes the next look at each pending start
  /// (advanceDockStart()), and starts the stream of those that are finished
  /// (streamPanelByKey() again, without fitting). A start whose dock is not on show, or whose QarOS
  /// session is not live (arSessionLive()), is dropped with a line instead.
  void startPendingDockStreams();

  /// @brief Does the start of the stream @p manager_key wait for its fit? A dock fit and the fit
  /// of the hidden Displays copy both count.
  bool dockStartWaits(const std::string & manager_key) const;

  /// @brief Is the switch of the panel @p manager_key still on its way? A show or hide waits for
  /// the manager's tick, and a start waits for its fit (a few periods of 100 ms).
  /// Neither is a reason to tell the page that the switch it has drawn is wrong.
  bool panelSwitchWaits(const std::string & manager_key) const;

  /// @brief Drops the start that waits for @p manager_key, if there is one: the limits it put on
  /// the docks come off, its dock-close hook goes, and one line says why: "AR PANEL: '<key>'
  /// start cancelled (<reason>) while its dock was being fitted.", or "... while the copy was
  /// being fitted." for the hidden Displays copy. Whatever the operator or the session does while a
  /// fit is running (Stop, the X of the dock, the switch on the AR control page, a disconnect)
  /// ends the start this way, so that no stream is added after it.
  /// @return true when a start was waiting.
  bool cancelPendingDockStart(const std::string & manager_key, const QString & reason);

  /// @brief cancelPendingDockStart() for every start that waits.
  void cancelPendingDockStarts(const QString & reason);

  // --- This QAR panel is fitted to its pages when it starts -----------------------------------
  /// @brief Arms start_fit_timer_ once, from the first show of the panel: the window state of RViz
  /// is restored by then, and the timer gives the layout the same moment the default dock size
  /// waits for (kDefaultDockDelayMs).
  void armStartFit();

  /// @brief The dock of this panel is made as high as its tallest page needs, once, so that the
  /// panel shows no scroll bar in RViz before it is streamed to AR (growDockBeforeStreaming() does
  /// the same for the stream). Runs from start_fit_timer_: the first call decides, with
  /// start_fit_skip_reason(), whether the dock is fitted at all (no dock, a dock that is not on
  /// show, an AR panel that streams and a fit for the stream of this panel all end it), then takes
  /// one look per period like advanceDockStart(), with the same SettledHeightFit: it only grows
  /// the dock, as far as its screen goes, under a hold that is released at the first quiet look,
  /// and ends when two looks agree. It waits for the default dock size to be applied and settled
  /// first. It starts no stream. Says what it did in one line: "PANEL SIZE: This QAR panel fitted
  /// at start: ..." or "... not fitted at start: <why>".
  void startFit();

  /// @brief The stream of this panel is due while startFit() still looks at its dock: the fit that
  /// runs goes on as the fit of the stream (one fit per dock at a time, so no second resize), with
  /// its looks logged from now on, and the stream is added when it ends
  /// (startPendingDockStreams()). @p widget is the panel, whose dock the X hook watches meanwhile.
  /// @return true when a fit at start took over, and the stream waits for it.
  bool adoptStartFit(QWidget * widget);

  /// @brief Can an AR panel be started now? The AR panel manager exists and, in live mode, the
  /// QarOS session is connected.
  bool arSessionLive() const;

  // --- Test markers (Diagnostics tab) ---------------------------------------------------------
  /// What the two environment variables of the test markers allow: the python and the script to
  /// run, or the reason there is nothing to run.
  struct TestMarkersSetup
  {
    QString python;
    QString script;
    QString problem;  ///< empty when both variables name an existing file
  };
  /// @brief Reads QAR_PANEL_TEST_MARKERS_PYTHON and QAR_PANEL_TEST_MARKERS_SCRIPT and checks that
  /// both files exist.
  TestMarkersSetup testMarkersSetup() const;

  /// @brief Brings the button in step with the process: "Stop test markers" while one runs, else
  /// "Start test markers", enabled only when the setup allows it, with a tool tip that says what is
  /// missing when it does not. The button's text changes; the size of the panel does not.
  void refreshTestMarkersUi();

  /// @brief The button: starts the publisher, or stops the one that runs.
  void onTestMarkersClicked();

  /// @brief Starts the python script in a QProcess that has the environment of this process.
  /// Never waits for it: the output arrives as signals, one log line a second at most.
  void startTestMarkers();

  /// @brief Asks the running process to end (kill, no waiting). The finished signal does the rest.
  void stopTestMarkers();

  /// @brief Moves what the process printed into the log, line by line, "TEST MARKERS: <line>".
  /// @param final_read the process has ended: a last line without a line break is logged too.
  void drainTestMarkersOutput(bool final_read);

  /// @brief Logs one line of the output as "TEST MARKERS: <line>", unless a line was logged less
  /// than a second ago: then it is only counted, and the next line shown says how many were.
  void logTestMarkersLine(const QByteArray & line);

  /// @brief The process has ended: logs how (stopped, or its exit code), forgets it and frees it
  /// later. @p crashed is the process's exit status being a crash.
  void finishTestMarkers(int exit_code, bool crashed);

  /// @brief Ends the process object without waiting for the process: none of its signals reaches
  /// this panel, a running process is killed and the object is deleted at once (it is told first
  /// that the process is over, so that ~QProcess has nothing to wait for). For the destructor:
  /// neither the panel nor the unload of the plugin may wait, and the object cannot outlive the
  /// plugin, because its virtual table is in the plugin.
  void releaseTestMarkersProcess();

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

  /// @brief Tear one exposure down: page, session, subscription, registry row, and its QarOS
  /// panel. With hiding as the way of the switch the QarOS panel is parked (see
  /// parked_tool_panels_): the next tool tick hides it and logs the retire line, and a new
  /// exposure of the same entity takes it over; otherwise it is closed. A tool that has no QarOS
  /// panel yet has nothing to park or close, and the line says so. With hiding the call itself
  /// makes no QarOS call.
  void retireExposure(RosEntityKind kind, const QString & name);

  /// @brief Mirror the registry into exposure_list_.
  void refreshExposureList();

  /// @brief Per-tick work for the generated panels: hide the QarOS panels that retirements
  /// parked, open the QarOS panels that are still pending, push new ROS values at each panel's
  /// own cadence, and give the service/action bridges their timeout tick.
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

  /// @brief Makes the panel manager from @p config: gives it this panel as its diagnostics sink,
  /// the close mode the settings call for and the ground rule for the dialogs it promotes. The
  /// sender factory and the input source are the caller's (ensurePanelManager() takes them from
  /// the QarOS session).
  /// @return empty on success, else the reason.
  QString startPanelManager(qar_qt_streaming::QtPanelStreamingConfig config);

  /// @brief The close mode the settings call for. "AR Visibility" = hide means park: a window
  /// that closes only hides its AR panel, and nothing is closed while a session runs. Otherwise
  /// it is the "AR Panel Close Mode" choice (destroy or notice). Park is never a value of that
  /// choice: the visibility switch selects it.
  qar_qt_streaming::PanelCloseModeChoice closeModeFromSettings() const;

  /// @brief Tells the panel manager, when there is one, the close mode the settings call for
  /// now. Called after every re-resolve of the settings, so a change of either setting acts on
  /// the closes that follow; a panel that is already parked stays parked.
  void syncPanelCloseMode();

  /// What a window that closes comes to under @p mode, as a clause for a log line.
  static QString closeModeSentence(qar_qt_streaming::PanelCloseMode mode);

  /// A pose after the ground rule: where it ended up and by how much it was raised.
  struct GroundedPose
  {
    QarPose pose{};
    float raised_m = 0.0f;       ///< 0 when the panel was high enough already
    float ground_room_y = 0.0f;  ///< the room height of the RViz ground it was measured against
  };

  /// @brief Applies the ground rule (panel_ground_rule.hpp) to a panel of @p height_m metres
  /// whose top edge is centred at @p top_centre: the lower edge ends up at least 0.10 m above the
  /// RViz ground. The live scene placement is read first; while QarOS has not reported one the
  /// ground is the centre height of the app volume (@ref volume_center_y_m_). Only raises.
  GroundedPose liftAboveGround(const QarPose & top_centre, float height_m);

  /// @brief Where the top edge of a panel with a fixed place hangs, at the room point
  /// (@p room_x, @p room_z): the room height kFixedPanelTopM with QAR_PANEL_FIXED_POSES=room (the
  /// default), that height above the RViz ground with `ground` (fixed_panel_top_y()). The ground
  /// rule (liftAboveGround()) is applied to the result as before.
  QarVector3 fixedPanelPosition(float room_x, float room_z);

  /// @brief Reads QAR_PANEL_AR_FRAME (four insets and optionally the fit margin), else
  /// QAR_PANEL_AR_HEADROOM (parse_ar_frame()) into @ref ar_frame_: the library default when
  /// neither is set or the one that is set is not acceptable (the latter with a `LIVE: WARNING:`
  /// line), and says the result and the variable it came from in one `LIVE: AR panel frame`
  /// line. Called at the first connect, before the panel manager exists; the manager is given
  /// the value in startPanelManager().
  void readArFrame();

  /// Says that @p grounded was raised ("AR PANEL: '<label>' raised by ..."); nothing when it was
  /// not.
  void logGroundRaise(const QString & label, const GroundedPose & grounded);

  /// Marks the entries of the panel list whose AR panel is parked, and unmarks the others. Only
  /// item texts change (the combo's geometry is fixed once it has been shown).
  void markParkedPanelsInCombo();

  /// One window that can be streamed into AR: the entry of the desktop's panel list and of the
  /// AR control panel's list.
  struct StreamableEntry
  {
    QString key;                ///< the manager key
    QString label;              ///< the text of the entry in the desktop's list
    QString title;              ///< the name the AR control panel shows
    QPointer<QWidget> widget;   ///< null while the window does not exist yet
  };

  /// @brief Walks the windows that can be streamed, in the order of the desktop's list: this
  /// panel first, then every other dock widget RViz hosts, the video panels and, last, the
  /// hidden Displays copy. Touches no control, so the AR control panel can read it often.
  std::vector<StreamableEntry> collectStreamableEntries();

  /// @brief Repopulate the streamable-panel list: this panel first, then every
  /// other dock widget RViz currently hosts.
  void refreshStreamablePanels();

  /// @brief What "Stream panel to AR" does for the entry @p key of the panel list. A panel that
  /// is hidden in AR is shown again; one that is streaming is left alone; a parked one is
  /// revived onto its window; any other starts a stream (a new QarOS panel on the next tick) or
  /// revives a retired one. Every refusal is the last error.
  /// @return false when nothing is streaming for @p key afterwards.
  bool streamPanelByKey(const QString & key);

  /// @brief THE close for a panel the OPERATOR chose to stream — the "Stop streaming"
  /// button, the sweep for a destroyed widget and a closed RViz dock all end here, so the
  /// manager's close mode applies to each of them alike.
  ///
  /// Forgets the host's own bookkeeping (@ref streamed_panels_, the dock hook),
  /// then asks the manager to close the panel (QtPanelStreamManager::close_panel):
  /// by default the AR panel is closed and its video sender destroyed, under
  /// `QAR_PANEL_CLOSE_MODE=notice` it becomes the closed-window notice, and while "AR
  /// Visibility" says hide it is parked: hidden in AR, its QarOS panel and sender kept until the
  /// session ends. Logs the outcome in words that say which one happened. Dialog panels are not
  /// closed here, the manager sweeps those itself.
  /// @param short_reason the phrase the manager's close line carries ("closed in
  ///        RViz", "stopped in RViz").
  /// @param notice_text what the notice says in notice mode (a full sentence).
  /// @return false if the manager was not streaming @p manager_key, unless a start of that stream
  ///         was waiting for its dock to be fitted: it is cancelled (cancelPendingDockStart()) and
  ///         the answer is true.
  bool closeStreamedPanel(
    const std::string & manager_key, const QString & short_reason, const QString & notice_text);

  /// @brief When @p widget lives in an RViz dock, close its AR panel (per the close mode:
  /// closed, a notice, or hidden and parked) when that dock is closed (its X button). RViz hosts
  /// every panel in an `rviz_common::PanelDockWidget`, an exported class whose `closed()`
  /// signal is emitted from its `closeEvent`, and without this hook the AR panel of a closed
  /// dock would go on streaming a blank rectangle. Re-showing the dock does NOT
  /// restart the stream: the operator streams it again with "Stream panel to AR".
  ///
  /// A widget with no such ancestor (an "Open video in AR" panel, which is
  /// parentless) is left alone. Replaces any hook already on @p manager_key.
  /// @param say false: the line that names the dock is not logged. The start of a stream that
  ///        waits for its dock to be fitted puts the hook in without it, so that the operator
  ///        closing the dock meanwhile ends the start; the line follows when the stream is added.
  void watchDockClose(const std::string & manager_key, QWidget * widget, bool say = true);

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
  ///
  /// The copy is made as high as its display tree needs before it streams: a SettledHeightFit
  /// takes a look every 100 ms (hidden_displays_fit_timer_) and the stream is added when the fit
  /// is over, so that its QarOS panel is made once, at the final size, and its first frame shows
  /// no scroll bar. A size from QAR_PANEL_HIDDEN_DISPLAYS_SIZE is the operator's and is not fitted.
  /// @return empty on success (including "already streaming" and "being fitted"), else the reason.
  QString startHiddenDisplaysStream();

  /// @brief The part of startHiddenDisplaysStream() that follows the fit: clears a stale stream,
  /// places the copy and adds the stream, then says so. Called at once when the copy is not
  /// fitted, else by advanceHiddenDisplaysFit() when the fit is over.
  /// @return empty on success, else the reason.
  QString streamHiddenDisplaysPanel();

  /// @brief Starts the fit of the hidden Displays copy, when it has not been fitted yet and its
  /// size is not the operator's: creates the fit and arms hidden_displays_fit_timer_.
  /// @return true when a fit runs now.
  bool beginHiddenDisplaysFit();

  /// @brief One look at the hidden Displays copy (hidden_displays_fit_timer_): resizes it, waits,
  /// or ends the fit and adds the stream. One `fit round` line per look and one `fitted to` line
  /// at the end, with the key 'hidden:Displays'. Ends the fit with a line when the QarOS session
  /// is no longer live.
  void advanceHiddenDisplaysFit();

  /// @brief Ends the fit of the hidden Displays copy without a stream, if one runs, and says why:
  /// "AR PANEL: 'hidden:Displays' start cancelled (<reason>) while the copy was being fitted.".
  /// The copy keeps the size it has and is fitted again at the next start.
  /// @return true when a fit was running.
  bool cancelHiddenDisplaysFit(const QString & reason);

  /// @brief Watches the display tree of the hidden Displays copy, once, when its stream is added:
  /// whenever the model of the tree inserts or removes rows, or the tree expands or collapses an
  /// item, onHiddenDisplaysTreeChanged() runs.
  void watchHiddenDisplaysTree();

  /// @brief The display tree of the copy changed. While the copy streams this starts
  /// hidden_displays_census_timer_ (one second) unless it runs, and when it fires the library is
  /// asked for a census of the copy under the reason `display tree changed`
  /// (requestHiddenDisplaysCensus()): at most one a second, taken after the changes of a burst of
  /// rows have come in. Diagnostic only: the copy is not fitted again, because a size change of a
  /// streamed panel rebuilds its AR panel.
  void onHiddenDisplaysTreeChanged();

  /// @brief The wait after a change of the display tree is over: asks for the census.
  void requestHiddenDisplaysCensus();

  /// @brief What startHiddenDisplaysWhenAPeerStreams() calls once the first peer's 3D stream is
  /// open: startHiddenDisplaysStream() unless QAR_PANEL_HIDDEN_DISPLAYS says off (then it says so,
  /// once, and does nothing). The copy waits for that stream so that it is not created before
  /// anything can receive it. The variable only switches the AUTOMATIC start off; "Stream panel
  /// to AR" on the entry in the panel list still works.
  void startHiddenDisplaysStreamOnConnect();

  /// @brief Called by driveLiveStreaming() on every tick: once a connect is pending for the
  /// hidden Displays copy and some peer has a 3D stream, says that the copy waited for it and
  /// starts it (startHiddenDisplaysStreamOnConnect()). Does nothing before that.
  void startHiddenDisplaysWhenAPeerStreams();

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

  /// @brief Refresh the AR-panel status line from the manager's diagnostics. The text is compact:
  /// the input line, the `panels:` line and one short line per panel (its size, the frames sent
  /// and the flags that apply). The long per-panel counters are in the heartbeat. When the lines
  /// would not fit the box without a scroll bar, the last panel lines are left out and a line says
  /// how many.
  void updateArPanelStatus();

  /// @brief The width of the text of the readout in pixels: its own when its page is on show,
  /// else derived from the page that is (all pages share one viewport).
  int readoutTextWidth() const;
  void appendLog(const QString & line) const;
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
  /// @brief Refreshes the state line of the status strip AND the buttons that follow the
  /// state ("Connect to QarOS" is off while a connect is pending, "Disconnect (pause)" is on
  /// while running, "Invite peer" is on while live and connected, "Stream panel to AR" is on
  /// while a QarOS session exists). Called on every path that changes or ends a connect.
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

  /// @brief Apply a render-tick period (ms; fractional). Stops/reconfigures/
  /// restarts render_timer_ (Qt::PreciseTimer for ms >= 1; a zero-interval timer for
  /// ms < 1, since QTimer cannot represent a sub-1ms period directly), resets the
  /// zero-interval throttle, forwards the value to the manager (informational, for
  /// the frame-trace CSV) and logs it. onInitialize() calls it once, with the period of
  /// QAR_PANEL_TICK_MS (0.1 ms when the variable is unset or not a period); the panel has no
  /// control for it.
  /// @param from_environment whether QAR_PANEL_TICK_MS gave @p ms, for the log line.
  void applyTickPeriodMs(double ms, bool from_environment = false);

  struct RenderSettings
  {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t fps = 60;
  };
  RenderSettings currentSettings() const;

  // --- UI widgets (owned by the Qt parent/child tree) ----------------------
  // The panel is a status strip over a QTabWidget of six scroll pages (Connection, AR panels,
  // AR tools, Picking, Settings, Diagnostics); buildUi() says what sits on which page.
  /// The two fixed lines above the tabs. Their texts are set with setFullText().
  QFrame * status_strip_ = nullptr;
  ElidedLabel * runtime_state_label_ = nullptr;
  QTabWidget * tabs_ = nullptr;
  /// The content widget of every page, in tab order (the scroll page owns it).
  std::vector<QWidget *> page_contents_;
  QPushButton * start_qar_button_ = nullptr;
  QPushButton * stop_qar_button_ = nullptr;
  /// "Invite peer": enabled only while live and connected; outside a live QarOS session it is
  /// disabled and its tool tip says what it needs. The simulated peer of the preview has its own
  /// button (add_sim_peer_button_).
  QPushButton * add_peer_button_ = nullptr;
  QPushButton * add_sim_peer_button_ = nullptr;
  QPushButton * remove_peer_button_ = nullptr;
  QPushButton * start_stream_button_ = nullptr;
  QPushButton * stop_stream_button_ = nullptr;
  QPushButton * refresh_button_ = nullptr;
  QPushButton * capture_button_ = nullptr;
  QPushButton * stereo_capture_button_ = nullptr;
  /// "Start test markers" / "Stop test markers" (Diagnostics tab). Follows test_markers_process_
  /// and the two QAR_PANEL_TEST_MARKERS_* variables; it is not saved in the configuration.
  QPushButton * test_markers_button_ = nullptr;
  /// The groups that one mode shows: "Invite a device" (every mode; only its button follows the
  /// session), "Advanced" (Settings, shown by "Show advanced options") and "Simulated peers"
  /// (Diagnostics; always shown in the preview, advanced when streaming to QarOS). "Advanced"
  /// keeps its room in its page while hidden; "Simulated peers" takes none, which cannot move the
  /// panel because every page scrolls.
  QGroupBox * invite_group_ = nullptr;
  QGroupBox * advanced_group_ = nullptr;
  QGroupBox * sim_peers_group_ = nullptr;
  // --- Settings tab: the persisted controls and the switches around them ---------------------
  QComboBox * resolution_combo_ = nullptr;
  QComboBox * fps_combo_ = nullptr;
  /// "Peer admission:" - invited or new (QAR_PANEL_PEER_ADMISSION may also say named or any).
  QComboBox * peer_admission_combo_ = nullptr;
  QCheckBox * hidden_displays_checkbox_ = nullptr;
  QCheckBox * ar_control_checkbox_ = nullptr;
  QCheckBox * show_advanced_checkbox_ = nullptr;
  /// "Closed AR panels:" - destroy or notice (QAR_PANEL_CLOSE_MODE). Disabled while hiding is on
  /// (refreshCloseModeControl()).
  QComboBox * close_mode_combo_ = nullptr;
  QCheckBox * ar_visibility_checkbox_ = nullptr;
  /// "Prime next frame" (QAR_ADAPTER_PRIME); default ON (checked). Not saved.
  QCheckBox * prime_checkbox_ = nullptr;
  // --- Connection tab: the AR control panel group (inert until the control panel exists) ----
  QLabel * ar_control_status_label_ = nullptr;
  QPushButton * ar_control_show_button_ = nullptr;
  QPushButton * ar_control_copy_button_ = nullptr;
  /// Which device type "Invite peer" invites, and the hostname HoloLens needs - it is the only
  /// device type the SDK can address by name. Both are shown in every mode; the button needs a
  /// live QarOS session (updateRuntimeStateLabel()).
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
  /// the page's own measurement (url_panel_sizing.hpp). Shows the "Web Panel Scale"
  /// setting (QAR_PANEL_WEB_SCALE, else the saved value, else 1.00) and is applied by
  /// "Re-open in AR", so the operator can tune a panel in a running session without a
  /// rebuild or a restart.
  QDoubleSpinBox * ar_panel_scale_spin_ = nullptr;
  QLabel * exposure_status_label_ = nullptr;
  QComboBox * ar_panel_combo_ = nullptr;
  QPushButton * refresh_panels_button_ = nullptr;
  QPushButton * stream_panel_button_ = nullptr;
  QPushButton * stop_panel_button_ = nullptr;
  /// The live "AR panels:" readout (make_readout_box()), and the box it is confined to.
  ///
  /// It sits in a scroll area for a reason that is not cosmetic. This label is
  /// inside the very widget that is streamed to AR, and it is rewritten several
  /// times a second with counters whose width and line count change. A plain
  /// QLabel would push its new size hint into the dock's layout, the dock would
  /// change width, QtPanelStream would see a new buffer size and rebuild the
  /// whole AR panel, and the rebuild would rewrite the label again -- an endless
  /// feedback loop. The box pins the height and the horizontal size policy is
  /// Ignored, so no text this label ever holds can move the layout by a pixel.
  /// A scroll bar in AR cannot be used, so the box has none: updateArPanelStatus()
  /// shortens the text until it fits and says how many panel lines it left out.
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
  ElidedLabel * last_error_label_ = nullptr;
  QString last_error_text_;

  // --- Persisted settings (panel_settings.hpp) ------------------------------
  /// The environment variables of the settings that were set, read once by the constructor.
  std::map<std::string, std::string> env_snapshot_;
  /// The operator's own values (canonical text): what load() read from the RViz configuration
  /// and what the operator has edited since. save() writes these.
  SettingsText user_settings_;
  /// The value in force for every setting and where it came from: the precedence rule applied to
  /// the two above. The connect and the readers of a setting use these.
  ResolvedSettings effective_settings_;
  /// What resolveSettings() refused, as sentences without the log prefix.
  std::vector<std::string> settings_warnings_;
  /// The sentences of SETTINGS: lines already logged, so each is logged once.
  std::set<std::string> settings_lines_logged_;

  // --- Size log ("PANEL SIZE:") ---------------------------------------------
  /// The constant size hints (compute_stable_hints(), measured at the end of buildUi()).
  QSize stable_size_hint_;
  QSize stable_minimum_size_hint_;
  /// onInitialize() has run: from then on the first show schedules the size report.
  bool panel_initialized_ = false;
  bool size_report_scheduled_ = false;
  bool size_reported_ = false;
  /// The last panel size that a log line named, and when a resize line was last logged.
  QSize last_logged_panel_size_;
  std::chrono::steady_clock::time_point resize_logged_at_{};
  /// Fires flushResizeLog() when resizeEvent() held a line back. A child of the panel.
  QTimer * resize_flush_timer_ = nullptr;
  /// Fires applyDefaultDockSize() once, a moment after load() found that the saved configuration
  /// asks for the default dock size. A child of the panel, never a QTimer::singleShot: a pending
  /// one would outlive the panel and still point into this plugin when RViz unloads it.
  QTimer * default_dock_timer_ = nullptr;
  /// load() has started default_dock_timer_ (or it has fired): the size is applied once.
  bool default_dock_size_armed_ = false;
  /// The saved settings version for the log line: its number, or "none".
  QString default_dock_saved_version_;
  /// Fires releaseDefaultDockHold() about 300 ms after applyDefaultDockSize() limited the heights
  /// of docks for the layout to settle. A child of the panel, never a QTimer::singleShot.
  QTimer * default_dock_release_timer_ = nullptr;
  /// The dock that was given the size, the limits that were set, and the size it was asked for.
  QPointer<QDockWidget> default_dock_held_;
  DockHold default_dock_hold_;
  QSize default_dock_asked_;
  /// Fires startPendingDockStreams() every 100 ms while a dock is fitted for its stream. A child of
  /// the panel, never a QTimer::singleShot.
  QTimer * dock_start_timer_ = nullptr;
  /// The dock streams that wait for dock_start_timer_, one per key.
  std::vector<PendingDockStart> pending_dock_starts_;
  /// startPendingDockStreams() is starting a stream: its dock has been grown and is not grown
  /// again.
  bool dock_start_resumed_ = false;
  /// Fires startFit() once the panel was first shown, then once per look. A child of the panel,
  /// never a QTimer::singleShot.
  QTimer * start_fit_timer_ = nullptr;
  /// armStartFit() has started start_fit_timer_: the fit at start is made once.
  bool start_fit_armed_ = false;
  /// The fit of the dock of this panel while startFit() looks at it; null before the first look
  /// and after the last. It is not in pending_dock_starts_: no stream waits for it.
  std::unique_ptr<PendingDockStart> start_fit_;

  // --- Test markers ------------------------------------------------------------------------
  /// The publisher process; null while none runs. Created by startTestMarkers(), a child of the
  /// panel, deleted later by its own finished signal or by releaseTestMarkersProcess().
  QProcess * test_markers_process_ = nullptr;
  /// stopTestMarkers() has killed it and the finished signal has not come yet.
  bool test_markers_stop_requested_ = false;
  /// What the process printed that does not yet end in a line break.
  QByteArray test_markers_pending_;
  /// When a line of its output was last logged (epoch = none yet), and how many lines the rate
  /// limit has held back since then and over the whole run.
  std::chrono::steady_clock::time_point test_markers_logged_at_{};
  std::uint64_t test_markers_held_back_ = 0;
  std::uint64_t test_markers_held_back_total_ = 0;

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
  /// @brief The callback group on RViz's node (ros_node_) that every ROS entity this panel makes
  /// there joins: gps_sub_ and everything of the qar_ros_ui bridges (NodeHandles::callback_group).
  ///
  /// A callback group holds a weak reference to each of its entities, and that reference is
  /// released through code of this library (the rclcpp templates that made the entity are
  /// instantiated here). RViz's node outlives this library: RViz frees the library first and
  /// destroys its node later, so a reference left in a group of that node, the default group
  /// included, is released through unloaded code. This group is the panel's own:
  /// releaseRosEntities() lets it go, and its references with it, while the library is loaded.
  /// RViz's executor spins it with the node (it is added to the executor with the node) and holds
  /// it only weakly. Made by rosCallbackGroup(); null while there is no node.
  rclcpp::CallbackGroup::SharedPtr ros_callback_group_;
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
  /// ros_callback_group_, made on ros_node_ the first time; null while there is no node.
  /// Throws what rclcpp throws when the node cannot make a group.
  rclcpp::CallbackGroup::SharedPtr rosCallbackGroup();
  /// @brief For the destructor: the ROS entities of the AR tools, the spatial publisher, the
  /// bridges and the geolocation subscription go, then ros_callback_group_. GUI thread; returns at
  /// once.
  void releaseRosEntities();
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
    /// The AR panel is made once the first frame has given the widget its size: a later size
    /// change makes the library close the QarOS panel and create it again, and a close in the
    /// middle of a session can crash the desktop Visualizer. Until then the entry waits with its
    /// source and its widget, and startPendingVideoPanel() makes the AR panel.
    bool pending = false;
    /// The key is a retired notice slot, which the AR panel made later revives.
    bool reviving = false;
    ImageTransport transport = ImageTransport::Raw;
    std::chrono::steady_clock::time_point since{};  ///< when the button was pressed
    bool waiting_logged = false;
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
  /// A connect has been adopted and the hidden Displays copy has not been started for it yet: the
  /// first tick on which a peer has a 3D stream starts it (see driveLiveStreaming()).
  bool hidden_displays_start_pending_ = false;
  /// The fit of the hidden Displays copy while it runs (null otherwise), the timer that takes one
  /// look per 100 ms, the width the copy keeps while its height is fitted, and whether the copy
  /// still has to be fitted: true from its creation until a fit ends. The timer is a child of the
  /// panel, never a QTimer::singleShot.
  std::unique_ptr<SettledHeightFit> hidden_displays_fit_;
  QTimer * hidden_displays_fit_timer_ = nullptr;
  int hidden_displays_fit_width_ = 0;
  bool hidden_displays_needs_fit_ = false;
  /// The wait of one second between a change of the display tree of the copy and the census it
  /// earns (a child of the panel, never a QTimer::singleShot), and whether the tree is watched.
  QTimer * hidden_displays_census_timer_ = nullptr;
  bool hidden_displays_tree_watched_ = false;

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
  /// The room height (m) of the centre of the app volume as it was configured at connect
  /// (QAR_PANEL_VOLUME_Y, else the adapter's default). While the scene is where it was made, the
  /// RViz ground is at this height, so it is the ground the placement rule falls back to until
  /// QarOS has reported the live placement.
  float volume_center_y_m_ = 1.0f;
  /// How the panels with a fixed top edge are hung (QAR_PANEL_FIXED_POSES, read at the first
  /// connect). The default is the room: the top edge at kFixedPanelTopM.
  FixedPoseMode fixed_pose_mode_ = FixedPoseMode::Room;
  /// The frame QarOS draws around the video of an AR panel, four insets in metres: a panel is
  /// made as tall as its video needs inside it, a dialog is given the width it needs
  /// (QAR_PANEL_AR_FRAME or QAR_PANEL_AR_HEADROOM, read at the first connect; see
  /// parse_ar_frame). The panel manager is given this value, and the panel asks the manager for
  /// the height of a panel (ar_panel_height_m), so that the ground rule works on the height that
  /// is sent.
  qar_qt_streaming::PanelFrame ar_frame_{};
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
    /// The operator asked for the panel to come back while hiding is the way of the switch:
    /// the next tick asks QarOS to show it again and, if that fails, reports it (nothing is
    /// closed or made again while hiding is the way of the switch).
    bool pending_show = false;
    /// The AR control panel (or the operator) switched the tool off: the next tick hides its
    /// QarOS panel. The exposure, the page and the ROS entity stay.
    bool pending_hide = false;
    /// The QarOS panel exists and QarOS accepted a hide: the tool's switch reads off.
    bool ar_hidden = false;
    /// When the QarOS panel could not be made or taken over, the earliest time to try again (a
    /// refused attempt must not be repeated on every render tick).
    std::chrono::steady_clock::time_point open_retry_at{};
    /// What the live panel was last given, so a viewport report can resize it
    /// without recomputing where it hangs in the room.
    qar_ar_core::UrlPanelPlacement placement;
    /// The pose of the tool's slot before the ground rule: the rule is applied to it again when
    /// the panel is resized, so that a panel that becomes shorter comes back down.
    QarPose nominal_pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
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
  /// Open the QarOS URL panel for @p tool, or take over the parked panel of the same entity
  /// (parked_tool_panels_): it is pointed at the new page, sized and shown. Empty on success,
  /// else the reason (which leaves the tool pending so a later tick retries).
  QString openArPanelFor(ArToolPanel & tool);

  /// What takeOverParkedToolPanel() found and did.
  enum class ParkedToolPanelTakeOver { NoPanel, Done, Refused };
  /// If the entity of @p tool has a parked QarOS panel, points it at the page of @p row, gives
  /// it @p placement and shows it: the tool then owns it. QarOS refusing a step leaves the panel
  /// parked, sets the last error of @p row and holds the tool's next attempt back for a moment;
  /// nothing is closed or made. @p size_note is the sizing remark for the log.
  ParkedToolPanelTakeOver takeOverParkedToolPanel(
    ArToolPanel & tool, ExposedEntity & row, const qar_ar_core::UrlPanelPlacement & placement,
    const QString & size_note);
  /// "Re-open in AR", from the desktop or from the AR control panel. With hiding as the way of
  /// the switch the next tick shows the QarOS panel again; otherwise (and when there is nothing
  /// to show) it is closed and made again on the next tick.
  void reopenArTool(ArToolPanel & tool, ExposedEntity & row);
  /// The tick that follows reopenArTool() with hiding: asks QarOS to show the panel again. If
  /// QarOS refuses, hiding as the way of the switch reports it and leaves the panel as it is;
  /// with closing as the way of the switch the panel is closed and made again.
  void showArToolAgain(ArToolPanel & tool);
  /// The tick that follows a switch-off of the tool: asks QarOS to hide the panel. A refusal is
  /// reported and the panel stays visible; nothing is closed.
  void hideArTool(ArToolPanel & tool);
  /// Gives the live QarOS panel of @p tool the size @p size, hung from the pose of its slot
  /// raised above the RViz ground for that height. @return false when QarOS refuses (the reason is
  /// the panel's last error and the placement is unchanged).
  bool placeArToolPanel(ArToolPanel & tool, const QarGuiPanelSize & size);
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
  /// A key already live (has_panel && !is_tombstoned) is a no-op ack, and so is a press while
  /// the panel waits for its first frame. A parked key is shown again through
  /// streamPanelByKey(). Anything else (never opened, or tombstoned) gets a
  /// `RosImageFrameSource` (shared with variant (b) for the same topic via
  /// @ref acquireFrameSource, or a new subscription if none exists yet) + a BRAND NEW
  /// `VideoPanelWidget` (a tombstoned slot cannot be revived onto its old widget: reviving
  /// means a fresh `VideoPanelWidget`, since its `LatestVideoFrame` is fixed at construction;
  /// the frame SOURCE may still be reused). The viewer is answered at once; the AR panel is made
  /// by tickVideoPanels() once the first frame has arrived (startPendingVideoPanel()).
  void openVideoPanel(ArToolPanel & tool, const qar_web_panels::PanelControlEvent & event);

  /// What startPendingVideoPanel() did.
  enum class VideoPanelStart { Waiting, Made, Failed };
  /// @brief Make the AR panel of a video entry that waits for its first frame. Until a frame
  /// has arrived it only says once, after 5 s, that it is waiting. Then the widget takes the
  /// aspect ratio of the camera, the ground rule is applied for the final height, and
  /// `panel_manager_->add_panel` adds the widget, which revives a tombstoned key onto it rather
  /// than minting a second sender. The panel is then the size of the video from its creation,
  /// so QarOS never has to close and create it again for the aspect ratio.
  /// @return Waiting (no frame yet), Made, or Failed (the manager refused; the entry is dropped).
  VideoPanelStart startPendingVideoPanel(const std::string & key, VideoPanel & panel);

  /// @brief Stop @p key's ROS subscription (freeing CPU) the first time its
  /// panel is observed tombstoned, make the AR panels of the entries that waited for their first
  /// frame, and sync_to_frame() every panel still live.
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

  // --- The AR control panel -------------------------------------------------------------------
  // One page with the panel's main controls, served by web_panel_server_ and shown in AR as a
  // QarOS URL panel in front of each user. Everything below borrows the server's session and the
  // QarOS session of the render path, so it is declared AFTER web_panel_server_ and
  // url_panel_factory_ (and before ar_tools_): the QarOS panels of the instances are closed
  // first, then the server, and the session they belong to (qar_factory_) lives longest.

  using SteadyTime = std::chrono::steady_clock::time_point;

  /// One QarOS URL panel that shows the page.
  struct ArControlInstance
  {
    std::string key;                       ///< "everybody", or "peer:" and the 32 hex digits
    ControlPanelInstanceRequest request;   ///< who it is for and where it was placed
    /// Null until the QarOS panel exists (or after it could not be made).
    std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> adapter;
    qar_ar_core::UrlPanelPlacement placement;  ///< what the QarOS panel was last given
    float raised_m = 0.0f;                 ///< how far the ground rule raised the panel (0 = not)
    float ground_room_y = 0.0f;            ///< room height of the RViz ground it was checked with
    bool wanted_visible = true;            ///< what the placement policy last asked for
    bool show_failed = false;              ///< the last "Show AR control panel" was refused
    bool has_head = false;                 ///< head and gaze below belong to the pose
    QarVector3 head{};
    QarVector3 gaze{};
    std::string last_error;
    std::string logged_error;              ///< the failure already written to the log
    std::chrono::steady_clock::time_point next_attempt{};
  };

  /// A command of the page that the next render tick carries out.
  struct QueuedControlCommand
  {
    ControlPanelCommandKind kind = ControlPanelCommandKind::None;
    std::string key;    ///< the panel's manager key, or the tool's panel id
    std::string title;  ///< what the operator knows it as
    bool on = false;
  };

  /// A panel switch the page has already drawn: checked against the panel's real state, and the
  /// page is told the truth when they disagree.
  struct PendingToggle
  {
    std::string key;     ///< the panel's manager key, or the tool's panel id when @ref tool is set
    bool tool = false;   ///< the switch of an AR tool, not of a streamed panel
    bool on = false;
    bool failed = false;
    std::chrono::steady_clock::time_point at{};
  };

  /// A hide or show of a streamed panel that the next manager tick carries out: the direction
  /// that was asked for, and the manager's counters at the time, to tell what happened.
  struct VisibilityRequest
  {
    bool visible = true;
    std::uint64_t changes_seen = 0;
    std::uint64_t failures_seen = 0;
    std::chrono::steady_clock::time_point since{};
  };

  std::shared_ptr<qar_web_panels::PanelSession> ar_control_session_;
  ArControlPanelState ar_control_state_;
  std::string ar_control_page_url_;
  /// Why the page is not served (empty = nothing went wrong).
  QString ar_control_page_error_;
  ControlPanelPlacementPolicy control_policy_;
  bool control_flip_facing_ = false;
  std::vector<ArControlInstance> control_instances_;
  std::vector<QueuedControlCommand> control_commands_;
  std::vector<PendingToggle> control_toggles_;
  /// Hides and shows of streamed panels waiting for the manager's answer, by manager key.
  std::map<std::string, VisibilityRequest> visibility_requests_;
  /// When checkVisibilityOutcomes() last looked (it looks at most every 20 ms).
  std::chrono::steady_clock::time_point visibility_checked_{};
  /// The state of the page changed, or the operator did something: build the snapshot at the first
  /// tick that is 50 ms or more after the last one, instead of waiting for the 250 ms idle pace.
  /// It stays set until a snapshot is built, so a burst of changes costs one snapshot per 50 ms.
  bool ar_control_dirty_ = true;
  /// Send the whole document with the next publish (a command was refused or did not happen).
  bool ar_control_resync_ = false;
  bool ar_control_publish_failed_logged_ = false;
  std::chrono::steady_clock::time_point ar_control_snapshot_last_{};
  std::chrono::steady_clock::time_point control_placement_last_{};
  std::chrono::steady_clock::time_point ar_control_status_last_{};
  bool ar_control_status_dirty_ = true;
  QString ar_control_status_text_;
  std::size_t ar_control_viewers_ = 0;
  /// Since when instances exist and no viewer is connected to the page.
  std::chrono::steady_clock::time_point ar_control_no_viewer_since_{};
  /// The newest viewport a viewer of the page reported; the QarOS panels are sized to it.
  std::optional<qar_ui_model::ViewportBody> ar_control_viewport_;
  bool ar_control_resize_pending_ = false;
  /// "Show AR control panel" was pressed: show the instances (first press) or make the ones that
  /// could not be shown again (second press within ten seconds).
  bool control_show_requested_ = false;
  bool control_recreate_requested_ = false;
  bool control_show_failed_ = false;
  std::chrono::steady_clock::time_point control_show_failed_at_{};
  std::string control_show_failure_;
  /// When QarOS last accepted a "Show AR control panel" (empty = never); the line says so for
  /// ten seconds.
  std::chrono::steady_clock::time_point control_show_accepted_at_{};
  bool control_scene_unknown_logged_ = false;
  /// A pick command of the AR control panel is setting the Mode combo: onSpatialModeChanged()
  /// says so in its log line.
  bool spatial_mode_from_control_ = false;
  /// The peers that have a render stream (live mode), in the order the streams were opened.
  std::vector<QarPeerId> live_peers_;

  /// @brief Serves the control page when the "AR Control Panel" setting is on (the preview and
  /// every live connect call this) and, @p live, starts the placement policy's wait. Says why
  /// when it does nothing. Idempotent.
  void startArControlPage(bool live);
  /// @brief Serves the control page (once), with its control sink and its signals.
  /// @return empty on success, else the reason.
  QString ensureArControlPage();
  /// @brief Mints the QarOS URL panel factory against the render path's session, once.
  void ensureUrlPanelFactory();
  /// @brief Per-tick work of the control panel, called before the early-outs of the render tick:
  /// carries out the queued commands, creates and hides the instances, applies a resize and a
  /// "Show AR control panel" press, publishes the state of the page and refreshes the status
  /// line. Every QarOS call of the feature is made from here, never from a click or a control
  /// event.
  void tickArControlPanel();
  void runArControlCommands(SteadyTime now);
  void runPanelSwitch(const QueuedControlCommand & command, SteadyTime now);
  /// Switches an AR tool on or off for the page: asks the next tick to show or hide the tool's
  /// QarOS panel, and remembers the switch the page has drawn ahead of the tool.
  void runToolSwitch(const QueuedControlCommand & command, SteadyTime now);
  void tickArControlPlacement(SteadyTime now);
  bool createArControlInstance(ArControlInstance & instance, SteadyTime now);
  void logArControlInstance(const ArControlInstance & instance);
  ArControlInstance * findControlInstance(const std::string & key);
  void applyInstanceVisibility(ArControlInstance & instance, bool visible);
  void applyArControlShow(SteadyTime now);
  void applyArControlResize();
  void publishArControlState(SteadyTime now);
  void refreshArControlStatus(bool force);
  QString arControlStatusText(SteadyTime now) const;
  qar_web_panels::UrlPanelSizeResult arControlPanelSize();
  /// Everything the page shows, gathered from the desktop's state.
  ControlPanelSnapshot controlPanelSnapshot();
  /// A control event of the page. Refusals are acked and logged; pick commands run at once;
  /// panel and tool commands are queued for the next tick.
  void onArControlEvent(
    qar_web_panels::PanelSession & session, const qar_web_panels::PanelControlEvent & event);
  void refuseArControl(
    qar_web_panels::PanelSession & session, const qar_web_panels::PanelControlEvent & event,
    const std::string & error);
  void acceptArControl(
    qar_web_panels::PanelSession & session, const qar_web_panels::PanelControlEvent & event,
    const std::string & text);
  void onArControlViewers(std::size_t count);
  void onArControlViewport(const qar_web_panels::PanelViewportReport & report);
  /// Arms the desktop's Mode combo for a command of the page. False when the combo has no such
  /// mode.
  bool setPickModeFromControl(ArPickMode mode);
  /// The body of the Confirm button; returns the error text (empty on success).
  QString confirmSpatialPick(std::string * published_topic);

  // --- Hiding panels in AR instead of closing them ---------------------------------------------
  /// The "AR Visibility" setting as it is now: true = hide and show, false = close and make again.
  bool arVisibilityHides() const;
  /// Asks the manager to hide (@p visible false) or show the streamed panel @p key on its next
  /// tick and remembers the request, so the outcome can be judged after that tick.
  /// @return false when the manager refused the request at once (the reason is logged).
  bool requestPanelVisibility(const std::string & key, bool visible);
  /// After the manager's tick: logs the hides and shows it carried out and, for one it could not
  /// carry out, reports it. While hiding is the way of the switch nothing is closed and nothing is
  /// made again: a hide that QarOS refused leaves the panel visible, a show that QarOS refused
  /// leaves it as it is, and both are the last error. With closing as the way of the switch a
  /// refused hide closes the panel and a refused show makes it again (except in close mode
  /// notice, where a close keeps the QarOS panel and the panel is left as it is). A failure
  /// marks the page's switch as failed.
  void checkVisibilityOutcomes();
  /// The name the operator knows the streamed panel @p key by.
  QString streamableTitle(const std::string & key);

  /// The QarOS panels of AR tools that were removed while hiding is the way of the switch: hidden
  /// and kept, by entity kind and name, until the session ends or the same entity is exposed
  /// again (which takes the panel over). Closing a QarOS panel while a session runs crashed the
  /// desktop Visualizer, so a removed tool's panel is never closed before the session ends; the
  /// adapter closes it once when it is destroyed. Declared AFTER the members the adapters borrow
  /// (the QarOS session of qar_factory_ and the URL panel factory) so it dies before them.
  struct ParkedToolPanel
  {
    std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> adapter;
    qar_ar_core::UrlPanelPlacement placement;  ///< what the QarOS panel was last given
    /// The retirement is a desktop click and makes no QarOS call: the tick that follows asks
    /// QarOS to hide the panel (hideParkedToolPanels()) and logs what became of it.
    bool pending_hide = false;
    std::string name;  ///< the retired tool's entity name, for the retire line
  };
  std::map<std::string, ParkedToolPanel> parked_tool_panels_;
  /// The key of a tool's parked panel: the entity kind and name.
  static std::string parkedToolKey(RosEntityKind kind, const std::string & name);
  /// Ask QarOS to hide the panels that retirements parked since the last tick. Runs at the top
  /// of tickArToolPanels(), before the tools that are left open or take a parked panel over.
  void hideParkedToolPanels();
  /// Hide one parked panel (clearing its pending_hide) and log the retire line of its tool: the
  /// panel is parked, or QarOS refused the hide and it stays visible and kept. Never closes.
  void carryOutParkedHide(ParkedToolPanel & parked);

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
