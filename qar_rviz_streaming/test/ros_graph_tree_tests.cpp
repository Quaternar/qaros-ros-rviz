// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The topic tree widened to services and actions. QtCore only, so it runs in the
// plain-CMake build with no ROS graph.

#include <gtest/gtest.h>

#include "qar_rviz_streaming/ros_graph_tree.hpp"

#include <vector>

using qar_rviz_streaming::build_graph_tree;
using qar_rviz_streaming::entries_of_kind;
using qar_rviz_streaming::filter_graph_entries;
using qar_rviz_streaming::find_graph_entry;
using qar_rviz_streaming::graph_entry_matches_filter;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::RosGraphEntry;
using qar_rviz_streaming::RosTopicNode;

namespace
{

RosGraphEntry
entry(RosEntityKind kind, const char * name, const char * type)
{
  return RosGraphEntry{kind, QString::fromLatin1(name),
    QStringList{QString::fromLatin1(type)}};
}

std::vector<RosGraphEntry>
graph()
{
  return {
    entry(RosEntityKind::Topic, "/scan", "sensor_msgs/msg/LaserScan"),
    entry(RosEntityKind::Topic, "/robot/cmd_vel", "geometry_msgs/msg/Twist"),
    entry(RosEntityKind::Service, "/robot/set_mode", "std_srvs/srv/SetBool"),
    entry(RosEntityKind::Action, "/robot/navigate", "nav2_msgs/action/NavigateToPose"),
  };
}

const RosTopicNode *
find(const RosTopicNode & node, const QString & full_name)
{
  if (node.full_name == full_name) {
    return &node;
  }
  for (const auto & child : node.children) {
    if (const auto * hit = find(child, full_name)) {
      return hit;
    }
  }
  return nullptr;
}

}  // namespace

TEST(RosGraphTreeTest, EntriesAreSplitByKind)
{
  const auto all = graph();
  EXPECT_EQ(entries_of_kind(all, RosEntityKind::Topic).size(), 2u);
  EXPECT_EQ(entries_of_kind(all, RosEntityKind::Service).size(), 1u);
  EXPECT_EQ(entries_of_kind(all, RosEntityKind::Action).size(), 1u);
}

TEST(RosGraphTreeTest, EachKindGroupsIntoItsOwnNamespaceTree)
{
  const auto all = graph();
  const RosTopicNode topics = build_graph_tree(all, RosEntityKind::Topic);
  const RosTopicNode * cmd_vel = find(topics, "/robot/cmd_vel");
  ASSERT_NE(cmd_vel, nullptr);
  EXPECT_TRUE(cmd_vel->is_topic());
  EXPECT_EQ(cmd_vel->types.front(), "geometry_msgs/msg/Twist");
  // A service of the same namespace must not appear in the topic tree.
  EXPECT_EQ(find(topics, "/robot/set_mode"), nullptr);

  const RosTopicNode actions = build_graph_tree(all, RosEntityKind::Action);
  ASSERT_NE(find(actions, "/robot/navigate"), nullptr);
}

TEST(RosGraphTreeTest, AnAbsentKindYieldsAnEmptyTree)
{
  const std::vector<RosGraphEntry> only_topics{
    entry(RosEntityKind::Topic, "/scan", "sensor_msgs/msg/LaserScan")};
  const RosTopicNode services = build_graph_tree(only_topics, RosEntityKind::Service);
  EXPECT_TRUE(services.children.empty());
}

TEST(RosGraphTreeTest, FilterMatchesNameAndType)
{
  const auto all = graph();
  EXPECT_EQ(filter_graph_entries(all, "cmd").size(), 1u);
  EXPECT_EQ(filter_graph_entries(all, "LaserScan").size(), 1u);
  EXPECT_EQ(filter_graph_entries(all, "robot").size(), 3u);
  EXPECT_EQ(filter_graph_entries(all, "").size(), all.size());
  EXPECT_TRUE(filter_graph_entries(all, "zzz").empty());
}

TEST(RosGraphTreeTest, TypingAKindWordNarrowsToThatKind)
{
  const auto all = graph();
  // With three kinds in one window, the filter is the fastest way to say which.
  const auto services = filter_graph_entries(all, "service");
  ASSERT_EQ(services.size(), 1u);
  EXPECT_EQ(services.front().kind, RosEntityKind::Service);

  const auto actions = filter_graph_entries(all, "ACTION");
  ASSERT_FALSE(actions.empty());
  EXPECT_EQ(actions.front().kind, RosEntityKind::Action);
}

TEST(RosGraphTreeTest, FilterIsCaseInsensitive)
{
  const RosGraphEntry e = entry(RosEntityKind::Topic, "/Scan", "sensor_msgs/msg/LaserScan");
  EXPECT_TRUE(graph_entry_matches_filter(e, "scan"));
  EXPECT_TRUE(graph_entry_matches_filter(e, "LASERSCAN"));
  EXPECT_FALSE(graph_entry_matches_filter(e, "odom"));
}

TEST(RosGraphTreeTest, LookupIsByKindAndName)
{
  const auto all = graph();
  const RosGraphEntry * found = find_graph_entry(all, RosEntityKind::Action, "/robot/navigate");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->primary_type(), "nav2_msgs/action/NavigateToPose");
  EXPECT_EQ(find_graph_entry(all, RosEntityKind::Topic, "/robot/navigate"), nullptr);
  EXPECT_EQ(find_graph_entry(all, RosEntityKind::Action, "/nope"), nullptr);
}

TEST(RosGraphTreeTest, AnEntryWithNoTypeHasNoPrimaryType)
{
  RosGraphEntry bare;
  bare.kind = RosEntityKind::Topic;
  bare.name = "/bare";
  EXPECT_TRUE(bare.primary_type().isEmpty());
}

TEST(RosGraphTreeTest, KindLabelsAreThePluralHeadings)
{
  EXPECT_EQ(qar_rviz_streaming::ros_entity_kind_label(RosEntityKind::Topic), "Topics");
  EXPECT_EQ(qar_rviz_streaming::ros_entity_kind_label(RosEntityKind::Service), "Services");
  EXPECT_EQ(qar_rviz_streaming::ros_entity_kind_label(RosEntityKind::Action), "Actions");
}
