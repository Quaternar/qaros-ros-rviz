// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the "QAROS Hub" group of the Connection tab (hub_connection_widget.hpp), offscreen.
// The group holds no session logic, so each test gives it the view of one state and checks what
// the person sees and what the group reports back. The offscreen platform plugin rasterizes no
// glyphs and its font is not the desktop's: no test asserts on a pixel or an absolute size, only
// on a size before and after.

#include "qar_rviz_streaming/hub_connection_widget.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSize>
#include <QString>
#include <QStringList>

#include "qar_rviz_streaming/connection_view.hpp"

namespace
{

using qar_rviz_streaming::ConnectionInput;
using qar_rviz_streaming::ConnectionPhase;
using qar_rviz_streaming::ConnectionView;
using qar_rviz_streaming::HubConnectionWidget;
using qar_rviz_streaming::HubTargetRow;
using qar_rviz_streaming::make_connection_view;

ConnectionInput input_of(ConnectionPhase phase)
{
  ConnectionInput input;
  input.phase = phase;
  return input;
}

/// Counts what the group reports.
class Reports
{
public:
  explicit Reports(HubConnectionWidget & widget)
  {
    QObject::connect(&widget, &HubConnectionWidget::cancelRequested, [this]() { ++cancels; });
  }

  int cancels = 0;
};

/// The hint of @p widget after the layout has seen every change.
QSize settled_hint(QWidget & widget)
{
  QCoreApplication::sendPostedEvents();
  QCoreApplication::processEvents();
  if (widget.layout() != nullptr) {
    widget.layout()->invalidate();
    widget.layout()->activate();
  }
  return widget.sizeHint();
}

QSize settled_minimum_hint(QWidget & widget)
{
  QCoreApplication::sendPostedEvents();
  QCoreApplication::processEvents();
  if (widget.layout() != nullptr) {
    widget.layout()->invalidate();
    widget.layout()->activate();
  }
  return widget.minimumSizeHint();
}

}  // namespace

TEST(HubConnectionWidget, TheOfflinePreviewShowsTheGroupWithNothingToCancel)
{
  HubConnectionWidget widget;
  widget.apply(make_connection_view(input_of(ConnectionPhase::Preview)));
  EXPECT_EQ(widget.title(), "QAROS Hub");
  EXPECT_EQ(
    widget.state_label()->text(),
    "QarOS: preview mode - set QAR_PANEL_LIVE_STREAM to connect");
  EXPECT_EQ(widget.identity_label()->text(), "This PC (not joined yet)");
  EXPECT_FALSE(widget.cancel_button()->isEnabled());
  EXPECT_EQ(widget.target_list()->count(), 0);
}

TEST(HubConnectionWidget, HasNoModeHostPortCodePairOrForget)
{
  HubConnectionWidget widget;
  EXPECT_TRUE(widget.findChildren<QComboBox *>().isEmpty());
  EXPECT_TRUE(widget.findChildren<QLineEdit *>().isEmpty());
  EXPECT_TRUE(widget.findChildren<QAbstractSpinBox *>().isEmpty());
  // Cancel is the only button of the group.
  const QList<QAbstractButton *> buttons = widget.findChildren<QAbstractButton *>();
  ASSERT_EQ(buttons.size(), 1);
  EXPECT_EQ(buttons.front(), widget.cancel_button());
  QStringList labels;
  for (const QLabel * label : widget.findChildren<QLabel *>()) {
    labels << label->text();
  }
  for (const QString & removed : {"Hub:", "Host:", "Port:", "Code:"}) {
    EXPECT_FALSE(labels.contains(removed)) << removed.toStdString();
  }
  // What stays: the identity line, the state line and the targets.
  EXPECT_TRUE(labels.contains("Identity:"));
  EXPECT_TRUE(labels.contains("State:"));
  EXPECT_TRUE(labels.contains("Targets:"));
  EXPECT_EQ(widget.identity_label()->text(), "This PC (not joined yet)");
  EXPECT_EQ(widget.state_label()->text(), "QarOS: not connected");
}

TEST(HubConnectionWidget, TheGroupToolTipSaysTheHubIsThatOfThisPc)
{
  HubConnectionWidget widget;
  EXPECT_EQ(
    widget.toolTip(),
    "The plugin joins the QAROS Hub of this PC. The Hub asks once whether rviz2 may join (click "
    "Allow in the QAROS window); the approval is revoked in the Visualizer. To reach the Hub of "
    "another PC, make the Hub of this PC a member of it.");
}

TEST(HubConnectionWidget, TheIdentityLineFollowsTheJoin)
{
  HubConnectionWidget widget;
  ConnectionInput running = input_of(ConnectionPhase::Running);
  running.program_name = "rviz2.exe";
  widget.apply(make_connection_view(running));
  EXPECT_EQ(widget.identity_label()->text(), "This PC: approved local app (rviz2.exe)");
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle)));
  EXPECT_EQ(widget.identity_label()->text(), "This PC (not joined yet)");
}

TEST(HubConnectionWidget, CancelReportsItself)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  widget.apply(make_connection_view(input_of(ConnectionPhase::WaitingForApproval)));
  ASSERT_TRUE(widget.cancel_button()->isEnabled());
  widget.cancel_button()->click();
  EXPECT_EQ(reports.cancels, 1);
}

TEST(HubConnectionWidget, TheCancelButtonTakesTheTextOfTheView)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle)));
  EXPECT_EQ(widget.cancel_button()->text(), "Cancel");
  const QString plain_tool_tip = widget.cancel_button()->toolTip();
  EXPECT_EQ(
    plain_tool_tip, "Stop loading QAROS, waiting for the approval in the Hub or for the rejoin.");

  // The Hub does not answer: the button joins again, and says what it does.
  widget.apply(make_connection_view(input_of(ConnectionPhase::Reconnecting)));
  EXPECT_EQ(widget.cancel_button()->text(), "Join again");
  EXPECT_TRUE(widget.cancel_button()->isEnabled());
  EXPECT_TRUE(widget.cancel_button()->toolTip().contains("join the Hub again"));
  widget.cancel_button()->click();
  EXPECT_EQ(reports.cancels, 1);

  // The session is lost: the button stops the automatic join.
  ConnectionInput lost = input_of(ConnectionPhase::Lost);
  lost.auto_rejoin = true;
  lost.rejoin_attempt = 1;
  lost.rejoin_in_s = 2;
  widget.apply(make_connection_view(lost));
  EXPECT_EQ(widget.cancel_button()->text(), "Stop rejoining");
  EXPECT_TRUE(widget.cancel_button()->isEnabled());
  EXPECT_TRUE(widget.cancel_button()->toolTip().contains("Stop joining the QAROS Hub again"));
  widget.cancel_button()->click();
  EXPECT_EQ(reports.cancels, 2);

  // Back to a plain join: the first words and the first tool tip return.
  widget.apply(make_connection_view(input_of(ConnectionPhase::WaitingForApproval)));
  EXPECT_EQ(widget.cancel_button()->text(), "Cancel");
  EXPECT_EQ(widget.cancel_button()->toolTip(), plain_tool_tip);
}

TEST(HubConnectionWidget, CancelKeepsItsWidthAcrossItsTexts)
{
  HubConnectionWidget widget;
  // The button has the room of its longest words from the start.
  int widest = 0;
  for (const char * text : {"Cancel", "Join again", "Stop rejoining"}) {
    widget.cancel_button()->setText(text);
    widest = std::max(widest, widget.cancel_button()->sizeHint().width());
  }
  widget.cancel_button()->setText("Cancel");
  EXPECT_GE(widget.cancel_button()->minimumWidth(), widest);
  const int width = widget.cancel_button()->minimumWidth();
  ConnectionInput lost = input_of(ConnectionPhase::Lost);
  lost.auto_rejoin = true;
  widget.apply(make_connection_view(lost));
  EXPECT_EQ(widget.cancel_button()->text(), "Stop rejoining");
  EXPECT_EQ(widget.cancel_button()->minimumWidth(), width);
  widget.apply(make_connection_view(input_of(ConnectionPhase::Reconnecting)));
  EXPECT_EQ(widget.cancel_button()->text(), "Join again");
  EXPECT_EQ(widget.cancel_button()->minimumWidth(), width);
}

TEST(HubConnectionWidget, TheWordsOfTheCancelButtonDoNotWidenTheGroup)
{
  HubConnectionWidget widget;
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle)));
  const QSize hint = settled_hint(widget);
  const QSize minimum = settled_minimum_hint(widget);
  ConnectionInput lost = input_of(ConnectionPhase::Lost);
  lost.auto_rejoin = true;
  widget.apply(make_connection_view(lost));
  EXPECT_EQ(widget.cancel_button()->text(), "Stop rejoining");
  EXPECT_EQ(settled_hint(widget).width(), hint.width());
  EXPECT_EQ(settled_minimum_hint(widget).width(), minimum.width());
  widget.apply(make_connection_view(input_of(ConnectionPhase::Reconnecting)));
  EXPECT_EQ(settled_hint(widget).width(), hint.width());
  EXPECT_EQ(settled_minimum_hint(widget).width(), minimum.width());
}

TEST(HubConnectionWidget, TheTargetListShowsOneRowPerTarget)
{
  HubConnectionWidget widget;
  HubTargetRow served;
  served.name = "Quest 3 of Ana";
  served.category = "headset";
  served.presence = "online";
  HubTargetRow capped;
  capped.name = "Receiver 2";
  capped.category = "Visualizer";
  capped.presence = "unknown";
  capped.served = false;
  capped.not_served_reason = "cap reached";
  widget.set_targets({served, capped});
  ASSERT_EQ(widget.target_list()->count(), 2);
  EXPECT_EQ(widget.target_list()->item(0)->text(), "Quest 3 of Ana - headset, online - served");
  EXPECT_EQ(
    widget.target_list()->item(1)->text(),
    "Receiver 2 - Visualizer, unknown - not served: cap reached");

  // The same rows again rebuild nothing (the items stay).
  QListWidgetItem * first = widget.target_list()->item(0);
  widget.set_targets({served, capped});
  EXPECT_EQ(widget.target_list()->item(0), first);

  widget.set_targets({});
  EXPECT_EQ(widget.target_list()->count(), 0);
  EXPECT_EQ(
    qar_rviz_streaming::hub_target_row_text(HubTargetRow{}), "(unnamed device) - served");
}

/// The height of one list row as the style gives it (an item is measured, not the font).
int list_row_height(QListWidget * list)
{
  list->addItem("Device");
  const int height = list->sizeHintForRow(0);
  list->clear();
  return height;
}

TEST(HubConnectionWidget, TheTargetListIsEightRowsHighWhateverTheHubHolds)
{
  HubConnectionWidget widget;
  QListWidget * list = widget.target_list();
  const int eight_rows = list_row_height(list) * 8 + 2 * list->frameWidth();
  EXPECT_EQ(list->minimumHeight(), eight_rows);
  EXPECT_EQ(list->maximumHeight(), eight_rows);
  EXPECT_EQ(list->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);

  // Targets come and go: the list keeps its height and scrolls instead.
  std::vector<HubTargetRow> rows;
  for (int i = 0; i < 12; ++i) {
    HubTargetRow row;
    row.name = "Device " + std::to_string(i);
    rows.push_back(row);
  }
  widget.set_targets(rows);
  EXPECT_EQ(list->count(), 12);
  EXPECT_EQ(list->minimumHeight(), eight_rows);
  EXPECT_EQ(list->maximumHeight(), eight_rows);
  widget.set_targets({});
  EXPECT_EQ(list->maximumHeight(), eight_rows);
}

TEST(HubConnectionWidget, EightTargetsFitTheListWithoutAScrollBarAndANinthNeedsOne)
{
  HubConnectionWidget widget;
  widget.show();
  QListWidget * list = widget.target_list();
  std::vector<HubTargetRow> rows;
  for (int i = 0; i < 9; ++i) {
    HubTargetRow row;
    row.name = "Device " + std::to_string(i);
    rows.push_back(row);
  }

  widget.set_targets(std::vector<HubTargetRow>(rows.begin(), rows.begin() + 8));
  settled_hint(widget);
  list->doItemsLayout();
  EXPECT_EQ(list->count(), 8);
  EXPECT_FALSE(list->verticalScrollBar()->isVisible());
  EXPECT_EQ(list->verticalScrollBar()->maximum(), 0);
  // The eighth row lies wholly inside the viewport.
  EXPECT_LE(list->visualItemRect(list->item(7)).bottom(), list->viewport()->rect().bottom());

  widget.set_targets(rows);
  settled_hint(widget);
  list->doItemsLayout();
  EXPECT_EQ(list->count(), 9);
  EXPECT_GT(list->verticalScrollBar()->maximum(), 0);
}

TEST(HubConnectionWidget, EightTargetRowsMakeTheGroupLowerThanNine)
{
  HubConnectionWidget widget;
  const int eight_rows = widget.target_list()->maximumHeight();
  const int eight_row_group = settled_minimum_hint(widget).height();
  const int one_row = list_row_height(widget.target_list());

  // The same group with a ninth row: one row higher, so the eight-row group is lower by one row.
  widget.target_list()->setFixedHeight(eight_rows + one_row);
  const int nine_row_group = settled_minimum_hint(widget).height();
  EXPECT_LT(eight_row_group, nine_row_group);
  EXPECT_EQ(nine_row_group - eight_row_group, one_row);
}

TEST(HubConnectionWidget, NoTextCanWidenTheGroup)
{
  // The panel streams itself to AR: a change of its width rebuilds the AR panel. The labels and
  // the list of the group never ask for room, whatever they say.
  HubConnectionWidget widget;
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle)));
  const QSize hint = settled_hint(widget);
  const QSize minimum = settled_minimum_hint(widget);

  ConnectionInput failed = input_of(ConnectionPhase::Failed);
  failed.failure_message = std::string(600, 'w');
  failed.program_name = std::string(300, 'p');
  widget.apply(make_connection_view(failed));
  HubTargetRow row;
  row.name = std::string(400, 'n');
  row.category = "headset";
  row.presence = "online";
  widget.set_targets({row, row, row, row, row, row});
  EXPECT_EQ(settled_hint(widget).width(), hint.width());
  EXPECT_EQ(settled_minimum_hint(widget).width(), minimum.width());
}
