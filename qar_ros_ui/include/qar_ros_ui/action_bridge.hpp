// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__ACTION_BRIDGE_HPP_
#define QAR_ROS_UI__ACTION_BRIDGE_HPP_

/// @file action_bridge.hpp
/// @brief Dynamic action goals: send, feedback, result, cancel — all as UiValues.
///
/// Built on `rclcpp_action::ClientBase`, whose constructor takes the action
/// typesupport as a run-time pointer and which is a `Waitable` of the node:
/// the executor delivers goal/result/cancel responses, feedback and status to
/// the bridge, which turns the generated protocol messages into UiValues with
/// the same introspection path as any other message. Nothing here waits.
///
/// A goal's life is exposed as a shared `ActionGoalHandle` (poll it from the UI
/// thread) and as `GoalEvent`s (executor thread, optional).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_value.hpp"

#include "qar_ros_ui/node_handles.hpp"
#include "qar_ros_ui/type_support_registry.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

/// Values are action_msgs/msg/GoalStatus's STATUS_* constants.
enum class GoalStatus : std::int8_t
{
  Unknown = 0,
  Accepted = 1,
  Executing = 2,
  Canceling = 3,
  Succeeded = 4,
  Canceled = 5,
  Aborted = 6
};
const char * to_string(GoalStatus status);
bool is_terminal(GoalStatus status);  ///< Succeeded / Canceled / Aborted.

using GoalId = std::array<std::uint8_t, 16>;
std::string goal_id_to_string(const GoalId & id);  ///< 32 lowercase hex digits.

enum class GoalEventKind
{
  Rejected,  ///< The server refused the goal; the handle is finished.
  Accepted,
  Feedback,  ///< `value` holds the feedback.
  Status,    ///< `status` changed (Executing, Canceling, ...).
  Result,    ///< `value` holds the result, `status` the terminal state.
  CancelResponse,  ///< `value` = {return_code, goals_canceling}; `error` set when refused.
  Error      ///< Timeout or rcl failure; the handle is finished.
};
const char * to_string(GoalEventKind kind);

class ActionGoalHandle;

struct GoalEvent
{
  GoalEventKind kind = GoalEventKind::Status;
  GoalStatus status = GoalStatus::Unknown;
  qar_ui_model::UiValue value;
  Result error;
};

using GoalEventCallback =
  std::function<void(const std::shared_ptr<ActionGoalHandle> & handle, const GoalEvent & event)>;

struct ActionGoalOptions
{
  /// Deadline for the server's accept/reject answer (and for a server to
  /// appear when wait_for_server is set). Execution itself is unbounded.
  std::chrono::milliseconds response_timeout{5000};
  bool wait_for_server = true;
  ConversionOptions conversion;
  ConversionLimits limits;
  GoalEventCallback on_event;
};

struct ActionGoalDiagnostics
{
  std::string action;
  std::string type_name;
  std::string goal_id;
  GoalStatus status = GoalStatus::Unknown;
  bool accepted = false;
  bool finished = false;
  std::uint64_t feedback_count = 0;
  std::string last_error;
};

/// @brief Thread-safe view of one goal. Kept alive by the caller; the bridge
/// holds it only while the goal is in flight.
class ActionGoalHandle : public std::enable_shared_from_this<ActionGoalHandle>
{
public:
  ~ActionGoalHandle();
  ActionGoalHandle(const ActionGoalHandle &) = delete;
  ActionGoalHandle & operator=(const ActionGoalHandle &) = delete;

  const GoalId & goal_id() const;
  std::string goal_id_string() const;
  const std::string & action_name() const;
  const std::string & type_name() const;

  GoalStatus status() const;
  bool accepted() const;
  bool rejected() const;
  /// Rejected, terminal status with result, or error.
  bool finished() const;

  /// Newest feedback if newer than the last take.
  bool take_feedback(qar_ui_model::UiValue & out, std::uint64_t * sequence = nullptr);
  bool latest_feedback(qar_ui_model::UiValue & out, std::uint64_t * sequence = nullptr) const;
  std::uint64_t feedback_count() const;

  /// The result, once the terminal state arrived.
  bool result(qar_ui_model::UiValue & out, GoalStatus * terminal_status = nullptr) const;
  /// Ok unless the goal ended in Timeout / RclError / ConversionFailed.
  Result error() const;

  ActionGoalDiagnostics diagnostics() const;

private:
  friend struct ActionBridgeImpl;
  friend class ActionBridge;
  struct State;
  explicit ActionGoalHandle(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
};

struct ActionBridgeDiagnostics
{
  std::size_t clients = 0;
  std::size_t active_goals = 0;
  std::uint64_t goals_sent = 0;
  std::uint64_t goals_accepted = 0;
  std::uint64_t goals_rejected = 0;
  std::uint64_t results = 0;
  std::uint64_t timeouts = 0;
  std::uint64_t failures = 0;
  std::string last_error;
};

class ActionBridge
{
public:
  explicit ActionBridge(
    NodeHandles handles,
    TypeSupportRegistry * registry = nullptr,
    RosUiDiagnosticsSink * sink = nullptr);
  /// In-flight goals stop receiving events; the server is not told.
  ~ActionBridge();
  ActionBridge(const ActionBridge &) = delete;
  ActionBridge & operator=(const ActionBridge &) = delete;

  /// Queue a goal. @p type_name "" = look the action up on the graph.
  /// @p goal is the action's Goal message as a UiValue.
  Result send_goal(
    const std::string & action,
    const std::string & type_name,
    const qar_ui_model::UiValue & goal,
    const ActionGoalOptions & options,
    std::shared_ptr<ActionGoalHandle> & out);

  /// Ask the server to cancel. The answer arrives as a CancelResponse event,
  /// and the goal's terminal state as usual.
  Result cancel(const std::shared_ptr<ActionGoalHandle> & handle);

  Result resolve_action_type(const std::string & action, std::string & out_type) const;
  Result describe(const std::string & type_name, qar_ui_model::ActionDescription & out) const;
  bool server_available(const std::string & action, const std::string & type_name);

  /// Expire goals whose accept/reject answer is overdue; send deferred goals.
  std::size_t poll();
  std::size_t active_goal_count() const;
  ActionBridgeDiagnostics diagnostics() const;

private:
  std::shared_ptr<ActionBridgeImpl> impl_;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__ACTION_BRIDGE_HPP_
