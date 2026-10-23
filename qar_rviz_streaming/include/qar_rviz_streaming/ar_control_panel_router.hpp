// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_ROUTER_HPP_
#define QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_ROUTER_HPP_

/// @file ar_control_panel_router.hpp
/// @brief One control event of the AR control page -> the command it means, or the reason it is
/// refused. Pure: no GUI toolkit, no ROS client library, no session.
///
/// A control frame comes from a browser in a headset, so nothing in it is trusted. The router
/// checks what it can check without the panel's state: the widget id must be one of the page's
/// widgets that has an action, the action the frame names must be the one the widget declares
/// (a frame cannot turn one widget into another), and the value must have the type that the
/// widget sends. Whether a slot holds something and whether a QarOS session exists are checks of
/// the host, which knows the state.
///
/// The router is the counterpart of the page model in ar_control_panel_model.hpp: every widget
/// of the page that has an action is accepted here, and nothing else is.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "qar_ui_model/ui_protocol.hpp"

namespace qar_rviz_streaming
{

/// The pick modes that the page offers. A mode that only the desktop can arm is not among them.
enum class ArPickMode : std::uint8_t { Off = 0, Point, NavGoal };
const char * to_string(ArPickMode mode);  ///< "off", "point", "nav_goal"
/// @return false (and @p out unchanged) unless @p text is exactly one of the three words.
bool ar_pick_mode_from_string(std::string_view text, ArPickMode & out);

enum class ControlPanelCommandKind : std::uint8_t
{
  None = 0,
  SetPickMode,  ///< arm Off, Point or Nav goal
  ConfirmPick,  ///< publish the pending pick
  CancelPick,   ///< drop the pending pick
  SetPanelOn,   ///< show or hide one panel of the desktop in AR
  SetToolOn     ///< show or hide the AR panel of one AR tool
};
/// "none", "set_pick_mode", "confirm_pick", "cancel_pick", "set_panel_on", "set_tool_on"
const char * to_string(ControlPanelCommandKind kind);

struct ControlPanelCommand
{
  ControlPanelCommandKind kind = ControlPanelCommandKind::None;
  ArPickMode pick_mode = ArPickMode::Off;  ///< SetPickMode
  std::size_t slot = 0;                    ///< SetPanelOn, SetToolOn
  bool on = false;                         ///< SetPanelOn, SetToolOn
};

struct ControlPanelRouting
{
  bool ok = false;
  std::string error;            ///< verbatim into PanelSession::ack(event, false, error)
  ControlPanelCommand command;  ///< valid when ok
};

/// @brief Checks widget id, declared action and value type of one control event.
///
/// Refusals, in the order they are checked:
///  * "control event names no widget";
///  * "unknown control '<id>'" for an id that is no widget of the page, and "control '<id>' has no
///    action" for a widget that only shows something;
///  * "control '<id>' declares action '<a>' but the event named '<b>'" when the frame names
///    another action (a frame that names none is read as the declared one);
///  * "not a pick mode: '<v>'", "a panel switch needs on or off" and "a tool switch needs on or
///    off" for a wrong value.
/// A value that is echoed into a refusal is cut and made printable.
ControlPanelRouting route_control_panel_event(const qar_ui_model::ControlBody & control);

/// SetPanelOn and SetToolOn may call QarOS and run on the next render tick; the others run at
/// once.
bool control_command_is_deferred(ControlPanelCommandKind kind);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_ROUTER_HPP_
