// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the "QAROS Hub" group of the Connection tab (hub_connection_widget.hpp), offscreen.
// The group holds no session logic, so each test gives it the view of one state and checks what
// the person sees and what the group reports back. The offscreen platform plugin rasterizes no
// glyphs and its font is not the desktop's: no test asserts on a pixel or an absolute size, only
// on a size before and after. The plugin joins the Hub of this PC as a local app: the group has
// no Hub choice, no pairing and no Forget button.

#include "qar_rviz_streaming/hub_connection_widget.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSize>
#include <QString>

#include "qar_rviz_streaming/connection_view.hpp"

namespace
{

using qar_rviz_streaming::ConnectionInput;
using qar_rviz_streaming::ConnectionPhase;
using qar_rviz_streaming::HubConnectionWidget;
using qar_rviz_streaming::HubTargetRow;
using qar_rviz_streaming::make_connection_view;

ConnectionInput input_of(ConnectionPhase phase)
{
  ConnectionInput input;
  input.phase = phase;
  return input;
}

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

TEST(HubConnectionWidget, TheOfflinePreviewShowsTheGroupWithNothingToPress)
{
  HubConnectionWidget widget;
  widget.apply(make_connection_view(input_of(ConnectionPhase::Preview)));
  EXPECT_EQ(widget.title(), "QAROS Hub");
  EXPECT_EQ(
    widget.state_label()->text(),
    "QarOS: preview mode - set QAR_PANEL_LIVE_STREAM to connect");
  EXPECT_EQ(widget.identity_label()->text(), "preview (no QAROS)");
  EXPECT_FALSE(widget.cancel_button()->isEnabled());
  EXPECT_EQ(widget.target_list()->count(), 0);
}

TEST(HubConnectionWidget, TheGroupHasNoPairingWithAnotherPc)
{
  HubConnectionWidget widget;
  // No Hub choice, no host, port or code field, and only the Cancel button.
  EXPECT_TRUE(widget.findChildren<QComboBox *>().isEmpty());
  EXPECT_TRUE(widget.findChildren<QLineEdit *>().isEmpty());
  const QList<QPushButton *> buttons = widget.findChildren<QPushButton *>();
  ASSERT_EQ(buttons.size(), 1);
  EXPECT_EQ(buttons[0], widget.cancel_button());
  // The tool tips say how the Hub of another PC is reached.
  EXPECT_NE(widget.toolTip().indexOf("member"), -1);
  EXPECT_NE(widget.identity_label()->toolTip().indexOf("Revoke the approval"), -1);
}

TEST(HubConnectionWidget, CancelIsEnabledWhileTheJoinIsUnderWayAndReportsItself)
{
  HubConnectionWidget widget;
  int cancels = 0;
  QObject::connect(&widget, &HubConnectionWidget::cancelRequested, [&cancels]() { ++cancels; });
  for (const ConnectionPhase phase : {ConnectionPhase::Idle, ConnectionPhase::Running,
      ConnectionPhase::Reconnecting, ConnectionPhase::Stopping, ConnectionPhase::Failed})
  {
    widget.apply(make_connection_view(input_of(phase)));
    EXPECT_FALSE(widget.cancel_button()->isEnabled()) << static_cast<int>(phase);
  }
  widget.apply(make_connection_view(input_of(ConnectionPhase::Loading)));
  EXPECT_TRUE(widget.cancel_button()->isEnabled());
  widget.apply(make_connection_view(input_of(ConnectionPhase::WaitingForApproval)));
  ASSERT_TRUE(widget.cancel_button()->isEnabled());
  widget.cancel_button()->click();
  EXPECT_EQ(cancels, 1);
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
  failed.program_name = std::string(250, 'p');
  widget.apply(make_connection_view(failed));
  HubTargetRow row;
  row.name = std::string(400, 'n');
  row.category = "headset";
  row.presence = "online";
  widget.set_targets({row, row, row, row, row, row});
  EXPECT_EQ(settled_hint(widget).width(), hint.width());
  EXPECT_EQ(settled_minimum_hint(widget).width(), minimum.width());
}
