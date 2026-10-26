// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// A control event of the AR control page -> the command it means. The frame comes from a
// browser in a headset, so every refusal is tested as carefully as every success. Pure C++17:
// no session, no ROS graph, no Qt.

#include "qar_rviz_streaming/ar_control_panel_router.hpp"

#include <string>
#include <utility>

#include <gtest/gtest.h>

#include "qar_rviz_streaming/ar_control_panel_model.hpp"

namespace
{

using qar_rviz_streaming::ArPickMode;
using qar_rviz_streaming::ControlPanelCommandKind;
using qar_rviz_streaming::ControlPanelRouting;
using qar_rviz_streaming::route_control_panel_event;
using qar_ui_model::ControlBody;
using qar_ui_model::UiValue;
namespace ac = qar_rviz_streaming::ar_control;

ControlBody event(const std::string & widget, const std::string & action, UiValue value = {})
{
  ControlBody body;
  body.control = widget;
  body.action = action;
  body.value = std::move(value);
  return body;
}

}  // namespace

TEST(ArControlPanelRouter, PickModeValues)
{
  struct Case
  {
    const char * word;
    ArPickMode mode;
  };
  for (const Case & c : {Case{"off", ArPickMode::Off}, Case{"point", ArPickMode::Point},
      Case{"nav_goal", ArPickMode::NavGoal}})
  {
    const ControlPanelRouting routing = route_control_panel_event(
      event(ac::kIdPickMode, ac::kActionPickMode, UiValue(c.word)));
    ASSERT_TRUE(routing.ok) << c.word << ": " << routing.error;
    EXPECT_TRUE(routing.error.empty());
    EXPECT_EQ(routing.command.kind, ControlPanelCommandKind::SetPickMode);
    EXPECT_EQ(routing.command.pick_mode, c.mode) << c.word;
    EXPECT_STREQ(qar_rviz_streaming::to_string(c.mode), c.word);
    ArPickMode parsed = ArPickMode::Off;
    ASSERT_TRUE(qar_rviz_streaming::ar_pick_mode_from_string(c.word, parsed));
    EXPECT_EQ(parsed, c.mode);
  }
  // A frame that names no action is read as the widget's own.
  const ControlPanelRouting bare = route_control_panel_event(
    event(ac::kIdPickMode, "", UiValue("point")));
  ASSERT_TRUE(bare.ok) << bare.error;
  EXPECT_EQ(bare.command.pick_mode, ArPickMode::Point);
  // The ids and the action ids are the contract with the page.
  EXPECT_STREQ(ac::kIdPickMode, "cp_pick_mode");
  EXPECT_STREQ(ac::kActionPickMode, "cp.pick.mode");
}

TEST(ArControlPanelRouter, PickModeRefusesOtherModes)
{
  // "other" is what the page shows when the desktop armed a mode of its own; the page cannot
  // send it, and neither can it arm the modes that only the desktop offers.
  for (const char * word : {"other", "initial_pose", "area", "Point", "POINT", "nav goal", " point",
      "point ", "", "navgoal", "0"})
  {
    const ControlPanelRouting routing = route_control_panel_event(
      event(ac::kIdPickMode, ac::kActionPickMode, UiValue(word)));
    EXPECT_FALSE(routing.ok) << word;
    EXPECT_EQ(routing.error, std::string("not a pick mode: '") + word + "'");
    EXPECT_EQ(routing.command.kind, ControlPanelCommandKind::None);
    ArPickMode untouched = ArPickMode::NavGoal;
    EXPECT_FALSE(qar_rviz_streaming::ar_pick_mode_from_string(word, untouched)) << word;
    EXPECT_EQ(untouched, ArPickMode::NavGoal);
  }
  // A value that is no string at all.
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, ac::kActionPickMode, UiValue())).error,
    "not a pick mode: 'null'");
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, ac::kActionPickMode, UiValue(true))).error,
    "not a pick mode: 'true'");
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, ac::kActionPickMode, UiValue(1))).error,
    "not a pick mode: '1'");
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, ac::kActionPickMode, UiValue(1.5))).error,
    "not a pick mode: 'a number'");
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, ac::kActionPickMode, UiValue::array())).error,
    "not a pick mode: 'an array'");
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, ac::kActionPickMode, UiValue::object())).error,
    "not a pick mode: 'an object'");
  // What is echoed is printable and short.
  const std::string odd = std::string("a\nb\xff") + std::string(100, 'z');
  const ControlPanelRouting long_value = route_control_panel_event(
    event(ac::kIdPickMode, ac::kActionPickMode, UiValue(odd)));
  EXPECT_FALSE(long_value.ok);
  EXPECT_EQ(long_value.error.compare(0, 24, "not a pick mode: 'a?b?zz"), 0);
  EXPECT_EQ(long_value.error.find('\n'), std::string::npos);
  EXPECT_LT(long_value.error.size(), 80u);
}

TEST(ArControlPanelRouter, ConfirmAndCancel)
{
  const ControlPanelRouting confirm = route_control_panel_event(
    event(ac::kIdPickConfirm, ac::kActionPickConfirm));
  ASSERT_TRUE(confirm.ok) << confirm.error;
  EXPECT_EQ(confirm.command.kind, ControlPanelCommandKind::ConfirmPick);

  const ControlPanelRouting cancel = route_control_panel_event(
    event(ac::kIdPickCancel, ac::kActionPickCancel));
  ASSERT_TRUE(cancel.ok) << cancel.error;
  EXPECT_EQ(cancel.command.kind, ControlPanelCommandKind::CancelPick);

  // The button's payload is null; whatever else arrives with it is not used.
  const ControlPanelRouting with_value = route_control_panel_event(
    event(ac::kIdPickConfirm, ac::kActionPickConfirm, UiValue("yes")));
  ASSERT_TRUE(with_value.ok) << with_value.error;
  EXPECT_EQ(with_value.command.kind, ControlPanelCommandKind::ConfirmPick);

  // Each of the two means exactly its own action.
  const ControlPanelRouting crossed = route_control_panel_event(
    event(ac::kIdPickConfirm, ac::kActionPickCancel));
  EXPECT_FALSE(crossed.ok);
  EXPECT_EQ(
    crossed.error,
    "control 'cp_pick_confirm' declares action 'cp.pick.confirm' but the event named "
    "'cp.pick.cancel'");
}

TEST(ArControlPanelRouter, PanelToggleSlot)
{
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    for (const bool on : {true, false}) {
      const ControlPanelRouting routing = route_control_panel_event(
        event(ac::panel_toggle_id(slot), ac::kActionPanelToggle, UiValue(on)));
      ASSERT_TRUE(routing.ok) << slot << ": " << routing.error;
      EXPECT_EQ(routing.command.kind, ControlPanelCommandKind::SetPanelOn);
      EXPECT_EQ(routing.command.slot, slot);
      EXPECT_EQ(routing.command.on, on);
    }
  }
  EXPECT_EQ(ac::panel_toggle_id(0), "cp_panel_0");
  EXPECT_EQ(ac::panel_toggle_id(5), "cp_panel_5");
  EXPECT_EQ(ac::kPanelSlots, 6u);

  // A slot beyond the last, or an id that is not a plain slot number, is no widget of the page.
  for (const char * id : {"cp_panel_6", "cp_panel_9", "cp_panel_10", "cp_panel_", "cp_panel_00",
      "cp_panel_05", "cp_panel_-1", "cp_panel_+1", "cp_panel_1x", "cp_panel_ 1", "cp_panel_1.0",
      "cp_panel", "cp_panels_1", "CP_PANEL_1", "cp_panel_99999999999999"})
  {
    const ControlPanelRouting routing = route_control_panel_event(
      event(id, ac::kActionPanelToggle, UiValue(true)));
    EXPECT_FALSE(routing.ok) << id;
    EXPECT_EQ(routing.error, std::string("unknown control '") + id + "'") << id;
  }
}

TEST(ArControlPanelRouter, PanelToggleNeedsBool)
{
  for (const UiValue & value : {UiValue(), UiValue(1), UiValue(0), UiValue(1.0), UiValue("true"),
      UiValue("on"), UiValue(""), UiValue::array(), UiValue::object()})
  {
    const ControlPanelRouting routing = route_control_panel_event(
      event("cp_panel_2", ac::kActionPanelToggle, value));
    EXPECT_FALSE(routing.ok);
    EXPECT_EQ(routing.error, "a panel switch needs on or off");
    EXPECT_EQ(routing.command.kind, ControlPanelCommandKind::None);
  }
}

TEST(ArControlPanelRouter, ToolToggleSlot)
{
  // A tool row is a switch like a panel row: the slot is in the widget id, and the value says on
  // or off.
  for (std::size_t slot = 0; slot < ac::kToolSlots; ++slot) {
    for (const bool on : {true, false}) {
      const ControlPanelRouting routing = route_control_panel_event(
        event(ac::tool_toggle_id(slot), ac::kActionToolToggle, UiValue(on)));
      ASSERT_TRUE(routing.ok) << slot << ": " << routing.error;
      EXPECT_TRUE(routing.error.empty());
      EXPECT_EQ(routing.command.kind, ControlPanelCommandKind::SetToolOn);
      EXPECT_EQ(routing.command.slot, slot);
      EXPECT_EQ(routing.command.on, on);
    }
  }
  EXPECT_EQ(ac::tool_toggle_id(0), "cp_tool_0");
  EXPECT_EQ(ac::tool_toggle_id(3), "cp_tool_3");
  EXPECT_EQ(ac::kToolSlots, 4u);
  EXPECT_STREQ(ac::kActionToolToggle, "cp.tool.toggle");
  // A frame that names no action is read as the widget's own.
  const ControlPanelRouting bare = route_control_panel_event(event("cp_tool_1", "", UiValue(true)));
  ASSERT_TRUE(bare.ok) << bare.error;
  EXPECT_EQ(bare.command.kind, ControlPanelCommandKind::SetToolOn);
  EXPECT_EQ(bare.command.slot, 1u);
  EXPECT_TRUE(bare.command.on);

  // A slot beyond the last, or an id that is not a plain slot number, is no widget of the page.
  for (const char * id : {"cp_tool_4", "cp_tool_9", "cp_tool_10", "cp_tool_", "cp_tool_00",
      "cp_tool_01", "cp_tool_-1", "cp_tool_+1", "cp_tool_1x", "cp_tool_x", "cp_tool_ 1",
      "cp_tool_1.0", "cp_tool", "cp_tools_1", "CP_TOOL_1", "cp_tool_99999999999999"})
  {
    const ControlPanelRouting routing = route_control_panel_event(
      event(id, ac::kActionToolToggle, UiValue(true)));
    EXPECT_FALSE(routing.ok) << id;
    EXPECT_EQ(routing.error, std::string("unknown control '") + id + "'") << id;
  }
}

TEST(ArControlPanelRouter, ToolToggleNeedsBool)
{
  // The slot number is no value of the widget: it is in the id, and the value is on or off.
  for (const UiValue & value : {UiValue(), UiValue(1), UiValue(0), UiValue(2), UiValue(1.0),
      UiValue("true"), UiValue("on"), UiValue(""), UiValue::array(), UiValue::object()})
  {
    const ControlPanelRouting routing = route_control_panel_event(
      event("cp_tool_2", ac::kActionToolToggle, value));
    EXPECT_FALSE(routing.ok);
    EXPECT_EQ(routing.error, "a tool switch needs on or off");
    EXPECT_EQ(routing.command.kind, ControlPanelCommandKind::None);
  }
}

TEST(ArControlPanelRouter, UnknownControlIsRefused)
{
  EXPECT_EQ(route_control_panel_event(ControlBody{}).error, "control event names no widget");
  EXPECT_EQ(
    route_control_panel_event(event("cp_nonexistent", "cp.pick.mode")).error,
    "unknown control 'cp_nonexistent'");
  EXPECT_EQ(
    route_control_panel_event(event("topic._cmd_vel.draft.linear", "ros.set")).error,
    "unknown control 'topic._cmd_vel.draft.linear'");
  EXPECT_FALSE(route_control_panel_event(event("CP_PICK_MODE", ac::kActionPickMode)).ok);
  EXPECT_FALSE(route_control_panel_event(event("cp_pick_mode ", ac::kActionPickMode)).ok);
  EXPECT_FALSE(route_control_panel_event(event(ac::kPanelId, "")).ok) << "the root is no control";

  // The id that is echoed is printable and short.
  const ControlPanelRouting routing = route_control_panel_event(
    event(std::string("x\ny") + std::string(200, 'q'), ""));
  EXPECT_FALSE(routing.ok);
  EXPECT_EQ(routing.error.find('\n'), std::string::npos);
  EXPECT_LT(routing.error.size(), 80u);

  // A widget that only shows something has no action to run.
  for (const char * id : {"cp_conn", "cp_peers", "cp_error", "cp_h_panels", "cp_h_tools",
      "cp_more_panels", "cp_more_tools"})
  {
    const ControlPanelRouting shown = route_control_panel_event(event(id, "", UiValue(true)));
    EXPECT_FALSE(shown.ok) << id;
    EXPECT_EQ(shown.error, std::string("control '") + id + "' has no action");
  }
}

TEST(ArControlPanelRouter, ForeignActionIsRefused)
{
  // A page must not be able to turn one widget into another by naming another action: not the
  // action of another widget of the page, and not an action of the AR tools.
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, ac::kActionPanelToggle, UiValue("off"))).error,
    "control 'cp_pick_mode' declares action 'cp.pick.mode' but the event named "
    "'cp.panel.toggle'");
  EXPECT_EQ(
    route_control_panel_event(event("cp_panel_1", ac::kActionToolToggle, UiValue(true))).error,
    "control 'cp_panel_1' declares action 'cp.panel.toggle' but the event named "
    "'cp.tool.toggle'");
  EXPECT_EQ(
    route_control_panel_event(event("cp_tool_1", ac::kActionPanelToggle, UiValue(true))).error,
    "control 'cp_tool_1' declares action 'cp.tool.toggle' but the event named "
    "'cp.panel.toggle'");
  EXPECT_EQ(
    route_control_panel_event(event("cp_tool_0", ac::kActionPickCancel, UiValue(true))).error,
    "control 'cp_tool_0' declares action 'cp.tool.toggle' but the event named 'cp.pick.cancel'");
  // 'cp.tool.reopen' is no action of this page: a frame that names it is refused.
  EXPECT_EQ(
    route_control_panel_event(event("cp_tool_0", "cp.tool.reopen", UiValue(0))).error,
    "control 'cp_tool_0' declares action 'cp.tool.toggle' but the event named "
    "'cp.tool.reopen'");
  for (const char * foreign : {"ros.publish", "ros.set", "ros.call", "ros.send_goal",
      "ros.cancel_goal", "ros.open_video", "cp.pick.mode ", "CP.PICK.MODE"})
  {
    const ControlPanelRouting routing = route_control_panel_event(
      event(ac::kIdPickConfirm, foreign));
    EXPECT_FALSE(routing.ok) << foreign;
    EXPECT_EQ(
      routing.error,
      std::string("control 'cp_pick_confirm' declares action 'cp.pick.confirm' but the event "
      "named '") + foreign + "'");
  }
  // The action is checked before the value.
  EXPECT_EQ(
    route_control_panel_event(event(ac::kIdPickMode, "ros.publish", UiValue("nonsense"))).error,
    "control 'cp_pick_mode' declares action 'cp.pick.mode' but the event named 'ros.publish'");
}

TEST(ArControlPanelRouter, DeferredKinds)
{
  using qar_rviz_streaming::control_command_is_deferred;
  // The commands that may call QarOS run on the next render tick; the pick commands run at once.
  EXPECT_TRUE(control_command_is_deferred(ControlPanelCommandKind::SetPanelOn));
  EXPECT_TRUE(control_command_is_deferred(ControlPanelCommandKind::SetToolOn));
  EXPECT_FALSE(control_command_is_deferred(ControlPanelCommandKind::SetPickMode));
  EXPECT_FALSE(control_command_is_deferred(ControlPanelCommandKind::ConfirmPick));
  EXPECT_FALSE(control_command_is_deferred(ControlPanelCommandKind::CancelPick));
  EXPECT_FALSE(control_command_is_deferred(ControlPanelCommandKind::None));

  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelCommandKind::None), "none");
  EXPECT_STREQ(
    qar_rviz_streaming::to_string(ControlPanelCommandKind::SetPickMode), "set_pick_mode");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelCommandKind::ConfirmPick), "confirm_pick");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelCommandKind::CancelPick), "cancel_pick");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelCommandKind::SetPanelOn), "set_panel_on");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelCommandKind::SetToolOn), "set_tool_on");
}
