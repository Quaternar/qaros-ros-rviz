// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_panel_list.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "qar_rviz_streaming/ar_copy_scale.hpp"

namespace qar_rviz_streaming
{
namespace
{

constexpr std::size_t kErrorCells = 40;

/// The first @p count bytes of @p text with line breaks and other control characters turned into
/// spaces; a cut never splits a UTF-8 sequence.
std::string printable_prefix(const std::string & text, std::size_t count)
{
  std::string out;
  out.reserve(std::min(text.size(), count));
  for (const char c : text) {
    const unsigned char u = static_cast<unsigned char>(c);
    out.push_back(u < 0x20 || u == 0x7f ? ' ' : c);
  }
  if (out.size() <= count) {
    return out;
  }
  std::size_t end = count;
  while (end > 0 && (static_cast<unsigned char>(out[end]) & 0xC0) == 0x80) {
    --end;
  }
  out.resize(end);
  return out;
}

bool starts_with(const std::string & text, const char * prefix)
{
  return text.rfind(prefix, 0) == 0;
}

/// The kinds of entry whose primary action streams, hides, shows or stops a Qt panel.
bool is_qt_panel(ArPanelKind kind)
{
  return kind == ArPanelKind::ThisPanel || kind == ArPanelKind::RvizPanel ||
         kind == ArPanelKind::Video || kind == ArPanelKind::DisplaysCopy;
}

}  // namespace

const char * to_string(ArPanelKind kind)
{
  switch (kind) {
    case ArPanelKind::ThisPanel: return "this panel";
    case ArPanelKind::RvizPanel: return "RViz panel";
    case ArPanelKind::Dialog: return "dialog";
    case ArPanelKind::Video: return "video";
    case ArPanelKind::DisplaysCopy: return "Displays copy";
    case ArPanelKind::ArControl: return "AR control";
    case ArPanelKind::ArTool: return "AR tool";
  }
  return "RViz panel";
}

ArPanelKind ar_panel_kind_of_key(const std::string & key)
{
  if (key == "qar_streaming_panel") {
    return ArPanelKind::ThisPanel;
  }
  if (key == "hidden:Displays") {
    return ArPanelKind::DisplaysCopy;
  }
  if (starts_with(key, "dialog:")) {
    return ArPanelKind::Dialog;
  }
  if (starts_with(key, "video:")) {
    return ArPanelKind::Video;
  }
  if (starts_with(key, "control:")) {
    return ArPanelKind::ArControl;
  }
  if (starts_with(key, "tool:")) {
    return ArPanelKind::ArTool;
  }
  return ArPanelKind::RvizPanel;
}

std::string ar_panel_size_text(bool native, double scale)
{
  if (native) {
    return "native";
  }
  // Hundredths, without trailing zeros: "2x", "1.5x", "1.07x".
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", std::floor(scale * 100.0 + 0.5) / 100.0);
  std::string text = buffer;
  while (!text.empty() && text.back() == '0') {
    text.pop_back();
  }
  if (!text.empty() && text.back() == '.') {
    text.pop_back();
  }
  return text + "x";
}

std::string ar_panel_state_text(const ArPanelListInput & input)
{
  if (input.preview) {
    return input.in_manager ? "shown (preview)" : "off";
  }
  if (!input.in_manager) {
    return "off";
  }
  if (input.tombstoned) {
    return "notice";
  }
  if (input.parked) {
    return "parked";
  }
  if (input.ar_closed) {
    return "closed in AR";
  }
  if (input.ar_hidden) {
    return "hidden";
  }
  if (!input.error.empty()) {
    return "error: " + printable_prefix(input.error, kErrorCells);
  }
  if (input.waiting) {
    return "waiting for a receiver";
  }
  return input.frames_sent ? "shown" : "starting";
}

std::vector<ArPanelRow> make_ar_panel_rows(
  const std::vector<ArPanelListInput> & entries, bool hide_mode)
{
  std::vector<ArPanelRow> rows;
  rows.reserve(entries.size());
  for (const ArPanelListInput & input : entries) {
    ArPanelRow row;
    row.key = input.key;

    const std::string state = ar_panel_state_text(input);
    const bool off = state == "off";
    row.cells[0] = input.title;
    row.cells[1] = to_string(input.kind);
    if (off || input.preview) {
      row.cells[2] = "-";
    } else {
      row.cells[2] = input.seen_by.empty() ? "everyone" : input.seen_by;
    }
    row.cells[3] = state;
    row.cells[4] = input.scaled_kind ? ar_panel_size_text(input.size_native, input.scale) : "-";

    // The whole row in words; an error is kept in full here, the cell shows its first characters.
    std::string full_state = state;
    if (!input.preview && input.in_manager && !input.tombstoned && !input.parked &&
      !input.ar_closed && !input.ar_hidden && !input.error.empty())
    {
      full_state = "error: " + printable_prefix(input.error, input.error.size());
    }
    row.tool_tip = input.title + "\nKind: " + row.cells[1] + "\nSeen by: " + row.cells[2] +
      "\nIn AR: " + full_state + "\nSize: " + row.cells[4];
    if (!input.detail.empty()) {
      row.tool_tip += "\n" + input.detail;
    }

    if (input.kind == ArPanelKind::ArTool) {
      // The panel of a tool is hidden or shown, never stopped: the tool, its page and its ROS
      // entity stay. Until the QarOS panel exists the tick makes it and there is nothing to press.
      if (input.in_manager && input.frames_sent) {
        const bool away = input.ar_hidden || input.ar_closed;
        row.primary = away ? ArPanelAction::Show : ArPanelAction::Hide;
        row.primary_text = away ? "Show in AR" : "Hide in AR";
      }
    } else if (is_qt_panel(input.kind)) {
      const bool away = input.in_manager && !input.tombstoned &&
        (input.parked || input.ar_hidden || input.ar_closed);
      const bool running = input.in_manager && !input.tombstoned && !away;
      if (away) {
        row.primary = ArPanelAction::Show;
        row.primary_text = "Show in AR";
      } else if (running) {
        row.primary = hide_mode ? ArPanelAction::Hide : ArPanelAction::Stop;
        row.primary_text = hide_mode ? "Hide in AR" : "Stop streaming";
      } else {
        row.primary = ArPanelAction::Stream;
        row.primary_text = "Stream to AR";
      }
    }
    row.size_enabled = input.scaled_kind;
    row.size_text = input.size_native ? kArCopyButtonToEnlargedText : kArCopyButtonToNativeText;
    row.reopen = input.kind == ArPanelKind::ArControl || input.kind == ArPanelKind::ArTool;
    rows.push_back(std::move(row));
  }
  return rows;
}

ArPanelPage ar_panel_page(int total, int page_index)
{
  ArPanelPage page;
  page.total = std::max(total, 0);
  if (page.total == 0) {
    return page;
  }
  const int pages = (page.total + kArPanelListRows - 1) / kArPanelListRows;
  const int index = std::clamp(page_index, 0, pages - 1);
  page.first = index * kArPanelListRows;
  page.last = std::min(page.first + kArPanelListRows, page.total);
  page.prev = index > 0;
  page.next = index + 1 < pages;
  if (pages > 1) {
    page.label = "Rows " + std::to_string(page.first + 1) + "-" + std::to_string(page.last) +
      " of " + std::to_string(page.total);
  }
  return page;
}

}  // namespace qar_rviz_streaming
