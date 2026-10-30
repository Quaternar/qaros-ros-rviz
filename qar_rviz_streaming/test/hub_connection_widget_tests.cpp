// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the "QAROS Hub" group of the Connection tab (hub_connection_widget.hpp), offscreen.
// The group holds no session logic, so each test gives it the view of one state and checks what
// the person sees and what the group reports back. The offscreen platform plugin rasterizes no
// glyphs and its font is not the desktop's: no test asserts on a pixel or an absolute size, only
// on a size before and after.

#include "qar_rviz_streaming/hub_connection_widget.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QSize>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "qar_rviz_streaming/connection_view.hpp"

namespace
{

using qar_rviz_streaming::ConnectionInput;
using qar_rviz_streaming::ConnectionPhase;
using qar_rviz_streaming::ConnectionView;
using qar_rviz_streaming::HubChoice;
using qar_rviz_streaming::HubConnectionWidget;
using qar_rviz_streaming::HubTargetRow;
using qar_rviz_streaming::make_connection_view;

ConnectionInput input_of(ConnectionPhase phase, HubChoice mode)
{
  ConnectionInput input;
  input.phase = phase;
  input.mode_setting = mode;
  return input;
}

/// Counts what the group reports.
class Reports
{
public:
  explicit Reports(HubConnectionWidget & widget)
  {
    QObject::connect(&widget, &HubConnectionWidget::modeEdited, [this](const QString & word) {
        modes.push_back(word);
      });
    QObject::connect(&widget, &HubConnectionWidget::hostEdited, [this](const QString & text) {
        hosts.push_back(text);
      });
    QObject::connect(&widget, &HubConnectionWidget::portEdited, [this](const QString & text) {
        ports.push_back(text);
      });
    QObject::connect(
      &widget, &HubConnectionWidget::pairRequested,
      [this](const QString & code, const QString & host, int port) {
        pairs.push_back(code + "|" + host + "|" + QString::number(port));
      });
    QObject::connect(&widget, &HubConnectionWidget::pairRefused, [this](const QString & reason) {
        refusals.push_back(reason);
      });
    QObject::connect(&widget, &HubConnectionWidget::cancelRequested, [this]() { ++cancels; });
    QObject::connect(&widget, &HubConnectionWidget::forgetConfirmed, [this]() { ++forgets; });
  }

  std::vector<QString> modes, hosts, ports, pairs, refusals;
  int cancels = 0;
  int forgets = 0;
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

/// The group in the state of a remote Hub that waits for a code.
void show_needs_code(HubConnectionWidget & widget)
{
  ConnectionInput input = input_of(ConnectionPhase::NeedsCode, HubChoice::Remote);
  input.has_remote_identity = false;
  widget.apply(make_connection_view(input));
}

}  // namespace

TEST(HubConnectionWidget, TheOfflinePreviewShowsTheGroupWithTheSettingsEditableAndNoPairing)
{
  HubConnectionWidget widget;
  widget.apply(make_connection_view(input_of(ConnectionPhase::Preview, HubChoice::Remote)));
  EXPECT_EQ(widget.title(), "QAROS Hub");
  EXPECT_EQ(
    widget.state_label()->text(),
    "QarOS: preview mode - set QAR_PANEL_LIVE_STREAM to connect");
  EXPECT_EQ(widget.identity_label()->text(), "preview (no QAROS)");
  // The mode, the host and the port are settings: editable.
  EXPECT_TRUE(widget.mode_combo()->isEnabled());
  EXPECT_TRUE(widget.host_edit()->isEnabled());
  EXPECT_TRUE(widget.port_edit()->isEnabled());
  EXPECT_TRUE(widget.remote_fields()->isVisibleTo(&widget));
  // There is nothing to pair with.
  EXPECT_FALSE(widget.code_edit()->isEnabled());
  EXPECT_FALSE(widget.pair_button()->isEnabled());
  EXPECT_FALSE(widget.cancel_button()->isEnabled());
  EXPECT_FALSE(widget.forget_button()->isEnabled());
  EXPECT_EQ(widget.target_list()->count(), 0);
}

TEST(HubConnectionWidget, HostAndPortShowOnlyForAnotherPcsHub)
{
  HubConnectionWidget widget;
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle, HubChoice::Local)));
  EXPECT_FALSE(widget.remote_fields()->isVisibleTo(&widget));
  EXPECT_FALSE(widget.host_edit()->isVisibleTo(&widget));
  EXPECT_FALSE(widget.port_edit()->isVisibleTo(&widget));
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle, HubChoice::Auto)));
  EXPECT_FALSE(widget.remote_fields()->isVisibleTo(&widget));
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle, HubChoice::Remote)));
  EXPECT_TRUE(widget.host_edit()->isVisibleTo(&widget));
  EXPECT_TRUE(widget.port_edit()->isVisibleTo(&widget));
  widget.apply(make_connection_view(input_of(ConnectionPhase::Running, HubChoice::Local)));
  EXPECT_FALSE(widget.host_edit()->isVisibleTo(&widget));
}

TEST(HubConnectionWidget, TheCodeFieldAndPairAreEnabledOnlyInTheStateThatNeedsACode)
{
  HubConnectionWidget widget;
  for (const ConnectionPhase phase : {ConnectionPhase::Idle, ConnectionPhase::Loading,
      ConnectionPhase::WaitingForApproval, ConnectionPhase::Rejoining, ConnectionPhase::Pairing,
      ConnectionPhase::Running, ConnectionPhase::Reconnecting, ConnectionPhase::Stopping,
      ConnectionPhase::Failed})
  {
    widget.apply(make_connection_view(input_of(phase, HubChoice::Remote)));
    EXPECT_FALSE(widget.code_edit()->isEnabled()) << static_cast<int>(phase);
    EXPECT_FALSE(widget.pair_button()->isEnabled()) << static_cast<int>(phase);
  }
  show_needs_code(widget);
  EXPECT_TRUE(widget.code_edit()->isEnabled());
  EXPECT_TRUE(widget.pair_button()->isEnabled());
  EXPECT_TRUE(widget.cancel_button()->isEnabled()) << "the wait for a code can be left";
  widget.apply(make_connection_view(input_of(ConnectionPhase::Pairing, HubChoice::Remote)));
  EXPECT_TRUE(widget.cancel_button()->isEnabled());
}

TEST(HubConnectionWidget, EnterSubmitsTheCodeAtOnceAndEmptiesTheField)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  show_needs_code(widget);
  widget.set_host_text("hub.example.org");
  widget.set_port_text("19120");
  widget.code_edit()->setText("  482913 ");
  QMetaObject::invokeMethod(widget.code_edit(), "returnPressed");
  ASSERT_EQ(reports.pairs.size(), 1u);
  EXPECT_EQ(reports.pairs[0], "482913|hub.example.org|19120");
  EXPECT_TRUE(widget.code_edit()->text().isEmpty()) << "the code is not kept";
  EXPECT_TRUE(reports.refusals.empty());

  // The Pair button does the same.
  widget.code_edit()->setText("771205");
  widget.pair_button()->click();
  ASSERT_EQ(reports.pairs.size(), 2u);
  EXPECT_EQ(reports.pairs[1], "771205|hub.example.org|19120");
  EXPECT_TRUE(widget.code_edit()->text().isEmpty());
}

TEST(HubConnectionWidget, ACodeWithoutAUsableHubIsRefusedAndStaysInItsField)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  show_needs_code(widget);

  widget.code_edit()->setText("482913");
  widget.set_host_text("");
  widget.set_port_text("19120");
  widget.pair_button()->click();
  ASSERT_EQ(reports.refusals.size(), 1u);
  EXPECT_EQ(reports.refusals[0], "Type the host name or address of the Hub.");

  widget.set_host_text("hub.example.org");
  widget.set_port_text("");
  widget.pair_button()->click();
  ASSERT_EQ(reports.refusals.size(), 2u);
  EXPECT_NE(reports.refusals[1].indexOf("port"), -1);
  widget.set_port_text("70000");
  widget.pair_button()->click();
  ASSERT_EQ(reports.refusals.size(), 3u);

  widget.set_port_text("19120");
  widget.code_edit()->setText("   ");
  widget.pair_button()->click();
  ASSERT_EQ(reports.refusals.size(), 4u);
  EXPECT_EQ(reports.refusals[3], "Type the pairing code that the Hub shows.");

  EXPECT_TRUE(reports.pairs.empty());
  widget.code_edit()->setText("482913");
  widget.pair_button()->click();
  EXPECT_EQ(reports.pairs.size(), 1u);
}

TEST(HubConnectionWidget, TheCodeIsHeldInItsFieldOnly)
{
  HubConnectionWidget widget;
  show_needs_code(widget);
  widget.set_host_text("hub.example.org");
  widget.set_port_text("19120");
  widget.code_edit()->setText("482913");
  // No other control and no label shows the code, and clearing it leaves nothing to undo.
  for (QLabel * label : {widget.state_label(), widget.identity_label()}) {
    EXPECT_EQ(label->text().indexOf("482913"), -1);
  }
  EXPECT_EQ(widget.host_edit()->text().indexOf("482913"), -1);
  widget.clear_code();
  EXPECT_TRUE(widget.code_edit()->text().isEmpty());
  widget.code_edit()->undo();
  EXPECT_TRUE(widget.code_edit()->text().isEmpty()) << "clearing leaves no undo history";
}

TEST(HubConnectionWidget, ForgetNeedsASecondClickWithinTheConfirmationTime)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  ConnectionInput paired = input_of(ConnectionPhase::Idle, HubChoice::Remote);
  paired.has_remote_identity = true;
  paired.identity_host = "hub.example.org";
  paired.identity_port = 19120;
  widget.apply(make_connection_view(paired));
  ASSERT_TRUE(widget.forget_button()->isEnabled());
  EXPECT_EQ(widget.forget_button()->text(), "Forget pairing");

  // The first click only asks.
  widget.forget_button()->click();
  EXPECT_EQ(reports.forgets, 0);
  EXPECT_TRUE(widget.forget_confirmation_pending());
  EXPECT_EQ(widget.forget_button()->text(), "Click again to forget");

  // The state is refreshed while the question stands (the countdown does it): it stays.
  widget.apply(make_connection_view(paired));
  EXPECT_TRUE(widget.forget_confirmation_pending());
  EXPECT_EQ(widget.forget_button()->text(), "Click again to forget");

  // The second click confirms.
  widget.forget_button()->click();
  EXPECT_EQ(reports.forgets, 1);
  EXPECT_FALSE(widget.forget_confirmation_pending());
  EXPECT_EQ(widget.forget_button()->text(), "Forget pairing");

  // A question that is not answered lapses; the next click asks again instead of confirming.
  widget.forget_button()->click();
  EXPECT_TRUE(widget.forget_confirmation_pending());
  widget.reset_forget_confirmation();
  EXPECT_FALSE(widget.forget_confirmation_pending());
  EXPECT_EQ(widget.forget_button()->text(), "Forget pairing");
  widget.forget_button()->click();
  EXPECT_EQ(reports.forgets, 1);
  EXPECT_TRUE(widget.forget_confirmation_pending());

  // A state that cannot forget takes the question back.
  widget.apply(make_connection_view(input_of(ConnectionPhase::Pairing, HubChoice::Remote)));
  EXPECT_FALSE(widget.forget_confirmation_pending());
  EXPECT_FALSE(widget.forget_button()->isEnabled());
  EXPECT_EQ(reports.forgets, 1);
}

TEST(HubConnectionWidget, TheConfirmationLapsesOnItsOwnTimer)
{
  HubConnectionWidget widget;
  ConnectionInput paired = input_of(ConnectionPhase::Idle, HubChoice::Remote);
  paired.has_remote_identity = true;
  widget.apply(make_connection_view(paired));
  widget.set_forget_confirmation_ms(20);
  widget.forget_button()->click();
  ASSERT_TRUE(widget.forget_confirmation_pending());
  // The timer is a child of the group: it ends with the group and holds no slot of this library
  // after it.
  EXPECT_FALSE(widget.findChildren<QTimer *>().isEmpty());
  QElapsedTimer clock;
  clock.start();
  while (widget.forget_confirmation_pending() && clock.elapsed() < 2000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  EXPECT_FALSE(widget.forget_confirmation_pending());
  EXPECT_EQ(widget.forget_button()->text(), "Forget pairing");
}

TEST(HubConnectionWidget, ThisPcModeDoesNotOfferForget)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  widget.apply(make_connection_view(input_of(ConnectionPhase::Running, HubChoice::Local)));
  EXPECT_FALSE(widget.forget_button()->isEnabled());
  EXPECT_EQ(widget.forget_button()->text(), "Approval: revoke in the Visualizer");
  EXPECT_NE(
    widget.forget_button()->toolTip().indexOf("Revoke the approval in the Visualizer"), -1);
  widget.forget_button()->click();
  EXPECT_FALSE(widget.forget_confirmation_pending());
  EXPECT_EQ(reports.forgets, 0);
}

TEST(HubConnectionWidget, CancelReportsItself)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  widget.apply(
    make_connection_view(input_of(ConnectionPhase::WaitingForApproval, HubChoice::Local)));
  ASSERT_TRUE(widget.cancel_button()->isEnabled());
  widget.cancel_button()->click();
  EXPECT_EQ(reports.cancels, 1);
}

TEST(HubConnectionWidget, OnlyAPersonsEditReportsAndASettingOfTheHostDoesNot)
{
  HubConnectionWidget widget;
  Reports reports(widget);
  widget.set_mode_word("remote");
  widget.set_host_text("hub.example.org");
  widget.set_port_text("19120");
  EXPECT_TRUE(reports.modes.empty());
  EXPECT_TRUE(reports.hosts.empty());
  EXPECT_TRUE(reports.ports.empty());
  EXPECT_EQ(widget.mode_word(), "remote");
  EXPECT_EQ(widget.host_text(), "hub.example.org");
  EXPECT_EQ(widget.port_text(), "19120");

  widget.mode_combo()->setCurrentIndex(widget.mode_combo()->findData("local"));
  ASSERT_EQ(reports.modes.size(), 1u);
  EXPECT_EQ(reports.modes[0], "local");

  widget.host_edit()->setText("other.example.org");
  QMetaObject::invokeMethod(widget.host_edit(), "editingFinished");
  ASSERT_EQ(reports.hosts.size(), 1u);
  EXPECT_EQ(reports.hosts[0], "other.example.org");
  widget.port_edit()->setText("19121");
  QMetaObject::invokeMethod(widget.port_edit(), "editingFinished");
  ASSERT_EQ(reports.ports.size(), 1u);
  EXPECT_EQ(reports.ports[0], "19121");
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
  widget.apply(make_connection_view(input_of(ConnectionPhase::Idle, HubChoice::Remote)));
  const QSize hint = settled_hint(widget);
  const QSize minimum = settled_minimum_hint(widget);

  ConnectionInput failed = input_of(ConnectionPhase::Failed, HubChoice::Remote);
  failed.failure_message = std::string(600, 'w');
  failed.has_remote_identity = true;
  failed.identity_host = std::string(250, 'h');
  failed.identity_port = 19120;
  failed.pairing_id = "1a2b3c4d";
  widget.apply(make_connection_view(failed));
  HubTargetRow row;
  row.name = std::string(400, 'n');
  row.category = "headset";
  row.presence = "online";
  widget.set_targets({row, row, row, row, row, row});
  EXPECT_EQ(settled_hint(widget).width(), hint.width());
  EXPECT_EQ(settled_minimum_hint(widget).width(), minimum.width());
}

TEST(HubConnectionWidget, ShowingAndHidingTheRemoteFieldsMovesNothing)
{
  HubConnectionWidget widget;
  // The preview: the texts of the group are the same for every mode, so only the row can move
  // the size.
  widget.apply(make_connection_view(input_of(ConnectionPhase::Preview, HubChoice::Remote)));
  ASSERT_TRUE(widget.remote_fields()->isVisibleTo(&widget));
  const QSize shown = settled_hint(widget);
  widget.apply(make_connection_view(input_of(ConnectionPhase::Preview, HubChoice::Local)));
  ASSERT_FALSE(widget.remote_fields()->isVisibleTo(&widget));
  EXPECT_EQ(settled_hint(widget), shown) << "the hidden row keeps its room";
  widget.apply(make_connection_view(input_of(ConnectionPhase::Preview, HubChoice::Auto)));
  EXPECT_EQ(settled_hint(widget), shown);
}

TEST(HubConnectionWidget, TheModeComboOffersTheThreeHubs)
{
  HubConnectionWidget widget;
  ASSERT_EQ(widget.mode_combo()->count(), 3);
  EXPECT_EQ(widget.mode_combo()->itemData(0).toString(), "auto");
  EXPECT_EQ(widget.mode_combo()->itemData(1).toString(), "local");
  EXPECT_EQ(widget.mode_combo()->itemData(2).toString(), "remote");
  EXPECT_EQ(widget.mode_combo()->itemText(0), "Automatic");
  EXPECT_EQ(widget.mode_combo()->itemText(1), "This PC (approve once in the QAROS Hub)");
  EXPECT_EQ(widget.mode_combo()->itemText(2), "Another PC (pairing code)");
  widget.set_mode_word("nonsense");
  EXPECT_EQ(widget.mode_word(), "auto") << "an unknown word leaves the choice alone";
}
