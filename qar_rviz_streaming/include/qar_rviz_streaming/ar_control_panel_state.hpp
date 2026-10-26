// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_STATE_HPP_
#define QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_STATE_HPP_

/// @file ar_control_panel_state.hpp
/// @brief What the AR control page shows: a snapshot of the desktop state -> the data document
/// of the page -> only the paths that changed. Pure: no GUI toolkit, no ROS client library, no
/// session.
///
/// ## Slots
///
/// The page has six panel slots and four tool slots (ar_control_panel_model.hpp). A slot is
/// stable: a panel that is listed keeps its slot for as long as it is listed, and a panel that
/// is new takes the lowest free slot. A slot whose panel has just left stays empty for one
/// update, so a tap that the page sent before it learned of the change cannot reach the panel
/// that takes the slot next. Entries that find no free slot are counted in the "more" lines of
/// the page, so a list longer than the page never makes a slot jump from one entry to another
/// under the operator's finger.
///
/// ## Not on the page
///
/// The standard Displays dock of RViz (@ref ar_control::kStandardDisplaysKey) is left out of the
/// panels: the page shows no switch for it, it takes no slot and the "more panels" line does not
/// count it. Its AR copy, the hidden Displays panel, is listed under a key of its own. The
/// desktop's own list of panels is not changed by this.
///
/// ## Without a QarOS session
///
/// The switches act on QarOS. While the connection is @ref ConnectionState::Offline or
/// @ref ConnectionState::Preview there is nothing for them to act on, so the state publishes no
/// panel and no tool: every slot is free and hidden, and the "more panels" line shows
/// @ref ar_control::kLineNoLiveSession instead of an overflow count. The "more tools" line stays
/// hidden. When the connection is live again the entries take their slots again, in the order of
/// the lists. The connections Connecting, Live and Paused list the entries.
///
/// ## The document
///
/// | Path | Value |
/// |---|---|
/// | `status.connection` | the words of @ref ConnectionState |
/// | `status.peers` | the number of render streams |
/// | `status.error` | "none", or the last error cut to 200 bytes |
/// | `pick.mode` | "off", "point", "nav_goal" or "other" |
/// | `pick.caption` | see @ref pick_caption |
/// | `panels.s<n>.show` / `.title` / `.on` | a slot: shown, caption (null while free), switch |
/// | `tools.s<n>.show` / `.title` / `.on` | a slot: shown, caption (null while free), switch |
/// | `more.panels`, `more.panels_show`, `more.tools`, `more.tools_show` | the overflow lines |

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "qar_ui_model/ui_protocol.hpp"
#include "qar_ui_model/ui_value.hpp"

#include "qar_rviz_streaming/ar_control_panel_model.hpp"
#include "qar_rviz_streaming/ar_control_panel_router.hpp"

namespace qar_rviz_streaming
{

enum class ConnectionState : std::uint8_t { Offline = 0, Connecting, Live, Paused, Preview };
/// "offline", "connecting", "live", "paused", "preview"
const char * to_string(ConnectionState state);

/// Where a panel of the desktop stands as an AR panel.
enum class PanelArState : std::uint8_t
{
  Off = 0,     ///< not streamed, or only a closed-window notice
  Starting,    ///< streamed, no frame sent yet
  Streaming,   ///< streamed and sending frames
  Hidden,      ///< streamed, but hidden in AR
  ClosedInAr   ///< the operator closed its AR panel
};
/// "off", "starting", "streaming", "hidden", "closed_in_ar"
const char * to_string(PanelArState state);
/// True for Starting and Streaming: the states in which the panel's switch reads "on".
bool panel_state_is_on(PanelArState state);

struct ControlPanelPanelEntry
{
  std::string key;   ///< the stable key of the panel (the key of the desktop's list)
  std::string name;  ///< what the operator knows it as
  PanelArState state{};
};

struct ControlPanelToolEntry
{
  std::string panel_id;  ///< the stable id of the tool's panel
  std::string name;
  bool in_ar = false;    ///< its AR panel exists (shown or hidden)
  bool on = false;       ///< its AR panel is shown in AR: the state of the tool's switch
};

/// Everything the page shows, gathered by the host.
struct ControlPanelSnapshot
{
  ConnectionState connection = ConnectionState::Offline;
  std::uint32_t peers = 0;
  std::string last_error;              ///< empty = none
  ArPickMode pick_mode = ArPickMode::Off;
  bool pick_mode_other = false;        ///< the desktop armed a mode the page does not offer
  std::string pick_mode_other_name;    ///< e.g. "initial pose"
  bool pick_pending = false;
  bool pick_auto_confirm = false;
  std::string pick_summary;            ///< the summary line of the pick status
  std::string pick_error;              ///< the last refusal of the pick status
  std::vector<ControlPanelPanelEntry> panels;  ///< the desktop's list, in its order
  std::vector<ControlPanelToolEntry> tools;    ///< the AR tools, in their order
};

/// @brief The caption of the pick block:
///
///  * a pick waits for Confirm: "Waiting: <summary> - Confirm or Cancel";
///  * the last pick was refused: "Refused: <error>";
///  * the desktop armed a mode of its own: "Armed on the desktop: <mode>";
///  * Off: "Choose Point or Nav goal, then aim in the scene";
///  * Point: "Point: aim and tap in the scene";
///  * Nav goal: "Nav goal: press, drag to set the heading, release".
///
/// While a mode is armed and the desktop publishes without confirming, " - publishes on release
/// (desktop setting)" is added. Texts that come from the desktop are cut to 200 bytes.
std::string pick_caption(const ControlPanelSnapshot & snapshot);

/// The caption of a panel slot: the name, with " (starting)" or " (closed in AR)" added.
std::string panel_slot_title(const ControlPanelPanelEntry & entry);
/// The caption of a tool slot: the name, with " (pending)" added while the tool has no AR panel
/// yet.
std::string tool_slot_title(const ControlPanelToolEntry & entry);

/// @brief The slots and the document of one page. Use a new object (or assign a default one)
/// when the page's session is created again, so that the next update publishes every path.
///
/// ## A page changes its own copy first
///
/// When the operator taps a switch or a choice, the page writes the new value into its own copy
/// of the document at once and then sends the control event. If the host refuses the event, or
/// the panel does not reach the state that was asked for, the snapshot still says what it said
/// before, so @ref update has nothing to publish and the page would go on showing a value that
/// is not true. After such a command the host sends @ref resync_patch.
class ArControlPanelState
{
public:
  /// @brief Takes the snapshot into the slots and the document and returns the paths whose
  /// value changed since the last call; the first call returns every path. The host sends the
  /// result as one data frame.
  std::vector<qar_ui_model::PatchEntry> update(const ControlPanelSnapshot & snapshot);

  /// @brief One entry with the empty path, which replaces the page's whole document with the
  /// current one; empty before the first update.
  std::vector<qar_ui_model::PatchEntry> resync_patch() const;

  /// The key of the panel in @p slot; empty = free (also for a slot beyond the last).
  const std::string & panel_key(std::size_t slot) const;
  /// The panel id of the tool in @p slot; empty = free.
  const std::string & tool_panel_id(std::size_t slot) const;
  /// The plain name of the panel in @p slot, for the texts of answers; empty = free.
  const std::string & panel_title(std::size_t slot) const;
  /// The plain name of the tool in @p slot, for the texts of answers; empty = free.
  const std::string & tool_title(std::size_t slot) const;

  /// The document as of the last update; null before the first.
  const qar_ui_model::UiValue & document() const { return document_; }

private:
  std::array<std::string, ar_control::kPanelSlots> panel_keys_{};
  std::array<std::string, ar_control::kToolSlots> tool_ids_{};
  std::array<std::string, ar_control::kPanelSlots> panel_titles_{};
  std::array<std::string, ar_control::kToolSlots> tool_titles_{};
  qar_ui_model::UiValue document_;
  bool published_once_ = false;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_STATE_HPP_
