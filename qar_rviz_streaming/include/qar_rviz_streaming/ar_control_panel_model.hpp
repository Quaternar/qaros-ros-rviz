// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_MODEL_HPP_
#define QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_MODEL_HPP_

/// @file ar_control_panel_model.hpp
/// @brief The page of the AR control panel: its widget ids, actions, document paths and the
/// model that carries them. Pure; standard library and qar_ui_model only.
///
/// The page is one column, because the in-AR browser gives a fixed viewport of 500 x 500 CSS
/// pixels and a row asks for 260 pixels per child. From the top it holds
///
///  1. the pick block: the pick mode (Off / Point / Nav goal) with a caption that says what the
///     operator can do now, then Confirm and Cancel;
///  2. the state of the QarOS connection;
///  3. six switches for the panels of the desktop that can be shown in AR;
///  4. four switches for the AR tools: off hides the AR panel of a tool, on shows it again;
///  5. the number of peers and the last error.
///
/// A page cannot grow or shrink at run time: its model is fixed when the page is served and only
/// its data document changes. A list is therefore a fixed number of SLOTS. A slot is a widget
/// whose caption comes from the document (`label_binding`) and which is shown only while the
/// document says so (`visible_binding`). @ref ArControlPanelState decides what each slot holds.
///
/// The ids and the action ids below are the contract between this page and
/// @ref route_control_panel_event.

#include <cstddef>
#include <string>

#include "qar_ui_model/ui_model.hpp"
#include "qar_ui_model/ui_value.hpp"

namespace qar_rviz_streaming
{
namespace ar_control
{

constexpr const char * kPanelId = "qar_control";   ///< root id == PanelSession::panel_id()
constexpr const char * kPageTitle = "QAR control";
constexpr std::size_t kPanelSlots = 6;
constexpr std::size_t kToolSlots = 4;

// Action ids.
constexpr const char * kActionPickMode = "cp.pick.mode";
constexpr const char * kActionPickConfirm = "cp.pick.confirm";
constexpr const char * kActionPickCancel = "cp.pick.cancel";
constexpr const char * kActionPanelToggle = "cp.panel.toggle";
constexpr const char * kActionToolToggle = "cp.tool.toggle";

// Widget ids.
constexpr const char * kIdPickMode = "cp_pick_mode";
constexpr const char * kIdPickConfirm = "cp_pick_confirm";
constexpr const char * kIdPickCancel = "cp_pick_cancel";
constexpr const char * kIdConnection = "cp_conn";
constexpr const char * kIdPanelsHeader = "cp_h_panels";
constexpr const char * kIdMorePanels = "cp_more_panels";
constexpr const char * kIdToolsHeader = "cp_h_tools";
constexpr const char * kIdMoreTools = "cp_more_tools";
constexpr const char * kIdPeers = "cp_peers";
constexpr const char * kIdError = "cp_error";
constexpr const char * kPanelIdPrefix = "cp_panel_";  ///< + slot: the switch of a panel
constexpr const char * kToolIdPrefix = "cp_tool_";    ///< + slot: the switch of an AR tool

// Paths into the data document.
constexpr const char * kPathConnection = "status.connection";
constexpr const char * kPathPeers = "status.peers";
constexpr const char * kPathError = "status.error";
constexpr const char * kPathPickMode = "pick.mode";
constexpr const char * kPathPickCaption = "pick.caption";
constexpr const char * kPathMorePanels = "more.panels";
constexpr const char * kPathMorePanelsShow = "more.panels_show";
constexpr const char * kPathMoreTools = "more.tools";
constexpr const char * kPathMoreToolsShow = "more.tools_show";

/// The caption of the pick block while no mode is armed.
constexpr const char * kCaptionPickOff = "Choose Point or Nav goal, then aim in the scene";

/// The line that stands in the place of the panel and tool switches while there is no live QarOS
/// session (the connection is offline or a preview): the switches act on QarOS and have nothing
/// to act on. It is shown through the "more panels" line.
constexpr const char * kLineNoLiveSession =
  "AR panels and tools are switched in a live QarOS session";

/// The key of RViz's standard "Displays" dock in the desktop's list of panels ("dock:" and the
/// title of the dock). The page has no switch for it: the AR copy of the Displays panel is an
/// entry of the list of its own (see @ref ArControlPanelState).
constexpr const char * kStandardDisplaysKey = "dock:Displays";

/// "cp_panel_<slot>"
inline std::string panel_toggle_id(std::size_t slot)
{
  return std::string(kPanelIdPrefix) + std::to_string(slot);
}

/// "cp_tool_<slot>"
inline std::string tool_toggle_id(std::size_t slot)
{
  return std::string(kToolIdPrefix) + std::to_string(slot);
}

/// Reads the slot out of a widget id: @p prefix followed by the decimal number of a slot below
/// @p limit, written without a sign, a leading zero or anything after it.
/// @return false (and @p out_slot unchanged) for any other id.
inline bool parse_slot_id(
  const std::string & id, const char * prefix, std::size_t limit, std::size_t & out_slot)
{
  const std::string head(prefix);
  if (id.size() <= head.size() || id.compare(0, head.size(), head) != 0) {
    return false;
  }
  const std::string digits = id.substr(head.size());
  if (digits.size() > 6 || (digits.size() > 1 && digits[0] == '0')) {
    return false;
  }
  std::size_t slot = 0;
  for (const char c : digits) {
    if (c < '0' || c > '9') {
      return false;
    }
    slot = slot * 10 + static_cast<std::size_t>(c - '0');
  }
  if (slot >= limit) {
    return false;
  }
  out_slot = slot;
  return true;
}

/// "panels.s<slot>.<field>"; the fields are show, title and on.
inline std::string panel_path(std::size_t slot, const char * field)
{
  return "panels.s" + std::to_string(slot) + "." + field;
}

/// "tools.s<slot>.<field>"; the fields are show, title and on.
inline std::string tool_path(std::size_t slot, const char * field)
{
  return "tools.s" + std::to_string(slot) + "." + field;
}

}  // namespace ar_control

/// The page; it validates without a schema. Its initial data is
/// @ref make_ar_control_initial_document, so a page that is served before the first state
/// update is not blank.
qar_ui_model::UiModel make_ar_control_page_model();

/// The document of a session that has not heard from the panel yet: the connection is
/// "offline", the pick mode is "off" with its caption, every slot is hidden and holds no title,
/// and the "more panels" line shows @ref ar_control::kLineNoLiveSession. It is what
/// @ref ArControlPanelState publishes first for a snapshot of an offline desktop.
qar_ui_model::UiValue make_ar_control_initial_document();

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_MODEL_HPP_
