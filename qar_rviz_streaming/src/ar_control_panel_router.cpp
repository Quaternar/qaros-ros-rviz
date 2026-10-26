// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_control_panel_router.hpp"

#include <utility>

#include "qar_rviz_streaming/ar_control_panel_model.hpp"

namespace qar_rviz_streaming
{
namespace
{

using qar_ui_model::UiValue;

/// A text that is safe to echo into an answer or a log line: printable ASCII, at most 40 bytes.
std::string printable(const std::string & text)
{
  constexpr std::size_t kMaxBytes = 40;
  std::string out;
  for (std::size_t i = 0; i < text.size() && i < kMaxBytes; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    out.push_back((c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?');
  }
  if (text.size() > kMaxBytes) {
    out += "...";
  }
  return out;
}

/// What a value is, in a few printable words.
std::string describe(const UiValue & value)
{
  switch (value.kind()) {
    case qar_ui_model::UiValueKind::Null:
      return "null";
    case qar_ui_model::UiValueKind::Bool:
      return value.as_bool() ? "true" : "false";
    case qar_ui_model::UiValueKind::Int:
      return std::to_string(value.as_int());
    case qar_ui_model::UiValueKind::Double:
      return "a number";
    case qar_ui_model::UiValueKind::String:
      return printable(value.as_string());
    case qar_ui_model::UiValueKind::Array:
      return "an array";
    case qar_ui_model::UiValueKind::Object:
      return "an object";
  }
  return "?";
}

ControlPanelRouting refuse(std::string error)
{
  ControlPanelRouting routing;
  routing.ok = false;
  routing.error = std::move(error);
  return routing;
}

/// Widgets of the page that show something and send nothing.
bool is_display_only(const std::string & id)
{
  return id == ar_control::kIdConnection || id == ar_control::kIdPanelsHeader ||
         id == ar_control::kIdMorePanels || id == ar_control::kIdToolsHeader ||
         id == ar_control::kIdMoreTools || id == ar_control::kIdPeers ||
         id == ar_control::kIdError;
}

}  // namespace

const char * to_string(ArPickMode mode)
{
  switch (mode) {
    case ArPickMode::Off:
      return "off";
    case ArPickMode::Point:
      return "point";
    case ArPickMode::NavGoal:
      return "nav_goal";
  }
  return "off";
}

bool ar_pick_mode_from_string(std::string_view text, ArPickMode & out)
{
  for (const ArPickMode mode : {ArPickMode::Off, ArPickMode::Point, ArPickMode::NavGoal}) {
    if (text == to_string(mode)) {
      out = mode;
      return true;
    }
  }
  return false;
}

const char * to_string(ControlPanelCommandKind kind)
{
  switch (kind) {
    case ControlPanelCommandKind::None:
      return "none";
    case ControlPanelCommandKind::SetPickMode:
      return "set_pick_mode";
    case ControlPanelCommandKind::ConfirmPick:
      return "confirm_pick";
    case ControlPanelCommandKind::CancelPick:
      return "cancel_pick";
    case ControlPanelCommandKind::SetPanelOn:
      return "set_panel_on";
    case ControlPanelCommandKind::SetToolOn:
      return "set_tool_on";
  }
  return "none";
}

bool control_command_is_deferred(ControlPanelCommandKind kind)
{
  return kind == ControlPanelCommandKind::SetPanelOn ||
         kind == ControlPanelCommandKind::SetToolOn;
}

ControlPanelRouting route_control_panel_event(const qar_ui_model::ControlBody & control)
{
  const std::string & id = control.control;
  if (id.empty()) {
    return refuse("control event names no widget");
  }

  // Which widget of the page is this, and what does that widget declare?
  ControlPanelCommand command;
  const char * action = "";
  std::size_t slot = 0;
  if (id == ar_control::kIdPickMode) {
    command.kind = ControlPanelCommandKind::SetPickMode;
    action = ar_control::kActionPickMode;
  } else if (id == ar_control::kIdPickConfirm) {
    command.kind = ControlPanelCommandKind::ConfirmPick;
    action = ar_control::kActionPickConfirm;
  } else if (id == ar_control::kIdPickCancel) {
    command.kind = ControlPanelCommandKind::CancelPick;
    action = ar_control::kActionPickCancel;
  } else if (ar_control::parse_slot_id(
      id, ar_control::kPanelIdPrefix, ar_control::kPanelSlots, slot))
  {
    command.kind = ControlPanelCommandKind::SetPanelOn;
    command.slot = slot;
    action = ar_control::kActionPanelToggle;
  } else if (ar_control::parse_slot_id(
      id, ar_control::kToolIdPrefix, ar_control::kToolSlots, slot))
  {
    command.kind = ControlPanelCommandKind::SetToolOn;
    command.slot = slot;
    action = ar_control::kActionToolToggle;
  } else if (is_display_only(id)) {
    return refuse("control '" + id + "' has no action");
  } else {
    return refuse("unknown control '" + printable(id) + "'");
  }

  // The widget's own action wins. A frame that names another one is refused: a page must not
  // be able to turn one widget into another by relabelling it.
  if (!control.action.empty() && control.action != action) {
    return refuse(
      "control '" + id + "' declares action '" + action + "' but the event named '" +
      printable(control.action) + "'");
  }

  switch (command.kind) {
    case ControlPanelCommandKind::SetPickMode:
      if (!control.value.is_string() ||
        !ar_pick_mode_from_string(control.value.as_string(), command.pick_mode))
      {
        return refuse("not a pick mode: '" + describe(control.value) + "'");
      }
      break;
    case ControlPanelCommandKind::SetPanelOn:
      if (!control.value.is_bool()) {
        return refuse("a panel switch needs on or off");
      }
      command.on = control.value.as_bool();
      break;
    case ControlPanelCommandKind::SetToolOn:
      if (!control.value.is_bool()) {
        return refuse("a tool switch needs on or off");
      }
      command.on = control.value.as_bool();
      break;
    case ControlPanelCommandKind::ConfirmPick:
    case ControlPanelCommandKind::CancelPick:
    case ControlPanelCommandKind::None:
      break;
  }

  ControlPanelRouting routing;
  routing.ok = true;
  routing.command = command;
  return routing;
}

}  // namespace qar_rviz_streaming
