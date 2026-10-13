// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__ROS_GRAPH_TREE_HPP_
#define QAR_RVIZ_STREAMING__ROS_GRAPH_TREE_HPP_

/// @file ros_graph_tree.hpp
/// @brief The topic tree of `ros_topic_tree.hpp`, widened to the whole graph:
/// topics, services and actions.
///
/// The grouping and filtering rules live in `ros_topic_tree.hpp` and are tested
/// there, so this file adds only what a three-kind browser needs — an entry that
/// knows its kind, and the split into the three per-kind listings
/// `build_topic_tree` then groups. **QtCore only**, like `ros_topic_tree.hpp`,
/// so it is unit-tested from the plain-CMake project without a ROS graph;
/// `qar_ros_ui::GraphDiscovery` is what fills the entries in the plugin.

#include <QString>
#include <QStringList>

#include <vector>

#include "qar_rviz_streaming/ar_ros_entity.hpp"
#include "qar_rviz_streaming/ros_topic_tree.hpp"

namespace qar_rviz_streaming
{

/// @brief One graph entity as discovery reports it.
struct RosGraphEntry
{
  RosEntityKind kind = RosEntityKind::Topic;
  QString name;
  QStringList types;

  /// The type the panel will introspect: the first one. DDS lets publishers
  /// disagree; `qar_ros_ui` reports all of them and uses the first (sorted).
  QString primary_type() const { return types.isEmpty() ? QString() : types.front(); }
};

/// @brief The entries of one kind, as `build_topic_tree` wants them.
std::vector<RosTopicEntry> entries_of_kind(
  const std::vector<RosGraphEntry> & entries, RosEntityKind kind);

/// @brief Namespace tree of one kind. Empty tree when the kind is absent.
RosTopicNode build_graph_tree(const std::vector<RosGraphEntry> & entries, RosEntityKind kind);

/// @brief Case-insensitive substring match against the name, any type, or the
/// kind word ("service"), so typing "action" narrows to actions. Empty matches all.
bool graph_entry_matches_filter(const RosGraphEntry & entry, const QString & filter);

std::vector<RosGraphEntry> filter_graph_entries(
  const std::vector<RosGraphEntry> & entries, const QString & filter);

/// @brief Look one entry up by kind and fully-qualified name.
const RosGraphEntry * find_graph_entry(
  const std::vector<RosGraphEntry> & entries, RosEntityKind kind, const QString & name);

/// @brief Plural heading for a kind: "Topics", "Services", "Actions".
QString ros_entity_kind_label(RosEntityKind kind);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__ROS_GRAPH_TREE_HPP_
