// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/qar_streaming_panel.hpp"

#include "qar_rviz_streaming/ar_semantic_templates.hpp"
#include "qar_rviz_streaming/panel_display_names.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIODevice>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <pluginlib/class_list_macros.hpp>

// Display discovery. NOT pluginlib::ClassLoader: its header pulls in
// <tinyxml2.h>, which the ROS 2 Humble Windows distro does not install.
#include <ament_index_cpp/get_resource.hpp>
#include <ament_index_cpp/get_resources.hpp>

#include "qar_ogre_streaming/ogre_opengl_texture_bridge.hpp"  // GlFenceHook
#include "qar_ogre_streaming/ogre_qar_stereo_stream.hpp"  // complete type for set_active()

#include "qar_rviz_streaming/mock_sender_factory.hpp"
#include "qar_rviz_streaming/qar_offscreen_capture.hpp"

// Live streaming: the real-QAR-C-API sender factory and the QarOS panel, input and
// gesture adapters.
#include "qar_qar_adapter/env_config.hpp"
#include "qar_qar_adapter/geo_anchor.hpp"
#include "qar_qar_adapter/qar_capi_panel_sender_adapter.hpp"
#include "qar_qar_adapter/qar_capi_render_sender_adapter.hpp"
#include "qar_qar_adapter/qar_capi_spatial_gesture_source.hpp"
// The QarOS side of a browser-backed panel — the URL sibling of the panel
// sender above. It borrows the same QarSession the render path owns.
#include "qar_qar_adapter/qar_capi_url_panel_adapter.hpp"
#include "qar_ros_ui/introspection.hpp"
#include "qar_ros_ui/node_handles.hpp"
// getDisplayContext() is declared by rviz_common::Panel, but reaching
// getRosNodeAbstraction() through it needs the complete DisplayContext type.
#include "rviz_common/display_context.hpp"
#include "rviz_common/ros_integration/ros_node_abstraction_iface.hpp"
// "Add topic": DisplayGroup::createDisplay + addDisplay are what
// VisualizationManager::createDisplay is built from, and Display::setTopic is
// what RViz's own "New display by topic" window calls.
//
// NOT visualization_manager.hpp: it reaches pluginlib/class_loader.hpp (through
// transformation_manager.hpp -> pluginlib_factory.hpp), which includes
// <tinyxml2.h> — and the ROS 2 Humble Windows distro ships tinyxml2's DLL and a
// CMake shim but NOT its headers, so that include does not compile here.
// display_group.hpp pulls in only display.hpp and is therefore clean.
#include "rviz_common/display.hpp"
#include "rviz_common/display_group.hpp"
// A streamed dock panel is closed in AR when its RViz dock is closed: RViz hosts
// every panel in an rviz_common::PanelDockWidget (an exported class), and its
// closed() signal is emitted from closeEvent — i.e. by the dock's X button.
// Pulls in only <QDockWidget>, <QLabel> and the config/visibility headers
// panel.hpp already brought.
#include "rviz_common/panel_dock_widget.hpp"

// The hidden Displays panel: the off-screen window state (reusable, Qt only) and the
// RViz side that makes the panel (needs rviz_common, so it lives in this package).
#include "qar_qt_streaming/offscreen_panel.hpp"
#include "qar_rviz_streaming/hidden_rviz_panel.hpp"

#include "qar_rviz_streaming/rviz_display_context_adapter.hpp"
#include "qar_rviz_streaming/rviz_render_lock.hpp"
#include "qar_rviz_streaming/rviz_render_thread_executor.hpp"

namespace qar_rviz_streaming
{
namespace
{

/// QAR_PANEL_HIDDEN_DISPLAYS=off keeps the hidden Displays panel from starting on
/// connect (qar_qar_adapter::parse_bool_env words; default on).
constexpr const char * kHiddenDisplaysEnv = "QAR_PANEL_HIDDEN_DISPLAYS";

/// QAR_PANEL_HIDDEN_DISPLAYS_SIZE=WxH, in logical pixels, each side clamped to 200..2000
/// (qar_qt_streaming::parse_offscreen_size). Default 480x720.
constexpr const char * kHiddenDisplaysSizeEnv = "QAR_PANEL_HIDDEN_DISPLAYS_SIZE";

/// What the AR panel is titled in the headset.
constexpr const char * kHiddenDisplaysPanelName = "Displays (AR)";

/// The panel-list entry that starts the hidden Displays copy by hand. U+2014 is spelled
/// as a code point so the label does not depend on how the compiler reads this file.
QString hiddenDisplaysLabel()
{
  return QStringLiteral("Displays (AR copy ") + QChar(0x2014) + QStringLiteral(" hidden in RViz)");
}

/// @brief Stop a widget's content from deciding this panel's width.
///
/// This panel streams ITSELF to AR, and QtPanelStream rebuilds the whole QarOS
/// panel (sender + panel + click/hover subscription) whenever the captured
/// buffer size changes. So any status widget in here whose text can change is a
/// potential feedback loop: longer text -> wider size hint -> wider dock ->
/// buffer size change -> panel rebuilt -> status rewritten. QSizePolicy::Ignored
/// on the horizontal axis is the direct answer — the layout stops asking the
/// widget how wide it would like to be and simply gives it what is available.
///
/// heightForWidth is preserved rather than reset, so a word-wrapped label still
/// reflows into whatever width it is handed instead of being clipped.
void makeWidthNeutral(QWidget * widget)
{
  if (widget == nullptr) {
    return;
  }
  QSizePolicy policy = widget->sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Ignored);
  widget->setSizePolicy(policy);
}

/// The QarOS panel display names this process has handed out. One object for the whole
/// process, so that every QarStreamingPanel of the process counts together.
PanelDisplayNames & process_panel_display_names()
{
  static PanelDisplayNames names;
  return names;
}

}  // namespace

QarStreamingPanel::QarStreamingPanel(QWidget * parent)
: rviz_common::Panel(parent)
{
  // The semantic layouts go in ahead of the generic form, which stays as the
  // fallback for everything they do not claim.
  register_semantic_templates(panel_templates_);
  buildUi();

  // The picker MUST use the same room -> ROS frame change the camera uses,
  // read from the same environment qar_pose_conversion.cpp reads. Anything else
  // puts the pick where the AR user was not pointing, silently.
  spatial_.set_transform(qar_ar_core::room_to_ros_from_environment());
  refreshSpatialUi();
  appendLog("Qar streaming panel constructed.");

  // Per-frame stage timing (opt-in: QAR_VIDEO_TIMING_DIR, the launcher's -TimingCsv). Say so
  // loudly, so a log can never be mistaken for an uninstrumented run, and open the
  // render-tick gap file (the stage files are opened by the sources/streams themselves).
  {
    qar_ar_core::TimingLog & timing_log = qar_ar_core::TimingLog::instance();
    if (timing_log.enabled()) {
      gui_gap_timing_ = timing_log.open(
        "gui_gaps", {"start_us", "end_us", "gap_us"});
      appendLog(
        QString("TIMING: per-frame stage timing is ON -> %1 (video_*.csv, rx_*.csv, "
          "render3d*.csv, gui_gaps.csv; epoch microseconds; flushed ~1/s by a background "
          "thread, never on this one)")
        .arg(QString::fromStdString(timing_log.directory())));
    }
  }
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
  // Pairs the timeBeginPeriod(1) taken in onInitialize (no-op if it never ran).
  scheduling_guard_.release();
  // Per-frame timing CSVs (QAR_VIDEO_TIMING_DIR): stop the writer thread and write what is
  // still queued. The thread must not outlive this plugin's code (RViz may unload the
  // library), so this is a join, not a detach. A later panel simply restarts it.
  gui_gap_timing_.reset();
  qar_ar_core::TimingLog::instance().shutdown();
}

// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------
void QarStreamingPanel::buildUi()
{
  auto * root = new QVBoxLayout(this);

  // --- Qar runtime ---------------------------------------------------------
  auto * runtime_group = new QGroupBox("Qar runtime", this);
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
  // FPS options MUST include 30 and 60.
  fps_combo_ = new QComboBox(settings_group);
  for (const int fps : {15, 30, 60, 72, 90}) {
    fps_combo_->addItem(QString("%1 fps").arg(fps), fps);
  }
  fps_combo_->setCurrentIndex(2);  // 60 fps (the optimum target)
  settings_layout->addRow("Resolution:", resolution_combo_);
  settings_layout->addRow("FPS:", fps_combo_);

  // --- Frame-timing diagnostics (QAR_PANEL_TICK_MS / QAR_ADAPTER_PRIME) ----
  // See docs/ARCHITECTURE.md#the-render-tick. These are live A/B switches for
  // telling a fixed-tick wait apart from an SDK pose/frame mismatch, not a change
  // to the render budget above. Priming defaults to on; the render-tick default is
  // 0.1 ms, so the panel starts in the tightest stock A/B state.
  tick_combo_ = new QComboBox(settings_group);
  const QString qtick_ms = qEnvironmentVariable("QAR_PANEL_TICK_MS");
  const QByteArray qtick_ms_utf8 = qtick_ms.toUtf8();
  const double env_tick_ms = qar_qar_adapter::parse_tick_ms_env(
    qtick_ms.isEmpty() ? nullptr : qtick_ms_utf8.constData(), 0.1);
  // QTimer's own interval cannot represent sub-1ms periods; 0.1 ms is realized as a
  // zero-interval timer (fires whenever the event loop is otherwise idle) plus a
  // steady-clock throttle in onRenderTick() that runs the tick body at most once per
  // its nominal period (100us here) — see applyTickPeriodMs().
  for (const double ms : {16.0, 8.0, 4.0, 2.0, 1.0, 0.1}) {
    tick_combo_->addItem(QString("%1 ms").arg(ms), ms);
  }
  int tick_index = -1;
  for (int i = 0; i < tick_combo_->count(); ++i) {
    if (std::fabs(tick_combo_->itemData(i).toDouble() - env_tick_ms) < 1e-9) {
      tick_index = i;
      break;
    }
  }
  if (tick_index < 0) {
    // Non-standard env value (e.g. QAR_PANEL_TICK_MS=33): append it rather than
    // silently rounding to the nearest stock entry.
    tick_combo_->addItem(QString("%1 ms").arg(env_tick_ms), env_tick_ms);
    tick_index = tick_combo_->count() - 1;
  }
  tick_combo_->setCurrentIndex(tick_index);
  tick_combo_->setToolTip(
    "Render-tick period. A request can wait up to one full tick for us to notice it "
    "is ready (see docs) — this is a live A/B switch, not a change to the per-peer "
    "FPS budget below, which still governs how often a peer is actually due. 0.1 ms "
    "runs the event loop as fast as it will go, throttled to at most once per 100us.");
  settings_layout->addRow("Render tick:", tick_combo_);

  prime_checkbox_ = new QCheckBox("Prime next frame", settings_group);
  const QString qprime = qEnvironmentVariable("QAR_ADAPTER_PRIME");
  const QByteArray qprime_utf8 = qprime.toUtf8();
  prime_checkbox_->setChecked(
    qar_qar_adapter::parse_bool_env(qprime.isEmpty() ? nullptr : qprime_utf8.constData(), true));
  prime_checkbox_->setToolTip(
    "Start the next frame's SDK wait right after this one is shown (one tick per "
    "frame). Off: the wait starts at the next tick's "
    "begin_frame instead (two ticks per frame).");
  settings_layout->addRow(prime_checkbox_);

  root->addWidget(settings_group);

  // --- Peers ---------------------------------------------------------------
  auto * peers_group = new QGroupBox("Peers", this);
  auto * peers_layout = new QVBoxLayout(peers_group);
  // Device selector for LIVE mode: which kind of real peer the add button invites.
  // Hidden in the offline preview (onInitialize shows it for live mode), where
  // "add" means a simulated peer.
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
  add_peer_button_ = new QPushButton("Add simulated peer", peers_group);
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

  // --- ROS topics + AR GUI panels ------------------------------------------
  auto * ar_group = new QGroupBox("AR GUI panels", this);
  auto * ar_layout = new QVBoxLayout(ar_group);

  topic_watch_list_ = new QListWidget(ar_group);
  topic_watch_list_->setToolTip(
    "Topics added from the topic browser. Adding one also creates the matching "
    "RViz display, so it appears in the streamed 3D scene.");
  auto * topic_buttons = new QHBoxLayout();
  add_topic_button_ = new QPushButton("Add from ROS graph...", ar_group);
  add_topic_button_->setToolTip(
    "Browse the live ROS 2 graph — topics, services and actions — add a topic to "
    "RViz, and/or expose any of them as a QAR AR tool.");
  remove_topic_button_ = new QPushButton("Remove topic", ar_group);
  topic_buttons->addWidget(add_topic_button_);
  topic_buttons->addWidget(remove_topic_button_);

  // Any RViz dock panel can be streamed, not just this one — the combo is
  // repopulated from the live dock widgets each time it is opened.
  auto * panel_pick_row = new QHBoxLayout();
  ar_panel_combo_ = new QComboBox(ar_group);
  ar_panel_combo_->setToolTip(
    "Which Qt panel to publish as a QarOS GUI panel. Any RViz dock panel works; "
    "this QAR panel is listed first.");
  refresh_panels_button_ = new QPushButton("Refresh", ar_group);
  refresh_panels_button_->setToolTip("Re-scan RViz for dock panels (they come and go).");
  panel_pick_row->addWidget(ar_panel_combo_, 1);
  panel_pick_row->addWidget(refresh_panels_button_);

  auto * panel_buttons = new QHBoxLayout();
  stream_panel_button_ = new QPushButton("Stream panel to AR", ar_group);
  stream_panel_button_->setToolTip(
    "Publish the selected panel as a QarOS GUI panel, so it is visible and clickable "
    "in AR. Needs a running QarOS session.");
  stop_panel_button_ = new QPushButton("Stop streaming", ar_group);
  panel_buttons->addWidget(stream_panel_button_);
  panel_buttons->addWidget(stop_panel_button_);

  // The live "AR panels:" readout. Built to be LAYOUT-NEUTRAL and READABLE, in
  // that order of priority, because this panel streams ITSELF to AR:
  //
  //  * layout-neutral, or the readout drives the very feedback loop it exists to
  //    diagnose. The text is rewritten several times a second with counters of
  //    changing width and line count; a plain word-wrapped QLabel turns that
  //    into a changing size hint -> a changing dock width -> a changing capture
  //    buffer -> QtPanelStream tearing down and rebuilding the AR panel -> the
  //    status line being rewritten again. That loop freezes the panel stream and
  //    starves the 3D stream of its sender.
  //    An Ignored horizontal policy means the label's own width preference is
  //    never consulted, and the scroll area's fixed height pins the other axis,
  //    so no text can move this widget's geometry by a pixel.
  //
  //  * readable, because a clipped counter is a counter the operator cannot
  //    use. Pinning the height without somewhere for the overflow to go would
  //    truncate the text to a few lines, so the viewport scrolls, the text is
  //    selectable for copying into a report, and the height is set from the
  //    FONT rather than a pixel guess.
  ar_panel_status_label_ = new QLabel("AR panels: none");
  ar_panel_status_label_->setWordWrap(true);
  ar_panel_status_label_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  ar_panel_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  ar_panel_status_label_->setMinimumWidth(0);
  makeWidthNeutral(ar_panel_status_label_);

  ar_panel_status_scroll_ = new QScrollArea(ar_group);
  ar_panel_status_scroll_->setWidget(ar_panel_status_label_);
  ar_panel_status_scroll_->setWidgetResizable(true);
  ar_panel_status_scroll_->setFrameShape(QFrame::StyledPanel);
  ar_panel_status_scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  ar_panel_status_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  ar_panel_status_scroll_->setToolTip(
    "Live AR panel counters. Scrolls — this box never resizes, because it is "
    "inside the widget being streamed and a resize here would rebuild the AR "
    "panel it is reporting on.");
  {
    // setFixedHeight pins minimum AND maximum, so the vertical size hint is
    // fixed no matter how many lines the text grows to. Ten lines of the
    // widget's actual font: enough for the manager's input line plus several
    // wrapped per-panel lines without scrolling at all in the common case.
    const int line_height = ar_panel_status_scroll_->fontMetrics().lineSpacing();
    ar_panel_status_scroll_->setFixedHeight(line_height * 10 + 8);
  }
  ar_panel_status_scroll_->setMinimumWidth(0);
  makeWidthNeutral(ar_panel_status_scroll_);

  // --- Exposed AR tools ---------------------------------------------------
  // The management list: what is exposed right now, and the three things an
  // operator needs to do with a row.
  exposure_list_ = new QListWidget(ar_group);
  exposure_list_->setToolTip(
    "Topics, services and actions exposed as QAR AR tools. Each row is a "
    "generated panel served over HTTP and shown in the headset's browser.");
  auto * exposure_buttons = new QHBoxLayout();
  remove_exposure_button_ = new QPushButton("Remove tool", ar_group);
  remove_exposure_button_->setToolTip("Close the AR panel and stop serving its page.");
  reopen_exposure_button_ = new QPushButton("Re-open in AR", ar_group);
  reopen_exposure_button_->setToolTip(
    "Create the QarOS panel again — after a session restart, or if it was closed "
    "in the headset. The page and its token are unchanged.");
  copy_url_button_ = new QPushButton("Copy URL", ar_group);
  copy_url_button_->setToolTip(
    "Copy the panel's URL, to open it in a desktop browser while debugging.");
  // Live size trim. The panel's physical size is DERIVED from the generated
  // page's own measurement (qar_web_panels/url_panel_sizing.hpp), which is the
  // right default; this is the operator's "a little bigger, please" knob, and
  // it takes effect on the next "Re-open in AR" — no rebuild, no restart. Its
  // starting value comes from QAR_PANEL_WEB_SCALE.
  ar_panel_scale_spin_ = new QDoubleSpinBox(ar_group);
  ar_panel_scale_spin_->setRange(0.25, 4.0);
  ar_panel_scale_spin_->setSingleStep(0.1);
  ar_panel_scale_spin_->setDecimals(2);
  ar_panel_scale_spin_->setPrefix("size x");
  ar_panel_scale_spin_->setValue(urlPanelSizing().scale);
  ar_panel_scale_spin_->setToolTip(
    "Multiplier on the AR panel size derived from the page's own content size. "
    "Press \"Re-open in AR\" to apply it to the selected tool.");
  exposure_buttons->addWidget(remove_exposure_button_);
  exposure_buttons->addWidget(reopen_exposure_button_);
  exposure_buttons->addWidget(copy_url_button_);
  exposure_buttons->addWidget(ar_panel_scale_spin_);
  exposure_status_label_ = new QLabel("AR tools: none", ar_group);
  exposure_status_label_->setWordWrap(true);
  exposure_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // Same feedback loop as the AR panel status below: this label is inside the
  // streamed widget and its text changes whenever a tool is added or removed.
  makeWidthNeutral(exposure_status_label_);

  ar_layout->addLayout(topic_buttons);
  ar_layout->addWidget(topic_watch_list_);
  ar_layout->addWidget(exposure_list_);
  ar_layout->addLayout(exposure_buttons);
  ar_layout->addWidget(exposure_status_label_);
  ar_layout->addLayout(panel_pick_row);
  ar_layout->addLayout(panel_buttons);
  ar_layout->addWidget(ar_panel_status_scroll_);
  root->addWidget(ar_group);

  // --- Spatial AR interaction ----------------------------------------------
  // Arming is explicit and starts at "Off": a hand gesture in the headset must
  // never move a robot because a mode was left on. What each control means is
  // decided by SpatialInteractionController (spatial_interaction.hpp), which is
  // where the rules are unit-tested; this group only sets it and shows it.
  auto * spatial_group = new QGroupBox("Spatial AR interaction", this);
  auto * spatial_layout = new QVBoxLayout(spatial_group);
  auto * spatial_form = new QFormLayout();
  spatial_mode_combo_ = new QComboBox(spatial_group);
  spatial_mode_combo_->addItem("Off", static_cast<int>(SpatialMode::Off));
  spatial_mode_combo_->addItem("Pick a point -> /clicked_point",
    static_cast<int>(SpatialMode::Point));
  spatial_mode_combo_->addItem("Set a navigation goal -> /goal_pose",
    static_cast<int>(SpatialMode::NavGoal));
  spatial_mode_combo_->addItem("Set the initial pose -> /initialpose",
    static_cast<int>(SpatialMode::InitialPose));
  // Area comes in three shapes. They are encoded into the same item data as
  // shape * 100 + mode, so one combo arms the mode AND picks the shape without
  // a second control the operator has to notice.
  spatial_mode_combo_->addItem("Select a rectangle -> polygon",
    static_cast<int>(SpatialMode::Area));
  spatial_mode_combo_->addItem("Select a circle -> polygon",
    100 + static_cast<int>(SpatialMode::Area));
  spatial_mode_combo_->addItem("Select a polygon (tap vertices)",
    200 + static_cast<int>(SpatialMode::Area));
  spatial_mode_combo_->setToolTip(
    "What an AR pointer means in the 3D scene. Off ignores spatial input.");
  spatial_target_combo_ = new QComboBox(spatial_group);
  // Built from spatial_target_options() rather than typed out here: the list,
  // its order, its default and its help text are one unit-tested table, so the
  // dropdown cannot drift from what the controller actually arms. The default
  // is "Action Point (QarOS scene intersect)": the drop after the minimum drop fixes
  // the near-field action point of the minimum drop, which makes the runtime's own
  // intersection the right default. The default needs that drop or a newer one; see
  // spatial_interaction.hpp. Every other entry is offered too.
  {
    QString target_help = "What the pointer is resolved against.\n\n";
    for (const SpatialTargetOption & option : spatial_target_options()) {
      spatial_target_combo_->addItem(QString::fromUtf8(option.label), option.id);
      target_help += QString("%1\n    %2\n")
        .arg(QString::fromUtf8(option.label), QString::fromUtf8(option.help));
    }
    spatial_target_combo_->setToolTip(target_help.trimmed());
    // Selected BY ID, not by index 0. The table happens to put the default
    // first and a test pins that, but the combo must show what the controller
    // is about to be armed with (below) even if the two ever disagree.
    const int default_index = spatial_target_combo_->findData(default_spatial_target_id());
    if (default_index >= 0) {
      spatial_target_combo_->setCurrentIndex(default_index);
    }
  }
  spatial_frame_edit_ = new QLineEdit("map", spatial_group);
  spatial_frame_edit_->setToolTip("The frame every published pick is stamped with.");
  spatial_auto_confirm_ = new QCheckBox("Publish without confirming", spatial_group);
  spatial_auto_confirm_->setToolTip(
    "Off by default: a pick waits for Confirm so a stray gesture cannot drive "
    "the robot.");
  spatial_form->addRow("Mode", spatial_mode_combo_);
  spatial_form->addRow("Target", spatial_target_combo_);
  spatial_form->addRow("Frame", spatial_frame_edit_);
  auto * spatial_buttons = new QHBoxLayout();
  spatial_confirm_button_ = new QPushButton("Confirm pick", spatial_group);
  spatial_cancel_button_ = new QPushButton("Cancel", spatial_group);
  spatial_close_polygon_button_ = new QPushButton("Close polygon", spatial_group);
  spatial_close_polygon_button_->setToolTip(
    "Area mode with the polygon shape: finish the region the taps collected.");
  spatial_buttons->addWidget(spatial_confirm_button_);
  spatial_buttons->addWidget(spatial_cancel_button_);
  spatial_buttons->addWidget(spatial_close_polygon_button_);
  spatial_status_label_ = new QLabel("Spatial: off", spatial_group);
  spatial_status_label_->setWordWrap(true);
  spatial_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  makeWidthNeutral(spatial_status_label_);
  spatial_layout->addLayout(spatial_form);
  spatial_layout->addWidget(spatial_auto_confirm_);
  spatial_layout->addLayout(spatial_buttons);
  spatial_layout->addWidget(spatial_status_label_);
  root->addWidget(spatial_group);

  // --- Diagnostics ---------------------------------------------------------
  auto * diag_group = new QGroupBox("Diagnostics", this);
  auto * diag_layout = new QVBoxLayout(diag_group);
  context_status_label_ = new QLabel("DisplayContext: (not initialized)", diag_group);
  context_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  makeWidthNeutral(context_status_label_);
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
  // An error message is arbitrary-length text arriving at an arbitrary moment;
  // it must not be able to widen the dock either.
  makeWidthNeutral(last_error_label_);
  diag_layout->addWidget(context_status_label_);
  diag_layout->addLayout(diag_buttons);
  diag_layout->addWidget(diagnostics_view_);
  diag_layout->addWidget(last_error_label_);
  root->addWidget(diag_group);

  root->addStretch(1);

  connect(
    spatial_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    &QarStreamingPanel::onSpatialModeChanged);
  connect(
    spatial_target_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    &QarStreamingPanel::onSpatialTargetChanged);
  // The combo already displays index 0; adding the items fired no signal (the
  // connect above did not exist yet), so the controller is told explicitly.
  // Without this the panel would SHOW the Action Point while the controller was
  // still on its own library default, which is the quietest kind of wrong.
  spatial_target_combo_->setCurrentIndex(0);
  spatial_.set_target(spatial_target_from_id(default_spatial_target_id()));
  connect(
    spatial_confirm_button_, &QPushButton::clicked, this, &QarStreamingPanel::onSpatialConfirm);
  connect(
    spatial_cancel_button_, &QPushButton::clicked, this, &QarStreamingPanel::onSpatialCancel);
  connect(
    spatial_close_polygon_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onSpatialClosePolygon);
  connect(
    spatial_frame_edit_, &QLineEdit::editingFinished, this,
    [this]() { spatial_.set_frame_id(spatial_frame_edit_->text().trimmed().toStdString()); });
  connect(
    spatial_auto_confirm_, &QCheckBox::toggled, this,
    [this](bool on) {
      spatial_.set_auto_confirm(on);
      refreshSpatialUi();
    });

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
  connect(add_topic_button_, &QPushButton::clicked, this, &QarStreamingPanel::onAddTopic);
  connect(
    remove_topic_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onRemoveWatchedTopic);
  connect(
    stream_panel_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onStreamSelectedPanel);
  connect(
    stop_panel_button_, &QPushButton::clicked, this, &QarStreamingPanel::onStopSelectedPanel);
  connect(
    refresh_panels_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onRefreshStreamablePanels);
  connect(
    remove_exposure_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onRemoveExposure);
  connect(
    reopen_exposure_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onReopenExposure);
  connect(copy_url_button_, &QPushButton::clicked, this, &QarStreamingPanel::onCopyExposureUrl);
  connect(
    tick_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    &QarStreamingPanel::onTickComboChanged);
  connect(
    prime_checkbox_, &QCheckBox::toggled, this, &QarStreamingPanel::onPrimeCheckboxToggled);

  refreshStreamablePanels();
}

// ---------------------------------------------------------------------------
// RViz lifecycle
// ---------------------------------------------------------------------------
void QarStreamingPanel::onInitialize()
{
  appendLog("QarStreamingPanel::onInitialize() — obtaining DisplayContext.");

  // Before anything timer-driven starts: a full-screen window covering RViz (the
  // QarOS Visualizer) makes Windows stop honouring RViz's timer-resolution
  // request and may power-throttle it, which snaps every timer cadence — the AR
  // video panels' included — back to the 15.6 ms scheduler tick. Opt out, once.
  appendLog(QString::fromStdString(scheduling_guard_.apply().summary()));

  // buildUi()'s refreshStreamablePanels() runs while pluginlib still owns an
  // unparented panel, so window() is `this` and finds no other docks — "This QAR
  // panel" only, until a manual Refresh. VisualizationFrame::addPane() has docked
  // us by now (it always runs before Panel::initialize()/onInitialize, for both
  // config-driven load and the runtime "Add New Panel" menu), so window() is the
  // real top level and any dock listed earlier in the config (Displays, Views, ...
  // precede us in every .rviz this repo ships) is found right away.
  refreshStreamablePanels();
  // Safety net for a dock listed AFTER us in Panels: (none of this repo's
  // .rviz files do that, but it is not guaranteed) — one more refresh
  // deferred to the next event-loop turn, by which point
  // VisualizationFrame::load() has finished the whole Panels: list plus the
  // window-geometry restore that follows it.
  QTimer::singleShot(0, this, &QarStreamingPanel::refreshStreamablePanels);

  adapter_ = std::make_unique<RVizDisplayContextAdapter>(getDisplayContext());
  executor_ = std::make_shared<RVizRenderThreadExecutor>();
  render_lock_ = std::make_shared<RVizRenderLock>(adapter_.get());
  frame_source_ = std::make_shared<qar_ogre_streaming::MockQarFrameInfoSource>();
  mock_factory_ = std::make_shared<AnimatedMockSenderFactory>();
  sender_factory_ = mock_factory_;
  rebuildFrameSource();  // pushes the base config into mock_factory_

  // Live streaming: when QAR_PANEL_LIVE_STREAM is set, the real-QAR-C-API sender
  // factory replaces the simulated one at connect. The peers, and their head
  // poses, then come FROM the QarOS runtime; the panel renders the live RViz
  // scene per peer and submits through the QAR C API. The offline preview path
  // is untouched.
  live_stream_mode_ = !qEnvironmentVariable("QAR_PANEL_LIVE_STREAM").isEmpty();
  if (live_stream_mode_) {
    // This makes the panel LIVE-CAPABLE but does NOT connect. Capture the QAR
    // session config now; the real connection (library_init -> runtime_create ->
    // create_session -> join -> subscribe -> invite_peer) is built lazily on the
    // "Connect to QarOS" button (onStartQar). Connecting during onInitialize would
    // freeze RViz — it would run the whole zenoh/peer handshake on the GUI thread
    // before the Qt event loop is live, so the runtime's async peer coordination
    // could never progress. Deferring to an operator click (event loop running)
    // lets that coordination complete. Until connect, the manager uses the
    // simulated-peer factory and driveLiveStreaming() is a no-op (qar_factory_ == null).
    // Tell the QAR runtime which folder to spawn app binaries (the Visualizer)
    // from. Honour QAR_RUNTIME_BINARIES_DIR if set, else leave empty (the runtime
    // falls back to cwd with a warning). It should point at the SDK's bin folder
    // (run_live_rviz_stream_real.ps1 points it at <SdkRoot>/bin).
    const QString rt_bin = qEnvironmentVariable("QAR_RUNTIME_BINARIES_DIR");
    qar_runtime_binaries_dir_ = rt_bin.isEmpty() ? std::string() : rt_bin.toStdString();
    appendLog(
      "LIVE mode: panel is live-capable. Click 'Connect to QarOS' to "
      "open the real session — deferred, NOT connected at load.");
    // Relabel the runtime controls so the live connection is discoverable (buildUi
    // runs before onInitialize, so live_stream_mode_ isn't known there).
    if (start_qar_button_ != nullptr) { start_qar_button_->setText("Connect to QarOS"); }
    if (stop_qar_button_ != nullptr) { stop_qar_button_->setText("Disconnect (pause)"); }
    // Live mode has no simulated peers: "add a peer" means "add a physical client
    // head", i.e. invite a real device into the running session. Reveal the device
    // selector.
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
      // RViz's only light is a DIRECTIONAL "MainDirectional" that
      // VisualizationManager re-aims at the LOCAL view camera every frame — a
      // headlight. Shared with remote peers that is wrong: their shading pivots
      // around whenever the RViz operator orbits. Add a world-fixed omni light (and
      // optionally one per peer head) while streaming. Applied on set_streaming_active
      // and fully restored on stop; OFF by default outside live mode so the offline
      // preview and the scripted captures keep RViz's own lighting.
      const qar_ogre_streaming::SceneLightingConfig light_cfg = buildSceneLightingConfig();
      scene_lighting_ = std::make_shared<qar_ogre_streaming::QarSceneLighting>(
        adapter_->getSceneManager(), light_cfg);
      cfg.scene_lighting = scene_lighting_;
      appendLog(
        QString("Scene lighting: mode=%1, RViz headlight=%2")
        .arg(qar_ogre_streaming::to_string(light_cfg.mode))
        .arg(light_cfg.suppress_light_name.empty() ? "kept" : "suppressed while streaming"));
      // Render budget: target 60 FPS/peer, hard floor 30, ~16 ms/tick batch time
      // budget, designed for up to 8 peers. A single batch render lock (via
      // render_lock_) wraps all peers so they share one coherent scene snapshot.
      cfg.render_lock = render_lock_;
      cfg.render_budget.max_batch_milliseconds = 16.0;
      cfg.render_budget.floor_fps = 30;
      // Install the real GL fence seam. After each stream's targets render (in
      // the batch, inside lockRender), a glFenceSync is inserted and carried on the
      // submitted QarVideoFrameOpenGl so the Qar consumer can wait before sampling.
      cfg.fence_hook = std::make_shared<qar_ogre_streaming::GlFenceHook>();
      manager_ = std::make_unique<qar_ogre_streaming::OgreQarStreamingManager>(cfg);
      appendLog("Reusable qar_ogre_streaming manager constructed and wired to the RViz scene.");
      // Seed the prime switch from the current checkbox (itself seeded from
      // QAR_ADAPTER_PRIME in buildUi()) so a stream created before it is ever
      // touched still gets the requested value. The tick period is seeded the same
      // way below, via applyTickPeriodMs(), once render_timer_ exists.
      manager_->set_prime_enabled(prime_checkbox_->isChecked());
      clearLastError();
      // Do NOT auto-start streaming in live mode. Streaming begins when the
      // operator connects (onStartQar builds qar_factory_ and calls set_streaming_active).
    } catch (const std::exception & e) {
      setLastError(QString("Library manager construction failed: %1").arg(e.what()));
    }
  } else {
    setLastError("DisplayContext or Ogre::SceneManager unavailable at onInitialize().");
  }

  // Floor-plane feature: an opaque companion box under every
  // rviz_default_plugins Grid display, so the AR depth map sees a solid floor
  // instead of the gaps between grid lines. On by default; read the same way
  // the panel reads its other QAR_PANEL_* env vars (see geo_enabled_ below).
  // Independent of whether the qar_ogre_streaming manager above constructed
  // successfully — this only needs the scene, not a working streaming
  // manager — but still gated on adapter_->isValid() for the same reason.
  const bool floor_plane_enabled =
    qEnvironmentVariable("QAR_PANEL_FLOOR_PLANE").trimmed().toLower() != "off";
  if (!floor_plane_enabled) {
    appendLog("Floor plane: disabled via QAR_PANEL_FLOOR_PLANE=off.");
  } else if (adapter_->isValid()) {
    floor_plane_ = std::make_unique<GridFloorPlaneManager>(
      adapter_->getSceneManager(), getDisplayContext(),
      [this](const std::string & line) {appendLog(QString::fromStdString(line));});
  }

  refreshContextStatus();
  updateRuntimeStateLabel();

  // Periodic render tick on the Qt/render thread. It renders active streaming
  // streams each tick and is a no-op when nothing is streaming, so it does not
  // interfere with the auto-dump / auto-capture flows.
  // NOT a fixed FPS target: the scheduler paces per-peer via the render budget
  // regardless of how often this timer fires. Qt picks CoarseTimer by default, which
  // may drift 5% and — on Windows — is served off the ~15.6 ms system tick, so even
  // the slowest stock period (16 ms) would alias to ~31ms/~32 FPS without PreciseTimer
  // (the multimedia timer); applyTickPeriodMs() always sets PreciseTimer.
  render_timer_ = new QTimer(this);
  connect(render_timer_, &QTimer::timeout, this, &QarStreamingPanel::onRenderTick);
  // Initial period from the tick combo, itself seeded from QAR_PANEL_TICK_MS
  // (default 0.1 ms) in buildUi().
  applyTickPeriodMs(tick_combo_->currentData().toDouble());

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

  // Non-interactive verification hook: dump + quit.
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

  // Non-interactive offscreen-capture hook: render the live scene
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

  // Non-interactive single-peer stereo capture hook: create a simulated peer +
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

  // Non-interactive MULTI-PEER scripted proof: add K simulated peers with
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
// PanelDiagnosticsSink
// ---------------------------------------------------------------------------
void QarStreamingPanel::on_message(
  qar_ar_core::DiagnosticSeverity severity,
  const std::string & category,
  const std::string & message,
  const std::string & panel_key)
{
  QString line = QString("[qt_lib:%1:%2] %3")
    .arg(QString::fromLatin1(qar_ar_core::to_string(severity)))
    .arg(QString::fromStdString(category))
    .arg(QString::fromStdString(message));
  if (!panel_key.empty()) {
    line += QString(" (panel '%1')").arg(QString::fromStdString(panel_key));
  }

  // The manager only ever calls this on the Qt GUI thread, but the sink contract
  // makes no promise a host may lean on, so marshal like the other two sinks.
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
  // LIVE: "Connect to QarOS" connects to the real QarOS runtime and begins streaming.
  // The session is built HERE (on an operator click, event loop running), never at load.
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
    // The panel must NOT start qar-runtime-launcher.exe itself. The QAR library
    // spawns and configures the launcher from `QarSessionCreateInit.launcher_config_json`
    // + the runtime binaries folder. An externally started launcher competes with it
    // (peerSpec/consumer-id chaos).
    if (pending_connect_) {
      appendLog("QarOS: connect already in progress — RViz stays live; please wait.");
      return;
    }
    if (!qar_factory_) {
      appendLog("Connecting to QarOS runtime (building session; RViz stays responsive)...");
      qar_qar_adapter::QarCApiSenderFactory::Config cfg;
      // near/far are BOTH the clip planes we render with and the range we advertise to
      // QAR every show_frame (QarRenderFrameShow.rendered_near_far, which cannot
      // compensate for a wrong depth encoding). The far plane matters: the Visualizer
      // expects the window-space z-buffer value (QAR_DEPTH_ENCODING=ndc), which is
      // reciprocal in distance, so an over-wide far plane squashes a room-sized scene
      // into the first per-cent of the range and every object comes out the same
      // colour (content spanning 0.14-4.99 m under a 0.1-500 m range; see
      // qar_ogre_adapter docs/ARCHITECTURE.md, "Depth encoding"). Tunable per run for
      // exactly that. 0.01 m near keeps close-up content out of the near plane. The
      // far default is 150 m: a real LiDAR bag reaches much further (an Ouster OS1
      // sees ~120 m), and everything past the far plane is simply clipped away — a
      // 50 m far plane removes most of a 32768-point frame before it ever reaches the
      // Visualizer. Still far short of 500 m, which costs depth precision under the
      // reciprocal NDC encoding.
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
      // depth_scale converts our metric metres into whatever a consumer that honours
      // it expects ("norm" is shorthand for 1/far, i.e. metres -> [0,1] across the
      // frustum). It cannot make metric depth right for the Visualizer, which needs
      // QAR_DEPTH_ENCODING=ndc (qar_ogre_adapter docs/ARCHITECTURE.md, "Depth
      // encoding").
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
      // The session GUID is always unique and non-zero (handled inside the factory).
      // The launcher config defaults to {"enableMixer": true};
      // QAR_PANEL_LAUNCHER_CONFIG overrides it (e.g. '{"enableMixer": false}' for the
      // direct RViz -> Visualizer path used when debugging).
      const QString lcfg = qEnvironmentVariable("QAR_PANEL_LAUNCHER_CONFIG");
      if (!lcfg.isEmpty()) { cfg.launcher_config_json = lcfg.toStdString(); }
      // QAR library log folder (TRACE severity is the factory default) — full library
      // logs should accompany bug reports to the QarOS vendor. Empty => %TEMP%/quaternar.
      const QString qlog = qEnvironmentVariable("QAR_PANEL_LOG_DIR");
      if (!qlog.isEmpty()) { cfg.log_folder_path = qlog.toStdString(); }
      // Library log severity and console duplication. TRACE (+ console) is the default
      // so that full logs accompany bug reports to the QarOS vendor —
      // but TRACE costs real I/O (tens of MB per minute once frames flow: 480k lines /
      // 81 MB in six minutes), so QAR_PANEL_LOG_SEVERITY / _CONSOLE lower it
      // when the question is throughput or stability rather than protocol.
      //
      // Resolved by a pure function because the vendor library gives the QAR_LOG_SEVERITY
      // ENVIRONMENT VARIABLE precedence over QarLibraryInit.log_severity: a run passing
      // severity=warn still logs at TRACE when a stale QAR_LOG_SEVERITY=trace sits in
      // the shell. The adapter exports the decision to the environment as well
      // (QarCApiSenderFactory ctor); see qar_qar_adapter::resolve_library_logging for the
      // precedence and qar_ogre_adapter docs/QAROS_INTEGRATION_NOTES.md, "Logging
      // control", for the measurements.
      {
        const QByteArray panel_severity = qgetenv("QAR_PANEL_LOG_SEVERITY");
        const QByteArray env_severity = qgetenv("QAR_LOG_SEVERITY");
        const QByteArray panel_console = qgetenv("QAR_PANEL_LOG_CONSOLE");
        const qar_qar_adapter::LibraryLogging logging = qar_qar_adapter::resolve_library_logging(
          panel_severity.isEmpty() ? nullptr : panel_severity.constData(),
          env_severity.isEmpty() ? nullptr : env_severity.constData(),
          panel_console.isEmpty() ? nullptr : panel_console.constData(),
          qar_qar_adapter::LogSeverity::kTrace);
        cfg.log_severity = static_cast<QarLogSeverity>(static_cast<int>(logging.severity));
        cfg.enable_console_logging = logging.console;
        appendLog(
          QString("LIVE: QarOS library logging: severity=%1 (from %2), console duplicate to "
            "stdout=%3%4")
          .arg(QString::fromLatin1(qar_qar_adapter::log_severity_name(logging.severity)))
          .arg(QString::fromLatin1(logging.severity_source))
          .arg(QString::fromLatin1(logging.console ? "on" : "off"))
          .arg(logging.environment_overridden ?
            QString(" - QAR_LOG_SEVERITY in the environment disagrees and is overwritten "
              "(the vendor library would otherwise obey it over the init struct)") :
            QString()));
        if (!logging.warning.empty()) {
          appendLog(QString("LIVE: WARNING: %1").arg(QString::fromStdString(logging.warning)));
        }
      }
      // Distinct payload for the Visualizer connection string. Defaults to "auto"
      // (fresh UUID per invite) because that is what allows several Visualizers to
      // be invited and streamed to at once. Set QAR_PANEL_VISUALIZER_GUID=off to
      // fall back to the stock string, where every invite collapses onto one peer;
      // set a literal GUID to address one specific instance.
      const QString qvguid = qEnvironmentVariable("QAR_PANEL_VISUALIZER_GUID");
      if (!qvguid.isEmpty()) {
        cfg.visualizer_conn_suffix =
          (qvguid.toLower() == "off") ? std::string() : qvguid.toStdString();
      }
      // qar_render_sender_frame_opengl takes an OUT frame, so QAR owns the textures and
      // we must copy our Ogre color/depth INTO them. The hook does that with an FBO blit
      // (glBlitFramebuffer) on the render thread. QAR_PANEL_GL_SUBMIT=supply-own still
      // allows a supply-own run for comparison, but it renders black against the real
      // SDK (QAR overwrites the ids we pass). See qar_ogre_adapter
      // docs/QAROS_INTEGRATION_NOTES.md, "Render-into and the GL context".
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
      // QAR_PANEL_VISUALIZERS=N to auto-invite N of them at connect.
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
      // scene is: nothing clips the content to the box, but it states where the
      // app is meant to be, so a 20 m grid needs a big box or a small app_scale.
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
      // runtime's internal app peers too (5 ids for 1 Visualizer), so
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
        appendLog("LIVE: peer admission = NAMED — note that the SDK's display names "
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
      // that on the Qt thread would freeze RViz for the whole of it.
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

  // Offline preview path.
  qar_running_ = true;
  if (manager_) {
    manager_->set_streaming_active(true);
  }
  updateRuntimeStateLabel();
  appendLog("Qar runtime STARTED (offline preview, simulated peers).");
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
  // silently receive nothing from such a publisher — the same QoS mismatch that
  // makes rosbag point clouds invisible. SensorDataQoS is the matching profile.
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
  startSpatialGestureSource();
  live_connected_ = true;
  qar_running_ = true;
  updateRuntimeStateLabel();
  // Connect invites nothing by default, so say what actually happened rather than
  // claiming to wait for a peer that was never asked for.
  if (qar_factory_->invited_peers().empty()) {
    appendLog("Connected to QarOS: session open, streaming active. No peer invited — "
      "pick a device kind and click 'Invite peer' to open a Visualizer.");
  } else {
    appendLog("Connected to QarOS: streaming active; waiting for the invited peer(s).");
  }

  // The hidden RViz Displays panel goes up with the session, by default: a second
  // instance of RViz's own Displays panel, never shown on the desktop, streamed into AR
  // so the headset can toggle, edit, add and remove the real displays. Last, so a
  // failure here (it logs its own and carries on) can never leave the connect half
  // done. Connect can run more than once ("Disconnect (pause)" pauses, "Connect to QarOS"
  // resumes); the start is idempotent.
  startHiddenDisplaysStreamOnConnect();
}

void QarStreamingPanel::onStopQar()
{
  // LIVE: "Disconnect (pause)" pauses streaming. The QAR session/handles are kept alive
  // (torn down in the panel dtor, in the correct order: manager_/streams before
  // qar_factory_/session) so we never leave the render tick pointing at a destroyed
  // session. Clicking "Connect to QarOS" again resumes.
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
  appendLog("Qar runtime STOPPED.");
}

void QarStreamingPanel::onAddPeer()
{
  // With a connected QarOS session "add a peer" means "add a physical client head",
  // i.e. invite a real device into the running session. Simulated peers make no
  // sense there.
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
      "NOTE: each Visualizer invite carries a fresh GUID by default "
      "(QAR_PANEL_VISUALIZER_GUID=auto), so every invite opens its own Visualizer. "
      "With QAR_PANEL_VISUALIZER_GUID=off or a fixed GUID, repeated invites address "
      "the same Visualizer and return the peer that already exists.");
  }
}

qar_ogre_streaming::SceneLightingConfig QarStreamingPanel::buildSceneLightingConfig() const
{
  using qar_ogre_streaming::SceneLightingMode;
  qar_ogre_streaming::SceneLightingConfig cfg;

  // Default OFF outside live mode: the offline preview and the scripted captures keep
  // RViz's own lighting. LIVE mode defaults to BOTH: the omnipresent directional rig
  // leaves no surface unlit, and a modest light at each peer's own head keeps what
  // that peer looks at well lit (a single static point light leaves far sides and
  // undersides dark).
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
  updateArPanelStatus();
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
// Offscreen capture
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
    // All Ogre/GL work must run on the render thread. Our call sites (button
    // click / QTimer) are already on it; refuse otherwise.
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
        std::make_unique<qar_ogre_streaming::OgreStereoRenderTarget>(sm, "qar_offscreen");
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
  ts << "# Qar RViz2 offscreen capture\n";
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
// Single-peer stereo streaming
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
  // from it (each peer offset in orbit phase for an independent view).
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
  //    (QarStreamParamsOpenGlWgl) when a sender is created, but this tick is a QTimer
  //    slot that can land BETWEEN Ogre frames — so we capture opportunistically
  //    every tick and create_sender() uses the latched handles.
  const bool gl_ready = qar_factory_->capture_gl_context();

  // 1) Open a stereo stream for every peer that requested one (the real QAR
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
      // the NORMAL case for the first few ticks: sender creation is asynchronous
      // (qar_render_sender_create_async), so it reports "not yet" until the receiver
      // has finished negotiating. The other benign case is the startup race where
      // RViz has not drawn a frame yet, so no GL context could be chained.
      //
      // Deliberately not an error: the tick keeps running and the GUI thread is
      // never blocked waiting for the sender.
      qar_factory_->on_render_request(id);
      if (gl_ready && !live_sender_pending_logged_) {
        live_sender_pending_logged_ = true;
        appendLog("LIVE: sender creation in flight (async) — RViz stays responsive; "
          "the stream opens as soon as the peer completes negotiation.");
      }
    }
  }

  // 2) Render + submit every active stream. The scheduler renders the LIVE RViz
  //    scene from each peer's head pose (begin_frame) and submits via the QAR C API.
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

  // 3) Close streams the runtime closed (begin_frame reported STREAM_CLOSED).
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
  // Zero-interval throttle (0.1 ms tick setting): a
  // zero-interval QTimer fires as fast as the event loop is otherwise idle, which on
  // a quiet system is far more often than any real begin-frame wait could resolve.
  // Cap the actual tick body to at most once per tick_throttle_interval_us_ (100us at
  // 0.1 ms) so heartbeat cadence, budget pacing, etc. all keep their designed
  // effective rate regardless of how often Qt calls in — and so a throttled call is
  // the cheapest possible no-op: one steady_clock read, one comparison, no logging,
  // no allocation. Ticks at 1 ms and above are already paced by Qt::PreciseTimer
  // itself and never hit this branch.
  if (zero_interval_mode_) {
    const auto now = std::chrono::steady_clock::now();
    if (last_tick_body_run_.time_since_epoch().count() != 0 &&
      std::chrono::duration<double, std::micro>(now - last_tick_body_run_).count() <
        tick_throttle_interval_us_)
    {
      return;
    }
    last_tick_body_run_ = now;
  }

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
    if (gap_ms > kGuiGapRecordMs && gui_gap_timing_) {
      // A stall of the Qt thread, with absolute (epoch) bounds so it lines up with the
      // stage CSVs. One queued row; the clock is read only on the rare gap.
      const std::int64_t end_us = qar_ar_core::timing_epoch_us();
      gui_gap_timing_->append({
          end_us - static_cast<std::int64_t>(gap_ms * 1000.0), end_us,
          static_cast<std::int64_t>(gap_ms * 1000.0)});
    }
  }
  tick_last_ = tick_now;
  tick_gap_valid_ = true;
  emitHeartbeat();

  // AR input is pumped from a POSTED event, never from inside this timer
  // handler: a click that opens a modal dialog spins a nested event loop, and
  // Qt will not re-enter a timer whose handler is still on the stack — which is
  // exactly how the whole stream would freeze for as long as a dialog opened by an
  // AR tap on "Add" stays open. See scheduleArPanelInputPump().
  scheduleArPanelInputPump();

  // AR GUI panels run BEFORE the early-outs below: they are independent of the
  // Ogre scene path (a widget capture needs no DisplayContext and no peer), and
  // they are paced by their own per-panel FPS cap, so an idle 3D stream must not
  // stop them.
  tickArPanels();
  // Same reasoning for the generated AR panels: they need no DisplayContext and
  // no peer, they are paced per panel, and the ROS bridges' timeout tick must
  // keep running whether or not the 3D path has anything to do.
  tickArToolPanels();
  // Same again for spatial picks: they arrive on the QarOS gesture thread, are
  // drained here on the Qt thread, and need neither a DisplayContext nor a peer.
  tickSpatialInteraction();
  // Floor plane: re-sync against the display tree regardless of streaming
  // state, same reasoning as the three ticks above — it tracks the Grid
  // display's own properties, not anything peer/stream related. Cheap (see
  // GridFloorPlaneManager::sync()'s own comment); null when disabled via
  // QAR_PANEL_FLOOR_PLANE=off or construction failed.
  if (floor_plane_) {
    floor_plane_->sync();
  }

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

void QarStreamingPanel::applyTickPeriodMs(double ms)
{
  if (render_timer_ == nullptr) {
    return;
  }
  tick_period_ms_ = ms;
  // QTimer::setInterval() takes an int ms and cannot represent a sub-1ms period: for
  // ms < 1 we run it as a zero-interval timer instead (fires whenever the event loop
  // is otherwise idle — Qt's documented meaning of interval 0) and throttle the
  // actual tick body ourselves in onRenderTick(), rather than letting it run
  // literally as fast as the event loop will dispatch (which would still be
  // silent/allocation-free per declined call, but would needlessly burn CPU on a
  // busy-poll well past anything a real begin-frame wait could resolve at).
  zero_interval_mode_ = (ms < 1.0);
  tick_throttle_interval_us_ = ms * 1000.0;
  // Reset to "run unconditionally next time": a period CHANGE must not inherit a
  // stale last-run timestamp from the previous (possibly much slower) period.
  last_tick_body_run_ = std::chrono::steady_clock::time_point{};

  // Applies immediately: stop, reconfigure, restart — never leave the timer running
  // with a stale interval while it is stopped (a queued timeout from the OLD period
  // could otherwise still land after start()).
  render_timer_->stop();
  render_timer_->setTimerType(Qt::PreciseTimer);
  render_timer_->setInterval(zero_interval_mode_ ? 0 : static_cast<int>(ms + 0.5));
  render_timer_->start();

  if (manager_) {
    manager_->set_tick_ms(ms);  // informational only; see set_tick_ms doc comment
  }
  appendLog(QString("Render tick: %1 ms").arg(ms));
}

void QarStreamingPanel::onTickComboChanged(int index)
{
  if (render_timer_ == nullptr) {
    return;
  }
  bool ok = false;
  const double ms = tick_combo_->itemData(index).toDouble(&ok);
  if (!ok || ms < 0.0) {
    return;
  }
  applyTickPeriodMs(ms);
}

void QarStreamingPanel::onPrimeCheckboxToggled(bool checked)
{
  if (manager_) {
    manager_->set_prime_enabled(checked);
  }
  appendLog(QString("Prime next frame: %1").arg(checked ? "ON" : "OFF"));
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
  ts << "# Qar RViz2 single-peer stereo capture\n";
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
  // Find the first streaming peer (the capture is single-peer). Ensure it has rendered.
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
  ts << "# Qar RViz2 single-peer stereo AUTOSTEREO summary\n";
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
// Multi-peer batched render scheduler proof
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
    peers = 3;  // validate with 3-4 peers
  }
  if (peers > 8) {
    peers = 8;  // designed for up to 8
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
  ts << "# Qar RViz2 multi-peer batched render scheduler proof\n";
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

  // Drive the simulated runtime + reusable library end to end.
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
  ts << "# Qar RViz2 streaming panel - auto diagnostics dump\n";
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

  std::uint64_t trace_rows_total = 0;
  std::uint64_t trace_changed_total = 0;
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
      // WHY the skips happened. A bare climbing `skip=` next to a frozen `sub=`
      // is the shape every stereo stall takes, and on its own it names no cause.
      // The reason, the per-reason totals and the adapter's own detail turn a
      // stall into a single line of log.
      if (sd.frames_skipped > 0) {
        line += QString(" why=%1[nr=%2 err=%3 tgt=%4 bud=%5]")
          .arg(QString::fromLatin1(qar_ogre_streaming::to_string(sd.last_skip_reason)))
          .arg(static_cast<qulonglong>(sd.skips_sender_not_ready))
          .arg(static_cast<qulonglong>(sd.skips_sender_error))
          .arg(static_cast<qulonglong>(sd.skips_target_init))
          .arg(static_cast<qulonglong>(sd.skips_budget));
        if (!sd.last_skip_detail.empty()) {
          line += QString(" (%1)").arg(QString::fromStdString(sd.last_skip_detail));
        }
      }
      trace_rows_total += sd.frame_trace_rows;
      trace_changed_total += sd.frame_trace_changed_rows;
    }
  }
  // tick=, prime= and rr= come after the per-stream fields, and the panels section, the
  // spatial counters and vdisp are appended after them, so existing parsers of the earlier
  // fields are unaffected. rr=<changed>/<frames> only while the trace is on (at least one
  // row written by some stream). tick_period_ms_ (not render_timer_->interval()) is the
  // source of truth: the zero-interval 0.1 ms setting reports a raw QTimer interval of 0.
  line += QString(" tick=%1 prime=%2")
    .arg(tick_period_ms_, 0, 'g', 4)
    .arg((prime_checkbox_ != nullptr && prime_checkbox_->isChecked()) ? 1 : 0);
  if (trace_rows_total > 0) {
    line += QString(" rr=%1/%2")
      .arg(static_cast<qulonglong>(trace_changed_total))
      .arg(static_cast<qulonglong>(trace_rows_total));
  }

  // AR panel streams: without them a session with three live GUI panels would
  // print `streams=1` and say nothing about the other two, and a panel QarOS is
  // asking for and we are not serving would go unnoticed for the whole session.
  //
  // Per panel: frames submitted, then — only when it is not being served — the
  // not-ready streak and how long the sender has gone without landing a frame.
  // A healthy panel is therefore one short field; an unserved one says so on
  // every heartbeat until it is fixed or retired.
  if (panel_manager_) {
    const auto panels = panel_manager_->diagnostics();
    // Shown once any panel has ever been closed too: in the default (destroy) close
    // mode a session whose panels have all been closed has panel_count 0, and a line
    // that vanishes then says nothing about the three it closed.
    if (panels.panel_count > 0 || panels.panels_closed > 0) {
      // live / closed / notice, not just a total: `live` is what is being streamed
      // now, `closed` is how many panels were closed AND destroyed this session (no
      // entry is left to count them by), `notice` is how many show the closed-window
      // notice instead (notice mode only). A total alone cannot tell these apart.
      line += QString(" || panels=%1 live=%2 closed=%3 notice=%4")
        .arg(panels.panel_count)
        .arg(panels.live_panels)
        .arg(static_cast<qulonglong>(panels.panels_closed))
        .arg(panels.tombstoned_panels);
      if (panels.ar_closed_panels > 0) {
        line += QString(" arclosed=%1").arg(panels.ar_closed_panels);
      }
      if (panels.close_panel_failures > 0) {
        line += QString(" CLOSEFAIL=%1")
          .arg(static_cast<qulonglong>(panels.close_panel_failures));
      }
      if (panels.tombstone_cap_refusals > 0) {
        line += QString(" CAPPED=%1")
          .arg(static_cast<qulonglong>(panels.tombstone_cap_refusals));
      }
      for (const auto & pd : panels.panels) {
        line += QString(" | %1%2 sub=%3")
          .arg(QString::fromStdString(pd.key))
          .arg(pd.tombstoned ? (pd.ar_panel_closed ? "[NOTICE,ARCLOSED]" : "[NOTICE]")
                             : (pd.ar_panel_closed ? "[ARCLOSED]" : ""))
          .arg(static_cast<qulonglong>(pd.frames_submitted));
        if (pd.frames_submitted == 0 && pd.active && !pd.ar_panel_closed) {
          line += QString(" UNSERVED %1s nr=%2 stream=%3")
            .arg(pd.unserved_seconds, 0, 'f', 1)
            .arg(static_cast<qulonglong>(pd.not_ready_streak))
            .arg(QString::fromStdString(pd.stream_id));
        } else if (pd.not_ready_streak > 0) {
          line += QString(" nr=%1").arg(static_cast<qulonglong>(pd.not_ready_streak));
        }
      }
    }
  }
  // The spatial pointer path, on EVERY heartbeat and not only when something
  // arrives: "rx=0" repeated once every two seconds is the observation that
  // proves the runtime is silent, and a line that appears only when an event
  // lands could never make it.
  line += QString(" || %1").arg(spatialCountersLine());
  // RViz's own Image/Camera displays, last so every earlier field keeps its place:
  // "No Image" leaves nothing in the log to tell "RViz never saw the publisher"
  // from "saw it, received nothing", so this field carries that evidence.
  const QString video_displays = videoDisplayHealthField();
  if (!video_displays.isEmpty()) {
    line += QString(" || vdisp %1").arg(video_displays);
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
    ? (live_connected_ ? "Qar runtime: RUNNING (live)" : "Qar runtime: RUNNING (offline preview)")
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

// ===========================================================================
//  ROS topic browser + AR GUI panels
// ===========================================================================

void QarStreamingPanel::ensureDisplayCatalog()
{
  if (display_catalog_ready_) {
    return;
  }
  display_catalog_ready_ = true;  // one attempt; a failure is not worth retrying per click

  // rviz_common::DisplayFactory already builds this index, but its header lives in
  // rviz_common's src/ and is NOT installed — getDisplayFactory() hands back an
  // incomplete type. So walk the same manifests ourselves (see
  // display_class_catalog.hpp).
  //
  // Discovery goes through the ament resource index rather than
  // pluginlib::ClassLoader: <pluginlib/class_loader.hpp> includes <tinyxml2.h>,
  // and the ROS 2 Humble Windows distro ships tinyxml2's CMake config but NOT its
  // headers, so that include does not compile here. The index is the same source
  // of truth pluginlib itself reads — `pluginlib_export_plugin_description_file`
  // registers each manifest under the `rviz_common__pluginlib__plugin` resource
  // type as "<package> -> <path relative to its install prefix>".
  try {
    const auto resources =
      ament_index_cpp::get_resources("rviz_common__pluginlib__plugin");
    for (const auto & [package, _] : resources) {
      std::string content;
      std::string prefix;
      if (!ament_index_cpp::get_resource(
          "rviz_common__pluginlib__plugin", package, content, &prefix))
      {
        continue;
      }
      // One package may register several manifests, one per line.
      const QStringList relative_paths =
        QString::fromStdString(content).split('\n', Qt::SkipEmptyParts);
      for (const QString & relative : relative_paths) {
        const QString manifest =
          QString::fromStdString(prefix) + '/' + relative.trimmed();
        QFile file(manifest);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
          continue;  // a broken plugin must not sink the whole catalog
        }
        display_catalog_.add_manifest(QString::fromUtf8(file.readAll()));
      }
    }
  } catch (const std::exception & e) {
    appendLog(QString("TOPICS: could not enumerate RViz displays: %1").arg(e.what()));
    return;
  }

  appendLog(
    QString("TOPICS: indexed %1 message type(s) across the installed RViz displays.")
    .arg(display_catalog_.type_count()));
}

QString QarStreamingPanel::createDisplayForTopic(const AddTopicSelection & selection)
{
  if (selection.display_class.isEmpty()) {
    return "no RViz display handles " + selection.message_type;
  }
  if (getDisplayContext() == nullptr) {
    return "no RViz DisplayContext";
  }
  rviz_common::DisplayGroup * root = getDisplayContext()->getRootDisplayGroup();
  if (root == nullptr) {
    return "RViz has no root display group";
  }

  // These four steps ARE VisualizationManager::createDisplay + addDisplay, which
  // this plugin cannot call directly (see the display_group.hpp include note).
  rviz_common::Display * display = root->createDisplay(selection.display_class);
  if (display == nullptr) {
    return "RViz refused to create a " + selection.display_class + " display";
  }
  root->addDisplay(display);
  display->initialize(getDisplayContext());
  display->setName(selection.name);

  // No success check here on purpose: for an unloadable plugin RViz hands back a
  // FailedDisplay whose getClassId() is the REQUESTED id (it calls
  // setClassId(desired_class_id)), so there is nothing cheap to compare against.
  // That is fine — a FailedDisplay appears in RViz's own display tree with an
  // error icon and message, which is exactly what RViz's native "Add by topic"
  // does too.

  // setTopic is exactly what RViz's own "New display by topic" window calls.
  display->setTopic(selection.name, selection.message_type);
  display->setEnabled(true);
  return QString();
}

void QarStreamingPanel::pinWatchedTopic(const AddTopicSelection & selection)
{
  for (const auto & existing : watched_topics_) {
    if (existing.name == selection.name) {
      return;
    }
  }
  watched_topics_.push_back(selection);

  auto * item = new QListWidgetItem(
    QString("%1  [%2]").arg(selection.name, selection.message_type), topic_watch_list_);
  item->setData(Qt::UserRole, selection.name);
  item->setToolTip(
    selection.display_class.isEmpty()
    ? QString("%1\nNo RViz display for this type.").arg(selection.name)
    : QString("%1\nDisplay: %2").arg(selection.name, selection.display_class));
}

void QarStreamingPanel::onAddTopic()
{
  ensureDisplayCatalog();

  auto entries = queryRosGraph();
  if (entries.empty()) {
    appendLog("GRAPH: no ROS node, or nothing visible on the graph.");
  }

  AddTopicDialog dialog(std::move(entries), display_catalog_, this);
  // The dialog never touches ROS itself; a refresh comes back here for a fresh
  // listing, which keeps every rclcpp call on the GUI thread with the rest.
  connect(
    &dialog, &AddTopicDialog::refreshRequested, this,
    [this, &dialog]() { dialog.setEntries(queryRosGraph()); });

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const AddTopicSelection selection = dialog.selection();
  if (selection.empty()) {
    return;
  }

  // For a TOPIC, "Add" creates the matching RViz display AND pins it to the watch
  // list. The optional third thing is the generated AR panel — and when that's
  // what the operator asked for, an IMAGE topic (sensor_msgs/Image or
  // CompressedImage) gets NO RViz display at all: exposing a topic as an AR web
  // panel must not add a display that was not asked for. (Skipping only
  // Image/Camera displays would let a colour video exposed this way still get
  // whatever else the catalog offers for sensor_msgs/Image, such as a DepthCloud
  // display.) Skipped entirely rather than created-then-hidden. See
  // should_skip_display_for_exposed_topic().
  if (selection.kind == RosEntityKind::Topic) {
    const bool skip_display = should_skip_display_for_exposed_topic(
      selection.expose_as_ar_tool, selection.message_type.toStdString(),
      selection.display_class.toStdString());
    if (skip_display) {
      appendLog(
        QString("TOPICS: no RViz display created for %1 [%2] — it is exposed as a QAR "
          "AR tool, and exposing an image topic must not add a display that was not "
          "asked for (candidate was %3).")
        .arg(
          selection.name, selection.message_type,
          selection.display_class.isEmpty() ? QString("none") : selection.display_class));
    } else {
      const QString error = createDisplayForTopic(selection);
      if (error.isEmpty()) {
        appendLog(
          QString("TOPICS: added display %1 for %2.")
          .arg(selection.display_class, selection.name));
      } else {
        // Still pin it: a topic with no display is legitimately watchable.
        appendLog(QString("TOPICS: %1 (%2) — added to the watch list only.")
          .arg(selection.name, error));
      }
    }
    pinWatchedTopic(selection);
  }

  if (!selection.expose_as_ar_tool) {
    return;
  }
  const QString expose_error = exposeEntity(selection);
  if (!expose_error.isEmpty()) {
    setLastError("AR tool: " + expose_error);
    appendLog(QString("AR TOOL: could not expose %1 — %2")
      .arg(selection.name, expose_error));
  }
}

void QarStreamingPanel::onRemoveWatchedTopic()
{
  auto * item = topic_watch_list_->currentItem();
  if (item == nullptr) {
    return;
  }
  const QString topic = item->data(Qt::UserRole).toString();
  watched_topics_.erase(
    std::remove_if(
      watched_topics_.begin(), watched_topics_.end(),
      [&topic](const AddTopicSelection & s) { return s.name == topic; }),
    watched_topics_.end());
  delete topic_watch_list_->takeItem(topic_watch_list_->row(item));
  // The RViz display is deliberately left alone: it is now an ordinary display
  // the operator owns, and silently deleting it from here would be surprising.
  appendLog(QString("TOPICS: stopped watching %1 (its display, if any, stays).").arg(topic));
}

QString QarStreamingPanel::ensurePanelManager()
{
  if (panel_manager_) {
    return QString();
  }
  if (!qar_factory_ || !qar_factory_->ready()) {
    return "QarOS session is not running — connect to QarOS first (live mode)";
  }

  // The input source first, so the factory can hand it to every panel it mints.
  // This is the whole click path: a streamed QWidget reacts to being poked in AR
  // through a per-panel contact binding (qar_ogre_adapter
  // docs/QAROS_INTEGRATION_NOTES.md, "Panel pixel coordinates and
  // content_scale"). Since the minimum drop it is
  // qar_gui_panels_subscribe_all_gesture_events, one reader carrying hover / tap /
  // long-press / DRAG, so the same source also makes a QSlider handle draggable
  // in AR.
  panel_input_source_ =
    std::make_shared<qar_qar_adapter::QarCApiPanelInputSource>(qar_factory_->session());

  panel_sender_factory_ = std::make_shared<qar_qar_adapter::QarCApiPanelSenderFactory>(
    qar_factory_.get(), panel_input_source_);

  qar_qt_streaming::QtPanelStreamingConfig config;
  config.panel_sender_factory = panel_sender_factory_;
  // Drained on the Qt GUI thread. The QarOS callback thread only ever reaches
  // the queue inside the source.
  config.input_source = panel_input_source_;
  // ...but NOT from inside tick(), because tick() runs in a QTimer handler and
  // a delivered click can enter QDialog::exec(). scheduleArPanelInputPump()
  // owns the drain instead, from a posted event.
  config.pump_input_in_tick = false;
  // The manager's diagnostics: without a sink `emit_diagnostic` has nowhere to go,
  // and every line it produces — panel added, dialog promoted, panel closed, the
  // "never served a frame" warning — is lost from the .err.log. The panel IS the
  // sink, and it forwards to appendLog.
  config.diagnostics = this;
  // What a closing Qt window does to its AR panel: destroy it (the default) or keep
  // it as a closed-window notice. QAR_PANEL_CLOSE_MODE=destroy|notice; anything
  // else warns once and runs the default.
  const QByteArray close_mode_text =
    qEnvironmentVariable(qar_qt_streaming::kPanelCloseModeEnv).toUtf8();
  const qar_qt_streaming::PanelCloseModeChoice close_choice =
    qar_qt_streaming::parse_panel_close_mode(close_mode_text.constData());
  config.close_mode = close_choice.mode;
  if (!close_choice.warning.empty()) {
    appendLog(QString("AR PANEL: %1").arg(QString::fromStdString(close_choice.warning)));
  }
  appendLog(
    QString("AR PANEL: close mode '%1'%2 — a streamed window that closes %3.")
    .arg(QString::fromLatin1(qar_qt_streaming::to_string(close_choice.mode)))
    .arg(
      close_choice.explicitly_chosen
        ? QString(" (from %1)").arg(QString::fromLatin1(qar_qt_streaming::kPanelCloseModeEnv))
        : QString(" (the default)"))
    .arg(
      close_choice.mode == qar_qt_streaming::PanelCloseMode::Destroy
      ? QString("has its AR panel closed and its video sender destroyed")
      : QString("leaves its AR panel up as a closed-window notice and keeps its video sender until the session ends")));
  try {
    panel_manager_ = std::make_unique<qar_qt_streaming::QtPanelStreamManager>(config);
  } catch (const std::exception & e) {
    panel_sender_factory_.reset();
    panel_input_source_.reset();
    return QString("could not start the AR panel manager: %1").arg(e.what());
  }
  return QString();
}

void QarStreamingPanel::refreshStreamablePanels()
{
  const QString previous = ar_panel_combo_->currentData().toString();

  streamable_panels_.clear();
  ar_panel_combo_->clear();

  // This panel first, so the default selection streams this QAR panel.
  streamable_panels_.emplace_back(QString::fromLatin1(kSelfPanelKey), QPointer<QWidget>(this));
  ar_panel_combo_->addItem("This QAR panel", QString::fromLatin1(kSelfPanelKey));

  // Then every other RViz dock panel. QDockWidget rather than
  // rviz_common::PanelDockWidget: the base class is enough to reach the content
  // widget, and it also picks up RViz's built-in docks (Displays, Views, Tool
  // Properties, Time), not only plugin panels.
  if (QWidget * top = window()) {
    for (QDockWidget * dock : top->findChildren<QDockWidget *>()) {
      QWidget * content = dock->widget();
      if (content == nullptr || content == this) {
        continue;
      }
      QString title = dock->windowTitle();
      if (title.isEmpty()) {
        title = dock->objectName();
      }
      if (title.isEmpty()) {
        continue;  // nothing to name the AR panel with
      }
      const QString key = "dock:" + title;
      streamable_panels_.emplace_back(key, QPointer<QWidget>(content));
      ar_panel_combo_->addItem(title, key);
    }
  }

  // Then every "Open video in AR" panel a web tool's button has opened. Listed
  // here (rather than a widget of their own) so the EXISTING "Stop streaming" button
  // and combo are the desktop operator's way to stop one — openVideoPanel()
  // already logs with an "AR VIDEO:" prefix and updateArPanelStatus() already
  // shows it (it walks every panel_manager_ key, not just this list). "Stop streaming"
  // on it closes it like any other panel (QAR_PANEL_CLOSE_MODE): by default
  // the stream is destroyed and the next tickVideoPanels() frees the ROS
  // subscription AND drops the entry from video_panels_, so it leaves this list
  // too (pressing the AR button builds a fresh one). In notice mode it is
  // tombstoned instead, tickVideoPanels() frees only the subscription, and "Stream panel
  // to AR" on the tombstoned entry revives it onto this SAME (now-stale) widget rather
  // than the fresh source pressing the AR button again would build — acceptable, since
  // the AR button remains the fully correct way to revive.
  for (const auto & [video_key, panel] : video_panels_) {
    if (!panel.widget) {
      continue;
    }
    const QString label = "Video: " + QString::fromStdString(video_key.substr(6));
    const QString key = QString::fromStdString(video_key);
    streamable_panels_.emplace_back(key, QPointer<QWidget>(panel.widget.get()));
    ar_panel_combo_->addItem(label, key);
  }

  // Last: the hidden Displays copy. Always offered, whether or not the widget exists yet
  // ("Stream panel to AR" creates it - onStreamSelectedPanel special-cases this key and
  // never looks it up here), because "Stop streaming" and the operator closing its AR
  // panel both leave the widget alive and "Stream panel to AR" is how it comes back.
  // QAR_PANEL_HIDDEN_DISPLAYS=off only stops the automatic start on connect.
  streamable_panels_.emplace_back(
    QString::fromLatin1(kHiddenDisplaysKey), QPointer<QWidget>(hidden_displays_.get()));
  ar_panel_combo_->addItem(hiddenDisplaysLabel(), QString::fromLatin1(kHiddenDisplaysKey));

  const int restored = ar_panel_combo_->findData(previous);
  if (restored >= 0) {
    ar_panel_combo_->setCurrentIndex(restored);
  }
}

void QarStreamingPanel::onRefreshStreamablePanels()
{
  refreshStreamablePanels();
  appendLog(
    QString("AR PANEL: %1 streamable panel(s) found.").arg(streamable_panels_.size()));
}

void QarStreamingPanel::onStreamSelectedPanel()
{
  const QString key = ar_panel_combo_->currentData().toString();
  if (key.isEmpty()) {
    return;
  }

  // The hidden Displays copy is not an RViz dock to look up: its widget may not exist
  // yet, and "Stream panel to AR" must also clear a stale stream first (see
  // startHiddenDisplaysStream). "Stop streaming" needs no such case: onStopSelectedPanel's
  // generic close only ever tears the STREAM down, never the widget.
  if (key == QString::fromLatin1(kHiddenDisplaysKey)) {
    const QString hidden_error = startHiddenDisplaysStream();
    if (!hidden_error.isEmpty()) {
      setLastError("AR panel: " + hidden_error);
    }
    return;
  }

  QWidget * widget = nullptr;
  for (const auto & [candidate_key, candidate] : streamable_panels_) {
    if (candidate_key == key && !candidate.isNull()) {
      widget = candidate.data();
      break;
    }
  }
  if (widget == nullptr) {
    setLastError("AR panel: that panel no longer exists — press Refresh.");
    refreshStreamablePanels();
    return;
  }

  const QString error = ensurePanelManager();
  if (!error.isEmpty()) {
    setLastError("AR panel: " + error);
    appendLog("AR PANEL: " + error);
    return;
  }

  const std::string manager_key = key.toStdString();
  // A TOMBSTONED key is not "already streaming" — it is a retired slot showing
  // the panel-closed notice, and starting it again is exactly how that slot
  // gets re-used instead of a second video sender being minted. See
  // QtPanelStreamManager's lifetime table.
  if (panel_manager_->has_panel(manager_key) && !panel_manager_->is_tombstoned(manager_key)) {
    appendLog(QString("AR PANEL: %1 is already streaming.").arg(key));
    return;
  }
  const bool reviving = panel_manager_->is_tombstoned(manager_key);

  qar_qt_streaming::PanelStreamOptions options;
  options.display_name = ar_panel_combo_->currentText().toStdString();
  // Hang it at eye height, one metre in front of the room origin, and step each
  // additional panel sideways so several do not stack on top of each other.
  // No call asks QarOS where the operator is: the viewer's pose comes only with a render
  // sender's frame information (qar_ogre_adapter docs/QAROS_INTEGRATION_NOTES.md,
  // "Known SDK limitations"), so a fixed placement is the best available choice.
  // Count only the panels the OPERATOR started. A dialog the library promoted to
  // a panel of its own (qar_qt_streaming docs/ARCHITECTURE.md, "Popups and
  // dialogs") is already placed relative to the panel that opened it, and
  // counting it here would shove the next operator-chosen panel a step further
  // out for no reason.
  const float slot = static_cast<float>(streamed_panels_.size());
  options.placement.pose.position = QarVector3{0.5f * slot, 1.5f, -1.0f};
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  // A dock holding an RViz Image/Camera display carries video (variant b): the
  // 15 fps control-panel default would halve a 30 fps stream, so match openVideoPanel.
  if (getDisplayContext() != nullptr) {
    std::vector<rviz_common::Display *> video_displays;
    collectVideoDisplays(getDisplayContext()->getRootDisplayGroup(), widget, video_displays);
    if (!video_displays.empty()) {
      options.max_fps = 30;
    }
  }

  // A new QarOS panel goes under a name no earlier QarOS panel of this process used; the
  // revival of a retired slot creates no QarOS panel and keeps the name it has.
  if (!reviving) {
    applyUniqueDisplayName(manager_key, options);
  }

  const qar_ar_core::Result added =
    panel_manager_->add_panel(manager_key, widget, options);
  if (!added.ok()) {
    setLastError(QString("AR panel: %1").arg(QString::fromStdString(added.message)));
    return;
  }
  streamed_panels_.emplace_back(manager_key, QPointer<QWidget>(widget));
  // Closing the RViz dock this widget lives in (its X button) closes the AR panel.
  watchDockClose(manager_key, widget);
  // Variant (b): substitute ROS video for any RViz Image/Camera display whose
  // render window lives inside this dock — a no-op for every other panel.
  attachVideoRegions(manager_key, widget);
  appendLog(
    reviving
    ? QString("AR PANEL: re-using the retired AR panel for '%1' — same QarOS panel, "
      "same video stream, no new sender.").arg(key)
    : QString("AR PANEL: streaming '%1' as a QarOS GUI panel. The QarOS panel itself "
      "is created on the next tick, off this click.").arg(key));
  updateArPanelStatus();
}

void QarStreamingPanel::onStopSelectedPanel()
{
  const QString key = ar_panel_combo_->currentData().toString();
  if (key.isEmpty() || !panel_manager_) {
    return;
  }
  // Through the manager's close mode, like every other Qt-side close: by default
  // the AR panel is closed and its video sender destroyed; under
  // QAR_PANEL_CLOSE_MODE=notice, the escape hatch for a QarOS runtime that does
  // not survive destroying a sender mid-session, it stays up as "this window has
  // closed on the RViz side, you may close the panel" and keeps its video sender
  // (qar_ogre_adapter docs/QAROS_INTEGRATION_NOTES.md, "Teardown and destroy
  // order"). Pressed in AR on the very panel it stops, this runs inside that
  // panel's own click delivery; the manager defers the teardown until the click
  // has returned.
  closeStreamedPanel(
    key.toStdString(), "stopped in RViz", "Streaming of this window was stopped in RViz.");
}

bool QarStreamingPanel::closeStreamedPanel(
  const std::string & manager_key, const QString & short_reason, const QString & notice_text)
{
  const QString key = QString::fromStdString(manager_key);

  // The host's own bookkeeping first: whatever the manager answers, this key is no
  // longer one the operator is streaming, and its dock hook has nothing left to
  // close.
  unwatchDockClose(manager_key);
  streamed_panels_.erase(
    std::remove_if(
      streamed_panels_.begin(), streamed_panels_.end(),
      [&manager_key](const std::pair<std::string, QPointer<QWidget>> & entry) {
        return entry.first == manager_key;
      }),
    streamed_panels_.end());

  if (!panel_manager_) {
    return false;
  }
  const qar_ar_core::Result closed = panel_manager_->close_panel(
    manager_key, short_reason.toStdString(), notice_text.toStdString());
  if (!closed.ok()) {
    appendLog(QString("AR PANEL: '%1' was not streaming.").arg(key));
    updateArPanelStatus();
    return false;
  }

  if (panel_manager_->close_mode() == qar_qt_streaming::PanelCloseMode::Destroy) {
    // "requested", not "done": "Stop streaming" pressed in AR on the very panel it stops
    // runs inside that panel's own click, and the manager finishes the teardown when the
    // click returns. Its own "closed ..." line (each step, frames served) is the
    // record of it having happened.
    appendLog(
      QString("AR PANEL: close requested for '%1' (%2): its AR panel is closed and its "
      "video sender destroyed — the [qt_lib] 'closed' line has each step. Starting it "
      "again makes a new AR panel.")
      .arg(key, short_reason));
  } else {
    appendLog(
      QString("AR PANEL: '%1' (%2): its AR panel now shows the window-closed notice and "
      "stays up until you close it in AR (notice mode keeps the video sender until the "
      "session ends).")
      .arg(key, short_reason));
  }
  updateArPanelStatus();
  return true;
}

void QarStreamingPanel::watchDockClose(const std::string & manager_key, QWidget * widget)
{
  unwatchDockClose(manager_key);
  // The dock that hosts a streamed RViz panel, found by walking up from its content
  // widget. rviz_common::PanelDockWidget is what RViz wraps every panel in
  // (rviz_common/panel_dock_widget.hpp), `closed()` is declared there under
  // Q_SIGNALS, and the dock's own X button reaches it through closeEvent. A widget
  // with no such ancestor — an "Open video in AR" panel is parentless — has no dock
  // to close and is simply not hooked.
  for (QWidget * ancestor = widget; ancestor != nullptr; ancestor = ancestor->parentWidget()) {
    auto * dock = qobject_cast<rviz_common::PanelDockWidget *>(ancestor);
    if (dock == nullptr) {
      continue;
    }
    dock_close_connections_[manager_key] = connect(
      dock, &rviz_common::PanelDockWidget::closed, this,
      [this, manager_key]() {
        // The dock was closed in RViz. No automatic reopen when it is shown again:
        // the operator streams it again with "Stream panel to AR".
        closeStreamedPanel(
          manager_key, "closed in RViz",
          "This window has been closed in RViz, so its picture is no longer live.");
      });
    appendLog(
      QString("AR PANEL: '%1' is in the RViz dock '%2'; closing that dock closes its AR panel.")
      .arg(QString::fromStdString(manager_key), dock->windowTitle()));
    return;
  }
}

void QarStreamingPanel::unwatchDockClose(const std::string & manager_key)
{
  const auto it = dock_close_connections_.find(manager_key);
  if (it == dock_close_connections_.end()) {
    return;
  }
  QObject::disconnect(it->second);
  dock_close_connections_.erase(it);
}

void QarStreamingPanel::applyUniqueDisplayName(
  const std::string & key, qar_qt_streaming::PanelStreamOptions & options)
{
  const std::string wanted = options.display_name;
  options.display_name = process_panel_display_names().claim(wanted);
  if (options.display_name != wanted) {
    appendLog(
      QString("AR PANEL: '%1' gets the QarOS panel name '%2', because an earlier QarOS panel "
        "of this process already used '%3'.")
      .arg(
        QString::fromStdString(key), QString::fromStdString(options.display_name),
        QString::fromStdString(wanted)));
  }
}

// ---------------------------------------------------------------------------
// The hidden RViz Displays panel
// ---------------------------------------------------------------------------

void QarStreamingPanel::startHiddenDisplaysStreamOnConnect()
{
  const QString setting = qEnvironmentVariable(kHiddenDisplaysEnv);
  const QByteArray setting_utf8 = setting.toUtf8();
  if (!qar_qar_adapter::parse_bool_env(
      setting.isEmpty() ? nullptr : setting_utf8.constData(), true))
  {
    if (!hidden_displays_disabled_logged_) {
      hidden_displays_disabled_logged_ = true;
      appendLog(
        QString("AR PANEL: the hidden Displays panel is not started on connect (%1=%2). "
          "'%3' in the panel list still starts it by hand.")
        .arg(QString::fromLatin1(kHiddenDisplaysEnv), setting.trimmed(), hiddenDisplaysLabel()));
    }
    return;
  }
  startHiddenDisplaysStream();
}

QString QarStreamingPanel::startHiddenDisplaysStream()
{
  const std::string key = kHiddenDisplaysKey;
  const QString key_text = QString::fromLatin1(kHiddenDisplaysKey);

  // A QarOS session first: the panel manager needs a running session, and says so when
  // there is none ("QarOS session is not running — connect to QarOS first (live mode)").
  // Nothing is created before it is known to be useful.
  const QString manager_error = ensurePanelManager();
  if (!manager_error.isEmpty()) {
    appendLog(
      QString("AR PANEL: the hidden Displays panel was not started: %1").arg(manager_error));
    return manager_error;
  }

  // The widget, once. Created on this (the Qt GUI) thread, shown off-screen, and kept for
  // the life of this panel: "Stop streaming", and the operator closing its AR panel, only
  // ever end the STREAM, so "Stream panel to AR" finds the same widget again.
  if (!hidden_displays_) {
    rviz_common::DisplayContext * context = getDisplayContext();
    if (context == nullptr) {
      const QString reason = "RViz has not given this panel its display context";
      appendLog(QString("AR PANEL: the hidden Displays panel was not started: %1").arg(reason));
      return reason;
    }

    // The size is read here, once, with the widget: it is fixed for the widget's life
    // (a size change would rebuild the sender), so there is nothing to re-read later.
    const QString size_setting = qEnvironmentVariable(kHiddenDisplaysSizeEnv);
    const QByteArray size_utf8 = size_setting.toUtf8();
    const qar_qt_streaming::OffscreenSizeChoice size = qar_qt_streaming::parse_offscreen_size(
      size_setting.isEmpty() ? nullptr : size_utf8.constData(),
      qar_qt_streaming::kDefaultOffscreenWidth, qar_qt_streaming::kDefaultOffscreenHeight,
      kHiddenDisplaysSizeEnv);
    if (!size.warning.empty()) {
      appendLog(QString("AR PANEL: %1").arg(QString::fromStdString(size.warning)));
    }

    HiddenRvizPanel made = create_hidden_displays_panel(context);
    if (!made.panel) {
      const QString reason = QString::fromStdString(made.error);
      appendLog(
        QString("AR PANEL: could not create the hidden Displays panel (class id '%1'%2%3): %4 "
          "-- continuing without it.")
        .arg(
          QString::fromStdString(made.class_id),
          made.factory_type.empty()
          ? QString()
          : QString(", panel factory '%1'").arg(QString::fromStdString(made.factory_type)),
          made.class_name.empty()
          ? QString()
          : QString(", Qt class '%1'").arg(QString::fromStdString(made.class_name)),
          reason));
      return reason;
    }
    if (!qar_qt_streaming::show_offscreen(made.panel.get(), size.width, size.height)) {
      const QString reason = "the new panel could not be shown off-screen";
      appendLog(
        QString("AR PANEL: could not create the hidden Displays panel: %1 -- continuing "
          "without it.").arg(reason));
      return reason;
    }
    hidden_displays_ = std::move(made.panel);

    appendLog(
      QString("AR PANEL: created a hidden RViz '%1' panel (%2): %3x%4 px, shown off-screen "
        "only - it never appears on the desktop - with item dragging off on this copy. It "
        "views RViz's live display tree and will stream as '%5' (\"%6\"). RViz's panel "
        "factory verified by its own RTTI as '%7'.")
      .arg(
        QString::fromStdString(made.class_id), QString::fromStdString(made.class_name))
      .arg(hidden_displays_->width())
      .arg(hidden_displays_->height())
      .arg(key_text, QString::fromLatin1(kHiddenDisplaysPanelName))
      .arg(QString::fromStdString(made.factory_type)));
  }

  // The stream's state. A live one is left alone (the idempotent case: "Connect to QarOS"
  // or "Stream panel to AR" pressed again). A stream the OPERATOR closed in AR is not live
  // and not gone either: capture stopped, but the entry stays because its Qt window never
  // closes - nothing would ever retire it - so it is cleared here, which is what lets the
  // add below succeed. A retired notice (QAR_PANEL_CLOSE_MODE=notice) needs nothing:
  // add_panel revives that slot.
  if (panel_manager_->has_panel(key)) {
    const bool tombstoned = panel_manager_->is_tombstoned(key);
    bool closed_in_ar = false;
    const qar_ar_core::PanelDiagnosticsSnapshot snapshot = panel_manager_->diagnostics();
    for (const qar_ar_core::PanelStreamDiagnostics & panel : snapshot.panels) {
      if (panel.key == key) {
        closed_in_ar = panel.ar_panel_closed;
        break;
      }
    }
    if (!tombstoned && !closed_in_ar) {
      appendLog(QString("AR PANEL: %1 is already streaming.").arg(key_text));
      return QString();
    }
    if (!tombstoned) {
      appendLog(
        QString("AR PANEL: '%1' was closed in AR, which stops its capture but leaves its "
          "stream entry; clearing that entry before starting it again.").arg(key_text));
      panel_manager_->close_panel(key, "restarted after being closed in AR");
    }
  }

  qar_qt_streaming::PanelStreamOptions options;
  options.display_name = kHiddenDisplaysPanelName;
  // Left of and clear of the operator's own panels, which step out from x = 0 in +0.5 m
  // slots: no call asks QarOS where the operator is looking (qar_ogre_adapter
  // docs/QAROS_INTEGRATION_NOTES.md, "Known SDK limitations"), so the placement is
  // fixed. The dialogs this panel opens (the Add-display box) are
  // placed by the manager 0.5 m to its +X, which lands them between this and the rest.
  options.placement.pose.position = QarVector3{-1.2f, 1.5f, -1.0f};
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};

  // A new QarOS panel goes under a name no earlier QarOS panel of this process used; the
  // revival of a retired slot creates no QarOS panel and keeps the name it has.
  const bool reviving = panel_manager_->is_tombstoned(key);
  if (!reviving) {
    applyUniqueDisplayName(key, options);
  }

  const qar_ar_core::Result added =
    panel_manager_->add_panel(key, hidden_displays_.get(), options);
  if (!added.ok()) {
    const QString reason = QString::fromStdString(added.message);
    appendLog(
      QString("AR PANEL: could not stream the hidden Displays panel as '%1': %2")
      .arg(key_text, reason));
    return reason;
  }
  // Deliberately NOT added to streamed_panels_: that list drives the destroyed-widget
  // sweep and the dock-closed hooks, and this widget has neither a dock nor an RViz close
  // to follow. Its stream ends only through "Stop streaming", the operator closing it in
  // AR, or the session ending.
  refreshStreamablePanels();
  appendLog(
    QString("AR PANEL: streaming the hidden Displays panel as '%1' (\"%2\", %3x%4 px) at "
      "room (-1.2, 1.5, -1.0) m. The QarOS panel itself is created on the next tick, off "
      "this call. 'Stop streaming' and 'Stream panel to AR' act on the stream only: the widget "
      "stays shown off-screen.")
    .arg(key_text, QString::fromLatin1(kHiddenDisplaysPanelName))
    .arg(hidden_displays_->width())
    .arg(hidden_displays_->height()));
  updateArPanelStatus();
  return QString();
}

void QarStreamingPanel::keepHiddenDisplaysShown()
{
  if (!hidden_displays_ || hidden_displays_->isVisible()) {
    return;
  }
  // Something hid it (a close() or hide() on its window). A hidden Qt widget is not
  // painted, so the stream would go on sending a blank rectangle; the same call that
  // showed it the first time puts it back, at the size it already has.
  qar_qt_streaming::show_offscreen(
    hidden_displays_.get(), hidden_displays_->width(), hidden_displays_->height());
  if (!hidden_displays_reshown_logged_) {
    hidden_displays_reshown_logged_ = true;
    appendLog(
      "AR PANEL: the hidden Displays panel had been hidden by something (a hide() or "
      "close() on it); it is shown off-screen again. Said once: a hidden widget would "
      "have streamed a blank rectangle.");
  }
}

void QarStreamingPanel::tickArPanels()
{
  if (!panel_manager_) {
    return;
  }

  // Before anything is captured: a hidden Displays copy is put back first.
  keepHiddenDisplaysShown();

  // Retire any stream whose source widget has been destroyed BEFORE capturing:
  // QtPanelStream holds a raw QWidget*, so a closed dock panel would otherwise be
  // painted through a dangling pointer. QPointer is what makes this detectable.
  // The keys are collected first: closing one edits streamed_panels_.
  std::vector<std::string> destroyed_widgets;
  for (const auto & entry : streamed_panels_) {
    if (entry.second.isNull()) {
      destroyed_widgets.push_back(entry.first);
    }
  }
  for (const std::string & key : destroyed_widgets) {
    closeStreamedPanel(
      key, "closed in RViz",
      "This window has been closed in RViz, so its picture is no longer live.");
  }

  // Adopt each live video panel's aspect ratio before the capture tick reads
  // it, and stop any ROS subscription whose panel has since been tombstoned.
  tickVideoPanels();
  // Variant (b)'s own throttled upkeep (~0.5 s, self-paced inside the call) —
  // same per-tick hook as tickVideoPanels() just above, for the same reason.
  maintainVideoRegions();

  // Same Qt GUI thread as the widgets themselves — the only thread allowed to
  // paint a QWidget — so there is no handoff and no lock here, unlike the Ogre path.
  panel_manager_->tick();
  // Rate-limited per-panel/per-region breakdown (~2 s) — this tick's counters
  // are current now that panel_manager_->tick() just ran.
  logVideoDiagnostics();

  // The status line is a LIVE readout and has to be refreshed from here. Rebuilt
  // only when a panel is started, stopped, or the operator hits Refresh, it would
  // show "0x0 px, 0.00 x 0.00 m, sent 0" for as long as the panel streams: the
  // start-time rebuild happens BEFORE the first tick has run, i.e. before the
  // stream has an adapter, a buffer or a size. Paced to a few Hz: at the 60 Hz
  // tick rate a QLabel::setText per frame is a relayout per frame, for a line
  // nobody reads that fast.
  const auto status_now = std::chrono::steady_clock::now();
  if (ar_status_last_.time_since_epoch().count() == 0 ||
    std::chrono::duration<double>(status_now - ar_status_last_).count() >= 0.25)
  {
    ar_status_last_ = status_now;
    updateArPanelStatus();
  }
}

void QarStreamingPanel::scheduleArPanelInputPump()
{
  if (panel_manager_ == nullptr || ar_input_pump_pending_) {
    return;
  }
  ar_input_pump_pending_ = true;
  // Coalesced: at 60 Hz a pump is posted every tick, and a pump stuck inside a
  // modal dialog's event loop must not accumulate a queue of duplicates behind
  // it. The flag is cleared BEFORE the drain, so the tick that runs inside that
  // nested loop can post the next one — which is what carries the operator's AR
  // clicks into the dialog.
  QMetaObject::invokeMethod(
    this,
    [this]() {
      ar_input_pump_pending_ = false;
      if (panel_manager_ != nullptr) {
        panel_manager_->pump_input();
      }
    },
    Qt::QueuedConnection);
}

void QarStreamingPanel::updateArPanelStatus()
{
  if (ar_panel_status_label_ == nullptr) {
    return;
  }
  if (!panel_manager_) {
    ar_panel_status_label_->setText("AR panels: none");
    return;
  }

  const auto snapshot = panel_manager_->diagnostics();
  if (snapshot.panels.empty()) {
    // Nothing is streaming. In the default close mode the panels that were are not
    // kept anywhere, so say how many were closed rather than a bare "none".
    ar_panel_status_label_->setText(
      snapshot.panels_closed > 0
      ? QString("AR panels: none (live=0 closed=%1)")
        .arg(static_cast<qulonglong>(snapshot.panels_closed))
      : QString("AR panels: none"));
    return;
  }

  QStringList lines;
  // Manager-wide input FIRST: this is the line that says whether any pointer
  // sample reached the library at all. "in" counts everything drained from the
  // input source, "missed" the ones that resolved to no panel (pointing at
  // nothing), "dropped" the ones the state machine refused (a release with no
  // press, ...). in 0 means the runtime is sending us nothing, and then no
  // per-panel number below it can be anything but zero either -- that is exactly
  // the distinction this line exists to make.
  QString input_line = QString("input: in %1, missed %2, dropped %3")
    .arg(static_cast<qulonglong>(snapshot.input_samples))
    .arg(static_cast<qulonglong>(snapshot.input_samples_missed))
    .arg(static_cast<qulonglong>(snapshot.input_samples_dropped));
  if (!snapshot.last_input_drop_reason.empty()) {
    input_line += " (" + QString::fromStdString(snapshot.last_input_drop_reason) + ")";
  }
  lines.append(input_line);

  // Live / closed / notice, before the per-panel lines. `live` is what is being
  // streamed now. `closed` is how many panels were closed and their video senders
  // destroyed this session (the default close mode keeps no entry to count them
  // by). `notice` is how many rectangles in the room are closed-window notices
  // instead (QAR_PANEL_CLOSE_MODE=notice) — a tombstoned panel is still a panel
  // QarOS holds a video sender for (qar_ogre_adapter
  // docs/QAROS_INTEGRATION_NOTES.md, "Teardown and destroy order"), so the
  // operator has to be able to see how many are notices and how many are real.
  {
    QString life = QString("panels: live=%1 closed=%2 (destroyed) notice=%3")
      .arg(snapshot.live_panels)
      .arg(static_cast<qulonglong>(snapshot.panels_closed))
      .arg(snapshot.tombstoned_panels);
    if (snapshot.ar_closed_panels > 0) {
      life += QString(" arclosed=%1").arg(snapshot.ar_closed_panels);
    }
    if (snapshot.close_panel_skipped > 0) {
      life += QString(" (%1 already closed in AR, close_panel skipped)")
        .arg(static_cast<qulonglong>(snapshot.close_panel_skipped));
    }
    if (snapshot.tombstones_created > 0) {
      life += QString(" — %1 retired as notices this session, %2 slot(s) re-used")
        .arg(static_cast<qulonglong>(snapshot.tombstones_created))
        .arg(static_cast<qulonglong>(snapshot.tombstones_revived));
    }
    if (snapshot.close_panel_failures > 0) {
      life += QString("  [%1 close_panel FAILED — see the log]")
        .arg(static_cast<qulonglong>(snapshot.close_panel_failures));
    }
    if (snapshot.tombstone_cap_refusals > 0) {
      life += QString("  [CAP REACHED — %1 panel(s) refused]")
        .arg(static_cast<qulonglong>(snapshot.tombstone_cap_refusals));
    }
    lines.append(life);
  }

  for (const auto & panel : snapshot.panels) {
    // content_scale is what QarOS is actually sent (a ZOOM, per
    // qar_streaming.h — always 1.0); device_pixel_ratio is
    // the dpr x render_scale supersample ratio behind the buffer size, shown
    // alongside it so neither number is mistaken for the other.
    QString line =
      QString("%1: %2x%3 px, %4 x %5 m, content_scale %6 sent (supersample x%7) — sent %8, unchanged %9")
      .arg(QString::fromStdString(panel.key))
      .arg(panel.buffer_width).arg(panel.buffer_height)
      .arg(panel.width_meters, 0, 'f', 2).arg(panel.height_meters, 0, 'f', 2)
      .arg(panel.content_scale, 0, 'f', 2)
      .arg(panel.device_pixel_ratio, 0, 'f', 2)
      .arg(static_cast<qulonglong>(panel.frames_submitted))
      .arg(static_cast<qulonglong>(panel.frames_skipped_unchanged));
    // Per-panel input, in the order the events travel: received from the source
    // -> delivered into the widget tree -> dropped on the way, with the most
    // recent reason. rx 0 on a panel that is visibly being poked means the events
    // never arrived; rx > 0 with delivered 0 means we are discarding them here.
    line += QString("; input rx %1, delivered %2, dropped %3")
      .arg(static_cast<qulonglong>(panel.input_samples_received))
      .arg(static_cast<qulonglong>(panel.input_events_delivered))
      .arg(static_cast<qulonglong>(panel.input_events_dropped));
    if (!panel.last_input_drop_reason.empty()) {
      line += " (" + QString::fromStdString(panel.last_input_drop_reason) + ")";
    }
    // "No receiver has attached to this panel's stream" is a STATE, not an
    // error, and the log does not say it at 60 Hz — so it has to be legible
    // here instead, or a panel that is streaming to nobody looks identical to
    // one that is working. A streak that only grows is the signature.
    if (panel.not_ready_streak > 0) {
      line += QString("; waiting for a receiver (%1 ticks not ready, %2 total)")
        .arg(static_cast<qulonglong>(panel.not_ready_streak))
        .arg(static_cast<qulonglong>(panel.frames_skipped_not_ready));
    }
    // Rebuilds: 1 is a panel that started once and has been streaming since. A
    // climbing number is resize churn, which is expensive (a new sender, a new
    // QarOS panel, a new click/hover subscription every time) and is exactly
    // what the resize hysteresis exists to prevent — so show both it and the
    // resize it is currently sitting on.
    if (panel.adapter_rebuilds > 1 || panel.resize_pending ||
      panel.resizes_ignored_small > 0)
    {
      line += QString("; rebuilds %1").arg(static_cast<qulonglong>(panel.adapter_rebuilds));
      if (panel.resizes_ignored_small > 0) {
        line += QString(", %1 small resizes absorbed")
          .arg(static_cast<qulonglong>(panel.resizes_ignored_small));
      }
      if (panel.resize_pending) {
        line += QString(", resize to %1x%2 settling")
          .arg(panel.pending_buffer_width).arg(panel.pending_buffer_height);
      }
    }
    // The notice markers come before every other remark: a panel whose source is
    // gone is not "hidden" or "unserved", it is finished, and reading the rest
    // of its counters as a live panel's would mislead.
    if (panel.tombstoned) {
      line += "  [NOTICE — source window closed";
      if (!panel.tombstone_reason.empty()) {
        line += ": " + QString::fromStdString(panel.tombstone_reason);
      }
      line += "; sender kept until session end]";
      if (panel.tombstone_clipped) {
        line += "  [panel too narrow to show its name in full]";
      }
    }
    if (panel.ar_panel_closed) {
      line += panel_manager_->close_mode() == qar_qt_streaming::PanelCloseMode::Destroy
        ? "  [CLOSED IN AR — capture stopped; the sender is destroyed when its window closes]"
        : "  [CLOSED IN AR — capture stopped, sender still held]";
      if (panel.key == kHiddenDisplaysKey) {
        // Its Qt window is never closed by anyone, so the sentence above would wait for
        // ever: say what actually brings it back.
        line += "  [the hidden Displays copy is never closed on the RViz side: pick it in "
          "the panel list and press 'Stream panel to AR' to bring it back]";
      }
    }
    if (!panel.widget_visible && !panel.tombstoned) {
      line += "  [WIDGET HIDDEN — the AR panel will look blank]";
    }
    if (!panel.last_error.empty()) {
      line += "  [" + QString::fromStdString(panel.last_error) + "]";
    }
    lines.append(line);
  }
  ar_panel_status_label_->setText("AR panels:\n" + lines.join('\n'));
}


// ===========================================================================
//  The ROS graph as AR tools
// ===========================================================================
//
// This is where the three reusable libraries meet, and the join is deliberately
// thin: qar_ros_ui turns a live topic/service/action into a UiValue and back,
// qar_ui_model is the model both sides speak, qar_web_panels serves the page
// QarOS's browser loads, and qar_qar_adapter opens the panel in the room. This
// file only sequences them; every rule with an edge case (schema -> UiModel,
// control event -> ROS action, what may be exposed) lives in the pure units
// next door, which the plain-CMake test project compiles and tests.
//
// Nothing here calls QarOS from the click: introspection and generation are
// in-process work on already-loaded typesupport, add_panel performs no runtime
// call, and every QarOS-facing step runs on a later onRenderTick(), on the GUI thread.

QString QarStreamingPanel::ensureRosBridges()
{
  if (graph_discovery_) {
    return QString();
  }
  if (getDisplayContext() == nullptr) {
    return "no RViz DisplayContext";
  }
  // The panel may not have needed a node yet (geolocation is opt-out), so
  // resolve it here rather than relying on setupGeoSubscription having run.
  if (!ros_node_) {
    auto ros_abstraction = getDisplayContext()->getRosNodeAbstraction().lock();
    if (!ros_abstraction) {
      return "RViz has no ROS node abstraction";
    }
    ros_node_ = ros_abstraction->get_raw_node();
  }
  if (!ros_node_) {
    return "RViz has no ROS node";
  }

  // RViz's own node, deliberately: it is already spun with spin_some(10 ms) ON
  // THIS THREAD by VisualizationManager::onUpdate, so every bridge callback
  // lands on the Qt GUI thread and no executor of our own is needed. That is
  // exactly the host qar_ros_ui was designed against
  // (docs/ARCHITECTURE.md, "Threading: nothing waits").
  // Dereferenced on purpose: NodeHandles::from has both a `NodeT&` and a
  // `shared_ptr<NodeT>` overload, and a non-const shared_ptr lvalue binds the
  // first with NodeT = shared_ptr<Node>, which has none of the interfaces.
  const qar_ros_ui::NodeHandles handles = qar_ros_ui::NodeHandles::from(*ros_node_);
  if (!handles.valid()) {
    return "RViz's ROS node did not yield the interfaces qar_ros_ui needs";
  }
  try {
    graph_discovery_ = std::make_unique<qar_ros_ui::GraphDiscovery>(handles);
    topic_bridge_ = std::make_unique<qar_ros_ui::TopicBridge>(handles, nullptr, this);
    service_bridge_ = std::make_unique<qar_ros_ui::ServiceBridge>(handles, nullptr, this);
    action_bridge_ = std::make_unique<qar_ros_ui::ActionBridge>(handles, nullptr, this);
  } catch (const std::exception & e) {
    graph_discovery_.reset();
    topic_bridge_.reset();
    service_bridge_.reset();
    action_bridge_.reset();
    return QString("could not start the ROS bridges: %1").arg(e.what());
  }
  appendLog("ROS UI: graph discovery + topic/service/action bridges live on RViz's node.");
  return QString();
}

std::vector<RosGraphEntry> QarStreamingPanel::queryRosGraph()
{
  std::vector<RosGraphEntry> entries;
  const QString error = ensureRosBridges();
  if (!error.isEmpty()) {
    appendLog("GRAPH: " + error);
    return entries;
  }

  // refresh() never waits on discovery: it reports what the middleware knows
  // now, and recovers actions from their five protocol entities (an action is
  // not a graph entity of its own).
  const qar_ros_ui::Result refreshed = graph_discovery_->refresh();
  if (!refreshed.ok()) {
    appendLog(QString("GRAPH: refresh failed — %1")
      .arg(QString::fromStdString(refreshed.message)));
  }

  const qar_ros_ui::GraphSnapshot & snapshot = graph_discovery_->current();
  const auto append = [&entries](RosEntityKind kind,
      const std::vector<qar_ros_ui::GraphEntity> & source) {
      for (const qar_ros_ui::GraphEntity & item : source) {
        RosGraphEntry entry;
        entry.kind = kind;
        entry.name = QString::fromStdString(item.name);
        for (const std::string & type : item.types) {
          entry.types.append(QString::fromStdString(type));
        }
        entries.push_back(std::move(entry));
      }
    };
  append(RosEntityKind::Topic, snapshot.topics);
  append(RosEntityKind::Service, snapshot.services);
  append(RosEntityKind::Action, snapshot.actions);
  return entries;
}

const qar_web_panels::UrlPanelSizing & QarStreamingPanel::urlPanelSizing()
{
  if (url_panel_sizing_loaded_) {
    return url_panel_sizing_;
  }
  url_panel_sizing_loaded_ = true;
  // Same family and spelling as QAR_PANEL_WEB_HOST / QAR_PANEL_WEB_PORT:
  //   QAR_PANEL_WEB_SIZE=<w>x<h>  metres, pins the size outright
  //   QAR_PANEL_WEB_SCALE=<f>     multiplier on the derived size
  // Read ONCE, because the derived default is the answer for a session that
  // does not care; the spin box next to "Re-open in AR" is the live knob.
  // No logging here: this runs while the widgets are still being built, before
  // the diagnostics view exists. The note is kept and reported by
  // ensureWebPanelServer(), where the operator can actually read it.
  qar_web_panels::apply_sizing_overrides(
    url_panel_sizing_, qar_web_panels::process_environment(), url_panel_sizing_note_);
  return url_panel_sizing_;
}

QString QarStreamingPanel::ensureWebPanelServer()
{
  if (web_panel_server_ && web_panel_server_->is_running()) {
    return QString();
  }
  if (!web_panel_server_) {
    qar_web_panels::WebPanelServerConfig config;
    // Defaults on purpose: bind every interface (the headset has to reach us) on
    // an OS-assigned port (two RViz instances on one machine must not collide),
    // with the 128-bit token in each URL as the gate. Both are overridable for a
    // firewall rule or a machine with several networks — "which network is the
    // headset on?" has no right automatic answer, only a good default.
    const QByteArray host = qgetenv("QAR_PANEL_WEB_HOST");
    if (!host.isEmpty()) {
      config.advertised_host = host.toStdString();
    }
    const QByteArray port = qgetenv("QAR_PANEL_WEB_PORT");
    if (!port.isEmpty()) {
      config.port = static_cast<quint16>(port.toUShort());
    }
    web_panel_server_ = std::make_unique<qar_web_panels::WebPanelServer>(config);
  }

  std::string error;
  if (!web_panel_server_->start(&error)) {
    return QString::fromStdString(error);
  }
  appendLog(
    QString("AR TOOL: panel server listening on %1:%2 (a wrong or missing token is a 404).")
    .arg(QString::fromStdString(web_panel_server_->advertised_host()))
    .arg(web_panel_server_->port()));
  // Reported here rather than where it was read: urlPanelSizing() runs during
  // widget construction, when there is no diagnostics view to write to.
  urlPanelSizing();
  if (!url_panel_sizing_note_.empty()) {
    appendLog("AR TOOL: " + QString::fromStdString(url_panel_sizing_note_) + ".");
    url_panel_sizing_note_.clear();
  }
  return QString();
}

QString QarStreamingPanel::exposeEntity(const AddTopicSelection & selection)
{
  RosEntityRef entity;
  entity.kind = selection.kind;
  entity.name = selection.name.toStdString();
  entity.type_name = selection.message_type.toStdString();
  if (entity.type_name.empty()) {
    return "the graph reports no type for " + selection.name;
  }
  if (exposures_.contains(entity.kind, entity.name)) {
    return selection.name + " is already exposed";
  }

  QString error = ensureRosBridges();
  if (!error.isEmpty()) {
    return error;
  }

  // Introspection needs no generated header and no compile-time knowledge of the
  // type: it reads the interface package's own introspection typesupport.
  PanelGenerationRequest request;
  request.entity = entity;
  request.writable = selection.allow_publish;
  qar_ros_ui::Result introspected;
  switch (entity.kind) {
    case RosEntityKind::Topic:
      introspected = qar_ros_ui::introspect_message(entity.type_name, request.message);
      break;
    case RosEntityKind::Service:
      introspected = qar_ros_ui::introspect_service(entity.type_name, request.service);
      break;
    case RosEntityKind::Action:
      introspected = qar_ros_ui::introspect_action(entity.type_name, request.action);
      break;
  }
  if (!introspected.ok()) {
    return QString("could not introspect %1: %2")
           .arg(selection.message_type, QString::fromStdString(introspected.message));
  }

  // The template seam. The registry holds the semantic layouts and the generic
  // form; a semantic template outscores the generic form for the types it claims.
  const PanelGenerationResult generated = panel_templates_.generate(request);
  if (!generated.ok) {
    return QString::fromStdString(generated.error);
  }

  error = ensureWebPanelServer();
  if (!error.isEmpty()) {
    return "panel server: " + error;
  }

  std::string add_error;
  std::shared_ptr<qar_web_panels::PanelSession> session =
    web_panel_server_->add_panel(generated.model, &add_error);
  if (!session) {
    return QString("panel server refused the model: %1").arg(QString::fromStdString(add_error));
  }
  session->set_control_sink(this);
  // The page measures the viewport it was actually given and tells us; that is
  // the only source of the number, because the SDK does not state the rule
  // (docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser"). Direct
  // connection: the session lives on this thread, which is also the thread that
  // may touch the QarOS panel.
  const std::string session_panel_id = generated.model.root.id;
  connect(
    session.get(), &qar_web_panels::PanelSession::viewport_reported, this,
    [this, session_panel_id](const qar_web_panels::PanelViewportReport & report) {
      onArToolViewport(session_panel_id, report);
    });

  auto tool = std::make_unique<ArToolPanel>();
  tool->panel_id = generated.model.root.id;
  tool->entity = entity;
  tool->writable = request.writable;
  tool->session = session;
  tool->router = std::make_unique<ArControlRouter>(
    entity, generated.model, generated.document_schema);

  if (entity.kind == RosEntityKind::Topic) {
    // PULL, not push: the subscription keeps the newest serialized message and
    // the panel converts it on its own refresh tick, so a 500 Hz topic shown at
    // 5 Hz costs five conversions a second — on the GUI thread, which is also
    // the executor thread here.
    qar_ros_ui::TopicSubscriptionOptions options;  // Adaptive QoS by default
    const qar_ros_ui::Result subscribed = topic_bridge_->subscribe(
      entity.name, entity.type_name, options, tool->subscription);
    if (!subscribed.ok()) {
      appendLog(QString("AR TOOL: %1 has no live subscription — %2")
        .arg(selection.name, QString::fromStdString(subscribed.message)));
    }
  }

  ExposedEntity row;
  row.entity = entity;
  row.writable = request.writable;
  row.panel_id = tool->panel_id;
  row.template_name = generated.template_name;
  row.token = session->token();
  row.page_url = web_panel_server_->page_url(*session);
  row.subscribed = static_cast<bool>(tool->subscription);
  row.refresh_hz = tool->refresh_hz;

  std::string registry_error;
  if (!exposures_.add(row, &registry_error)) {
    web_panel_server_->remove_panel(session->token());
    return QString::fromStdString(registry_error);
  }
  ar_tools_.push_back(std::move(tool));
  refreshExposureList();

  appendLog(
    QString("AR TOOL: exposed %1 as '%2' (%3) at %4. The QarOS panel is created "
    "on the next tick, off this click.")
    .arg(selection.name, QString::fromStdString(row.panel_id),
      QString::fromStdString(row.template_name), QString::fromStdString(row.page_url)));
  return QString();
}

QarStreamingPanel::ArToolPanel * QarStreamingPanel::findArTool(const std::string & panel_id)
{
  for (auto & tool : ar_tools_) {
    if (tool->panel_id == panel_id) {
      return tool.get();
    }
  }
  return nullptr;
}

void QarStreamingPanel::onArToolViewport(
  const std::string & panel_id, const qar_web_panels::PanelViewportReport & report)
{
  ArToolPanel * tool = findArTool(panel_id);
  if (tool == nullptr || !tool->session || !web_panel_server_) {
    return;
  }
  const std::string described = report.describe();
  appendLog(QString("[qar-web] %1").arg(QString::fromStdString(described)));

  const bool first = !tool->viewport.has_value();
  tool->viewport = report.viewport;

  qar_web_panels::UrlPanelSizing sizing = urlPanelSizing();
  if (ar_panel_scale_spin_ != nullptr) {
    sizing.scale = ar_panel_scale_spin_->value();
  }
  qar_web_panels::use_reported_viewport(sizing, report.viewport);
  const qar_web_panels::UrlPanelSizeResult sized = qar_web_panels::size_url_panel_for(
    tool->session->model(), web_panel_server_->config().render.theme, sizing);

  // The entity name, not the panel id: the status line is read by an operator
  // who knows /scan, not the UUID we minted for it. It carries the panel the
  // viewport produced as well as the viewport itself, because "the panel is too
  // small" is the problem this whole path solves and the metres are the answer
  // to it.
  const QString fitted = QString(" -> panel %1x%2 m, theme x%3, %4")
    .arg(static_cast<double>(sized.size.width_meters), 0, 'f', 2)
    .arg(static_cast<double>(sized.size.height_meters), 0, 'f', 2)
    .arg(sized.theme_scale, 0, 'f', 2)
    .arg(sized.columns == 1 ? QStringLiteral("1 column")
      : QStringLiteral("%1 columns").arg(static_cast<int>(sized.columns)));
  ar_viewport_note_ = tool->entity.name + ": " + described + fitted.toStdString();

  // Only when it actually moves. The page re-reports on every resize, and a
  // resize is what a re-placement causes, so pushing an unchanged size would be
  // a loop; a millimetre of rounding is not a change.
  const bool moved =
    std::abs(sized.size.width_meters - tool->placement.size.width_meters) > 0.005f ||
    std::abs(sized.size.height_meters - tool->placement.size.height_meters) > 0.005f;
  if (first || moved) {
    appendLog(QString("AR TOOL: %1 — %2.")
      .arg(QString::fromStdString(tool->entity.name), QString::fromStdString(sized.note)));
  }
  if (moved && tool->ar_panel) {
    qar_ar_core::UrlPanelPlacement placement = tool->placement;
    placement.size = sized.size;
    if (tool->ar_panel->set_placement(placement)) {
      tool->placement = placement;
    } else if (ExposedEntity * row = exposures_.find_by_panel_id(panel_id); row != nullptr) {
      row->last_error = tool->ar_panel->last_error();
    }
  }
  refreshExposureList();
}

void QarStreamingPanel::retireExposure(RosEntityKind kind, const QString & name)
{
  const std::string entity_name = name.toStdString();
  const ExposedEntity * row = exposures_.find(kind, entity_name);
  if (row == nullptr) {
    return;
  }
  const std::string panel_id = row->panel_id;

  for (auto it = ar_tools_.begin(); it != ar_tools_.end(); ++it) {
    if ((*it)->panel_id != panel_id) {
      continue;
    }
    if ((*it)->ar_panel) {
      (*it)->ar_panel->close();
    }
    if ((*it)->session && web_panel_server_) {
      // Forgets the token BEFORE closing the viewers, so a viewer's automatic
      // reconnect meets a 404 rather than a half-removed session.
      web_panel_server_->remove_panel((*it)->session->token());
    }
    // Dropping the tool drops the subscription / publisher / goal handle with
    // it: every qar_ros_ui entity is RAII and leaves the node when released.
    ar_tools_.erase(it);
    break;
  }
  exposures_.remove(kind, entity_name);
  refreshExposureList();
  appendLog(QString("AR TOOL: retired %1.").arg(name));
}

void QarStreamingPanel::refreshExposureList()
{
  if (exposure_list_ == nullptr) {
    return;
  }
  const QString previous =
    exposure_list_->currentItem() == nullptr
    ? QString()
    : exposure_list_->currentItem()->data(Qt::UserRole).toString();

  exposure_list_->clear();
  for (const ExposedEntity & row : exposures_.entries()) {
    QString label = QString::fromStdString(row.describe());
    label += row.ar_panel_open ? "  [in AR]" : "  [pending]";
    if (row.writable) {
      label += " [writable]";
    }
    auto * item = new QListWidgetItem(label, exposure_list_);
    item->setData(Qt::UserRole, QString::fromStdString(row.panel_id));
    QString tip = QString::fromStdString(row.page_url);
    tip += QString("\ntemplate: %1").arg(QString::fromStdString(row.template_name));
    if (!row.last_error.empty()) {
      tip += "\n" + QString::fromStdString(row.last_error);
    }
    item->setToolTip(tip);
    if (!previous.isEmpty() && QString::fromStdString(row.panel_id) == previous) {
      exposure_list_->setCurrentItem(item);
    }
  }

  if (exposure_status_label_ == nullptr) {
    return;
  }
  if (exposures_.empty()) {
    exposure_status_label_->setText("AR tools: none");
    return;
  }
  QString status = QString("AR tools: %1").arg(exposures_.size());
  if (web_panel_server_ && web_panel_server_->is_running()) {
    status += QString(" — served on %1:%2")
      .arg(QString::fromStdString(web_panel_server_->advertised_host()))
      .arg(web_panel_server_->port());
  }
  // What the page says about the box it was given. Until one has loaded there
  // is nothing to say, and saying so is better than an assumed number.
  status += ar_viewport_note_.empty()
    ? QString("\nviewport: no page has reported one yet")
    : QString("\n%1").arg(QString::fromStdString(ar_viewport_note_));
  exposure_status_label_->setText(status);
}

ExposedEntity * QarStreamingPanel::selectedExposure()
{
  if (exposure_list_ == nullptr || exposure_list_->currentItem() == nullptr) {
    return nullptr;
  }
  const QString panel_id = exposure_list_->currentItem()->data(Qt::UserRole).toString();
  return exposures_.find_by_panel_id(panel_id.toStdString());
}

void QarStreamingPanel::onRemoveExposure()
{
  const ExposedEntity * row = selectedExposure();
  if (row == nullptr) {
    return;
  }
  retireExposure(row->entity.kind, QString::fromStdString(row->entity.name));
}

void QarStreamingPanel::onReopenExposure()
{
  ExposedEntity * row = selectedExposure();
  if (row == nullptr) {
    return;
  }
  ArToolPanel * tool = findArTool(row->panel_id);
  if (tool == nullptr) {
    return;
  }
  if (tool->ar_panel) {
    tool->ar_panel->close();
    tool->ar_panel.reset();
  }
  // The page and its token are untouched: only the QarOS panel is re-created,
  // and that happens on the next render tick like every other QarOS call.
  tool->ar_panel_pending = true;
  tool->pending_logged = false;
  row->ar_panel_open = false;
  refreshExposureList();
  appendLog(QString("AR TOOL: %1 will be re-opened on the next tick.")
    .arg(QString::fromStdString(row->entity.name)));
}

void QarStreamingPanel::onCopyExposureUrl()
{
  const ExposedEntity * row = selectedExposure();
  if (row == nullptr || row->page_url.empty()) {
    return;
  }
  if (QClipboard * clipboard = QApplication::clipboard()) {
    clipboard->setText(QString::fromStdString(row->page_url));
  }
  appendLog(QString("AR TOOL: copied %1").arg(QString::fromStdString(row->page_url)));
}

QString QarStreamingPanel::openArPanelFor(ArToolPanel & tool)
{
  if (!qar_factory_ || !qar_factory_->ready()) {
    return "QarOS session is not running — connect to QarOS first (live mode)";
  }
  if (!url_panel_factory_) {
    // Borrows the QarSession the render path already owns: a panel belongs to a
    // session, and that is the session the headset is attached to.
    url_panel_factory_ =
      std::make_shared<qar_qar_adapter::QarCApiUrlPanelFactory>(qar_factory_.get());
  }
  ExposedEntity * row = exposures_.find_by_panel_id(tool.panel_id);
  if (row == nullptr || row->page_url.empty()) {
    return "the panel has no URL yet";
  }
  if (!tool.session || !web_panel_server_) {
    // The page is what is measured, so there is nothing to size against.
    return "the panel has no served session yet";
  }

  qar_ar_core::UrlPanelDesc desc;
  desc.url = row->page_url;
  desc.display_name = tool.entity.name;
  // No call asks QarOS where the operator is: the viewer's pose comes only with a render
  // sender's frame information (qar_ogre_adapter docs/QAROS_INTEGRATION_NOTES.md,
  // "Known SDK limitations"), so a fixed placement is the best available choice; each
  // tool steps sideways so several do not stack on top of one another.
  std::size_t slot = 0;
  for (const auto & other : ar_tools_) {
    if (other.get() == &tool) {
      break;
    }
    ++slot;
  }
  desc.placement.pose.position = QarVector3{-0.7f * static_cast<float>(slot), 1.5f, -1.2f};
  desc.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};

  // Size from the PAGE, not from a guess. qar_ui_model::measure_page walks the
  // generated widget tree with the AR theme's own numbers and says how many CSS
  // pixels it wants; url_panel_sizing turns those into metres at the same
  // arc-minutes of text a QWidget panel gets (see its file comment). A fixed
  // landscape size would show only the top half of a generated form, which is
  // portrait.
  qar_web_panels::UrlPanelSizing sizing = urlPanelSizing();
  if (ar_panel_scale_spin_ != nullptr) {
    sizing.scale = ar_panel_scale_spin_->value();
  }
  // If this panel has been open before, its page has already said what viewport
  // the runtime gives it, and that retires the estimate — see the "the viewport
  // does NOT follow the panel" section of url_panel_sizing.hpp.
  if (tool.viewport) {
    qar_web_panels::use_reported_viewport(sizing, *tool.viewport);
  }
  const qar_web_panels::UrlPanelSizeResult sized = qar_web_panels::size_url_panel_for(
    tool.session->model(), web_panel_server_->config().render.theme, sizing);
  desc.placement.size = sized.size;

  std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> adapter =
    url_panel_factory_->create_url_panel(desc);
  if (!adapter) {
    row->last_error = url_panel_factory_->last_error();
    return QString::fromStdString(row->last_error);
  }
  tool.ar_panel = std::move(adapter);
  tool.placement = desc.placement;
  tool.ar_panel_pending = false;
  tool.pending_logged = false;
  row->ar_panel_open = true;
  row->last_error.clear();
  refreshExposureList();
  appendLog(QString("AR TOOL: opened %1 in AR at %2 — %3.")
    .arg(QString::fromStdString(tool.entity.name), QString::fromStdString(row->page_url),
      QString::fromStdString(sized.note)));
  return QString();
}

void QarStreamingPanel::pushArToolData(
  ArToolPanel & tool, std::chrono::steady_clock::time_point now)
{
  if (!tool.session) {
    return;
  }
  // Coalesce to the panel's cadence rather than pushing per message: a sensor
  // topic at 500 Hz and a panel a human reads at 5 Hz are different clocks, and
  // the subscription's mailbox converts on demand precisely so this is the cheap
  // direction.
  const double hz = tool.refresh_hz > 0.0 ? tool.refresh_hz : 5.0;
  const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
    std::chrono::duration<double>(1.0 / hz));
  if (tool.last_push.time_since_epoch().count() != 0 && (now - tool.last_push) < period) {
    return;
  }
  tool.last_push = now;

  if (tool.entity.kind == RosEntityKind::Topic && tool.subscription) {
    qar_ui_model::UiValue value;
    if (tool.subscription->take_latest(value)) {
      // An Image's `data` is megabytes and a LaserScan's `ranges` thousands
      // of floats. No template binds them, but the host pushes the WHOLE message,
      // so "not bound" alone would still put them on the socket to be ignored by
      // the page. Capped here, once, before either copy is taken.
      elide_large_arrays(value);
      // The router's document is what a later publish stages against, so both
      // copies move together.
      tool.router->set_document_value(ar_doc::kMessage, value);
      tool.session->set_value(ar_doc::kMessage, value);
    }
    return;
  }

  if (tool.entity.kind == RosEntityKind::Action && tool.goal) {
    qar_ui_model::UiValue feedback;
    if (tool.goal->take_feedback(feedback)) {
      tool.session->set_value(ar_doc::kFeedback, feedback);
    }
    // +1 so "never published" (0) is distinguishable from GoalStatus::Unknown.
    const auto status = static_cast<std::uint64_t>(tool.goal->status()) + 1;
    if (status != tool.last_status_published) {
      tool.last_status_published = status;
      tool.session->set_value(
        ar_doc::kStatus,
        qar_ui_model::UiValue(std::string(qar_ros_ui::to_string(tool.goal->status()))));
    }
    if (!tool.result_published && tool.goal->finished()) {
      qar_ui_model::UiValue result;
      if (tool.goal->result(result)) {
        tool.session->set_value(ar_doc::kResult, result);
      }
      tool.result_published = true;
    }
  }
}

void QarStreamingPanel::tickArToolPanels()
{
  if (ar_tools_.empty()) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();

  // Expire overdue calls/goals and send deferred ones. Both are cheap when
  // nothing is pending, and this is the tick qar_ros_ui asks a host for instead
  // of a thread of its own.
  if (service_bridge_) {
    service_bridge_->poll();
  }
  if (action_bridge_) {
    action_bridge_->poll();
  }
  // Adaptive QoS has to follow the publishers that come and go; once a second is
  // what `ros2 topic echo` effectively does by hand.
  if (topic_bridge_ &&
    (last_qos_reconcile_.time_since_epoch().count() == 0 ||
    (now - last_qos_reconcile_) >= std::chrono::seconds(1)))
  {
    topic_bridge_->reconcile();
    last_qos_reconcile_ = now;
  }

  for (auto & tool : ar_tools_) {
    if (tool->ar_panel_pending) {
      const QString error = openArPanelFor(*tool);
      if (!error.isEmpty() && !tool->pending_logged) {
        // Once per tool, not once per tick: "no session yet" is the normal state
        // until the operator connects to QarOS.
        tool->pending_logged = true;
        appendLog(QString("AR TOOL: %1 is served but not in AR yet — %2")
          .arg(QString::fromStdString(tool->entity.name), error));
      }
    }
    pushArToolData(*tool, now);
  }
}

// --- Control in -------------------------------------------------------------

void QarStreamingPanel::on_control(
  qar_web_panels::PanelSession & session, const qar_web_panels::PanelControlEvent & event)
{
  ArToolPanel * tool = findArTool(session.panel_id());
  if (tool == nullptr) {
    session.ack(event, false, "this panel is no longer bound to a ROS entity");
    return;
  }
  const ControlRouting routing = tool->router->route(event.control);
  if (!routing.ok) {
    session.ack(event, false, routing.error);
    appendLog(QString("AR CONTROL: refused '%1' on %2 — %3")
      .arg(QString::fromStdString(event.control.control),
        QString::fromStdString(tool->entity.name),
        QString::fromStdString(routing.error)));
    return;
  }
  executeControlIntent(*tool, routing.intent, event);
}

void QarStreamingPanel::executeControlIntent(
  ArToolPanel & tool, const ControlIntent & intent,
  const qar_web_panels::PanelControlEvent & event)
{
  // Deferred callbacks capture the panel id, never a pointer into ar_tools_: a
  // service response or a goal event arrives later, by which time the tool may
  // have been retired.
  const std::string panel_id = tool.panel_id;
  const std::uint64_t client_id = event.client_id;
  const std::uint64_t event_seq = event.seq;

  switch (intent.kind) {
    case RosActionKind::SetField:
      // The page applied it optimistically; echoing it makes every OTHER viewer
      // of this panel see the edit at once, which the protocol leaves to the host.
      tool.session->set_value(intent.document_path, intent.value);
      tool.session->ack(event, true, "staged");
      return;

    case RosActionKind::PublishTopic: {
      if (!tool.publisher) {
        qar_ros_ui::TopicPublisherOptions options;
        const qar_ros_ui::Result advertised = topic_bridge_->advertise(
          tool.entity.name, tool.entity.type_name, options, tool.publisher);
        if (!advertised.ok()) {
          tool.session->ack(event, false, advertised.message);
          return;
        }
      }
      const qar_ros_ui::Result published = tool.publisher->publish(intent.payload);
      tool.session->ack(
        event, published.ok(), published.ok() ? "published" : published.message);
      appendLog(QString("AR CONTROL: publish to %1 %2")
        .arg(QString::fromStdString(tool.entity.name),
          published.ok() ? QString("ok") : QString::fromStdString(published.message)));
      return;
    }

    case RosActionKind::CallService: {
      qar_ros_ui::ServiceCallOptions options;
      const qar_ros_ui::Result queued = service_bridge_->call(
        tool.entity.name, tool.entity.type_name, intent.payload, options,
        [this, panel_id, client_id, event_seq](const qar_ros_ui::ServiceCallOutcome & outcome) {
          ArToolPanel * live = findArTool(panel_id);
          if (live == nullptr || !live->session) {
            return;  // the tool was retired while the call was in flight
          }
          if (outcome.ok()) {
            live->router->set_document_value(ar_doc::kResponse, outcome.response);
            live->session->set_value(ar_doc::kResponse, outcome.response);
          }
          live->session->ack(
            client_id, event_seq, outcome.ok(),
            outcome.ok() ? std::string("responded") : outcome.status.message);
        });
      if (!queued.ok()) {
        tool.session->ack(event, false, queued.message);
      }
      // Otherwise the ack is the callback's job: nothing here waits.
      return;
    }

    case RosActionKind::SendGoal: {
      qar_ros_ui::ActionGoalOptions options;
      options.on_event =
        [this, panel_id, client_id, event_seq](
        const std::shared_ptr<qar_ros_ui::ActionGoalHandle> &,
        const qar_ros_ui::GoalEvent & goal_event) {
          ArToolPanel * live = findArTool(panel_id);
          if (live == nullptr || !live->session) {
            return;
          }
          switch (goal_event.kind) {
            case qar_ros_ui::GoalEventKind::Accepted:
              live->session->ack(client_id, event_seq, true, "goal accepted");
              break;
            case qar_ros_ui::GoalEventKind::Rejected:
              live->session->ack(client_id, event_seq, false, "goal rejected");
              break;
            case qar_ros_ui::GoalEventKind::Error:
              live->session->ack(client_id, event_seq, false, goal_event.error.message);
              break;
            default:
              break;  // feedback / status / result reach the page on the tick
          }
        };
      tool.last_status_published = 0;
      tool.result_published = false;
      const qar_ros_ui::Result sent = action_bridge_->send_goal(
        tool.entity.name, tool.entity.type_name, intent.payload, options, tool.goal);
      if (!sent.ok()) {
        tool.session->ack(event, false, sent.message);
      }
      return;
    }

    case RosActionKind::CancelGoal: {
      if (!tool.goal) {
        tool.session->ack(event, false, "no goal is in flight");
        return;
      }
      const qar_ros_ui::Result cancelled = action_bridge_->cancel(tool.goal);
      tool.session->ack(
        event, cancelled.ok(), cancelled.ok() ? std::string("cancel requested")
        : cancelled.message);
      return;
    }

    case RosActionKind::None:
      tool.session->ack(event, false, "nothing to do");
      return;

    case RosActionKind::OpenVideoPanel:
      openVideoPanel(tool, event);
      return;
  }
}

std::shared_ptr<RosImageFrameSource> QarStreamingPanel::acquireFrameSource(
  const std::string & topic, ImageTransport transport)
{
  auto it = video_frame_sources_.find(topic);
  if (it != video_frame_sources_.end()) {
    if (auto existing = it->second.lock()) {
      appendLog(
        QString("AR VIDEO: '%1' reusing its existing ROS subscription — no second "
          "subscribe/decode for the same topic.")
        .arg(QString::fromStdString(topic)));
      return existing;
    }
  }
  // The subscription itself is on video_executor_'s dedicated node/thread,
  // NOT ros_node_ (RViz's own node, spun with spin_some only when the Qt GUI
  // thread is idle) — see video_source_executor.hpp. ros_node_ is still used
  // elsewhere (get_topic_names_and_types() / transport choice above): graph
  // introspection has no such starvation problem.
  if (!video_executor_) {
    auto executor = std::make_unique<VideoSourceExecutor>();
    if (executor->error().empty()) {
      video_executor_ = std::move(executor);
      appendLog(
        "AR VIDEO: video topics are received in their own ROS context (a DDS participant "
        "separate from RViz's), so RViz's own displays of the same topic cannot starve them.");
    } else {
      // Not kept: video_executor_ stays null so the next open retries. This source
      // gets a null node and says "No ROS node available" instead of subscribing.
      appendLog(
        QString("AR VIDEO: could not create the private ROS context for video topics (%1) — "
          "'%2' will show no frames.")
        .arg(QString::fromStdString(executor->error()), QString::fromStdString(topic)));
    }
  }
  // The new context's participant has discovered nothing yet, so the QoS decision
  // (reliable vs best-effort, from the topic's current publishers) must read the
  // graph from RViz's long-lived node instead — see RosImageFrameSource's graph_node.
  auto source = std::make_shared<RosImageFrameSource>(
    video_executor_ ? video_executor_->node() : nullptr, topic, transport, ros_node_);
  if (video_executor_) {
    video_frame_sources_[topic] = source;
  }
  return source;
}

void QarStreamingPanel::openVideoPanel(
  ArToolPanel & tool, const qar_web_panels::PanelControlEvent & event)
{
  if (!ros_node_ && getDisplayContext() != nullptr) {
    auto ros_abstraction = getDisplayContext()->getRosNodeAbstraction().lock();
    if (ros_abstraction) {
      ros_node_ = ros_abstraction->get_raw_node();
    }
  }
  if (!ros_node_) {
    tool.session->ack(event, false, "no ROS node available");
    return;
  }
  const QString manager_error = ensurePanelManager();
  if (!manager_error.isEmpty()) {
    tool.session->ack(event, false, manager_error.toStdString());
    return;
  }

  const std::string & topic = tool.entity.name;
  const std::string key = "video:" + topic;

  if (panel_manager_->has_panel(key) && !panel_manager_->is_tombstoned(key)) {
    tool.session->ack(event, true, "video panel already open");
    return;
  }
  // A tombstoned key cannot be revived onto its OLD widget: reviving means a
  // fresh ROS subscription, and VideoPanelWidget's LatestVideoFrame is fixed at
  // construction (see ros_image_frame_source.hpp). So every path here — first
  // open or revival — builds a brand new {source, widget} pair; add_panel below
  // is what actually revives the tombstoned SLOT (same sender, same QarOS
  // panel) onto this new widget rather than minting a second sender.
  const bool reviving = panel_manager_->is_tombstoned(key);

  std::vector<std::string> advertised_types;
  const auto topic_types = ros_node_->get_topic_names_and_types();
  const auto found_topic = topic_types.find(topic);
  if (found_topic != topic_types.end()) {
    advertised_types = found_topic->second;
  }
  const ImageTransport transport = choose_image_transport(topic, advertised_types);

  auto source = acquireFrameSource(topic, transport);
  auto widget = std::make_unique<qar_qt_streaming::VideoPanelWidget>(
    source->frames(), QString::fromStdString(topic));

  qar_qt_streaming::PanelStreamOptions options;
  options.display_name = topic;
  // Actual video, not a status form: the 15 Hz control-panel default would
  // visibly stutter a camera feed.
  options.max_fps = 30;
  // Same placement rule onStreamSelectedPanel uses: no call asks QarOS where the operator
  // is looking, only a render sender reports the viewer's pose (qar_ogre_adapter
  // docs/QAROS_INTEGRATION_NOTES.md, "Known SDK limitations"), so a fixed spot, stepped
  // sideways per already-streamed panel, is the best available choice.
  // Placing this beside the web tool's OWN AR panel would need translating
  // qar_ar_core::UrlPanelPlacement into this manager's room-space
  // PanelPlacement, which are two different subsystems' placement types with
  // no existing conversion — not the "straightforward" case, so this uses the
  // same fallback slot logic rather than inventing that conversion here.
  const float slot = static_cast<float>(streamed_panels_.size());
  options.placement.pose.position = QarVector3{0.5f * slot, 1.5f, -1.0f};
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};

  // A new QarOS panel goes under a name no earlier QarOS panel of this process used; the
  // revival of a retired slot creates no QarOS panel and keeps the name it has.
  if (!reviving) {
    applyUniqueDisplayName(key, options);
  }

  const qar_ar_core::Result added = panel_manager_->add_panel(key, widget.get(), options);
  if (!added.ok()) {
    tool.session->ack(event, false, added.message);
    return;  // source/widget destroyed here: add_panel never took ownership
  }

  // Drop any stale streamed_panels_ entry for this key AFTER add_panel
  // succeeded, so a failure above leaves bookkeeping exactly as it was.
  streamed_panels_.erase(
    std::remove_if(
      streamed_panels_.begin(), streamed_panels_.end(),
      [&key](const std::pair<std::string, QPointer<QWidget>> & entry) {
        return entry.first == key;
      }),
    streamed_panels_.end());
  streamed_panels_.emplace_back(key, QPointer<QWidget>(widget.get()));
  // Variant (b)'s own attachment: harmless no-op here (a VideoPanelWidget
  // hosts no RViz render window), kept only for uniformity with every other
  // add_panel() call site — see attachVideoRegions()'s header comment.
  attachVideoRegions(key, widget.get());

  // Replaces (and destroys) any previous entry for this key — safe: the old
  // widget was either never registered (add_panel would have failed above,
  // already handled) or has just been superseded in the manager by the new one.
  video_panels_[key] = VideoPanel{std::move(source), std::move(widget)};

  refreshStreamablePanels();
  appendLog(
    QString("AR VIDEO: %1 video panel for '%2' (%3).")
    .arg(reviving ? QString("revived the") : QString("opened a new"))
    .arg(QString::fromStdString(topic))
    .arg(QString::fromLatin1(to_string(transport))));
  tool.session->ack(event, true, reviving ? "video panel reopened" : "video panel opened");
}

void QarStreamingPanel::tickVideoPanels()
{
  if (!panel_manager_) {
    return;
  }
  bool dropped_one = false;
  for (auto it = video_panels_.begin(); it != video_panels_.end(); ) {
    const std::string key = it->first;
    VideoPanel & panel = it->second;
    if (!panel_manager_->has_panel(key)) {
      // The stream is GONE from the manager: in the default close mode that is what
      // closing the video panel does (its AR panel is closed, its sender destroyed,
      // the key forgotten). Nothing streams this widget any more, so the ROS
      // subscription + worker thread go now and the entry with them — pressing the
      // AR button again builds a fresh {source, widget} pair (see openVideoPanel),
      // and a stale entry left in the panel list would restream a widget
      // whose source is dead. (A panel that was never added, or a deferred close
      // still waiting on its own click, is still HELD by the manager and is not
      // seen here.)
      if (panel.source) {
        appendLog(
          QString("AR VIDEO: '%1' closed; stopping its ROS subscription.")
          .arg(QString::fromStdString(key)));
      }
      it = video_panels_.erase(it);
      dropped_one = true;
      continue;
    }
    if (panel_manager_->is_tombstoned(key)) {
      // Notice mode: free the ROS subscription + worker thread once, the moment the
      // panel stops being live — pressing the AR button again builds a fresh one
      // (see openVideoPanel). The widget itself is kept: the manager may still
      // reference it, and it costs nothing idle.
      if (panel.source) {
        appendLog(
          QString("AR VIDEO: '%1' closed; stopping its ROS subscription.")
          .arg(QString::fromStdString(key)));
        panel.source.reset();
      }
      ++it;
      continue;
    }
    if (panel.widget) {
      panel.widget->sync_to_frame();
    }
    ++it;
  }
  if (dropped_one) {
    // The "Video: <topic>" rows of the panel list follow video_panels_.
    refreshStreamablePanels();
  }
}

void QarStreamingPanel::collectVideoDisplays(
  rviz_common::DisplayGroup * group, QWidget * widget,
  std::vector<rviz_common::Display *> & out) const
{
  if (group == nullptr) {
    return;
  }
  for (int i = 0; i < group->numDisplays(); ++i) {
    rviz_common::Display * child = group->getDisplayAt(i);
    if (auto * nested_group = dynamic_cast<rviz_common::DisplayGroup *>(child)) {
      collectVideoDisplays(nested_group, widget, out);
    }
    QWidget * associated = child->getAssociatedWidget();
    // "equal to or a descendant of" — the same phrasing
    // QtPanelStreamManager::set_panel_region_source uses for what it will
    // match against the streamed widget at capture time.
    if (associated != nullptr && (associated == widget || widget->isAncestorOf(associated)) &&
      is_video_display_class(child->getClassId().toStdString()))
    {
      out.push_back(child);
    }
  }
}

namespace
{
/// Every Image/Camera display under @p group, however deeply nested — unlike
/// QarStreamingPanel::collectVideoDisplays, with no widget to be inside of.
void collect_all_video_displays(
  rviz_common::DisplayGroup * group, std::vector<rviz_common::Display *> & out)
{
  if (group == nullptr) {
    return;
  }
  for (int i = 0; i < group->numDisplays(); ++i) {
    rviz_common::Display * child = group->getDisplayAt(i);
    if (child == nullptr) {
      continue;
    }
    if (auto * nested_group = dynamic_cast<rviz_common::DisplayGroup *>(child)) {
      collect_all_video_displays(nested_group, out);
    }
    if (is_video_display_class(child->getClassId().toStdString())) {
      out.push_back(child);
    }
  }
}
}  // namespace

QString QarStreamingPanel::videoDisplayHealthField() const
{
  rviz_common::DisplayContext * context = getDisplayContext();
  if (context == nullptr) {
    return QString();
  }
  std::vector<rviz_common::Display *> displays;
  collect_all_video_displays(context->getRootDisplayGroup(), displays);
  if (displays.empty()) {
    return QString();
  }

  // RViz's own node: the publisher count it reports is RViz's view of the graph,
  // which is what the Image display's subscription is matched against.
  rclcpp::Node::SharedPtr node = ros_node_;
  if (!node) {
    if (auto abstraction = context->getRosNodeAbstraction().lock()) {
      node = abstraction->get_raw_node();
    }
  }

  QStringList fields;
  for (rviz_common::Display * display : displays) {
    // subProp() on a missing row returns an inert property, so the chain is safe.
    // The Image display's topic row is "Topic", the Camera display's "Image Topic".
    QString topic = display->subProp("Topic")->getValue().toString();
    if (topic.isEmpty()) {
      topic = display->subProp("Image Topic")->getValue().toString();
    }
    std::size_t publishers = 0;
    if (node && !topic.isEmpty()) {
      try {
        publishers = node->get_publishers_info_by_topic(topic.toStdString()).size();
      } catch (const std::exception &) {
        // An invalid topic name has no publishers; the display's own row says why.
      }
    }
    // The Status list renames itself "Status: Ok" / "Status: Warn" / "Status: Error"
    // (StatusList::updateLabel), so subProp("Status") would never find it: take the
    // child whose name starts with "Status", then its "Topic" row.
    QString status;
    for (int i = 0; i < display->numChildren(); ++i) {
      rviz_common::properties::Property * child = display->childAt(i);
      if (child != nullptr && child->getName().startsWith(QStringLiteral("Status"))) {
        status = child->subProp("Topic")->getValue().toString();
        break;
      }
    }
    fields << QString::fromStdString(
      format_video_display_health(
        display->getName().toStdString(), topic.toStdString(), publishers,
        status.toStdString(), display->isEnabled()));
  }
  return fields.join(' ');
}

void QarStreamingPanel::attachVideoRegions(const std::string & manager_key, QWidget * widget)
{
  if (!panel_manager_ || widget == nullptr || getDisplayContext() == nullptr) {
    return;
  }
  if (!ros_node_) {
    auto ros_abstraction = getDisplayContext()->getRosNodeAbstraction().lock();
    if (ros_abstraction) {
      ros_node_ = ros_abstraction->get_raw_node();
    }
  }
  if (!ros_node_) {
    // No ROS node yet to subscribe with. Not fatal: the next successful
    // add_panel() for this dock ("Stop streaming", then "Stream panel to AR") retries.
    return;
  }

  std::vector<rviz_common::Display *> found;
  collectVideoDisplays(getDisplayContext()->getRootDisplayGroup(), widget, found);
  if (found.empty()) {
    return;  // the common case: an ordinary dock with no RViz render window in it
  }

  std::vector<VideoRegionAttachment> attachments;
  for (rviz_common::Display * display : found) {
    QWidget * region = display->getAssociatedWidget();
    const std::string topic = display->subProp("Topic")->getValue().toString().toStdString();
    if (topic.empty()) {
      appendLog(
        QString("AR PANEL: '%1' has no Topic set — its render window will show blank in AR "
          "until one is.").arg(display->getName()));
      // Tracked with no source: maintainVideoRegions() subscribes the moment
      // the operator sets a Topic, with no need to re-stream the dock.
      attachments.push_back(
        VideoRegionAttachment{
          QPointer<rviz_common::Display>(display), QPointer<QWidget>(region), std::string(),
          nullptr});
      continue;
    }

    std::vector<std::string> advertised_types;
    const auto topic_types = ros_node_->get_topic_names_and_types();
    const auto found_topic = topic_types.find(topic);
    if (found_topic != topic_types.end()) {
      advertised_types = found_topic->second;
    }
    const ImageTransport transport = choose_image_transport(topic, advertised_types);

    auto source = acquireFrameSource(topic, transport);
    panel_manager_->set_panel_region_source(manager_key, region, source->frames());

    const std::string caveat = video_display_class_caveat(display->getClassId().toStdString());
    appendLog(
      QString("AR PANEL: '%1' is an RViz %2 display — its render window cannot be captured, "
        "so '%3' is streamed into the AR panel directly.%4")
      .arg(display->getName())
      .arg(display->getClassId())
      .arg(QString::fromStdString(topic))
      .arg(caveat.empty() ? QString() : QString(" ") + QString::fromStdString(caveat)));

    attachments.push_back(
      VideoRegionAttachment{
        QPointer<rviz_common::Display>(display), QPointer<QWidget>(region), topic,
        std::move(source)});
  }
  // Overwrites any previous entry for manager_key: the natural full-rebuild a
  // first stream and a revive both want, since either way add_panel() (the
  // caller) just succeeded — see this method's header comment.
  video_regions_[manager_key] = std::move(attachments);
}

void QarStreamingPanel::maintainVideoRegions()
{
  if (video_regions_.empty() || !panel_manager_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (video_regions_last_maintained_.time_since_epoch().count() != 0 &&
    now - video_regions_last_maintained_ < std::chrono::milliseconds(500))
  {
    return;
  }
  video_regions_last_maintained_ = now;

  for (auto key_it = video_regions_.begin(); key_it != video_regions_.end(); ) {
    const std::string key = key_it->first;
    if (!panel_manager_->has_panel(key)) {
      // Panel gone from the manager some other way — nothing left to maintain.
      key_it = video_regions_.erase(key_it);
      continue;
    }

    auto & attachments = key_it->second;

    if (panel_manager_->is_tombstoned(key)) {
      // Free the ROS subscriptions while the panel shows the closed notice.
      // The manager's own contract (qt_panel_stream_manager.hpp
      // set_panel_region_source) is that a tombstoned stream's regions simply
      // stop matching and match again on revive, so the registration itself
      // is left alone here. Entries are KEPT (not erased) so the topic/
      // display bookkeeping survives to the next "Stream panel to AR".
      for (auto & attachment : attachments) {
        if (attachment.source) {
          appendLog(
            QString("AR PANEL: '%1' closed; stopping its AR video region's ROS subscription.")
            .arg(attachment.display ? attachment.display->getName() : QString("(display gone)")));
          attachment.source.reset();
        }
      }
      ++key_it;
      continue;
    }

    // Drop any attachment whose display (or its render window) is gone. Only
    // a clear-everything-then-re-add-the-survivors path exists for one key —
    // clear_panel_region_sources has no single-region form.
    bool lost_one = false;
    for (auto att_it = attachments.begin(); att_it != attachments.end(); ) {
      if (!att_it->display || !att_it->region) {
        att_it = attachments.erase(att_it);
        lost_one = true;
      } else {
        ++att_it;
      }
    }
    if (lost_one) {
      panel_manager_->clear_panel_region_sources(key);
      for (auto & attachment : attachments) {
        if (attachment.source) {
          panel_manager_->set_panel_region_source(
            key, attachment.region.data(), attachment.source->frames());
        }
      }
    }

    // Re-subscribe anything whose Topic property changed, was just revived
    // (source dropped by an earlier tombstoned pass above), or newly has a
    // topic where it had none before.
    for (auto & attachment : attachments) {
      const std::string current_topic =
        attachment.display->subProp("Topic")->getValue().toString().toStdString();
      if (current_topic.empty()) {
        if (attachment.source) {
          appendLog(
            QString("AR PANEL: '%1' Topic property was cleared; its AR video region stopped.")
            .arg(attachment.display->getName()));
          attachment.source.reset();
        }
        attachment.topic.clear();
        continue;
      }
      if (attachment.source && current_topic == attachment.topic) {
        continue;  // unchanged, already subscribed
      }
      if (!ros_node_) {
        continue;  // try again next sweep
      }
      std::vector<std::string> advertised_types;
      const auto topic_types = ros_node_->get_topic_names_and_types();
      const auto found_topic = topic_types.find(current_topic);
      if (found_topic != topic_types.end()) {
        advertised_types = found_topic->second;
      }
      const ImageTransport transport = choose_image_transport(current_topic, advertised_types);
      const bool topic_changed = !attachment.topic.empty() && attachment.topic != current_topic;
      attachment.source = acquireFrameSource(current_topic, transport);
      attachment.topic = current_topic;
      panel_manager_->set_panel_region_source(
        key, attachment.region.data(), attachment.source->frames());
      appendLog(
        QString("AR PANEL: '%1' AR video region %2 '%3'.")
        .arg(attachment.display->getName())
        .arg(topic_changed ? QString("re-subscribed to") : QString("subscribed to"))
        .arg(QString::fromStdString(current_topic)));
    }

    if (attachments.empty()) {
      key_it = video_regions_.erase(key_it);
    } else {
      ++key_it;
    }
  }
}

void QarStreamingPanel::logVideoDiagnostics()
{
  if (video_panels_.empty() && video_regions_.empty()) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (video_diagnostics_last_logged_.time_since_epoch().count() != 0 &&
    now - video_diagnostics_last_logged_ < std::chrono::milliseconds(2000))
  {
    return;
  }
  video_diagnostics_last_logged_ = now;

  if (!panel_manager_) {
    return;
  }
  const qar_ar_core::PanelDiagnosticsSnapshot snapshot = panel_manager_->diagnostics();
  auto find_diag =
    [&snapshot](const std::string & key) -> const qar_ar_core::PanelStreamDiagnostics *
    {
      for (const auto & panel : snapshot.panels) {
        if (panel.key == key) {
          return &panel;
        }
      }
      return nullptr;
    };

  // Smoothness vs. occlusion, read off the same line: the interval between the
  // frames actually submitted since the previous line (max and average, ms), and
  // whether RViz's window is the foreground one (fg=1) or something — the QarOS
  // Visualizer — has taken over the screen (fg=0; '?' when it cannot be told).
  const char * fg_text = "?";
  {
    QWidget * top = window();
    QWindow * handle = top != nullptr ? top->windowHandle() : nullptr;
    switch (foreground_state(
        handle != nullptr ? static_cast<std::uintptr_t>(handle->winId()) : 0))
    {
      case ForegroundState::kForeground: fg_text = "1"; break;
      case ForegroundState::kBackground: fg_text = "0"; break;
      case ForegroundState::kUnknown: break;
    }
  }
  const auto pacing_fields =
    [fg_text](const qar_ar_core::PanelStreamDiagnostics * d) -> QString
    {
      const QString max_gap = d ? QString::number(d->submit_gap_max_ms, 'f', 1) : QStringLiteral("-");
      const QString avg_gap = d
        ? QString::number(
          d->submit_gap_count > 0 ? d->submit_gap_sum_ms / static_cast<double>(d->submit_gap_count)
                                  : 0.0, 'f', 1)
        : QStringLiteral("-");
      return QString("submit_gap_max_ms=%1 submit_gap_avg_ms=%2 fg=%3")
             .arg(max_gap, avg_gap, QString::fromLatin1(fg_text));
    };

  // Variant (a): one stand-alone AR video panel per topic.
  for (const auto & [key, panel] : video_panels_) {
    if (!panel.source) {
      continue;
    }
    const auto * diag = find_diag(key);
    // Two short lines rather than one with a dozen %N placeholders — safer
    // than relying on QString::arg's multi-digit placeholder parsing.
    appendLog(
      QString("AR VIDEO: '%1' msgs=%2 decoded=%3 dropped=%4 mailbox_pub=%5 avg_decode_ms=%6")
      .arg(QString::fromStdString(key))
      .arg(panel.source->received())
      .arg(panel.source->decoded())
      .arg(panel.source->dropped())
      .arg(panel.source->frames()->sequence())
      .arg(panel.source->avg_decode_ms(), 0, 'f', 1));
    appendLog(
      QString("AR VIDEO: '%1' tick: captures=%2 unchanged=%3 not_ready=%4 submitted=%5 "
        "avg_capture_ms=%6 %7")
      .arg(QString::fromStdString(key))
      .arg(diag ? QString::number(diag->frames_captured) : QStringLiteral("-"))
      .arg(diag ? QString::number(diag->frames_skipped_unchanged) : QStringLiteral("-"))
      .arg(diag ? QString::number(diag->frames_skipped_not_ready) : QStringLiteral("-"))
      .arg(diag ? QString::number(diag->frames_submitted) : QStringLiteral("-"))
      .arg(diag ? QString::number(diag->avg_capture_ms, 'f', 1) : QStringLiteral("-"))
      .arg(pacing_fields(diag)));
  }

  // Variant (b): every region source, grouped by the dock (manager_key) whose
  // capture/submit counters they share.
  for (const auto & [key, attachments] : video_regions_) {
    const auto * diag = find_diag(key);
    for (const auto & attachment : attachments) {
      if (!attachment.source) {
        continue;
      }
      appendLog(
        QString("AR VIDEO: '%1' region '%2' msgs=%3 decoded=%4 dropped=%5 mailbox_pub=%6 "
          "avg_decode_ms=%7")
        .arg(QString::fromStdString(key))
        .arg(QString::fromStdString(attachment.topic))
        .arg(attachment.source->received())
        .arg(attachment.source->decoded())
        .arg(attachment.source->dropped())
        .arg(attachment.source->frames()->sequence())
        .arg(attachment.source->avg_decode_ms(), 0, 'f', 1));
      appendLog(
        QString("AR VIDEO: '%1' region '%2' dock tick: captures=%3 unchanged=%4 not_ready=%5 "
          "submitted=%6 avg_capture_ms=%7 %8")
        .arg(QString::fromStdString(key))
        .arg(QString::fromStdString(attachment.topic))
        .arg(diag ? QString::number(diag->frames_captured) : QStringLiteral("-"))
        .arg(diag ? QString::number(diag->frames_skipped_unchanged) : QStringLiteral("-"))
        .arg(diag ? QString::number(diag->frames_skipped_not_ready) : QStringLiteral("-"))
        .arg(diag ? QString::number(diag->frames_submitted) : QStringLiteral("-"))
        .arg(diag ? QString::number(diag->avg_capture_ms, 'f', 1) : QStringLiteral("-"))
        .arg(pacing_fields(diag)));
    }
  }

  // The gap statistics describe the period since the previous line.
  panel_manager_->reset_submit_gap_windows();
}

void QarStreamingPanel::on_message(
  qar_ros_ui::DiagnosticSeverity severity,
  const std::string & category,
  const std::string & message)
{
  const QString line = QString("[ros_ui:%1:%2] %3")
    .arg(QString::fromLatin1(qar_ros_ui::to_string(severity)),
      QString::fromStdString(category), QString::fromStdString(message));
  // In RViz these arrive on the GUI thread (it is the executor thread), but the
  // library makes no such promise to its hosts, so marshal like the Ogre sink.
  if (QThread::currentThread() == this->thread()) {
    appendLog(line);
  } else {
    QMetaObject::invokeMethod(
      this, [this, line]() { appendLog(line); }, Qt::QueuedConnection);
  }
}

// ---------------------------------------------------------------------------
// Spatial AR interaction
// ---------------------------------------------------------------------------

void QarStreamingPanel::submitSpatialSample(const qar_ar_core::PanelPointerSample & sample)
{
  // Queue only. The producer may be the QarOS gesture callback thread; every
  // decision happens on the Qt thread when the render tick drains this, which
  // is the same discipline the panel-input seam uses.
  spatial_input_.push(sample);
}

void QarStreamingPanel::startSpatialGestureSource()
{
  // The wire from QarOS to the spatial picker behind submitSpatialSample():
  // app-volume CLICK and HOVER gestures, converted by the seam's own
  // pointer_sample_from_gesture_event, queued on the QarOS callback thread and
  // drained below on the Qt thread.
  //
  // The subscription is per app volume, and the session's volume is created by
  // the render factory at connect. Without one (a session configured with
  // create_app_volume off) there is simply nothing to subscribe to, and the
  // picker keeps working off whatever else feeds it.
  spatial_gesture_source_.reset();
  spatial_subscription_attempted_ = true;
  if (!qar_factory_) {
    spatial_subscription_note_ = "no QarOS session";
    return;
  }
  QarAppVolumeId volume{};
  if (!qar_factory_->app_volume(volume)) {
    spatial_subscription_note_ = "no app volume";
    appendLog("SPATIAL: no app volume in this session — AR pointer gestures are not subscribed.");
    return;
  }
  auto source =
    std::make_unique<qar_qar_adapter::QarCApiSpatialGestureSource>(qar_factory_->session());
  if (!source->subscribe(volume)) {
    spatial_subscription_note_ = QString::fromStdString(source->last_error());
    appendLog(QString("SPATIAL: could not subscribe to AR pointer gestures — %1")
      .arg(QString::fromStdString(source->last_error())));
    return;
  }
  spatial_gesture_source_ = std::move(source);
  // The FALLBACK aiming ray. Since the drop after the minimum drop the gesture
  // carries its own aim -- its start point (the hand) through its action point -- and that
  // is what a plane or fixed-depth target normally intersects. This wire is for
  // the one case the header still allows: the runtime intersected nothing, so
  // it hands the start point back as the action point, and without a ray
  // "Ground plane (z = 0)" cannot be an intersection at all and degrades to
  // projecting the reported point straight down.
  spatial_gesture_source_->set_pointing_ray_slot(qar_factory_->pointing_ray_slot());
  spatial_subscription_note_.clear();
  appendLog("SPATIAL: subscribed to the session's AR pointer gestures (click + hover).");
  // WHERE QarOS is actually displaying our scene, read back from the volume
  // rather than assumed from what we passed at create. A DIAGNOSTIC, not part
  // of the pick map (see refreshScenePlacement): logged once here so the first
  // line of a spatial log is the placement itself, and refreshed every tick
  // below, because the operator's own grab/pinch gestures change it.
  refreshScenePlacement();
  appendLog(QString("SPATIAL: scene placement %1")
    .arg(QString::fromStdString(
      qar_ar_core::describe_scene_placement(scene_placement_))));
  appendLog(
    "SPATIAL: that placement is REPORTED, not applied. Gesture points, the view pose and "
    "the hand joints are reported in APP space (eye-to-fingertip measures "
    "0.500 m at app_scale=1.0 and 5.002 m at app_scale=0.1, so those numbers are app units, "
    "not metres), so a pick takes the room->ROS basis change and nothing else.");
  if (!scene_placement_.known) {
    appendLog(
      "SPATIAL: the app volume would not report its placement — the line above is an "
      "assumption, not a measurement. Picks are unaffected: they never use it.");
  }
  appendLog(
    "SPATIAL: target 'Action Point (QarOS scene intersect)' is the default and needs no "
    "ray - it publishes the runtime's own scene intersection verbatim.");
  appendLog(
    "SPATIAL: aiming ray = the SDK's own, start point -> action point (the drop after the "
    "minimum drop says: "
    "'from the start point to the action point is the ray the user is pointing along'). "
    "It falls back to OUR straight eye-through-hand line, from qar_render_sender_last_hands "
    "+ the frame's eye pose, only when the runtime intersected nothing and the two points "
    "coincide. The gesture trace prints ray_src=SDK(...) or ray_src=FALLBACK(...) on every "
    "line, and 'aim_off' is that fallback's own error against the runtime's action point.");
}

void QarStreamingPanel::refreshScenePlacement()
{
  // The placement is NOT ours after the volume is created: our own gesture
  // configuration hands the runtime grab-to-move (-> app_pose) and
  // pinch-to-zoom (-> app_scale), and the QarOS session publishes
  // `appVolume/appPose/updated` / `appScale/updated` for our volume as the
  // operator uses them. So it is re-read here, on the Qt thread, once per tick
  // — three getters — and recorded in scene_placement_ as a DIAGNOSTIC. Read
  // the block below before wiring it into anything.
  if (!qar_factory_) {
    return;
  }
  qar_ar_core::ScenePlacement placement;
  if (!qar_factory_->scene_placement(placement)) {
    return;  // keep whatever we had; a failed read is not "identity".
  }
  const qar_ar_core::ScenePlacement & current = scene_placement_;
  const bool changed = !current.known ||
    std::abs(current.app_scale - placement.app_scale) > 1e-4f ||
    std::abs(current.volume_pose.position.x - placement.volume_pose.position.x) > 1e-3f ||
    std::abs(current.volume_pose.position.y - placement.volume_pose.position.y) > 1e-3f ||
    std::abs(current.volume_pose.position.z - placement.volume_pose.position.z) > 1e-3f ||
    std::abs(current.app_pose.position.x - placement.app_pose.position.x) > 1e-3f ||
    std::abs(current.app_pose.position.y - placement.app_pose.position.y) > 1e-3f ||
    std::abs(current.app_pose.position.z - placement.app_pose.position.z) > 1e-3f;
  scene_placement_ = placement;
  // DELIBERATELY NOT `transform.placement = placement`.
  //
  // A gesture's `action_point` does NOT arrive in ROOM metres, so the volume's
  // scale and offset must not be divided back out of it. QarOS reports gesture
  // action points, the render view pose and the hand joints in APP space — our
  // own scene's frame — which the arm-length invariant settles: the same
  // operator's eye-to-fingertip distance measures 0.500 m at app_scale=1.0 and
  // 5.002 m at app_scale=0.1, i.e. exactly 1/app_scale, because those numbers
  // are app units and not metres. Undoing the placement on top of that puts
  // picks metres away from the point the operator aims at (a median 14 m,
  // against 0.74 m for the basis change alone; qar_ar_core's spatial_frames.hpp,
  // "The SDK reports gesture points in APP content space", and the tests named
  // there).
  //
  // So the placement is READ and LOGGED — it is the diagnostic that shows this,
  // it is the first thing a spatial investigation should see, and it stays the
  // right model for anything genuinely stated in room metres — but it does NOT
  // enter the pick transform. `spatial_pick.cpp` derives the ray origin and
  // direction through the same RoomToRosTransform, so leaving it identity is
  // also what keeps the straight-ray plane/depth targets correct: the eye and
  // the hand joints they are built from are in app space too.
  //
  // Belt and braces: if anything else ever sets a placement on the picker, take
  // it straight back out rather than let it reach a published pick.
  if (!qar_ar_core::scene_placement_is_identity(spatial_.transform().placement)) {
    qar_ar_core::RoomToRosTransform transform = spatial_.transform();
    transform.placement = qar_ar_core::ScenePlacement{};
    spatial_.set_transform(transform);
  }
  if (changed) {
    // Only when it MOVES, so a steady session does not drown the log — but
    // every move is recorded, because "the operator grabbed or pinched the
    // scene at this moment" is the context a later pick reading needs, even
    // though the pick maths itself does not depend on it.
    appendLog(QString("SPATIAL: scene placement changed — %1")
      .arg(QString::fromStdString(
        qar_ar_core::describe_scene_placement(placement))));
  }
}

void QarStreamingPanel::tickSpatialInteraction()
{
  // Hand whatever the runtime reported since the last tick to the same queue the
  // rest of the panel pushes into, so everything downstream sees one ordered
  // stream and one thread. Draining here rather than in the callback is what
  // keeps the QarOS thread out of the controller entirely.
  if (spatial_gesture_source_) {
    // Before the drain, not after: every sample below must be resolved against
    // the placement the operator is looking at right now.
    refreshScenePlacement();
    spatial_drain_buffer_.clear();
    spatial_gesture_source_->drain(spatial_drain_buffer_);
    for (const auto & sample : spatial_drain_buffer_) {
      // Rate-limited, and on the Qt thread: the first 20 samples plus one in 50
      // thereafter, with BOTH the raw room point and the ROS-frame point the
      // transform produced. A pick that lands in the wrong place is then a
      // reading rather than an investigation — the transform is the one
      // step between the runtime's numbers and the published message that
      // nothing else in the log shows.
      ++spatial_samples_drained_;
      if (spatial_samples_drained_ <= 20 || (spatial_samples_drained_ % 50) == 0) {
        const auto pointer =
          qar_ar_core::spatial_pointer_from_sample(sample, spatial_.transform());
        // The whole chain in one line, so a log is a READING and not an
        // investigation:
        //   room=   the raw action point the runtime gave us (metres, +Y up)
        //   ros=    the same point after the room -> ROS frame change
        //   ray=    the aiming ray, only when the sample actually carried one
        //   pick=   what the armed target resolved it to -- the point that
        //           would be published -- and HOW it got there:
        //             QarOS action point  the runtime's own curved-ray scene
        //                                 intersection, published verbatim (the default)
        //             ray/plane           the aiming ray intersected with the target.
        //                                 Normally the SDK's own start->action
        //                                 line; OUR straight reconstruction, a
        //                                 local approximation, when the runtime
        //                                 intersected nothing. The [qar-spatial]
        //                                 trace's ray_src= says which.
        //             projected           a bare position flattened onto the target
        //   commits= whether this gesture may commit at all (tap yes, hover no)
        const qar_ar_core::SpatialPoint resolved =
          qar_ar_core::resolve_pick_point(spatial_.target(), pointer);
        // ONE phrase for how the answer was reached -- see PickMethod. It is
        // the field to read first: "QarOS action point" is the runtime's own
        // intersection taken at face value (the authoritative answer),
        // "ray/plane" and "ray/depth" intersect the aiming ray with the armed
        // target, and "projected" is the degraded no-ray fallback. Note that
        // from_scene_hit is FALSE when the runtime intersected nothing and
        // handed its start point back as the action point -- that point is the
        // operator's hand, and the phrase below deliberately does not claim it
        // is an intersection.
        QString how = QString::fromUtf8(qar_ar_core::to_string(resolved.method));
        if (resolved.from_scene_hit && !resolved.from_ray) {
          how += " (the runtime's own curved-ray scene intersection)";
        }
        // The ray and the plane it is being intersected with, both in the ROS
        // frame the pick is published in, so a log can be checked with
        // arithmetic instead of an instrumented run.
        QString ray = " ray=none (no hand ray this sample)";
        if (pointer.has_ray) {
          ray = QString(" ray=(%1, %2, %3)->(%4, %5, %6)")
            .arg(pointer.ray_origin.x, 0, 'f', 3).arg(pointer.ray_origin.y, 0, 'f', 3)
            .arg(pointer.ray_origin.z, 0, 'f', 3).arg(pointer.ray_direction.x, 0, 'f', 3)
            .arg(pointer.ray_direction.y, 0, 'f', 3).arg(pointer.ray_direction.z, 0, 'f', 3);
        }
        const auto & pick_target = spatial_.target();
        QString plane;
        if (pick_target.kind == qar_ar_core::PickTargetKind::GroundPlane ||
          pick_target.kind == qar_ar_core::PickTargetKind::Plane)
        {
          plane = QString(" plane=point(%1, %2, %3) normal(%4, %5, %6)")
            .arg(pick_target.plane_point.x, 0, 'f', 3)
            .arg(pick_target.plane_point.y, 0, 'f', 3)
            .arg(pick_target.plane_point.z, 0, 'f', 3)
            .arg(pick_target.plane_normal.x, 0, 'f', 3)
            .arg(pick_target.plane_normal.y, 0, 'f', 3)
            .arg(pick_target.plane_normal.z, 0, 'f', 3);
        } else if (pick_target.kind == qar_ar_core::PickTargetKind::Depth) {
          plane = QString(" depth=%1 m").arg(pick_target.depth_m, 0, 'f', 3);
        }
        appendLog(
          QString("SPATIAL: sample #%1 %2/%3 commits=%4 room=(%5, %6, %7) -> ros=(%8, %9, %10)%11")
          .arg(static_cast<qulonglong>(spatial_samples_drained_))
          .arg(QString::fromUtf8(qar_ar_core::to_string(sample.gesture)),
            QString::fromUtf8(qar_ar_core::to_string(sample.phase)),
            qar_ar_core::gesture_commits_pick(sample.gesture) ? "yes" : "no (preview only)")
          .arg(sample.room_point.x, 0, 'f', 3)
          .arg(sample.room_point.y, 0, 'f', 3)
          .arg(sample.room_point.z, 0, 'f', 3)
          .arg(pointer.point.x, 0, 'f', 3)
          .arg(pointer.point.y, 0, 'f', 3)
          .arg(pointer.point.z, 0, 'f', 3)
          .arg(sample.has_room_point ? QString() : QString(" (no room point)")));
        // NOT the transform that took `room=` to `ros=` on the line above —
        // that is the basis change alone. This is where QarOS was displaying
        // our scene at the moment of the sample, printed alongside it because
        // it CHANGES mid-session (grab/pinch) and because anyone doubting the
        // pick maths wants the two side by side: that comparison is what shows
        // the placement does not belong in the pick transform.
        appendLog(QString("SPATIAL: ... scene displayed at (not applied to the pick) %1")
          .arg(QString::fromStdString(
            qar_ar_core::describe_scene_placement(scene_placement_))));
        // The trailing clause says what the number before it MEANS, which is
        // different per method: travel along the ray for a real intersection,
        // and how far off the plane the answer would have been for the
        // no-ray projection -- i.e. the error the projection just removed.
        QString detail;
        if (!resolved.valid) {
          detail = QString(" - %1").arg(QString::fromStdString(resolved.error));
        } else if (resolved.method == qar_ar_core::PickMethod::RayPlane) {
          detail = QString(", %1 m along OUR STRAIGHT ray to the plane (local "
            "approximation: QarOS aims along a curve)").arg(resolved.distance_m, 0, 'f', 3);
        } else if (resolved.method == qar_ar_core::PickMethod::RayDepth) {
          detail = QString(", %1 m along OUR STRAIGHT ray (local approximation: QarOS aims "
            "along a curve)").arg(resolved.distance_m, 0, 'f', 3);
        } else if (resolved.method == qar_ar_core::PickMethod::ProjectedPoint) {
          detail = QString(", NO RAY: the action point sat %1 m off the plane and was "
            "projected onto it").arg(resolved.distance_m, 0, 'f', 3);
        } else {
          detail = ", QarOS's own scene intersection, published verbatim";
        }
        appendLog(
          QString("SPATIAL: sample #%1%2 pick=%3 via %4 on '%5'%6")
          .arg(static_cast<qulonglong>(spatial_samples_drained_))
          .arg(ray)
          .arg(resolved.valid
            ? QString("(%1, %2, %3)").arg(resolved.position.x, 0, 'f', 3)
              .arg(resolved.position.y, 0, 'f', 3).arg(resolved.position.z, 0, 'f', 3)
            : QString("none"))
          .arg(how)
          .arg(QString::fromStdString(resolved.target_name))
          .arg(detail));
      }
      submitSpatialSample(sample);
    }
  }

  if (spatial_input_.pending() == 0 && !spatial_.pending_ready()) {
    return;  // nothing arrived and nothing is waiting to go out
  }
  spatial_drain_buffer_.clear();
  spatial_input_.drain(spatial_drain_buffer_);

  bool changed = false;
  for (const auto & sample : spatial_drain_buffer_) {
    changed = spatial_.handle_sample(sample) || changed;
    if (spatial_.pending_ready()) {
      // "Publish without confirming" is on: send it and keep draining, so a
      // burst of samples cannot pile up behind one pick.
      const SpatialPublishRequest request = spatial_.confirm();
      const QString error = publishSpatialPick(request);
      if (!error.isEmpty()) {
        setLastError(error);
      }
      changed = true;
    }
  }
  if (changed) {
    refreshSpatialUi();
  }
}

void QarStreamingPanel::refreshSpatialUi()
{
  if (spatial_status_label_ == nullptr) {
    return;
  }
  const SpatialInteractionStatus status = spatial_.status();
  QString text = QString("Spatial: %1").arg(QString::fromUtf8(to_string(status.mode)));
  if (!status.summary.empty()) {
    text += QString(" - %1").arg(QString::fromStdString(status.summary));
  } else if (status.mode != SpatialMode::Off) {
    text += " - waiting for an AR pointer";
  }
  if (!status.error.empty()) {
    text += QString(" [%1]").arg(QString::fromStdString(status.error));
  }
  text += "\n" + spatialCountersLine();
  spatial_status_label_->setText(text);

  const bool armed = status.mode != SpatialMode::Off;
  spatial_confirm_button_->setEnabled(status.has_pending);
  spatial_cancel_button_->setEnabled(armed);
  spatial_close_polygon_button_->setEnabled(
    armed && status.mode == SpatialMode::Area &&
    spatial_.area_shape() == qar_ar_core::AreaShape::Polygon);
  spatial_target_combo_->setEnabled(armed);
  spatial_frame_edit_->setEnabled(armed);
}

QString QarStreamingPanel::spatialCountersLine() const
{
  // Subscription state first: a zero rx count means one of two completely
  // different things depending on whether we are even subscribed, and without
  // the subscription state the two cannot be told apart.
  QString text;
  if (spatial_gesture_source_) {
    const auto gs = spatial_gesture_source_->stats();
    text = QString("AR gestures: rx=%1 (click=%2 hover=%3 other=%4) queued=%5 dropped=%6")
      .arg(static_cast<qulonglong>(gs.events_received))
      .arg(static_cast<qulonglong>(gs.events_click))
      .arg(static_cast<qulonglong>(gs.events_hover))
      .arg(static_cast<qulonglong>(gs.events_other_kind))
      .arg(static_cast<qulonglong>(gs.events_queued))
      .arg(static_cast<qulonglong>(gs.events_unmapped));
    if (gs.events_no_action_point > 0) {
      text += QString(" no_point=%1").arg(static_cast<qulonglong>(gs.events_no_action_point));
    }
    if (gs.events_other_volume > 0) {
      text += QString(" other_volume=%1").arg(static_cast<qulonglong>(gs.events_other_volume));
    }
    if (!gs.last_drop_reason.empty()) {
      text += QString(" last=%1").arg(QString::fromStdString(gs.last_drop_reason));
    }
  } else if (spatial_subscription_attempted_) {
    text = QString("AR gestures: NOT SUBSCRIBED (%1)")
      .arg(spatial_subscription_note_.isEmpty() ? QString("reason not recorded")
                                                : spatial_subscription_note_);
  } else {
    text = "AR gestures: not subscribed yet (connect to QarOS first)";
  }

  const auto & in = spatial_.input_stats();
  text += QString(" | picker: seen=%1 used=%2 picks=%3 ignored=%4")
    .arg(static_cast<qulonglong>(in.samples_seen))
    .arg(static_cast<qulonglong>(in.samples_accepted))
    .arg(static_cast<qulonglong>(in.picks_committed))
    .arg(static_cast<qulonglong>(
      in.ignored_mode_off + in.ignored_not_spatial + in.ignored_pending +
      in.ignored_by_session));
  // Its own number, not folded into ignored= above (of which it is a part): a
  // click the runtime resolved to NOTHING is the operator aiming at empty space,
  // and it must read as that rather than as a silent drop.
  if (in.refused_no_intersection > 0) {
    text += QString(" clicks-on-nothing=%1")
      .arg(static_cast<qulonglong>(in.refused_no_intersection));
  }
  if (!in.last_drop_reason.empty()) {
    text += QString(" [%1]").arg(QString::fromStdString(in.last_drop_reason));
  }
  return text;
}

QString QarStreamingPanel::publishSpatialPick(const SpatialPublishRequest & request)
{
  if (!request.ok) {
    return QString::fromStdString(
      request.error.empty() ? std::string("nothing to publish") : request.error);
  }
  const QString bridge_error = ensureRosBridges();
  if (!bridge_error.isEmpty()) {
    return bridge_error;
  }
  // One publisher, re-created only when the destination actually changes: a
  // publisher that comes and goes takes its discovery with it, and a subscriber
  // that joined late would miss the next pick.
  if (!spatial_publisher_ || spatial_publisher_topic_ != request.topic ||
    spatial_publisher_type_ != request.type_name)
  {
    qar_ros_ui::TopicPublisherOptions options;
    const qar_ros_ui::Result advertised = topic_bridge_->advertise(
      request.topic, request.type_name, options, spatial_publisher_);
    if (!advertised.ok()) {
      spatial_publisher_.reset();
      return QString("Spatial publish failed: %1").arg(QString::fromStdString(advertised.message));
    }
    spatial_publisher_topic_ = request.topic;
    spatial_publisher_type_ = request.type_name;
  }
  const qar_ros_ui::Result published = spatial_publisher_->publish(request.message);
  if (!published.ok()) {
    return QString("Spatial publish failed: %1").arg(QString::fromStdString(published.message));
  }
  appendLog(
    QString("SPATIAL: published %1 to %2")
    .arg(QString::fromStdString(request.type_name), QString::fromStdString(request.topic)));
  return QString();
}

void QarStreamingPanel::onSpatialModeChanged(int index)
{
  const int encoded = spatial_mode_combo_->itemData(index).toInt();
  const auto mode = static_cast<SpatialMode>(encoded % 100);
  const auto shape = static_cast<qar_ar_core::AreaShape>(encoded / 100);
  spatial_.set_frame_id(spatial_frame_edit_->text().trimmed().toStdString());
  spatial_.set_mode(mode);
  if (mode == SpatialMode::Area) {
    spatial_.set_area_shape(shape);
  }
  appendLog(QString("SPATIAL: mode = %1").arg(QString::fromUtf8(to_string(mode))));
  refreshSpatialUi();
}

void QarStreamingPanel::onSpatialTargetChanged(int index)
{
  const int id = spatial_target_combo_->itemData(index).toInt();
  spatial_.set_target(spatial_target_from_id(id));
  // Say WHICH of the two kinds of answer the operator just armed, because the
  // difference is not cosmetic: one is the runtime's own scene intersection,
  // published verbatim, and the other resolves the aiming ray against a
  // geometry of our choosing.
  for (const SpatialTargetOption & option : spatial_target_options()) {
    if (option.id == id) {
      appendLog(
        QString("SPATIAL: target = %1 - %2")
        .arg(QString::fromUtf8(option.label),
          option.straight_ray_approximation
            ? QString("the aiming ray intersected with a geometry of our choosing - the "
              "SDK's own start->action line where the runtime intersected something, OUR "
              "straight eye-through-hand reconstruction where it did not; a local "
              "approximation either way, and the gesture trace's ray_src= says which")
            : QString("QarOS's own scene intersection, published verbatim in the chosen "
              "frame")));
      break;
    }
  }
  refreshSpatialUi();
}

void QarStreamingPanel::onSpatialConfirm()
{
  const SpatialPublishRequest request = spatial_.confirm();
  const QString error = publishSpatialPick(request);
  if (!error.isEmpty()) {
    setLastError(error);
  }
  refreshSpatialUi();
}

void QarStreamingPanel::onSpatialCancel()
{
  spatial_.cancel();
  appendLog("SPATIAL: pick canceled");
  refreshSpatialUi();
}

void QarStreamingPanel::onSpatialClosePolygon()
{
  if (!spatial_.close_polygon()) {
    setLastError("Close polygon: need at least three vertices in polygon area mode.");
  }
  refreshSpatialUi();
}

}  // namespace qar_rviz_streaming

PLUGINLIB_EXPORT_CLASS(qar_rviz_streaming::QarStreamingPanel, rviz_common::Panel)
