// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/graph_discovery.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <utility>

#include "rcl/allocator.h"
#include "rcl/error_handling.h"
#include "rcl/graph.h"
#include "rcl_action/graph.h"

namespace qar_ros_ui
{

namespace
{

constexpr std::string_view kSendGoalSuffix = "/_action/send_goal";
constexpr std::string_view kFeedbackSuffix = "/_action/feedback";
constexpr std::string_view kSendGoalTypeSuffix = "_SendGoal";
constexpr std::string_view kFeedbackTypeSuffix = "_FeedbackMessage";

bool strip_suffix(std::string_view text, std::string_view suffix, std::string & out)
{
  if (text.size() <= suffix.size() || text.substr(text.size() - suffix.size()) != suffix) {
    return false;
  }
  out.assign(text.substr(0, text.size() - suffix.size()));
  return !out.empty();
}

void sort_by_name(std::vector<GraphEntity> & entities)
{
  std::sort(
    entities.begin(), entities.end(),
    [](const GraphEntity & a, const GraphEntity & b) {return a.name < b.name;});
}

std::vector<GraphEntity> entities_from_map(
  const std::map<std::string, std::vector<std::string>> & names_and_types)
{
  std::vector<GraphEntity> out;
  out.reserve(names_and_types.size());
  for (const auto & [name, types] : names_and_types) {
    GraphEntity entity;
    entity.name = name;
    entity.types = types;
    std::sort(entity.types.begin(), entity.types.end());
    out.push_back(std::move(entity));
  }
  sort_by_name(out);
  return out;
}

std::vector<GraphEntity> without_hidden(const std::vector<GraphEntity> & entities)
{
  std::vector<GraphEntity> out;
  for (const GraphEntity & entity : entities) {
    if (!is_hidden_name(entity.name)) {
      out.push_back(entity);
    }
  }
  return out;
}

const GraphEntity * find_sorted(const std::vector<GraphEntity> & entities, std::string_view name)
{
  const auto it = std::lower_bound(
    entities.begin(), entities.end(), name,
    [](const GraphEntity & entity, std::string_view key) {return entity.name < key;});
  if (it == entities.end() || it->name != name) {
    return nullptr;
  }
  return &*it;
}

void diff_sorted(
  const std::vector<GraphEntity> & before,
  const std::vector<GraphEntity> & after,
  std::vector<GraphEntity> & added,
  std::vector<GraphEntity> & removed,
  std::vector<GraphEntity> & changed)
{
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < before.size() || j < after.size()) {
    if (j >= after.size() || (i < before.size() && before[i].name < after[j].name)) {
      removed.push_back(before[i]);
      ++i;
    } else if (i >= before.size() || after[j].name < before[i].name) {
      added.push_back(after[j]);
      ++j;
    } else {
      if (before[i].types != after[j].types) {
        changed.push_back(after[j]);
      }
      ++i;
      ++j;
    }
  }
}

}  // namespace

const std::string & GraphEntity::primary_type() const
{
  static const std::string empty;
  return types.empty() ? empty : types.front();
}

const GraphEntity * GraphSnapshot::find_topic(std::string_view name) const
{
  return find_sorted(topics, name);
}

const GraphEntity * GraphSnapshot::find_service(std::string_view name) const
{
  return find_sorted(services, name);
}

const GraphEntity * GraphSnapshot::find_action(std::string_view name) const
{
  return find_sorted(actions, name);
}

bool GraphDelta::empty() const
{
  return added_topics.empty() && removed_topics.empty() && changed_topics.empty() &&
         added_services.empty() && removed_services.empty() && changed_services.empty() &&
         added_actions.empty() && removed_actions.empty() && changed_actions.empty();
}

bool is_hidden_name(std::string_view name)
{
  std::size_t start = 0;
  while (start <= name.size()) {
    const std::size_t slash = name.find('/', start);
    const std::string_view token =
      name.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
    if (!token.empty() && token[0] == '_') {
      return true;
    }
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1;
  }
  return false;
}

bool action_name_from_send_goal_service(std::string_view service, std::string & out_action)
{
  return strip_suffix(service, kSendGoalSuffix, out_action);
}

bool action_name_from_feedback_topic(std::string_view topic, std::string & out_action)
{
  return strip_suffix(topic, kFeedbackSuffix, out_action);
}

bool action_type_from_send_goal_type(std::string_view type, std::string & out_type)
{
  return strip_suffix(type, kSendGoalTypeSuffix, out_type) &&
         out_type.find("/action/") != std::string::npos;
}

bool action_type_from_feedback_type(std::string_view type, std::string & out_type)
{
  return strip_suffix(type, kFeedbackTypeSuffix, out_type) &&
         out_type.find("/action/") != std::string::npos;
}

std::vector<GraphEntity> recover_actions(
  const std::vector<GraphEntity> & topics,
  const std::vector<GraphEntity> & services)
{
  std::map<std::string, std::set<std::string>> found;
  for (const GraphEntity & service : services) {
    std::string action;
    if (!action_name_from_send_goal_service(service.name, action)) {
      continue;
    }
    std::set<std::string> & types = found[action];
    for (const std::string & type : service.types) {
      std::string action_type;
      if (action_type_from_send_goal_type(type, action_type)) {
        types.insert(action_type);
      }
    }
  }
  for (const GraphEntity & topic : topics) {
    std::string action;
    if (!action_name_from_feedback_topic(topic.name, action)) {
      continue;
    }
    std::set<std::string> & types = found[action];
    for (const std::string & type : topic.types) {
      std::string action_type;
      if (action_type_from_feedback_type(type, action_type)) {
        types.insert(action_type);
      }
    }
  }
  std::vector<GraphEntity> out;
  out.reserve(found.size());
  for (auto & [name, types] : found) {
    GraphEntity entity;
    entity.name = name;
    entity.types.assign(types.begin(), types.end());
    out.push_back(std::move(entity));
  }
  return out;  // std::map iteration is already sorted by name
}

GraphDelta diff_graph(const GraphSnapshot & before, const GraphSnapshot & after)
{
  GraphDelta delta;
  diff_sorted(before.topics, after.topics, delta.added_topics, delta.removed_topics, delta.changed_topics);
  diff_sorted(
    before.services, after.services, delta.added_services, delta.removed_services,
    delta.changed_services);
  diff_sorted(
    before.actions, after.actions, delta.added_actions, delta.removed_actions, delta.changed_actions);
  return delta;
}

Result rcl_action_names_and_types(const NodeHandles & handles, std::vector<GraphEntity> & out)
{
  rcl_node_t * node = handles.rcl_node();
  if (node == nullptr) {
    return Result::error(Status::InvalidArgument, "node handles are empty");
  }
  rcl_names_and_types_t names_and_types = rcl_get_zero_initialized_names_and_types();
  rcl_allocator_t allocator = rcl_get_default_allocator();
  const rcl_ret_t ret = rcl_action_get_names_and_types(node, &allocator, &names_and_types);
  if (ret != RCL_RET_OK) {
    std::string message = rcl_get_error_string().str;
    rcl_reset_error();
    return Result::error(Status::RclError, "rcl_action_get_names_and_types: " + message);
  }
  out.clear();
  for (std::size_t i = 0; i < names_and_types.names.size; ++i) {
    GraphEntity entity;
    entity.name = names_and_types.names.data[i] ? names_and_types.names.data[i] : "";
    const rcutils_string_array_t & types = names_and_types.types[i];
    for (std::size_t j = 0; j < types.size; ++j) {
      if (types.data[j] != nullptr) {
        entity.types.emplace_back(types.data[j]);
      }
    }
    std::sort(entity.types.begin(), entity.types.end());
    out.push_back(std::move(entity));
  }
  (void)rcl_names_and_types_fini(&names_and_types);
  sort_by_name(out);
  return Result::success();
}

Result take_graph_snapshot(
  const NodeHandles & handles, GraphSnapshot & out, const GraphDiscoveryOptions & options)
{
  if (!handles.graph) {
    return Result::error(Status::InvalidArgument, "node handles are empty");
  }
  std::vector<GraphEntity> all_topics;
  std::vector<GraphEntity> all_services;
  try {
    all_topics = entities_from_map(handles.graph->get_topic_names_and_types());
    all_services = entities_from_map(handles.graph->get_service_names_and_types());
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, std::string("graph query failed: ") + e.what());
  }

  GraphSnapshot snapshot;
  snapshot.actions = recover_actions(all_topics, all_services);

  // rcl_action derives the same list from the same graph; anything it knows
  // that the convention missed is merged in rather than argued with.
  std::vector<GraphEntity> from_rcl;
  if (rcl_action_names_and_types(handles, from_rcl).ok()) {
    for (const GraphEntity & entity : from_rcl) {
      if (find_sorted(snapshot.actions, entity.name) == nullptr) {
        snapshot.actions.push_back(entity);
      }
    }
    sort_by_name(snapshot.actions);
  }

  snapshot.topics = options.include_hidden ? std::move(all_topics) : without_hidden(all_topics);
  snapshot.services =
    options.include_hidden ? std::move(all_services) : without_hidden(all_services);
  out = std::move(snapshot);
  return Result::success();
}

GraphDiscovery::GraphDiscovery(NodeHandles handles, GraphDiscoveryOptions options)
: handles_(std::move(handles)), options_(options)
{
}

Result GraphDiscovery::refresh(GraphDelta * out_delta)
{
  GraphSnapshot next;
  Result result = take_graph_snapshot(handles_, next, options_);
  if (!result.ok()) {
    return result;
  }
  GraphDelta delta = diff_graph(current_, next);
  if (!delta.empty() || !have_snapshot_) {
    ++revision_;
  }
  have_snapshot_ = true;
  current_ = std::move(next);
  if (out_delta != nullptr) {
    *out_delta = std::move(delta);
  }
  return Result::success();
}

}  // namespace qar_ros_ui
