// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/qar_streaming_panel.hpp"

#include "qar_rviz_streaming/ar_semantic_templates.hpp"
#include "qar_rviz_streaming/panel_display_names.hpp"
#include "qar_rviz_streaming/panel_ground_rule.hpp"
#include "qar_rviz_streaming/panel_settings.hpp"
#include "qar_rviz_streaming/stable_panel_layout.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include <QAbstractItemModel>
#include <QAbstractScrollArea>
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
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QTreeView>
#include <QVariant>
#include <QVBoxLayout>
#include <QWindow>

#include <pluginlib/class_list_macros.hpp>

#include <OgreRenderSystem.h>
#include <OgreRenderSystemCapabilities.h>
#include <OgreRoot.h>

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

/// QAR_PANEL_HIDDEN_DISPLAYS_SIZE=WxH, in logical pixels, each side clamped to 200..2000
/// (qar_qt_streaming::parse_offscreen_size). When it holds a size, the copy has that size.
/// Without it, or with a value that is not a size, the copy starts at 480x756 and is made as high
/// as its display tree needs.
constexpr const char * kHiddenDisplaysSizeEnv = "QAR_PANEL_HIDDEN_DISPLAYS_SIZE";

/// The size the hidden Displays copy starts at: the dock-shaped surface of the library (480 x
/// 720), 5 % taller.
constexpr int kHiddenDisplaysDefaultWidth = 480;
constexpr int kHiddenDisplaysDefaultHeight = 756;

/// The least and the most height the hidden Displays copy gets when it is fitted to its tree.
constexpr int kHiddenDisplaysMinFitHeight = 400;
constexpr int kHiddenDisplaysMaxFitHeight = 1400;

/// The size of the hidden Displays copy for the text of QAR_PANEL_HIDDEN_DISPLAYS_SIZE: the
/// default when @p setting is empty or not a size.
qar_qt_streaming::OffscreenSizeChoice hiddenDisplaysSize(const QString & setting)
{
  const QByteArray utf8 = setting.toUtf8();
  return qar_qt_streaming::parse_offscreen_size(
    setting.isEmpty() ? nullptr : utf8.constData(), kHiddenDisplaysDefaultWidth,
    kHiddenDisplaysDefaultHeight, kHiddenDisplaysSizeEnv);
}

/// The words for the fit of the hidden Displays copy in the log line of its creation. The copy is
/// fitted to its display tree on a member timer (advanceHiddenDisplaysFit()) before it streams,
/// unless the operator chose a size (@p size.explicitly_chosen, a well-formed value) or it has no
/// tree view to fit to.
QString hiddenDisplaysFitNote(
  const qar_qt_streaming::OffscreenSizeChoice & size, bool has_tree)
{
  if (size.explicitly_chosen) {
    return QString("size from %1").arg(QString::fromLatin1(kHiddenDisplaysSizeEnv));
  }
  if (!has_tree) {
    return QStringLiteral("not fitted: it has no tree view");
  }
  return QStringLiteral("fitting to its display tree before it streams");
}

/// The period of the member timers that take one look per period at a widget being fitted: time for
/// the layout, the rows of the views and the ranges of the bars to follow the last resize.
constexpr int kFitPeriodMs = 100;

/// The wait between a change of the display tree of the hidden Displays copy and the census it
/// earns: the changes of a burst of rows come in, and the tree lays itself out, before it is taken.
constexpr int kHiddenDisplaysCensusPeriodMs = 1000;

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

/// "WxH", the spelling of the Resolution setting and of the "PANEL SIZE:" lines.
QString sizeWord(const QSize & size)
{
  return QString("%1x%2").arg(size.width()).arg(size.height());
}

/// "true" or "false", the spelling of the bool settings.
QString boolWord(bool on)
{
  return on ? QStringLiteral("true") : QStringLiteral("false");
}

/// Where the value in force of @p key comes from, for a log line: "from QAR_PANEL_PEER_ADMISSION",
/// "from the panel settings" (the saved configuration or the Settings tab) or "the default".
QString settingSourceWords(const ResolvedSettings & settings, const char * key)
{
  const auto found = settings.find(key);
  if (found == settings.end()) {
    return QStringLiteral("the default");
  }
  switch (found->second.source) {
    case SettingSource::Environment:
      return QString("from %1").arg(QString::fromStdString(found->second.env_name));
    case SettingSource::Saved:
      return QStringLiteral("from the panel settings");
    case SettingSource::Default:
      break;
  }
  return QStringLiteral("the default");
}

/// Makes @p widget say that the environment decides its value, or stop saying it. A control
/// whose variable wins shows that value (the caller sets it), is disabled, and starts its tool
/// tip with the sentence below, ahead of the tool tip it was built with.
///
/// @p unused_because, when it is not empty, says why the control has no effect at the moment:
/// the control is disabled for that reason as well, and the sentence stands in for the tool tip
/// it was built with, after the sentence of the environment when both apply. Only the enabled
/// state and the tool tip change, so the size of the panel cannot.
void markEnvironmentControl(
  QWidget * widget, const ResolvedSetting & setting, const QString & unused_because = QString())
{
  if (widget == nullptr) {
    return;
  }
  const char * const kBaseToolTip = "qarBaseToolTip";
  if (!widget->property(kBaseToolTip).isValid()) {
    widget->setProperty(kBaseToolTip, widget->toolTip());
  }
  const QString base = widget->property(kBaseToolTip).toString();
  const bool from_environment = setting.source == SettingSource::Environment;
  const bool unused = !unused_because.isEmpty();
  widget->setEnabled(!from_environment && !unused);
  const QString rest = unused ? unused_because : base;
  if (!from_environment) {
    widget->setToolTip(rest);
    return;
  }
  const QString sentence = QString(
    "Set by the environment: %1=%2. It overrides the saved setting; unset the variable and "
    "restart RViz to choose here.")
    .arg(
      QString::fromStdString(setting.env_name),
      QString::fromStdString(setting.env_raw).trimmed());
  widget->setToolTip(rest.isEmpty() ? sentence : sentence + "\n\n" + rest);
}

/// A list box that asks its layout for half the height a QListWidget asks for by default. The AR
/// tools page holds two of them, and with the stock height they were most of that page.
class CompactList : public QListWidget
{
public:
  explicit CompactList(QWidget * parent = nullptr)
  : QListWidget(parent) {}

  QSize sizeHint() const override
  {
    const QSize stock = QListWidget::sizeHint();
    return QSize(stock.width(), stock.height() / 2);
  }
};

/// The process of the test markers: a QProcess that can be told that its process is over without
/// asking the system (see QarStreamingPanel::releaseTestMarkersProcess()). Not a QObject class
/// of its own: it adds no signal or slot.
class MarkerProcess : public QProcess
{
public:
  explicit MarkerProcess(QObject * parent = nullptr)
  : QProcess(parent) {}

  /// Sets the state to NotRunning. Protected in QProcess, which offers it to subclasses.
  void markNotRunning() { setProcessState(QProcess::NotRunning); }
};

/// The tool tips of "Invite peer": what it does while a live QarOS session exists, and what it
/// needs while none does.
constexpr const char * kInviteToolTip =
  "Invite the selected device into the running QarOS session. Available once the panel is "
  "connected to QarOS.";
constexpr const char * kInviteNeedsSessionToolTip =
  "Inviting a device needs a live QarOS session: start RViz with the live launcher and press "
  "Connect to QarOS.";

/// Why "Closed AR panels:" has no effect while hiding is on.
constexpr const char * kCloseModeUnusedToolTip =
  "Used only when \"Hide panels in AR instead of closing them\" is off: hiding parks every "
  "closed window.";

/// The two environment variables of the test markers: the python to run and the script it runs.
constexpr const char * kTestMarkersPythonEnv = "QAR_PANEL_TEST_MARKERS_PYTHON";
constexpr const char * kTestMarkersScriptEnv = "QAR_PANEL_TEST_MARKERS_SCRIPT";

/// The longest piece of the test markers' output that one log line shows, in bytes.
constexpr std::size_t kTestMarkersLineBytes = 200;

/// How long after load() the default dock size is applied: time for RViz to restore its window
/// state first.
constexpr int kDefaultDockDelayMs = 500;

/// How long the dock of this panel is held at its default height by a temporary minimum height
/// (set_dock_height()) before the minimum is taken away: time for the layout of the window to
/// settle. A dock that is fitted for its stream is looked at every kFitPeriodMs instead.
constexpr int kDockSettleMs = 300;

/// The most that a dock is taken to add around a panel (title bar and frame), in pixels. A dock
/// that has not been laid out can give a number far beyond it.
constexpr int kMaxDockChromePx = 120;

/// The dock that holds @p widget, or null.
QDockWidget * enclosingDock(QWidget * widget)
{
  for (QWidget * ancestor = widget; ancestor != nullptr; ancestor = ancestor->parentWidget()) {
    if (auto * dock = qobject_cast<QDockWidget *>(ancestor)) {
      return dock;
    }
  }
  return nullptr;
}

/// The QarOS panel display names this process has handed out. One object for the whole
/// process, so that every QarStreamingPanel of the process counts together.
PanelDisplayNames & process_panel_display_names()
{
  static PanelDisplayNames names;
  return names;
}

/// Seconds on the steady clock. The placement policy compares two of them, so the origin does
/// not matter.
double steadySeconds()
{
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
         .count();
}

/// "x, y, z" with two decimals, for the log lines of the AR control panel.
QString vector3Words(const QarVector3 & v)
{
  return QString("%1, %2, %3")
         .arg(static_cast<double>(v.x), 0, 'f', 2)
         .arg(static_cast<double>(v.y), 0, 'f', 2)
         .arg(static_cast<double>(v.z), 0, 'f', 2);
}

/// The turn about the vertical axis of an upright pose, in degrees within (-180, 180].
double yawDegrees(const QarQuaternion & q)
{
  double degrees = 2.0 * std::atan2(static_cast<double>(q.y), static_cast<double>(q.w)) *
    180.0 / 3.14159265358979323846;
  while (degrees > 180.0) {
    degrees -= 360.0;
  }
  while (degrees <= -180.0) {
    degrees += 360.0;
  }
  return degrees;
}

/// The first eight hex digits of a peer id, the way the AR control panel names a peer in logs.
QString shortPeerWords(const QarPeerId & peer)
{
  return QString::fromStdString(control_panel_instance_key(peer).substr(5, 8));
}

/// Text that came from a page, made safe for a log line: bytes 0x20 to 0x7E are kept, every other
/// byte becomes '?', at most @p max_bytes bytes are kept and "..." says that the rest was cut.
/// A frame that carries the page's token can hold any bytes, a line break included, and the log
/// is read line by line.
std::string printableText(const std::string & text, std::size_t max_bytes = 40)
{
  std::string out;
  for (std::size_t i = 0; i < text.size() && i < max_bytes; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    out.push_back((c >= 0x20 && c <= 0x7e) ? static_cast<char>(c) : '?');
  }
  if (text.size() > max_bytes) {
    out += "...";
  }
  return out;
}

/// The `RENDER:` line: the render system RViz renders with and the device behind it, read from
/// Ogre now. Without an Ogre root or a render system (a preview, a test) it says that there is
/// none yet; it never needs one and never throws.
std::string currentRenderDeviceLine()
{
  Ogre::Root * const root = Ogre::Root::getSingletonPtr();
  Ogre::RenderSystem * const system = root != nullptr ? root->getRenderSystem() : nullptr;
  if (system == nullptr) {
    return render_device_line(nullptr, nullptr);
  }
  const std::string name = system->getName();
  std::string device;
  if (const Ogre::RenderSystemCapabilities * const capabilities = system->getCapabilities()) {
    device = capabilities->getDeviceName();
  }
  return render_device_line(name.c_str(), device.c_str());
}

/// Reads a switch from the environment. Unset or blank gives @p fallback; so does a text that is
/// no switch word, and then @p note says so.
bool environmentSwitch(const char * name, bool fallback, QString & note)
{
  const QString raw = qEnvironmentVariable(name).trimmed();
  if (raw.isEmpty()) {
    return fallback;
  }
  bool value = fallback;
  if (parse_bool_word(raw.toStdString(), value)) {
    return value;
  }
  note = QString("%1='%2' is not a switch (on or off) - using %3")
    .arg(QString::fromLatin1(name), raw, fallback ? QStringLiteral("on") : QStringLiteral("off"));
  return fallback;
}

/// Where a streamed panel stands as an AR panel. A notice (a retired panel) shows no window and
/// counts as off, whatever else is true of it; so does a parked panel (its window was closed on
/// the desktop; it is hidden in AR, and starting it again shows it).
PanelArState arStateOf(const qar_ar_core::PanelStreamDiagnostics * panel)
{
  if (panel == nullptr || panel->tombstoned || panel->parked) {
    return PanelArState::Off;
  }
  if (panel->ar_panel_closed) {
    return PanelArState::ClosedInAr;
  }
  if (panel->ar_hidden) {
    return PanelArState::Hidden;
  }
  return panel->frames_submitted == 0 ? PanelArState::Starting : PanelArState::Streaming;
}

/// The diagnostics of the panel @p key in @p snapshot, or null.
const qar_ar_core::PanelStreamDiagnostics * findPanelDiagnostics(
  const qar_ar_core::PanelDiagnosticsSnapshot & snapshot, const std::string & key)
{
  for (const qar_ar_core::PanelStreamDiagnostics & panel : snapshot.panels) {
    if (panel.key == key) {
      return &panel;
    }
  }
  return nullptr;
}

}  // namespace

QarStreamingPanel::QarStreamingPanel(QWidget * parent)
: rviz_common::Panel(parent)
{
  // The semantic layouts go in ahead of the generic form, which stays as the
  // fallback for everything they do not claim.
  register_semantic_templates(panel_templates_);
  // The environment of the persisted settings is read ONCE, here, and the settings in force are
  // resolved from it before the controls are built, so each control is built with the value it
  // is going to show. load() and the edits of the controls re-resolve from this snapshot.
  snapshotEnvironment();
  resolveSettings();
  buildUi();
  applySettingsToUi();

  // The picker MUST use the same room -> ROS frame change the camera uses,
  // read from the same environment qar_pose_conversion.cpp reads. Anything else
  // puts the pick where the AR user was not pointing, silently.
  spatial_.set_transform(qar_ar_core::room_to_ros_from_environment());
  refreshSpatialUi();
  appendLog("Qar streaming panel constructed.");
  reportSettings();

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
  // The test-marker process is let go first: none of its signals reaches this panel any more,
  // and one that still runs is killed without waiting for it.
  releaseTestMarkersProcess();
  for (QTimer * timer : {default_dock_timer_, default_dock_release_timer_, dock_start_timer_,
      start_fit_timer_, hidden_displays_fit_timer_, hidden_displays_census_timer_})
  {
    if (timer != nullptr) {
      timer->stop();
    }
  }
  // The display tree of the hidden Displays copy is watched through connections that end with this
  // panel; they are cut now, while every member is still alive.
  if (hidden_displays_tree_watched_ && hidden_displays_) {
    if (QTreeView * const tree = hidden_displays_->findChild<QTreeView *>()) {
      disconnect(tree, nullptr, this, nullptr);
      if (const QAbstractItemModel * const model = tree->model()) {
        disconnect(model, nullptr, this, nullptr);
      }
    }
  }
  // The limits that a dock height request put on the other docks of the window come off now, for
  // the docks outlive the panel when the operator deletes it alone. Its own dock goes with it and
  // is not touched; it is found the way every other helper finds it.
  QDockWidget * const own_dock = enclosingDock(parentWidget());
  const auto release_all = [own_dock](DockHold & hold) {
      hold.docks.erase(
        std::remove_if(
          hold.docks.begin(), hold.docks.end(),
          [own_dock](const QPointer<QDockWidget> & dock) { return dock.data() == own_dock; }),
        hold.docks.end());
      release_dock_hold(hold);
    };
  release_all(default_dock_hold_);
  for (PendingDockStart & start : pending_dock_starts_) {
    release_all(start.hold);
  }
  if (start_fit_) {
    release_all(start_fit_->hold);
  }
  // Stop the render tick before the manager/adapter members are torn down so a
  // queued timeout can never call into a half-destroyed stream / DisplayContext.
  if (render_timer_ != nullptr) {
    render_timer_->stop();
    disconnect(render_timer_, nullptr, this, nullptr);
  }
  // The control page's session outlives the members of this panel that its signals and its
  // control sink reach (the server that holds it is declared earlier and dies later, and closes
  // its viewers while it does): cut both now, while every member is still alive.
  if (ar_control_session_) {
    ar_control_session_->set_control_sink(nullptr);
    disconnect(ar_control_session_.get(), nullptr, this, nullptr);
  }
  // Join a connect still in flight. Closing RViz mid-connect can cost up to the
  // ~3 s the session build takes, which is far better than unloading this DLL out
  // from under a running thread. PendingConnect is held by shared_ptr, so the
  // worker's own writes stay valid even though the panel is going away.
  if (connect_thread_.joinable()) {
    connect_thread_.join();
  }
  // The ROS entities on RViz's node, and the callback group that holds them, go while this
  // library is loaded: RViz destroys its node only after it has freed the library.
  releaseRosEntities();
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
  // The panel is a status strip over a tab widget. A control keeps its name and its text; only
  // the page it sits on changed. Why every page is a scroll page and why the panel's size hints
  // are constants: see stable_panel_layout.hpp. In short, this panel streams ITSELF to AR, and a
  // change of its size rebuilds the AR panel, so nothing a page shows may reach the panel's size.
  auto * root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // --- Status strip --------------------------------------------------------
  const StatusStrip strip = make_status_strip(this);
  status_strip_ = strip.frame;
  runtime_state_label_ = strip.first;
  last_error_label_ = strip.second;
  runtime_state_label_->setFullText("Qar runtime: STOPPED");
  last_error_label_->setFullText("Last error: <none>");
  root->addWidget(status_strip_);

  tabs_ = make_tab_widget(this);
  root->addWidget(tabs_, 1);

  // One page: a plain widget with a vertical layout, wrapped by make_scroll_page().
  auto add_page = [this](const QString & title, QWidget * content) {
      page_contents_.push_back(content);
      tabs_->addTab(make_scroll_page(content, tabs_), title);
    };
  // A list or a log is never lower than a few rows. The log takes any height its page has to
  // spare; the two lists of the AR tools page do not (see there).
  auto set_minimum_rows = [](QWidget * widget, int rows = 4) {
      widget->setMinimumHeight(widget->fontMetrics().lineSpacing() * rows + 6);
    };

  // =========================================================================
  // Connection
  // =========================================================================
  auto * connection_page = new QWidget();
  auto * connection_layout = new QVBoxLayout(connection_page);
  connection_layout->setContentsMargins(6, 6, 6, 6);
  connection_layout->setSpacing(6);

  auto * runtime_buttons = new QHBoxLayout();
  start_qar_button_ = new QPushButton("Start Qar", connection_page);
  stop_qar_button_ = new QPushButton("Stop Qar", connection_page);
  runtime_buttons->addWidget(start_qar_button_);
  runtime_buttons->addWidget(stop_qar_button_);
  connection_layout->addLayout(runtime_buttons);

  // Which kind of real device "Invite peer" invites. The group is shown in every mode, so that
  // the way to invite a device can always be found; the button is enabled only while a live QarOS
  // session exists (updateRuntimeStateLabel()) and says what it needs when it is not.
  invite_group_ = new QGroupBox("Invite a device", connection_page);
  auto * invite_layout = new QVBoxLayout(invite_group_);
  auto * invite_row = new QHBoxLayout();
  peer_kind_combo_ = new QComboBox(invite_group_);
  peer_kind_combo_->addItem("Visualizer", "visualizer");
  peer_kind_combo_->addItem("HoloLens 2", "hololens");
  peer_kind_combo_->addItem("Meta Quest", "quest");
  peer_kind_combo_->addItem("ZED camera", "zed");
  peer_kind_combo_->setToolTip(
    "Device type to invite into the running QarOS session. Each invited peer gets its "
    "own stereo stream of the RViz scene.");
  peer_hostname_edit_ = new QLineEdit(invite_group_);
  peer_hostname_edit_->setPlaceholderText("HoloLens hostname (e.g. HOLOLENS-A2B3)");
  peer_hostname_edit_->setToolTip(
    "Required for HoloLens 2 — it is the only device type the SDK can address by name. "
    "Ignored for the other kinds.");
  peer_hostname_edit_->setEnabled(false);  // the first kind, Visualizer, needs no host name
  invite_row->addWidget(peer_kind_combo_);
  invite_row->addWidget(peer_hostname_edit_, 1);
  add_peer_button_ = new QPushButton("Invite peer", invite_group_);
  add_peer_button_->setToolTip(kInviteNeedsSessionToolTip);  // no session yet
  invite_layout->addLayout(invite_row);
  invite_layout->addWidget(add_peer_button_);
  connection_layout->addWidget(invite_group_);

  // The AR control panel: the page that puts the panel's main controls into AR. The label
  // says whether it exists; the two buttons follow its state and stay disabled while there is
  // no control panel.
  auto * control_group = new QGroupBox("AR control panel", connection_page);
  auto * control_layout = new QVBoxLayout(control_group);
  ar_control_status_label_ = new QLabel(
    "AR control panel: not created (connect to QarOS)", control_group);
  ar_control_status_label_->setWordWrap(true);
  ar_control_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // Its text changes at run time and the panel streams itself: same rule as every status label.
  makeWidthNeutral(ar_control_status_label_);
  auto * control_buttons = new QHBoxLayout();
  ar_control_show_button_ = new QPushButton("Show AR control panel", control_group);
  ar_control_show_button_->setToolTip(
    "Show the AR control panel again if it was closed in AR. If QarOS cannot show it, the line "
    "above says so. With \"Hide panels in AR instead of closing them\" on, no panel is made "
    "again and the button asks QarOS to show it once more; with it off, pressing the button "
    "again within ten seconds makes the panel again. Available once the control panel exists.");
  ar_control_show_button_->setEnabled(false);
  ar_control_copy_button_ = new QPushButton("Copy page URL", control_group);
  ar_control_copy_button_->setToolTip(
    "Copy the address of the control panel's page, to open it in a desktop browser. "
    "Available once the page is served.");
  ar_control_copy_button_->setEnabled(false);
  control_buttons->addWidget(ar_control_show_button_);
  control_buttons->addWidget(ar_control_copy_button_);
  control_layout->addWidget(ar_control_status_label_);
  control_layout->addLayout(control_buttons);
  connection_layout->addWidget(control_group);
  connection_layout->addStretch(1);
  add_page("Connection", connection_page);

  // =========================================================================
  // AR panels
  // =========================================================================
  auto * panels_page = new QWidget();
  auto * panels_layout = new QVBoxLayout(panels_page);
  panels_layout->setContentsMargins(6, 6, 6, 6);
  panels_layout->setSpacing(6);

  // Any RViz dock panel can be streamed, not just this one - the combo is
  // repopulated from the live dock widgets each time it is opened.
  auto * panel_pick_row = new QHBoxLayout();
  ar_panel_combo_ = new QComboBox(panels_page);
  ar_panel_combo_->setToolTip(
    "Which Qt panel to publish as a QarOS GUI panel. Any RViz dock panel works; "
    "this QAR panel is listed first.");
  refresh_panels_button_ = new QPushButton("Refresh", panels_page);
  refresh_panels_button_->setToolTip("Re-scan RViz for dock panels (they come and go).");
  panel_pick_row->addWidget(ar_panel_combo_, 1);
  panel_pick_row->addWidget(refresh_panels_button_);

  auto * panel_buttons = new QHBoxLayout();
  stream_panel_button_ = new QPushButton("Stream panel to AR", panels_page);
  stream_panel_button_->setToolTip(
    "Publish the selected panel as a QarOS GUI panel, so it is visible and clickable "
    "in AR. Needs a running QarOS session.");
  stream_panel_button_->setEnabled(false);
  stop_panel_button_ = new QPushButton("Stop streaming", panels_page);
  stop_panel_button_->setToolTip(
    "Stop streaming the selected panel to AR. With \"Hide panels in AR instead of closing "
    "them\" on, its AR panel is hidden and parked: its QarOS panel and video sender are kept "
    "until the session ends, and \"Stream panel to AR\" shows it again. With it off, the AR "
    "panel is closed (or shows a notice, as \"Closed AR panels\" says).");
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
  //    never consulted, and the box's fixed height pins the other axis, so no
  //    text can move this widget's geometry by a pixel.
  //
  //  * free of scroll bars, because a scroll bar in AR cannot be used: the
  //    pointer there is a ray. The box holds sixteen lines of the widget's own
  //    font, both bars are off, and updateArPanelStatus() shortens the text
  //    until it fits and says how many panel lines it left out. The text is
  //    selectable for copying into a report.
  const ReadoutBox readout = make_readout_box(
    panels_page, QStringLiteral("AR panels: none"),
    QStringLiteral(
      "Live AR panel counters. This box never resizes and never scrolls: it is inside the "
      "widget being streamed, and a resize here would rebuild the AR panel it is reporting on. "
      "The long counters of every panel are in the log."));
  ar_panel_status_label_ = readout.label;
  ar_panel_status_scroll_ = readout.area;

  panels_layout->addLayout(panel_pick_row);
  panels_layout->addLayout(panel_buttons);
  panels_layout->addWidget(ar_panel_status_scroll_);
  panels_layout->addStretch(1);
  add_page("AR panels", panels_page);

  // =========================================================================
  // AR tools
  // =========================================================================
  auto * tools_page = new QWidget();
  auto * tools_layout = new QVBoxLayout(tools_page);
  tools_layout->setContentsMargins(6, 6, 6, 6);
  tools_layout->setSpacing(6);

  auto * watched_group = new QGroupBox("Watched topics", tools_page);
  auto * watched_layout = new QVBoxLayout(watched_group);
  auto * topic_buttons = new QHBoxLayout();
  add_topic_button_ = new QPushButton("Add from ROS graph...", watched_group);
  add_topic_button_->setToolTip(
    "Browse the live ROS 2 graph — topics, services and actions — add a topic to "
    "RViz, and/or expose any of them as a QAR AR tool.");
  remove_topic_button_ = new QPushButton("Remove topic", watched_group);
  topic_buttons->addWidget(add_topic_button_);
  topic_buttons->addWidget(remove_topic_button_);
  // The two lists of this page are half as high as a stock list box (CompactList) and do not
  // take the height the page has to spare: it stays below the groups (the stretch at the end).
  // Their minimum is two rows. A vertical policy of Preferred keeps a taller panel from
  // stretching them.
  topic_watch_list_ = new CompactList(watched_group);
  topic_watch_list_->setToolTip(
    "Topics added from the topic browser. Adding one also creates the matching "
    "RViz display, so it appears in the streamed 3D scene.");
  set_minimum_rows(topic_watch_list_, 2);
  topic_watch_list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  watched_layout->addLayout(topic_buttons);
  watched_layout->addWidget(topic_watch_list_);
  tools_layout->addWidget(watched_group);

  // The management list: what is exposed right now, and the things an operator needs to do
  // with a row.
  auto * exposed_group = new QGroupBox("AR tools", tools_page);
  auto * exposed_layout = new QVBoxLayout(exposed_group);
  exposure_list_ = new CompactList(exposed_group);
  exposure_list_->setToolTip(
    "Topics, services and actions exposed as QAR AR tools. Each row is a "
    "generated panel served over HTTP and shown in the headset's browser.");
  set_minimum_rows(exposure_list_, 2);
  exposure_list_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto * exposure_buttons = new QHBoxLayout();
  remove_exposure_button_ = new QPushButton("Remove tool", exposed_group);
  remove_exposure_button_->setToolTip(
    "Stop serving the tool's page and take its AR panel out of use. With \"Hide panels in AR "
    "instead of closing them\" on, the AR panel is hidden and kept until the session ends, and "
    "exposing the same tool again takes it over; with it off, the AR panel is closed.");
  reopen_exposure_button_ = new QPushButton("Re-open in AR", exposed_group);
  reopen_exposure_button_->setToolTip(
    "Bring the QarOS panel back, after a session restart or if it was hidden or closed in the "
    "headset. With \"Hide panels in AR instead of closing them\" on, QarOS is asked to show the "
    "panel again; if QarOS refuses, the log and the last error say so and nothing is closed or "
    "made again. With it off, the panel is closed and made again. The page and its token are "
    "unchanged.");
  copy_url_button_ = new QPushButton("Copy URL", exposed_group);
  copy_url_button_->setToolTip(
    "Copy the panel's URL, to open it in a desktop browser while debugging.");
  // Live size trim. The panel's physical size is DERIVED from the generated
  // page's own measurement (qar_web_panels/url_panel_sizing.hpp), which is the
  // right default; this is the operator's "a little bigger, please" knob, and
  // it takes effect on the next "Re-open in AR" - no rebuild, no restart. Its
  // starting value is the "Web Panel Scale" setting (QAR_PANEL_WEB_SCALE, the saved
  // configuration, or 1.00; applySettingsToUi() sets it).
  ar_panel_scale_spin_ = new QDoubleSpinBox(exposed_group);
  ar_panel_scale_spin_->setRange(0.25, 4.0);
  ar_panel_scale_spin_->setSingleStep(0.1);
  ar_panel_scale_spin_->setDecimals(2);
  ar_panel_scale_spin_->setPrefix("size x");
  ar_panel_scale_spin_->setValue(setting_double(effective_settings_, settings_key::kWebPanelScale));
  ar_panel_scale_spin_->setToolTip(
    "Multiplier on the AR panel size derived from the page's own content size. "
    "Press \"Re-open in AR\" to apply it to the selected tool.");
  exposure_buttons->addWidget(remove_exposure_button_);
  exposure_buttons->addWidget(reopen_exposure_button_);
  exposure_buttons->addWidget(copy_url_button_);
  exposure_buttons->addWidget(ar_panel_scale_spin_);
  exposure_status_label_ = new QLabel("AR tools: none", exposed_group);
  exposure_status_label_->setWordWrap(true);
  exposure_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // Same feedback loop as the AR panel status: this label is inside the
  // streamed widget and its text changes whenever a tool is added or removed.
  makeWidthNeutral(exposure_status_label_);
  exposed_layout->addWidget(exposure_list_);
  exposed_layout->addLayout(exposure_buttons);
  exposed_layout->addWidget(exposure_status_label_);
  tools_layout->addWidget(exposed_group);
  tools_layout->addStretch(1);
  add_page("AR tools", tools_page);

  // =========================================================================
  // Picking
  // =========================================================================
  // Arming is explicit and starts at "Off": a hand gesture in the headset must
  // never move a robot because a mode was left on. What each control means is
  // decided by SpatialInteractionController (spatial_interaction.hpp), which is
  // where the rules are unit-tested; this page only sets it and shows it.
  auto * picking_page = new QWidget();
  auto * picking_layout = new QVBoxLayout(picking_page);
  picking_layout->setContentsMargins(6, 6, 6, 6);
  picking_layout->setSpacing(6);
  auto * spatial_form = new QFormLayout();
  spatial_mode_combo_ = new QComboBox(picking_page);
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
  spatial_target_combo_ = new QComboBox(picking_page);
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
  spatial_frame_edit_ = new QLineEdit("map", picking_page);
  spatial_frame_edit_->setToolTip("The frame every published pick is stamped with.");
  spatial_auto_confirm_ = new QCheckBox("Publish without confirming", picking_page);
  spatial_auto_confirm_->setToolTip(
    "Off by default: a pick waits for Confirm so a stray gesture cannot drive "
    "the robot.");
  spatial_form->addRow("Mode", spatial_mode_combo_);
  spatial_form->addRow("Target", spatial_target_combo_);
  spatial_form->addRow("Frame", spatial_frame_edit_);
  auto * spatial_buttons = new QHBoxLayout();
  spatial_confirm_button_ = new QPushButton("Confirm pick", picking_page);
  spatial_cancel_button_ = new QPushButton("Cancel", picking_page);
  spatial_close_polygon_button_ = new QPushButton("Close polygon", picking_page);
  spatial_close_polygon_button_->setToolTip(
    "Area mode with the polygon shape: finish the region the taps collected.");
  spatial_buttons->addWidget(spatial_confirm_button_);
  spatial_buttons->addWidget(spatial_cancel_button_);
  spatial_buttons->addWidget(spatial_close_polygon_button_);
  spatial_status_label_ = new QLabel("Spatial: off", picking_page);
  spatial_status_label_->setWordWrap(true);
  spatial_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  makeWidthNeutral(spatial_status_label_);
  picking_layout->addLayout(spatial_form);
  picking_layout->addWidget(spatial_auto_confirm_);
  picking_layout->addLayout(spatial_buttons);
  picking_layout->addWidget(spatial_status_label_);
  picking_layout->addStretch(1);
  add_page("Picking", picking_page);

  // =========================================================================
  // Settings
  // =========================================================================
  // What the panel keeps in the RViz configuration, and the switches that an environment
  // variable can also set. Each persisted control shows the value in force and, where an
  // environment variable decides it, is disabled with the variable named in its tool tip
  // (applySettingsToUi()). The group titles say WHEN a setting takes effect.
  auto * settings_page = new QWidget();
  auto * settings_layout = new QVBoxLayout(settings_page);
  settings_layout->setContentsMargins(6, 6, 6, 6);
  settings_layout->setSpacing(6);

  auto * streams_group = new QGroupBox("Streams (apply to new streams)", settings_page);
  auto * streams_form = new QFormLayout(streams_group);
  resolution_combo_ = new QComboBox(streams_group);
  resolution_combo_->addItem("640 x 480", QSize(640, 480));
  resolution_combo_->addItem("1024 x 1024", QSize(1024, 1024));
  resolution_combo_->addItem("1280 x 720", QSize(1280, 720));
  resolution_combo_->addItem("1600 x 900", QSize(1600, 900));
  resolution_combo_->addItem("1920 x 1080", QSize(1920, 1080));
  resolution_combo_->addItem("2048 x 2048", QSize(2048, 2048));
  resolution_combo_->setCurrentIndex(2);  // 1280 x 720
  resolution_combo_->setToolTip(
    "The size of the pictures of the streams that are created from now on. Streams that "
    "are already open keep their size.");
  // FPS options MUST include 30 and 60.
  fps_combo_ = new QComboBox(streams_group);
  for (const int fps : {15, 30, 60, 72, 90}) {
    fps_combo_->addItem(QString("%1 fps").arg(fps), fps);
  }
  fps_combo_->setCurrentIndex(2);  // 60 fps (the optimum target)
  fps_combo_->setToolTip(
    "The most frames per second a stream is asked for, for the streams that are created from "
    "now on.");
  streams_form->addRow("Resolution:", resolution_combo_);
  streams_form->addRow("FPS cap:", fps_combo_);
  settings_layout->addWidget(streams_group);

  auto * session_group = new QGroupBox("Session (apply when you connect)", settings_page);
  auto * session_form = new QFormLayout(session_group);
  peer_admission_combo_ = new QComboBox(session_group);
  peer_admission_combo_->addItem("Invited peers only", "invited");
  peer_admission_combo_->addItem("Invited peers and devices that join later", "new");
  peer_admission_combo_->setToolTip(
    "Which devices get a stream. Invited peers only: the devices you invite with \"Invite "
    "peer\" (and QAR_PANEL_INVITE_PEERS). Invited peers and devices that join later: also a "
    "device that onboards by itself after the first seconds of the session (a headset, "
    "another machine's Visualizer). Applies at the first connect of this RViz run: a later "
    "connect after a pause reuses the session.");
  hidden_displays_checkbox_ = new QCheckBox(
    "Start the hidden Displays copy when connecting", session_group);
  hidden_displays_checkbox_->setToolTip(
    "Streams a second copy of RViz's Displays panel into AR when the panel connects, so the "
    "headset can switch displays on and off. Off keeps it from starting by itself; picking "
    "its entry in the AR panels list and pressing \"Stream panel to AR\" still starts it. "
    "Applies at every connect.");
  ar_control_checkbox_ = new QCheckBox(
    "Show the AR control panel when connecting", session_group);
  ar_control_checkbox_->setToolTip(
    "Serves the AR control panel, a page with the panel's main controls, when the panel "
    "connects, and shows it in AR in front of each user once that user's view is being "
    "streamed. In the offline preview only the page is served: \"Copy page URL\" opens it in a "
    "desktop browser. Applies at every connect.");
  session_form->addRow("Peer admission:", peer_admission_combo_);
  session_form->addRow(hidden_displays_checkbox_);
  session_form->addRow(ar_control_checkbox_);
  settings_layout->addWidget(session_group);

  show_advanced_checkbox_ = new QCheckBox("Show advanced options", settings_page);
  show_advanced_checkbox_->setToolTip(
    "Shows the advanced settings below and, in a QarOS session, the simulated-peer controls on "
    "the Diagnostics tab. This choice is not saved.");
  settings_layout->addWidget(show_advanced_checkbox_);

  // --- Frame-timing diagnostics (QAR_ADAPTER_PRIME) -------------------------
  // See docs/ARCHITECTURE.md#the-render-tick. Priming is a live A/B switch for telling a
  // fixed-tick wait apart from an SDK pose/frame mismatch, not a change to the render budget.
  // It defaults to on.
  advanced_group_ = new QGroupBox("Advanced", settings_page);
  auto * advanced_form = new QFormLayout(advanced_group_);
  prime_checkbox_ = new QCheckBox("Prime next frame", advanced_group_);
  const QString qprime = qEnvironmentVariable("QAR_ADAPTER_PRIME");
  const QByteArray qprime_utf8 = qprime.toUtf8();
  prime_checkbox_->setChecked(
    qar_qar_adapter::parse_bool_env(qprime.isEmpty() ? nullptr : qprime_utf8.constData(), true));
  prime_checkbox_->setToolTip(
    "Start the next frame's SDK wait right after this one is shown (one tick per "
    "frame). Off: the wait starts at the next tick's "
    "begin_frame instead (two ticks per frame).");
  close_mode_combo_ = new QComboBox(advanced_group_);
  close_mode_combo_->addItem("Close them", "destroy");
  close_mode_combo_->addItem("Show a notice", "notice");
  close_mode_combo_->setToolTip(
    "What happens to the AR copy of a panel when its RViz window is closed, while \"Hide panels "
    "in AR instead of closing them\" is off. Close them: the AR panel is closed and its video "
    "sender is destroyed. Show a notice: the AR panel stays as a closed-window notice and its "
    "sender is kept until the session ends, for a QarOS runtime that does not survive "
    "destroying a sender. With hiding on, a window that closes only hides its AR panel and "
    "this choice is not used. A change acts on the closes that follow.");
  ar_visibility_checkbox_ = new QCheckBox(
    "Hide panels in AR instead of closing them (experimental)", advanced_group_);
  ar_visibility_checkbox_->setToolTip(
    "On: no QarOS panel is closed while a session runs. Switching a panel off hides it in AR and "
    "switching it on shows it again; a window that closes on the desktop (Stop streaming, a "
    "closed dock or dialog) and a removed AR tool leave their AR panel hidden and parked, and "
    "opening the window or exposing the tool again shows it again without a new QarOS panel; "
    "\"Re-open in AR\" shows an AR tool again. Everything is closed once, when the session ends. "
    "Off: the panel is closed and made again. A change acts on the next switch, close or "
    "removal.");
  advanced_form->addRow(prime_checkbox_);
  advanced_form->addRow("Closed AR panels:", close_mode_combo_);
  advanced_form->addRow(ar_visibility_checkbox_);
  retain_size_when_hidden(advanced_group_);
  settings_layout->addWidget(advanced_group_);
  settings_layout->addStretch(1);
  add_page("Settings", settings_page);

  // =========================================================================
  // Diagnostics
  // =========================================================================
  auto * diag_page = new QWidget();
  auto * diag_layout = new QVBoxLayout(diag_page);
  diag_layout->setContentsMargins(6, 6, 6, 6);
  diag_layout->setSpacing(6);
  context_status_label_ = new QLabel("DisplayContext: (not initialized)", diag_page);
  context_status_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  makeWidthNeutral(context_status_label_);
  auto * diag_buttons = new QHBoxLayout();
  refresh_button_ = new QPushButton("Refresh diagnostics", diag_page);
  capture_button_ = new QPushButton("Capture offscreen frame", diag_page);
  stereo_capture_button_ = new QPushButton("Capture stereo pair", diag_page);
  diag_buttons->addWidget(refresh_button_);
  diag_buttons->addWidget(capture_button_);
  diag_buttons->addWidget(stereo_capture_button_);

  // The test markers: a script that publishes a few coloured markers for the test scenes. The
  // launchers say which python and which script to run (QAR_PANEL_TEST_MARKERS_PYTHON and
  // QAR_PANEL_TEST_MARKERS_SCRIPT); without them the button is disabled and its tool tip says what
  // is missing. refreshTestMarkersUi() sets the text, the state and the tool tip.
  auto * marker_row = new QHBoxLayout();
  test_markers_button_ = new QPushButton("Start test markers", diag_page);
  marker_row->addWidget(test_markers_button_);
  marker_row->addStretch(1);

  // The simulated peers of the offline preview. In a QarOS session they are an advanced section
  // (shown by "Show advanced options"); in the preview they are how the panel works, so
  // refreshAdvancedVisibility() always shows them there.
  sim_peers_group_ = new QGroupBox("Simulated peers", diag_page);
  auto * sim_layout = new QVBoxLayout(sim_peers_group_);
  auto * peer_buttons = new QHBoxLayout();
  add_sim_peer_button_ = new QPushButton("Add simulated peer", sim_peers_group_);
  add_sim_peer_button_->setToolTip(
    "Add a simulated peer with its own stereo stream of the RViz scene, for the offline "
    "preview.");
  remove_peer_button_ = new QPushButton("Remove peer", sim_peers_group_);
  peer_buttons->addWidget(add_sim_peer_button_);
  peer_buttons->addWidget(remove_peer_button_);
  peer_list_ = new QListWidget(sim_peers_group_);
  set_minimum_rows(peer_list_);
  auto * stream_buttons = new QHBoxLayout();
  start_stream_button_ = new QPushButton("Start stream", sim_peers_group_);
  stop_stream_button_ = new QPushButton("Stop stream", sim_peers_group_);
  stream_buttons->addWidget(start_stream_button_);
  stream_buttons->addWidget(stop_stream_button_);
  sim_layout->addLayout(peer_buttons);
  sim_layout->addWidget(peer_list_);
  sim_layout->addLayout(stream_buttons);

  diagnostics_view_ = new QPlainTextEdit(diag_page);
  diagnostics_view_->setReadOnly(true);
  diagnostics_view_->setMaximumBlockCount(2000);
  set_minimum_rows(diagnostics_view_);
  diag_layout->addWidget(context_status_label_);
  diag_layout->addLayout(diag_buttons);
  diag_layout->addLayout(marker_row);
  diag_layout->addWidget(sim_peers_group_);
  diag_layout->addWidget(diagnostics_view_, 1);
  add_page("Diagnostics", diag_page);

  // The states the controls start in: the preview's. onInitialize() knows whether the panel
  // is live and applies that.
  stop_qar_button_->setEnabled(false);
  add_peer_button_->setEnabled(false);
  refreshAdvancedVisibility();
  refreshTestMarkersUi();

  // The pages scroll. A wheel turned over a page must scroll it, not change the setting (or arm
  // the pick mode) of the control under the pointer; a click gives a control the wheel.
  for (QWidget * control : {
      static_cast<QWidget *>(resolution_combo_), static_cast<QWidget *>(fps_combo_),
      static_cast<QWidget *>(peer_admission_combo_), static_cast<QWidget *>(close_mode_combo_),
      static_cast<QWidget *>(ar_panel_scale_spin_), static_cast<QWidget *>(peer_kind_combo_),
      static_cast<QWidget *>(spatial_mode_combo_), static_cast<QWidget *>(spatial_target_combo_),
      static_cast<QWidget *>(ar_panel_combo_)})
  {
    guard_wheel(control);
  }

  // What scrolls by design: a list grows with what the operator adds to it and a log with what
  // happened. The scroll-area census leaves these out when it asks whether anything that must not
  // scroll in AR does (the readout is marked by make_readout_box()). The pages themselves are not
  // marked: a page that scrolls is a panel that is too low.
  for (QWidget * area : {
      static_cast<QWidget *>(topic_watch_list_), static_cast<QWidget *>(exposure_list_),
      static_cast<QWidget *>(peer_list_), static_cast<QWidget *>(diagnostics_view_)})
  {
    area->setProperty(qar_qt_streaming::kScrollsByDesignProperty, true);
  }

  // The size hints are measured once, with every page built and holding its first content, and
  // are the answer of sizeHint() and minimumSizeHint() from now on.
  const StableHints hints = compute_stable_hints(*tabs_, page_contents_, *status_strip_);
  stable_size_hint_ = hints.hint;
  stable_minimum_size_hint_ = hints.minimum;

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
  {
    // Not the operator's choice, so it must not reach the settings as one.
    const QSignalBlocker blocker(spatial_target_combo_);
    spatial_target_combo_->setCurrentIndex(0);
  }
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
    [this]() {
      const QString frame = spatial_frame_edit_->text().trimmed();
      spatial_.set_frame_id(frame.toStdString());
      onSettingEdited(settings_key::kPickFrame, frame);
    });
  connect(
    spatial_auto_confirm_, &QCheckBox::toggled, this,
    [this](bool on) {
      spatial_.set_auto_confirm(on);
      refreshSpatialUi();
    });

  // --- Signal/slot wiring --------------------------------------------------
  connect(start_qar_button_, &QPushButton::clicked, this, &QarStreamingPanel::onStartQar);
  connect(stop_qar_button_, &QPushButton::clicked, this, &QarStreamingPanel::onStopQar);
  connect(add_peer_button_, &QPushButton::clicked, this, &QarStreamingPanel::onInvitePeer);
  connect(
    test_markers_button_, &QPushButton::clicked, this, &QarStreamingPanel::onTestMarkersClicked);
  connect(
    add_sim_peer_button_, &QPushButton::clicked, this, &QarStreamingPanel::onAddSimulatedPeer);
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
    ar_control_show_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onShowArControlPanel);
  connect(
    ar_control_copy_button_, &QPushButton::clicked, this,
    &QarStreamingPanel::onCopyArControlUrl);
  connect(
    prime_checkbox_, &QCheckBox::toggled, this, &QarStreamingPanel::onPrimeCheckboxToggled);
  connect(
    show_advanced_checkbox_, &QCheckBox::toggled, this,
    [this](bool) { refreshAdvancedVisibility(); });

  // --- The persisted controls: an edit is the operator's own value ---------------------------
  connect(
    resolution_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int) {
      onSettingEdited(
        settings_key::kResolution,
        sizeWord(resolution_combo_->currentData().toSize()));
    });
  connect(
    fps_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int) {
      onSettingEdited(settings_key::kFpsCap, fps_combo_->currentData().toString());
    });
  connect(
    peer_admission_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int) {
      onSettingEdited(
        settings_key::kPeerAdmission, peer_admission_combo_->currentData().toString());
    });
  connect(
    hidden_displays_checkbox_, &QCheckBox::toggled, this,
    [this](bool on) { onSettingEdited(settings_key::kHiddenDisplaysOnConnect, boolWord(on)); });
  connect(
    ar_control_checkbox_, &QCheckBox::toggled, this,
    [this](bool on) { onSettingEdited(settings_key::kArControlPanel, boolWord(on)); });
  connect(
    close_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int) {
      onSettingEdited(settings_key::kArPanelCloseMode, close_mode_combo_->currentData().toString());
    });
  connect(
    ar_visibility_checkbox_, &QCheckBox::toggled, this,
    [this](bool on) {
      onSettingEdited(settings_key::kArVisibility, QString(on ? "hide" : "close"));
    });
  connect(
    ar_panel_scale_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
    [this](double value) {
      onSettingEdited(settings_key::kWebPanelScale, QString::number(value, 'f', 2));
    });
  connect(
    peer_kind_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int) {
      const QString kind = peer_kind_combo_->currentData().toString();
      // Only a HoloLens is addressed by name; the field is for it alone.
      peer_hostname_edit_->setEnabled(kind == "hololens");
      onSettingEdited(settings_key::kInviteDevice, kind);
    });
  connect(
    peer_hostname_edit_, &QLineEdit::editingFinished, this,
    [this]() {
      onSettingEdited(settings_key::kInviteHostname, peer_hostname_edit_->text().trimmed());
    });

  // --- The size of the panel is logged, to prove it does not move ------------------------------
  connect(
    tabs_, &QTabWidget::currentChanged, this,
    [this](int index) {
      const QString name = tabs_->tabText(index);
      // After the layout of the switch has run; a queued call dies with the panel.
      QMetaObject::invokeMethod(
        this, [this, name]() { logTabSelected(name); }, Qt::QueuedConnection);
    });
  resize_flush_timer_ = new QTimer(this);
  resize_flush_timer_->setSingleShot(true);
  connect(resize_flush_timer_, &QTimer::timeout, this, &QarStreamingPanel::flushResizeLog);
  // Started by load() when the saved configuration asks for the default dock size.
  default_dock_timer_ = new QTimer(this);
  default_dock_timer_->setSingleShot(true);
  connect(
    default_dock_timer_, &QTimer::timeout, this, &QarStreamingPanel::applyDefaultDockSize);
  // Both are children of the panel (a QTimer::singleShot would outlive it): the first takes the
  // temporary minimum height off a dock that applyDefaultDockSize() held, the second starts the
  // streams of docks that were grown for them.
  default_dock_release_timer_ = new QTimer(this);
  default_dock_release_timer_->setSingleShot(true);
  connect(
    default_dock_release_timer_, &QTimer::timeout, this,
    &QarStreamingPanel::releaseDefaultDockHold);
  dock_start_timer_ = new QTimer(this);
  dock_start_timer_->setSingleShot(true);
  connect(
    dock_start_timer_, &QTimer::timeout, this, &QarStreamingPanel::startPendingDockStreams);
  // The fit of this panel's own dock at start: started by the first show, one look per period.
  start_fit_timer_ = new QTimer(this);
  start_fit_timer_->setSingleShot(true);
  connect(start_fit_timer_, &QTimer::timeout, this, &QarStreamingPanel::startFit);
  // The same for the hidden Displays copy: one look per period while it is being fitted.
  hidden_displays_fit_timer_ = new QTimer(this);
  hidden_displays_fit_timer_->setSingleShot(true);
  connect(
    hidden_displays_fit_timer_, &QTimer::timeout, this,
    &QarStreamingPanel::advanceHiddenDisplaysFit);
  // And one wait of a second between a change of its display tree and the census it earns.
  hidden_displays_census_timer_ = new QTimer(this);
  hidden_displays_census_timer_->setSingleShot(true);
  connect(
    hidden_displays_census_timer_, &QTimer::timeout, this,
    &QarStreamingPanel::requestHiddenDisplaysCensus);

  refreshStreamablePanels();
}

// ---------------------------------------------------------------------------
// Size hints and the size log
// ---------------------------------------------------------------------------
QSize QarStreamingPanel::sizeHint() const
{
  return stable_size_hint_;
}

QSize QarStreamingPanel::minimumSizeHint() const
{
  return stable_minimum_size_hint_;
}

void QarStreamingPanel::showEvent(QShowEvent * event)
{
  rviz_common::Panel::showEvent(event);
  scheduleSizeReport();
}

void QarStreamingPanel::resizeEvent(QResizeEvent * event)
{
  rviz_common::Panel::resizeEvent(event);
  if (!size_reported_ || event->size() == last_logged_panel_size_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  const bool first = resize_logged_at_.time_since_epoch().count() == 0;
  const long long since_ms = first ? 1000000LL :
    static_cast<long long>(
    std::chrono::duration_cast<std::chrono::milliseconds>(now - resize_logged_at_).count());
  if (since_ms >= 1000) {
    appendLog(
      QString("PANEL SIZE: panel resized %1 -> %2")
      .arg(sizeWord(last_logged_panel_size_), sizeWord(event->size())));
    last_logged_panel_size_ = event->size();
    resize_logged_at_ = now;
    return;
  }
  // Within a second of the last line: one line a second. A burst is one line now and one for
  // where it ended. The timer is a member (a child of this panel), not a QTimer::singleShot: a
  // singleShot timer outlives the panel and would still hold a slot of this plugin when RViz
  // unloads it.
  if (resize_flush_timer_ != nullptr && !resize_flush_timer_->isActive()) {
    resize_flush_timer_->start(static_cast<int>(1000 - since_ms));
  }
}

void QarStreamingPanel::flushResizeLog()
{
  if (size() == last_logged_panel_size_) {
    return;
  }
  appendLog(
    QString("PANEL SIZE: panel resized %1 -> %2")
    .arg(sizeWord(last_logged_panel_size_), sizeWord(size())));
  last_logged_panel_size_ = size();
  resize_logged_at_ = std::chrono::steady_clock::now();
}

void QarStreamingPanel::scheduleSizeReport()
{
  if (!panel_initialized_ || size_report_scheduled_) {
    return;
  }
  size_report_scheduled_ = true;
  // After the layout of the show has run, so the sizes are the ones the panel settled on. A
  // queued call dies with the panel; a QTimer::singleShot would outlive it.
  QMetaObject::invokeMethod(this, [this]() { reportPanelSizes(); }, Qt::QueuedConnection);
}

void QarStreamingPanel::reportPanelSizes()
{
  if (tabs_ == nullptr || size_reported_) {
    return;
  }
  size_reported_ = true;
  last_logged_panel_size_ = size();
  appendLog(
    QString("PANEL SIZE: panel %1 (hint %2, min %3), strip %4 px, tab bar %5 px")
    .arg(sizeWord(size()), sizeWord(sizeHint()), sizeWord(minimumSizeHint()))
    .arg(status_strip_->height())
    .arg(tab_bar_height(*tabs_)));
  for (const std::string & line : describe_page_fit(*tabs_, page_contents_)) {
    appendLog("PANEL SIZE: " + QString::fromStdString(line));
  }
  // From the first show on, the dock may be fitted to the pages (armStartFit()).
  armStartFit();
}

void QarStreamingPanel::logTabSelected(const QString & tab_name)
{
  // Another page is another picture in AR: when this panel streams, its scroll areas are counted
  // again at the next frame it sends. The manager answers false when it does not stream.
  if (panel_manager_) {
    panel_manager_->request_scroll_census(
      kSelfPanelKey, QString("tab '%1' selected").arg(tab_name).toStdString());
  }
  if (!size_reported_) {
    return;
  }
  const QSize now = size();
  if (now == last_logged_panel_size_) {
    appendLog(
      QString("PANEL SIZE: tab '%1' selected; panel %2 unchanged").arg(tab_name, sizeWord(now)));
    return;
  }
  appendLog(
    QString("PANEL SIZE: tab '%1' selected; panel %2 CHANGED from %3")
    .arg(tab_name, sizeWord(now), sizeWord(last_logged_panel_size_)));
  last_logged_panel_size_ = now;
}

void QarStreamingPanel::refreshAdvancedVisibility()
{
  const bool advanced = show_advanced_checkbox_ != nullptr && show_advanced_checkbox_->isChecked();
  if (advanced_group_ != nullptr) {
    advanced_group_->setVisible(advanced);
  }
  if (sim_peers_group_ != nullptr) {
    sim_peers_group_->setVisible(!live_stream_mode_ || advanced);
  }
}

// ---------------------------------------------------------------------------
// Persisted settings (panel_settings.hpp)
// ---------------------------------------------------------------------------
void QarStreamingPanel::snapshotEnvironment()
{
  env_snapshot_.clear();
  for (const SettingSpec & spec : panel_setting_specs()) {
    if (spec.env != nullptr && qEnvironmentVariableIsSet(spec.env)) {
      env_snapshot_[spec.env] = qEnvironmentVariable(spec.env).toStdString();
    }
  }
}

void QarStreamingPanel::resolveSettings()
{
  const EnvLookup lookup = [this](const char * name) -> std::optional<std::string> {
      const auto found = env_snapshot_.find(name);
      if (found == env_snapshot_.end()) {
        return std::nullopt;
      }
      return found->second;
    };
  settings_warnings_.clear();
  effective_settings_ = resolve_settings(user_settings_, lookup, settings_warnings_);
}

void QarStreamingPanel::logSettingsLine(const QString & text)
{
  if (settings_lines_logged_.insert(text.toStdString()).second) {
    appendLog("SETTINGS: " + text);
  }
}

void QarStreamingPanel::reportSettings()
{
  for (const std::string & warning : settings_warnings_) {
    logSettingsLine(QString::fromStdString(warning));
  }
  for (const SettingSpec & spec : panel_setting_specs()) {
    const auto found = effective_settings_.find(spec.key);
    if (found == effective_settings_.end() ||
      found->second.source != SettingSource::Environment)
    {
      continue;
    }
    logSettingsLine(
      QString("%1 = %2 comes from the environment (%3=%4); its control shows that value and is "
        "disabled")
      .arg(
        QString::fromLatin1(spec.key), QString::fromStdString(found->second.value),
        QString::fromStdString(found->second.env_name),
        QString::fromStdString(found->second.env_raw).trimmed()));
  }
}

QString QarStreamingPanel::refusedEnvironmentText(const char * key) const
{
  const SettingSpec * spec = find_setting_spec(key);
  if (spec == nullptr || spec->env == nullptr) {
    return QString();
  }
  const auto raw = env_snapshot_.find(spec->env);
  if (raw == env_snapshot_.end()) {
    return QString();
  }
  const auto found = effective_settings_.find(key);
  if (found != effective_settings_.end() && found->second.source == SettingSource::Environment) {
    return QString();
  }
  return QString::fromStdString(raw->second).trimmed().toLower();
}

void QarStreamingPanel::applySettingsToUi()
{
  const ResolvedSetting none;
  const auto resolved = [this, &none](const char * key) -> const ResolvedSetting & {
      const auto found = effective_settings_.find(key);
      return found != effective_settings_.end() ? found->second : none;
    };
  const auto text = [this](const char * key) {
      return QString::fromStdString(setting_text(effective_settings_, key));
    };
  const auto select_data = [](QComboBox * combo, const QVariant & data) {
      const int index = combo->findData(data);
      if (index >= 0) {
        combo->setCurrentIndex(index);
      }
    };

  // Streams: the combos hold the value; the streams created from now on read it.
  {
    const QSignalBlocker blocker(resolution_combo_);
    const QString word = text(settings_key::kResolution);
    for (int i = 0; i < resolution_combo_->count(); ++i) {
      if (sizeWord(resolution_combo_->itemData(i).toSize()) == word) {
        resolution_combo_->setCurrentIndex(i);
        break;
      }
    }
  }
  {
    const QSignalBlocker blocker(fps_combo_);
    select_data(fps_combo_, setting_int(effective_settings_, settings_key::kFpsCap));
  }

  // Session. The environment may name an admission the list does not offer (named, any): the
  // list then gets one more item, which shows it, and goes again when the value changes.
  {
    const QSignalBlocker blocker(peer_admission_combo_);
    for (int i = peer_admission_combo_->count() - 1; i >= 0; --i) {
      if (peer_admission_combo_->itemData(i, Qt::UserRole + 1).toBool()) {
        peer_admission_combo_->removeItem(i);
      }
    }
    const QString admission = text(settings_key::kPeerAdmission);
    int index = peer_admission_combo_->findData(admission);
    if (index < 0) {
      peer_admission_combo_->addItem(admission + " (from the environment)", admission);
      index = peer_admission_combo_->count() - 1;
      peer_admission_combo_->setItemData(index, true, Qt::UserRole + 1);
    }
    peer_admission_combo_->setCurrentIndex(index);
  }
  {
    const QSignalBlocker blocker(hidden_displays_checkbox_);
    hidden_displays_checkbox_->setChecked(
      setting_bool(effective_settings_, settings_key::kHiddenDisplaysOnConnect));
  }
  {
    const QSignalBlocker blocker(ar_control_checkbox_);
    ar_control_checkbox_->setChecked(
      setting_bool(effective_settings_, settings_key::kArControlPanel));
  }

  // Advanced.
  {
    const QSignalBlocker blocker(close_mode_combo_);
    select_data(close_mode_combo_, text(settings_key::kArPanelCloseMode));
  }
  {
    const QSignalBlocker blocker(ar_visibility_checkbox_);
    ar_visibility_checkbox_->setChecked(text(settings_key::kArVisibility) == "hide");
  }

  // AR tools: the spin box shows the scale; "Re-open in AR" is what applies it.
  {
    const QSignalBlocker blocker(ar_panel_scale_spin_);
    ar_panel_scale_spin_->setValue(
      setting_double(effective_settings_, settings_key::kWebPanelScale));
  }

  // Picking: what the signals of the two controls would have done goes to the controller.
  {
    const QSignalBlocker blocker(spatial_target_combo_);
    int target_id = default_spatial_target_id();
    if (pick_target_id_from_word(
        setting_text(effective_settings_, settings_key::kPickTarget), target_id))
    {
      select_data(spatial_target_combo_, target_id);
    }
    spatial_.set_target(spatial_target_from_id(target_id));
  }
  {
    const QSignalBlocker blocker(spatial_frame_edit_);
    spatial_frame_edit_->setText(text(settings_key::kPickFrame));
    spatial_.set_frame_id(text(settings_key::kPickFrame).toStdString());
  }
  refreshSpatialUi();

  // Connection: which device "Invite peer" invites. Only a HoloLens is addressed by name.
  {
    const QSignalBlocker blocker(peer_kind_combo_);
    select_data(peer_kind_combo_, text(settings_key::kInviteDevice));
  }
  {
    const QSignalBlocker blocker(peer_hostname_edit_);
    peer_hostname_edit_->setText(text(settings_key::kInviteHostname));
  }
  peer_hostname_edit_->setEnabled(peer_kind_combo_->currentData().toString() == "hololens");

  // A control whose variable decides says so, and cannot be edited.
  markEnvironmentControl(peer_admission_combo_, resolved(settings_key::kPeerAdmission));
  markEnvironmentControl(
    hidden_displays_checkbox_, resolved(settings_key::kHiddenDisplaysOnConnect));
  markEnvironmentControl(ar_control_checkbox_, resolved(settings_key::kArControlPanel));
  markEnvironmentControl(ar_visibility_checkbox_, resolved(settings_key::kArVisibility));
  markEnvironmentControl(ar_panel_scale_spin_, resolved(settings_key::kWebPanelScale));
  // After the visibility switch above has its value: the close choice is used only without hiding.
  refreshCloseModeControl();
}

void QarStreamingPanel::refreshCloseModeControl()
{
  if (close_mode_combo_ == nullptr) {
    return;
  }
  const ResolvedSetting none;
  const auto found = effective_settings_.find(settings_key::kArPanelCloseMode);
  markEnvironmentControl(
    close_mode_combo_, found != effective_settings_.end() ? found->second : none,
    arVisibilityHides() ? QString::fromLatin1(kCloseModeUnusedToolTip) : QString());
}

void QarStreamingPanel::onSettingEdited(const char * key, const QString & text)
{
  const SettingSpec * spec = find_setting_spec(key);
  if (spec == nullptr) {
    return;
  }
  std::string value;
  std::string error;
  if (!canonicalize_setting(*spec, text.toStdString(), false, value, error)) {
    appendLog(
      QString("SETTINGS: %1 '%2' is %3 - not kept for the RViz config")
      .arg(QString::fromLatin1(key), text, QString::fromStdString(error)));
    return;
  }
  // An edit that changes nothing is not an edit: a line edit reports "finished" when it loses
  // the focus, whatever was typed, and that must not mark the configuration as changed.
  const auto previous = user_settings_.find(key);
  const auto in_force = effective_settings_.find(key);
  std::string current = spec->default_value;
  if (previous != user_settings_.end()) {
    current = previous->second;
  } else if (in_force != effective_settings_.end()) {
    current = in_force->second.value;
  }
  if (current == value) {
    return;
  }
  user_settings_[key] = value;
  resolveSettings();
  appendLog(
    QString("SETTINGS: %1 = %2 (the operator's own value, written by File > Save Config)")
    .arg(QString::fromLatin1(key), QString::fromStdString(value)));
  reportSettings();
  // Either of the two close settings changes what a window that closes does from now on, and
  // turning hiding on or off turns the choice of the close mode off or on.
  syncPanelCloseMode();
  refreshCloseModeControl();
  Q_EMIT configChanged();
}

void QarStreamingPanel::load(const rviz_common::Config & config)
{
  rviz_common::Panel::load(config);

  // Every key of the panel's map goes to the settings unit as text; it knows which keys are
  // its own, which values are acceptable and which keys it has never heard of.
  SettingsText saved;
  for (rviz_common::Config::MapIterator entry = config.mapIterator(); entry.isValid();
    entry.advance())
  {
    saved[entry.currentKey().toStdString()] =
      entry.currentChild().getValue().toString().toStdString();
  }
  SettingsLoadReport report;
  user_settings_ = parse_saved_settings(saved, report);

  if (!user_settings_.empty() || !report.warnings.empty() || !report.unknown_keys.empty()) {
    appendLog(
      QString("SETTINGS: loaded %1 values from the RViz config (version %2)")
      .arg(user_settings_.size()).arg(report.version));
    for (const std::string & warning : report.warnings) {
      appendLog("SETTINGS: " + QString::fromStdString(warning));
    }
    if (!report.unknown_keys.empty()) {
      QStringList names;
      for (const std::string & name : report.unknown_keys) {
        names.append(QString::fromStdString(name));
      }
      appendLog("SETTINGS: ignored unknown keys: " + names.join(", "));
    }
  }

  resolveSettings();
  applySettingsToUi();
  reportSettings();
  syncPanelCloseMode();

  // A configuration written by an older panel restores the dock at the size of that panel. The
  // default size is applied once, a moment from now, when RViz has restored its window state
  // (load() runs before that). A configuration saved by this panel keeps the size it holds.
  if (wants_default_dock_size(report) && !default_dock_size_armed_ &&
    default_dock_timer_ != nullptr)
  {
    default_dock_size_armed_ = true;
    default_dock_saved_version_ =
      report.version > 0 ? QString::number(report.version) : QStringLiteral("none");
    default_dock_timer_->start(kDefaultDockDelayMs);
  }
}

void QarStreamingPanel::applyDefaultDockSize()
{
  const QString version =
    QString("(the config had settings version %1)").arg(default_dock_saved_version_);

  // An AR panel that streams would be rebuilt by a change of its size, and a rebuild is a close
  // and a new QarOS panel: the size is left as it is. Nothing streams at this time unless the
  // timer fired very late.
  if (panel_manager_ && !panel_manager_->panel_keys().empty()) {
    appendLog(
      "PANEL SIZE: default dock size not applied: an AR panel is already streaming and a resize "
      "would rebuild it " + version);
    return;
  }

  // The dock that holds this panel, and the window it is docked in.
  QDockWidget * dock = enclosingDock(parentWidget());
  if (dock == nullptr) {
    appendLog("PANEL SIZE: default dock size not applied: the panel is not in a dock " + version);
    return;
  }
  auto * main_window = qobject_cast<QMainWindow *>(dock->parentWidget());

  // The room the dock may take: the screen for a floating dock, the RViz window for a docked one.
  QSize available;
  if (dock->isFloating()) {
    if (const QScreen * screen = dock->screen()) {
      available = screen->availableGeometry().size();
    }
  } else if (main_window != nullptr) {
    available = main_window->size();
  } else {
    appendLog("PANEL SIZE: default dock size not applied: the dock has no main window " + version);
    return;
  }

  // The hints are the panel's own; the dock is the panel plus its title bar and frame, and the
  // pages only fit when the panel has the height of its hint.
  const QSize chrome = dockChrome(*dock);
  const QSize target = default_dock_size(
    sizeHint() + chrome, available, minimumSizeHint() + chrome);
  if (dock->isFloating()) {
    dock->resize(target);
    logDefaultDockApplied(*dock, target);
    return;
  }
  main_window->resizeDocks({dock}, {target.width()}, Qt::Horizontal);
  giveDockHeight(*dock, QString::fromLatin1(kSelfPanelKey), target.height(), default_dock_hold_);
  if (!default_dock_hold_.active()) {
    logDefaultDockApplied(*dock, target);
    return;
  }
  // The dock shares its column with other docks, or docks around it keep their height: the limits
  // are taken away, and the size said, when the layout has settled.
  default_dock_held_ = dock;
  default_dock_asked_ = target;
  default_dock_release_timer_->start(kDockSettleMs);
}

void QarStreamingPanel::releaseDefaultDockHold()
{
  release_dock_hold(default_dock_hold_);
  QDockWidget * dock = default_dock_held_.data();
  default_dock_held_.clear();
  if (dock == nullptr) {
    return;
  }
  logDefaultDockApplied(*dock, default_dock_asked_);
}

DockHeightResult QarStreamingPanel::giveDockHeight(
  QDockWidget & dock, const QString & label, int height, DockHold & hold)
{
  DockHeightOptions options;
  options.keep = streamedDocksExcept(&dock);
  const DockHeightResult result = set_dock_height(dock, height, hold, options);
  if (result.window_after > result.window_before) {
    appendLog(
      QString("PANEL SIZE: RViz window grown from %1 to %2 px so that '%3' gets %4 px.")
      .arg(result.window_before).arg(result.window_after).arg(label).arg(height));
  }
  if (result.window != WindowGrowth::kNotNeeded && result.window != WindowGrowth::kGrown) {
    const char * reason = "screen bottom";
    if (result.window == WindowGrowth::kMaximized) {
      reason = "maximized";
    } else if (result.window == WindowGrowth::kFullScreen) {
      reason = "full screen";
    } else if (result.window == WindowGrowth::kStreamedElsewhere) {
      reason = "a streamed dock in another column";
    }
    appendLog(
      QString("PANEL SIZE: RViz window cannot grow (%1); '%2' stays %3 px.")
      .arg(QString::fromLatin1(reason), label).arg(result.got));
  }
  return result;
}

std::vector<QDockWidget *> QarStreamingPanel::streamedDocksExcept(const QDockWidget * dock) const
{
  std::vector<QDockWidget *> streamed;
  if (!panel_manager_) {
    return streamed;
  }
  const auto is_streamed = [this](const std::string & key) {
      return panel_manager_->has_panel(key) && !panel_manager_->is_tombstoned(key);
    };
  // This panel's own dock, when this panel is streamed.
  QDockWidget * own = enclosingDock(parentWidget());
  if (own != nullptr && own != dock && is_streamed(kSelfPanelKey)) {
    streamed.push_back(own);
  }
  // The RViz docks, named as collectStreamableEntries() names them.
  if (QWidget * top = window()) {
    for (QDockWidget * other : top->findChildren<QDockWidget *>()) {
      if (other == dock || other == own || other->widget() == nullptr) {
        continue;
      }
      const QString title =
        other->windowTitle().isEmpty() ? other->objectName() : other->windowTitle();
      if (!title.isEmpty() && is_streamed(("dock:" + title).toStdString())) {
        streamed.push_back(other);
      }
    }
  }
  return streamed;
}

QSize QarStreamingPanel::dockChrome(const QDockWidget & dock) const
{
  const QSize extra = dock.size() - size();
  return QSize(
    std::clamp(extra.width(), 0, kMaxDockChromePx),
    std::clamp(extra.height(), 0, kMaxDockChromePx));
}

void QarStreamingPanel::logDefaultDockApplied(const QDockWidget & dock, const QSize & asked)
{
  appendLog(
    QString("PANEL SIZE: default dock size applied: %1 (asked %2; the config had settings "
      "version %3)")
    .arg(sizeWord(dock.size()), sizeWord(asked), default_dock_saved_version_));
}

int QarStreamingPanel::dockRoomHeight(const QDockWidget & dock) const
{
  // The window can grow as far as the screen goes, so the screen is the limit for a docked dock
  // too; where the screen is not known, the window.
  int room = 0;
  if (const QScreen * screen = dock.screen()) {
    room = screen->availableGeometry().height();
  }
  if (room <= 0) {
    if (const auto * window = qobject_cast<const QMainWindow *>(dock.parentWidget())) {
      room = window->height();
    }
  }
  return room > 0 ? room - kDockClearance : std::numeric_limits<int>::max();
}

bool QarStreamingPanel::growDockBeforeStreaming(const QString & key, QWidget * widget)
{
  // The docks RViz hosts and this panel; the hidden Displays copy and the video panels have no
  // dock to grow.
  if (!key.startsWith(QStringLiteral("dock:")) && key != QString::fromLatin1(kSelfPanelKey)) {
    return false;
  }
  QDockWidget * dock = enclosingDock(widget);
  if (dock == nullptr || !dock->isVisible()) {
    return false;
  }

  // The fit at start may still look at the dock of this panel: one fit per dock at a time, so it
  // goes on as the fit of the stream instead of a second one starting beside it.
  if (key == QString::fromLatin1(kSelfPanelKey) && adoptStartFit(widget)) {
    return true;
  }

  // The dock is only ever made higher, as far as its screen goes. Whether a bar shows is not
  // settled by one look: the first one is taken now, the next ones on dock_start_timer_, and the
  // stream is added when two looks in a row agree after the hold on the dock was released.
  FitPolicy policy;
  policy.max_h = dockRoomHeight(*dock);
  PendingDockStart start(policy);
  start.key = key.toStdString();
  start.dock = dock;
  if (advanceDockStart(start) == DockStartStep::Finished) {
    // A dock without content has nothing to fit: the stream goes now.
    return false;
  }
  pending_dock_starts_.push_back(std::move(start));
  // The X of the dock ends the start while it waits (the hook of a stream is put in only when
  // the stream is added, and the dock can be closed before that).
  watchDockClose(key.toStdString(), widget, /*say=*/false);
  // Every start waits the full period, the earlier ones included.
  dock_start_timer_->start(kFitPeriodMs);
  return true;
}

QarStreamingPanel::DockStartStep QarStreamingPanel::advanceDockStart(PendingDockStart & start)
{
  QDockWidget * const dock = start.dock.data();
  QWidget * const content = dock != nullptr ? dock->widget() : nullptr;
  if (content == nullptr) {
    // Nothing to look at; the stream finds out whether its window is still there.
    release_dock_hold(start.hold);
    return DockStartStep::Finished;
  }
  const QString key = QString::fromStdString(start.key);
  const auto say = [this, &key, &start](const std::string & text) {
      if (!start.quiet) {
        appendLog(QString("AR PANEL: '%1' %2").arg(key, QString::fromStdString(text)));
      }
    };

  // The pages of this panel that are not on show are out of the census's sight: what they lack is
  // measured here, with the texts they hold now.
  ExtraNeed extra;
  if (start.key == kSelfPanelKey) {
    const PageShortfall shortfall = page_shortfall(*tabs_, page_contents_);
    extra.px = shortfall.px;
    extra.what = shortfall.page;
  }
  const SettledHeightFit::Step step = start.fit.advance(*content, dock->height(), extra);
  say(describe_fit_round(step));

  switch (step.action) {
    case SettledHeightFit::Action::Resize:
      start.window = giveDockHeight(*dock, key, step.target, start.hold).window;
      return DockStartStep::Waiting;
    case SettledHeightFit::Action::Wait:
      // The first look that finds nothing a taller dock would remove: the hold comes off now, and
      // the next look, a period later, is the one that confirms it, made on a layout that is free
      // to take the height back. That a hold was there is remembered: a dock that is low again at
      // the next look lost its height to the layout, and the last words of the fit say so.
      if (step.height_ok) {
        start.hold_released = start.hold_released || start.hold.active();
        release_dock_hold(start.hold);
      }
      return DockStartStep::Waiting;
    case SettledHeightFit::Action::Done:
    case SettledHeightFit::Action::GaveUp:
      break;
  }

  release_dock_hold(start.hold);
  std::string reason;
  if (step.action == SettledHeightFit::Action::GaveUp) {
    if (step.reason == kFitReasonNoGrowth) {
      reason = dock_growth_reason(start.window, start.hold_released);
    } else if (step.reason == kFitReasonHeight) {
      reason = "the screen height";
    }
  }
  start.result = describe_fit_result(step, dock->height(), reason);
  start.confirmed = step.action == SettledHeightFit::Action::Done && step.honest_no_bar;
  say(start.result);
  return DockStartStep::Finished;
}

void QarStreamingPanel::startPendingDockStreams()
{
  std::vector<PendingDockStart> pending;
  pending.swap(pending_dock_starts_);
  bool waiting = false;
  for (PendingDockStart & start : pending) {
    const QString key = QString::fromStdString(start.key);

    // What happened while the dock was being fitted: the session is not live, or the dock is not
    // on show (hidden by a way the dock hook does not see, or destroyed). The start ends here,
    // with the limits it put on the docks taken off.
    const char * ended = nullptr;
    if (!arSessionLive()) {
      ended = "the QarOS session is not live";
    } else if (start.dock.isNull() || !start.dock->isVisible()) {
      ended = "its dock is not visible";
    }
    if (ended != nullptr) {
      release_dock_hold(start.hold);
      unwatchDockClose(start.key);
      ar_control_dirty_ = true;
      appendLog(
        QString("AR PANEL: '%1' start cancelled (%2) while its dock was being fitted.")
        .arg(key, QString::fromLatin1(ended)));
      continue;
    }

    if (advanceDockStart(start) == DockStartStep::Waiting) {
      pending_dock_starts_.push_back(std::move(start));
      waiting = true;
      continue;
    }

    // The stream is added now, like a click that finds the dock settled. A refusal is the last
    // error, as for any other start; the hook that watched the dock meanwhile goes with it
    // (a stream that was added puts its own hook in).
    dock_start_resumed_ = true;
    streamPanelByKey(key);
    dock_start_resumed_ = false;
    if (panel_manager_ && !panel_manager_->has_panel(start.key)) {
      unwatchDockClose(start.key);
    }
  }
  if (waiting) {
    dock_start_timer_->start(kFitPeriodMs);
  }
}

bool QarStreamingPanel::dockStartWaits(const std::string & manager_key) const
{
  // The hidden Displays copy has no dock; its start waits for the fit of its height.
  if (manager_key == kHiddenDisplaysKey && hidden_displays_fit_) {
    return true;
  }
  return std::any_of(
    pending_dock_starts_.begin(), pending_dock_starts_.end(),
    [&manager_key](const PendingDockStart & start) { return start.key == manager_key; });
}

bool QarStreamingPanel::panelSwitchWaits(const std::string & manager_key) const
{
  return visibility_requests_.count(manager_key) > 0 || dockStartWaits(manager_key);
}

bool QarStreamingPanel::cancelPendingDockStart(
  const std::string & requested_key, const QString & reason)
{
  // A copy, because the caller's reference may be to the key of the start that is taken out of
  // the list below.
  const std::string manager_key = requested_key;
  if (manager_key == kHiddenDisplaysKey) {
    return cancelHiddenDisplaysFit(reason);
  }
  const auto waiting = std::find_if(
    pending_dock_starts_.begin(), pending_dock_starts_.end(),
    [&manager_key](const PendingDockStart & start) { return start.key == manager_key; });
  if (waiting == pending_dock_starts_.end()) {
    return false;
  }
  // The limits the start put on the docks come off, and the hook that watched the dock goes.
  PendingDockStart start = std::move(*waiting);
  pending_dock_starts_.erase(waiting);
  release_dock_hold(start.hold);
  unwatchDockClose(manager_key);
  if (pending_dock_starts_.empty() && dock_start_timer_ != nullptr) {
    dock_start_timer_->stop();
  }
  ar_control_dirty_ = true;
  appendLog(
    QString("AR PANEL: '%1' start cancelled (%2) while its dock was being fitted.")
    .arg(QString::fromStdString(manager_key), reason));
  return true;
}

void QarStreamingPanel::cancelPendingDockStarts(const QString & reason)
{
  // Each cancel edits the list: take the first key until none is left.
  while (!pending_dock_starts_.empty()) {
    cancelPendingDockStart(pending_dock_starts_.front().key, reason);
  }
  cancelHiddenDisplaysFit(reason);
}

void QarStreamingPanel::armStartFit()
{
  if (start_fit_armed_ || start_fit_timer_ == nullptr) {
    return;
  }
  start_fit_armed_ = true;
  // The same wait as for the default dock size, so that the window of RViz has settled.
  start_fit_timer_->start(kDefaultDockDelayMs);
}

void QarStreamingPanel::startFit()
{
  // The default dock size comes first, and its limits on the layout come off before the fit looks
  // at the dock.
  if (default_dock_timer_->isActive() || default_dock_release_timer_->isActive()) {
    start_fit_timer_->start(kFitPeriodMs);
    return;
  }
  QDockWidget * const dock = enclosingDock(parentWidget());
  // The same facts decide at every look: a fit that has begun ends when an AR panel starts to
  // stream, for a change of a streamed size rebuilds its AR panel.
  StartFitFacts facts;
  facts.in_dock = dock != nullptr;
  facts.dock_visible = dock != nullptr && dock->isVisible();
  facts.ar_panel_streams = panel_manager_ && !panel_manager_->panel_keys().empty();
  facts.stream_fit_runs = std::any_of(
    pending_dock_starts_.begin(), pending_dock_starts_.end(),
    [](const PendingDockStart & waiting) { return waiting.key == kSelfPanelKey; });
  const std::string why = start_fit_skip_reason(facts);
  if (!why.empty()) {
    // What the dock has grown stays; the limits the fit put on the docks come off.
    if (start_fit_) {
      release_dock_hold(start_fit_->hold);
      start_fit_.reset();
    }
    appendLog(
      QString("PANEL SIZE: This QAR panel not fitted at start: %1")
      .arg(QString::fromStdString(why)));
    return;
  }

  if (!start_fit_) {
    // The dock is only ever made higher (the policy trims nothing), as far as its screen goes.
    FitPolicy policy;
    policy.max_h = dockRoomHeight(*dock);
    start_fit_ = std::make_unique<PendingDockStart>(policy);
    start_fit_->key = kSelfPanelKey;
    start_fit_->dock = dock;
    start_fit_->quiet = true;
    start_fit_->first_height = dock->height();
  }
  if (advanceDockStart(*start_fit_) == DockStartStep::Waiting) {
    start_fit_timer_->start(kFitPeriodMs);
    return;
  }
  appendLog(
    QString("PANEL SIZE: This QAR panel fitted at start: %1")
    .arg(
      QString::fromStdString(
        describe_start_fit(
          start_fit_->first_height, dock->height(), start_fit_->confirmed, start_fit_->result))));
  start_fit_.reset();
}

bool QarStreamingPanel::adoptStartFit(QWidget * widget)
{
  if (!start_fit_) {
    return false;
  }
  start_fit_timer_->stop();
  PendingDockStart start = std::move(*start_fit_);
  start_fit_.reset();
  // From here on the looks are logged like those of any other start of a stream.
  start.quiet = false;
  appendLog(
    QString("PANEL SIZE: This QAR panel fitted at start: goes on as the fit of its stream "
      "(dock %1 -> %2 px so far)")
    .arg(start.first_height).arg(start.dock.isNull() ? 0 : start.dock->height()));
  pending_dock_starts_.push_back(std::move(start));
  // The X of the dock ends the start while it waits, as for any other stream that waits.
  watchDockClose(kSelfPanelKey, widget, /*say=*/false);
  dock_start_timer_->start(kFitPeriodMs);
  return true;
}

bool QarStreamingPanel::arSessionLive() const
{
  return panel_manager_ != nullptr && (!live_stream_mode_ || live_connected_);
}

void QarStreamingPanel::save(rviz_common::Config config) const
{
  rviz_common::Panel::save(config);

  // The version key and the operator's own values; a value that only the environment supplied
  // is left out, so removing the variable brings the operator's choice back.
  const SettingsText out = settings_to_save(user_settings_, effective_settings_);
  int written = 0;
  for (const auto & [key, value] : out) {
    const QString name = QString::fromStdString(key);
    const QString text = QString::fromStdString(value);
    if (key == kSettingsVersionKey) {
      config.mapSetValue(name, kPanelSettingsVersion);
      continue;
    }
    const SettingSpec * spec = find_setting_spec(key);
    if (spec == nullptr) {
      continue;
    }
    switch (spec->kind) {
      case SettingKind::Int:
        config.mapSetValue(name, text.toInt());
        break;
      case SettingKind::Bool:
        config.mapSetValue(name, text == QStringLiteral("true"));
        break;
      case SettingKind::Double:
        config.mapSetValue(name, std::round(text.toDouble() * 100.0) / 100.0);
        break;
      case SettingKind::Choice:
      case SettingKind::Text:
        config.mapSetValue(name, text);
        break;
    }
    ++written;
  }
  appendLog(
    QString("SETTINGS: saved %1 values (version %2)").arg(written).arg(kPanelSettingsVersion));
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
    // A QarOS session has no simulated peers by default: "Invite peer" invites a physical
    // client head, a real device, into the running session. Its group (the device selector, the
    // host name and the button) is shown in every mode; the button follows the session.
  }
  // The simulated peers are how the offline preview works, and an advanced section of a live
  // session; the Advanced group follows its check box.
  refreshAdvancedVisibility();

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
      // Which graphics card RViz renders the streamed scene on, once, with the other startup lines.
      appendLog(QString::fromStdString(currentRenderDeviceLine()));
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
  // The period of the tick is QAR_PANEL_TICK_MS and nothing else: 0.1 ms when the variable is
  // unset or is not a period (a negative, a huge or a non-numeric text), since the tightest
  // stock setting is the one a frame-timing A/B starts from. The panel has no control for it.
  // QTimer's own interval cannot represent sub-1ms periods; 0.1 ms is realized as a
  // zero-interval timer (fires whenever the event loop is otherwise idle) plus a
  // steady-clock throttle in onRenderTick() that runs the tick body at most once per
  // its nominal period (100us here) - see applyTickPeriodMs().
  {
    const QString qtick_ms = qEnvironmentVariable("QAR_PANEL_TICK_MS");
    const QByteArray qtick_ms_utf8 = qtick_ms.toUtf8();
    // A fallback of -1 tells "the variable gave a period" from "the variable gave nothing usable".
    const double env_tick_ms = qar_qar_adapter::parse_tick_ms_env(
      qtick_ms.isEmpty() ? nullptr : qtick_ms_utf8.constData(), -1.0);
    const bool tick_from_environment = env_tick_ms >= 0.0;
    applyTickPeriodMs(tick_from_environment ? env_tick_ms : 0.1, tick_from_environment);
  }

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

  // From here on the first show of the panel reports its size ("PANEL SIZE:"). A panel that is
  // already on screen has had its first show; report now.
  panel_initialized_ = true;
  if (isVisible()) {
    scheduleSizeReport();
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
      // The box covers size / app_scale metres of the scene. A stated size is the edge of the
      // cube; with only the scale stated the edge is the default one times the scale, so the box
      // keeps holding the same part of the scene (choose_app_volume_size()).
      bool vs_ok = false;
      const float vol_size =
        qEnvironmentVariable("QAR_PANEL_VOLUME_SIZE").trimmed().toFloat(&vs_ok);
      bool vsc_ok = false;
      const float vol_scale =
        qEnvironmentVariable("QAR_PANEL_VOLUME_SCALE").trimmed().toFloat(&vsc_ok);
      const float default_edge_m = cfg.app_volume_width_m;
      const AppVolumeSize volume_size = choose_app_volume_size(
        vs_ok ? std::optional<float>(vol_size) : std::nullopt,
        vsc_ok ? std::optional<float>(vol_scale) : std::nullopt, default_edge_m);
      cfg.app_volume_width_m = volume_size.edge_m;
      cfg.app_volume_length_m = volume_size.edge_m;
      cfg.app_volume_height_m = volume_size.edge_m;
      if (vsc_ok && vol_scale > 0.0f) { cfg.app_volume_scale = vol_scale; }
      bool vy_ok = false;
      const float vol_y = qEnvironmentVariable("QAR_PANEL_VOLUME_Y").trimmed().toFloat(&vy_ok);
      if (vy_ok) { cfg.app_volume_center_y_m = vol_y; }
      // The centre of the box is the RViz ground until QarOS reports where the scene really is
      // (the placement rule of the AR panels falls back to it).
      volume_center_y_m_ = cfg.app_volume_center_y_m;
      // How the panels with a fixed place are hung: from a fixed height of the room (the default)
      // or from the RViz ground.
      const QByteArray fixed_poses_text = qEnvironmentVariable("QAR_PANEL_FIXED_POSES").toUtf8();
      const FixedPoseChoice fixed_poses = parse_fixed_pose_mode(
        fixed_poses_text.isEmpty() ? nullptr : fixed_poses_text.constData());
      fixed_pose_mode_ = fixed_poses.mode;
      if (!fixed_poses.warning.empty()) {
        appendLog(QString("LIVE: WARNING: %1").arg(QString::fromStdString(fixed_poses.warning)));
      }
      if (cfg.create_app_volume) {
        const QString edge_source =
          volume_size.source == AppVolumeSizeSource::Stated ?
          QStringLiteral("QAR_PANEL_VOLUME_SIZE") :
          volume_size.source == AppVolumeSizeSource::FromScale ?
          QString("%1 m x app_scale %2")
            .arg(static_cast<double>(default_edge_m)).arg(static_cast<double>(vol_scale)) :
          QStringLiteral("default size");
        appendLog(QString("LIVE: app volume '%1' %2 m cube (%3) at y=%4 m, app_scale=%5, fixed "
            "panel poses: %6")
          .arg(QString::fromStdString(cfg.app_volume_name))
          .arg(static_cast<double>(cfg.app_volume_width_m))
          .arg(edge_source)
          .arg(static_cast<double>(cfg.app_volume_center_y_m))
          .arg(static_cast<double>(cfg.app_volume_scale))
          .arg(QString::fromLatin1(to_string(fixed_pose_mode_))));
      } else {
        appendLog(QString("LIVE: fixed panel poses: %1")
          .arg(QString::fromLatin1(to_string(fixed_pose_mode_))));
      }
      // The frame QarOS draws around the video of an AR panel (QAR_PANEL_AR_FRAME).
      readArFrame();
      // Which subscription peers may get a stream. The subscription surfaces the
      // runtime's internal app peers too (5 ids for 1 Visualizer), so
      // the default admits only the ones we invited ourselves. 'named' additionally
      // admits peers whose display name matches QAR_PANEL_PEER_NAME_FILTER — the
      // policy for Visualizers that join the session on their own, once a traced run
      // (QAR_ADAPTER_TRACE=1, "peer_spec" lines) shows what they call themselves.
      //
      // The policy is the "Peer Admission" setting AS IT IS NOW: QAR_PANEL_PEER_ADMISSION when
      // it is set and acceptable, else the value saved in the RViz configuration or chosen in
      // the Settings tab, else invited. It is read here, at the first connect of the run,
      // because the factory's configuration is assembled once.
      const QString adm = QString::fromStdString(
        setting_text(effective_settings_, settings_key::kPeerAdmission));
      // A variable the rule refused keeps the error it always had.
      const QString refused_admission = refusedEnvironmentText(settings_key::kPeerAdmission);
      if (!refused_admission.isEmpty()) {
        setLastError(QString("unknown QAR_PANEL_PEER_ADMISSION '%1' "
          "(use invited|new|named|any)").arg(refused_admission));
      }
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
        .arg(adm)
        .arg(cfg.max_peers));
      appendLog(QString("SETTINGS: peer admission '%1' (%2) is what this connect uses.")
        .arg(adm, settingSourceWords(effective_settings_, settings_key::kPeerAdmission)));
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
  // The control page is served here too, with no QarOS panel: Copy page URL opens it in a
  // desktop browser, where the pick commands work and the panel commands answer that QarOS is
  // not connected. Serving the page makes no QarOS call.
  startArControlPage(false);
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
  // The subscription joins the panel's own callback group (see ros_callback_group_).
  rclcpp::SubscriptionOptions options;
  try {
    options.callback_group = rosCallbackGroup();
  } catch (const std::exception & e) {
    appendLog(QString("GEO: no callback group on RViz's node (%1); geolocation disabled for "
      "this session.").arg(e.what()));
    geo_enabled_ = false;
    return;
  }
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
    },
    options);
  appendLog(QString("GEO: listening for NavSatFix on '%1' "
      "(QAR_PANEL_GPS_TOPIC; QAR_PANEL_GEO=off disables).")
    .arg(QString::fromStdString(gps_topic_)));
}

rclcpp::CallbackGroup::SharedPtr QarStreamingPanel::rosCallbackGroup()
{
  if (!ros_callback_group_ && ros_node_) {
    // Added to RViz's executor with the node, so it is spun where the node is (on this thread).
    ros_callback_group_ = ros_node_->create_callback_group(
      rclcpp::CallbackGroupType::MutuallyExclusive, true);
  }
  return ros_callback_group_;
}

void QarStreamingPanel::releaseRosEntities()
{
  // The entities first, then their group: each goes while this library is loaded, and with the
  // group go the weak references to them that it holds (see ros_callback_group_). RViz's executor
  // finds the group expired at its next spin and forgets it. Nothing here waits.
  const bool had_group = ros_callback_group_ != nullptr;
  const std::weak_ptr<rclcpp::CallbackGroup> group = ros_callback_group_;
  for (const std::unique_ptr<ArToolPanel> & tool : ar_tools_) {
    if (tool) {
      tool->subscription.reset();
      tool->publisher.reset();
      tool->goal.reset();
    }
  }
  spatial_publisher_.reset();
  action_bridge_.reset();
  service_bridge_.reset();
  topic_bridge_.reset();
  graph_discovery_.reset();
  gps_sub_.reset();
  ros_callback_group_.reset();
  // The bridges' entities hold the bridges' copies of the node handles, and so the group: the
  // log says whether it is gone now or goes with a holder outside this list.
  if (had_group) {
    appendLog(
      group.expired() ?
      QString("ROS UI: the panel's ROS entities and their callback group on RViz's node are "
      "released.") :
      QString("ROS UI: the panel let its ROS entities go; the callback group on RViz's node is "
      "still held and goes with its last holder."));
  }
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

  // Every way out of a connect refreshes the state line: pending_connect_ was reset above, so
  // CONNECTING must not outlive the attempt, whatever its result.
  if (pending->threw) {
    setLastError(
      QString("QarOS connect threw: %1").arg(QString::fromStdString(pending->error)));
    updateRuntimeStateLabel();
    return;
  }
  if (!pending->factory || !pending->factory->ready()) {
    setLastError(QString("QarOS connect failed: %1").arg(
        pending->factory
        ? QString::fromStdString(pending->factory->last_error())
        : QString("session was not built")));
    updateRuntimeStateLabel();
    return;
  }
  qar_factory_ = pending->factory;
  finishLiveConnect();
}

void QarStreamingPanel::finishLiveConnect()
{
  if (!qar_factory_ || !manager_) {
    updateRuntimeStateLabel();
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
  // How a panel is switched off from now on: hidden (the QarOS panel stays) or closed.
  appendLog(
    arVisibilityHides()
    ? "AR VISIBILITY: hide (panels are hidden and parked in AR, never closed while the session "
      "runs; experimental)"
    : "AR VISIBILITY: close (panels are closed and made again)");

  // The hidden RViz Displays panel goes up with the session, by default: a second
  // instance of RViz's own Displays panel, never shown on the desktop, streamed into AR
  // so the headset can toggle, edit, add and remove the real displays. It is made once
  // the first peer's 3D stream is open (driveLiveStreaming()): a panel made before anything
  // can receive it shows black until the first frame arrives. A failure there (it logs its
  // own and carries on) can never leave the connect half done. Connect can run more than
  // once ("Disconnect (pause)" pauses, "Connect to QarOS" resumes); the start is idempotent.
  hidden_displays_start_pending_ = true;

  // Last: the AR control panel. The page is served now; its QarOS panels are made by the
  // render tick, each in front of its user once that user's head pose is known. Idempotent
  // like the start above: a resume creates nothing new.
  startArControlPage(true);
}

void QarStreamingPanel::startArControlPage(bool live)
{
  if (!setting_bool(effective_settings_, settings_key::kArControlPanel)) {
    appendLog(
      "AR CONTROL PANEL: not created - the setting \"Show the AR control panel when "
      "connecting\" is off");
    refreshArControlStatus(true);
    return;
  }
  const QString error = ensureArControlPage();
  if (!error.isEmpty()) {
    ar_control_page_error_ = error;
    setLastError("AR control panel: " + error);
    appendLog("AR CONTROL PANEL: not created - " + error);
    refreshArControlStatus(true);
    return;
  }
  ar_control_page_error_.clear();
  if (live) {
    // The placement policy starts counting its wait from here; a second call is ignored.
    control_policy_.session_started(steadySeconds());
  }
  refreshArControlStatus(true);
}

void QarStreamingPanel::onStopQar()
{
  // An AR panel whose dock is still being fitted is not started after the session has been
  // stopped.
  cancelPendingDockStarts(
    live_stream_mode_ && live_connected_ ? QStringLiteral("disconnected from QarOS")
                                         : QStringLiteral("the Qar runtime was stopped"));

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

void QarStreamingPanel::onInvitePeer()
{
  // "Invite peer" means "add a physical client head", i.e. invite a real device into the
  // running QarOS session. The button is enabled only while live and connected; a simulated
  // peer has its own button (onAddSimulatedPeer()), so this one never adds one by mistake.
  if (!live_stream_mode_ || !live_connected_) {
    setLastError("Invite peer: not connected to QarOS.");
    return;
  }
  inviteSelectedPeer();
}

void QarStreamingPanel::onAddSimulatedPeer()
{
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
// Test markers
// ---------------------------------------------------------------------------
QarStreamingPanel::TestMarkersSetup QarStreamingPanel::testMarkersSetup() const
{
  TestMarkersSetup setup;
  setup.python = qEnvironmentVariable(kTestMarkersPythonEnv).trimmed();
  setup.script = qEnvironmentVariable(kTestMarkersScriptEnv).trimmed();
  // What is missing, named so that the tool tip tells the operator what to fix.
  const auto missing = [](const char * variable, const QString & path) -> QString {
      if (path.isEmpty()) {
        return QString("%1 is not set").arg(QString::fromLatin1(variable));
      }
      if (!QFileInfo::exists(path)) {
        return QString("the file %1=%2 does not exist").arg(QString::fromLatin1(variable), path);
      }
      return QString();
    };
  QStringList problems;
  const QString python_problem = missing(kTestMarkersPythonEnv, setup.python);
  const QString script_problem = missing(kTestMarkersScriptEnv, setup.script);
  if (!python_problem.isEmpty()) {
    problems.append(python_problem);
  }
  if (!script_problem.isEmpty()) {
    problems.append(script_problem);
  }
  setup.problem = problems.join("; ");
  return setup;
}

void QarStreamingPanel::refreshTestMarkersUi()
{
  if (test_markers_button_ == nullptr) {
    return;
  }
  // The text of the button changes with the state; the size of the panel cannot follow it, because
  // the size hints of the panel are constants.
  if (test_markers_process_ != nullptr) {
    test_markers_button_->setText("Stop test markers");
    test_markers_button_->setEnabled(!test_markers_stop_requested_);
    test_markers_button_->setToolTip(
      test_markers_stop_requested_
      ? QString("The test markers are being stopped.")
      : QString("Stop the test marker publisher that this button started."));
    return;
  }
  const TestMarkersSetup setup = testMarkersSetup();
  test_markers_button_->setText("Start test markers");
  test_markers_button_->setEnabled(setup.problem.isEmpty());
  test_markers_button_->setToolTip(
    setup.problem.isEmpty()
    ? QString("Start the test marker publisher (%1 %2). What it prints goes to the log, one line "
      "a second at most. The button then stops it.").arg(setup.python, setup.script)
    : QString("Test markers are not available: %1. The launchers set both variables when they "
      "find python and the test marker script.").arg(setup.problem));
}

void QarStreamingPanel::onTestMarkersClicked()
{
  if (test_markers_process_ != nullptr) {
    stopTestMarkers();
  } else {
    startTestMarkers();
  }
}

void QarStreamingPanel::startTestMarkers()
{
  if (test_markers_process_ != nullptr) {
    return;
  }
  // The files are looked at again: they may be gone since the panel was built.
  const TestMarkersSetup setup = testMarkersSetup();
  if (!setup.problem.isEmpty()) {
    appendLog("TEST MARKERS: not started - " + setup.problem);
    refreshTestMarkersUi();
    return;
  }

  auto * process = new MarkerProcess(this);
  // One stream for the log: what the script writes to either output channel.
  process->setProcessChannelMode(QProcess::MergedChannels);
  // The environment of this process. Python writes to a pipe in blocks unless it is told not to,
  // and the lines would reach the log only when the script ends.
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert("PYTHONUNBUFFERED", "1");
  process->setProcessEnvironment(environment);

  // Nothing here waits: the process reports through signals, which run on the GUI thread between
  // two events. A handler ignores a process that is not the current one of this panel.
  connect(
    process, &QProcess::started, this,
    [this, setup]() {
      appendLog(QString("TEST MARKERS: started (%1 %2)").arg(setup.python, setup.script));
    });
  connect(
    process, &QProcess::readyReadStandardOutput, this, [this]() { drainTestMarkersOutput(false); });
  connect(
    process, &QProcess::errorOccurred, this,
    [this, process](QProcess::ProcessError error) {
      if (error != QProcess::FailedToStart || process != test_markers_process_) {
        return;  // a crash is reported by the finished signal
      }
      appendLog("TEST MARKERS: could not start (" + process->errorString() + ")");
      test_markers_process_ = nullptr;
      test_markers_stop_requested_ = false;
      QObject::disconnect(process, nullptr, this, nullptr);
      process->deleteLater();
      refreshTestMarkersUi();
    });
  connect(
    process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
    [this, process](int exit_code, QProcess::ExitStatus status) {
      if (process == test_markers_process_) {
        finishTestMarkers(exit_code, status == QProcess::CrashExit);
      }
    });

  test_markers_pending_.clear();
  test_markers_logged_at_ = std::chrono::steady_clock::time_point{};
  test_markers_held_back_ = 0;
  test_markers_held_back_total_ = 0;
  test_markers_stop_requested_ = false;
  test_markers_process_ = process;
  process->start(setup.python, QStringList{setup.script});
  refreshTestMarkersUi();
}

void QarStreamingPanel::stopTestMarkers()
{
  if (test_markers_process_ == nullptr || test_markers_stop_requested_) {
    return;
  }
  test_markers_stop_requested_ = true;
  // kill() asks the system to end the process and returns at once; finishTestMarkers() runs when
  // the event loop reports that it has ended. The button stays off until then.
  test_markers_process_->kill();
  refreshTestMarkersUi();
}

void QarStreamingPanel::drainTestMarkersOutput(bool final_read)
{
  if (test_markers_process_ == nullptr) {
    return;
  }
  test_markers_pending_ += test_markers_process_->readAllStandardOutput();
  int start = 0;
  for (int end = test_markers_pending_.indexOf('\n', start); end >= 0;
    end = test_markers_pending_.indexOf('\n', start))
  {
    logTestMarkersLine(test_markers_pending_.mid(start, end - start));
    start = end + 1;
  }
  test_markers_pending_.remove(0, start);
  // The end of the output, or a script that prints no line breaks at all: what is held is a line.
  if (!test_markers_pending_.isEmpty() &&
    (final_read || test_markers_pending_.size() > static_cast<int>(4 * kTestMarkersLineBytes)))
  {
    logTestMarkersLine(test_markers_pending_);
    test_markers_pending_.clear();
  }
}

void QarStreamingPanel::logTestMarkersLine(const QByteArray & raw)
{
  QByteArray line = raw;
  while (line.endsWith('\r') || line.endsWith(' ')) {
    line.chop(1);
  }
  if (line.isEmpty()) {
    return;
  }
  // At most one line a second reaches the log; the others are counted and the count is said with
  // the next line that is shown, and again when the process ends.
  const auto now = std::chrono::steady_clock::now();
  const bool none_yet = test_markers_logged_at_.time_since_epoch().count() == 0;
  if (!none_yet && now - test_markers_logged_at_ < std::chrono::seconds(1)) {
    ++test_markers_held_back_;
    ++test_markers_held_back_total_;
    return;
  }
  QString text = "TEST MARKERS: " + QString::fromLatin1(
    printableText(std::string(line.constData(), static_cast<std::size_t>(line.size())),
      kTestMarkersLineBytes).c_str());
  if (test_markers_held_back_ > 0) {
    text += QString(" (%1 more line(s) since the last one shown)").arg(test_markers_held_back_);
  }
  test_markers_held_back_ = 0;
  test_markers_logged_at_ = now;
  appendLog(text);
}

void QarStreamingPanel::finishTestMarkers(int exit_code, bool crashed)
{
  QProcess * process = test_markers_process_;
  if (process == nullptr) {
    return;
  }
  drainTestMarkersOutput(true);
  if (test_markers_held_back_total_ > 0) {
    appendLog(
      QString("TEST MARKERS: %1 output line(s) were not shown (at most one line a second is "
        "logged)").arg(test_markers_held_back_total_));
  }
  if (test_markers_stop_requested_) {
    appendLog("TEST MARKERS: stopped");
  } else {
    appendLog(
      QString("TEST MARKERS: exited with code %1%2")
      .arg(exit_code).arg(crashed ? QString(" (the process crashed)") : QString()));
  }
  test_markers_process_ = nullptr;
  test_markers_stop_requested_ = false;
  test_markers_pending_.clear();
  // It is over: nothing more is wanted from it, and it is freed once its own signal has returned.
  QObject::disconnect(process, nullptr, this, nullptr);
  process->deleteLater();
  refreshTestMarkersUi();
}

void QarStreamingPanel::releaseTestMarkersProcess()
{
  QProcess * process = test_markers_process_;
  test_markers_process_ = nullptr;
  test_markers_stop_requested_ = false;
  if (process == nullptr) {
    return;
  }
  // No signal of the process reaches this panel from here on.
  QObject::disconnect(process, nullptr, this, nullptr);
  if (process->state() != QProcess::NotRunning) {
    appendLog("TEST MARKERS: stopped (the panel is closing)");
    // ~QProcess waits for a process that it believes still runs, and the destructor of the panel
    // may not wait. So the process is killed (which returns at once) and the object is told that
    // it is over; the system finishes ending it by itself. The object cannot be left to outlive
    // the panel instead: its virtual table is in this plugin, which RViz may unload straight
    // after the panel is gone, and the event that reports the end of the process would then
    // call into code that has been unloaded.
    process->kill();
    static_cast<MarkerProcess *>(process)->markNotRunning();
  }
  delete process;
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
      // The AR control panel is placed in front of every peer that has a render stream.
      live_peers_.push_back(id);
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

  // 1b) The hidden Displays copy goes up once a peer has a 3D stream.
  startHiddenDisplaysWhenAPeerStreams();

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
    live_peers_.erase(
      std::remove_if(
        live_peers_.begin(), live_peers_.end(),
        [&id](const QarPeerId & tracked) {return qar_ogre_streaming::peer_id_equal(tracked, id);}),
      live_peers_.end());
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
  // And for the AR control panel: its page and its commands need no peer, and every QarOS
  // call it makes belongs on this tick, not on a click or on a control event.
  tickArControlPanel();
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

void QarStreamingPanel::applyTickPeriodMs(double ms, bool from_environment)
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
  appendLog(QString("Render tick: %1 ms (%2)")
    .arg(ms)
    .arg(from_environment ? QStringLiteral("QAR_PANEL_TICK_MS") : QStringLiteral("default")));
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
  // manager.render_tick() renders the due peers that the budget admits inside ONE
  // render lock (the scheduler's batch). A precise BUSY-WAIT is used instead of sleep_for
  // because Windows' ~15.6 ms sleep granularity would otherwise alias the cadence down.
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
  // every heartbeat until it is fixed or retired. The part of every panel ends with the
  // brightness of the frame it sent last (brightness_field()).
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
      // hidden= comes right after notice= and only while a panel is hidden in AR, so a parser
      // of the fields before it reads the line as it always did. parked= follows it, only while
      // a panel is parked (its window closed on the desktop): the check counts parked panels
      // with it.
      if (panels.hidden_panels > 0) {
        line += QString(" hidden=%1").arg(panels.hidden_panels);
      }
      if (panels.parked_panels > 0) {
        line += QString(" parked=%1").arg(panels.parked_panels);
      }
      if (panels.ar_closed_panels > 0) {
        line += QString(" arclosed=%1").arg(panels.ar_closed_panels);
      }
      if (panels.close_panel_failures > 0) {
        line += QString(" CLOSEFAIL=%1")
          .arg(static_cast<qulonglong>(panels.close_panel_failures));
      }
      std::uint64_t visibility_failures = 0;
      for (const auto & pd : panels.panels) {
        visibility_failures += pd.visibility_failures;
      }
      if (visibility_failures > 0) {
        line += QString(" VISFAIL=%1").arg(static_cast<qulonglong>(visibility_failures));
      }
      if (panels.tombstone_cap_refusals > 0) {
        line += QString(" CAPPED=%1")
          .arg(static_cast<qulonglong>(panels.tombstone_cap_refusals));
      }
      for (const auto & pd : panels.panels) {
        QStringList marks;
        if (pd.tombstoned) {
          marks.append("NOTICE");
        }
        if (pd.ar_panel_closed) {
          marks.append("ARCLOSED");
        }
        if (pd.ar_hidden) {
          marks.append("HIDDEN");
        }
        if (pd.parked) {
          marks.append("PARKED");
        }
        line += QString(" | %1%2 sub=%3")
          .arg(QString::fromStdString(pd.key))
          .arg(marks.isEmpty() ? QString() : "[" + marks.join(",") + "]")
          .arg(static_cast<qulonglong>(pd.frames_submitted));
        if (pd.frames_submitted == 0 && pd.active && !pd.ar_panel_closed && !pd.ar_hidden &&
          !pd.parked)
        {
          // nr= is the streak split by cause: <no destination>/<604>/<603>. Only here: the nr=
          // of a panel that is being served stays one number.
          line += QString(" UNSERVED %1s nr=%2 stream=%3")
            .arg(pd.unserved_seconds, 0, 'f', 1)
            .arg(QString::fromStdString(qar_ar_core::not_ready_split_text(pd)))
            .arg(QString::fromStdString(pd.stream_id));
        } else if (pd.not_ready_streak > 0) {
          line += QString(" nr=%1").arg(static_cast<qulonglong>(pd.not_ready_streak));
        }
        // Last in the part, after every tail above: the scorecard reads it from the end.
        line += QString::fromStdString(brightness_field(pd));
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

void QarStreamingPanel::appendLog(const QString & line) const
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
    // One elided line in the status strip; the whole text is its tool tip and the log line below.
    last_error_label_->setFullText("Last error: " + error);
    last_error_label_->setStyleSheet("color: #c0392b;");
  }
  appendLog("ERROR: " + error);
}

void QarStreamingPanel::clearLastError()
{
  last_error_text_.clear();
  if (last_error_label_ != nullptr) {
    last_error_label_->setFullText("Last error: <none>");
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
  // The buttons that follow the state. "Connect to QarOS" is off while a connect is pending
  // (a second click would only log that one is in progress), "Disconnect (pause)" is on while
  // running, "Invite peer" is on while live and connected, and "Stream panel to AR" is on
  // while a QarOS session exists (connected or paused).
  const bool connecting = static_cast<bool>(pending_connect_);
  if (start_qar_button_ != nullptr) { start_qar_button_->setEnabled(!connecting); }
  if (stop_qar_button_ != nullptr) { stop_qar_button_->setEnabled(qar_running_ && !connecting); }
  if (add_peer_button_ != nullptr) {
    // The group is always shown; without a live QarOS session the button says what it needs.
    const bool live_session = live_stream_mode_ && live_connected_;
    add_peer_button_->setEnabled(live_session);
    add_peer_button_->setToolTip(live_session ? kInviteToolTip : kInviteNeedsSessionToolTip);
  }
  if (stream_panel_button_ != nullptr) {
    stream_panel_button_->setEnabled(static_cast<bool>(qar_factory_));
  }
  // The AR control panel's line and its two buttons follow the same states.
  refreshArControlStatus(true);
  // CONNECTING is its own state: the session is being built on a worker thread and
  // the panel stays interactive throughout, so reporting STOPPED for those ~3 s
  // would read as "the click did nothing".
  if (connecting) {
    runtime_state_label_->setFullText("Qar runtime: CONNECTING...");
    runtime_state_label_->setStyleSheet("font-weight: bold; color: #d35400;");
    return;
  }
  runtime_state_label_->setFullText(
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
  const QString error = startPanelManager(std::move(config));
  if (!error.isEmpty()) {
    panel_sender_factory_.reset();
    panel_input_source_.reset();
  }
  return error;
}

QString QarStreamingPanel::startPanelManager(qar_qt_streaming::QtPanelStreamingConfig config)
{
  // The manager's diagnostics: without a sink `emit_diagnostic` has nowhere to go,
  // and every line it produces — panel added, dialog promoted, panel closed, the
  // "never served a frame" warning — is lost from the .err.log. The panel IS the
  // sink, and it forwards to appendLog.
  config.diagnostics = this;
  // The frame of QarOS in every AR panel (QAR_PANEL_AR_FRAME, else the library default); the
  // height of a panel that is placed before it exists comes from the manager (ar_panel_height_m).
  config.ar_frame = ar_frame_;
  // What a closing Qt window does to its AR panel: park it (hidden in AR, nothing closed while
  // the session runs) while "AR Visibility" says hide, else destroy it (the default) or keep it
  // as a closed-window notice, as the "AR Panel Close Mode" setting says
  // (QAR_PANEL_CLOSE_MODE=destroy|notice when set and acceptable; any other text is refused
  // when the settings are resolved, with a SETTINGS: line). The mode is taken here, when the
  // manager starts, and again whenever either setting changes (syncPanelCloseMode()).
  const qar_qt_streaming::PanelCloseModeChoice close_choice = closeModeFromSettings();
  config.close_mode = close_choice.mode;
  if (!close_choice.warning.empty()) {
    appendLog(QString("AR PANEL: %1").arg(QString::fromStdString(close_choice.warning)));
  }
  appendLog(
    QString("AR PANEL: close mode '%1' (%2) - a streamed window that closes %3.")
    .arg(
      QString::fromLatin1(qar_qt_streaming::to_string(close_choice.mode)),
      close_choice.mode == qar_qt_streaming::PanelCloseMode::Park
      ? QString("AR Visibility is hide: %1")
        .arg(settingSourceWords(effective_settings_, settings_key::kArVisibility))
      : settingSourceWords(effective_settings_, settings_key::kArPanelCloseMode),
      closeModeSentence(close_choice.mode)));
  try {
    panel_manager_ = std::make_unique<qar_qt_streaming::QtPanelStreamManager>(config);
  } catch (const std::exception & e) {
    return QString("could not start the AR panel manager: %1").arg(e.what());
  }
  // A dialog that is promoted to a panel of its own (the Add-display box, say) hangs beside its
  // owner; the ground rule keeps its lower edge off the RViz ground like every other AR panel.
  // The manager gives no key here, so the line says "dialog".
  panel_manager_->set_dialog_pose_adjuster(
    [this](const QarPose & top_centre, float height_m) {
      const GroundedPose grounded = liftAboveGround(top_centre, height_m);
      logGroundRaise(QStringLiteral("dialog"), grounded);
      return grounded.pose;
    });
  // A dialog is fitted to its scroll areas on the desktop before its AR panel is created: the
  // manager measures it over a few rounds and streams it once, at the size the fit ends with, so
  // that its QarOS panel is made at the final size and a later change of the dialog's size does
  // not rebuild it. A text browser that scrolls by design is left out of the measure.
  panel_manager_->set_dialog_size_adjuster(
    qar_qt_streaming::QtPanelStreamManager::make_census_dialog_adjuster());
  return QString();
}

QString QarStreamingPanel::closeModeSentence(qar_qt_streaming::PanelCloseMode mode)
{
  switch (mode) {
    case qar_qt_streaming::PanelCloseMode::Destroy:
      return QStringLiteral("has its AR panel closed and its video sender destroyed");
    case qar_qt_streaming::PanelCloseMode::Notice:
      return QStringLiteral(
        "leaves its AR panel up as a closed-window notice and keeps its video sender until the "
        "session ends");
    case qar_qt_streaming::PanelCloseMode::Park:
      break;
  }
  return QStringLiteral(
    "is hidden and parked in AR: nothing is closed while the session runs, its QarOS panel and "
    "video sender are kept until it ends, and opening the window again shows the panel again");
}

qar_qt_streaming::PanelCloseModeChoice QarStreamingPanel::closeModeFromSettings() const
{
  if (arVisibilityHides()) {
    qar_qt_streaming::PanelCloseModeChoice choice;
    choice.mode = qar_qt_streaming::PanelCloseMode::Park;
    choice.explicitly_chosen = true;
    return choice;
  }
  const std::string text = setting_text(effective_settings_, settings_key::kArPanelCloseMode);
  return qar_qt_streaming::parse_panel_close_mode(text.c_str());
}

void QarStreamingPanel::syncPanelCloseMode()
{
  if (!panel_manager_) {
    return;  // ensurePanelManager() takes the mode when the manager starts
  }
  const qar_qt_streaming::PanelCloseMode mode = closeModeFromSettings().mode;
  if (panel_manager_->close_mode() == mode) {
    return;
  }
  panel_manager_->set_close_mode(mode);
  appendLog(
    QString("AR PANEL: close mode is now '%1' - a streamed window that closes %2; panels that "
      "are parked or show a notice stay as they are.")
    .arg(QString::fromLatin1(qar_qt_streaming::to_string(mode)), closeModeSentence(mode)));
}

QarStreamingPanel::GroundedPose QarStreamingPanel::liftAboveGround(
  const QarPose & top_centre, float height_m)
{
  // The scene can be moved, turned and scaled by grabbing it, so the ground is read where it is.
  refreshScenePlacement();
  GroundedPose grounded;
  grounded.ground_room_y = scene_ground_room_y(
    scene_placement_, top_centre.position.x, top_centre.position.z, volume_center_y_m_);
  grounded.raised_m = ground_raise_m(top_centre, height_m, grounded.ground_room_y);
  grounded.pose = keep_above_ground(top_centre, height_m, grounded.ground_room_y);
  return grounded;
}

void QarStreamingPanel::readArFrame()
{
  // QarOS draws the video of a stream panel at the full panel width under its title bar when it
  // fits there, and otherwise scrolls it behind a track that narrows it. The library therefore
  // makes a panel a fit margin taller than its video needs and gives a dialog the width its height
  // needs (qar_qt_streaming's panel_geometry.hpp). The four insets and the margin come from
  // QAR_PANEL_AR_FRAME; QAR_PANEL_AR_HEADROOM, one number, sets the top inset when that is not
  // set.
  const QByteArray frame_text = qEnvironmentVariable("QAR_PANEL_AR_FRAME").toUtf8();
  const QByteArray headroom_text = qEnvironmentVariable("QAR_PANEL_AR_HEADROOM").toUtf8();
  const qar_qt_streaming::PanelFrame defaults{};
  const ArFrameChoice choice = parse_ar_frame(
    frame_text.isEmpty() ? nullptr : frame_text.constData(),
    headroom_text.isEmpty() ? nullptr : headroom_text.constData(),
    ArFrameInsets{
      defaults.top_m, defaults.bottom_m, defaults.left_m, defaults.right_m,
      defaults.fit_margin_m});
  ar_frame_ = qar_qt_streaming::PanelFrame{
    choice.insets.top_m, choice.insets.bottom_m, choice.insets.left_m, choice.insets.right_m,
    choice.insets.fit_margin_m};
  if (!choice.warning.empty()) {
    appendLog(QString("LIVE: WARNING: %1").arg(QString::fromStdString(choice.warning)));
  }
  appendLog(
    QString("LIVE: AR panel frame top %1, bottom %2, left %3, right %4 m, fit margin %5 m (QarOS "
      "draws a stream panel's video at the full panel width under its title bar when it fits "
      "there and otherwise adds a scroll track at the right: a panel is made as tall as the "
      "video needs plus the margin, a dialog as narrow; source: %6%7)")
    .arg(ar_frame_.top_m, 0, 'f', 3)
    .arg(ar_frame_.bottom_m, 0, 'f', 3)
    .arg(ar_frame_.left_m, 0, 'f', 3)
    .arg(ar_frame_.right_m, 0, 'f', 3)
    .arg(ar_frame_.fit_margin_m, 0, 'f', 3)
    .arg(QString::fromLatin1(to_string(choice.source)))
    .arg(choice.headroom_ignored ?
      QStringLiteral(", QAR_PANEL_AR_HEADROOM ignored") : QString()));
}

QarVector3 QarStreamingPanel::fixedPanelPosition(float room_x, float room_z)
{
  // The room is the default: the top edge is at a fixed height and nothing is read. Hung from the
  // ground, the scene is read where it is now (it can be moved by grabbing it).
  if (fixed_pose_mode_ == FixedPoseMode::Ground) {
    refreshScenePlacement();
  }
  return QarVector3{
    room_x,
    fixed_panel_top_y(fixed_pose_mode_, scene_placement_, room_x, room_z, volume_center_y_m_),
    room_z};
}

void QarStreamingPanel::logGroundRaise(const QString & label, const GroundedPose & grounded)
{
  if (!(grounded.raised_m > 0.0f)) {
    return;
  }
  appendLog(
    QString("AR PANEL: '%1' raised by %2 m so that its lower edge lies %3 m above the RViz "
      "ground (room y %4).")
    .arg(label)
    .arg(static_cast<double>(grounded.raised_m), 0, 'f', 2)
    .arg(static_cast<double>(kPanelGroundMarginM), 0, 'f', 2)
    .arg(static_cast<double>(grounded.ground_room_y), 0, 'f', 2));
}

void QarStreamingPanel::markParkedPanelsInCombo()
{
  if (ar_panel_combo_ == nullptr) {
    return;
  }
  for (int i = 0; i < ar_panel_combo_->count(); ++i) {
    // The text the entry has without the mark is kept in the item, so the mark can come and go.
    const QString base = ar_panel_combo_->itemData(i, Qt::UserRole + 1).toString();
    if (base.isEmpty()) {
      continue;
    }
    const bool parked = panel_manager_ &&
      panel_manager_->is_parked(ar_panel_combo_->itemData(i).toString().toStdString());
    const QString wanted = parked ? base + QStringLiteral(" (parked in AR)") : base;
    if (ar_panel_combo_->itemText(i) != wanted) {
      ar_panel_combo_->setItemText(i, wanted);
    }
  }
}

std::vector<QarStreamingPanel::StreamableEntry> QarStreamingPanel::collectStreamableEntries()
{
  std::vector<StreamableEntry> entries;

  // This panel first, so the default selection streams this QAR panel.
  entries.push_back(
    {QString::fromLatin1(kSelfPanelKey), "This QAR panel", "QAR desktop panel",
      QPointer<QWidget>(this)});

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
      entries.push_back({key, title, title, QPointer<QWidget>(content)});
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
    if (!panel.widget || panel.pending) {
      continue;  // a panel that waits for its first frame has no stream to start or stop yet
    }
    const QString topic = QString::fromStdString(video_key.substr(6));
    const QString key = QString::fromStdString(video_key);
    entries.push_back(
      {key, "Video: " + topic, "Video " + topic, QPointer<QWidget>(panel.widget.get())});
  }

  // Last: the hidden Displays copy. Always offered, whether or not the widget exists yet
  // ("Stream panel to AR" creates it - onStreamSelectedPanel special-cases this key and
  // never looks it up here), because "Stop streaming" and the operator closing its AR
  // panel both leave the widget alive and "Stream panel to AR" is how it comes back.
  // QAR_PANEL_HIDDEN_DISPLAYS=off only stops the automatic start on connect.
  entries.push_back(
    {QString::fromLatin1(kHiddenDisplaysKey), hiddenDisplaysLabel(), "Displays (AR copy)",
      QPointer<QWidget>(hidden_displays_.get())});
  return entries;
}

void QarStreamingPanel::refreshStreamablePanels()
{
  const QString previous = ar_panel_combo_->currentData().toString();

  streamable_panels_.clear();
  ar_panel_combo_->clear();
  for (const StreamableEntry & entry : collectStreamableEntries()) {
    streamable_panels_.emplace_back(entry.key, entry.widget);
    ar_panel_combo_->addItem(entry.label, entry.key);
    ar_panel_combo_->setItemData(ar_panel_combo_->count() - 1, entry.label, Qt::UserRole + 1);
  }

  const int restored = ar_panel_combo_->findData(previous);
  if (restored >= 0) {
    ar_panel_combo_->setCurrentIndex(restored);
  }
  markParkedPanelsInCombo();
}

void QarStreamingPanel::onRefreshStreamablePanels()
{
  refreshStreamablePanels();
  appendLog(
    QString("AR PANEL: %1 streamable panel(s) found.").arg(streamable_panels_.size()));
}

void QarStreamingPanel::onStreamSelectedPanel()
{
  streamPanelByKey(ar_panel_combo_->currentData().toString());
}

bool QarStreamingPanel::streamPanelByKey(const QString & key)
{
  if (key.isEmpty()) {
    return false;
  }
  const std::string manager_key = key.toStdString();

  // A dock whose content is being fitted for its stream waits for the layout to settle, at least
  // two looks (growDockBeforeStreaming). Another click on it while it waits does nothing more.
  for (const PendingDockStart & waiting : pending_dock_starts_) {
    if (waiting.key == manager_key) {
      return true;
    }
  }

  // A panel that is hidden in AR is shown again, not made again: its QarOS panel and its video
  // sender are still there. This comes before the special case and the "already streaming"
  // check below, which would both leave it hidden. (A parked panel is hidden too, but it also
  // has to be put back on a window and into the host's lists: it goes the way of a start, and
  // the manager revives it instead of making a second panel.)
  if (panel_manager_ && panel_manager_->has_panel(manager_key) &&
    !panel_manager_->is_tombstoned(manager_key) && !panel_manager_->is_parked(manager_key) &&
    panel_manager_->is_ar_hidden(manager_key))
  {
    if (!requestPanelVisibility(manager_key, true)) {
      setLastError(QString("AR panel: could not show '%1' again.").arg(key));
      return false;
    }
    appendLog(QString("AR PANEL: '%1' was hidden in AR; shown again").arg(key));
    return true;
  }

  // The hidden Displays copy is not an RViz dock to look up: its widget may not exist
  // yet, and "Stream panel to AR" must also clear a stale stream first (see
  // startHiddenDisplaysStream). "Stop streaming" needs no such case: onStopSelectedPanel's
  // generic close only ever tears the STREAM down, never the widget.
  if (key == QString::fromLatin1(kHiddenDisplaysKey)) {
    const QString hidden_error = startHiddenDisplaysStream();
    if (!hidden_error.isEmpty()) {
      setLastError("AR panel: " + hidden_error);
      return false;
    }
    return true;
  }

  // The window is looked up in a fresh walk, the same one that fills the list, so a dock that
  // was opened since the list was last refreshed can be streamed from the AR control panel.
  QWidget * widget = nullptr;
  QString display_name;
  for (const StreamableEntry & entry : collectStreamableEntries()) {
    if (entry.key == key && !entry.widget.isNull()) {
      widget = entry.widget.data();
      display_name = entry.label;
      break;
    }
  }
  if (widget == nullptr) {
    setLastError("AR panel: that panel no longer exists — press Refresh.");
    refreshStreamablePanels();
    return false;
  }

  const QString error = ensurePanelManager();
  if (!error.isEmpty()) {
    setLastError("AR panel: " + error);
    appendLog("AR PANEL: " + error);
    return false;
  }

  // A TOMBSTONED key is not "already streaming" — it is a retired slot showing
  // the panel-closed notice, and starting it again is exactly how that slot
  // gets re-used instead of a second video sender being minted. A PARKED key is no
  // more "already streaming": its window was closed on the desktop, its AR panel is
  // hidden, and starting it again shows that panel again. See QtPanelStreamManager's
  // lifetime table.
  const bool parked = panel_manager_->is_parked(manager_key);
  if (panel_manager_->has_panel(manager_key) && !panel_manager_->is_tombstoned(manager_key) &&
    !parked)
  {
    appendLog(QString("AR PANEL: %1 is already streaming.").arg(key));
    return true;
  }
  const bool reviving = parked || panel_manager_->is_tombstoned(manager_key);

  // A dock is fitted to its content first (made taller where a scroll bar shows), so that its AR
  // copy needs no scroll bar and its QarOS panel is made at the final size. The click returns at
  // once; the stream is added when two looks agree (startPendingDockStreams(), which comes back
  // here without fitting).
  if (!dock_start_resumed_ && growDockBeforeStreaming(key, widget)) {
    return true;
  }

  qar_qt_streaming::PanelStreamOptions options;
  options.display_name = display_name.toStdString();
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
  options.placement.pose.position =
    fixedPanelPosition(panel_slot_x(streamed_panels_.size()), -1.0f);
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  // The lower edge of the panel stays above the RViz ground (the height the manager gives the
  // panel once it is laid out: its video inside the frame of QarOS).
  const GroundedPose grounded = liftAboveGround(
    options.placement.pose, panel_manager_->ar_panel_height_m(*widget, options));
  options.placement.pose = grounded.pose;
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
    return false;
  }
  // One entry per key, whatever the key went through before.
  streamed_panels_.erase(
    std::remove_if(
      streamed_panels_.begin(), streamed_panels_.end(),
      [&manager_key](const std::pair<std::string, QPointer<QWidget>> & entry) {
        return entry.first == manager_key;
      }),
    streamed_panels_.end());
  streamed_panels_.emplace_back(manager_key, QPointer<QWidget>(widget));
  // Closing the RViz dock this widget lives in (its X button) closes the AR panel.
  watchDockClose(manager_key, widget);
  // Variant (b): substitute ROS video for any RViz Image/Camera display whose
  // render window lives inside this dock — a no-op for every other panel.
  attachVideoRegions(manager_key, widget);
  if (parked) {
    // The show is carried out by the next tick. Recorded like every other show, so that a
    // refusal is reported (hiding is the way of the switch: nothing is closed or made again).
    appendLog(
      QString("AR PANEL: '%1' was parked; QarOS is asked to show it again (no new QarOS panel).")
      .arg(key));
    requestPanelVisibility(manager_key, true);
  } else {
    appendLog(
      reviving
      ? QString("AR PANEL: re-using the retired AR panel for '%1' - same QarOS panel, "
        "same video stream, no new sender.").arg(key)
      : QString("AR PANEL: streaming '%1' as a QarOS GUI panel. The QarOS panel itself "
        "is created on the next tick, off this click.").arg(key));
  }
  logGroundRaise(key, grounded);
  markParkedPanelsInCombo();
  updateArPanelStatus();
  return true;
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
  // order"); while "AR Visibility" says hide it is hidden and parked, and neither the QarOS
  // panel nor the sender is touched. Pressed in AR on the very panel it stops, this runs
  // inside that panel's own click delivery; the manager defers the teardown until the click
  // has returned.
  closeStreamedPanel(
    key.toStdString(), "stopped in RViz", "Streaming of this window was stopped in RViz.");
}

bool QarStreamingPanel::closeStreamedPanel(
  const std::string & manager_key, const QString & short_reason, const QString & notice_text)
{
  const QString key = QString::fromStdString(manager_key);

  // A start that still waits for its dock to be fitted ends here: no stream is added after the
  // operator has stopped it, closed its dock or switched it off.
  const bool cancelled = cancelPendingDockStart(manager_key, short_reason);

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
  // A hide or show that was still waiting for the manager's tick has nothing left to wait for.
  visibility_requests_.erase(manager_key);
  ar_control_dirty_ = true;

  if (!panel_manager_) {
    return cancelled;
  }
  const bool was_parked = panel_manager_->is_parked(manager_key);
  const qar_ar_core::Result closed = panel_manager_->close_panel(
    manager_key, short_reason.toStdString(), notice_text.toStdString());
  if (!closed.ok()) {
    // A start that was cancelled had no stream yet: the cancel line is all there is to say.
    if (!cancelled) {
      appendLog(QString("AR PANEL: '%1' was not streaming.").arg(key));
    }
    updateArPanelStatus();
    return cancelled;
  }
  if (was_parked && panel_manager_->is_parked(manager_key)) {
    // A close of a panel that is parked already changes nothing: it stays hidden and kept. When
    // the close mode was switched meanwhile the manager closes it or turns it into a notice, and
    // the line of that mode follows instead.
    appendLog(
      QString("AR PANEL: '%1' (%2): it is parked already; hidden in AR, its QarOS panel and "
        "video sender are kept until the session ends.").arg(key, short_reason));
    updateArPanelStatus();
    return true;
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
  } else if (panel_manager_->close_mode() == qar_qt_streaming::PanelCloseMode::Park) {
    if (panel_manager_->is_parked(manager_key)) {
      appendLog(
        QString("AR PANEL: '%1' parked (%2): hidden in AR; its QarOS panel and video sender "
        "are kept until the session ends.")
        .arg(key, short_reason));
    } else if (panel_manager_->has_panel(manager_key)) {
      // Asked for from inside the panel's own click: the manager parks it when the click
      // returns, and its own line says so.
      appendLog(
        QString("AR PANEL: park requested for '%1' (%2): it is hidden in AR and parked when "
        "the click being delivered has returned; its QarOS panel and video sender are kept "
        "until the session ends.")
        .arg(key, short_reason));
    } else {
      appendLog(
        QString("AR PANEL: '%1' (%2): it had no QarOS panel yet, so there is nothing to "
        "park; starting it again makes one.")
        .arg(key, short_reason));
    }
  } else {
    appendLog(
      QString("AR PANEL: '%1' (%2): its AR panel now shows the window-closed notice and "
      "stays up until you close it in AR (notice mode keeps the video sender until the "
      "session ends).")
      .arg(key, short_reason));
  }
  markParkedPanelsInCombo();
  updateArPanelStatus();
  return true;
}

void QarStreamingPanel::watchDockClose(
  const std::string & manager_key, QWidget * widget, bool say)
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
    if (!say) {
      return;
    }
    // What the close comes to is the close mode's answer at the time of the close.
    QString effect = QStringLiteral("closes its AR panel");
    if (panel_manager_) {
      switch (panel_manager_->close_mode()) {
        case qar_qt_streaming::PanelCloseMode::Destroy:
          break;
        case qar_qt_streaming::PanelCloseMode::Notice:
          effect = QStringLiteral("turns its AR panel into a closed-window notice");
          break;
        case qar_qt_streaming::PanelCloseMode::Park:
          effect = QStringLiteral(
            "hides its AR panel and parks it (its QarOS panel is kept until the session ends)");
          break;
      }
    }
    appendLog(
      QString("AR PANEL: '%1' is in the RViz dock '%2'; closing that dock %3.")
      .arg(QString::fromStdString(manager_key), dock->windowTitle(), effect));
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
  // The "Hidden Displays On Connect" setting as it is now: QAR_PANEL_HIDDEN_DISPLAYS when it is
  // set and acceptable, else the saved value or the Settings tab's choice, else on.
  if (!setting_bool(effective_settings_, settings_key::kHiddenDisplaysOnConnect)) {
    if (!hidden_displays_disabled_logged_) {
      hidden_displays_disabled_logged_ = true;
      const auto found = effective_settings_.find(settings_key::kHiddenDisplaysOnConnect);
      const bool from_environment =
        found != effective_settings_.end() && found->second.source == SettingSource::Environment;
      appendLog(
        QString("AR PANEL: the hidden Displays panel is not started on connect (%1). "
          "'%2' in the panel list still starts it by hand.")
        .arg(
          from_environment
          ? QString("%1=%2").arg(
            QString::fromStdString(found->second.env_name),
            QString::fromStdString(found->second.env_raw).trimmed())
          : QString("the setting \"Start the hidden Displays copy when connecting\" is off"),
          hiddenDisplaysLabel()));
    }
    return;
  }
  startHiddenDisplaysStream();
}

void QarStreamingPanel::startHiddenDisplaysWhenAPeerStreams()
{
  if (!hidden_displays_start_pending_ || live_peers_.empty()) {
    return;
  }
  // A copy made earlier would sit unserved and show black until the first frame arrives. The
  // setting is checked by startHiddenDisplaysStreamOnConnect().
  hidden_displays_start_pending_ = false;
  if (!hidden_displays_ &&
    setting_bool(effective_settings_, settings_key::kHiddenDisplaysOnConnect))
  {
    appendLog(
      "AR PANEL: the hidden Displays copy starts now; it waited for the first peer's 3D "
      "stream so that it is not made before anything can receive it.");
  }
  startHiddenDisplaysStreamOnConnect();
}

QString QarStreamingPanel::startHiddenDisplaysStream()
{
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
    const qar_qt_streaming::OffscreenSizeChoice size =
      hiddenDisplaysSize(qEnvironmentVariable(kHiddenDisplaysSizeEnv));
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

    // The size is the operator's when the variable gave a well-formed size; otherwise (no
    // variable, or a value that is not a size) the copy is fitted to its display tree on a
    // member timer before it streams (advanceHiddenDisplaysFit()), so that its first frame is
    // the final size and shows no scroll bar.
    const bool has_tree = hidden_displays_->findChild<QTreeView *>() != nullptr;
    hidden_displays_fit_width_ = size.width;
    hidden_displays_needs_fit_ = !size.explicitly_chosen && has_tree;

    appendLog(
      QString("AR PANEL: created a hidden RViz '%1' panel (%2): %3x%4 px (%8), shown off-screen "
        "only - it never appears on the desktop - with item dragging off%9 on this copy. It "
        "views RViz's live display tree and will stream as '%5' (\"%6\"). RViz's panel "
        "factory verified by its own RTTI as '%7'.")
      .arg(
        QString::fromStdString(made.class_id), QString::fromStdString(made.class_name))
      .arg(hidden_displays_->width())
      .arg(hidden_displays_->height())
      .arg(key_text, QString::fromLatin1(kHiddenDisplaysPanelName))
      .arg(QString::fromStdString(made.factory_type), hiddenDisplaysFitNote(size, has_tree),
        made.help_area_hidden ? QStringLiteral(" and its help area hidden") : QString()));
  }

  // The copy is made as high as its display tree needs before its stream is made. A fit that is
  // running already is not started twice: the stream follows its end.
  if (hidden_displays_needs_fit_) {
    if (hidden_displays_fit_) {
      appendLog(
        QString("AR PANEL: the hidden Displays copy is being fitted to its display tree; it "
          "streams when the fit is over."));
      return QString();
    }
    if (beginHiddenDisplaysFit()) {
      return QString();
    }
  }
  return streamHiddenDisplaysPanel();
}

bool QarStreamingPanel::beginHiddenDisplaysFit()
{
  if (!hidden_displays_ || !hidden_displays_needs_fit_ || hidden_displays_fit_ ||
    hidden_displays_fit_timer_ == nullptr)
  {
    return false;
  }
  // Between the least and the most height; a tree whose rows take less room than it has makes the
  // copy lower, once; one spare row of room is kept for the rows that appear later.
  FitPolicy policy;
  policy.min_h = kHiddenDisplaysMinFitHeight;
  policy.max_h = kHiddenDisplaysMaxFitHeight;
  policy.row_slack = 1;
  policy.trim_to_rows = true;
  hidden_displays_fit_ = std::make_unique<SettledHeightFit>(policy);
  // The first look comes a period from now: the copy was shown a moment ago, and its tree has not
  // had a turn of the event loop yet.
  hidden_displays_fit_timer_->start(kFitPeriodMs);
  return true;
}

void QarStreamingPanel::advanceHiddenDisplaysFit()
{
  if (!hidden_displays_ || !hidden_displays_fit_) {
    hidden_displays_fit_.reset();
    return;
  }
  if (!arSessionLive()) {
    cancelHiddenDisplaysFit(QStringLiteral("the QarOS session is not live"));
    return;
  }
  const QString key_text = QString::fromLatin1(kHiddenDisplaysKey);
  const SettledHeightFit::Step step =
    hidden_displays_fit_->advance(*hidden_displays_, hidden_displays_->height());
  appendLog(
    QString("AR PANEL: '%1' %2")
    .arg(key_text, QString::fromStdString(describe_fit_round(step))));

  switch (step.action) {
    case SettledHeightFit::Action::Resize:
      hidden_displays_->setFixedSize(hidden_displays_fit_width_, step.target);
      hidden_displays_fit_timer_->start(kFitPeriodMs);
      return;
    case SettledHeightFit::Action::Wait:
      hidden_displays_fit_timer_->start(kFitPeriodMs);
      return;
    case SettledHeightFit::Action::Done:
    case SettledHeightFit::Action::GaveUp:
      break;
  }

  appendLog(
    QString("AR PANEL: '%1' %2")
    .arg(key_text, QString::fromStdString(describe_fit_result(step, hidden_displays_->height()))));
  hidden_displays_fit_.reset();
  hidden_displays_needs_fit_ = false;
  const QString error = streamHiddenDisplaysPanel();
  if (!error.isEmpty()) {
    setLastError("AR panel: " + error);
  }
}

bool QarStreamingPanel::cancelHiddenDisplaysFit(const QString & reason)
{
  if (!hidden_displays_fit_) {
    return false;
  }
  hidden_displays_fit_.reset();
  if (hidden_displays_fit_timer_ != nullptr) {
    hidden_displays_fit_timer_->stop();
  }
  ar_control_dirty_ = true;
  appendLog(
    QString("AR PANEL: '%1' start cancelled (%2) while the copy was being fitted.")
    .arg(QString::fromLatin1(kHiddenDisplaysKey), reason));
  return true;
}

void QarStreamingPanel::watchHiddenDisplaysTree()
{
  if (hidden_displays_tree_watched_ || !hidden_displays_) {
    return;
  }
  QTreeView * const tree = hidden_displays_->findChild<QTreeView *>();
  if (tree == nullptr) {
    return;
  }
  hidden_displays_tree_watched_ = true;
  // The panel is the context of every connection: they end with it, whichever of the three lives
  // longer.
  const auto changed = [this]() {onHiddenDisplaysTreeChanged();};
  connect(tree, &QTreeView::expanded, this, changed);
  connect(tree, &QTreeView::collapsed, this, changed);
  if (const QAbstractItemModel * const model = tree->model()) {
    connect(model, &QAbstractItemModel::rowsInserted, this, changed);
    connect(model, &QAbstractItemModel::rowsRemoved, this, changed);
  }
}

void QarStreamingPanel::onHiddenDisplaysTreeChanged()
{
  // Only while the copy streams, and only the first change of a second starts the wait: the census
  // is taken after the changes of a burst have come in, and a burst of rows earns one census.
  if (!panel_manager_ || hidden_displays_census_timer_ == nullptr ||
    hidden_displays_census_timer_->isActive() || !panel_manager_->has_panel(kHiddenDisplaysKey))
  {
    return;
  }
  hidden_displays_census_timer_->start(kHiddenDisplaysCensusPeriodMs);
}

void QarStreamingPanel::requestHiddenDisplaysCensus()
{
  // The manager asks nothing of a copy that is hidden, parked, closed in AR or a notice.
  if (panel_manager_) {
    panel_manager_->request_scroll_census(kHiddenDisplaysKey, "display tree changed");
  }
}

QString QarStreamingPanel::streamHiddenDisplaysPanel()
{
  const std::string key = kHiddenDisplaysKey;
  const QString key_text = QString::fromLatin1(kHiddenDisplaysKey);
  if (!panel_manager_ || !hidden_displays_) {
    return QStringLiteral("the hidden Displays copy has no panel manager to stream to");
  }

  // The stream's state. A live one is left alone (the idempotent case: "Connect to QarOS"
  // or "Stream panel to AR" pressed again). A stream the OPERATOR closed in AR is not live
  // and not gone either: capture stopped, but the entry stays because its Qt window never
  // closes - nothing would ever retire it. Hiding as the way of the switch asks QarOS to show
  // that panel again and closes nothing; closing as the way of the switch clears the entry
  // here, which is what lets the add below succeed. A retired notice
  // (QAR_PANEL_CLOSE_MODE=notice) and a parked stream need nothing: add_panel revives that
  // slot.
  bool parked = false;
  if (panel_manager_->has_panel(key)) {
    const bool tombstoned = panel_manager_->is_tombstoned(key);
    parked = panel_manager_->is_parked(key);
    bool closed_in_ar = false;
    const qar_ar_core::PanelDiagnosticsSnapshot snapshot = panel_manager_->diagnostics();
    for (const qar_ar_core::PanelStreamDiagnostics & panel : snapshot.panels) {
      if (panel.key == key) {
        closed_in_ar = panel.ar_panel_closed;
        break;
      }
    }
    if (!tombstoned && !parked && !closed_in_ar) {
      appendLog(QString("AR PANEL: %1 is already streaming.").arg(key_text));
      return QString();
    }
    if (!tombstoned && !parked && arVisibilityHides()) {
      appendLog(
        QString("AR PANEL: '%1' was closed in AR; QarOS is asked to show it again. Hiding is "
          "the way of the switch, so the panel is neither closed nor made again.").arg(key_text));
      if (!requestPanelVisibility(key, true)) {
        return QString("QarOS could not be asked to show the panel again");
      }
      markParkedPanelsInCombo();
      return QString();
    }
    if (!tombstoned && !parked) {
      appendLog(
        QString("AR PANEL: '%1' was closed in AR, which stops its capture but leaves its "
          "stream entry; clearing that entry before starting it again.").arg(key_text));
      panel_manager_->close_panel(key, "restarted after being closed in AR");
    }
  }

  qar_qt_streaming::PanelStreamOptions options;
  options.display_name = kHiddenDisplaysPanelName;
  // Left of and clear of the operator's own panels, which step out from x = 0 in slots of
  // kPanelSlotStepM: no call asks QarOS where the operator is looking (qar_ogre_adapter
  // docs/QAROS_INTEGRATION_NOTES.md, "Known SDK limitations"), so the placement is
  // fixed. The dialogs this panel opens (the Add-display box) are
  // placed by the manager 0.5 m to its +X, which lands them between this and the rest.
  options.placement.pose.position = fixedPanelPosition(-1.2f, -1.0f);
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  // The lower edge of the copy stays above the RViz ground (its height in metres is the one the
  // manager gives the panel: its video inside the frame of QarOS).
  const GroundedPose grounded = liftAboveGround(
    options.placement.pose, panel_manager_->ar_panel_height_m(*hidden_displays_, options));
  options.placement.pose = grounded.pose;

  // A new QarOS panel goes under a name no earlier QarOS panel of this process used; the
  // revival of a retired or parked slot creates no QarOS panel and keeps the name it has.
  const bool reviving = parked || panel_manager_->is_tombstoned(key);
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
  watchHiddenDisplaysTree();
  // Deliberately NOT added to streamed_panels_: that list drives the destroyed-widget
  // sweep and the dock-closed hooks, and this widget has neither a dock nor an RViz close
  // to follow. Its stream ends only through "Stop streaming", the operator closing it in
  // AR, or the session ending.
  refreshStreamablePanels();
  if (parked) {
    // The show is carried out by the next tick; recorded so that a refusal is reported.
    appendLog(
      QString("AR PANEL: '%1' was parked; QarOS is asked to show it again (no new QarOS panel).")
      .arg(key_text));
    requestPanelVisibility(key, true);
  }
  appendLog(
    QString("AR PANEL: streaming the hidden Displays panel as '%1' (\"%2\", %3x%4 px) at "
      "room (%5) m. The QarOS panel itself is created on the next tick, off "
      "this call. 'Stop streaming' and 'Stream panel to AR' act on the stream only: the widget "
      "stays shown off-screen.")
    .arg(key_text, QString::fromLatin1(kHiddenDisplaysPanelName))
    .arg(hidden_displays_->width())
    .arg(hidden_displays_->height())
    .arg(vector3Words(options.placement.pose.position)));
  logGroundRaise(key_text, grounded);
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
  // A hide or show asked for since the last tick has been carried out, or refused, by now.
  checkVisibilityOutcomes();
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

  // The panel list follows the manager: an entry whose AR panel is parked says so.
  markParkedPanelsInCombo();

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

  // What the box shows: the heading, the manager-wide input line, the life line and one short line
  // per panel. The long counters of every panel (its size in metres, the supersample ratio, the
  // input it received and delivered, ...) are in the heartbeat of the log: the box holds sixteen
  // lines and may not scroll, so a line that is not needed at the desktop is not shown.
  QStringList head;
  head.append(QStringLiteral("AR panels:"));
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
    input_line += " (" +
      QString::fromStdString(printableText(snapshot.last_input_drop_reason, 60)) + ")";
  }
  head.append(input_line);

  // Live / closed / notice, before the per-panel lines. `live` is what is being
  // streamed now. `closed` is how many panels were closed and their video senders
  // destroyed this session (the default close mode keeps no entry to count them
  // by). `notice` is how many rectangles in the room are closed-window notices
  // instead (QAR_PANEL_CLOSE_MODE=notice) — a tombstoned panel is still a panel
  // QarOS holds a video sender for (qar_ogre_adapter
  // docs/QAROS_INTEGRATION_NOTES.md, "Teardown and destroy order"), so the
  // operator has to be able to see how many are notices and how many are real.
  {
    // `hidden` panels are among the live (or notice) ones: hidden in AR, nothing closed. A
    // `parked` panel is one whose window closed on the desktop; it counts among the live ones
    // and, once QarOS has accepted the hide, among the hidden ones.
    QString life = QString("panels: live=%1").arg(snapshot.live_panels);
    if (snapshot.hidden_panels > 0) {
      life += QString(" (hidden %1)").arg(snapshot.hidden_panels);
    }
    if (snapshot.parked_panels > 0) {
      life += QString(" (parked %1)").arg(snapshot.parked_panels);
    }
    life += QString(" closed=%1 (destroyed) notice=%2")
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
    head.append(life);
  }

  // One short line per panel: its key, the size of its buffer and the frames sent, then only the
  // flags that apply. "waiting for a receiver" is a STATE, not an error, and the log does not say
  // it at 60 Hz, so it has to be legible here or a panel that is streaming to nobody looks
  // identical to one that is working. "rebuilds" counts the new senders a panel needed: 1 is a
  // panel that started once, a climbing number is resize churn.
  QStringList panel_lines;
  for (const auto & panel : snapshot.panels) {
    QString line = QString("%1: %2x%3 px, sent %4")
      .arg(QString::fromStdString(panel.key))
      .arg(panel.buffer_width).arg(panel.buffer_height)
      .arg(static_cast<qulonglong>(panel.frames_submitted));
    QStringList flags;
    if (panel.tombstoned) {
      flags.append(QStringLiteral("notice"));
    }
    if (panel.ar_panel_closed) {
      flags.append(QStringLiteral("closed in AR"));
    }
    if (panel.parked) {
      flags.append(QStringLiteral("parked"));
    } else if (panel.ar_hidden) {
      flags.append(QStringLiteral("hidden"));
    }
    if (!panel.widget_visible && !panel.tombstoned && !panel.parked) {
      flags.append(QStringLiteral("widget hidden"));
    }
    if (panel.not_ready_streak > 0) {
      flags.append(QStringLiteral("waiting for a receiver"));
    }
    if (panel.adapter_rebuilds > 1 || panel.resize_pending) {
      flags.append(QString("rebuilds %1").arg(static_cast<qulonglong>(panel.adapter_rebuilds)));
    }
    if (!panel.last_error.empty()) {
      flags.append(
        QString("error: %1").arg(QString::fromStdString(printableText(panel.last_error, 48))));
    }
    if (!flags.isEmpty()) {
      line += ", " + flags.join(", ");
    }
    panel_lines.append(line);
  }

  const ReadoutBox box{ar_panel_status_scroll_, ar_panel_status_label_};
  set_readout_text(
    box, head, panel_lines,
    QStringLiteral("... and %1 more panel(s); all counters are in the log"), readoutTextWidth());
}

int QarStreamingPanel::readoutTextWidth() const
{
  if (ar_panel_status_scroll_ == nullptr) {
    return 0;
  }
  // The page of the readout is on show: the box knows its own width.
  if (ar_panel_status_scroll_->isVisible()) {
    return ar_panel_status_scroll_->viewport()->width();
  }
  // Another page is on show. A page that is not on show keeps the size it had when it was last
  // laid out, so its box is no guide; all pages share one viewport, and the readout is that wide
  // less the margins of its page and the frame of the box.
  int width = 0;
  if (const auto * current =
    qobject_cast<const QScrollArea *>(tabs_ != nullptr ? tabs_->currentWidget() : nullptr))
  {
    width = current->viewport()->width();
    if (const QWidget * page = ar_panel_status_scroll_->parentWidget()) {
      if (const QLayout * layout = page->layout()) {
        width -= layout->contentsMargins().left() + layout->contentsMargins().right();
      }
    }
    width -= 2 * ar_panel_status_scroll_->frameWidth();
  }
  // A panel that has not been laid out yet: the width its dock starts with, less the margins.
  return width > 100 ? width : kDefaultDockWidth - 60;
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
  qar_ros_ui::NodeHandles handles = qar_ros_ui::NodeHandles::from(*ros_node_);
  if (!handles.valid()) {
    return "RViz's ROS node did not yield the interfaces qar_ros_ui needs";
  }
  try {
    // Every entity of the bridges joins the panel's own group (see ros_callback_group_).
    handles.callback_group = rosCallbackGroup();
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
  // does not care; the spin box next to "Re-open in AR" is the live knob (it shows the
  // "Web Panel Scale" setting, so QAR_PANEL_WEB_SCALE reaches the sizing through that box).
  // No logging here: the note is kept and reported by ensureWebPanelServer(), where the
  // operator can actually read it.
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
  // Reported here rather than where it was read: urlPanelSizing() keeps its note and has no
  // log of its own.
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
    if (!placeArToolPanel(*tool, sized.size)) {
      if (ExposedEntity * row = exposures_.find_by_panel_id(panel_id); row != nullptr) {
        row->last_error = tool->ar_panel->last_error();
      }
    }
  }
  refreshExposureList();
}

bool QarStreamingPanel::placeArToolPanel(ArToolPanel & tool, const QarGuiPanelSize & size)
{
  // The pose of the slot is lifted again for the new height: a panel that became taller reaches
  // further down and may need to go up, and one that became shorter comes back to its slot.
  qar_ar_core::UrlPanelPlacement placement = tool.placement;
  placement.size = size;
  const GroundedPose grounded = liftAboveGround(tool.nominal_pose, size.height_meters);
  placement.pose = grounded.pose;
  if (!tool.ar_panel->set_placement(placement)) {
    return false;
  }
  tool.placement = placement;
  logGroundRaise(QString("tool:%1").arg(QString::fromStdString(tool.entity.name)), grounded);
  return true;
}

void QarStreamingPanel::retireExposure(RosEntityKind kind, const QString & name)
{
  const std::string entity_name = name.toStdString();
  const ExposedEntity * row = exposures_.find(kind, entity_name);
  if (row == nullptr) {
    return;
  }
  const std::string panel_id = row->panel_id;

  // The line for a tool that has no QarOS panel (yet): there is nothing to close or park.
  QString outcome = QString("AR TOOL: retired %1; it had no QarOS panel yet, so nothing was "
    "closed or parked.").arg(name);
  for (auto it = ar_tools_.begin(); it != ar_tools_.end(); ++it) {
    if ((*it)->panel_id != panel_id) {
      continue;
    }
    ArToolPanel & tool = **it;
    if (tool.ar_panel && arVisibilityHides()) {
      // Closing a QarOS panel while a session runs crashed the desktop Visualizer, so the panel
      // is hidden and kept until the session ends (parked_tool_panels_); a new exposure of the
      // same entity takes it over. The hide is a QarOS call, and this is a desktop click: the
      // panel is parked with a pending hide that the next tool tick carries out and logs
      // (hideParkedToolPanels). A hide that QarOS refuses leaves the panel visible, kept all
      // the same: it is reported and never closed here.
      std::string parked_key = parkedToolKey(kind, entity_name);
      while (parked_tool_panels_.count(parked_key) > 0) {
        parked_key += "+";  // one panel per key; two cannot meet in practice
      }
      const bool hidden_already = tool.ar_hidden;
      ParkedToolPanel parked{std::move(tool.ar_panel), tool.placement};
      parked.pending_hide = !hidden_already;
      parked.name = entity_name;
      parked_tool_panels_[parked_key] = std::move(parked);
      if (hidden_already) {
        outcome = QString("AR TOOL: retired %1; its QarOS panel is parked (hidden in AR) until "
          "the session ends.").arg(name);
      } else {
        outcome.clear();  // said by the tick that hides it
      }
    } else if (tool.ar_panel) {
      tool.ar_panel->close();
      outcome = QString("AR TOOL: retired %1.").arg(name);
    }
    if (tool.session && web_panel_server_) {
      // Forgets the token BEFORE closing the viewers, so a viewer's automatic
      // reconnect meets a 404 rather than a half-removed session.
      web_panel_server_->remove_panel(tool.session->token());
    }
    // Dropping the tool drops the subscription / publisher / goal handle with
    // it: every qar_ros_ui entity is RAII and leaves the node when released.
    ar_tools_.erase(it);
    break;
  }
  exposures_.remove(kind, entity_name);
  refreshExposureList();
  ar_control_dirty_ = true;
  if (!outcome.isEmpty()) {
    appendLog(outcome);
  }
}

std::string QarStreamingPanel::parkedToolKey(RosEntityKind kind, const std::string & name)
{
  return std::string(to_string(kind)) + ":" + name;
}

void QarStreamingPanel::hideParkedToolPanels()
{
  for (auto & entry : parked_tool_panels_) {
    if (entry.second.pending_hide) {
      carryOutParkedHide(entry.second);
    }
  }
}

void QarStreamingPanel::carryOutParkedHide(ParkedToolPanel & parked)
{
  parked.pending_hide = false;
  const QString name = QString::fromStdString(parked.name);
  if (parked.adapter->set_state(QAR_GUI_PANEL_STATE_HIDDEN)) {
    appendLog(QString("AR TOOL: retired %1; its QarOS panel is parked (hidden in AR) until "
      "the session ends.").arg(name));
    return;
  }
  const QString refusal = QString::fromStdString(parked.adapter->last_error());
  appendLog(QString("AR TOOL: retired %1; its QarOS panel could not be hidden (%2), so "
    "it stays visible and is kept until the session ends.").arg(name, refusal));
  setLastError(
    QString("AR tool: could not hide the QarOS panel of '%1' (%2); it stays visible and "
      "is kept until the session ends.").arg(name, refusal));
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
    const ArToolPanel * tool = findArTool(row.panel_id);
    if (!row.ar_panel_open) {
      label += "  [pending]";
    } else if (tool != nullptr && tool->ar_hidden) {
      label += "  [hidden in AR]";
    } else {
      label += "  [in AR]";
    }
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
  reopenArTool(*tool, *row);
}

void QarStreamingPanel::reopenArTool(ArToolPanel & tool, ExposedEntity & row)
{
  // The page and its token are untouched in both ways: only the QarOS panel is shown again or
  // made again, and that happens on the next render tick like every other QarOS call.
  if (arVisibilityHides() && tool.ar_panel) {
    tool.pending_show = true;
    tool.pending_hide = false;
    appendLog(QString("AR TOOL: %1 will be shown again on the next tick.")
      .arg(QString::fromStdString(row.entity.name)));
    return;
  }
  if (tool.ar_panel) {
    tool.ar_panel->close();
    tool.ar_panel.reset();
  }
  tool.pending_show = false;
  tool.pending_hide = false;
  tool.ar_hidden = false;
  tool.ar_panel_pending = true;
  tool.pending_logged = false;
  row.ar_panel_open = false;
  refreshExposureList();
  appendLog(QString("AR TOOL: %1 will be re-opened on the next tick.")
    .arg(QString::fromStdString(row.entity.name)));
}

void QarStreamingPanel::showArToolAgain(ArToolPanel & tool)
{
  tool.pending_show = false;
  tool.pending_hide = false;
  if (!tool.ar_panel) {
    return;  // no QarOS panel yet: the pending open on this tick makes one
  }
  ExposedEntity * row = exposures_.find_by_panel_id(tool.panel_id);
  const QString name = "tool:" + QString::fromStdString(tool.entity.name);
  ar_control_dirty_ = true;
  if (tool.ar_panel->set_state(QAR_GUI_PANEL_STATE_VISIBLE)) {
    tool.ar_hidden = false;
    appendLog(QString("AR VISIBILITY: '%1' shown again in AR (no new QarOS panel)").arg(name));
    // The size box next to "Re-open in AR" applies here too, as it does to a new panel.
    if (ar_panel_scale_spin_ != nullptr && web_panel_server_ && tool.session) {
      qar_web_panels::UrlPanelSizing sizing = urlPanelSizing();
      sizing.scale = ar_panel_scale_spin_->value();
      if (tool.viewport) {
        qar_web_panels::use_reported_viewport(sizing, *tool.viewport);
      }
      const qar_web_panels::UrlPanelSizeResult sized = qar_web_panels::size_url_panel_for(
        tool.session->model(), web_panel_server_->config().render.theme, sizing);
      if (std::abs(sized.size.width_meters - tool.placement.size.width_meters) > 0.005f ||
        std::abs(sized.size.height_meters - tool.placement.size.height_meters) > 0.005f)
      {
        placeArToolPanel(tool, sized.size);
      }
    }
    if (row != nullptr) {
      row->ar_panel_open = true;
      row->last_error.clear();
    }
    refreshExposureList();
    return;
  }
  const QString reason = QString::fromStdString(tool.ar_panel->last_error());
  if (arVisibilityHides()) {
    // Hiding is the way of the switch: nothing is closed and nothing is made again, because
    // closing a QarOS panel while a session runs crashed the desktop Visualizer. The panel stays
    // as it is, and the page's switch is told the truth.
    appendLog(
      QString("AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - left as it is (hiding is "
        "the way of the switch: no panel is closed or made again)").arg(name, reason));
    setLastError(
      QString("AR tool: could not show '%1' again (%2); it was not re-created.")
      .arg(QString::fromStdString(tool.entity.name), reason));
    if (row != nullptr) {
      row->last_error = reason.toStdString();
    }
    ar_control_resync_ = true;
    refreshExposureList();
    return;
  }
  // Closing is the way of the switch: QarOS would not show it, so it is closed and made again
  // on this very tick.
  appendLog(QString("AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - re-created it")
    .arg(name, reason));
  tool.ar_panel->close();
  tool.ar_panel.reset();
  tool.ar_hidden = false;
  tool.ar_panel_pending = true;
  tool.pending_logged = false;
  if (row != nullptr) {
    row->ar_panel_open = false;
  }
  refreshExposureList();
}

void QarStreamingPanel::hideArTool(ArToolPanel & tool)
{
  tool.pending_hide = false;
  if (!tool.ar_panel || tool.ar_hidden) {
    return;  // nothing to hide, or hidden already
  }
  ExposedEntity * row = exposures_.find_by_panel_id(tool.panel_id);
  const QString name = "tool:" + QString::fromStdString(tool.entity.name);
  ar_control_dirty_ = true;
  if (tool.ar_panel->set_state(QAR_GUI_PANEL_STATE_HIDDEN)) {
    tool.ar_hidden = true;
    appendLog(
      QString("AR VISIBILITY: '%1' hidden in AR; its QarOS panel is kept").arg(name));
    if (row != nullptr) {
      row->last_error.clear();
    }
    refreshExposureList();
    return;
  }
  // QarOS refused: the panel stays visible, nothing is closed, and the page's switch is told the
  // truth.
  const QString reason = QString::fromStdString(tool.ar_panel->last_error());
  appendLog(
    QString("AR VISIBILITY: set_state(HIDDEN) failed for '%1': %2 - left visible (nothing is "
      "closed)").arg(name, reason));
  setLastError(
    QString("AR tool: could not hide '%1' in AR (%2); it stays visible.")
    .arg(QString::fromStdString(tool.entity.name), reason));
  if (row != nullptr) {
    row->last_error = reason.toStdString();
  }
  ar_control_resync_ = true;
  refreshExposureList();
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
  ensureUrlPanelFactory();
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
  desc.placement.pose.position = fixedPanelPosition(-0.7f * static_cast<float>(slot), -1.2f);
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
  // The lower edge stays above the RViz ground. The pose of the slot is kept, so that a resize
  // can lift it again for the new height.
  tool.nominal_pose = desc.placement.pose;
  const GroundedPose grounded = liftAboveGround(tool.nominal_pose, sized.size.height_meters);
  desc.placement.pose = grounded.pose;
  const QString tool_label = QString("tool:%1").arg(QString::fromStdString(tool.entity.name));

  // A QarOS panel that an earlier exposure of this entity left parked is taken over instead of
  // making a new one.
  switch (takeOverParkedToolPanel(tool, *row, desc.placement, QString::fromStdString(sized.note))) {
    case ParkedToolPanelTakeOver::Done:
      logGroundRaise(tool_label, grounded);
      return QString();
    case ParkedToolPanelTakeOver::Refused:
      return QString::fromStdString(row->last_error);
    case ParkedToolPanelTakeOver::NoPanel:
      break;
  }

  std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> adapter =
    url_panel_factory_->create_url_panel(desc);
  if (!adapter) {
    row->last_error = url_panel_factory_->last_error();
    tool.open_retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    return QString::fromStdString(row->last_error);
  }
  tool.ar_panel = std::move(adapter);
  tool.placement = desc.placement;
  tool.ar_hidden = false;
  tool.ar_panel_pending = false;
  tool.pending_logged = false;
  row->ar_panel_open = true;
  row->last_error.clear();
  refreshExposureList();
  appendLog(QString("AR TOOL: opened %1 in AR at %2 — %3.")
    .arg(QString::fromStdString(tool.entity.name), QString::fromStdString(row->page_url),
      QString::fromStdString(sized.note)));
  logGroundRaise(tool_label, grounded);
  return QString();
}

QarStreamingPanel::ParkedToolPanelTakeOver QarStreamingPanel::takeOverParkedToolPanel(
  ArToolPanel & tool, ExposedEntity & row, const qar_ar_core::UrlPanelPlacement & placement,
  const QString & size_note)
{
  const auto parked = parked_tool_panels_.find(parkedToolKey(tool.entity.kind, tool.entity.name));
  if (parked == parked_tool_panels_.end()) {
    return ParkedToolPanelTakeOver::NoPanel;
  }
  // A hide that the retirement left for the tick comes before the show below, so QarOS sees them
  // in that order and the retire line is logged. (The tool tick hides the parked panels before
  // it opens any tool, so this only runs for a caller outside the tick.)
  if (parked->second.pending_hide) {
    carryOutParkedHide(parked->second);
  }
  // Pointed at the new page, sized and shown. If QarOS refuses a step the panel stays parked,
  // nothing is closed or made, and the attempt is repeated later.
  const std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> adapter = parked->second.adapter;
  std::string step;
  if (!adapter->navigate(row.page_url)) {
    step = "navigate";
  } else if (!adapter->set_placement(placement)) {
    step = "set_placement";
  } else if (!adapter->set_state(QAR_GUI_PANEL_STATE_VISIBLE)) {
    step = "set_state(VISIBLE)";
  }
  if (!step.empty()) {
    row.last_error = "could not take over the parked QarOS panel (" + step + "): " +
      adapter->last_error();
    tool.open_retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    refreshExposureList();
    return ParkedToolPanelTakeOver::Refused;
  }
  tool.ar_panel = adapter;
  parked_tool_panels_.erase(parked);
  tool.placement = placement;
  tool.ar_hidden = false;
  tool.ar_panel_pending = false;
  tool.pending_logged = false;
  row.ar_panel_open = true;
  row.last_error.clear();
  refreshExposureList();
  const QString name = QString::fromStdString(tool.entity.name);
  appendLog(QString("AR TOOL: %1 reuses its parked QarOS panel (no new QarOS panel).").arg(name));
  appendLog(QString("AR TOOL: %1 - %2.").arg(name, size_note));
  return ParkedToolPanelTakeOver::Done;
}

void QarStreamingPanel::ensureUrlPanelFactory()
{
  if (!url_panel_factory_ && qar_factory_) {
    // Borrows the QarSession the render path already owns: a panel belongs to a
    // session, and that is the session the headset is attached to.
    url_panel_factory_ =
      std::make_shared<qar_qar_adapter::QarCApiUrlPanelFactory>(qar_factory_.get());
  }
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
  // First, and whether or not a tool is left: the panels that retirements parked since the last
  // tick are hidden here, ahead of any tool that takes one of them over.
  hideParkedToolPanels();
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
    if (tool->pending_hide) {
      hideArTool(*tool);
    }
    if (tool->pending_show) {
      // Ahead of the open below: when QarOS cannot show the panel again and closing is the way
      // of the switch, it is closed here and made again by that very open.
      showArToolAgain(*tool);
    }
    if (tool->ar_panel_pending && now >= tool->open_retry_at) {
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
  if (session.panel_id() == ar_control::kPanelId) {
    onArControlEvent(session, event);
    return;
  }
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

  if (panel_manager_->is_parked(key)) {
    // The window and its ROS source were kept with the parked panel (tickVideoPanels()): the
    // panel is shown again on them, with the QarOS panel and the sender it already has.
    if (streamPanelByKey(QString::fromStdString(key))) {
      tool.session->ack(event, true, "video panel shown again");
    } else {
      tool.session->ack(event, false, last_error_text_.toStdString());
    }
    return;
  }
  if (panel_manager_->has_panel(key) && !panel_manager_->is_tombstoned(key)) {
    tool.session->ack(event, true, "video panel already open");
    return;
  }
  const auto waiting = video_panels_.find(key);
  if (waiting != video_panels_.end() && waiting->second.pending) {
    tool.session->ack(event, true, "video panel opens when the video arrives");
    return;
  }
  // A tombstoned key cannot be revived onto its OLD widget: reviving means a
  // fresh ROS subscription, and VideoPanelWidget's LatestVideoFrame is fixed at
  // construction (see ros_image_frame_source.hpp). So every path here (first
  // open or revival) builds a brand new {source, widget} pair; add_panel (in
  // startPendingVideoPanel) is what actually revives the tombstoned SLOT (same
  // sender, same QarOS panel) onto this new widget rather than minting a second sender.
  const bool reviving = panel_manager_->is_tombstoned(key);

  std::vector<std::string> advertised_types;
  const auto topic_types = ros_node_->get_topic_names_and_types();
  const auto found_topic = topic_types.find(topic);
  if (found_topic != topic_types.end()) {
    advertised_types = found_topic->second;
  }
  const ImageTransport transport = choose_image_transport(topic, advertised_types);

  VideoPanel entry;
  entry.source = acquireFrameSource(topic, transport);
  entry.widget = std::make_unique<qar_qt_streaming::VideoPanelWidget>(
    entry.source->frames(), QString::fromStdString(topic));
  // The AR panel is made by tickVideoPanels() once the first frame has given the widget the
  // aspect ratio of the camera (startPendingVideoPanel). A panel made now would have the 16:9
  // of a widget that has not seen a frame, and the first frame would resize it: the library
  // then closes the QarOS panel and creates it again, a close in the middle of a session.
  // Replaces (and destroys) any stale entry of this key: a retired slot streams its notice
  // widget, so nothing of the manager refers to the stale video widget.
  entry.pending = true;
  entry.reviving = reviving;
  entry.transport = transport;
  entry.since = std::chrono::steady_clock::now();
  video_panels_[key] = std::move(entry);

  // The page's button is answered at once; the panel follows with the video.
  tool.session->ack(event, true, "video panel opens when the video arrives");
}

QarStreamingPanel::VideoPanelStart QarStreamingPanel::startPendingVideoPanel(
  const std::string & key, VideoPanel & panel)
{
  const std::string topic = key.substr(std::string("video:").size());
  const QString topic_text = QString::fromStdString(topic);
  const std::shared_ptr<qar_qt_streaming::LatestVideoFrame> & frames =
    panel.widget->source();
  const std::shared_ptr<const QImage> frame =
    frames ? frames->latest() : std::shared_ptr<const QImage>();
  if (!frame || frame->isNull()) {
    if (!panel.waiting_logged &&
      std::chrono::steady_clock::now() - panel.since >= std::chrono::seconds(5))
    {
      panel.waiting_logged = true;
      appendLog(
        QString("AR VIDEO: '%1' is waiting for its first frame; its AR panel is made when it "
          "arrives.").arg(topic_text));
    }
    return VideoPanelStart::Waiting;
  }

  // The widget takes the aspect ratio of the camera before the AR panel exists, so that the
  // panel has its final size from its creation.
  panel.widget->sync_to_frame();
  appendLog(
    QString("AR VIDEO: '%1' got its first frame (%2x%3 px); its AR panel is made now.")
    .arg(topic_text).arg(frame->width()).arg(frame->height()));

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
  options.placement.pose.position =
    fixedPanelPosition(panel_slot_x(streamed_panels_.size()), -1.0f);
  options.placement.pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  // The lower edge stays above the RViz ground. The widget has the aspect ratio of the video
  // now, so the height the manager gives the panel (its video inside the frame of QarOS) is the
  // one the panel keeps.
  const GroundedPose grounded = liftAboveGround(
    options.placement.pose, panel_manager_->ar_panel_height_m(*panel.widget, options));
  options.placement.pose = grounded.pose;

  // A new QarOS panel goes under a name no earlier QarOS panel of this process used; the
  // revival of a retired slot creates no QarOS panel and keeps the name it has.
  if (!panel.reviving) {
    applyUniqueDisplayName(key, options);
  }

  const qar_ar_core::Result added = panel_manager_->add_panel(key, panel.widget.get(), options);
  if (!added.ok()) {
    appendLog(
      QString("AR VIDEO: '%1' could not make its AR panel: %2")
      .arg(topic_text, QString::fromStdString(added.message)));
    setLastError(
      QString("AR video: the AR panel of '%1' could not be made (%2).")
      .arg(topic_text, QString::fromStdString(added.message)));
    return VideoPanelStart::Failed;  // the caller drops the entry, and with it source and widget
  }
  logGroundRaise(QString::fromStdString(key), grounded);

  // Drop any stale streamed_panels_ entry for this key AFTER add_panel succeeded, so a failure
  // above leaves bookkeeping exactly as it was.
  streamed_panels_.erase(
    std::remove_if(
      streamed_panels_.begin(), streamed_panels_.end(),
      [&key](const std::pair<std::string, QPointer<QWidget>> & entry) {
        return entry.first == key;
      }),
    streamed_panels_.end());
  streamed_panels_.emplace_back(key, QPointer<QWidget>(panel.widget.get()));
  // Variant (b)'s own attachment: harmless no-op here (a VideoPanelWidget
  // hosts no RViz render window), kept only for uniformity with every other
  // add_panel() call site — see attachVideoRegions()'s header comment.
  attachVideoRegions(key, panel.widget.get());
  panel.pending = false;

  refreshStreamablePanels();
  appendLog(
    QString("AR VIDEO: %1 video panel for '%2' (%3).")
    .arg(panel.reviving ? QString("revived the") : QString("opened a new"))
    .arg(topic_text)
    .arg(QString::fromLatin1(to_string(panel.transport))));
  return VideoPanelStart::Made;
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
    if (panel.pending) {
      // No stream of the manager yet (or only the retired notice of this key): the entry waits
      // for its first frame. The checks below are about streams that exist.
      const VideoPanelStart start = startPendingVideoPanel(key, panel);
      if (start == VideoPanelStart::Waiting) {
        ++it;
        continue;
      }
      if (start == VideoPanelStart::Failed) {
        it = video_panels_.erase(it);
        dropped_one = true;
        continue;
      }
      // Made: the panel is a stream of the manager like any other from here on.
    }
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
    if (panel_manager_->is_parked(key)) {
      // A parked video panel keeps its ROS subscription and its widget, which are bound to each
      // other for life (RosImageFrameSource has no pause), so that showing the panel again needs
      // no new QarOS panel, no new sender and no new window size. Nothing captures it while it
      // is parked, so its aspect ratio is not followed until it is shown again.
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

    if (panel_manager_->is_tombstoned(key) || panel_manager_->is_parked(key)) {
      // Free the ROS subscriptions while the panel shows the closed notice or is parked.
      // The manager's own contract (qt_panel_stream_manager.hpp
      // set_panel_region_source) is that a tombstoned stream's regions simply
      // stop matching and match again on revive, so the registration itself
      // is left alone here. Entries are KEPT (not erased) so the topic/
      // display bookkeeping survives to the next "Stream panel to AR", which
      // subscribes again (attachVideoRegions()).
      const QString what = panel_manager_->is_parked(key)
        ? QStringLiteral("parked") : QStringLiteral("closed");
      for (auto & attachment : attachments) {
        if (attachment.source) {
          appendLog(
            QString("AR PANEL: '%1' %2; stopping its AR video region's ROS subscription.")
            .arg(
              attachment.display ? attachment.display->getName() : QString("(display gone)"),
              what));
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
  // The AR control panel shows the pick state: it follows within one tick.
  ar_control_dirty_ = true;
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
  appendLog(QString("SPATIAL: mode = %1%2")
    .arg(
      QString::fromUtf8(to_string(mode)),
      spatial_mode_from_control_ ? QString(" (from the AR control panel)") : QString()));
  refreshSpatialUi();
}

void QarStreamingPanel::onSpatialTargetChanged(int index)
{
  const int id = spatial_target_combo_->itemData(index).toInt();
  spatial_.set_target(spatial_target_from_id(id));
  onSettingEdited(settings_key::kPickTarget, QString::fromLatin1(pick_target_word(id)));
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
  confirmSpatialPick(nullptr);
}

QString QarStreamingPanel::confirmSpatialPick(std::string * published_topic)
{
  const SpatialPublishRequest request = spatial_.confirm();
  if (published_topic != nullptr) {
    *published_topic = request.topic;
  }
  const QString error = publishSpatialPick(request);
  if (!error.isEmpty()) {
    setLastError(error);
  }
  refreshSpatialUi();
  return error;
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

// ===========================================================================
//  The AR control panel
// ===========================================================================
//
// One web page with the panel's main controls (pick mode, Confirm and Cancel, a switch for each
// streamed panel, a button for each AR tool), shown in AR as a QarOS URL panel in front of each
// user. The page is a qar_ui_model page served by the panel server, like an AR tool; its data
// follows the desktop (ArControlPanelState), and its commands come back through on_control().
//
// The rule that shapes the code: no control event and no click makes a QarOS call. A pick
// command acts at once, because it touches no QarOS state. A panel or tool command is queued, and
// tickArControlPanel() carries it out on the next render tick, together with every other QarOS
// call of the feature (making an instance, hiding it, resizing it).

QString QarStreamingPanel::ensureArControlPage()
{
  if (ar_control_session_) {
    return QString();
  }
  const QString error = ensureWebPanelServer();
  if (!error.isEmpty()) {
    return "panel server: " + error;
  }
  std::string add_error;
  std::shared_ptr<qar_web_panels::PanelSession> session =
    web_panel_server_->add_panel(make_ar_control_page_model(), &add_error);
  if (!session) {
    return QString("panel server refused the page: %1").arg(QString::fromStdString(add_error));
  }
  session->set_control_sink(this);
  // Direct connections: the session lives on this thread, which is also the thread that may
  // touch the QarOS panels. Both slots only record what they were told; the tick acts on it.
  connect(
    session.get(), &qar_web_panels::PanelSession::viewport_reported, this,
    [this](const qar_web_panels::PanelViewportReport & report) {onArControlViewport(report);});
  connect(
    session.get(), &qar_web_panels::PanelSession::client_count_changed, this,
    [this](std::size_t count) {onArControlViewers(count);});

  // How the instances are placed: one per peer, or one for everybody; and whether the yaw is
  // turned by half a turn for a panel that shows its back. Both are read when the page is made.
  QString note;
  const bool per_peer = environmentSwitch("QAR_PANEL_AR_CONTROL_PER_PEER", true, note);
  if (!note.isEmpty()) {
    appendLog("AR CONTROL PANEL: " + note);
    note.clear();
  }
  control_flip_facing_ = environmentSwitch("QAR_PANEL_AR_CONTROL_FLIP", false, note);
  if (!note.isEmpty()) {
    appendLog("AR CONTROL PANEL: " + note);
  }
  control_policy_ = ControlPanelPlacementPolicy(20.0, per_peer);

  ar_control_session_ = session;
  ar_control_state_ = ArControlPanelState();
  ar_control_page_url_ = web_panel_server_->page_url(*session);
  ar_control_dirty_ = true;
  ar_control_snapshot_last_ = SteadyTime{};
  appendLog(
    QString("AR CONTROL PANEL: page served at %1 - %2, facing flip %3")
    .arg(
      QString::fromStdString(ar_control_page_url_),
      per_peer ? QStringLiteral("one panel in front of each peer")
      : QStringLiteral("one shared panel"),
      control_flip_facing_ ? QStringLiteral("on") : QStringLiteral("off")));
  return QString();
}

void QarStreamingPanel::onArControlViewers(std::size_t count)
{
  ar_control_viewers_ = count;
  ar_control_status_dirty_ = true;
  appendLog(QString("AR CONTROL PANEL: page viewers %1").arg(static_cast<qulonglong>(count)));
}

void QarStreamingPanel::onArControlViewport(const qar_web_panels::PanelViewportReport & report)
{
  const bool changed = !ar_control_viewport_ ||
    ar_control_viewport_->css_width != report.viewport.css_width ||
    ar_control_viewport_->css_height != report.viewport.css_height;
  ar_control_viewport_ = report.viewport;
  // The QarOS panels are resized by the next tick, and only when the size moves.
  ar_control_resize_pending_ = true;
  if (changed) {
    // The reason is a text the page chose; the rest of the line is numbers.
    qar_web_panels::PanelViewportReport shown = report;
    shown.viewport.reason = printableText(report.viewport.reason);
    appendLog("AR CONTROL PANEL: " + QString::fromStdString(shown.describe()));
  }
}

// ---------------------------------------------------------------------------
// Hiding panels in AR instead of closing them
// ---------------------------------------------------------------------------
bool QarStreamingPanel::arVisibilityHides() const
{
  return setting_text(effective_settings_, settings_key::kArVisibility) != "close";
}

bool QarStreamingPanel::requestPanelVisibility(const std::string & key, bool visible)
{
  if (!panel_manager_) {
    return false;
  }
  // Only records the request on the stream: the manager's next tick makes the QarOS call.
  const qar_ar_core::Result result = panel_manager_->set_panel_ar_visible(key, visible);
  if (!result.ok()) {
    appendLog(
      QString("AR VISIBILITY: could not ask for '%1' to be %2: %3")
      .arg(
        QString::fromStdString(key),
        visible ? QStringLiteral("shown") : QStringLiteral("hidden"),
        QString::fromStdString(result.message)));
    return false;
  }
  VisibilityRequest request;
  request.visible = visible;
  request.since = std::chrono::steady_clock::now();
  const qar_ar_core::PanelDiagnosticsSnapshot snapshot = panel_manager_->diagnostics();
  if (const auto * panel = findPanelDiagnostics(snapshot, key)) {
    request.changes_seen = panel->visibility_changes;
    request.failures_seen = panel->visibility_failures;
  }
  visibility_requests_[key] = request;
  ar_control_dirty_ = true;
  return true;
}

QString QarStreamingPanel::streamableTitle(const std::string & key)
{
  const QString wanted = QString::fromStdString(key);
  for (const StreamableEntry & entry : collectStreamableEntries()) {
    if (entry.key == wanted) {
      return entry.title;
    }
  }
  return wanted;
}

void QarStreamingPanel::checkVisibilityOutcomes()
{
  if (visibility_requests_.empty() || !panel_manager_) {
    return;
  }
  // The answer is there one tick after the request; a request that has to wait (the panel has
  // no QarOS sender yet) must not make this copy the manager's diagnostics on every tick.
  const SteadyTime now = std::chrono::steady_clock::now();
  if (visibility_checked_.time_since_epoch().count() != 0 &&
    std::chrono::duration<double>(now - visibility_checked_).count() < 0.02)
  {
    return;
  }
  visibility_checked_ = now;
  struct Failure
  {
    std::string key;
    bool visible = true;
    std::string reason;
  };
  std::vector<Failure> failures;
  std::vector<std::string> settled;
  const qar_ar_core::PanelDiagnosticsSnapshot snapshot = panel_manager_->diagnostics();
  for (const auto & [key, request] : visibility_requests_) {
    const qar_ar_core::PanelStreamDiagnostics * panel = findPanelDiagnostics(snapshot, key);
    if (panel == nullptr) {
      settled.push_back(key);  // the panel is gone: nothing left to judge
    } else if (std::chrono::duration<double>(now - request.since).count() > 30.0 &&
      panel->visibility_failures <= request.failures_seen &&
      panel->visibility_changes <= request.changes_seen)
    {
      appendLog(
        QString("AR VISIBILITY: '%1' was asked to be %2 and the manager has not carried it out "
          "in 30 s; the request is dropped")
        .arg(
          QString::fromStdString(key),
          request.visible ? QStringLiteral("shown") : QStringLiteral("hidden")));
      settled.push_back(key);
    } else if (panel->visibility_failures > request.failures_seen) {
      failures.push_back(Failure{key, request.visible, panel->last_visibility_error});
      settled.push_back(key);
    } else if (panel->visibility_changes > request.changes_seen) {
      appendLog(
        request.visible
        ? QString("AR VISIBILITY: '%1' shown again in AR (no new QarOS panel)")
          .arg(QString::fromStdString(key))
        : QString("AR VISIBILITY: '%1' hidden in AR; its QarOS panel and video sender are kept")
          .arg(QString::fromStdString(key)));
      settled.push_back(key);
    } else if (panel->ar_hidden == !request.visible &&
      !(request.visible && panel->ar_panel_closed))
    {
      settled.push_back(key);  // already in that state: the manager asked QarOS for nothing
    }
  }
  for (const std::string & key : settled) {
    visibility_requests_.erase(key);
  }
  if (!settled.empty()) {
    ar_control_dirty_ = true;
  }

  // A hide or show that QarOS refused. While hiding is the way of the switch nothing is closed and
  // nothing is made again (closing a QarOS panel mid-session crashed the desktop Visualizer): the
  // panel stays as it is and the failure is reported. With closing as the way of the switch it
  // falls back to the close setting: a panel that cannot be hidden is closed, and one that cannot
  // be shown is made again (except in close mode notice, where a close keeps the QarOS panel and
  // the panel is left as it is).
  for (const Failure & failure : failures) {
    const QString key = QString::fromStdString(failure.key);
    const QString title = streamableTitle(failure.key);
    const QString reason = failure.reason.empty()
      ? QStringLiteral("QarOS refused the request") : QString::fromStdString(failure.reason);
    bool done = false;
    if (arVisibilityHides()) {
      if (!failure.visible) {
        appendLog(
          QString("AR VISIBILITY: set_state(HIDDEN) failed for '%1': %2 - left visible (hiding "
            "is the way of the switch: nothing is closed)").arg(key, reason));
        setLastError(
          QString("AR panel: could not hide '%1' in AR (%2); it stays visible.")
          .arg(title, reason));
      } else {
        appendLog(
          QString("AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - left as it is (hiding "
            "is the way of the switch: no panel is closed or made again)").arg(key, reason));
        setLastError(
          QString("AR panel: could not show '%1' again (%2); it was not re-created.")
          .arg(title, reason));
      }
    } else if (!failure.visible) {
      appendLog(
        QString("AR VISIBILITY: set_state(HIDDEN) failed for '%1': %2 - closed it instead "
          "(close mode %3)")
        .arg(
          key, reason,
          QString::fromLatin1(qar_qt_streaming::to_string(panel_manager_->close_mode()))));
      done = closeStreamedPanel(
        failure.key, "could not be hidden in AR",
        "Streaming of this window was stopped because it could not be hidden in AR.");
      setLastError(
        QString("AR panel: could not hide '%1' in AR (%2); it was closed instead.")
        .arg(title, reason));
    } else if (panel_manager_->close_mode() != qar_qt_streaming::PanelCloseMode::Destroy) {
      // In close mode notice (and park, which hiding selects) a close keeps the QarOS panel, so
      // closing the panel and starting it again would find the very panel that QarOS would not
      // show. The panel is left as it is, and the page is told so.
      const QString mode =
        QString::fromLatin1(qar_qt_streaming::to_string(panel_manager_->close_mode()));
      appendLog(
        QString("AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - left as it is "
          "(close mode %3 keeps the QarOS panel)")
        .arg(key, reason, mode));
      setLastError(
        QString("AR panel: could not show '%1' again (%2); close mode %3 keeps the panel, "
          "so it was left as it is.")
        .arg(title, reason, mode));
    } else {
      // Destroy mode: the panel is closed and started again, which makes a new QarOS panel. A
      // close that is asked for while a pointer is being delivered into the panel is queued by
      // the manager: the panel is then still there, and starting it would only find it streaming.
      // So "re-created" is only said of a panel that was closed and then started again.
      QString outcome;
      QString consequence;
      const bool closed = closeStreamedPanel(
        failure.key, "could not be shown again in AR",
        "Streaming of this window was stopped because it could not be shown again in AR.");
      if (closed && !panel_manager_->has_panel(failure.key)) {
        done = streamPanelByKey(key);
        if (done) {
          outcome = QStringLiteral("re-created it");
          consequence = QStringLiteral(
            "it was re-created, which has crashed the desktop Visualizer before.");
        } else {
          outcome = QStringLiteral("closed it, but starting it again failed");
          consequence = QStringLiteral("it was closed, but starting it again failed.");
        }
      } else if (closed) {
        outcome = QStringLiteral("not re-created: its close is still under way");
        consequence = QStringLiteral("its close is still under way, so it was not re-created.");
      } else {
        outcome = QStringLiteral("not re-created: it could not be closed");
        consequence = QStringLiteral("it could not be closed, so it was not re-created.");
      }
      appendLog(
        QString("AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - %3")
        .arg(key, reason, outcome));
      setLastError(
        QString("AR panel: could not show '%1' again (%2); %3").arg(title, reason, consequence));
    }
    if (!done) {
      // The page may still show the switch the way the operator turned it.
      for (PendingToggle & toggle : control_toggles_) {
        if (!toggle.tool && toggle.key == failure.key) {
          toggle.failed = true;
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Control events of the page
// ---------------------------------------------------------------------------
void QarStreamingPanel::refuseArControl(
  qar_web_panels::PanelSession & session, const qar_web_panels::PanelControlEvent & event,
  const std::string & error)
{
  session.ack(event, false, error);
  // The AR tools log their refusals under another prefix; this panel has one of its own, so that
  // a refusal of the control panel is not read as a refusal of an AR tool.
  appendLog(
    QString("AR CONTROL PANEL: refused '%1' - %2")
    .arg(
      QString::fromStdString(printableText(event.control.control)),
      QString::fromStdString(error)));
  // The page writes the value it was given into its own copy of the data before it asks, so a
  // refused switch would go on showing the new value. The whole document says what is true.
  const std::vector<qar_ui_model::PatchEntry> truth = ar_control_state_.resync_patch();
  if (!truth.empty()) {
    session.publish(truth);
  }
}

void QarStreamingPanel::acceptArControl(
  qar_web_panels::PanelSession & session, const qar_web_panels::PanelControlEvent & event,
  const std::string & text)
{
  session.ack(event, true, text);
  appendLog(
    QString("AR CONTROL PANEL: '%1' - %2")
    .arg(
      QString::fromStdString(printableText(event.control.control)),
      QString::fromStdString(text)));
  ar_control_dirty_ = true;
}

bool QarStreamingPanel::setPickModeFromControl(ArPickMode mode)
{
  SpatialMode wanted = SpatialMode::Off;
  if (mode == ArPickMode::Point) {
    wanted = SpatialMode::Point;
  } else if (mode == ArPickMode::NavGoal) {
    wanted = SpatialMode::NavGoal;
  }
  const int index = spatial_mode_combo_->findData(static_cast<int>(wanted));
  if (index < 0) {
    return false;
  }
  // Through the desktop's own combo: onSpatialModeChanged() arms the controller, and the combo
  // and the controller stay one. The flag only adds the origin to its log line.
  spatial_mode_from_control_ = true;
  spatial_mode_combo_->setCurrentIndex(index);
  spatial_mode_from_control_ = false;
  return true;
}

void QarStreamingPanel::onArControlEvent(
  qar_web_panels::PanelSession & session, const qar_web_panels::PanelControlEvent & event)
{
  const ControlPanelRouting routing = route_control_panel_event(event.control);
  if (!routing.ok) {
    refuseArControl(session, event, routing.error);
    return;
  }
  const ControlPanelCommand & command = routing.command;
  const bool has_session = qar_factory_ && qar_factory_->ready();

  switch (command.kind) {
    case ControlPanelCommandKind::SetPickMode: {
      if (!setPickModeFromControl(command.pick_mode)) {
        refuseArControl(session, event, "the pick mode is not in the desktop's list");
        return;
      }
      acceptArControl(
        session, event,
        command.pick_mode == ArPickMode::Off ? "picking off"
        : command.pick_mode == ArPickMode::Point ? "Point armed" : "Nav goal armed");
      return;
    }

    case ControlPanelCommandKind::ConfirmPick: {
      if (!spatial_.has_pending()) {
        refuseArControl(session, event, "no pick is waiting for confirmation");
        return;
      }
      std::string topic;
      const QString error = confirmSpatialPick(&topic);
      if (!error.isEmpty()) {
        refuseArControl(session, event, error.toStdString());
        return;
      }
      acceptArControl(session, event, "published to " + topic);
      return;
    }

    case ControlPanelCommandKind::CancelPick: {
      const SpatialInteractionStatus pick = spatial_.status();
      if (!pick.has_pending && pick.state == qar_ar_core::PickState::Idle && pick.error.empty()) {
        acceptArControl(session, event, "nothing to cancel");
        return;
      }
      onSpatialCancel();
      acceptArControl(session, event, "pick dropped");
      return;
    }

    case ControlPanelCommandKind::SetPanelOn: {
      const std::string key = ar_control_state_.panel_key(command.slot);
      if (key.empty()) {
        refuseArControl(session, event, "no panel in this slot");
        return;
      }
      const std::string title = ar_control_state_.panel_title(command.slot);
      PanelArState state = PanelArState::Off;
      if (panel_manager_) {
        const qar_ar_core::PanelDiagnosticsSnapshot snapshot = panel_manager_->diagnostics();
        state = arStateOf(findPanelDiagnostics(snapshot, key));
      }
      if (command.on) {
        if (panel_state_is_on(state)) {
          acceptArControl(session, event, title + " is already on");
          return;
        }
        if (!has_session) {
          refuseArControl(session, event, "connect to QarOS first");
          return;
        }
      } else if (!panel_state_is_on(state)) {
        acceptArControl(session, event, title + " is already off");
        return;
      }
      // The change is carried out by the next render tick; the outcome reaches the page as its
      // state, and a failure as its last error.
      QueuedControlCommand queued;
      queued.kind = ControlPanelCommandKind::SetPanelOn;
      queued.key = key;
      queued.title = title;
      queued.on = command.on;
      control_commands_.push_back(queued);
      const bool shows = state == PanelArState::Hidden ||
        (state == PanelArState::ClosedInAr && arVisibilityHides()) ||
        (panel_manager_ && panel_manager_->is_parked(key));
      acceptArControl(
        session, event,
        (command.on ? (shows ? "showing " : "starting ")
         : (arVisibilityHides() ? "hiding " : "stopping ")) + title);
      return;
    }

    case ControlPanelCommandKind::SetToolOn: {
      const std::string panel_id = ar_control_state_.tool_panel_id(command.slot);
      const ArToolPanel * tool = panel_id.empty() ? nullptr : findArTool(panel_id);
      if (tool == nullptr) {
        refuseArControl(session, event, "no tool in this slot");
        return;
      }
      if (!has_session) {
        refuseArControl(session, event, "connect to QarOS first");
        return;
      }
      const std::string title = ar_control_state_.tool_title(command.slot);
      // On means the tool's QarOS panel exists and is shown. A tool whose panel is not made yet
      // reads off, and the render tick makes the panel without being asked.
      const bool shown = tool->ar_panel && !tool->ar_hidden;
      if (command.on && shown) {
        acceptArControl(session, event, title + " is already on");
        return;
      }
      if (!command.on && !shown) {
        acceptArControl(session, event, title + " is already off");
        return;
      }
      // The change is carried out by the next render tick; the outcome reaches the page as its
      // state, and a failure as its last error.
      QueuedControlCommand queued;
      queued.kind = ControlPanelCommandKind::SetToolOn;
      queued.key = panel_id;
      queued.title = title;
      queued.on = command.on;
      control_commands_.push_back(queued);
      acceptArControl(session, event, (command.on ? "showing " : "hiding ") + title);
      return;
    }

    case ControlPanelCommandKind::None:
      break;
  }
  refuseArControl(session, event, "nothing to do");
}

// ---------------------------------------------------------------------------
// The render tick
// ---------------------------------------------------------------------------
void QarStreamingPanel::tickArControlPanel()
{
  if (!ar_control_session_) {
    return;
  }
  const SteadyTime now = std::chrono::steady_clock::now();
  // Commands first, so that what they did reaches the page with this very tick's state.
  runArControlCommands(now);
  tickArControlPlacement(now);
  applyArControlShow(now);
  applyArControlResize();
  publishArControlState(now);
  refreshArControlStatus(false);
}

void QarStreamingPanel::runArControlCommands(SteadyTime now)
{
  if (control_commands_.empty()) {
    return;
  }
  std::vector<QueuedControlCommand> commands;
  commands.swap(control_commands_);
  for (const QueuedControlCommand & command : commands) {
    if (command.kind == ControlPanelCommandKind::SetPanelOn) {
      runPanelSwitch(command, now);
    } else if (command.kind == ControlPanelCommandKind::SetToolOn) {
      runToolSwitch(command, now);
    }
  }
  ar_control_dirty_ = true;
}

void QarStreamingPanel::runToolSwitch(const QueuedControlCommand & command, SteadyTime now)
{
  ArToolPanel * tool = findArTool(command.key);
  ExposedEntity * row = exposures_.find_by_panel_id(command.key);
  if (tool == nullptr || row == nullptr) {
    return;  // the tool has been retired since the command was queued
  }
  // Switching a tool on shows its QarOS panel and switching it off hides it; the exposure, the
  // page and the ROS entity stay either way, and nothing is closed. The QarOS call is made by
  // the render tick that follows (tickArToolPanels()). A tool whose panel is not made yet needs
  // no request: the tick makes it.
  if (command.on) {
    if (tool->ar_panel && tool->ar_hidden) {
      tool->pending_show = true;
      tool->pending_hide = false;
      appendLog(QString("AR TOOL: %1 will be shown again on the next tick.")
        .arg(QString::fromStdString(row->entity.name)));
    }
  } else if (tool->ar_panel && !tool->ar_hidden) {
    tool->pending_hide = true;
    tool->pending_show = false;
    appendLog(QString("AR TOOL: %1 will be hidden on the next tick.")
      .arg(QString::fromStdString(row->entity.name)));
  }

  // The page has already drawn the switch the way the operator turned it: remember it, and tell
  // the page the truth if the tool does not end up that way.
  control_toggles_.erase(
    std::remove_if(
      control_toggles_.begin(), control_toggles_.end(),
      [&command](const PendingToggle & toggle) {
        return toggle.tool && toggle.key == command.key;
      }),
    control_toggles_.end());
  PendingToggle toggle;
  toggle.key = command.key;
  toggle.tool = true;
  toggle.on = command.on;
  toggle.at = now;
  control_toggles_.push_back(toggle);
}

void QarStreamingPanel::runPanelSwitch(const QueuedControlCommand & command, SteadyTime now)
{
  const std::string & key = command.key;
  const QString title = QString::fromStdString(command.title);
  PanelArState state = PanelArState::Off;
  if (panel_manager_) {
    const qar_ar_core::PanelDiagnosticsSnapshot snapshot = panel_manager_->diagnostics();
    state = arStateOf(findPanelDiagnostics(snapshot, key));
  }

  bool done = true;
  if (command.on) {
    switch (state) {
      case PanelArState::Starting:
      case PanelArState::Streaming:
        break;  // it came on while the command waited
      case PanelArState::Hidden:
        done = requestPanelVisibility(key, true);
        break;
      case PanelArState::ClosedInAr:
        if (arVisibilityHides()) {
          // Can QarOS show a panel that was closed in AR? If it cannot, the answer comes back
          // through checkVisibilityOutcomes(), which reports it and leaves the panel as it is:
          // while hiding is the way of the switch nothing is closed or made again.
          done = requestPanelVisibility(key, true);
        } else {
          closeStreamedPanel(
            key, "restarted from the AR control panel",
            "Streaming of this window was stopped to start it again from the AR control panel.");
          done = streamPanelByKey(QString::fromStdString(key));
        }
        break;
      case PanelArState::Off:
        done = streamPanelByKey(QString::fromStdString(key));
        break;
    }
    if (!done && state != PanelArState::Off) {
      setLastError(QString("AR panel: could not show '%1' again.").arg(title));
    }
  } else if (panel_state_is_on(state)) {
    if (arVisibilityHides()) {
      done = requestPanelVisibility(key, false);
      if (!done) {
        setLastError(QString("AR panel: could not hide '%1' in AR.").arg(title));
      }
    } else {
      done = closeStreamedPanel(
        key, "stopped from the AR control panel",
        "Streaming of this window was stopped from the AR control panel.");
    }
  } else if (dockStartWaits(key)) {
    // The panel is not made yet: its dock is being fitted first. Switched off, it is not made.
    cancelPendingDockStart(key, QStringLiteral("switched off from the AR control panel"));
  }

  // The page has already drawn the switch the way the operator turned it: remember it, and tell
  // the page the truth if the panel does not end up that way.
  control_toggles_.erase(
    std::remove_if(
      control_toggles_.begin(), control_toggles_.end(),
      [&key](const PendingToggle & toggle) {return !toggle.tool && toggle.key == key;}),
    control_toggles_.end());
  PendingToggle toggle;
  toggle.key = key;
  toggle.on = command.on;
  toggle.failed = !done;
  toggle.at = now;
  control_toggles_.push_back(toggle);
}

// ---------------------------------------------------------------------------
// Where the instances go, and for whom
// ---------------------------------------------------------------------------
QarStreamingPanel::ArControlInstance * QarStreamingPanel::findControlInstance(
  const std::string & key)
{
  for (ArControlInstance & instance : control_instances_) {
    if (instance.key == key) {
      return &instance;
    }
  }
  return nullptr;
}

void QarStreamingPanel::tickArControlPlacement(SteadyTime now)
{
  // An instance that could not be made is tried again, slowly: each attempt is a synchronous
  // QarOS call.
  for (ArControlInstance & instance : control_instances_) {
    if (!instance.adapter && now >= instance.next_attempt) {
      createArControlInstance(instance, now);
    }
  }

  // Placement is for a connected QarOS session with a peer that is being streamed to.
  if (!live_stream_mode_ || !live_connected_ || !manager_ || !qar_factory_) {
    return;
  }
  if (control_placement_last_.time_since_epoch().count() != 0 &&
    std::chrono::duration<double>(now - control_placement_last_).count() < 0.1)
  {
    return;
  }
  control_placement_last_ = now;

  ControlPanelPlacementParams params;
  params.flip_facing = control_flip_facing_;
  const bool shared = control_policy_.mode() == ControlPanelMode::Shared;

  struct Head
  {
    QarPeerId peer{};
    QarVector3 head{};
    QarVector3 gaze{};
  };
  std::vector<Head> heads;
  std::vector<ControlPanelPeer> peers;
  bool placement_read = false;
  for (const QarPeerId & id : live_peers_) {
    qar_ogre_streaming::OgreQarStereoStream * stream = manager_->get_stream(id);
    if (stream == nullptr) {
      continue;
    }
    ControlPanelPeer peer;
    peer.peer = id;
    // The pose only decides where a NEW instance goes: once the instance of a peer (or the
    // shared one) exists there is nothing left to compute for it.
    const bool has_instance =
      findControlInstance(shared ? std::string(kSharedControlPanelKey)
        : control_panel_instance_key(id)) != nullptr;
    QarPose left{};
    QarPose right{};
    if (!has_instance && stream->last_eye_poses(left, right)) {
      if (!placement_read && !spatial_gesture_source_) {
        // The gesture source reads the placement every tick; without it, read it now.
        refreshScenePlacement();
        placement_read = true;
      }
      if (!scene_placement_.known && !control_scene_unknown_logged_) {
        control_scene_unknown_logged_ = true;
        appendLog(
          "AR CONTROL PANEL: the app volume has not reported its placement; the head pose is "
          "taken as it is (identity placement)");
      }
      Head head;
      head.peer = id;
      QarPose pose{};
      if (head_from_eye_poses(left, right, scene_placement_, head.head, head.gaze) &&
        place_in_front_of(head.head, head.gaze, params, pose))
      {
        peer.has_pose = true;
        peer.panel_pose = pose;
        heads.push_back(head);
      }
    }
    peers.push_back(peer);
  }

  // An instance that is created without the pose of a viewer hangs from a fixed place as well.
  if (fixed_pose_mode_ == FixedPoseMode::Ground) {
    control_policy_.set_fallback_top_y(fixedPanelPosition(0.7f, -1.2f).y);
  }
  const std::vector<ControlPanelInstanceRequest> requests =
    control_policy_.tick(steadySeconds(), peers);
  for (const ControlPanelInstanceRequest & request : requests) {
    ArControlInstance instance;
    instance.key = request.instance_key;
    instance.request = request;
    for (const Head & head : heads) {
      if (qar_ogre_streaming::peer_id_equal(head.peer, request.peer)) {
        instance.has_head = true;
        instance.head = head.head;
        instance.gaze = head.gaze;
        break;
      }
    }
    control_instances_.push_back(std::move(instance));
    createArControlInstance(control_instances_.back(), now);
  }

  // A peer whose stream closed has its panel hidden, and shown again when the stream returns. In
  // the "close" way of the switch the panel simply stays.
  for (const ControlPanelVisibilityChange & change : control_policy_.take_visibility_changes()) {
    ArControlInstance * instance = findControlInstance(change.instance_key);
    if (instance == nullptr) {
      continue;
    }
    instance->wanted_visible = change.visible;
    if (instance->adapter && arVisibilityHides()) {
      applyInstanceVisibility(*instance, change.visible);
    }
  }
}

qar_web_panels::UrlPanelSizeResult QarStreamingPanel::arControlPanelSize()
{
  // As an AR tool: from the page's own measurement and the size box, then from the viewport the
  // page reports once it has been laid out in the headset.
  qar_web_panels::UrlPanelSizing sizing = urlPanelSizing();
  if (ar_panel_scale_spin_ != nullptr) {
    sizing.scale = ar_panel_scale_spin_->value();
  }
  if (ar_control_viewport_) {
    qar_web_panels::use_reported_viewport(sizing, *ar_control_viewport_);
  }
  return qar_web_panels::size_url_panel_for(
    ar_control_session_->model(), web_panel_server_->config().render.theme, sizing);
}

bool QarStreamingPanel::createArControlInstance(ArControlInstance & instance, SteadyTime now)
{
  if (!qar_factory_ || !qar_factory_->ready() || !ar_control_session_ || !web_panel_server_) {
    instance.next_attempt = now + std::chrono::seconds(1);
    return false;  // no QarOS session yet: not a failure to report
  }
  ensureUrlPanelFactory();

  qar_ar_core::UrlPanelDesc desc;
  desc.url = ar_control_page_url_;
  desc.display_name = ar_control::kPageTitle;
  desc.placement.size = arControlPanelSize().size;
  // The pose in front of the viewer (or the fixed spot) is lifted above the RViz ground; the yaw
  // rule is not touched, only the height. The request keeps the pose it was given, so that a
  // resize of the panel can lift it again for the new height.
  const GroundedPose grounded =
    liftAboveGround(instance.request.pose, desc.placement.size.height_meters);
  desc.placement.pose = grounded.pose;
  instance.raised_m = grounded.raised_m;
  instance.ground_room_y = grounded.ground_room_y;
  if (!instance.request.everybody) {
    desc.visible_to_peers.push_back(instance.request.peer);
  }
  std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> adapter =
    url_panel_factory_->create_url_panel(desc);
  if (!adapter) {
    instance.last_error = url_panel_factory_->last_error();
    instance.next_attempt = now + std::chrono::seconds(5);
    if (instance.last_error != instance.logged_error) {
      instance.logged_error = instance.last_error;
      appendLog(
        QString("AR CONTROL PANEL: could not create instance '%1' - %2; trying again every 5 s")
        .arg(QString::fromStdString(instance.key), QString::fromStdString(instance.last_error)));
    }
    ar_control_status_dirty_ = true;
    return false;
  }
  instance.adapter = std::move(adapter);
  instance.placement = desc.placement;
  instance.last_error.clear();
  instance.logged_error.clear();
  logArControlInstance(instance);
  ar_control_status_dirty_ = true;
  ar_control_dirty_ = true;
  // A peer that left before its panel could be made gets it hidden at once.
  if (!instance.wanted_visible && arVisibilityHides()) {
    applyInstanceVisibility(instance, false);
  }
  return true;
}

void QarStreamingPanel::logArControlInstance(const ArControlInstance & instance)
{
  const ControlPanelInstanceRequest & request = instance.request;
  const QString who =
    request.everybody ? QStringLiteral("everybody") : shortPeerWords(request.peer);
  QString basis;
  if (instance.has_head) {
    basis = QString("head (%1), gaze (%2), flip %3")
      .arg(
        vector3Words(instance.head), vector3Words(instance.gaze),
        control_flip_facing_ ? QStringLiteral("on") : QStringLiteral("off"));
    if (request.everybody) {
      basis += ", in front of " + shortPeerWords(request.peer);
    }
  } else {
    basis = QStringLiteral("no head pose: fixed spot");
  }
  if (instance.raised_m > 0.0f) {
    // The ground rule lifted the panel; the position above is where it hangs now.
    basis += QString(", raised %1 m above the ground at room y %2")
      .arg(static_cast<double>(instance.raised_m), 0, 'f', 2)
      .arg(static_cast<double>(instance.ground_room_y), 0, 'f', 2);
  }
  appendLog(
    QString("AR CONTROL PANEL: instance '%1' for %2 at (%3) yaw %4 (%5)")
    .arg(QString::fromStdString(instance.key), who, vector3Words(instance.placement.pose.position))
    .arg(yawDegrees(instance.placement.pose.orientation), 0, 'f', 1)
    .arg(basis));
}

void QarStreamingPanel::applyInstanceVisibility(ArControlInstance & instance, bool visible)
{
  if (!instance.adapter) {
    return;
  }
  const QString key = QString::fromStdString(instance.key);
  const QarGuiPanelState state = visible ? QAR_GUI_PANEL_STATE_VISIBLE : QAR_GUI_PANEL_STATE_HIDDEN;
  if (instance.adapter->set_state(state)) {
    appendLog(
      visible
      ? QString("AR VISIBILITY: control panel instance '%1' shown again (its peer is back)")
        .arg(key)
      : QString("AR VISIBILITY: control panel instance '%1' hidden (its peer's stream closed)")
        .arg(key));
    return;
  }
  appendLog(
    QString("AR VISIBILITY: set_state(%1) failed for control panel instance '%2': %3")
    .arg(
      visible ? QStringLiteral("VISIBLE") : QStringLiteral("HIDDEN"), key,
      QString::fromStdString(instance.adapter->last_error())));
}

// ---------------------------------------------------------------------------
// "Show AR control panel" and the size of the instances
// ---------------------------------------------------------------------------
void QarStreamingPanel::onShowArControlPanel()
{
  const SteadyTime now = std::chrono::steady_clock::now();
  const bool second = control_show_failed_ &&
    std::chrono::duration<double>(now - control_show_failed_at_).count() <= 10.0;
  if (second && arVisibilityHides()) {
    // Hiding is the way of the switch: no panel is closed or made again, so the second press
    // asks QarOS to show the panels once more, like the first.
    control_show_requested_ = true;
    appendLog(
      "AR CONTROL PANEL: Show AR control panel pressed again within 10 s - hiding is the way of "
      "the switch, so no panel is made again; QarOS is asked to show the panels once more on "
      "the next tick");
  } else if (second) {
    control_recreate_requested_ = true;
    appendLog(
      "AR CONTROL PANEL: Show AR control panel pressed again within 10 s - the panels QarOS "
      "would not show are made again on the next tick");
  } else {
    control_show_requested_ = true;
    appendLog(
      "AR CONTROL PANEL: Show AR control panel pressed - QarOS is asked to show the panels "
      "again on the next tick");
  }
  ar_control_status_dirty_ = true;
}

void QarStreamingPanel::onCopyArControlUrl()
{
  if (ar_control_page_url_.empty()) {
    return;
  }
  if (QClipboard * clipboard = QApplication::clipboard()) {
    clipboard->setText(QString::fromStdString(ar_control_page_url_));
  }
  appendLog(
    QString("AR CONTROL PANEL: copied %1").arg(QString::fromStdString(ar_control_page_url_)));
}

void QarStreamingPanel::applyArControlShow(SteadyTime now)
{
  if (control_recreate_requested_ && arVisibilityHides()) {
    // The setting changed to hiding between the press and this tick: nothing is made again, the
    // press asks QarOS to show the panels once more.
    control_recreate_requested_ = false;
    control_show_requested_ = true;
  }
  if (control_recreate_requested_) {
    // The second press: the panels that QarOS would not show are closed and made again.
    control_recreate_requested_ = false;
    control_show_failed_ = false;
    for (ArControlInstance & instance : control_instances_) {
      if (!instance.show_failed) {
        continue;
      }
      appendLog(
        QString("AR CONTROL PANEL: re-creating instance '%1'")
        .arg(QString::fromStdString(instance.key)));
      instance.adapter.reset();  // closes its QarOS panel
      instance.show_failed = false;
      instance.wanted_visible = true;
      instance.next_attempt = SteadyTime{};
      createArControlInstance(instance, now);
    }
    ar_control_status_dirty_ = true;
    return;
  }
  if (!control_show_requested_) {
    return;
  }
  control_show_requested_ = false;
  control_show_failed_ = false;
  std::size_t shown = 0;
  for (ArControlInstance & instance : control_instances_) {
    if (!instance.adapter) {
      continue;
    }
    instance.wanted_visible = true;
    if (instance.adapter->set_state(QAR_GUI_PANEL_STATE_VISIBLE)) {
      instance.show_failed = false;
      ++shown;
      continue;
    }
    instance.show_failed = true;
    control_show_failed_ = true;
    control_show_failed_at_ = now;
    control_show_failure_ = instance.adapter->last_error();
    if (control_show_failure_.empty()) {
      control_show_failure_ = "QarOS refused the request";
    }
    appendLog(
      QString("AR CONTROL PANEL: could not show instance '%1' - %2")
      .arg(
        QString::fromStdString(instance.key), QString::fromStdString(control_show_failure_)));
  }
  if (shown > 0) {
    appendLog(
      QString("AR CONTROL PANEL: QarOS accepted set_state(VISIBLE) for %1 instance(s)")
      .arg(static_cast<qulonglong>(shown)));
    if (!control_show_failed_) {
      control_show_accepted_at_ = now;
    }
  }
  ar_control_status_dirty_ = true;
}

void QarStreamingPanel::applyArControlResize()
{
  if (!ar_control_resize_pending_) {
    return;
  }
  ar_control_resize_pending_ = false;
  bool any = false;
  for (const ArControlInstance & instance : control_instances_) {
    any = any || static_cast<bool>(instance.adapter);
  }
  if (!any) {
    return;  // the first instance is made at the size the page has reported by then
  }
  const qar_web_panels::UrlPanelSizeResult sized = arControlPanelSize();
  bool moved_any = false;
  for (ArControlInstance & instance : control_instances_) {
    if (!instance.adapter) {
      continue;
    }
    // Only when it actually moves: a page re-reports on every change of its layout, and a
    // millimetre of rounding is not a change.
    const bool moved =
      std::abs(sized.size.width_meters - instance.placement.size.width_meters) > 0.005f ||
      std::abs(sized.size.height_meters - instance.placement.size.height_meters) > 0.005f;
    if (!moved) {
      continue;
    }
    qar_ar_core::UrlPanelPlacement placement = instance.placement;
    placement.size = sized.size;
    // The ground rule again, for the new height, from the pose the panel was placed at: a taller
    // panel reaches further down and may have to go up; a shorter one comes back to its place.
    const GroundedPose grounded =
      liftAboveGround(instance.request.pose, sized.size.height_meters);
    placement.pose = grounded.pose;
    if (instance.adapter->set_placement(placement)) {
      instance.placement = placement;
      instance.raised_m = grounded.raised_m;
      instance.ground_room_y = grounded.ground_room_y;
      logGroundRaise(QString("control:%1").arg(QString::fromStdString(instance.key)), grounded);
      moved_any = true;
    } else {
      appendLog(
        QString("AR CONTROL PANEL: could not resize instance '%1' - %2")
        .arg(
          QString::fromStdString(instance.key),
          QString::fromStdString(instance.adapter->last_error())));
    }
  }
  if (moved_any) {
    appendLog(
      QString("AR CONTROL PANEL: panel size %1 x %2 m - %3.")
      .arg(static_cast<double>(sized.size.width_meters), 0, 'f', 2)
      .arg(static_cast<double>(sized.size.height_meters), 0, 'f', 2)
      .arg(QString::fromStdString(sized.note)));
  }
}

// ---------------------------------------------------------------------------
// What the page shows
// ---------------------------------------------------------------------------
ControlPanelSnapshot QarStreamingPanel::controlPanelSnapshot()
{
  ControlPanelSnapshot snapshot;
  if (live_stream_mode_) {
    if (pending_connect_) {
      snapshot.connection = ConnectionState::Connecting;
    } else if (live_connected_) {
      snapshot.connection = ConnectionState::Live;
    } else if (qar_factory_) {
      snapshot.connection = ConnectionState::Paused;
    } else {
      snapshot.connection = ConnectionState::Offline;
    }
  } else {
    snapshot.connection = qar_running_ ? ConnectionState::Preview : ConnectionState::Offline;
  }
  snapshot.peers = manager_ ? static_cast<std::uint32_t>(manager_->stream_count()) : 0;
  snapshot.last_error = last_error_text_.toStdString();

  // The pick: the page offers Off, Point and Nav goal; any other mode the desktop armed is named.
  const SpatialInteractionStatus pick = spatial_.status();
  switch (pick.mode) {
    case SpatialMode::Off:
      snapshot.pick_mode = ArPickMode::Off;
      break;
    case SpatialMode::Point:
      snapshot.pick_mode = ArPickMode::Point;
      break;
    case SpatialMode::NavGoal:
      snapshot.pick_mode = ArPickMode::NavGoal;
      break;
    case SpatialMode::InitialPose:
      snapshot.pick_mode_other = true;
      snapshot.pick_mode_other_name = "initial pose";
      break;
    case SpatialMode::Area:
      snapshot.pick_mode_other = true;
      snapshot.pick_mode_other_name =
        std::string("area (") + qar_ar_core::to_string(spatial_.area_shape()) + ")";
      break;
  }
  snapshot.pick_pending = pick.has_pending;
  snapshot.pick_auto_confirm = pick.auto_confirm;
  snapshot.pick_summary = pick.summary;
  snapshot.pick_error = pick.error;

  // The panels: the desktop's list, in its order, each with where it stands as an AR panel.
  qar_ar_core::PanelDiagnosticsSnapshot diagnostics;
  if (panel_manager_) {
    diagnostics = panel_manager_->diagnostics();
  }
  for (const StreamableEntry & entry : collectStreamableEntries()) {
    ControlPanelPanelEntry panel;
    panel.key = entry.key.toStdString();
    panel.name = entry.title.toStdString();
    panel.state = arStateOf(findPanelDiagnostics(diagnostics, panel.key));
    snapshot.panels.push_back(std::move(panel));
  }
  for (const ExposedEntity & row : exposures_.entries()) {
    ControlPanelToolEntry tool;
    tool.panel_id = row.panel_id;
    tool.name = row.entity.name;
    tool.in_ar = row.ar_panel_open;
    // The tool's switch reads on while its QarOS panel exists and is shown.
    const ArToolPanel * running = findArTool(row.panel_id);
    tool.on = row.ar_panel_open && running != nullptr && running->ar_panel &&
      !running->ar_hidden;
    snapshot.tools.push_back(std::move(tool));
  }
  return snapshot;
}

void QarStreamingPanel::publishArControlState(SteadyTime now)
{
  // At most every 250 ms while nothing happens, and at most every 50 ms after a command, a pick
  // sample or a change of a panel's state. Building the snapshot walks the RViz object tree, so it
  // must not run at the rate of the pick samples. The dirty flag stays set until a snapshot is
  // built, so what happened inside the 50 ms reaches the page with the next one.
  constexpr double kIdleSeconds = 0.25;
  constexpr double kDirtySeconds = 0.05;
  if (ar_control_snapshot_last_.time_since_epoch().count() != 0 &&
    std::chrono::duration<double>(now - ar_control_snapshot_last_).count() <
    (ar_control_dirty_ ? kDirtySeconds : kIdleSeconds))
  {
    return;
  }
  ar_control_snapshot_last_ = now;
  ar_control_dirty_ = false;

  const ControlPanelSnapshot snapshot = controlPanelSnapshot();
  std::vector<qar_ui_model::PatchEntry> patch = ar_control_state_.update(snapshot);

  // A switch the page has drawn ahead of the panel: when the panel does not end up that way,
  // the page is sent the whole truth. A hide or a show waits for the manager's tick, and a
  // close for the manager to let go of the stream, so a mismatch counts only after a moment.
  for (auto it = control_toggles_.begin(); it != control_toggles_.end();) {
    // The switch of a streamed panel, or of an AR tool: is it listed, and does it read on?
    bool listed = false;
    bool reads_on = false;
    if (it->tool) {
      for (const ControlPanelToolEntry & tool : snapshot.tools) {
        if (tool.panel_id == it->key) {
          listed = true;
          reads_on = tool.on;
          break;
        }
      }
    } else {
      for (const ControlPanelPanelEntry & panel : snapshot.panels) {
        if (panel.key == it->key) {
          listed = true;
          reads_on = panel_state_is_on(panel.state);
          break;
        }
      }
    }
    bool settled = !listed || reads_on == it->on;
    if (!settled) {
      const double age = std::chrono::duration<double>(now - it->at).count();
      const bool waiting = !it->tool && age < 10.0 && panelSwitchWaits(it->key);
      if (it->failed || (!waiting && age > 1.5) || age >= 10.0) {
        ar_control_resync_ = true;
        settled = true;
      }
    }
    it = settled ? control_toggles_.erase(it) : it + 1;
  }
  if (ar_control_resync_) {
    ar_control_resync_ = false;
    patch = ar_control_state_.resync_patch();
  }
  if (patch.empty()) {
    return;
  }
  std::string error;
  if (!ar_control_session_->publish(std::move(patch), &error)) {
    if (!ar_control_publish_failed_logged_) {
      ar_control_publish_failed_logged_ = true;
      appendLog(
        QString("AR CONTROL PANEL: the page's state could not be published - %1")
        .arg(QString::fromStdString(error)));
    }
    ar_control_resync_ = true;  // try the whole document next time
  }
}

QString QarStreamingPanel::arControlStatusText(SteadyTime now) const
{
  const QString head = QStringLiteral("AR control panel: ");
  if (!ar_control_session_) {
    if (!ar_control_page_error_.isEmpty()) {
      return head + "not created - " + ar_control_page_error_;
    }
    if (!setting_bool(effective_settings_, settings_key::kArControlPanel)) {
      return head + "off (the setting \"Show the AR control panel when connecting\" is off)";
    }
    return head + (live_stream_mode_ ? "not created (connect to QarOS)"
      : "not created (press Start Qar)");
  }

  QString text = head;
  bool exists = false;
  for (const ArControlInstance & instance : control_instances_) {
    exists = exists || static_cast<bool>(instance.adapter);
  }
  if (!live_stream_mode_) {
    text += "page served; the offline preview has no QarOS panel (Copy page URL opens it in a "
      "desktop browser).";
  } else if (control_instances_.empty()) {
    text += control_policy_.mode() == ControlPanelMode::Shared
      ? "waiting for a head pose (the shared panel goes to a fixed spot after 20 s)."
      : "waiting for the first peer's head pose.";
  } else {
    QStringList parts;
    int in_ar = 0;
    for (const ArControlInstance & instance : control_instances_) {
      QString part = instance.request.everybody
        ? QStringLiteral("everybody") : "peer " + shortPeerWords(instance.request.peer);
      if (instance.adapter) {
        ++in_ar;
      } else {
        part += instance.last_error.empty()
          ? QStringLiteral(" (not created yet)")
          : " (not created: " + QString::fromStdString(instance.last_error) + ")";
      }
      parts.append(part);
    }
    text += QString("%1 panel(s) in AR, for %2.").arg(in_ar).arg(parts.join(", "));
  }

  text += QString(" Page viewers: %1").arg(static_cast<qulonglong>(ar_control_viewers_));
  const bool gone = exists && ar_control_viewers_ == 0 &&
    ar_control_no_viewer_since_.time_since_epoch().count() != 0;
  const double silent_s = gone
    ? std::chrono::duration<double>(now - ar_control_no_viewer_since_).count() : 0.0;
  if (gone && silent_s >= 10.0) {
    text += QString("; no viewer connected for %1 s - closed in AR?")
      .arg(static_cast<int>(silent_s));
  } else {
    text += ".";
  }

  // The outcome of "Show AR control panel", for ten seconds.
  if (control_show_failed_) {
    text += "\ncould not show it (" + QString::fromStdString(control_show_failure_) + ").";
    if (arVisibilityHides()) {
      // Nothing is made again while hiding is the way of the switch.
      text += " Hiding is the way of the switch, so no panel is made again; press again to ask "
        "QarOS once more.";
    } else if (std::chrono::duration<double>(now - control_show_failed_at_).count() <= 10.0) {
      text += " Press again within 10 s to re-create it - re-creating a closed AR panel has "
        "crashed the desktop Visualizer";
    }
  } else if (control_show_accepted_at_.time_since_epoch().count() != 0 &&
    std::chrono::duration<double>(now - control_show_accepted_at_).count() <= 10.0)
  {
    text += "\nshown again (QarOS accepted the request).";
  }
  return text;
}

void QarStreamingPanel::refreshArControlStatus(bool force)
{
  if (ar_control_status_label_ == nullptr) {
    return;
  }
  const SteadyTime now = std::chrono::steady_clock::now();
  if (!force && !ar_control_status_dirty_ &&
    ar_control_status_last_.time_since_epoch().count() != 0 &&
    std::chrono::duration<double>(now - ar_control_status_last_).count() < 0.5)
  {
    return;
  }
  ar_control_status_last_ = now;
  ar_control_status_dirty_ = false;

  bool exists = false;
  for (const ArControlInstance & instance : control_instances_) {
    exists = exists || static_cast<bool>(instance.adapter);
  }
  // Since when panels exist and nobody has the page open: the "closed in AR?" hint counts from it.
  if (exists && ar_control_viewers_ == 0) {
    if (ar_control_no_viewer_since_.time_since_epoch().count() == 0) {
      ar_control_no_viewer_since_ = now;
    }
  } else {
    ar_control_no_viewer_since_ = SteadyTime{};
  }

  const QString text = arControlStatusText(now);
  if (text != ar_control_status_text_) {
    ar_control_status_text_ = text;
    ar_control_status_label_->setText(text);
  }
  if (ar_control_show_button_ != nullptr) {
    ar_control_show_button_->setEnabled(exists);
  }
  if (ar_control_copy_button_ != nullptr) {
    ar_control_copy_button_->setEnabled(ar_control_session_ != nullptr);
  }
}

}  // namespace qar_rviz_streaming

PLUGINLIB_EXPORT_CLASS(qar_rviz_streaming::QarStreamingPanel, rviz_common::Panel)
