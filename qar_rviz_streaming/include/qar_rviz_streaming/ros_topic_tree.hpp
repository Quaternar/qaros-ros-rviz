// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__ROS_TOPIC_TREE_HPP_
#define QAR_RVIZ_STREAMING__ROS_TOPIC_TREE_HPP_

/// @file ros_topic_tree.hpp
/// @brief Turns a flat ROS 2 topic listing into the namespace tree the Add-topic
/// dialog shows.
///
/// Deliberately depends on QtCore ONLY — no rclcpp, no rviz_common — so the
/// grouping rules (which are where the edge cases live) are unit-tested in the
/// plain-CMake build alongside the two libraries, rather than needing a live ROS
/// graph. The panel supplies the listing from
/// `rclcpp::Node::get_topic_names_and_types()`.

#include <QString>
#include <QStringList>

#include <vector>

namespace qar_rviz_streaming
{

/// @brief One topic as ROS reports it: a fully-qualified name plus its types.
///
/// A topic may legitimately advertise more than one type, so @c types is a list.
struct RosTopicEntry
{
  QString name;
  QStringList types;
};

/// @brief One node of the namespace tree.
///
/// A node can be BOTH a topic and a namespace: `/tf` and `/tf/status` can coexist,
/// so @c types being non-empty and @c children being non-empty are independent.
struct RosTopicNode
{
  /// The single path segment this node adds ("os1_cloud_node"). Empty at the root.
  QString segment;
  /// Fully-qualified path down to here ("/arco/os1_cloud_node"). Empty at the root.
  QString full_name;
  /// Non-empty only where an actual topic exists at this path.
  QStringList types;
  std::vector<RosTopicNode> children;

  bool is_topic() const { return !types.isEmpty(); }
};

/// @brief Group @p topics by their namespace path.
///
/// Children are sorted by segment so the dialog is stable between refreshes —
/// ROS returns topics in no particular order, and a tree that reshuffles under
/// the cursor is unusable. Entries with an empty name are skipped.
RosTopicNode build_topic_tree(const std::vector<RosTopicEntry> & topics);

/// @brief Case-insensitive substring match of @p filter against a topic's name
/// or any of its types. An empty filter matches everything.
bool topic_matches_filter(const RosTopicEntry & entry, const QString & filter);

/// @brief Keep only the entries matching @p filter, preserving order.
std::vector<RosTopicEntry> filter_topics(
  const std::vector<RosTopicEntry> & topics, const QString & filter);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__ROS_TOPIC_TREE_HPP_
