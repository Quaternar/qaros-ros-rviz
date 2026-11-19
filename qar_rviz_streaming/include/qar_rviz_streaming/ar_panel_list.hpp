// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_PANEL_LIST_HPP_
#define QAR_RVIZ_STREAMING__AR_PANEL_LIST_HPP_

/// @file ar_panel_list.hpp
/// @brief The rows of the list of AR panels on the "AR panels" tab: what is streamed, to whom, in
/// which state, and which actions the row offers. Standard library only.
///
/// The host collects one @ref ArPanelListInput per entry from its sources (the open RViz panels,
/// the stream manager's diagnostics, the AR control panel instances). This unit turns them into
/// the texts of the five columns and into the actions of the buttons under the list. It holds no
/// state and calls nothing.

#include <cstdint>
#include <string>
#include <vector>

namespace qar_rviz_streaming
{

/// The rows the list shows at a time; more rows are reached with the pager.
constexpr int kArPanelListRows = 8;

/// The columns of the list: "Panel", "Kind", "Seen by", "In AR", "Size".
constexpr int kArPanelListColumns = 5;

/// What an entry is.
enum class ArPanelKind : std::uint8_t
{
  ThisPanel,     ///< the Qar Streaming panel itself
  RvizPanel,     ///< a dock of the RViz window
  Dialog,        ///< a dialog streamed beside the panel that opened it
  Video,         ///< a video panel
  DisplaysCopy,  ///< the hidden copy of the Displays panel
  ArControl,     ///< one instance of the AR control panel
  ArTool         ///< a panel of an AR tool
};

/// What the primary button of a row does.
enum class ArPanelAction : std::uint8_t
{
  None,    ///< the row has no primary action
  Stream,  ///< stream the panel to AR
  Hide,    ///< hide it in AR (the stream is kept)
  Show,    ///< show it in AR again
  Stop     ///< stop streaming it (the AR panel is closed or kept as a notice)
};

/// @brief What the host knows about one entry.
struct ArPanelListInput
{
  std::string key;    ///< the stable key of the entry (the key of the stream manager for a panel)
  std::string title;  ///< what the person knows it as
  ArPanelKind kind{};
  bool in_manager = false;   ///< a stream (or an AR panel) exists for the entry
  bool tombstoned = false;   ///< the window is gone and the AR panel shows a notice
  bool parked = false;       ///< the window was closed on the desktop; the AR panel is kept hidden
  bool ar_hidden = false;    ///< hidden in AR
  bool ar_closed = false;    ///< the person closed its AR panel
  bool waiting = false;      ///< streamed, and nobody receives the frames yet
  bool frames_sent = false;  ///< at least one frame was sent
  std::string error;         ///< the last error of the stream; empty when there is none
  std::string seen_by;       ///< the peer a per-peer AR panel is for; empty = everyone
  bool scaled_kind = false;  ///< the AR copy of the entry has a size choice
  bool size_native = false;  ///< the AR copy is at native size (the choice, not the factor)
  double scale = 1.0;        ///< the factor in force for an enlarged copy
  bool preview = false;      ///< the offline preview: nothing is streamed to QarOS
  /// One more line for the tool tip (an AR tool: what it exposes and whether it can publish).
  std::string detail;
};

/// @brief One row of the list and the actions its buttons offer.
struct ArPanelRow
{
  std::string key;
  /// The five columns: Panel, Kind, Seen by, In AR, Size.
  std::string cells[kArPanelListColumns];
  std::string tool_tip;  ///< the whole row in words, with the full text of an error
  ArPanelAction primary = ArPanelAction::None;
  std::string primary_text;     ///< "Stream to AR", "Hide in AR", "Stop streaming", "Show in AR"
  bool size_enabled = false;    ///< the size action applies to the entry
  std::string size_text;        ///< "Native size in AR" or "Enlarged in AR"
  bool reopen = false;          ///< the "Re-open in AR" action applies to the entry
};

/// @brief The rows of @p entries, in the order given. @p hide_mode is the "AR Visibility" setting
/// "hide": a Qt panel that is shown is then hidden by its primary action, otherwise it is stopped.
/// The panel of an AR tool is always hidden or shown (the tool and its page stay), and while it has
/// no QarOS panel (frames_sent is false) its row has no primary action.
///
/// For an AR tool the host sets in_manager when its QarOS panel exists or is about to be made
/// (a session runs), and frames_sent when the panel exists: the "In AR" text is then off,
/// starting, shown or hidden.
std::vector<ArPanelRow> make_ar_panel_rows(
  const std::vector<ArPanelListInput> & entries, bool hide_mode);

/// @brief The "In AR" text of @p input: off, starting, shown, hidden, parked, closed in AR,
/// waiting for a receiver, notice, error: <first 40 characters>, or in the preview shown
/// (preview) / off.
std::string ar_panel_state_text(const ArPanelListInput & input);

/// @brief The pager state of a list of @p total rows at page @p page_index (0 is the first page; an
/// index past the end is taken as the last page, a negative one as the first).
struct ArPanelPage
{
  int first = 0;       ///< the index of the first row on the page
  int last = 0;        ///< one past the index of the last row on the page
  int total = 0;
  bool prev = false;   ///< there is a page before this one
  bool next = false;   ///< there is a page after this one
  std::string label;   ///< "Rows 1-8 of 12"; empty while all rows fit on one page
};
ArPanelPage ar_panel_page(int total, int page_index);

/// @brief The kind a key stands for: "qar_streaming_panel", "dock:...", "dialog:...", "video:...",
/// "hidden:Displays", "control:...", "tool:...". A key of another shape is taken as an RViz panel.
ArPanelKind ar_panel_kind_of_key(const std::string & key);

/// @brief The "Kind" column text of @p kind.
const char * to_string(ArPanelKind kind);

/// @brief The text of the "Size" column for a scaled entry: "2x", "1.07x", "native".
std::string ar_panel_size_text(bool native, double scale);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_PANEL_LIST_HPP_
