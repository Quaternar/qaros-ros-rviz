// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__DETAIL__DYNAMIC_ACTION_CLIENT_HPP_
#define QAR_ROS_UI__DETAIL__DYNAMIC_ACTION_CLIENT_HPP_

/// @file dynamic_action_client.hpp
/// @brief `rclcpp_action::ClientBase` over a run-time action typesupport.
///
/// The base class owns the rcl action client and the wait-set plumbing; the
/// seven `create_*` / `handle_*` virtuals it leaves to the typed subclass are
/// implemented here with DynamicMessages for the action-specific messages and
/// with `action_msgs` types for the two the protocol fixes (cancel response,
/// status array). Internal to the ActionBridge; exposed for tests.

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "action_msgs/msg/goal_status_array.hpp"
#include "action_msgs/srv/cancel_goal.hpp"
#include "rcl_action/action_client.h"
#include "rclcpp_action/client.hpp"
#include "rclcpp_action/types.hpp"

#include "qar_ros_ui/dynamic_message.hpp"
#include "qar_ros_ui/node_handles.hpp"
#include "qar_ros_ui/type_support_registry.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

class DynamicActionClient : public rclcpp_action::ClientBase
{
public:
  using ResponseCallback = rclcpp_action::ClientBase::ResponseCallback;
  /// The whole `<Name>_FeedbackMessage` instance (goal_id + feedback).
  using FeedbackHandler = std::function<void(const void * feedback_message)>;
  using StatusHandler = std::function<void(const action_msgs::msg::GoalStatusArray & status)>;

  /// Throws rclcpp::exceptions::RCLError when rcl_action_client_init fails.
  DynamicActionClient(
    const NodeHandles & handles,
    ActionTypeSupport type_support,
    const std::string & action_name,
    const rcl_action_client_options_t & options);
  ~DynamicActionClient() override = default;

  const ActionTypeSupport & type_support() const { return ts_; }
  void set_handlers(FeedbackHandler on_feedback, StatusHandler on_status);

  rclcpp_action::GoalUUID new_goal_id() { return generate_goal_id(); }

  /// The base's protected senders, with exceptions folded into Results. The
  /// request instances are kept alive until the response arrives.
  Result send_goal(DynamicMessage && request, ResponseCallback callback);
  Result send_result(DynamicMessage && request, ResponseCallback callback);
  Result send_cancel(
    std::shared_ptr<action_msgs::srv::CancelGoal::Request> request, ResponseCallback callback);

  std::shared_ptr<void> create_goal_response() const override;
  std::shared_ptr<void> create_result_response() const override;
  std::shared_ptr<void> create_cancel_response() const override;
  std::shared_ptr<void> create_feedback_message() const override;
  void handle_feedback_message(std::shared_ptr<void> message) override;
  std::shared_ptr<void> create_status_message() const override;
  void handle_status_message(std::shared_ptr<void> message) override;

private:
  ActionTypeSupport ts_;
  std::mutex mutex_;
  FeedbackHandler on_feedback_;
  StatusHandler on_status_;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__DETAIL__DYNAMIC_ACTION_CLIENT_HPP_
