// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/detail/dynamic_service_client.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include "rcl/client.h"
#include "rcl/error_handling.h"

namespace qar_ros_ui
{

DynamicServiceClient::DynamicServiceClient(
  rclcpp::node_interfaces::NodeBaseInterface * node_base,
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph,
  ServiceTypeSupport type_support,
  const std::string & service_name,
  const rmw_qos_profile_t & qos)
: rclcpp::ClientBase(node_base, std::move(node_graph)), ts_(std::move(type_support))
{
  if (!ts_.valid()) {
    throw std::runtime_error("service typesupport for '" + ts_.type_name + "' is incomplete");
  }
  rcl_client_options_t options = rcl_client_get_default_options();
  options.qos = qos;
  const rcl_ret_t ret = rcl_client_init(
    this->get_client_handle().get(), this->get_rcl_node_handle(), ts_.rmw,
    service_name.c_str(), &options);
  if (ret != RCL_RET_OK) {
    std::string message = rcl_get_error_string().str;
    rcl_reset_error();
    throw std::runtime_error("rcl_client_init('" + service_name + "') failed: " + message);
  }
}

void DynamicServiceClient::set_response_handler(ResponseHandler handler)
{
  std::lock_guard<std::mutex> lock(mutex_);
  handler_ = std::move(handler);
}

Result DynamicServiceClient::send_request(const DynamicMessage & request, std::int64_t & out_sequence)
{
  if (!request.valid()) {
    return Result::error(Status::InvalidArgument, "request instance is empty");
  }
  const rcl_ret_t ret =
    rcl_send_request(this->get_client_handle().get(), request.data(), &out_sequence);
  if (ret != RCL_RET_OK) {
    std::string message = rcl_get_error_string().str;
    rcl_reset_error();
    return Result::error(Status::RclError, "rcl_send_request failed: " + message);
  }
  return Result::success();
}

std::shared_ptr<void> DynamicServiceClient::create_response()
{
  return DynamicMessage::release_to_shared(DynamicMessage(ts_.response));
}

std::shared_ptr<rmw_request_id_t> DynamicServiceClient::create_request_header()
{
  return std::shared_ptr<rmw_request_id_t>(new rmw_request_id_t{});
}

void DynamicServiceClient::handle_response(
  std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> response)
{
  ResponseHandler handler;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    handler = handler_;
  }
  if (handler && request_header && response) {
    handler(request_header->sequence_number, response.get());
  }
}

}  // namespace qar_ros_ui
