// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ros_topic_tree.hpp"

#include <algorithm>

namespace qar_rviz_streaming
{
namespace
{

/// Find (or create) the child of @p parent whose segment is @p segment.
RosTopicNode &
child_for_segment(RosTopicNode & parent, const QString & segment, const QString & full_name)
{
  for (auto & child : parent.children) {
    if (child.segment == segment) {
      return child;
    }
  }
  RosTopicNode node;
  node.segment = segment;
  node.full_name = full_name;
  parent.children.push_back(std::move(node));
  return parent.children.back();
}

void
sort_recursive(RosTopicNode & node)
{
  std::sort(
    node.children.begin(), node.children.end(),
    [](const RosTopicNode & a, const RosTopicNode & b) { return a.segment < b.segment; });
  for (auto & child : node.children) {
    sort_recursive(child);
  }
}

}  // namespace

RosTopicNode
build_topic_tree(const std::vector<RosTopicEntry> & topics)
{
  RosTopicNode root;

  for (const auto & entry : topics) {
    if (entry.name.isEmpty()) {
      continue;
    }
    // Skip the empty leading segment produced by the leading '/', and any empty
    // segment from a doubled slash, rather than creating unnamed tree levels.
    const QStringList segments = entry.name.split('/', Qt::SkipEmptyParts);
    if (segments.isEmpty()) {
      continue;
    }

    RosTopicNode * node = &root;
    QString path;
    for (const auto & segment : segments) {
      path += '/';
      path += segment;
      node = &child_for_segment(*node, segment, path);
    }
    // Assigning (not appending) keeps a duplicate listing idempotent.
    node->types = entry.types;
  }

  sort_recursive(root);
  return root;
}

bool
topic_matches_filter(const RosTopicEntry & entry, const QString & filter)
{
  if (filter.isEmpty()) {
    return true;
  }
  if (entry.name.contains(filter, Qt::CaseInsensitive)) {
    return true;
  }
  for (const auto & type : entry.types) {
    if (type.contains(filter, Qt::CaseInsensitive)) {
      return true;
    }
  }
  return false;
}

std::vector<RosTopicEntry>
filter_topics(const std::vector<RosTopicEntry> & topics, const QString & filter)
{
  std::vector<RosTopicEntry> kept;
  kept.reserve(topics.size());
  for (const auto & entry : topics) {
    if (topic_matches_filter(entry, filter)) {
      kept.push_back(entry);
    }
  }
  return kept;
}

}  // namespace qar_rviz_streaming
