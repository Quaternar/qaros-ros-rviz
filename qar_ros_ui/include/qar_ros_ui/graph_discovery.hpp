// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__GRAPH_DISCOVERY_HPP_
#define QAR_ROS_UI__GRAPH_DISCOVERY_HPP_

/// @file graph_discovery.hpp
/// @brief What is on the ROS graph: topics, services and actions with their
/// types, and what changed since last time.
///
/// Actions are not a graph entity. An action `/fib` of type `pkg/action/Fib`
/// exists as three services and two topics, all hidden (a `_action` token):
///
///   /fib/_action/send_goal     pkg/action/Fib_SendGoal          (service)
///   /fib/_action/cancel_goal   action_msgs/srv/CancelGoal       (service)
///   /fib/_action/get_result    pkg/action/Fib_GetResult         (service)
///   /fib/_action/feedback      pkg/action/Fib_FeedbackMessage   (topic)
///   /fib/_action/status        action_msgs/msg/GoalStatusArray  (topic)
///
/// `recover_actions()` folds those back into `{"/fib", "pkg/action/Fib"}` from
/// the send_goal service and the feedback topic (either alone is enough), and
/// the snapshot cross-checks the result with `rcl_action_get_names_and_types`.
/// Hidden names are dropped from the topic and service lists unless asked for,
/// as `ros2 topic list` does, so a UI does not offer the protocol internals.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "qar_ros_ui/node_handles.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

/// @brief A named entity and the types seen for it (normally one; DDS allows
/// several publishers to disagree, and a UI must be told).
struct GraphEntity
{
  std::string name;
  std::vector<std::string> types;

  const std::string & primary_type() const;  ///< First type, or an empty string.
  bool operator==(const GraphEntity & other) const
  {
    return name == other.name && types == other.types;
  }
  bool operator!=(const GraphEntity & other) const { return !(*this == other); }
};

struct GraphSnapshot
{
  std::vector<GraphEntity> topics;    ///< Sorted by name.
  std::vector<GraphEntity> services;  ///< Sorted by name.
  std::vector<GraphEntity> actions;   ///< Sorted by name.

  const GraphEntity * find_topic(std::string_view name) const;
  const GraphEntity * find_service(std::string_view name) const;
  const GraphEntity * find_action(std::string_view name) const;
};

/// @brief Entities that appeared, disappeared or changed type between two snapshots.
struct GraphDelta
{
  std::vector<GraphEntity> added_topics, removed_topics, changed_topics;
  std::vector<GraphEntity> added_services, removed_services, changed_services;
  std::vector<GraphEntity> added_actions, removed_actions, changed_actions;

  bool empty() const;
};

struct GraphDiscoveryOptions
{
  bool include_hidden = false;  ///< Keep `_`-prefixed tokens (`/_action/…`, `/_ros2cli_…`).
};

// --- Pure helpers (no graph needed) -----------------------------------------

/// @brief True when any `/`-separated token starts with `_` (ROS's hidden-name rule).
bool is_hidden_name(std::string_view name);

/// @brief "/fib/_action/send_goal" -> "/fib" (false when not a send_goal service).
bool action_name_from_send_goal_service(std::string_view service, std::string & out_action);
/// @brief "/fib/_action/feedback" -> "/fib".
bool action_name_from_feedback_topic(std::string_view topic, std::string & out_action);
/// @brief "pkg/action/Fib_SendGoal" -> "pkg/action/Fib".
bool action_type_from_send_goal_type(std::string_view type, std::string & out_type);
/// @brief "pkg/action/Fib_FeedbackMessage" -> "pkg/action/Fib".
bool action_type_from_feedback_type(std::string_view type, std::string & out_type);

/// @brief Fold action-protocol topics and services into actions (sorted, deduplicated).
std::vector<GraphEntity> recover_actions(
  const std::vector<GraphEntity> & topics,
  const std::vector<GraphEntity> & services);

GraphDelta diff_graph(const GraphSnapshot & before, const GraphSnapshot & after);

// --- Live -------------------------------------------------------------------

/// @brief One pass over the graph. Never blocks on discovery: it reports what
/// the middleware knows right now.
Result take_graph_snapshot(
  const NodeHandles & handles,
  GraphSnapshot & out,
  const GraphDiscoveryOptions & options = {});

/// @brief The action names rcl_action itself derives, for cross-checking.
Result rcl_action_names_and_types(const NodeHandles & handles, std::vector<GraphEntity> & out);

/// @brief Snapshot + change detection with a revision counter, so a UI can
/// refresh on `refresh()` returning a non-empty delta and otherwise do nothing.
class GraphDiscovery
{
public:
  explicit GraphDiscovery(NodeHandles handles, GraphDiscoveryOptions options = {});

  /// Takes a new snapshot; @p out_delta (optional) receives the changes.
  Result refresh(GraphDelta * out_delta = nullptr);

  const GraphSnapshot & current() const { return current_; }
  /// Incremented on every refresh that changed something. 0 before the first.
  std::uint64_t revision() const { return revision_; }

private:
  NodeHandles handles_;
  GraphDiscoveryOptions options_;
  GraphSnapshot current_;
  std::uint64_t revision_ = 0;
  bool have_snapshot_ = false;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__GRAPH_DISCOVERY_HPP_
