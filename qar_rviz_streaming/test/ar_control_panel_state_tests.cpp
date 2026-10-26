// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// What the AR control page shows: snapshot -> slots -> document -> only the changed paths.
// Pure C++17: no session, no ROS graph, no Qt.

#include "qar_rviz_streaming/ar_control_panel_state.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "qar_ui_model/type_description.hpp"

namespace
{

using qar_rviz_streaming::ArControlPanelState;
using qar_rviz_streaming::ArPickMode;
using qar_rviz_streaming::ConnectionState;
using qar_rviz_streaming::ControlPanelPanelEntry;
using qar_rviz_streaming::ControlPanelSnapshot;
using qar_rviz_streaming::ControlPanelToolEntry;
using qar_rviz_streaming::PanelArState;
using qar_ui_model::PatchEntry;
using qar_ui_model::UiValue;
namespace ac = qar_rviz_streaming::ar_control;

ControlPanelPanelEntry panel(
  const std::string & key, const std::string & name, PanelArState state = PanelArState::Off)
{
  ControlPanelPanelEntry entry;
  entry.key = key;
  entry.name = name;
  entry.state = state;
  return entry;
}

ControlPanelToolEntry tool(const std::string & id, const std::string & name, bool in_ar = true)
{
  ControlPanelToolEntry entry;
  entry.panel_id = id;
  entry.name = name;
  entry.in_ar = in_ar;
  return entry;
}

/// The value at @p path of @p document, or a null value when there is none.
UiValue value_at(const UiValue & document, const std::string & path)
{
  qar_ui_model::FieldPath parsed;
  std::string error;
  EXPECT_TRUE(qar_ui_model::FieldPath::parse(path, parsed, error)) << path << ": " << error;
  const UiValue * found = qar_ui_model::get_path(document, parsed);
  EXPECT_NE(found, nullptr) << "no value at " << path;
  return found != nullptr ? *found : UiValue();
}

std::vector<std::string> paths_of(const std::vector<PatchEntry> & patch)
{
  std::vector<std::string> paths;
  for (const PatchEntry & entry : patch) {
    paths.push_back(entry.path);
  }
  return paths;
}

const UiValue * find_entry(const std::vector<PatchEntry> & patch, const std::string & path)
{
  for (const PatchEntry & entry : patch) {
    if (entry.path == path) {
      return &entry.value;
    }
  }
  return nullptr;
}

std::vector<std::string> sorted(std::vector<std::string> words)
{
  std::sort(words.begin(), words.end());
  return words;
}

std::string caption_of(const ControlPanelSnapshot & snapshot)
{
  return qar_rviz_streaming::pick_caption(snapshot);
}

/// A snapshot of a QarOS session, the connection whose page lists the panels and tools of the
/// desktop. A default snapshot is offline, and an offline page lists nothing.
ControlPanelSnapshot live_snapshot()
{
  ControlPanelSnapshot snapshot;
  snapshot.connection = ConnectionState::Live;
  return snapshot;
}

const char * const kNoSessionLine = "AR panels and tools are switched in a live QarOS session";

}  // namespace

TEST(ArControlPanelState, FirstUpdatePublishesEverything)
{
  ArControlPanelState state;
  EXPECT_TRUE(state.document().is_null()) << "no document before the first update";

  const std::vector<PatchEntry> first = state.update(ControlPanelSnapshot{});
  // 3 status + 2 pick + 6 x 3 panel + 4 x 3 tool + 4 more paths.
  EXPECT_EQ(first.size(), 39u);
  const std::vector<std::string> paths = paths_of(first);
  EXPECT_EQ(std::set<std::string>(paths.begin(), paths.end()).size(), paths.size())
    << "a path once";
  for (const char * path : {ac::kPathConnection, ac::kPathPeers, ac::kPathError, ac::kPathPickMode,
      ac::kPathPickCaption, ac::kPathMorePanels, ac::kPathMorePanelsShow, ac::kPathMoreTools,
      ac::kPathMoreToolsShow})
  {
    EXPECT_NE(find_entry(first, path), nullptr) << path;
  }
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    for (const char * field : {"show", "title", "on"}) {
      EXPECT_NE(find_entry(first, ac::panel_path(slot, field)), nullptr) << slot << field;
    }
  }
  for (std::size_t slot = 0; slot < ac::kToolSlots; ++slot) {
    for (const char * field : {"show", "title", "on"}) {
      EXPECT_NE(find_entry(first, ac::tool_path(slot, field)), nullptr) << slot << field;
    }
  }

  // The empty snapshot: offline, no peers, no error, nothing armed, every slot hidden, and the
  // line that says why there are no switches.
  EXPECT_EQ(*find_entry(first, "status.connection"), UiValue("offline"));
  EXPECT_EQ(*find_entry(first, "status.peers"), UiValue(0));
  EXPECT_EQ(*find_entry(first, "status.error"), UiValue("none"));
  EXPECT_EQ(*find_entry(first, "pick.mode"), UiValue("off"));
  EXPECT_EQ(
    *find_entry(first, "pick.caption"), UiValue("Choose Point or Nav goal, then aim in the scene"));
  EXPECT_EQ(*find_entry(first, "panels.s0.show"), UiValue(false));
  EXPECT_TRUE(find_entry(first, "panels.s0.title")->is_null());
  EXPECT_EQ(*find_entry(first, "panels.s5.on"), UiValue(false));
  EXPECT_EQ(*find_entry(first, "tools.s3.show"), UiValue(false));
  EXPECT_TRUE(find_entry(first, "tools.s3.title")->is_null());
  EXPECT_EQ(*find_entry(first, "tools.s3.on"), UiValue(false));
  EXPECT_EQ(*find_entry(first, "more.panels"), UiValue(kNoSessionLine));
  EXPECT_EQ(*find_entry(first, "more.panels_show"), UiValue(true));
  EXPECT_EQ(*find_entry(first, "more.tools"), UiValue(""));
  EXPECT_EQ(*find_entry(first, "more.tools_show"), UiValue(false));

  // That document is the one the page starts from, path by path; so is a document that the
  // patch builds from the initial one.
  EXPECT_EQ(state.document(), qar_rviz_streaming::make_ar_control_initial_document());
  qar_ui_model::UiValue session = qar_rviz_streaming::make_ar_control_initial_document();
  qar_ui_model::DataBody body;
  body.patch = first;
  std::string error;
  ASSERT_TRUE(qar_ui_model::apply_patch(session, body, error)) << error;
  EXPECT_EQ(session, state.document());

  // A state that is not the empty one also publishes every path on its first update.
  ControlPanelSnapshot busy;
  busy.connection = ConnectionState::Live;
  busy.peers = 2;
  busy.panels = {panel("dock:Views", "Views", PanelArState::Streaming)};
  busy.tools = {tool("tool.a", "Speed")};
  ArControlPanelState other;
  EXPECT_EQ(other.update(busy).size(), 39u);
}

TEST(ArControlPanelState, ResyncReplacesTheWholeDocument)
{
  ArControlPanelState state;
  EXPECT_TRUE(state.resync_patch().empty()) << "nothing to say before the first update";

  ControlPanelSnapshot snapshot;
  snapshot.connection = ConnectionState::Live;
  snapshot.panels = {panel("dock:Views", "Views", PanelArState::Off)};
  const std::vector<PatchEntry> first = state.update(snapshot);

  // The page, after the operator tapped the switch of "Views": the page writes the value into
  // its own copy of the document at once.
  UiValue page = qar_rviz_streaming::make_ar_control_initial_document();
  qar_ui_model::DataBody body;
  body.patch = first;
  std::string error;
  ASSERT_TRUE(qar_ui_model::apply_patch(page, body, error)) << error;
  qar_ui_model::FieldPath on_path;
  ASSERT_TRUE(qar_ui_model::FieldPath::parse("panels.s0.on", on_path, error)) << error;
  ASSERT_TRUE(qar_ui_model::set_path(page, on_path, UiValue(true), error)) << error;
  EXPECT_NE(page, state.document());

  // The host refused the command, so the snapshot has not changed and nothing is published ...
  EXPECT_TRUE(state.update(snapshot).empty());
  // ... which is why the host sends the whole document: it puts the true value back.
  const std::vector<PatchEntry> resync = state.resync_patch();
  ASSERT_EQ(resync.size(), 1u);
  EXPECT_EQ(resync[0].path, "");
  EXPECT_EQ(resync[0].value, state.document());
  body.patch = resync;
  ASSERT_TRUE(qar_ui_model::apply_patch(page, body, error)) << error;
  EXPECT_EQ(page, state.document());
  EXPECT_EQ(value_at(page, "panels.s0.on"), UiValue(false));
}

TEST(ArControlPanelState, UnchangedSnapshotPublishesNothing)
{
  ControlPanelSnapshot snapshot;
  snapshot.connection = ConnectionState::Live;
  snapshot.peers = 1;
  snapshot.panels = {
    panel("dock:Views", "Views", PanelArState::Streaming), panel("video:/cam", "Video /cam")};
  snapshot.tools = {tool("tool.a", "Speed")};
  ArControlPanelState state;
  state.update(snapshot);
  EXPECT_TRUE(state.update(snapshot).empty());
  EXPECT_TRUE(state.update(snapshot).empty());
  const UiValue before = state.document();

  // One change gives one path.
  snapshot.peers = 2;
  EXPECT_EQ(paths_of(state.update(snapshot)), (std::vector<std::string>{"status.peers"}));
  snapshot.last_error = "AR panel: could not hide 'Views' in AR";
  const std::vector<PatchEntry> error_patch = state.update(snapshot);
  ASSERT_EQ(error_patch.size(), 1u);
  EXPECT_EQ(error_patch[0].path, "status.error");
  EXPECT_EQ(error_patch[0].value, UiValue("AR panel: could not hide 'Views' in AR"));
  snapshot.last_error.clear();
  EXPECT_EQ(*find_entry(state.update(snapshot), "status.error"), UiValue("none"));
  snapshot.peers = 1;
  state.update(snapshot);
  EXPECT_EQ(state.document(), before);

  // A change of the pick state moves the mode and the caption together.
  snapshot.pick_mode = ArPickMode::Point;
  EXPECT_EQ(
    sorted(paths_of(state.update(snapshot))),
    (std::vector<std::string>{"pick.caption", "pick.mode"}));
  // A change inside one slot touches only that slot's paths.
  snapshot.panels[1].state = PanelArState::Streaming;
  EXPECT_EQ(
    sorted(paths_of(state.update(snapshot))),
    (std::vector<std::string>{"panels.s1.on"}));
  snapshot.panels[1].state = PanelArState::Hidden;
  EXPECT_EQ(
    sorted(paths_of(state.update(snapshot))),
    (std::vector<std::string>{"panels.s1.on"}));
  snapshot.tools[0].in_ar = false;
  EXPECT_EQ(paths_of(state.update(snapshot)), (std::vector<std::string>{"tools.s0.title"}));
  // A tool's switch is its own path, as a panel's is.
  snapshot.tools[0].on = true;
  EXPECT_EQ(paths_of(state.update(snapshot)), (std::vector<std::string>{"tools.s0.on"}));
  snapshot.tools[0].on = false;
  EXPECT_EQ(paths_of(state.update(snapshot)), (std::vector<std::string>{"tools.s0.on"}));
}

TEST(ArControlPanelState, SlotsStayStableWhenEntriesComeAndGo)
{
  ArControlPanelState state;
  ControlPanelSnapshot snapshot = live_snapshot();
  snapshot.panels = {panel("a", "Alpha"), panel("b", "Bravo"), panel("c", "Charlie")};
  snapshot.tools = {tool("t1", "One"), tool("t2", "Two")};
  state.update(snapshot);
  EXPECT_EQ(state.panel_key(0), "a");
  EXPECT_EQ(state.panel_key(1), "b");
  EXPECT_EQ(state.panel_key(2), "c");
  EXPECT_EQ(state.panel_key(3), "");
  EXPECT_EQ(state.panel_title(1), "Bravo");
  EXPECT_EQ(state.tool_panel_id(0), "t1");
  EXPECT_EQ(state.tool_panel_id(1), "t2");
  EXPECT_EQ(state.tool_title(1), "Two");

  // B goes: C keeps its slot and slot 1 becomes free (hidden, no title).
  snapshot.panels = {panel("a", "Alpha"), panel("c", "Charlie")};
  const std::vector<PatchEntry> gone = state.update(snapshot);
  EXPECT_EQ(state.panel_key(1), "");
  EXPECT_EQ(state.panel_key(2), "c");
  EXPECT_EQ(state.panel_title(1), "");
  EXPECT_EQ(
    sorted(paths_of(gone)), (std::vector<std::string>{"panels.s1.show", "panels.s1.title"}));
  EXPECT_EQ(value_at(state.document(), "panels.s1.show"), UiValue(false));
  EXPECT_TRUE(value_at(state.document(), "panels.s1.title").is_null());
  EXPECT_EQ(value_at(state.document(), "panels.s2.title"), UiValue("Charlie"));

  // A new panel takes the lowest free slot, whatever its place in the list.
  snapshot.panels = {panel("d", "Delta"), panel("a", "Alpha"), panel("c", "Charlie")};
  state.update(snapshot);
  EXPECT_EQ(state.panel_key(0), "a");
  EXPECT_EQ(state.panel_key(1), "d");
  EXPECT_EQ(state.panel_key(2), "c");

  // A new order of the same panels moves nothing and sends nothing.
  snapshot.panels = {panel("c", "Charlie"), panel("d", "Delta"), panel("a", "Alpha")};
  EXPECT_TRUE(state.update(snapshot).empty());
  EXPECT_EQ(state.panel_key(0), "a");
  EXPECT_EQ(state.panel_key(2), "c");

  // Two new panels at once take the two lowest free slots, in list order.
  snapshot.panels = {panel("e", "Echo"), panel("a", "Alpha"), panel("f", "Foxtrot"),
    panel("c", "Charlie"), panel("d", "Delta")};
  state.update(snapshot);
  EXPECT_EQ(state.panel_key(3), "e");
  EXPECT_EQ(state.panel_key(4), "f");

  // A panel that comes back is a new panel and takes the lowest free slot, not its old one.
  snapshot.panels = {panel("a", "Alpha"), panel("c", "Charlie"), panel("d", "Delta")};
  state.update(snapshot);
  EXPECT_EQ(state.panel_key(3), "");
  EXPECT_EQ(state.panel_key(4), "");
  snapshot.panels.push_back(panel("f", "Foxtrot"));
  state.update(snapshot);
  EXPECT_EQ(state.panel_key(3), "f");

  // The same holds for the tools. t1 leaves and t3 arrives in one update: t3 does not take the
  // slot that t1 has just left (FreedSlotStaysEmptyForOneUpdate), it takes the next free one.
  snapshot.tools = {tool("t2", "Two"), tool("t3", "Three")};
  state.update(snapshot);
  EXPECT_EQ(state.tool_panel_id(0), "") << "t1 left, so its slot stays empty for this update";
  EXPECT_EQ(state.tool_panel_id(1), "t2");
  EXPECT_EQ(state.tool_panel_id(2), "t3");
  snapshot.tools.push_back(tool("t4", "Four"));
  state.update(snapshot);
  EXPECT_EQ(state.tool_panel_id(0), "t4") << "the slot that t1 left is free from the next update";

  // An entry without a key has no slot, and a key listed twice has one.
  ArControlPanelState odd;
  ControlPanelSnapshot odd_snapshot = live_snapshot();
  odd_snapshot.panels = {panel("", "Nameless"), panel("x", "First"), panel("x", "Second")};
  odd.update(odd_snapshot);
  EXPECT_EQ(odd.panel_key(0), "x");
  EXPECT_EQ(odd.panel_key(1), "");
  EXPECT_EQ(odd.panel_title(0), "First");
  EXPECT_EQ(value_at(odd.document(), "more.panels_show"), UiValue(false));

  // Out-of-range slots read as free.
  EXPECT_EQ(state.panel_key(6), "");
  EXPECT_EQ(state.panel_key(1000), "");
  EXPECT_EQ(state.tool_panel_id(4), "");
  EXPECT_EQ(state.panel_title(6), "");
  EXPECT_EQ(state.tool_title(4), "");
}

TEST(ArControlPanelState, FreedSlotStaysEmptyForOneUpdate)
{
  ArControlPanelState state;
  ControlPanelSnapshot snapshot = live_snapshot();
  snapshot.panels = {panel("a", "Alpha"), panel("b", "Bravo"), panel("c", "Charlie")};
  snapshot.tools = {tool("t1", "One"), tool("t2", "Two")};
  state.update(snapshot);

  // b and t1 leave while d and t3 arrive, in one update. A tap that the page sent for panel
  // slot 1 or tool slot 0 before it learned of this must not act on a newcomer, so the
  // newcomers take the lowest slots that were free before the update, not the slots just left.
  snapshot.panels = {panel("a", "Alpha"), panel("c", "Charlie"), panel("d", "Delta")};
  snapshot.tools = {tool("t2", "Two"), tool("t3", "Three")};
  const std::vector<PatchEntry> changed = state.update(snapshot);
  EXPECT_EQ(state.panel_key(1), "");
  EXPECT_EQ(state.panel_title(1), "");
  EXPECT_EQ(state.panel_key(3), "d");
  EXPECT_EQ(state.tool_panel_id(0), "");
  EXPECT_EQ(state.tool_title(0), "");
  EXPECT_EQ(state.tool_panel_id(2), "t3");
  EXPECT_EQ(
    sorted(paths_of(changed)),
    (std::vector<std::string>{
      "panels.s1.show", "panels.s1.title", "panels.s3.show", "panels.s3.title",
      "tools.s0.show", "tools.s0.title", "tools.s2.show", "tools.s2.title"}));
  EXPECT_EQ(value_at(state.document(), "panels.s1.show"), UiValue(false));
  EXPECT_EQ(value_at(state.document(), "tools.s0.show"), UiValue(false));

  // The same list again changes nothing: the freed slots are free now, and nobody waits for them.
  EXPECT_TRUE(state.update(snapshot).empty());
  EXPECT_EQ(state.panel_key(1), "");
  EXPECT_EQ(state.tool_panel_id(0), "");

  // The next newcomers take them, the lowest first.
  snapshot.panels.push_back(panel("e", "Echo"));
  snapshot.tools.push_back(tool("t4", "Four"));
  const std::vector<PatchEntry> filled = state.update(snapshot);
  EXPECT_EQ(state.panel_key(1), "e");
  EXPECT_EQ(state.tool_panel_id(0), "t4");
  EXPECT_EQ(
    sorted(paths_of(filled)),
    (std::vector<std::string>{
      "panels.s1.show", "panels.s1.title", "tools.s0.show", "tools.s0.title"}));

  // Every slot taken: the slot that a panel leaves is not given to the newcomer of the same
  // update. The newcomer is counted in the "more" line until the next update places it.
  ArControlPanelState full;
  ControlPanelSnapshot crowd = live_snapshot();
  for (std::size_t i = 0; i < ac::kPanelSlots; ++i) {
    crowd.panels.push_back(panel("p" + std::to_string(i), "Panel " + std::to_string(i)));
  }
  full.update(crowd);
  crowd.panels.erase(crowd.panels.begin() + 2);
  crowd.panels.push_back(panel("p6", "Panel 6"));
  full.update(crowd);
  EXPECT_EQ(full.panel_key(2), "");
  EXPECT_EQ(value_at(full.document(), "panels.s2.show"), UiValue(false));
  EXPECT_EQ(value_at(full.document(), "more.panels"), UiValue("1 more on the desktop"));
  EXPECT_EQ(value_at(full.document(), "more.panels_show"), UiValue(true));
  full.update(crowd);
  EXPECT_EQ(full.panel_key(2), "p6");
  EXPECT_EQ(value_at(full.document(), "panels.s2.show"), UiValue(true));
  EXPECT_EQ(value_at(full.document(), "panels.s2.title"), UiValue("Panel 6"));
  EXPECT_EQ(value_at(full.document(), "more.panels_show"), UiValue(false));
}

TEST(ArControlPanelState, OverflowIsCounted)
{
  ControlPanelSnapshot snapshot = live_snapshot();
  for (int i = 0; i < 8; ++i) {
    snapshot.panels.push_back(panel("p" + std::to_string(i), "Panel " + std::to_string(i)));
  }
  for (int i = 0; i < 6; ++i) {
    snapshot.tools.push_back(tool("t" + std::to_string(i), "Tool " + std::to_string(i)));
  }
  ArControlPanelState state;
  state.update(snapshot);
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    EXPECT_EQ(state.panel_key(slot), "p" + std::to_string(slot));
    EXPECT_EQ(value_at(state.document(), ac::panel_path(slot, "show")), UiValue(true));
  }
  for (std::size_t slot = 0; slot < ac::kToolSlots; ++slot) {
    EXPECT_EQ(state.tool_panel_id(slot), "t" + std::to_string(slot));
  }
  EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue("2 more on the desktop"));
  EXPECT_EQ(value_at(state.document(), "more.panels_show"), UiValue(true));
  EXPECT_EQ(value_at(state.document(), "more.tools"), UiValue("2 more on the desktop"));
  EXPECT_EQ(value_at(state.document(), "more.tools_show"), UiValue(true));

  // Exactly as many as there are slots: no overflow.
  snapshot.panels.resize(6);
  snapshot.tools.resize(4);
  const std::vector<PatchEntry> fits = state.update(snapshot);
  EXPECT_EQ(
    sorted(paths_of(fits)),
    (std::vector<std::string>{"more.panels", "more.panels_show", "more.tools", "more.tools_show"}));
  EXPECT_EQ(value_at(state.document(), "more.panels_show"), UiValue(false));
  EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue(""));

  // One more than the slots: the count is 1.
  snapshot.panels.push_back(panel("p6", "Panel 6"));
  state.update(snapshot);
  EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue("1 more on the desktop"));

  // A listed panel that has a slot keeps it, even when it moves behind the others in the list;
  // and when a slot frees up, the first panel without a slot takes it in the next update.
  ControlPanelSnapshot crowded = live_snapshot();
  for (int i = 0; i < 8; ++i) {
    crowded.panels.push_back(panel("q" + std::to_string(i), "Q" + std::to_string(i)));
  }
  ArControlPanelState second;
  second.update(crowded);
  std::reverse(crowded.panels.begin(), crowded.panels.end());
  EXPECT_TRUE(second.update(crowded).empty()) << "the same panels in another order";
  EXPECT_EQ(second.panel_key(0), "q0");
  EXPECT_EQ(second.panel_key(5), "q5");
  crowded.panels.erase(
    std::remove_if(
      crowded.panels.begin(), crowded.panels.end(),
      [](const ControlPanelPanelEntry & e) {return e.key == "q1";}),
    crowded.panels.end());
  second.update(crowded);
  // The slot of q1 stays empty in the update in which q1 left, so q7 and q6 are both still
  // without a slot.
  EXPECT_EQ(second.panel_key(1), "");
  EXPECT_EQ(value_at(second.document(), "more.panels"), UiValue("2 more on the desktop"));
  second.update(crowded);
  // Reversed list: q7 is the first panel without a slot, so it takes the freed slot 1.
  EXPECT_EQ(second.panel_key(1), "q7");
  EXPECT_EQ(value_at(second.document(), "more.panels"), UiValue("1 more on the desktop"));
}

TEST(ArControlPanelState, OnFollowsArState)
{
  struct Case
  {
    PanelArState state;
    bool on;
    const char * word;
  };
  for (const Case & c : {Case{PanelArState::Off, false, "off"},
      Case{PanelArState::Starting, true, "starting"},
      Case{PanelArState::Streaming, true, "streaming"},
      Case{PanelArState::Hidden, false, "hidden"},
      Case{PanelArState::ClosedInAr, false, "closed_in_ar"}})
  {
    EXPECT_EQ(qar_rviz_streaming::panel_state_is_on(c.state), c.on) << c.word;
    EXPECT_STREQ(qar_rviz_streaming::to_string(c.state), c.word);
    ControlPanelSnapshot snapshot = live_snapshot();
    snapshot.panels = {panel("k", "Panel", c.state)};
    ArControlPanelState state;
    state.update(snapshot);
    EXPECT_EQ(value_at(state.document(), "panels.s0.on"), UiValue(c.on)) << c.word;
    // The slot is shown whatever the state: a hidden panel is a switch that reads off.
    EXPECT_EQ(value_at(state.document(), "panels.s0.show"), UiValue(true)) << c.word;
  }

  // The switch follows the state through updates, and only its path is sent.
  ControlPanelSnapshot snapshot = live_snapshot();
  snapshot.panels = {panel("k", "Panel", PanelArState::Starting)};
  ArControlPanelState state;
  state.update(snapshot);
  snapshot.panels[0].state = PanelArState::Streaming;
  // Starting and streaming read the same switch; only the mark of the title goes.
  EXPECT_EQ(paths_of(state.update(snapshot)), (std::vector<std::string>{"panels.s0.title"}));
  snapshot.panels[0].state = PanelArState::Hidden;
  const std::vector<PatchEntry> hidden = state.update(snapshot);
  ASSERT_EQ(hidden.size(), 1u);
  EXPECT_EQ(hidden[0].path, "panels.s0.on");
  EXPECT_EQ(hidden[0].value, UiValue(false));
}

TEST(ArControlPanelState, ToolOnFollowsTheSnapshot)
{
  // A tool slot carries its switch as a panel slot does: show, title and on, one path each.
  ControlPanelSnapshot snapshot = live_snapshot();
  snapshot.tools = {tool("t1", "Speed", true), tool("t2", "Pose", true)};
  snapshot.tools[0].on = true;
  ArControlPanelState state;
  state.update(snapshot);
  EXPECT_EQ(value_at(state.document(), "tools.s0.show"), UiValue(true));
  EXPECT_EQ(value_at(state.document(), "tools.s0.on"), UiValue(true));
  EXPECT_EQ(value_at(state.document(), "tools.s1.show"), UiValue(true));
  EXPECT_EQ(value_at(state.document(), "tools.s1.on"), UiValue(false));
  EXPECT_EQ(value_at(state.document(), "tools.s2.show"), UiValue(false));
  EXPECT_EQ(value_at(state.document(), "tools.s2.on"), UiValue(false)) << "a free slot reads off";

  // Off: only the switch moves. The tool stays listed, and so does its slot.
  snapshot.tools[0].on = false;
  const std::vector<PatchEntry> off = state.update(snapshot);
  ASSERT_EQ(off.size(), 1u);
  EXPECT_EQ(off[0].path, "tools.s0.on");
  EXPECT_EQ(off[0].value, UiValue(false));
  EXPECT_EQ(state.tool_panel_id(0), "t1");
  EXPECT_EQ(value_at(state.document(), "tools.s0.show"), UiValue(true));
  EXPECT_EQ(value_at(state.document(), "tools.s0.title"), UiValue("Speed"));

  // On again.
  snapshot.tools[0].on = true;
  const std::vector<PatchEntry> on = state.update(snapshot);
  ASSERT_EQ(on.size(), 1u);
  EXPECT_EQ(on[0].path, "tools.s0.on");
  EXPECT_EQ(on[0].value, UiValue(true));

  // A tool that has no AR panel yet may still read on (the host asked for it): the title says
  // pending, the switch says what the host says.
  snapshot.tools[1].in_ar = false;
  snapshot.tools[1].on = true;
  EXPECT_EQ(
    sorted(paths_of(state.update(snapshot))),
    (std::vector<std::string>{"tools.s1.on", "tools.s1.title"}));
  EXPECT_EQ(value_at(state.document(), "tools.s1.title"), UiValue("Pose (pending)"));

  // The tool that leaves takes its switch down with its slot.
  snapshot.tools.erase(snapshot.tools.begin());
  EXPECT_EQ(
    sorted(paths_of(state.update(snapshot))),
    (std::vector<std::string>{"tools.s0.on", "tools.s0.show", "tools.s0.title"}));
  EXPECT_EQ(value_at(state.document(), "tools.s0.on"), UiValue(false));
  EXPECT_EQ(state.tool_panel_id(0), "");
  EXPECT_EQ(state.tool_panel_id(1), "t2");
}

TEST(ArControlPanelState, NoLiveSessionListsNothing)
{
  // Without a QarOS session the switches have nothing to act on: the page lists no panel and no
  // tool, whatever the desktop holds, and says so in the "more panels" line.
  EXPECT_STREQ(ac::kLineNoLiveSession, kNoSessionLine);
  for (const ConnectionState connection : {ConnectionState::Offline, ConnectionState::Preview}) {
    ControlPanelSnapshot snapshot;
    snapshot.connection = connection;
    snapshot.panels = {panel("a", "Alpha", PanelArState::Streaming), panel("b", "Bravo"),
      panel("c", "Charlie", PanelArState::Hidden)};
    snapshot.tools = {tool("t1", "One", true), tool("t2", "Two", false)};
    snapshot.tools[0].on = true;
    const std::string word = qar_rviz_streaming::to_string(connection);

    ArControlPanelState state;
    const std::vector<PatchEntry> first = state.update(snapshot);
    EXPECT_EQ(first.size(), 39u) << word;
    EXPECT_EQ(value_at(state.document(), "status.connection"), UiValue(word));
    for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
      EXPECT_EQ(value_at(state.document(), ac::panel_path(slot, "show")), UiValue(false)) << word;
      EXPECT_TRUE(value_at(state.document(), ac::panel_path(slot, "title")).is_null()) << word;
      EXPECT_EQ(value_at(state.document(), ac::panel_path(slot, "on")), UiValue(false)) << word;
      EXPECT_EQ(state.panel_key(slot), "") << word;
      EXPECT_EQ(state.panel_title(slot), "") << word;
    }
    for (std::size_t slot = 0; slot < ac::kToolSlots; ++slot) {
      EXPECT_EQ(value_at(state.document(), ac::tool_path(slot, "show")), UiValue(false)) << word;
      EXPECT_TRUE(value_at(state.document(), ac::tool_path(slot, "title")).is_null()) << word;
      EXPECT_EQ(value_at(state.document(), ac::tool_path(slot, "on")), UiValue(false)) << word;
      EXPECT_EQ(state.tool_panel_id(slot), "") << word;
      EXPECT_EQ(state.tool_title(slot), "") << word;
    }
    EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue(kNoSessionLine)) << word;
    EXPECT_EQ(value_at(state.document(), "more.panels_show"), UiValue(true)) << word;
    EXPECT_EQ(value_at(state.document(), "more.tools"), UiValue("")) << word;
    EXPECT_EQ(value_at(state.document(), "more.tools_show"), UiValue(false)) << word;

    // Nothing the desktop does changes the page while there is no session, even a list that is
    // longer than the slots: the line is not an overflow count.
    for (int i = 0; i < 8; ++i) {
      snapshot.panels.push_back(panel("p" + std::to_string(i), "Panel"));
      snapshot.tools.push_back(tool("q" + std::to_string(i), "Tool"));
    }
    snapshot.panels[0].state = PanelArState::Off;
    EXPECT_TRUE(state.update(snapshot).empty()) << word;
    EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue(kNoSessionLine)) << word;
    EXPECT_EQ(value_at(state.document(), "more.tools_show"), UiValue(false)) << word;
  }
}

TEST(ArControlPanelState, ConnectingAndPausedStillListTheDesktop)
{
  // Only an offline desktop and a preview have no switches. A session that is coming up or
  // paused lists the panels, and the host refuses a tap that it cannot carry out.
  for (const ConnectionState connection :
    {ConnectionState::Connecting, ConnectionState::Live, ConnectionState::Paused})
  {
    ControlPanelSnapshot snapshot;
    snapshot.connection = connection;
    snapshot.panels = {panel("a", "Alpha", PanelArState::Streaming), panel("b", "Bravo")};
    snapshot.tools = {tool("t1", "One", true)};
    ArControlPanelState state;
    state.update(snapshot);
    const std::string word = qar_rviz_streaming::to_string(connection);
    EXPECT_EQ(state.panel_key(0), "a") << word;
    EXPECT_EQ(state.panel_key(1), "b") << word;
    EXPECT_EQ(state.tool_panel_id(0), "t1") << word;
    EXPECT_EQ(value_at(state.document(), "panels.s0.show"), UiValue(true)) << word;
    EXPECT_EQ(value_at(state.document(), "tools.s0.show"), UiValue(true)) << word;
    EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue("")) << word;
    EXPECT_EQ(value_at(state.document(), "more.panels_show"), UiValue(false)) << word;
  }
}

TEST(ArControlPanelState, BackToLiveTheSlotsReturn)
{
  ControlPanelSnapshot snapshot = live_snapshot();
  snapshot.panels = {panel("a", "Alpha", PanelArState::Streaming),
    panel("b", "Bravo", PanelArState::Hidden), panel("c", "Charlie")};
  snapshot.tools = {tool("t1", "One", true), tool("t2", "Two", false)};
  snapshot.tools[0].on = true;
  ArControlPanelState state;
  state.update(snapshot);
  const UiValue live_document = state.document();
  EXPECT_EQ(value_at(live_document, "panels.s0.title"), UiValue("Alpha"));
  EXPECT_EQ(value_at(live_document, "tools.s0.on"), UiValue(true));
  EXPECT_EQ(value_at(live_document, "more.panels_show"), UiValue(false));

  // The session ends. Every slot that showed something is hidden and emptied, the line appears,
  // and the connection word moves: 15 paths.
  snapshot.connection = ConnectionState::Preview;
  const std::vector<PatchEntry> gone = state.update(snapshot);
  EXPECT_EQ(
    sorted(paths_of(gone)),
    (std::vector<std::string>{
      "more.panels", "more.panels_show", "panels.s0.on", "panels.s0.show", "panels.s0.title",
      "panels.s1.show", "panels.s1.title", "panels.s2.show", "panels.s2.title", "status.connection",
      "tools.s0.on", "tools.s0.show", "tools.s0.title", "tools.s1.show", "tools.s1.title"}));
  EXPECT_EQ(state.panel_key(0), "");
  EXPECT_EQ(state.tool_panel_id(1), "");
  EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue(kNoSessionLine));
  const UiValue preview_document = state.document();

  // The session returns: the entries take their slots again, in the order of the lists, and the
  // line goes. The page ends up with the document it had.
  snapshot.connection = ConnectionState::Live;
  const std::vector<PatchEntry> back = state.update(snapshot);
  EXPECT_EQ(back.size(), gone.size());
  EXPECT_EQ(state.panel_key(0), "a");
  EXPECT_EQ(state.panel_key(1), "b");
  EXPECT_EQ(state.panel_key(2), "c");
  EXPECT_EQ(state.tool_panel_id(0), "t1");
  EXPECT_EQ(state.tool_panel_id(1), "t2");
  EXPECT_EQ(state.document(), live_document);
  EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue(""));
  EXPECT_EQ(value_at(state.document(), "more.panels_show"), UiValue(false));

  // A page that applies the two patches in turn is where the host's documents were.
  UiValue page = live_document;
  qar_ui_model::DataBody body;
  std::string error;
  body.patch = gone;
  ASSERT_TRUE(qar_ui_model::apply_patch(page, body, error)) << error;
  EXPECT_EQ(page, preview_document);
  body.patch = back;
  ASSERT_TRUE(qar_ui_model::apply_patch(page, body, error)) << error;
  EXPECT_EQ(page, live_document);
}

TEST(ArControlPanelState, TitlesMarkStartingAndClosedInAr)
{
  using qar_rviz_streaming::panel_slot_title;
  using qar_rviz_streaming::tool_slot_title;
  EXPECT_EQ(panel_slot_title(panel("k", "Views")), "Views");
  EXPECT_EQ(panel_slot_title(panel("k", "Views", PanelArState::Streaming)), "Views");
  EXPECT_EQ(panel_slot_title(panel("k", "Views", PanelArState::Hidden)), "Views");
  EXPECT_EQ(panel_slot_title(panel("k", "Views", PanelArState::Starting)), "Views (starting)");
  EXPECT_EQ(
    panel_slot_title(panel("k", "Views", PanelArState::ClosedInAr)), "Views (closed in AR)");
  EXPECT_EQ(panel_slot_title(panel("dock:Views", "")), "dock:Views") << "no name: the key";

  // A tool row is a switch, so its caption is the name of the tool; only a tool that has no AR
  // panel yet is marked.
  EXPECT_EQ(tool_slot_title(tool("t", "Speed", true)), "Speed");
  EXPECT_EQ(tool_slot_title(tool("t", "Speed", false)), "Speed (pending)");
  EXPECT_EQ(tool_slot_title(tool("tool.a", "", true)), "tool.a");

  // The page gets the marked title; the texts of answers get the plain name.
  ControlPanelSnapshot snapshot = live_snapshot();
  snapshot.panels = {panel("a", "Views", PanelArState::Starting),
    panel("b", "Displays (AR copy)", PanelArState::ClosedInAr)};
  snapshot.tools = {tool("t1", "Speed", true), tool("t2", "Pose", false)};
  ArControlPanelState state;
  state.update(snapshot);
  EXPECT_EQ(value_at(state.document(), "panels.s0.title"), UiValue("Views (starting)"));
  EXPECT_EQ(
    value_at(state.document(), "panels.s1.title"), UiValue("Displays (AR copy) (closed in AR)"));
  EXPECT_EQ(state.panel_title(0), "Views");
  EXPECT_EQ(state.panel_title(1), "Displays (AR copy)");
  EXPECT_EQ(value_at(state.document(), "tools.s0.title"), UiValue("Speed"));
  EXPECT_EQ(value_at(state.document(), "tools.s1.title"), UiValue("Pose (pending)"));
  EXPECT_EQ(state.tool_title(0), "Speed");
  EXPECT_EQ(state.tool_title(1), "Pose");

  // The mark follows the state: starting becomes streaming, the title loses its mark.
  snapshot.panels[0].state = PanelArState::Streaming;
  const std::vector<PatchEntry> patch = state.update(snapshot);
  ASSERT_EQ(patch.size(), 1u);
  EXPECT_EQ(patch[0].path, "panels.s0.title");
  EXPECT_EQ(patch[0].value, UiValue("Views"));
}

TEST(ArControlPanelState, ConnectionWords)
{
  struct Case
  {
    ConnectionState state;
    const char * word;
  };
  for (const Case & c : {Case{ConnectionState::Offline, "offline"},
      Case{ConnectionState::Connecting, "connecting"}, Case{ConnectionState::Live, "live"},
      Case{ConnectionState::Paused, "paused"}, Case{ConnectionState::Preview, "preview"}})
  {
    EXPECT_STREQ(qar_rviz_streaming::to_string(c.state), c.word);
    ControlPanelSnapshot snapshot;
    snapshot.connection = c.state;
    ArControlPanelState state;
    state.update(snapshot);
    EXPECT_EQ(value_at(state.document(), "status.connection"), UiValue(c.word));
  }
  // The page's light has a rule for each word, and for no other.
  const qar_ui_model::UiModel model = qar_rviz_streaming::make_ar_control_page_model();
  const qar_ui_model::Widget * light = model.find(ac::kIdConnection);
  ASSERT_NE(light, nullptr);
  std::set<std::string> words;
  for (const qar_ui_model::StatusRule & rule : light->rules) {
    words.insert(rule.equals.as_string());
  }
  EXPECT_EQ(words, (std::set<std::string>{"live", "connecting", "paused", "offline", "preview"}));

  // The peers and the error.
  ControlPanelSnapshot snapshot;
  snapshot.peers = 3;
  snapshot.last_error = "Invite peer: not connected to QarOS.";
  ArControlPanelState state;
  state.update(snapshot);
  EXPECT_EQ(value_at(state.document(), "status.peers"), UiValue(3));
  EXPECT_EQ(
    value_at(state.document(), "status.error"), UiValue("Invite peer: not connected to QarOS."));
}

TEST(ArControlPanelState, ErrorTextIsCutAt200Bytes)
{
  ControlPanelSnapshot snapshot;
  snapshot.last_error = std::string(200, 'e');
  ArControlPanelState state;
  state.update(snapshot);
  EXPECT_EQ(value_at(state.document(), "status.error").as_string(), std::string(200, 'e'));

  snapshot.last_error = std::string(201, 'e');
  state.update(snapshot);
  const std::string cut = value_at(state.document(), "status.error").as_string();
  EXPECT_EQ(cut.size(), 200u);
  EXPECT_EQ(cut.substr(197), "...");
  EXPECT_EQ(cut.substr(0, 197), std::string(197, 'e'));

  // A multi-byte letter is not split by the cut: 196 ASCII bytes, then a three-byte sequence
  // that would end at byte 199.
  snapshot.last_error = std::string(196, 'e') + "\xe2\x80\x94" + std::string(50, 'e');
  state.update(snapshot);
  const std::string utf8_cut = value_at(state.document(), "status.error").as_string();
  EXPECT_LE(utf8_cut.size(), 200u);
  EXPECT_EQ(utf8_cut.substr(utf8_cut.size() - 3), "...");
  EXPECT_EQ(utf8_cut.substr(0, 196), std::string(196, 'e'));
  EXPECT_EQ(utf8_cut.find('\xe2'), std::string::npos) << "the sequence was dropped whole";
}

TEST(ArControlPanelState, PickCaptionRules)
{
  ControlPanelSnapshot snapshot;
  EXPECT_EQ(caption_of(snapshot), "Choose Point or Nav goal, then aim in the scene");
  EXPECT_STREQ(ac::kCaptionPickOff, "Choose Point or Nav goal, then aim in the scene");

  snapshot.pick_mode = ArPickMode::Point;
  EXPECT_EQ(caption_of(snapshot), "Point: aim and tap in the scene");
  snapshot.pick_mode = ArPickMode::NavGoal;
  EXPECT_EQ(caption_of(snapshot), "Nav goal: press, drag to set the heading, release");

  // A pick that waits for Confirm.
  snapshot.pick_mode = ArPickMode::NavGoal;
  snapshot.pick_pending = true;
  snapshot.pick_summary = "Nav goal @ x=1.20 y=3.40 z=0.00, yaw 90 deg (ground plane)";
  EXPECT_EQ(
    caption_of(snapshot),
    "Waiting: Nav goal @ x=1.20 y=3.40 z=0.00, yaw 90 deg (ground plane) - Confirm or Cancel");
  snapshot.pick_summary.clear();
  EXPECT_EQ(caption_of(snapshot), "Waiting: pick - Confirm or Cancel");
  snapshot.pick_pending = false;
  snapshot.pick_summary = "left over";

  // A pick that was refused.
  snapshot.pick_error = "the runtime found no intersection";
  EXPECT_EQ(caption_of(snapshot), "Refused: the runtime found no intersection");
  snapshot.pick_mode = ArPickMode::Point;
  EXPECT_EQ(caption_of(snapshot), "Refused: the runtime found no intersection");
  // A pick that waits comes before an error.
  snapshot.pick_pending = true;
  snapshot.pick_summary = "Point @ x=1.00 y=2.00 z=0.00";
  EXPECT_EQ(caption_of(snapshot), "Waiting: Point @ x=1.00 y=2.00 z=0.00 - Confirm or Cancel");
  snapshot.pick_pending = false;

  // Nothing is armed: no pick and no error show, whatever the controller still holds.
  snapshot.pick_mode = ArPickMode::Off;
  snapshot.pick_pending = true;
  EXPECT_EQ(caption_of(snapshot), "Choose Point or Nav goal, then aim in the scene");

  // What the desktop says is cut to 200 bytes.
  ControlPanelSnapshot long_error;
  long_error.pick_mode = ArPickMode::Point;
  long_error.pick_error = std::string(300, 'x');
  const std::string refused = caption_of(long_error);
  EXPECT_EQ(refused, "Refused: " + std::string(197, 'x') + "...");

  // The mode word of the document follows the snapshot.
  ControlPanelSnapshot armed;
  armed.pick_mode = ArPickMode::NavGoal;
  ArControlPanelState state;
  state.update(armed);
  EXPECT_EQ(value_at(state.document(), "pick.mode"), UiValue("nav_goal"));
  EXPECT_EQ(
    value_at(state.document(), "pick.caption"),
    UiValue("Nav goal: press, drag to set the heading, release"));
  armed.pick_mode = ArPickMode::Point;
  const std::vector<PatchEntry> patch = state.update(armed);
  EXPECT_EQ(sorted(paths_of(patch)), (std::vector<std::string>{"pick.caption", "pick.mode"}));
  EXPECT_EQ(*find_entry(patch, "pick.mode"), UiValue("point"));
}

TEST(ArControlPanelState, AutoConfirmIsShown)
{
  const std::string suffix = " - publishes on release (desktop setting)";
  ControlPanelSnapshot snapshot;
  snapshot.pick_auto_confirm = true;
  // Nothing armed: nothing is published, so nothing is promised.
  EXPECT_EQ(caption_of(snapshot), "Choose Point or Nav goal, then aim in the scene");

  snapshot.pick_mode = ArPickMode::Point;
  EXPECT_EQ(caption_of(snapshot), "Point: aim and tap in the scene" + suffix);
  snapshot.pick_mode = ArPickMode::NavGoal;
  EXPECT_EQ(caption_of(snapshot), "Nav goal: press, drag to set the heading, release" + suffix);

  snapshot.pick_pending = true;
  snapshot.pick_summary = "Nav goal @ x=1.20 y=3.40 z=0.00, yaw 90 deg (ground plane)";
  EXPECT_EQ(
    caption_of(snapshot),
    "Waiting: Nav goal @ x=1.20 y=3.40 z=0.00, yaw 90 deg (ground plane) - Confirm or Cancel" +
    suffix);
  snapshot.pick_pending = false;
  snapshot.pick_error = "no intersection";
  EXPECT_EQ(caption_of(snapshot), "Refused: no intersection" + suffix);

  // The page cannot turn the setting off: it is part of the snapshot only, and the caption
  // is the only place the page shows it.
  snapshot.pick_error.clear();
  snapshot.pick_auto_confirm = false;
  EXPECT_EQ(caption_of(snapshot), "Nav goal: press, drag to set the heading, release");

  // It reaches the document as a caption change and nothing else.
  ArControlPanelState state;
  state.update(snapshot);
  snapshot.pick_auto_confirm = true;
  const std::vector<PatchEntry> patch = state.update(snapshot);
  ASSERT_EQ(patch.size(), 1u);
  EXPECT_EQ(patch[0].path, "pick.caption");
  EXPECT_EQ(
    patch[0].value, UiValue("Nav goal: press, drag to set the heading, release" + suffix));
}

TEST(ArControlPanelState, OtherModeIsShown)
{
  ControlPanelSnapshot snapshot;
  snapshot.pick_mode_other = true;
  snapshot.pick_mode_other_name = "initial pose";
  EXPECT_EQ(caption_of(snapshot), "Armed on the desktop: initial pose");
  snapshot.pick_mode_other_name.clear();
  EXPECT_EQ(caption_of(snapshot), "Armed on the desktop: another mode");
  snapshot.pick_mode_other_name = "area";

  ArControlPanelState state;
  state.update(snapshot);
  // The page's choice reads "other", a value none of its options has.
  EXPECT_EQ(value_at(state.document(), "pick.mode"), UiValue("other"));
  EXPECT_EQ(value_at(state.document(), "pick.caption"), UiValue("Armed on the desktop: area"));

  // The AR user arms one of the three: the document moves on.
  snapshot.pick_mode_other = false;
  snapshot.pick_mode = ArPickMode::Point;
  const std::vector<PatchEntry> patch = state.update(snapshot);
  EXPECT_EQ(sorted(paths_of(patch)), (std::vector<std::string>{"pick.caption", "pick.mode"}));
  EXPECT_EQ(value_at(state.document(), "pick.mode"), UiValue("point"));

  // A waiting pick of the desktop's mode is shown, and so is a refusal.
  snapshot.pick_mode = ArPickMode::Off;
  snapshot.pick_mode_other = true;
  snapshot.pick_pending = true;
  snapshot.pick_summary = "Initial pose @ x=0.00 y=0.00 z=0.00, yaw 0 deg";
  EXPECT_EQ(
    caption_of(snapshot),
    "Waiting: Initial pose @ x=0.00 y=0.00 z=0.00, yaw 0 deg - Confirm or Cancel");
  snapshot.pick_pending = false;
  snapshot.pick_error = "refused";
  EXPECT_EQ(caption_of(snapshot), "Refused: refused");
  snapshot.pick_error.clear();
  snapshot.pick_auto_confirm = true;
  EXPECT_EQ(
    caption_of(snapshot), "Armed on the desktop: area - publishes on release (desktop setting)");
}

TEST(ArControlPanelState, TheStandardDisplaysDockHasNoSwitch)
{
  EXPECT_STREQ(ac::kStandardDisplaysKey, "dock:Displays");

  // The desktop's list as RViz fills it: this panel, the docks (the standard Displays dock among
  // them), two video panels and, last, the AR copy of the Displays panel. Eight entries, one of
  // them the standard Displays dock: seven are listed, six take the slots and one is "more".
  ControlPanelSnapshot snapshot = live_snapshot();
  snapshot.panels = {
    panel("qar_streaming_panel", "This QAR panel", PanelArState::Streaming),
    panel("dock:Displays", "Displays", PanelArState::Streaming),
    panel("dock:Views", "Views"),
    panel("dock:Tool Properties", "Tool Properties"),
    panel("dock:Time", "Time"),
    panel("video:/a", "Video: /a"),
    panel("video:/b", "Video: /b"),
    panel("hidden:Displays", "Displays (AR copy)", PanelArState::Streaming)};
  ArControlPanelState state;
  state.update(snapshot);

  const std::vector<std::string> expected_keys = {
    "qar_streaming_panel", "dock:Views", "dock:Tool Properties", "dock:Time", "video:/a",
    "video:/b"};
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    EXPECT_EQ(state.panel_key(slot), expected_keys[slot]) << "slot " << slot;
    EXPECT_NE(state.panel_title(slot), "Displays") << "slot " << slot;
    EXPECT_NE(value_at(state.document(), ac::panel_path(slot, "title")), UiValue("Displays"))
      << "slot " << slot;
  }
  EXPECT_EQ(value_at(state.document(), "panels.s1.title"), UiValue("Views"))
    << "the next entry takes the place the Displays dock would have had";
  EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue("1 more on the desktop"))
    << "the Displays dock is not counted";
  EXPECT_EQ(value_at(state.document(), "more.panels_show"), UiValue(true));

  // A slot that falls free goes to the next listed entry, the AR copy of the Displays panel,
  // never to the Displays dock.
  snapshot.panels.erase(snapshot.panels.begin() + 2);  // dock:Views leaves
  state.update(snapshot);  // its slot stays empty for one update
  EXPECT_TRUE(state.panel_key(1).empty());
  state.update(snapshot);
  EXPECT_EQ(state.panel_key(1), "hidden:Displays");
  EXPECT_EQ(value_at(state.document(), "panels.s1.title"), UiValue("Displays (AR copy)"));
  EXPECT_EQ(value_at(state.document(), "panels.s1.on"), UiValue(true));
  EXPECT_EQ(value_at(state.document(), "more.panels"), UiValue(""));
  EXPECT_EQ(value_at(state.document(), "more.panels_show"), UiValue(false));

  // The Displays dock alone: no slot is shown and nothing is counted.
  ControlPanelSnapshot only_displays = live_snapshot();
  only_displays.panels = {panel("dock:Displays", "Displays", PanelArState::Streaming)};
  ArControlPanelState alone;
  alone.update(only_displays);
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    EXPECT_TRUE(alone.panel_key(slot).empty()) << "slot " << slot;
    EXPECT_EQ(value_at(alone.document(), ac::panel_path(slot, "show")), UiValue(false))
      << "slot " << slot;
  }
  EXPECT_EQ(value_at(alone.document(), "more.panels_show"), UiValue(false));
}
