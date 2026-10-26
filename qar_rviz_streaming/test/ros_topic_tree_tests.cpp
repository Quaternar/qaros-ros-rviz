// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Grouping rules for the Add-topic dialog's tree. Pure QtCore, so these run in
// the plain-CMake build with no ROS graph and no RViz.

#include <gtest/gtest.h>

#include "qar_rviz_streaming/ros_topic_tree.hpp"

#include <vector>

using qar_rviz_streaming::build_topic_tree;
using qar_rviz_streaming::filter_topics;
using qar_rviz_streaming::RosTopicEntry;
using qar_rviz_streaming::RosTopicNode;
using qar_rviz_streaming::topic_matches_filter;

namespace
{

RosTopicEntry
entry(const char * name, const char * type)
{
  return RosTopicEntry{QString::fromLatin1(name), QStringList{QString::fromLatin1(type)}};
}

/// Depth-first lookup by fully-qualified name.
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

TEST(TopicTree, EmptyListYieldsAnEmptyRoot)
{
  const RosTopicNode root = build_topic_tree({});
  EXPECT_TRUE(root.children.empty());
  EXPECT_FALSE(root.is_topic());
}

TEST(TopicTree, ATopLevelTopicBecomesOneChildOfTheRoot)
{
  const RosTopicNode root = build_topic_tree({entry("/tf", "tf2_msgs/msg/TFMessage")});

  ASSERT_EQ(root.children.size(), 1u);
  EXPECT_EQ(root.children[0].segment, "tf");
  EXPECT_EQ(root.children[0].full_name, "/tf");
  EXPECT_TRUE(root.children[0].is_topic());
}

TEST(TopicTree, NestedTopicsShareTheirCommonNamespaceNodes)
{
  const RosTopicNode root = build_topic_tree({
    entry("/arco/os1_cloud_node/points", "sensor_msgs/msg/PointCloud2"),
    entry("/arco/os1_cloud_node/imu", "sensor_msgs/msg/Imu"),
  });

  ASSERT_EQ(root.children.size(), 1u);
  const auto * node = find(root, "/arco/os1_cloud_node");
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->children.size(), 2u);
  // The shared namespace level is NOT itself a topic.
  EXPECT_FALSE(node->is_topic());
}

TEST(TopicTree, ANodeCanBeBothATopicAndANamespace)
{
  // /tf is a real topic AND a prefix of /tf/status — both must survive.
  const RosTopicNode root = build_topic_tree({
    entry("/tf", "tf2_msgs/msg/TFMessage"),
    entry("/tf/status", "std_msgs/msg/String"),
  });

  const auto * tf = find(root, "/tf");
  ASSERT_NE(tf, nullptr);
  EXPECT_TRUE(tf->is_topic());
  ASSERT_EQ(tf->children.size(), 1u);
  EXPECT_TRUE(tf->children[0].is_topic());
}

TEST(TopicTree, ChildrenAreSortedSoRefreshesDoNotReshuffleTheView)
{
  // ROS returns topics in no defined order; feed them backwards.
  const RosTopicNode root = build_topic_tree({
    entry("/zulu", "std_msgs/msg/String"),
    entry("/alpha", "std_msgs/msg/String"),
    entry("/mike", "std_msgs/msg/String"),
  });

  ASSERT_EQ(root.children.size(), 3u);
  EXPECT_EQ(root.children[0].segment, "alpha");
  EXPECT_EQ(root.children[1].segment, "mike");
  EXPECT_EQ(root.children[2].segment, "zulu");
}

TEST(TopicTree, FullNamesAreRebuiltFromTheRootDown)
{
  const RosTopicNode root = build_topic_tree({entry("/a/b/c", "std_msgs/msg/String")});

  EXPECT_NE(find(root, "/a"), nullptr);
  EXPECT_NE(find(root, "/a/b"), nullptr);
  const auto * leaf = find(root, "/a/b/c");
  ASSERT_NE(leaf, nullptr);
  EXPECT_EQ(leaf->segment, "c");
}

TEST(TopicTree, MultipleTypesOnOneTopicAreKept)
{
  RosTopicEntry both;
  both.name = "/dual";
  both.types = QStringList{"std_msgs/msg/String", "std_msgs/msg/Int32"};

  const RosTopicNode root = build_topic_tree({both});
  ASSERT_EQ(root.children.size(), 1u);
  EXPECT_EQ(root.children[0].types.size(), 2);
}

TEST(TopicTree, DuplicateListingsAreIdempotent)
{
  const RosTopicNode root = build_topic_tree({
    entry("/points", "sensor_msgs/msg/PointCloud2"),
    entry("/points", "sensor_msgs/msg/PointCloud2"),
  });

  ASSERT_EQ(root.children.size(), 1u);
  EXPECT_EQ(root.children[0].types.size(), 1);
}

TEST(TopicTree, DegenerateNamesAreSkippedRatherThanMakingUnnamedLevels)
{
  const RosTopicNode root = build_topic_tree({
    entry("", "std_msgs/msg/String"),
    entry("/", "std_msgs/msg/String"),
    entry("//doubled//slash", "std_msgs/msg/String"),
  });

  // Only the doubled-slash entry survives, collapsed to two clean levels.
  ASSERT_EQ(root.children.size(), 1u);
  EXPECT_EQ(root.children[0].segment, "doubled");
  ASSERT_EQ(root.children[0].children.size(), 1u);
  EXPECT_EQ(root.children[0].children[0].full_name, "/doubled/slash");
}

TEST(TopicFilter, EmptyFilterMatchesEverything)
{
  EXPECT_TRUE(topic_matches_filter(entry("/anything", "std_msgs/msg/String"), ""));
}

TEST(TopicFilter, MatchesOnNameOrTypeCaseInsensitively)
{
  const RosTopicEntry e = entry("/arco/points", "sensor_msgs/msg/PointCloud2");
  EXPECT_TRUE(topic_matches_filter(e, "ARCO"));
  EXPECT_TRUE(topic_matches_filter(e, "pointcloud"));
  EXPECT_FALSE(topic_matches_filter(e, "laser"));
}

TEST(TopicFilter, FilteringKeepsOrderAndDropsNonMatches)
{
  const std::vector<RosTopicEntry> all{
    entry("/a/points", "sensor_msgs/msg/PointCloud2"),
    entry("/b/image", "sensor_msgs/msg/Image"),
    entry("/c/points", "sensor_msgs/msg/PointCloud2"),
  };

  const auto kept = filter_topics(all, "points");
  ASSERT_EQ(kept.size(), 2u);
  EXPECT_EQ(kept[0].name, "/a/points");
  EXPECT_EQ(kept[1].name, "/c/points");
}
