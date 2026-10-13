// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ros_graph_tree.hpp"

namespace qar_rviz_streaming
{

std::vector<RosTopicEntry> entries_of_kind(
  const std::vector<RosGraphEntry> & entries, RosEntityKind kind)
{
  std::vector<RosTopicEntry> out;
  out.reserve(entries.size());
  for (const RosGraphEntry & entry : entries) {
    if (entry.kind == kind) {
      out.push_back(RosTopicEntry{entry.name, entry.types});
    }
  }
  return out;
}

RosTopicNode build_graph_tree(const std::vector<RosGraphEntry> & entries, RosEntityKind kind)
{
  return build_topic_tree(entries_of_kind(entries, kind));
}

bool graph_entry_matches_filter(const RosGraphEntry & entry, const QString & filter)
{
  if (filter.isEmpty()) {
    return true;
  }
  // The kind word is part of the haystack so "service" narrows the whole tree to
  // services without a separate control: with three kinds in one window, typing
  // is the fastest way to say which one you meant.
  if (QString::fromLatin1(to_string(entry.kind)).contains(filter, Qt::CaseInsensitive)) {
    return true;
  }
  return topic_matches_filter(RosTopicEntry{entry.name, entry.types}, filter);
}

std::vector<RosGraphEntry> filter_graph_entries(
  const std::vector<RosGraphEntry> & entries, const QString & filter)
{
  std::vector<RosGraphEntry> out;
  out.reserve(entries.size());
  for (const RosGraphEntry & entry : entries) {
    if (graph_entry_matches_filter(entry, filter)) {
      out.push_back(entry);
    }
  }
  return out;
}

const RosGraphEntry * find_graph_entry(
  const std::vector<RosGraphEntry> & entries, RosEntityKind kind, const QString & name)
{
  for (const RosGraphEntry & entry : entries) {
    if (entry.kind == kind && entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

QString ros_entity_kind_label(RosEntityKind kind)
{
  switch (kind) {
    case RosEntityKind::Topic:
      return QStringLiteral("Topics");
    case RosEntityKind::Service:
      return QStringLiteral("Services");
    case RosEntityKind::Action:
      return QStringLiteral("Actions");
  }
  return QStringLiteral("Topics");
}

}  // namespace qar_rviz_streaming
