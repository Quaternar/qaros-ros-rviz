// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_control_panel_state.hpp"

#include <algorithm>
#include <utility>

#include "qar_ui_model/type_description.hpp"

namespace qar_rviz_streaming
{
namespace
{

using qar_ui_model::PatchEntry;
using qar_ui_model::UiValue;

/// The longest text that the page takes from the desktop.
constexpr std::size_t kMaxTextBytes = 200;

/// @p text, or its first bytes with "..." when it is longer than kMaxTextBytes. The cut never
/// splits a UTF-8 sequence.
std::string cut(const std::string & text)
{
  if (text.size() <= kMaxTextBytes) {
    return text;
  }
  std::size_t end = kMaxTextBytes - 3;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) {
    --end;
  }
  return text.substr(0, end) + "...";
}

const std::string & empty_text()
{
  static const std::string kEmpty;
  return kEmpty;
}

const std::string & key_of_panel(const ControlPanelPanelEntry & entry)
{
  return entry.key;
}

const std::string & key_of_tool(const ControlPanelToolEntry & entry)
{
  return entry.panel_id;
}

/// What the operator reads for an entry: its name, else its key.
std::string name_of(const ControlPanelPanelEntry & entry)
{
  return cut(entry.name.empty() ? entry.key : entry.name);
}

std::string name_of(const ControlPanelToolEntry & entry)
{
  return cut(entry.name.empty() ? entry.panel_id : entry.name);
}

/// Puts the listed entries into the slots and returns how many found no slot.
///
/// A slot keeps its key while the key is listed and is freed when it is not. A slot freed by
/// this call stays empty until the next call, so a tap that the page sent before it learned of
/// the change cannot act on the entry that takes the slot next. The keys that have no slot yet
/// take the lowest slot that was already free before the call, in the order of the list.
/// @p slot_entry receives the entry that each slot shows (nullptr for a free slot). An entry
/// without a key and a second entry with a key that was seen already are ignored.
template<std::size_t N, typename Entry, typename KeyOf>
std::size_t assign_slots(
  std::array<std::string, N> & keys, const std::vector<Entry> & entries, KeyOf key_of,
  std::array<const Entry *, N> & slot_entry)
{
  const auto listed = [&](const std::string & key) {
      return std::any_of(
        entries.begin(), entries.end(), [&](const Entry & e) {return key_of(e) == key;});
    };
  std::array<bool, N> freed_now{};
  for (std::size_t slot = 0; slot < N; ++slot) {
    if (!keys[slot].empty() && !listed(keys[slot])) {
      keys[slot].clear();
      freed_now[slot] = true;
    }
  }

  std::size_t without_slot = 0;
  std::vector<std::string> seen;
  for (const Entry & entry : entries) {
    const std::string & key = key_of(entry);
    if (key.empty() || std::find(seen.begin(), seen.end(), key) != seen.end()) {
      continue;
    }
    seen.push_back(key);
    if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
      continue;
    }
    std::size_t free_slot = 0;
    while (free_slot < N && (!keys[free_slot].empty() || freed_now[free_slot])) {
      ++free_slot;
    }
    if (free_slot == N) {
      ++without_slot;
    } else {
      keys[free_slot] = key;
    }
  }

  for (std::size_t slot = 0; slot < N; ++slot) {
    slot_entry[slot] = nullptr;
    if (keys[slot].empty()) {
      continue;
    }
    for (const Entry & entry : entries) {
      if (key_of(entry) == keys[slot]) {
        slot_entry[slot] = &entry;
        break;
      }
    }
  }
  return without_slot;
}

std::string more_text(std::size_t count)
{
  return count == 0 ? std::string() : std::to_string(count) + " more on the desktop";
}

void add_leaf(std::vector<PatchEntry> & leaves, std::string path, UiValue value)
{
  leaves.push_back(PatchEntry{std::move(path), std::move(value)});
}

}  // namespace

const char * to_string(ConnectionState state)
{
  switch (state) {
    case ConnectionState::Offline:
      return "offline";
    case ConnectionState::Connecting:
      return "connecting";
    case ConnectionState::Live:
      return "live";
    case ConnectionState::Paused:
      return "paused";
    case ConnectionState::Preview:
      return "preview";
  }
  return "offline";
}

const char * to_string(PanelArState state)
{
  switch (state) {
    case PanelArState::Off:
      return "off";
    case PanelArState::Starting:
      return "starting";
    case PanelArState::Streaming:
      return "streaming";
    case PanelArState::Hidden:
      return "hidden";
    case PanelArState::ClosedInAr:
      return "closed_in_ar";
  }
  return "off";
}

bool panel_state_is_on(PanelArState state)
{
  return state == PanelArState::Starting || state == PanelArState::Streaming;
}

std::string pick_caption(const ControlPanelSnapshot & snapshot)
{
  const bool armed = snapshot.pick_mode_other || snapshot.pick_mode != ArPickMode::Off;
  if (!armed) {
    return ar_control::kCaptionPickOff;
  }

  std::string text;
  if (snapshot.pick_pending) {
    text = "Waiting: " + (snapshot.pick_summary.empty() ? std::string("pick") :
        cut(snapshot.pick_summary)) + " - Confirm or Cancel";
  } else if (!snapshot.pick_error.empty()) {
    text = "Refused: " + cut(snapshot.pick_error);
  } else if (snapshot.pick_mode_other) {
    text = "Armed on the desktop: " + (snapshot.pick_mode_other_name.empty() ?
        std::string("another mode") : cut(snapshot.pick_mode_other_name));
  } else if (snapshot.pick_mode == ArPickMode::Point) {
    text = "Point: aim and tap in the scene";
  } else {
    text = "Nav goal: press, drag to set the heading, release";
  }
  if (snapshot.pick_auto_confirm) {
    text += " - publishes on release (desktop setting)";
  }
  return text;
}

std::string panel_slot_title(const ControlPanelPanelEntry & entry)
{
  std::string title = name_of(entry);
  if (entry.state == PanelArState::Starting) {
    title += " (starting)";
  } else if (entry.state == PanelArState::ClosedInAr) {
    title += " (closed in AR)";
  }
  return title;
}

std::string tool_slot_title(const ControlPanelToolEntry & entry)
{
  const std::string name = name_of(entry);
  return entry.in_ar ? name : name + " (pending)";
}

std::vector<PatchEntry> ArControlPanelState::update(const ControlPanelSnapshot & snapshot)
{
  // The switches act on QarOS: without a QarOS session the page lists nothing, whatever the
  // desktop holds. Listing nothing frees every slot.
  const bool listed = snapshot.connection != ConnectionState::Offline &&
    snapshot.connection != ConnectionState::Preview;
  // The standard Displays dock has no switch on the page: its AR copy is listed instead.
  std::vector<ControlPanelPanelEntry> panels;
  if (listed) {
    panels.reserve(snapshot.panels.size());
    for (const ControlPanelPanelEntry & entry : snapshot.panels) {
      if (entry.key != ar_control::kStandardDisplaysKey) {
        panels.push_back(entry);
      }
    }
  }
  static const std::vector<ControlPanelToolEntry> kNoTools;
  const std::vector<ControlPanelToolEntry> & tools = listed ? snapshot.tools : kNoTools;

  std::array<const ControlPanelPanelEntry *, ar_control::kPanelSlots> panel_entry{};
  std::array<const ControlPanelToolEntry *, ar_control::kToolSlots> tool_entry{};
  const std::size_t more_panels = assign_slots(panel_keys_, panels, key_of_panel, panel_entry);
  const std::size_t more_tools = assign_slots(tool_ids_, tools, key_of_tool, tool_entry);

  for (std::size_t slot = 0; slot < ar_control::kPanelSlots; ++slot) {
    panel_titles_[slot] =
      panel_entry[slot] != nullptr ? name_of(*panel_entry[slot]) : std::string();
  }
  for (std::size_t slot = 0; slot < ar_control::kToolSlots; ++slot) {
    tool_titles_[slot] =
      tool_entry[slot] != nullptr ? name_of(*tool_entry[slot]) : std::string();
  }

  // Every path of the document, in document order.
  std::vector<PatchEntry> leaves;
  add_leaf(leaves, ar_control::kPathConnection, to_string(snapshot.connection));
  add_leaf(leaves, ar_control::kPathPeers, static_cast<std::int64_t>(snapshot.peers));
  add_leaf(
    leaves, ar_control::kPathError,
    snapshot.last_error.empty() ? std::string("none") : cut(snapshot.last_error));
  add_leaf(
    leaves, ar_control::kPathPickMode,
    snapshot.pick_mode_other ? "other" : to_string(snapshot.pick_mode));
  add_leaf(leaves, ar_control::kPathPickCaption, pick_caption(snapshot));
  for (std::size_t slot = 0; slot < ar_control::kPanelSlots; ++slot) {
    const ControlPanelPanelEntry * entry = panel_entry[slot];
    add_leaf(leaves, ar_control::panel_path(slot, "show"), entry != nullptr);
    add_leaf(
      leaves, ar_control::panel_path(slot, "title"),
      entry != nullptr ? UiValue(panel_slot_title(*entry)) : UiValue());
    add_leaf(
      leaves, ar_control::panel_path(slot, "on"),
      entry != nullptr && panel_state_is_on(entry->state));
  }
  for (std::size_t slot = 0; slot < ar_control::kToolSlots; ++slot) {
    const ControlPanelToolEntry * entry = tool_entry[slot];
    add_leaf(leaves, ar_control::tool_path(slot, "show"), entry != nullptr);
    add_leaf(
      leaves, ar_control::tool_path(slot, "title"),
      entry != nullptr ? UiValue(tool_slot_title(*entry)) : UiValue());
    add_leaf(leaves, ar_control::tool_path(slot, "on"), entry != nullptr && entry->on);
  }
  if (listed) {
    add_leaf(leaves, ar_control::kPathMorePanels, more_text(more_panels));
    add_leaf(leaves, ar_control::kPathMorePanelsShow, more_panels > 0);
  } else {
    add_leaf(leaves, ar_control::kPathMorePanels, ar_control::kLineNoLiveSession);
    add_leaf(leaves, ar_control::kPathMorePanelsShow, true);
  }
  add_leaf(leaves, ar_control::kPathMoreTools, more_text(more_tools));
  add_leaf(leaves, ar_control::kPathMoreToolsShow, more_tools > 0);

  // The new document, and the leaves whose value differs from the document of the last call.
  UiValue next = UiValue::object();
  std::vector<PatchEntry> changed;
  for (PatchEntry & leaf : leaves) {
    qar_ui_model::FieldPath path;
    std::string error;
    if (!qar_ui_model::FieldPath::parse(leaf.path, path, error)) {
      continue;
    }
    const UiValue * previous =
      published_once_ ? qar_ui_model::get_path(document_, path) : nullptr;
    if (previous == nullptr || *previous != leaf.value) {
      changed.push_back(leaf);
    }
    qar_ui_model::set_path(next, path, std::move(leaf.value), error);
  }
  document_ = std::move(next);
  published_once_ = true;
  return changed;
}

std::vector<PatchEntry> ArControlPanelState::resync_patch() const
{
  std::vector<PatchEntry> patch;
  if (published_once_) {
    patch.push_back(PatchEntry{std::string(), document_});
  }
  return patch;
}

const std::string & ArControlPanelState::panel_key(std::size_t slot) const
{
  return slot < panel_keys_.size() ? panel_keys_[slot] : empty_text();
}

const std::string & ArControlPanelState::tool_panel_id(std::size_t slot) const
{
  return slot < tool_ids_.size() ? tool_ids_[slot] : empty_text();
}

const std::string & ArControlPanelState::panel_title(std::size_t slot) const
{
  return slot < panel_titles_.size() ? panel_titles_[slot] : empty_text();
}

const std::string & ArControlPanelState::tool_title(std::size_t slot) const
{
  return slot < tool_titles_.size() ? tool_titles_[slot] : empty_text();
}

}  // namespace qar_rviz_streaming
