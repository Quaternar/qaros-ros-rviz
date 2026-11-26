// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/qar_panel_view.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include "qar_qt_streaming/scroll_census.hpp"
#include "qar_qt_streaming/widget_twin.hpp"
#include "qar_rviz_streaming/ar_copy_scale.hpp"
#include "qar_rviz_streaming/ar_panel_list_widget.hpp"
#include "qar_rviz_streaming/hub_connection_widget.hpp"
#include "qar_rviz_streaming/spatial_interaction.hpp"
#include "qar_rviz_streaming/stable_panel_layout.hpp"

namespace qar_rviz_streaming
{

namespace
{

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

/// The property that remembers how many rows a list or a log is kept at least that high for.
constexpr const char * kMinimumRowsProperty = "qar_minimum_rows";

/// The least height of a list or a log of @p rows rows under the style the widget uses: a list is
/// measured with one probe row (a row is as high as the style makes an item), anything else by its
/// line spacing, plus the frame on both sides.
int rows_height_by_style(QWidget * widget, int rows)
{
  int row_height = 0;
  if (auto * list = qobject_cast<QListWidget *>(widget)) {
    list->addItem(QStringLiteral("Mg"));
    row_height = list->sizeHintForRow(0);
    list->clear();
  }
  if (row_height <= 0) {
    row_height = widget->fontMetrics().lineSpacing();
  }
  int frame = 3;
  if (auto * framed = qobject_cast<QFrame *>(widget)) {
    frame = framed->frameWidth();
  }
  return row_height * rows + 2 * frame;
}

/// The least height of @p rows rows: by the style, or (the panel's own window) by the line spacing.
int minimum_rows_height(QWidget & widget, int rows, bool by_style)
{
  if (by_style) {
    return rows_height_by_style(&widget, rows);
  }
  return widget.fontMetrics().lineSpacing() * rows + 6;
}

}  // namespace

/// The tool tip of the publish toggle of the AR tools list.
const char * const kWritableToggleToolTip =
  "Make the selected topic tool again with the other publish flag. \"Allow publishing\" gives "
  "its page a draft and a Publish button that asks for a second press in AR; \"Make "
  "read-only\" takes them away. A service or an action keeps its form, so the button is grey "
  "for them. The old page stops working. With \"Hide panels in AR instead of closing them\" "
  "on, the AR panel is kept and shows the page of the remade tool, in its place and as shown "
  "or hidden as it was; with it off, the old AR panel is closed. "
  "Publishing to a topic is a command to a robot.";

QarPanelView build_qar_panel_view(QWidget & root, const QarPanelViewInputs & inputs)
{
  QarPanelView v;

  auto * top = new QVBoxLayout(&root);
  top->setContentsMargins(0, 0, 0, 0);
  top->setSpacing(0);

  // --- Status strip --------------------------------------------------------
  const StatusStrip strip = make_status_strip(&root);
  v.status_strip = strip.frame;
  v.runtime_state_label = strip.first;
  v.last_error_label = strip.second;
  v.runtime_state_label->setFullText("Qar runtime: STOPPED");
  v.last_error_label->setFullText("Last error: <none>");
  top->addWidget(v.status_strip);

  v.tabs = make_tab_widget(&root);
  top->addWidget(v.tabs, 1);

  // One page: a plain widget with a vertical layout, wrapped by make_scroll_page().
  auto add_page = [&v](const QString & title, QWidget * content) {
      v.page_contents.push_back(content);
      v.tabs->addTab(make_scroll_page(content, v.tabs), title);
    };
  // A list or a log is never lower than a few rows. The log takes any height its page has to
  // spare; the two lists of the AR tools page do not (see there).
  auto set_minimum_rows = [&inputs](QWidget * widget, int rows = 4) {
      widget->setProperty(kMinimumRowsProperty, rows);
      widget->setMinimumHeight(minimum_rows_height(*widget, rows, inputs.rows_by_style));
    };

  // =========================================================================
  // Connection
  // =========================================================================
  auto * connection_page = new QWidget(&root);
  auto * connection_layout = new QVBoxLayout(connection_page);
  connection_layout->setContentsMargins(6, 6, 6, 6);
  connection_layout->setSpacing(6);

  auto * runtime_buttons = new QHBoxLayout();
  v.start_qar_button = new QPushButton("Start Qar", connection_page);
  v.stop_qar_button = new QPushButton("Stop Qar", connection_page);
  runtime_buttons->addWidget(v.start_qar_button);
  runtime_buttons->addWidget(v.stop_qar_button);
  connection_layout->addLayout(runtime_buttons);

  // The state of the join to the Hub of this PC and the devices the Hub lists as targets. The
  // group shows in every mode; the preview shows the state only.
  v.hub_widget = new HubConnectionWidget(connection_page);
  connection_layout->addWidget(v.hub_widget);

  // Which kind of real device "Invite peer" asks the Hub to start an app for. The group is shown
  // in every mode, so that the way to invite a device can always be found; the button is enabled
  // only while connected (refreshConnectionView()) and says what it needs when it is not.
  v.invite_group = new QGroupBox("Invite a device", connection_page);
  auto * invite_layout = new QVBoxLayout(v.invite_group);
  auto * invite_row = new QHBoxLayout();
  v.peer_kind_combo = new QComboBox(v.invite_group);
  v.peer_kind_combo->addItem("HoloLens 2", "hololens");
  v.peer_kind_combo->addItem("Headset (Quest, Android)", "headset");
  v.peer_kind_combo->setToolTip(
    "Device type whose app the QAROS Hub is asked to start. The device then joins as a target "
    "and gets its own stereo stream of the RViz scene.");
  v.peer_hostname_edit = new QLineEdit(v.invite_group);
  v.peer_hostname_edit->setMaxLength(253);
  v.peer_hostname_edit->setPlaceholderText("device host name or address");
  v.peer_hostname_edit->setToolTip(
    "Required for both kinds: the host name or address of the HoloLens 2 or of the headset.");
  invite_row->addWidget(v.peer_kind_combo);
  invite_row->addWidget(v.peer_hostname_edit, 1);
  v.add_peer_button = new QPushButton("Invite peer", v.invite_group);
  // No session yet: the tool tip of the view of a panel that is not connected.
  v.add_peer_button->setToolTip(inputs.invite_tool_tip);
  invite_layout->addLayout(invite_row);
  invite_layout->addWidget(v.add_peer_button);
  connection_layout->addWidget(v.invite_group);

  // The AR control panel: the page that puts the panel's main controls into AR. The label
  // says whether it exists; the two buttons follow its state and stay disabled while there is
  // no control panel.
  auto * control_group = new QGroupBox("AR control panel", connection_page);
  auto * control_layout = new QVBoxLayout(control_group);
  v.ar_control_status_label = new QLabel(
    "AR control panel: not created (connect to QarOS)", control_group);
  v.ar_control_status_label->setWordWrap(true);
  v.ar_control_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // Its text changes at run time and the panel streams itself: same rule as every status label.
  makeWidthNeutral(v.ar_control_status_label);
  auto * control_buttons = new QHBoxLayout();
  v.ar_control_show_button = new QPushButton("Show AR control panel", control_group);
  v.ar_control_show_button->setToolTip(
    "Show the AR control panel again if it was closed in AR. If QarOS cannot show it, the line "
    "above says so. With \"Hide panels in AR instead of closing them\" on, no panel is made "
    "again and the button asks QarOS to show it once more; with it off, pressing the button "
    "again within ten seconds makes the panel again. Available once the control panel exists.");
  v.ar_control_show_button->setEnabled(false);
  v.ar_control_copy_button = new QPushButton("Copy page URL", control_group);
  v.ar_control_copy_button->setToolTip(
    "Copy the address of the control panel's page, to open it in a desktop browser. "
    "Available once the page is served.");
  v.ar_control_copy_button->setEnabled(false);
  control_buttons->addWidget(v.ar_control_show_button);
  control_buttons->addWidget(v.ar_control_copy_button);
  control_layout->addWidget(v.ar_control_status_label);
  control_layout->addLayout(control_buttons);
  connection_layout->addWidget(control_group);
  connection_layout->addStretch(1);
  add_page("Connection", connection_page);

  // =========================================================================
  // AR panels
  // =========================================================================
  auto * panels_page = new QWidget(&root);
  auto * panels_layout = new QVBoxLayout(panels_page);
  panels_layout->setContentsMargins(6, 6, 6, 6);
  panels_layout->setSpacing(6);

  // Any RViz dock panel can be streamed, not just this one. The list shows what is streamed, to
  // whom and in which state; its buttons act on the selected row. It is exactly eight rows high
  // and has no scroll bars (more rows are reached with its pager), so nothing it holds can move
  // the size of this panel, which streams itself to AR.
  v.ar_panel_list = new ArPanelListWidget(panels_page);
  panels_layout->addWidget(v.ar_panel_list);
  panels_layout->addStretch(1);
  add_page("AR panels", panels_page);

  // =========================================================================
  // AR tools
  // =========================================================================
  auto * tools_page = new QWidget(&root);
  auto * tools_layout = new QVBoxLayout(tools_page);
  tools_layout->setContentsMargins(6, 6, 6, 6);
  tools_layout->setSpacing(6);

  auto * watched_group = new QGroupBox("Watched topics", tools_page);
  auto * watched_layout = new QVBoxLayout(watched_group);
  auto * topic_buttons = new QHBoxLayout();
  v.add_topic_button = new QPushButton("Add from ROS graph...", watched_group);
  v.add_topic_button->setToolTip(
    "Browse the live ROS 2 graph — topics, services and actions — add a topic to "
    "RViz, and/or expose any of them as a QAR AR tool.");
  v.remove_topic_button = new QPushButton("Remove topic", watched_group);
  topic_buttons->addWidget(v.add_topic_button);
  topic_buttons->addWidget(v.remove_topic_button);
  // The two lists of this page are half as high as a stock list box (CompactList) and do not
  // take the height the page has to spare: it stays below the groups (the stretch at the end).
  // Their minimum is two rows. A vertical policy of Preferred keeps a taller panel from
  // stretching them.
  v.topic_watch_list = new CompactList(watched_group);
  v.topic_watch_list->setToolTip(
    "Topics added from the topic browser. Adding one also creates the matching "
    "RViz display, so it appears in the streamed 3D scene.");
  set_minimum_rows(v.topic_watch_list, 2);
  v.topic_watch_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  watched_layout->addLayout(topic_buttons);
  watched_layout->addWidget(v.topic_watch_list);
  tools_layout->addWidget(watched_group);

  // The management list: what is exposed right now, and the things an operator needs to do
  // with a row.
  auto * exposed_group = new QGroupBox("AR tools", tools_page);
  auto * exposed_layout = new QVBoxLayout(exposed_group);
  v.exposure_list = new CompactList(exposed_group);
  v.exposure_list->setToolTip(
    "Topics, services and actions exposed as QAR AR tools. Each row is a "
    "generated panel served over HTTP and shown in the headset's browser.");
  set_minimum_rows(v.exposure_list, 2);
  v.exposure_list->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto * exposure_buttons = new QHBoxLayout();
  v.remove_exposure_button = new QPushButton("Remove tool", exposed_group);
  v.remove_exposure_button->setToolTip(
    "Stop serving the tool's page and take its AR panel out of use. With \"Hide panels in AR "
    "instead of closing them\" on, the AR panel is hidden and kept until the session ends, and "
    "exposing the same tool again takes it over; with it off, the AR panel is closed.");
  // One button names what a press does to the selected topic tool. Its width is that of its
  // longer text, so the row does not change size when the text does.
  v.toggle_writable_button = new QPushButton("Allow publishing", exposed_group);
  v.toggle_writable_button->setToolTip(QString::fromLatin1(kWritableToggleToolTip));
  {
    int widest = 0;
    for (const char * text : {"Allow publishing", "Make read-only"}) {
      v.toggle_writable_button->setText(QString::fromLatin1(text));
      widest = std::max(widest, v.toggle_writable_button->sizeHint().width());
    }
    v.toggle_writable_button->setText("Allow publishing");
    v.toggle_writable_button->setFixedWidth(widest);
    v.toggle_writable_button->setEnabled(false);
  }
  v.reopen_exposure_button = new QPushButton("Re-open in AR", exposed_group);
  v.reopen_exposure_button->setToolTip(
    "Bring the QarOS panel back, after a session restart or if it was hidden or closed in the "
    "headset. With \"Hide panels in AR instead of closing them\" on, QarOS is asked to show the "
    "panel again; if QarOS refuses, the log and the last error say so and nothing is closed or "
    "made again. With it off, the panel is closed and made again. The page and its token are "
    "unchanged.");
  v.copy_url_button = new QPushButton("Copy URL", exposed_group);
  v.copy_url_button->setToolTip(
    "Copy the panel's URL, to open it in a desktop browser while debugging.");
  // Live size trim. The panel's physical size is DERIVED from the generated
  // page's own measurement (qar_web_panels/url_panel_sizing.hpp), which is the
  // right default; this is the operator's "a little bigger, please" knob, and
  // it takes effect on the next "Re-open in AR" - no rebuild, no restart. Its
  // starting value is the "Web Panel Scale" setting (QAR_PANEL_WEB_SCALE, the saved
  // configuration, or 1.00; applySettingsToUi() sets it).
  v.ar_panel_scale_spin = new QDoubleSpinBox(exposed_group);
  v.ar_panel_scale_spin->setRange(0.25, 4.0);
  v.ar_panel_scale_spin->setSingleStep(0.1);
  v.ar_panel_scale_spin->setDecimals(2);
  v.ar_panel_scale_spin->setPrefix("size x");
  // The starting value of the scale is the panel's setting; the panel sets it after the build.
  v.ar_panel_scale_spin->setToolTip(
    "Multiplier on the AR panel size derived from the page's own content size. "
    "Press \"Re-open in AR\" to apply it to the selected tool.");
  exposure_buttons->addWidget(v.remove_exposure_button);
  exposure_buttons->addWidget(v.reopen_exposure_button);
  exposure_buttons->addWidget(v.copy_url_button);
  exposure_buttons->addWidget(v.ar_panel_scale_spin);
  v.exposure_status_label = new QLabel("AR tools: none", exposed_group);
  v.exposure_status_label->setWordWrap(true);
  v.exposure_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  // Same feedback loop as the AR panel status: this label is inside the
  // streamed widget and its text changes whenever a tool is added or removed.
  makeWidthNeutral(v.exposure_status_label);
  exposed_layout->addWidget(v.exposure_list);
  exposed_layout->addLayout(exposure_buttons);
  auto * exposure_flag_buttons = new QHBoxLayout();
  exposure_flag_buttons->addWidget(v.toggle_writable_button);
  exposure_flag_buttons->addStretch(1);
  exposed_layout->addLayout(exposure_flag_buttons);
  exposed_layout->addWidget(v.exposure_status_label);
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
  auto * picking_page = new QWidget(&root);
  auto * picking_layout = new QVBoxLayout(picking_page);
  picking_layout->setContentsMargins(6, 6, 6, 6);
  picking_layout->setSpacing(6);
  auto * spatial_form = new QFormLayout();
  v.spatial_mode_combo = new QComboBox(picking_page);
  v.spatial_mode_combo->addItem("Off", static_cast<int>(SpatialMode::Off));
  v.spatial_mode_combo->addItem("Pick a point -> /clicked_point",
    static_cast<int>(SpatialMode::Point));
  v.spatial_mode_combo->addItem("Set a navigation goal -> /goal_pose",
    static_cast<int>(SpatialMode::NavGoal));
  v.spatial_mode_combo->addItem("Set the initial pose -> /initialpose",
    static_cast<int>(SpatialMode::InitialPose));
  // Area comes in three shapes. They are encoded into the same item data as
  // shape * 100 + mode, so one combo arms the mode AND picks the shape without
  // a second control the operator has to notice.
  v.spatial_mode_combo->addItem("Select a rectangle -> polygon",
    static_cast<int>(SpatialMode::Area));
  v.spatial_mode_combo->addItem("Select a circle -> polygon",
    100 + static_cast<int>(SpatialMode::Area));
  v.spatial_mode_combo->addItem("Select a polygon (tap vertices)",
    200 + static_cast<int>(SpatialMode::Area));
  v.spatial_mode_combo->setToolTip(
    "What an AR pointer means in the 3D scene. Off ignores spatial input.");
  v.spatial_target_combo = new QComboBox(picking_page);
  // Built from spatial_target_options() rather than typed out here: the list,
  // its order, its default and its help text are one unit-tested table, so the
  // dropdown cannot drift from what the controller actually arms. The default
  // is "Action Point (QarOS scene intersect)": the installed runtime reports its own
  // scene intersection as the action point, which makes that the right default. The
  // default relies on such a runtime; see spatial_interaction.hpp. Every other entry
  // is offered too.
  {
    QString target_help = "What the pointer is resolved against.\n\n";
    for (const QarPanelViewInputs::Target & target : inputs.spatial_targets) {
      v.spatial_target_combo->addItem(target.label, target.id);
      target_help += QString("%1\n    %2\n").arg(target.label, target.help);
    }
    v.spatial_target_combo->setToolTip(target_help.trimmed());
    // Selected BY ID, not by index 0. The table happens to put the default
    // first and a test pins that, but the combo must show what the controller
    // is about to be armed with (below) even if the two ever disagree.
    const int default_index = v.spatial_target_combo->findData(inputs.spatial_target_default_id);
    if (default_index >= 0) {
      v.spatial_target_combo->setCurrentIndex(default_index);
    }
  }
  v.spatial_topic_combo = new QComboBox(picking_page);
  v.spatial_topic_combo->setEditable(true);
  v.spatial_topic_combo->setInsertPolicy(QComboBox::NoInsert);
  v.spatial_topic_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  v.spatial_topic_combo->setMinimumContentsLength(12);
  v.spatial_topic_combo->setEnabled(false);
  v.spatial_topic_combo->setToolTip(
    "The topic a pick of the armed mode is published to. Choose one from the list, or type a "
    "name and press Enter. A topic that carries another message type is refused.");
  makeWidthNeutral(v.spatial_topic_combo);
  v.spatial_frame_edit = new QLineEdit("map", picking_page);
  v.spatial_frame_edit->setToolTip("The frame every published pick is stamped with.");
  v.spatial_auto_confirm = new QCheckBox("Publish without confirming", picking_page);
  v.spatial_auto_confirm->setToolTip(
    "Off by default: a pick waits for Confirm so a stray gesture cannot drive "
    "the robot.");
  spatial_form->addRow("Mode", v.spatial_mode_combo);
  spatial_form->addRow("Topic", v.spatial_topic_combo);
  spatial_form->addRow("Target", v.spatial_target_combo);
  spatial_form->addRow("Frame", v.spatial_frame_edit);
  auto * spatial_buttons = new QHBoxLayout();
  v.spatial_confirm_button = new QPushButton("Confirm pick", picking_page);
  v.spatial_cancel_button = new QPushButton("Cancel", picking_page);
  v.spatial_close_polygon_button = new QPushButton("Close polygon", picking_page);
  v.spatial_close_polygon_button->setToolTip(
    "Area mode with the polygon shape: finish the region the taps collected.");
  spatial_buttons->addWidget(v.spatial_confirm_button);
  spatial_buttons->addWidget(v.spatial_cancel_button);
  spatial_buttons->addWidget(v.spatial_close_polygon_button);
  v.spatial_status_label = new QLabel("Spatial: off", picking_page);
  v.spatial_status_label->setWordWrap(true);
  v.spatial_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  makeWidthNeutral(v.spatial_status_label);
  picking_layout->addLayout(spatial_form);
  picking_layout->addWidget(v.spatial_auto_confirm);
  picking_layout->addLayout(spatial_buttons);
  picking_layout->addWidget(v.spatial_status_label);
  picking_layout->addStretch(1);
  add_page("Picking", picking_page);

  // =========================================================================
  // Settings
  // =========================================================================
  // What the panel keeps in the RViz configuration, and the switches that an environment
  // variable can also set. Each persisted control shows the value in force and, where an
  // environment variable decides it, is disabled with the variable named in its tool tip
  // (applySettingsToUi()). The group titles say WHEN a setting takes effect.
  auto * settings_page = new QWidget(&root);
  auto * settings_layout = new QVBoxLayout(settings_page);
  settings_layout->setContentsMargins(6, 6, 6, 6);
  settings_layout->setSpacing(6);

  auto * streams_group = new QGroupBox("Streams (apply to new streams)", settings_page);
  auto * streams_form = new QFormLayout(streams_group);
  v.resolution_combo = new QComboBox(streams_group);
  v.resolution_combo->addItem("640 x 480", QSize(640, 480));
  v.resolution_combo->addItem("1024 x 1024", QSize(1024, 1024));
  v.resolution_combo->addItem("1280 x 720", QSize(1280, 720));
  v.resolution_combo->addItem("1600 x 900", QSize(1600, 900));
  v.resolution_combo->addItem("1920 x 1080", QSize(1920, 1080));
  v.resolution_combo->addItem("2048 x 2048", QSize(2048, 2048));
  v.resolution_combo->setCurrentIndex(2);  // 1280 x 720
  v.resolution_combo->setToolTip(
    "The size of the pictures of the streams that are created from now on. Streams that "
    "are already open keep their size.");
  // FPS options MUST include 30 and 60.
  v.fps_combo = new QComboBox(streams_group);
  for (const int fps : {15, 30, 60, 72, 90}) {
    v.fps_combo->addItem(QString("%1 fps").arg(fps), fps);
  }
  v.fps_combo->setCurrentIndex(2);  // 60 fps (the optimum target)
  v.fps_combo->setToolTip(
    "The most frames per second a stream is asked for, for the streams that are created from "
    "now on.");
  streams_form->addRow("Resolution:", v.resolution_combo);
  streams_form->addRow("FPS cap:", v.fps_combo);
  // The size of the AR copies of the Qt panels. The first item names the factor in force
  // (applySettingsToUi() rewrites it); the item data are the words of the setting.
  v.ar_copy_size_combo = new QComboBox(streams_group);
  v.ar_copy_size_combo->addItem(
    QString::fromStdString(ar_copy_enlarged_item_text(kArCopyScaleDefault)),
    QString::fromLatin1(to_string(ArCopySize::Enlarged)));
  v.ar_copy_size_combo->addItem(
    kArCopyNativeItemText, QString::fromLatin1(to_string(ArCopySize::Native)));
  v.ar_copy_size_combo->setCurrentIndex(0);
  v.ar_copy_size_combo->setToolTip(kArCopyRowToolTip);
  streams_form->addRow(kArCopyRowLabel, v.ar_copy_size_combo);
  settings_layout->addWidget(streams_group);

  auto * session_group = new QGroupBox("Session (apply when you connect)", settings_page);
  auto * session_form = new QFormLayout(session_group);
  v.hidden_displays_checkbox = new QCheckBox(
    "Start the hidden Displays copy when connecting", session_group);
  v.hidden_displays_checkbox->setToolTip(
    "Streams a second copy of RViz's Displays panel into AR when the panel connects, so the "
    "headset can switch displays on and off. Off keeps it from starting by itself; picking "
    "its entry in the AR panels list and pressing \"Stream to AR\" still starts it. "
    "Applies at every connect.");
  v.ar_control_checkbox = new QCheckBox(
    "Show the AR control panel when connecting", session_group);
  v.ar_control_checkbox->setToolTip(
    "Serves the AR control panel, a page with the panel's main controls, when the panel "
    "connects, and shows it in AR in front of each user once that user's view is being "
    "streamed. In the offline preview only the page is served: \"Copy page URL\" opens it in a "
    "desktop browser. Applies at every connect.");
  session_form->addRow(v.hidden_displays_checkbox);
  session_form->addRow(v.ar_control_checkbox);
  settings_layout->addWidget(session_group);

  v.show_advanced_checkbox = new QCheckBox("Show advanced options", settings_page);
  v.show_advanced_checkbox->setToolTip(
    "Shows the advanced settings below and, in a QarOS session, the simulated-peer controls on "
    "the Diagnostics tab. This choice is not saved.");
  settings_layout->addWidget(v.show_advanced_checkbox);

  // --- Frame-timing diagnostics (QAR_ADAPTER_PRIME) -------------------------
  // See docs/ARCHITECTURE.md#the-render-tick. Priming is a live A/B switch for telling a
  // fixed-tick wait apart from an SDK pose/frame mismatch, not a change to the render budget.
  // It defaults to on.
  v.advanced_group = new QGroupBox("Advanced", settings_page);
  auto * advanced_form = new QFormLayout(v.advanced_group);
  v.prime_checkbox = new QCheckBox("Prime next frame", v.advanced_group);
  // Whether the next frame is primed starts as the panel's environment says; the panel sets
  // the check state after the build.
  v.prime_checkbox->setToolTip(
    "Start the next frame's SDK wait right after this one is shown (one tick per "
    "frame). Off: the wait starts at the next tick's "
    "begin_frame instead (two ticks per frame).");
  v.close_mode_combo = new QComboBox(v.advanced_group);
  v.close_mode_combo->addItem("Close them", "destroy");
  v.close_mode_combo->addItem("Show a notice", "notice");
  v.close_mode_combo->setToolTip(
    "What happens to the AR copy of a panel when its RViz window is closed, while \"Hide panels "
    "in AR instead of closing them\" is off. Close them: the AR panel is closed and its video "
    "sender is destroyed. Show a notice: the AR panel stays as a closed-window notice and its "
    "sender is kept until the session ends, for a QarOS runtime that does not survive "
    "destroying a sender. With hiding on, a window that closes only hides its AR panel and "
    "this choice is not used. A change acts on the closes that follow.");
  v.ar_visibility_checkbox = new QCheckBox(
    "Hide panels in AR instead of closing them (experimental)", v.advanced_group);
  v.ar_visibility_checkbox->setToolTip(
    "On: no QarOS panel is closed while a session runs. Switching a panel off hides it in AR and "
    "switching it on shows it again; a window that closes on the desktop (Stop streaming, a "
    "closed dock or dialog) and a removed AR tool leave their AR panel hidden and parked, and "
    "opening the window or exposing the tool again shows it again without a new QarOS panel; "
    "\"Re-open in AR\" shows an AR tool again. When the session ends, every AR panel is hidden "
    "and left in QarOS under its name, not closed; the next session finds it and shows it "
    "again. Off: the panel is closed and made again. A change acts on the next switch, close "
    "or removal.");
  advanced_form->addRow(v.prime_checkbox);
  advanced_form->addRow("Closed AR panels:", v.close_mode_combo);
  advanced_form->addRow(v.ar_visibility_checkbox);
  retain_size_when_hidden(v.advanced_group);
  settings_layout->addWidget(v.advanced_group);
  settings_layout->addStretch(1);
  add_page("Settings", settings_page);

  // =========================================================================
  // Diagnostics
  // =========================================================================
  auto * diag_page = new QWidget(&root);
  auto * diag_layout = new QVBoxLayout(diag_page);
  diag_layout->setContentsMargins(6, 6, 6, 6);
  diag_layout->setSpacing(6);
  v.context_status_label = new QLabel("DisplayContext: (not initialized)", diag_page);
  v.context_status_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  makeWidthNeutral(v.context_status_label);
  auto * diag_buttons = new QHBoxLayout();
  v.refresh_button = new QPushButton("Refresh diagnostics", diag_page);
  v.capture_button = new QPushButton("Capture offscreen frame", diag_page);
  v.stereo_capture_button = new QPushButton("Capture stereo pair", diag_page);
  diag_buttons->addWidget(v.refresh_button);
  diag_buttons->addWidget(v.capture_button);
  diag_buttons->addWidget(v.stereo_capture_button);

  // The test markers: a script that publishes a few coloured markers for the test scenes. The
  // launchers say which python and which script to run (QAR_PANEL_TEST_MARKERS_PYTHON and
  // QAR_PANEL_TEST_MARKERS_SCRIPT); without them the button is disabled and its tool tip says what
  // is missing. refreshTestMarkersUi() sets the text, the state and the tool tip.
  auto * marker_row = new QHBoxLayout();
  v.test_markers_button = new QPushButton("Start test markers", diag_page);
  marker_row->addWidget(v.test_markers_button);
  marker_row->addStretch(1);

  // The simulated peers of the offline preview. In a QarOS session they are an advanced section
  // (shown by "Show advanced options"); in the preview they are how the panel works, so
  // refreshAdvancedVisibility() always shows them there.
  v.sim_peers_group = new QGroupBox("Simulated peers", diag_page);
  auto * sim_layout = new QVBoxLayout(v.sim_peers_group);
  auto * peer_buttons = new QHBoxLayout();
  v.add_sim_peer_button = new QPushButton("Add simulated peer", v.sim_peers_group);
  v.add_sim_peer_button->setToolTip(
    "Add a simulated peer with its own stereo stream of the RViz scene, for the offline "
    "preview.");
  v.remove_peer_button = new QPushButton("Remove peer", v.sim_peers_group);
  peer_buttons->addWidget(v.add_sim_peer_button);
  peer_buttons->addWidget(v.remove_peer_button);
  v.peer_list = new QListWidget(v.sim_peers_group);
  set_minimum_rows(v.peer_list);
  auto * stream_buttons = new QHBoxLayout();
  v.start_stream_button = new QPushButton("Start stream", v.sim_peers_group);
  v.stop_stream_button = new QPushButton("Stop stream", v.sim_peers_group);
  stream_buttons->addWidget(v.start_stream_button);
  stream_buttons->addWidget(v.stop_stream_button);
  sim_layout->addLayout(peer_buttons);
  sim_layout->addWidget(v.peer_list);
  sim_layout->addLayout(stream_buttons);

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
  //    pointer there is a ray. The box holds ten lines of the widget's own
  //    font, both bars are off, and updateArPanelStatus() shortens the text
  //    until it fits and says how many panel lines it left out. The text is
  //    selectable for copying into a report.
  auto * counters_group = new QGroupBox("AR panel counters", diag_page);
  auto * counters_layout = new QVBoxLayout(counters_group);
  const ReadoutBox readout = make_readout_box(
    counters_group, QStringLiteral("AR panels: none"),
    QStringLiteral(
      "Live AR panel counters. This box never resizes and never scrolls: it is inside the "
      "widget being streamed, and a resize here would rebuild the AR panel it is reporting on. "
      "The long counters of every panel are in the log."));
  v.ar_panel_status_label = readout.label;
  v.ar_panel_status_scroll = readout.area;
  counters_layout->addWidget(v.ar_panel_status_scroll);

  v.diagnostics_view = new QPlainTextEdit(diag_page);
  v.diagnostics_view->setReadOnly(true);
  v.diagnostics_view->setMaximumBlockCount(2000);
  set_minimum_rows(v.diagnostics_view);
  diag_layout->addWidget(v.context_status_label);
  diag_layout->addLayout(diag_buttons);
  diag_layout->addLayout(marker_row);
  diag_layout->addWidget(v.sim_peers_group);
  diag_layout->addWidget(counters_group);
  diag_layout->addWidget(v.diagnostics_view, 1);
  add_page("Diagnostics", diag_page);

  // The pages scroll. A wheel turned over a page must scroll it, not change the setting (or arm
  // the pick mode) of the control under the pointer; a click gives a control the wheel.
  for (QWidget * control : {
      static_cast<QWidget *>(v.resolution_combo), static_cast<QWidget *>(v.fps_combo),
      static_cast<QWidget *>(v.ar_copy_size_combo), static_cast<QWidget *>(v.close_mode_combo),
      static_cast<QWidget *>(v.ar_panel_scale_spin), static_cast<QWidget *>(v.peer_kind_combo),
      static_cast<QWidget *>(v.spatial_mode_combo), static_cast<QWidget *>(v.spatial_target_combo),
      static_cast<QWidget *>(v.spatial_topic_combo)})
  {
    guard_wheel(control);
  }

  // What scrolls by design: a list grows with what the operator adds to it and a log with what
  // happened. The scroll-area census leaves these out when it asks whether anything that must not
  // scroll in AR does (the readout is marked by make_readout_box()). The pages themselves are not
  // marked: a page that scrolls is a panel that is too low.
  for (QWidget * area : {
      static_cast<QWidget *>(v.topic_watch_list), static_cast<QWidget *>(v.exposure_list),
      static_cast<QWidget *>(v.peer_list), static_cast<QWidget *>(v.diagnostics_view),
      static_cast<QWidget *>(v.hub_widget->target_list())})
  {
    area->setProperty(qar_qt_streaming::kScrollsByDesignProperty, true);
  }

  return v;
}

namespace
{

/// The readout label of the counters box. Its text is fitted to the width of the box that shows
/// it, so the twin gets its own text from the caller (set_view_readout()), never a copy of the
/// text that was fitted for the other box.
struct ReadoutLabel
{
  QLabel * label = nullptr;
};

/// Calls @p pair(original control, twin control) once for every control that carries state, in
/// the order of the pages. The status lines are elided labels: they get the overload for
/// ElidedLabel, the readout label the one for ReadoutLabel, every other control the one for
/// QWidget. One table serves the binding and the count.
template<typename Pair>
void for_each_pair(const QarPanelView & o, const QarPanelView & t, Pair && pair)
{
  // The status strip
  pair(o.runtime_state_label, t.runtime_state_label);
  pair(o.last_error_label, t.last_error_label);
  // Connection
  pair(o.start_qar_button, t.start_qar_button);
  pair(o.stop_qar_button, t.stop_qar_button);
  pair(o.invite_group, t.invite_group);
  pair(o.peer_kind_combo, t.peer_kind_combo);
  pair(o.peer_hostname_edit, t.peer_hostname_edit);
  pair(o.add_peer_button, t.add_peer_button);
  pair(o.ar_control_status_label, t.ar_control_status_label);
  pair(o.ar_control_show_button, t.ar_control_show_button);
  pair(o.ar_control_copy_button, t.ar_control_copy_button);
  // AR tools
  pair(o.add_topic_button, t.add_topic_button);
  pair(o.remove_topic_button, t.remove_topic_button);
  pair(o.topic_watch_list, t.topic_watch_list);
  pair(o.exposure_list, t.exposure_list);
  pair(o.remove_exposure_button, t.remove_exposure_button);
  pair(o.toggle_writable_button, t.toggle_writable_button);
  pair(o.reopen_exposure_button, t.reopen_exposure_button);
  pair(o.copy_url_button, t.copy_url_button);
  pair(o.ar_panel_scale_spin, t.ar_panel_scale_spin);
  pair(o.exposure_status_label, t.exposure_status_label);
  // Picking
  pair(o.spatial_mode_combo, t.spatial_mode_combo);
  pair(o.spatial_topic_combo, t.spatial_topic_combo);
  pair(o.spatial_target_combo, t.spatial_target_combo);
  pair(o.spatial_frame_edit, t.spatial_frame_edit);
  pair(o.spatial_auto_confirm, t.spatial_auto_confirm);
  pair(o.spatial_confirm_button, t.spatial_confirm_button);
  pair(o.spatial_cancel_button, t.spatial_cancel_button);
  pair(o.spatial_close_polygon_button, t.spatial_close_polygon_button);
  pair(o.spatial_status_label, t.spatial_status_label);
  // Settings
  pair(o.resolution_combo, t.resolution_combo);
  pair(o.fps_combo, t.fps_combo);
  pair(o.ar_copy_size_combo, t.ar_copy_size_combo);
  pair(o.hidden_displays_checkbox, t.hidden_displays_checkbox);
  pair(o.ar_control_checkbox, t.ar_control_checkbox);
  pair(o.show_advanced_checkbox, t.show_advanced_checkbox);
  pair(o.advanced_group, t.advanced_group);
  pair(o.prime_checkbox, t.prime_checkbox);
  pair(o.close_mode_combo, t.close_mode_combo);
  pair(o.ar_visibility_checkbox, t.ar_visibility_checkbox);
  // Diagnostics
  pair(o.context_status_label, t.context_status_label);
  pair(o.refresh_button, t.refresh_button);
  pair(o.capture_button, t.capture_button);
  pair(o.stereo_capture_button, t.stereo_capture_button);
  pair(o.test_markers_button, t.test_markers_button);
  pair(o.sim_peers_group, t.sim_peers_group);
  pair(o.add_sim_peer_button, t.add_sim_peer_button);
  pair(o.remove_peer_button, t.remove_peer_button);
  pair(o.peer_list, t.peer_list);
  pair(o.start_stream_button, t.start_stream_button);
  pair(o.stop_stream_button, t.stop_stream_button);
  pair(ReadoutLabel{o.ar_panel_status_label}, ReadoutLabel{t.ar_panel_status_label});
  pair(o.diagnostics_view, t.diagnostics_view);
}

/// Binds the pairs one by one and counts the ones the binder took.
class PairBinder
{
public:
  explicit PairBinder(qar_qt_streaming::WidgetTwinBinder & binder)
  : binder_(binder) {}

  int bound() const {return bound_;}

  void operator()(QWidget * original, QWidget * twin)
  {
    if (binder_.bind(original, twin)) {
      ++bound_;
    }
  }

  /// The elided labels keep the whole text in a member of their own: it is what the twin copies.
  /// The style sheet is copied as well: it carries the weight and the colour of the phase.
  void operator()(ElidedLabel * original, ElidedLabel * twin)
  {
    const bool taken = binder_.bind_custom(
      original, twin, [](QWidget & from, QWidget & to) {
        const QString & text = static_cast<ElidedLabel &>(from).fullText();
        ElidedLabel & label = static_cast<ElidedLabel &>(to);
        if (label.fullText() != text) {
          label.setFullText(text);
        }
        if (label.styleSheet() != from.styleSheet()) {
          label.setStyleSheet(from.styleSheet());
        }
      });
    if (taken) {
      ++bound_;
    }
  }

  /// The enabled state, the hide and the tool tip follow the original; the text does not.
  void operator()(const ReadoutLabel & original, const ReadoutLabel & twin)
  {
    if (binder_.bind_custom(original.label, twin.label, [](QWidget &, QWidget &) {})) {
      ++bound_;
    }
  }

private:
  qar_qt_streaming::WidgetTwinBinder & binder_;
  int bound_ = 0;
};

/// Counts the pairs of the table.
class PairCounter
{
public:
  int count() const {return count_;}
  void operator()(QWidget *, QWidget *) {++count_;}
  void operator()(ElidedLabel *, ElidedLabel *) {++count_;}
  void operator()(const ReadoutLabel &, const ReadoutLabel &) {++count_;}

private:
  int count_ = 0;
};

}  // namespace

int bind_qar_panel_views(
  qar_qt_streaming::WidgetTwinBinder & binder, const QarPanelView & original,
  const QarPanelView & twin)
{
  PairBinder pairs(binder);
  for_each_pair(original, twin, pairs);
  return pairs.bound();
}

void remeasure_minimum_rows(QWidget & root)
{
  for (QWidget * widget : root.findChildren<QWidget *>()) {
    const QVariant rows = widget->property(kMinimumRowsProperty);
    if (rows.isValid()) {
      widget->setMinimumHeight(minimum_rows_height(*widget, rows.toInt(), true));
    }
  }
}

int qar_panel_view_bound_count()
{
  const QarPanelView none;
  PairCounter counter;
  for_each_pair(none, none, counter);
  return counter.count();
}

int readout_text_width(const QarPanelView & view)
{
  if (view.ar_panel_status_scroll == nullptr) {
    return 0;
  }
  // The page of the readout is on show: the box knows its own width.
  if (view.ar_panel_status_scroll->isVisible()) {
    return view.ar_panel_status_scroll->viewport()->width();
  }
  // Another page is on show. A page that is not on show keeps the size it had when it was last
  // laid out, so its box is no guide; all pages share one viewport, and the readout is that wide
  // less the margins of its page and the frame of the box.
  int width = 0;
  if (const auto * current = qobject_cast<const QScrollArea *>(
      view.tabs != nullptr ? view.tabs->currentWidget() : nullptr))
  {
    width = current->viewport()->width();
    // The margins of the group and of the page, up to the page that the viewport holds.
    for (const QWidget * level = view.ar_panel_status_scroll->parentWidget(); level != nullptr;
      level = level->parentWidget())
    {
      width -= level->contentsMargins().left() + level->contentsMargins().right();
      if (const QLayout * layout = level->layout()) {
        width -= layout->contentsMargins().left() + layout->contentsMargins().right();
      }
      const QWidget * viewport = level->parentWidget();
      if (viewport != nullptr && qobject_cast<const QScrollArea *>(viewport->parentWidget())) {
        break;
      }
    }
    width -= 2 * view.ar_panel_status_scroll->frameWidth();
  }
  // A panel that has not been laid out yet: the width its dock starts with, less the margins.
  return width > 100 ? width : kDefaultDockWidth - 60;
}

int set_view_readout(
  const QarPanelView & view, const QStringList & head, const QStringList & items,
  const QString & more_format)
{
  if (view.ar_panel_status_scroll == nullptr || view.ar_panel_status_label == nullptr) {
    return 0;
  }
  const ReadoutBox box{view.ar_panel_status_scroll, view.ar_panel_status_label};
  return set_readout_text(box, head, items, more_format, readout_text_width(view));
}

}  // namespace qar_rviz_streaming
