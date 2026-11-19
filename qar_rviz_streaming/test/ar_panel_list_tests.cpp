// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The rows of the list of AR panels: the kind of a key, the "In AR" text of every case, which
// action the primary button offers, the size column, per-peer AR control rows, the cut of an
// error and the pages of eight rows. Standard library only.

#include "qar_rviz_streaming/ar_panel_list.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::ar_panel_kind_of_key;
using qar_rviz_streaming::ar_panel_page;
using qar_rviz_streaming::ar_panel_size_text;
using qar_rviz_streaming::ar_panel_state_text;
using qar_rviz_streaming::ArPanelAction;
using qar_rviz_streaming::ArPanelKind;
using qar_rviz_streaming::ArPanelListInput;
using qar_rviz_streaming::ArPanelRow;
using qar_rviz_streaming::kArPanelListRows;
using qar_rviz_streaming::make_ar_panel_rows;

ArPanelListInput panel(const std::string & key, ArPanelKind kind = ArPanelKind::RvizPanel)
{
  ArPanelListInput input;
  input.key = key;
  input.title = key;
  input.kind = kind;
  return input;
}

ArPanelListInput streaming(const std::string & key)
{
  ArPanelListInput input = panel(key);
  input.in_manager = true;
  input.frames_sent = true;
  return input;
}

ArPanelRow row_of(const ArPanelListInput & input, bool hide_mode = true)
{
  return make_ar_panel_rows({input}, hide_mode).at(0);
}

}  // namespace

TEST(ArPanelList, KindsFromKeys)
{
  EXPECT_EQ(ar_panel_kind_of_key("qar_streaming_panel"), ArPanelKind::ThisPanel);
  EXPECT_EQ(ar_panel_kind_of_key("dock:Views"), ArPanelKind::RvizPanel);
  EXPECT_EQ(ar_panel_kind_of_key("dock:Time#2"), ArPanelKind::RvizPanel);
  EXPECT_EQ(ar_panel_kind_of_key("dialog:Add #2"), ArPanelKind::Dialog);
  EXPECT_EQ(ar_panel_kind_of_key("video:/camera/image"), ArPanelKind::Video);
  EXPECT_EQ(ar_panel_kind_of_key("hidden:Displays"), ArPanelKind::DisplaysCopy);
  EXPECT_EQ(ar_panel_kind_of_key("control:everybody"), ArPanelKind::ArControl);
  EXPECT_EQ(ar_panel_kind_of_key("tool:abc"), ArPanelKind::ArTool);
  EXPECT_EQ(ar_panel_kind_of_key("something else"), ArPanelKind::RvizPanel);
  EXPECT_STREQ(to_string(ArPanelKind::ThisPanel), "this panel");
  EXPECT_STREQ(to_string(ArPanelKind::RvizPanel), "RViz panel");
  EXPECT_STREQ(to_string(ArPanelKind::Dialog), "dialog");
  EXPECT_STREQ(to_string(ArPanelKind::Video), "video");
  EXPECT_STREQ(to_string(ArPanelKind::DisplaysCopy), "Displays copy");
  EXPECT_STREQ(to_string(ArPanelKind::ArControl), "AR control");
  EXPECT_STREQ(to_string(ArPanelKind::ArTool), "AR tool");
}

TEST(ArPanelList, StateTextsForEveryCase)
{
  EXPECT_EQ(ar_panel_state_text(panel("dock:A")), "off");

  ArPanelListInput input = panel("dock:A");
  input.in_manager = true;
  EXPECT_EQ(ar_panel_state_text(input), "starting");
  input.frames_sent = true;
  EXPECT_EQ(ar_panel_state_text(input), "shown");
  input.waiting = true;
  EXPECT_EQ(ar_panel_state_text(input), "waiting for a receiver");
  input.waiting = false;
  input.ar_hidden = true;
  EXPECT_EQ(ar_panel_state_text(input), "hidden");
  input.parked = true;
  EXPECT_EQ(ar_panel_state_text(input), "parked");
  input.parked = false;
  input.ar_hidden = false;
  input.ar_closed = true;
  EXPECT_EQ(ar_panel_state_text(input), "closed in AR");
  input.ar_closed = false;
  input.tombstoned = true;
  EXPECT_EQ(ar_panel_state_text(input), "notice");
  input.tombstoned = false;
  input.error = "no destination";
  EXPECT_EQ(ar_panel_state_text(input), "error: no destination");

  // The offline preview streams nothing to QarOS.
  ArPanelListInput mock = streaming("dock:A");
  mock.preview = true;
  EXPECT_EQ(ar_panel_state_text(mock), "shown (preview)");
  mock.in_manager = false;
  EXPECT_EQ(ar_panel_state_text(mock), "off");
  EXPECT_EQ(row_of(mock).cells[2], "-");
}

TEST(ArPanelList, SeenByIsEveryoneOnlyWhileInAr)
{
  EXPECT_EQ(row_of(panel("dock:A")).cells[2], "-");
  EXPECT_EQ(row_of(streaming("dock:A")).cells[2], "everyone");
}

TEST(ArPanelList, PrimaryActionFollowsTheState)
{
  // Off: stream.
  ArPanelRow row = row_of(panel("dock:A"));
  EXPECT_EQ(row.primary, ArPanelAction::Stream);
  EXPECT_EQ(row.primary_text, "Stream to AR");

  // Shown: hide in hide mode, stop in close mode.
  row = row_of(streaming("dock:A"), true);
  EXPECT_EQ(row.primary, ArPanelAction::Hide);
  EXPECT_EQ(row.primary_text, "Hide in AR");
  row = row_of(streaming("dock:A"), false);
  EXPECT_EQ(row.primary, ArPanelAction::Stop);
  EXPECT_EQ(row.primary_text, "Stop streaming");

  // Starting counts as shown: the stream runs.
  ArPanelListInput starting = panel("dock:A");
  starting.in_manager = true;
  EXPECT_EQ(row_of(starting).primary, ArPanelAction::Hide);

  // Hidden, parked and closed in AR: show.
  for (int which = 0; which < 3; ++which) {
    ArPanelListInput away = streaming("dock:A");
    away.ar_hidden = which == 0;
    away.parked = which == 1;
    away.ar_closed = which == 2;
    for (const bool hide_mode : {true, false}) {
      row = row_of(away, hide_mode);
      EXPECT_EQ(row.primary, ArPanelAction::Show) << which;
      EXPECT_EQ(row.primary_text, "Show in AR") << which;
    }
  }

  // A notice (a retired panel) is started again like an off panel.
  ArPanelListInput notice = streaming("dock:A");
  notice.tombstoned = true;
  EXPECT_EQ(row_of(notice).primary, ArPanelAction::Stream);

  // A dialog has no primary action; an AR control panel is re-opened instead.
  ArPanelListInput dialog = streaming("dialog:Add");
  dialog.kind = ArPanelKind::Dialog;
  EXPECT_EQ(row_of(dialog).primary, ArPanelAction::None);
  ArPanelListInput control = streaming("control:everybody");
  control.kind = ArPanelKind::ArControl;
  row = row_of(control);
  EXPECT_EQ(row.primary, ArPanelAction::None);
  EXPECT_TRUE(row.reopen);
  EXPECT_FALSE(row_of(streaming("dock:A")).reopen);
}

TEST(ArPanelList, SizeIsOnlyForScaledKinds)
{
  ArPanelListInput scaled = streaming("dock:A");
  scaled.scaled_kind = true;
  scaled.scale = 2.0;
  ArPanelRow row = row_of(scaled);
  EXPECT_EQ(row.cells[4], "2x");
  EXPECT_TRUE(row.size_enabled);
  EXPECT_EQ(row.size_text, "Native size in AR");

  scaled.scale = 1.07;
  EXPECT_EQ(row_of(scaled).cells[4], "1.07x");
  scaled.scale = 1.5;
  EXPECT_EQ(row_of(scaled).cells[4], "1.5x");

  scaled.size_native = true;
  scaled.scale = 1.0;
  row = row_of(scaled);
  EXPECT_EQ(row.cells[4], "native");
  EXPECT_EQ(row.size_text, "Enlarged in AR");

  ArPanelListInput video = streaming("video:/cam");
  video.kind = ArPanelKind::Video;
  row = row_of(video);
  EXPECT_EQ(row.cells[4], "-");
  EXPECT_FALSE(row.size_enabled);

  EXPECT_EQ(ar_panel_size_text(false, 3.0), "3x");
  EXPECT_EQ(ar_panel_size_text(false, 1.004), "1x");
  EXPECT_EQ(ar_panel_size_text(true, 2.0), "native");
}

TEST(ArPanelList, PerPeerControlRowsNameThePeer)
{
  ArPanelListInput everybody = streaming("control:everybody");
  everybody.kind = ArPanelKind::ArControl;
  everybody.title = "AR control panel";
  ArPanelListInput peer = streaming("control:peer:0a1b");
  peer.kind = ArPanelKind::ArControl;
  peer.title = "AR control panel";
  peer.seen_by = "Quest 3";
  const std::vector<ArPanelRow> rows = make_ar_panel_rows({everybody, peer}, true);
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_EQ(rows[0].cells[1], "AR control");
  EXPECT_EQ(rows[0].cells[2], "everyone");
  EXPECT_EQ(rows[1].cells[2], "Quest 3");
  EXPECT_EQ(rows[1].key, "control:peer:0a1b");
}

TEST(ArPanelList, ErrorsAreCutButKeptInTheToolTip)
{
  const std::string long_error =
    "the receiver reported a failure that goes on for a long while and a line\nbreak";
  ArPanelListInput input = streaming("dock:A");
  input.error = long_error;
  const ArPanelRow row = row_of(input);
  EXPECT_EQ(row.cells[3], "error: " + long_error.substr(0, 40));
  EXPECT_EQ(row.cells[3].size(), 7u + 40u);
  EXPECT_NE(row.tool_tip.find("In AR: error: the receiver reported a failure that goes on"),
    std::string::npos);
  // The tool tip holds the whole text, line break shown as a space.
  EXPECT_NE(row.tool_tip.find("a line break"), std::string::npos);
  EXPECT_EQ(row.cells[3].find('\n'), std::string::npos);

  // A cut never splits a UTF-8 sequence: 39 ASCII letters and a two-byte letter at 39..40.
  ArPanelListInput utf8 = streaming("dock:A");
  utf8.error = std::string(39, 'a') + "\xC3\xA9" + "tail";
  EXPECT_EQ(row_of(utf8).cells[3], "error: " + std::string(39, 'a'));
}

TEST(ArPanelList, TheToolTipNamesTheWholeRow)
{
  ArPanelListInput input = streaming("dock:Views");
  input.title = "Views";
  const ArPanelRow row = row_of(input);
  EXPECT_EQ(row.tool_tip, "Views\nKind: RViz panel\nSeen by: everyone\nIn AR: shown\nSize: -");
}

TEST(ArPanelList, PagesOfEight)
{
  EXPECT_EQ(kArPanelListRows, 8);

  auto page = ar_panel_page(0, 0);
  EXPECT_EQ(page.first, 0);
  EXPECT_EQ(page.last, 0);
  EXPECT_FALSE(page.prev);
  EXPECT_FALSE(page.next);
  EXPECT_TRUE(page.label.empty());

  page = ar_panel_page(8, 0);
  EXPECT_EQ(page.last, 8);
  EXPECT_FALSE(page.next);
  EXPECT_TRUE(page.label.empty());

  page = ar_panel_page(12, 0);
  EXPECT_EQ(page.first, 0);
  EXPECT_EQ(page.last, 8);
  EXPECT_FALSE(page.prev);
  EXPECT_TRUE(page.next);
  EXPECT_EQ(page.label, "Rows 1-8 of 12");

  page = ar_panel_page(12, 1);
  EXPECT_EQ(page.first, 8);
  EXPECT_EQ(page.last, 12);
  EXPECT_TRUE(page.prev);
  EXPECT_FALSE(page.next);
  EXPECT_EQ(page.label, "Rows 9-12 of 12");

  // An index past the end is the last page, a negative one the first.
  EXPECT_EQ(ar_panel_page(12, 9).first, 8);
  EXPECT_EQ(ar_panel_page(12, -3).first, 0);
  // Three pages.
  page = ar_panel_page(17, 1);
  EXPECT_TRUE(page.prev);
  EXPECT_TRUE(page.next);
  EXPECT_EQ(page.label, "Rows 9-16 of 17");
}

namespace
{

/// The row input of an AR tool the way the host fills it: in_manager while its QarOS panel exists
/// or is about to be made, frames_sent while the panel exists.
ArPanelListInput tool(const std::string & name, bool made, bool session = true)
{
  ArPanelListInput input;
  input.key = "tool:" + name;
  input.title = name;
  input.kind = ArPanelKind::ArTool;
  input.frames_sent = made;
  input.in_manager = made || session;
  input.detail = "AR tool: topic " + name + " (std_msgs/msg/Bool); read-only";
  return input;
}

}  // namespace

TEST(ArPanelList, AnArToolRowFollowsThePanelOfTheTool)
{
  // No session: nothing is made, the row says off and offers nothing to press.
  ArPanelRow row = row_of(tool("/scan", false, false));
  EXPECT_EQ(row.cells[1], "AR tool");
  EXPECT_EQ(row.cells[3], "off");
  EXPECT_EQ(row.cells[4], "-");
  EXPECT_EQ(row.primary, ArPanelAction::None);
  EXPECT_TRUE(row.reopen);
  EXPECT_FALSE(row.size_enabled);

  // A session runs and the panel is not made yet: starting, still nothing to press.
  row = row_of(tool("/scan", false));
  EXPECT_EQ(row.cells[2], "everyone");
  EXPECT_EQ(row.cells[3], "starting");
  EXPECT_EQ(row.primary, ArPanelAction::None);

  // Made: shown, and the primary action hides it, in both modes (a tool is never stopped).
  for (const bool hide_mode : {true, false}) {
    row = row_of(tool("/scan", true), hide_mode);
    EXPECT_EQ(row.cells[3], "shown");
    EXPECT_EQ(row.primary, ArPanelAction::Hide);
    EXPECT_EQ(row.primary_text, "Hide in AR");
  }

  // Hidden: show.
  ArPanelListInput hidden = tool("/scan", true);
  hidden.ar_hidden = true;
  for (const bool hide_mode : {true, false}) {
    row = row_of(hidden, hide_mode);
    EXPECT_EQ(row.cells[3], "hidden");
    EXPECT_EQ(row.primary, ArPanelAction::Show);
    EXPECT_EQ(row.primary_text, "Show in AR");
  }

  // Closed in AR: show as well.
  ArPanelListInput closed = tool("/scan", true);
  closed.ar_closed = true;
  row = row_of(closed);
  EXPECT_EQ(row.cells[3], "closed in AR");
  EXPECT_EQ(row.primary, ArPanelAction::Show);

  // An error is cut in the cell and whole in the tool tip, which also has the line of the host.
  ArPanelListInput failing = tool("/scan", false);
  failing.error = "QarOS would not open the page: the panel limit of the runtime is reached";
  row = row_of(failing);
  EXPECT_EQ(row.cells[3], "error: " + failing.error.substr(0, 40));
  EXPECT_NE(row.tool_tip.find(failing.error), std::string::npos);
  EXPECT_NE(row.tool_tip.find("AR tool: topic /scan (std_msgs/msg/Bool); read-only"),
    std::string::npos);

  // The offline preview shows nothing in AR.
  ArPanelListInput preview = tool("/scan", false);
  preview.preview = true;
  preview.in_manager = false;
  row = row_of(preview);
  EXPECT_EQ(row.cells[2], "-");
  EXPECT_EQ(row.cells[3], "off");
  EXPECT_EQ(row.primary, ArPanelAction::None);
}

TEST(ArPanelList, ToolRowsKeepTheirPlaceAfterTheControlRowsAndPage)
{
  std::vector<ArPanelListInput> entries;
  for (int i = 0; i < 6; ++i) {
    entries.push_back(streaming("dock:" + std::to_string(i)));
  }
  ArPanelListInput control = streaming("control:everybody");
  control.kind = ArPanelKind::ArControl;
  entries.push_back(control);
  entries.push_back(tool("/a", true));
  entries.push_back(tool("/b", false));
  entries.push_back(tool("/c", true));

  const std::vector<ArPanelRow> rows = make_ar_panel_rows(entries, true);
  ASSERT_EQ(rows.size(), 10u);
  EXPECT_EQ(rows[6].cells[1], "AR control");
  EXPECT_EQ(rows[7].key, "tool:/a");
  EXPECT_EQ(rows[8].key, "tool:/b");
  EXPECT_EQ(rows[9].key, "tool:/c");
  // The ninth and tenth row are on the second page.
  const auto page = ar_panel_page(static_cast<int>(rows.size()), 1);
  EXPECT_EQ(page.first, 8);
  EXPECT_EQ(page.last, 10);
  EXPECT_EQ(page.label, "Rows 9-10 of 10");
  EXPECT_EQ(ar_panel_kind_of_key("tool:/a"), ArPanelKind::ArTool);
}
