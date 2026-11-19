// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the view builder of the Qar Streaming panel (qar_panel_view.hpp), offscreen: every
// control and the six pages exist, two builds have the same structure, every control that carries
// state has a twin to bind, and the panel's own window keeps its size rules.

#include "qar_rviz_streaming/qar_panel_view.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QWidget>

#include "qar_qt_streaming/widget_twin.hpp"
#include "qar_rviz_streaming/ar_panel_list_widget.hpp"
#include "qar_rviz_streaming/hub_connection_widget.hpp"
#include "qar_rviz_streaming/stable_panel_layout.hpp"

namespace
{

using qar_rviz_streaming::build_qar_panel_view;
using qar_rviz_streaming::QarPanelView;
using qar_rviz_streaming::QarPanelViewInputs;

QarPanelViewInputs make_inputs()
{
  QarPanelViewInputs inputs;
  inputs.spatial_targets = {
    {QStringLiteral("Action Point"), QStringLiteral("The runtime's own intersection."), 1},
    {QStringLiteral("Ground plane"), QStringLiteral("The ray meets z = 0."), 0}};
  inputs.spatial_target_default_id = 1;
  inputs.invite_tool_tip = QStringLiteral("Connect first.");
  return inputs;
}

std::map<std::string, int> class_counts(QWidget & root)
{
  std::map<std::string, int> counts;
  for (QWidget * child : root.findChildren<QWidget *>()) {
    ++counts[child->metaObject()->className()];
  }
  return counts;
}

TEST(QarPanelView, TheBuilderMakesEveryControlAndTheSixPages)
{
  QWidget root;
  const QarPanelView v = build_qar_panel_view(root, make_inputs());

  std::vector<std::pair<const char *, const QObject *>> controls;
#define QAR_CONTROL(name) controls.emplace_back(#name, v.name)
  QAR_CONTROL(status_strip);
  QAR_CONTROL(runtime_state_label);
  QAR_CONTROL(last_error_label);
  QAR_CONTROL(tabs);
  QAR_CONTROL(start_qar_button);
  QAR_CONTROL(stop_qar_button);
  QAR_CONTROL(hub_widget);
  QAR_CONTROL(invite_group);
  QAR_CONTROL(peer_kind_combo);
  QAR_CONTROL(peer_hostname_edit);
  QAR_CONTROL(add_peer_button);
  QAR_CONTROL(ar_control_status_label);
  QAR_CONTROL(ar_control_show_button);
  QAR_CONTROL(ar_control_copy_button);
  QAR_CONTROL(ar_panel_list);
  QAR_CONTROL(add_topic_button);
  QAR_CONTROL(remove_topic_button);
  QAR_CONTROL(topic_watch_list);
  QAR_CONTROL(exposure_list);
  QAR_CONTROL(remove_exposure_button);
  QAR_CONTROL(toggle_writable_button);
  QAR_CONTROL(reopen_exposure_button);
  QAR_CONTROL(copy_url_button);
  QAR_CONTROL(ar_panel_scale_spin);
  QAR_CONTROL(exposure_status_label);
  QAR_CONTROL(spatial_mode_combo);
  QAR_CONTROL(spatial_target_combo);
  QAR_CONTROL(spatial_topic_combo);
  QAR_CONTROL(spatial_frame_edit);
  QAR_CONTROL(spatial_auto_confirm);
  QAR_CONTROL(spatial_confirm_button);
  QAR_CONTROL(spatial_cancel_button);
  QAR_CONTROL(spatial_close_polygon_button);
  QAR_CONTROL(spatial_status_label);
  QAR_CONTROL(resolution_combo);
  QAR_CONTROL(fps_combo);
  QAR_CONTROL(ar_copy_size_combo);
  QAR_CONTROL(hidden_displays_checkbox);
  QAR_CONTROL(ar_control_checkbox);
  QAR_CONTROL(show_advanced_checkbox);
  QAR_CONTROL(advanced_group);
  QAR_CONTROL(prime_checkbox);
  QAR_CONTROL(close_mode_combo);
  QAR_CONTROL(ar_visibility_checkbox);
  QAR_CONTROL(context_status_label);
  QAR_CONTROL(refresh_button);
  QAR_CONTROL(capture_button);
  QAR_CONTROL(stereo_capture_button);
  QAR_CONTROL(test_markers_button);
  QAR_CONTROL(sim_peers_group);
  QAR_CONTROL(add_sim_peer_button);
  QAR_CONTROL(remove_peer_button);
  QAR_CONTROL(peer_list);
  QAR_CONTROL(start_stream_button);
  QAR_CONTROL(stop_stream_button);
  QAR_CONTROL(ar_panel_status_scroll);
  QAR_CONTROL(ar_panel_status_label);
  QAR_CONTROL(diagnostics_view);
#undef QAR_CONTROL
  EXPECT_EQ(controls.size(), 58u);
  for (const auto & control : controls) {
    EXPECT_NE(control.second, nullptr) << control.first;
    if (control.second != nullptr) {
      EXPECT_TRUE(root.isAncestorOf(static_cast<const QWidget *>(control.second)))
        << control.first;
    }
  }

  ASSERT_EQ(v.page_contents.size(), 6u);
  ASSERT_EQ(v.tabs->count(), 6);
  const char * titles[] = {"Connection", "AR panels", "AR tools", "Picking", "Settings",
    "Diagnostics"};
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(v.tabs->tabText(i), QString::fromLatin1(titles[i]));
  }
  // The pages are made as children of the root and end inside the scroll areas of the tabs.
  for (QWidget * page : v.page_contents) {
    EXPECT_TRUE(v.tabs->isAncestorOf(page));
  }
}

TEST(QarPanelView, ThePickTargetsAndTheInviteToolTipComeFromTheInputs)
{
  QWidget root;
  const QarPanelViewInputs inputs = make_inputs();
  const QarPanelView v = build_qar_panel_view(root, inputs);

  ASSERT_EQ(v.spatial_target_combo->count(), 2);
  EXPECT_EQ(v.spatial_target_combo->itemText(0), QStringLiteral("Action Point"));
  EXPECT_EQ(v.spatial_target_combo->itemData(1).toInt(), 0);
  EXPECT_EQ(v.spatial_target_combo->currentData().toInt(), 1);
  EXPECT_TRUE(v.spatial_target_combo->toolTip().startsWith(
      QStringLiteral("What the pointer is resolved against.")));
  EXPECT_TRUE(v.spatial_target_combo->toolTip().contains(QStringLiteral("Ground plane")));
  EXPECT_EQ(v.add_peer_button->toolTip(), inputs.invite_tool_tip);
  EXPECT_STREQ(
    v.toggle_writable_button->toolTip().toUtf8().constData(),
    qar_rviz_streaming::kWritableToggleToolTip);
}

TEST(QarPanelView, TwoBuildsHaveTheSameStructure)
{
  QWidget first;
  QWidget second;
  build_qar_panel_view(first, make_inputs());
  QarPanelViewInputs by_style = make_inputs();
  by_style.rows_by_style = true;
  build_qar_panel_view(second, by_style);
  EXPECT_EQ(class_counts(first), class_counts(second));
  EXPECT_GT(class_counts(first).size(), 10u);
}

TEST(QarPanelView, EveryPairIsBoundAndOfAKnownKind)
{
  QWidget original_root;
  QWidget twin_root;
  const QarPanelView original = build_qar_panel_view(original_root, make_inputs());
  const QarPanelView twin = build_qar_panel_view(twin_root, make_inputs());

  qar_qt_streaming::WidgetTwinBinder binder;
  const int bound = qar_rviz_streaming::bind_qar_panel_views(binder, original, twin);
  EXPECT_EQ(bound, qar_rviz_streaming::qar_panel_view_bound_count());
  EXPECT_EQ(bound, 53);
  EXPECT_EQ(binder.counters().pairs, 53u);
  EXPECT_EQ(binder.counters().plain_pairs, 0u);

  // A second bind of the same originals is refused: nothing more is bound.
  EXPECT_EQ(qar_rviz_streaming::bind_qar_panel_views(binder, original, twin), 0);
  EXPECT_EQ(binder.counters().pairs, 53u);
}

TEST(QarPanelView, TheDesktopSizeRulesAreKept)
{
  QWidget root;
  const QarPanelView v = build_qar_panel_view(root, make_inputs());

  // rows_by_style false: the least height of a list or a log is the line spacing times the rows
  // plus six pixels, as it always was.
  const auto least = [](const QWidget * widget, int rows) {
      return widget->fontMetrics().lineSpacing() * rows + 6;
    };
  EXPECT_EQ(v.topic_watch_list->minimumHeight(), least(v.topic_watch_list, 2));
  EXPECT_EQ(v.exposure_list->minimumHeight(), least(v.exposure_list, 2));
  EXPECT_EQ(v.peer_list->minimumHeight(), least(v.peer_list, 4));
  EXPECT_EQ(v.diagnostics_view->minimumHeight(), least(v.diagnostics_view, 4));

  // The publish toggle is as wide as the wider of its two texts, whichever it shows.
  int widest = 0;
  const QString shown = v.toggle_writable_button->text();
  for (const char * text : {"Allow publishing", "Make read-only"}) {
    v.toggle_writable_button->setText(QString::fromLatin1(text));
    widest = std::max(widest, v.toggle_writable_button->sizeHint().width());
  }
  v.toggle_writable_button->setText(shown);
  EXPECT_EQ(v.toggle_writable_button->minimumWidth(), widest);
  EXPECT_EQ(v.toggle_writable_button->maximumWidth(), widest);
  EXPECT_FALSE(v.toggle_writable_button->isEnabled());

  // The lists and the readout are marked as areas that scroll by design (or never scroll).
  for (const QWidget * area : {static_cast<const QWidget *>(v.topic_watch_list),
      static_cast<const QWidget *>(v.exposure_list), static_cast<const QWidget *>(v.peer_list),
      static_cast<const QWidget *>(v.diagnostics_view),
      static_cast<const QWidget *>(v.hub_widget->target_list())})
  {
    EXPECT_TRUE(area->property("qar_scrolls_by_design").toBool());
  }

  // The combos and the spin box do not take the wheel without the focus.
  EXPECT_EQ(v.fps_combo->focusPolicy(), Qt::StrongFocus);
  EXPECT_EQ(v.ar_panel_scale_spin->focusPolicy(), Qt::StrongFocus);
}

TEST(QarPanelView, TheValuesThatComeFromThePanelAreNotSetByTheBuilder)
{
  QWidget root;
  const QarPanelView v = build_qar_panel_view(root, make_inputs());
  // The scale keeps the range of the builder and the prime box is unchecked: the panel sets the
  // value and the check state from its settings and its environment after the build.
  EXPECT_DOUBLE_EQ(v.ar_panel_scale_spin->minimum(), 0.25);
  EXPECT_DOUBLE_EQ(v.ar_panel_scale_spin->maximum(), 4.0);
  EXPECT_FALSE(v.prime_checkbox->isChecked());
}

}  // namespace
