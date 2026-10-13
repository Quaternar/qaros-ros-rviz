// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/detail/dynamic_action_client.hpp"

#include <exception>
#include <string>
#include <utility>

namespace qar_ros_ui
{

DynamicActionClient::DynamicActionClient(
  const NodeHandles & handles,
  ActionTypeSupport type_support,
  const std::string & action_name,
  const rcl_action_client_options_t & options)
: rclcpp_action::ClientBase(
    handles.base, handles.graph, handles.logging, action_name, type_support.rmw, options),
  ts_(std::move(type_support))
{
}

void DynamicActionClient::set_handlers(FeedbackHandler on_feedback, StatusHandler on_status)
{
  std::lock_guard<std::mutex> lock(mutex_);
  on_feedback_ = std::move(on_feedback);
  on_status_ = std::move(on_status);
}

Result DynamicActionClient::send_goal(DynamicMessage && request, ResponseCallback callback)
{
  if (!request.valid()) {
    return Result::error(Status::InvalidArgument, "goal request instance is empty");
  }
  try {
    this->send_goal_request(DynamicMessage::release_to_shared(std::move(request)), std::move(callback));
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, std::string("send goal request failed: ") + e.what());
  }
  return Result::success();
}

Result DynamicActionClient::send_result(DynamicMessage && request, ResponseCallback callback)
{
  if (!request.valid()) {
    return Result::error(Status::InvalidArgument, "result request instance is empty");
  }
  try {
    this->send_result_request(
      DynamicMessage::release_to_shared(std::move(request)), std::move(callback));
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, std::string("send result request failed: ") + e.what());
  }
  return Result::success();
}

Result DynamicActionClient::send_cancel(
  std::shared_ptr<action_msgs::srv::CancelGoal::Request> request, ResponseCallback callback)
{
  if (!request) {
    return Result::error(Status::InvalidArgument, "cancel request is null");
  }
  try {
    this->send_cancel_request(std::static_pointer_cast<void>(request), std::move(callback));
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, std::string("send cancel request failed: ") + e.what());
  }
  return Result::success();
}

std::shared_ptr<void> DynamicActionClient::create_goal_response() const
{
  return DynamicMessage::release_to_shared(DynamicMessage(ts_.send_goal_response));
}

std::shared_ptr<void> DynamicActionClient::create_result_response() const
{
  return DynamicMessage::release_to_shared(DynamicMessage(ts_.get_result_response));
}

std::shared_ptr<void> DynamicActionClient::create_cancel_response() const
{
  return std::static_pointer_cast<void>(std::make_shared<action_msgs::srv::CancelGoal::Response>());
}

std::shared_ptr<void> DynamicActionClient::create_feedback_message() const
{
  return DynamicMessage::release_to_shared(DynamicMessage(ts_.feedback_message));
}

void DynamicActionClient::handle_feedback_message(std::shared_ptr<void> message)
{
  FeedbackHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handler = on_feedback_;
  }
  if (handler && message) {
    handler(message.get());
  }
}

std::shared_ptr<void> DynamicActionClient::create_status_message() const
{
  return std::static_pointer_cast<void>(std::make_shared<action_msgs::msg::GoalStatusArray>());
}

void DynamicActionClient::handle_status_message(std::shared_ptr<void> message)
{
  StatusHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handler = on_status_;
  }
  if (handler && message) {
    handler(*std::static_pointer_cast<action_msgs::msg::GoalStatusArray>(message));
  }
}

}  // namespace qar_ros_ui
