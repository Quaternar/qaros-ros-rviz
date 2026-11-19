// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__QAR_PANEL_VIEW_HPP_
#define QAR_RVIZ_STREAMING__QAR_PANEL_VIEW_HPP_

/// @file qar_panel_view.hpp
/// @brief The widgets of the Qar Streaming panel as a function: a status strip over a tab widget
/// of six scroll pages, built into a root widget of the caller's. Qt Widgets and the panel's
/// layout helpers only: no RViz, no ROS, no QAROS.
///
/// The panel builds one view for its own window. The AR copy of the panel builds a second view
/// with the same function under another style and binds its controls to those of the first
/// (see bind_qar_panel_views()), so the two look the same and are laid out by the same code.

#include <vector>

#include <QString>
#include <QStringList>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QFrame;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QTabWidget;
class QWidget;

namespace qar_qt_streaming
{
class WidgetTwinBinder;
}  // namespace qar_qt_streaming

namespace qar_rviz_streaming
{

class ArPanelListWidget;
class ElidedLabel;
class HubConnectionWidget;

/// What the builder needs from the panel and cannot know itself.
struct QarPanelViewInputs
{
  /// One entry of the pick target drop-down.
  struct Target
  {
    QString label;
    QString help;
    int id = 0;
  };
  /// The entries of the pick target drop-down, in order.
  std::vector<Target> spatial_targets;
  /// The id the drop-down starts on.
  int spatial_target_default_id = 0;
  /// The tool tip of the invite button of a panel that is not connected.
  QString invite_tool_tip;
  /// The least height of a list is measured with one probe row under the widget's style (true,
  /// the AR copy) or as line spacing times the rows (false, the panel's own window).
  bool rows_by_style = false;
};

/// The controls of one view of the panel. build_qar_panel_view() sets every pointer. The widgets
/// belong to the root that was built into.
struct QarPanelView
{
  QFrame * status_strip = nullptr;
  ElidedLabel * runtime_state_label = nullptr;
  ElidedLabel * last_error_label = nullptr;
  QTabWidget * tabs = nullptr;
  std::vector<QWidget *> page_contents;

  // Connection
  QPushButton * start_qar_button = nullptr;
  QPushButton * stop_qar_button = nullptr;
  HubConnectionWidget * hub_widget = nullptr;
  QGroupBox * invite_group = nullptr;
  QComboBox * peer_kind_combo = nullptr;
  QLineEdit * peer_hostname_edit = nullptr;
  QPushButton * add_peer_button = nullptr;
  QLabel * ar_control_status_label = nullptr;
  QPushButton * ar_control_show_button = nullptr;
  QPushButton * ar_control_copy_button = nullptr;

  // AR panels
  ArPanelListWidget * ar_panel_list = nullptr;

  // AR tools
  QPushButton * add_topic_button = nullptr;
  QPushButton * remove_topic_button = nullptr;
  QListWidget * topic_watch_list = nullptr;
  QListWidget * exposure_list = nullptr;
  QPushButton * remove_exposure_button = nullptr;
  QPushButton * toggle_writable_button = nullptr;
  QPushButton * reopen_exposure_button = nullptr;
  QPushButton * copy_url_button = nullptr;
  QDoubleSpinBox * ar_panel_scale_spin = nullptr;
  QLabel * exposure_status_label = nullptr;

  // Picking
  QComboBox * spatial_mode_combo = nullptr;
  QComboBox * spatial_target_combo = nullptr;
  QComboBox * spatial_topic_combo = nullptr;
  QLineEdit * spatial_frame_edit = nullptr;
  QCheckBox * spatial_auto_confirm = nullptr;
  QPushButton * spatial_confirm_button = nullptr;
  QPushButton * spatial_cancel_button = nullptr;
  QPushButton * spatial_close_polygon_button = nullptr;
  QLabel * spatial_status_label = nullptr;

  // Settings
  QComboBox * resolution_combo = nullptr;
  QComboBox * fps_combo = nullptr;
  QComboBox * ar_copy_size_combo = nullptr;
  QCheckBox * hidden_displays_checkbox = nullptr;
  QCheckBox * ar_control_checkbox = nullptr;
  QCheckBox * show_advanced_checkbox = nullptr;
  QGroupBox * advanced_group = nullptr;
  QCheckBox * prime_checkbox = nullptr;
  QComboBox * close_mode_combo = nullptr;
  QCheckBox * ar_visibility_checkbox = nullptr;

  // Diagnostics
  QLabel * context_status_label = nullptr;
  QPushButton * refresh_button = nullptr;
  QPushButton * capture_button = nullptr;
  QPushButton * stereo_capture_button = nullptr;
  QPushButton * test_markers_button = nullptr;
  QGroupBox * sim_peers_group = nullptr;
  QPushButton * add_sim_peer_button = nullptr;
  QPushButton * remove_peer_button = nullptr;
  QListWidget * peer_list = nullptr;
  QPushButton * start_stream_button = nullptr;
  QPushButton * stop_stream_button = nullptr;
  QScrollArea * ar_panel_status_scroll = nullptr;
  QLabel * ar_panel_status_label = nullptr;
  QPlainTextEdit * diagnostics_view = nullptr;
};

/// The tool tip of the publish toggle of the AR tools list.
extern const char * const kWritableToggleToolTip;

/// @brief Builds the status strip and the six pages into @p root, which gets the top layout.
/// The texts, tool tips, size rules (scroll pages, width-neutral labels, fixed widths, the wheel
/// guard, the marks of what scrolls by design) are the same for every view. The controls start in
/// their built state: a value that comes from the panel's settings or environment, the state of
/// the buttons and the content of the lists are the caller's to set (or, for a copy, to bind).
QarPanelView build_qar_panel_view(QWidget & root, const QarPanelViewInputs & inputs);

/// @brief Binds every control of @p original that carries state to the same control of @p twin,
/// in a fixed order. The composite widgets (the AR panel list and the Hub group), the status
/// strip, the tab widget and the readout scroll area carry no pair: the caller feeds the two
/// composites itself, and the tabs and scroll positions belong to each view.
/// @return the number of pairs bound; the caller compares it with qar_panel_view_bound_count().
int bind_qar_panel_views(
  qar_qt_streaming::WidgetTwinBinder & binder, const QarPanelView & original,
  const QarPanelView & twin);

/// @brief Measures the lists and logs below @p root again that the builder gave a least height
/// of some rows, under the style they use now (a probe row for a list, the line spacing for a
/// log). Call it after the style of the tree changed.
void remeasure_minimum_rows(QWidget & root);

/// The number of pairs bind_qar_panel_views() binds when every control exists.
int qar_panel_view_bound_count();

/// @brief The width in pixels that the text of the counters box of @p view has, or the width of a
/// default dock less its margins for a view that has not been laid out. When the page of the box is
/// not on show the width is taken from the page that is: all pages share one viewport.
int readout_text_width(const QarPanelView & view);

/// @brief Shows @p head and as many of @p items as fit in the counters box of @p view, shortened
/// for the width of that box (see set_readout_text()). The readout label is no pair of the binder:
/// every view of the panel gets its own call, so a view that is narrower than the other drops more
/// lines instead of clipping the tail unannounced.
/// @return the number of items left out; 0 when the view has no counters box.
int set_view_readout(
  const QarPanelView & view, const QStringList & head, const QStringList & items,
  const QString & more_format);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__QAR_PANEL_VIEW_HPP_
