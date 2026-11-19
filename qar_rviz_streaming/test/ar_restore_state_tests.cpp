// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The AR state kept in the RViz configuration: reading and writing the record as text, the
// checks of each field, what is started after the first connect, the rule for the copy of
// Displays, and the copy between the configuration tree and the text. Standard library only.

#include "qar_rviz_streaming/ar_restore_state.hpp"

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::ArCopySize;
using qar_rviz_streaming::ArPanelState;
using qar_rviz_streaming::ArRestorePlan;
using qar_rviz_streaming::ArStateConfigIo;
using qar_rviz_streaming::ArStateText;
using qar_rviz_streaming::ArToolState;
using qar_rviz_streaming::ConfigNode;
using qar_rviz_streaming::ConfigRecord;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::WatchedTopicState;

ArRestorePlan sample_plan()
{
  ArRestorePlan plan;
  plan.panels.push_back({"dock:Views", true, ArCopySize::Native});
  plan.panels.push_back({"qar_streaming_panel", true, std::nullopt});
  plan.panels.push_back({"hidden:Displays", false, std::nullopt});
  plan.panels.push_back({"dock:Time", false, ArCopySize::Enlarged});
  ArToolState twist;
  twist.kind = RosEntityKind::Topic;
  twist.name = "/cmd_vel";
  twist.type = "geometry_msgs/msg/Twist";
  twist.writable = true;
  twist.shown = false;
  plan.tools.push_back(twist);
  ArToolState service;
  service.kind = RosEntityKind::Service;
  service.name = "/qar_demo/trigger";
  service.type = "std_srvs/srv/Trigger";
  plan.tools.push_back(service);
  plan.watched.push_back({"/scan", "sensor_msgs/msg/LaserScan"});
  plan.watched.push_back({"/odom", "nav_msgs/msg/Odometry"});
  return plan;
}

void expect_same_plan(const ArRestorePlan & a, const ArRestorePlan & b)
{
  ASSERT_EQ(a.panels.size(), b.panels.size());
  for (std::size_t i = 0; i < a.panels.size(); ++i) {
    EXPECT_EQ(a.panels[i].key, b.panels[i].key);
    EXPECT_EQ(a.panels[i].on, b.panels[i].on) << a.panels[i].key;
    EXPECT_EQ(a.panels[i].size.has_value(), b.panels[i].size.has_value()) << a.panels[i].key;
    if (a.panels[i].size.has_value() && b.panels[i].size.has_value()) {
      EXPECT_EQ(*a.panels[i].size, *b.panels[i].size) << a.panels[i].key;
    }
  }
  ASSERT_EQ(a.tools.size(), b.tools.size());
  for (std::size_t i = 0; i < a.tools.size(); ++i) {
    EXPECT_EQ(a.tools[i].kind, b.tools[i].kind);
    EXPECT_EQ(a.tools[i].name, b.tools[i].name);
    EXPECT_EQ(a.tools[i].type, b.tools[i].type);
    EXPECT_EQ(a.tools[i].writable, b.tools[i].writable) << a.tools[i].name;
    EXPECT_EQ(a.tools[i].shown, b.tools[i].shown) << a.tools[i].name;
  }
  ASSERT_EQ(a.watched.size(), b.watched.size());
  for (std::size_t i = 0; i < a.watched.size(); ++i) {
    EXPECT_EQ(a.watched[i].name, b.watched[i].name);
    EXPECT_EQ(a.watched[i].type, b.watched[i].type);
  }
}

bool mentions(const std::vector<std::string> & warnings, const std::string & word)
{
  for (const std::string & warning : warnings) {
    if (warning.find(word) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(ArRestoreState, RoundTripsPanelsToolsAndWatchedTopics)
{
  const ArRestorePlan plan = sample_plan();
  const ArStateText text = qar_rviz_streaming::make_ar_state(plan);
  EXPECT_EQ(text.version, "1");
  ASSERT_EQ(text.panels.size(), 4u);
  ASSERT_EQ(text.tools.size(), 2u);
  ASSERT_EQ(text.watched.size(), 2u);
  EXPECT_EQ(text.tools[0].at("Kind"), "topic");
  EXPECT_EQ(text.tools[0].at("Writable"), "true");
  EXPECT_EQ(text.tools[0].at("Shown"), "false");
  EXPECT_EQ(text.tools[1].at("Kind"), "service");

  std::vector<std::string> warnings;
  const ArRestorePlan read = qar_rviz_streaming::parse_ar_state(text, warnings);
  EXPECT_TRUE(warnings.empty()) << (warnings.empty() ? "" : warnings.front());
  expect_same_plan(plan, read);
  EXPECT_FALSE(read.empty());
  EXPECT_TRUE(ArRestorePlan().empty());
}

TEST(ArRestoreState, SizeChoicesRoundTrip)
{
  ArRestorePlan plan;
  plan.panels.push_back({"dock:A", true, ArCopySize::Native});
  plan.panels.push_back({"dock:B", true, ArCopySize::Enlarged});
  plan.panels.push_back({"dock:C", true, std::nullopt});
  const ArStateText text = qar_rviz_streaming::make_ar_state(plan);
  EXPECT_EQ(text.panels[0].at("Size"), "native");
  EXPECT_EQ(text.panels[1].at("Size"), "enlarged");
  EXPECT_EQ(text.panels[2].count("Size"), 0u);  // absent = the global choice

  std::vector<std::string> warnings;
  const ArRestorePlan read = qar_rviz_streaming::parse_ar_state(text, warnings);
  EXPECT_TRUE(warnings.empty());
  ASSERT_EQ(read.panels.size(), 3u);
  ASSERT_TRUE(read.panels[0].size.has_value());
  EXPECT_EQ(*read.panels[0].size, ArCopySize::Native);
  ASSERT_TRUE(read.panels[1].size.has_value());
  EXPECT_EQ(*read.panels[1].size, ArCopySize::Enlarged);
  EXPECT_FALSE(read.panels[2].size.has_value());

  // A size with no On field is a choice for a panel that is off.
  ArStateText only_size;
  only_size.panels.push_back({{"Key", "dock:D"}, {"Size", " Native "}});
  const ArRestorePlan sized = qar_rviz_streaming::parse_ar_state(only_size, warnings);
  EXPECT_TRUE(warnings.empty());
  ASSERT_EQ(sized.panels.size(), 1u);
  EXPECT_FALSE(sized.panels[0].on);
  ASSERT_TRUE(sized.panels[0].size.has_value());
  EXPECT_EQ(*sized.panels[0].size, ArCopySize::Native);
}

TEST(ArRestoreState, ABadRecordIsSkippedWithAWarning)
{
  ArStateText text;
  text.version = "1";
  text.panels = {
    {{"Key", "dock:Views"}, {"On", "true"}},
    {{"On", "true"}},                                   // no Key
    {{"Key", "dock:Time"}, {"On", "maybe"}},            // bad On
    {{"Key", "dock:Tool Properties"}, {"Size", "huge"}},  // bad Size
    {{"Key", "video:/camera"}, {"On", "true"}},         // not an RViz panel
    {{"Key", "dock:Views"}, {"On", "false"}},           // listed before
    {{"Key", "dock:Selection"}, {"On", "1"}}};
  text.tools = {
    {{"Kind", "topic"}, {"Name", "/a"}, {"Type", "std_msgs/msg/Bool"}},
    {{"Kind", "stream"}, {"Name", "/b"}, {"Type", "std_msgs/msg/Bool"}},  // bad Kind
    {{"Kind", "topic"}, {"Name", "/c"}},                                  // no Type
    {{"Kind", "topic"}, {"Name", "/d e"}, {"Type", "std_msgs/msg/Bool"}},  // space in the name
    {{"Kind", "topic"}, {"Name", "/e"}, {"Type", "Bool"}},                // not a type name
    {{"Kind", "topic"}, {"Name", "/f"}, {"Type", "std_msgs/msg/Bool"}, {"Writable", "x"}},
    {{"Kind", "topic"}, {"Name", "/a"}, {"Type", "std_msgs/msg/Bool"}}};  // listed before
  text.watched = {
    {{"Name", "/scan"}, {"Type", "sensor_msgs/msg/LaserScan"}},
    {{"Name", "/scan"}},
    {{"Type", "sensor_msgs/msg/LaserScan"}}};

  std::vector<std::string> warnings;
  const ArRestorePlan plan = qar_rviz_streaming::parse_ar_state(text, warnings);

  ASSERT_EQ(plan.panels.size(), 2u);
  EXPECT_EQ(plan.panels[0].key, "dock:Views");
  EXPECT_TRUE(plan.panels[0].on);
  EXPECT_EQ(plan.panels[1].key, "dock:Selection");
  EXPECT_TRUE(plan.panels[1].on);
  ASSERT_EQ(plan.tools.size(), 1u);
  EXPECT_EQ(plan.tools[0].name, "/a");
  EXPECT_FALSE(plan.tools[0].writable);  // missing = read-only
  EXPECT_TRUE(plan.tools[0].shown);      // missing = shown
  ASSERT_EQ(plan.watched.size(), 1u);
  // One sentence per skipped record: 5 panels, 6 tools, 2 watched topics.
  EXPECT_EQ(warnings.size(), 13u);
  EXPECT_TRUE(mentions(warnings, "Panels record 2 skipped: it has no Key"));
  EXPECT_TRUE(mentions(warnings, "Panels record 3 skipped: On 'maybe'"));
  EXPECT_TRUE(mentions(warnings, "Panels record 4 skipped: Size 'huge'"));
  EXPECT_TRUE(mentions(warnings, "'video:/camera' is not an RViz panel"));
  EXPECT_TRUE(mentions(warnings, "Tools record 2 skipped: Kind 'stream'"));
  EXPECT_TRUE(mentions(warnings, "Watched Topics record 3 skipped"));
}

TEST(ArRestoreState, ANewerVersionIsReadWithAWarning)
{
  ArStateText text;
  text.version = "2";
  text.panels = {{{"Key", "dock:Views"}, {"On", "true"}, {"Opacity", "0.5"}}};
  std::vector<std::string> warnings;
  const ArRestorePlan plan = qar_rviz_streaming::parse_ar_state(text, warnings);
  ASSERT_EQ(plan.panels.size(), 1u);
  ASSERT_EQ(warnings.size(), 1u);  // the version; the new field is not judged
  EXPECT_NE(warnings[0].find("newer plugin (version 2)"), std::string::npos);

  // A version that is not a number is read as version 1.
  text.version = "two";
  warnings.clear();
  const ArRestorePlan odd = qar_rviz_streaming::parse_ar_state(text, warnings);
  EXPECT_EQ(odd.panels.size(), 1u);
  EXPECT_TRUE(mentions(warnings, "Version 'two' is not a version number"));
  EXPECT_TRUE(mentions(warnings, "field 'Opacity' is not known"));

  // No Version is the current one, silently.
  text.version.clear();
  text.panels = {{{"Key", "dock:Views"}}};
  warnings.clear();
  qar_rviz_streaming::parse_ar_state(text, warnings);
  EXPECT_TRUE(warnings.empty());
}

TEST(ArRestoreState, PanelsToStartKeepTheSavedOrder)
{
  ArRestorePlan plan;
  plan.panels.push_back({"dock:Views", true, std::nullopt});
  plan.panels.push_back({"dock:Time", false, ArCopySize::Native});
  plan.panels.push_back({"qar_streaming_panel", true, std::nullopt});
  plan.panels.push_back({"dock:Displays", true, std::nullopt});
  const std::vector<std::string> keys = qar_rviz_streaming::panels_to_start(plan, {});
  EXPECT_EQ(
    keys, (std::vector<std::string>{"dock:Views", "qar_streaming_panel", "dock:Displays"}));
}

TEST(ArRestoreState, PanelsToStartSkipQueuedStreamingAndTheDisplaysCopy)
{
  ArRestorePlan plan;
  plan.panels.push_back({"hidden:Displays", true, std::nullopt});
  plan.panels.push_back({"dock:Views", true, std::nullopt});
  plan.panels.push_back({"dock:Time", true, std::nullopt});
  plan.panels.push_back({"dock:Selection", true, std::nullopt});
  const std::vector<std::string> keys =
    qar_rviz_streaming::panels_to_start(plan, {"dock:Time", "dock:Other"});
  EXPECT_EQ(keys, (std::vector<std::string>{"dock:Views", "dock:Selection"}));
  EXPECT_TRUE(qar_rviz_streaming::panels_to_start(ArRestorePlan(), {}).empty());
}

TEST(ArRestoreState, AnExplicitOffOfTheDisplaysCopyWinsOverTheSetting)
{
  ArRestorePlan plan;
  plan.panels.push_back({"hidden:Displays", false, std::nullopt});
  EXPECT_FALSE(qar_rviz_streaming::hidden_displays_starts(plan, true));
  EXPECT_FALSE(qar_rviz_streaming::hidden_displays_starts(plan, false));
  plan.panels[0].on = true;
  EXPECT_TRUE(qar_rviz_streaming::hidden_displays_starts(plan, false));
}

TEST(ArRestoreState, NoEntryFollowsTheSetting)
{
  ArRestorePlan plan;
  plan.panels.push_back({"dock:Views", true, std::nullopt});
  EXPECT_TRUE(qar_rviz_streaming::hidden_displays_starts(plan, true));
  EXPECT_FALSE(qar_rviz_streaming::hidden_displays_starts(plan, false));
  EXPECT_TRUE(qar_rviz_streaming::hidden_displays_starts(ArRestorePlan(), true));
  EXPECT_EQ(qar_rviz_streaming::find_ar_panel_state(plan, "hidden:Displays"), nullptr);
  ASSERT_NE(qar_rviz_streaming::find_ar_panel_state(plan, "dock:Views"), nullptr);
}

TEST(ArRestoreState, NothingOfThePickIsInTheState)
{
  // The plan has no field for the pick mode, a waiting pick, the confirmation, the priming or
  // the selected tab; the text a plan makes names none of them.
  const ArStateText text = qar_rviz_streaming::make_ar_state(sample_plan());
  const auto has_field = [&text](const std::string & field) {
      for (const auto * list : {&text.panels, &text.tools, &text.watched}) {
        for (const ConfigRecord & record : *list) {
          if (record.count(field) != 0) {
            return true;
          }
        }
      }
      return false;
    };
  for (const char * field :
    {"Pick Mode", "Pick Target", "Publish without confirming", "Prime next frame",
      "Show advanced options", "Selected Tab"})
  {
    EXPECT_FALSE(has_field(field)) << field;
  }

  // A record that carries such a field is read without it, and says so.
  ArStateText hand_made;
  hand_made.version = "1";
  hand_made.panels = {{{"Key", "dock:Views"}, {"On", "true"}, {"Pick Mode", "point"}}};
  std::vector<std::string> warnings;
  const ArRestorePlan plan = qar_rviz_streaming::parse_ar_state(hand_made, warnings);
  ASSERT_EQ(plan.panels.size(), 1u);
  ASSERT_EQ(warnings.size(), 1u);
  EXPECT_NE(warnings[0].find("field 'Pick Mode' is not known and is ignored"), std::string::npos);
}

TEST(ArRestoreState, OnlyRvizPanelsHaveAKey)
{
  for (const char * key :
    {"qar_streaming_panel", "hidden:Displays", "dock:Views", "dock:Views#2"})
  {
    EXPECT_TRUE(qar_rviz_streaming::ar_state_panel_key(key)) << key;
  }
  for (const char * key :
    {"", "dock:", "video:/camera", "dialog:Add", "control:everybody", "tool:/cmd_vel", "Views"})
  {
    EXPECT_FALSE(qar_rviz_streaming::ar_state_panel_key(key)) << key;
  }
}

TEST(ArRestoreState, TooManyRecordsAreCutWithOneWarning)
{
  ArStateText text;
  for (std::size_t i = 0; i < qar_rviz_streaming::kArStateMaxRecords + 5; ++i) {
    text.panels.push_back({{"Key", "dock:P" + std::to_string(i)}, {"On", "true"}});
  }
  std::vector<std::string> warnings;
  const ArRestorePlan plan = qar_rviz_streaming::parse_ar_state(text, warnings);
  EXPECT_EQ(plan.panels.size(), qar_rviz_streaming::kArStateMaxRecords);
  EXPECT_EQ(warnings.size(), 1u);
}

TEST(ArRestoreState, TheRecordKeepsWhatIsOnAndTheSizeChoices)
{
  const std::map<std::string, ArCopySize> choices = {{"dock:Time", ArCopySize::Native}};
  const std::vector<qar_rviz_streaming::ArPanelLive> streams = {
    {"dock:Views", true}, {"dock:Tool Properties", false}, {"qar_streaming_panel", true},
    {"dialog:Add", true}, {"video:/cam", true}};
  const std::vector<ArPanelState> panels =
    qar_rviz_streaming::merge_ar_panels({}, streams, {"dock:Selection", "video:/x"}, choices, true);
  // On: Views, this panel and the queued Selection; Time only for its size; the dialog and the
  // video panel are not part of the record; a panel that is off is not listed.
  ASSERT_EQ(panels.size(), 4u);
  EXPECT_EQ(panels[0].key, "dock:Views");
  EXPECT_EQ(panels[1].key, "qar_streaming_panel");
  EXPECT_EQ(panels[2].key, "dock:Selection");
  EXPECT_TRUE(panels[2].on);
  EXPECT_EQ(panels[3].key, "dock:Time");
  EXPECT_FALSE(panels[3].on);
  ASSERT_TRUE(panels[3].size.has_value());
  EXPECT_EQ(*panels[3].size, ArCopySize::Native);
}

TEST(ArRestoreState, TheBaseIsKeptWhileNothingStreams)
{
  // No session: the plan of the configuration stays whole, with the size choices of the moment.
  std::vector<ArPanelState> base;
  base.push_back({"dock:Views", true, ArCopySize::Native});
  base.push_back({"dock:Time", true, std::nullopt});
  const std::map<std::string, ArCopySize> choices = {{"dock:Time", ArCopySize::Native}};
  const std::vector<ArPanelState> panels =
    qar_rviz_streaming::merge_ar_panels(base, {}, {}, choices, true);
  ASSERT_EQ(panels.size(), 2u);
  // Views lost its choice (the person went back to the global size); Time gained one.
  EXPECT_EQ(panels[0].key, "dock:Views");
  EXPECT_TRUE(panels[0].on);
  EXPECT_FALSE(panels[0].size.has_value());
  EXPECT_EQ(panels[1].key, "dock:Time");
  EXPECT_TRUE(panels[1].on);
  ASSERT_TRUE(panels[1].size.has_value());
  EXPECT_EQ(*panels[1].size, ArCopySize::Native);
}

TEST(ArRestoreState, ABaseIsOnlyGivenWhileThePlanWaitsAndKeepsItsPanels)
{
  // The plan of the configuration waits for the first connect: a panel that is off at the moment
  // (not streamed yet) does not clear its entry, and a panel that streams is on whatever the
  // plan said.
  std::vector<ArPanelState> base;
  base.push_back({"dock:Views", true, std::nullopt});
  base.push_back({"dock:Time", false, ArCopySize::Native});
  const std::vector<qar_rviz_streaming::ArPanelLive> streams = {
    {"dock:Views", false}, {"dock:Time", true}};
  const std::map<std::string, ArCopySize> choices = {{"dock:Time", ArCopySize::Native}};
  const std::vector<ArPanelState> panels =
    qar_rviz_streaming::merge_ar_panels(base, streams, {}, choices, true);
  ASSERT_EQ(panels.size(), 2u);
  EXPECT_EQ(panels[0].key, "dock:Views");
  EXPECT_TRUE(panels[0].on);
  EXPECT_EQ(panels[1].key, "dock:Time");
  EXPECT_TRUE(panels[1].on);
}

TEST(ArRestoreState, TheDisplaysCopyIsListedOnlyAgainstTheSetting)
{
  using qar_rviz_streaming::ArPanelLive;
  // The setting starts it: streaming is the default and not listed, a hidden copy is an off.
  EXPECT_TRUE(
    qar_rviz_streaming::merge_ar_panels({}, {ArPanelLive{"hidden:Displays", true}}, {}, {}, true)
    .empty());
  const std::vector<ArPanelState> off =
    qar_rviz_streaming::merge_ar_panels({}, {ArPanelLive{"hidden:Displays", false}}, {}, {}, true);
  ASSERT_EQ(off.size(), 1u);
  EXPECT_EQ(off[0].key, "hidden:Displays");
  EXPECT_FALSE(off[0].on);
  // The setting does not start it: off is the default, a copy the person started is an on.
  EXPECT_TRUE(
    qar_rviz_streaming::merge_ar_panels({}, {ArPanelLive{"hidden:Displays", false}}, {}, {}, false)
    .empty());
  const std::vector<ArPanelState> on =
    qar_rviz_streaming::merge_ar_panels({}, {ArPanelLive{"hidden:Displays", true}}, {}, {}, false);
  ASSERT_EQ(on.size(), 1u);
  EXPECT_TRUE(on[0].on);
  // A copy the person switched on again after an off that the plan had is not an entry any more.
  std::vector<ArPanelState> base;
  base.push_back({"hidden:Displays", false, std::nullopt});
  EXPECT_TRUE(
    qar_rviz_streaming::merge_ar_panels(base, {ArPanelLive{"hidden:Displays", true}}, {}, {}, true)
    .empty());
}

TEST(ArStateConfig, SaveThenLoadGivesTheSamePlan)
{
  // A fake tree that stands for the configuration of the panel: the flat settings are in it.
  ConfigNode tree;
  tree.set("Class", ConfigNode::make_value("qar_rviz_streaming/QarStreamingPanel"));
  tree.set("Settings Version", ConfigNode::make_value("4"));
  const ArStateConfigIo io{[&tree]() { return tree; }, [&tree](const ConfigNode & node) {
      tree = node;
    }};

  const ArRestorePlan plan = sample_plan();
  qar_rviz_streaming::store_ar_state_text(io, qar_rviz_streaming::make_ar_state(plan));

  // The flat keys stay, the record is a map with the three lists in the order of the example.
  ASSERT_NE(tree.find("Class"), nullptr);
  const ConfigNode * state = tree.find(qar_rviz_streaming::kArStateKey);
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->kind, ConfigNode::Kind::Map);
  EXPECT_EQ(
    state->map_keys, (std::vector<std::string>{"Version", "Panels", "Tools", "Watched Topics"}));
  const ConfigNode * panels = state->find("Panels");
  ASSERT_NE(panels, nullptr);
  ASSERT_EQ(panels->list.size(), 4u);
  EXPECT_EQ(panels->list[0].map_keys, (std::vector<std::string>{"Key", "On", "Size"}));
  EXPECT_EQ(
    state->find("Tools")->list[0].map_keys,
    (std::vector<std::string>{"Kind", "Name", "Type", "Writable", "Shown"}));

  std::vector<std::string> warnings;
  const std::optional<ArStateText> text = qar_rviz_streaming::load_ar_state_text(io, warnings);
  ASSERT_TRUE(text.has_value());
  EXPECT_TRUE(warnings.empty());
  const ArRestorePlan read = qar_rviz_streaming::parse_ar_state(*text, warnings);
  EXPECT_TRUE(warnings.empty());
  expect_same_plan(plan, read);

  // Saving again replaces the record and keeps one copy of it.
  qar_rviz_streaming::store_ar_state_text(io, qar_rviz_streaming::make_ar_state(ArRestorePlan()));
  int copies = 0;
  for (const std::string & key : tree.map_keys) {
    copies += key == qar_rviz_streaming::kArStateKey ? 1 : 0;
  }
  EXPECT_EQ(copies, 1);
  const ConfigNode * empty = tree.find(qar_rviz_streaming::kArStateKey);
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(empty->map_keys, (std::vector<std::string>{"Version"}));
}

TEST(ArStateConfig, NonValueChildrenAreSkipped)
{
  ConfigNode record;
  record.set("Key", ConfigNode::make_value("dock:Views"));
  record.set("On", ConfigNode::make_value("true"));
  ConfigNode nested;
  nested.set("Deep", ConfigNode::make_value("x"));
  record.set("Nested", nested);
  ConfigNode inner_list;
  inner_list.kind = ConfigNode::Kind::List;
  record.set("Inner", inner_list);

  ConfigNode panels;
  panels.kind = ConfigNode::Kind::List;
  panels.list.push_back(record);
  panels.list.push_back(ConfigNode::make_value("stray"));  // a list element that is no map

  ConfigNode state;
  state.set("Version", ConfigNode::make_value("1"));
  state.set("Panels", panels);
  state.set("Tools", ConfigNode::make_value("not a list"));
  ConfigNode tree;
  tree.set("AR State", state);

  std::vector<std::string> warnings;
  const std::optional<ArStateText> text = qar_rviz_streaming::read_ar_state_text(tree, warnings);
  ASSERT_TRUE(text.has_value());
  ASSERT_EQ(text->panels.size(), 2u);
  EXPECT_EQ(text->panels[0].size(), 2u);  // Key and On only
  EXPECT_EQ(text->panels[0].at("Key"), "dock:Views");
  EXPECT_TRUE(text->panels[1].empty());
  EXPECT_TRUE(text->tools.empty());
  EXPECT_TRUE(mentions(warnings, "field 'Nested' is not a plain value"));
  EXPECT_TRUE(mentions(warnings, "field 'Inner' is not a plain value"));
  EXPECT_TRUE(mentions(warnings, "Panels record 2 is not a map"));
  EXPECT_TRUE(mentions(warnings, "Tools is not a list"));

  // No record, an empty record node, a record that is not a map.
  warnings.clear();
  EXPECT_FALSE(qar_rviz_streaming::read_ar_state_text(ConfigNode(), warnings).has_value());
  ConfigNode empty_state;
  empty_state.set("AR State", ConfigNode());
  EXPECT_FALSE(qar_rviz_streaming::read_ar_state_text(empty_state, warnings).has_value());
  EXPECT_TRUE(warnings.empty());
  ConfigNode wrong;
  wrong.set("AR State", ConfigNode::make_value("on"));
  EXPECT_FALSE(qar_rviz_streaming::read_ar_state_text(wrong, warnings).has_value());
  EXPECT_TRUE(mentions(warnings, "AR State is not a map"));
}
