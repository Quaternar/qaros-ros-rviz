// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__DETAIL__DYNAMIC_SERVICE_CLIENT_HPP_
#define QAR_ROS_UI__DETAIL__DYNAMIC_SERVICE_CLIENT_HPP_

/// @file dynamic_service_client.hpp
/// @brief `rclcpp::ClientBase` over a run-time service typesupport.
///
/// Humble's executor drives any ClientBase through three virtuals —
/// `create_response`, `create_request_header`, `handle_response` — and never
/// needs the C++ type. Implementing them with a DynamicMessage is the whole
/// trick (later ROS releases ship the same idea as `rclcpp::GenericClient`).
/// Internal to the ServiceBridge; exposed for tests.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "rclcpp/client.hpp"
#include "rclcpp/node_interfaces/node_base_interface.hpp"
#include "rclcpp/node_interfaces/node_graph_interface.hpp"
#include "rmw/types.h"

#include "qar_ros_ui/dynamic_message.hpp"
#include "qar_ros_ui/type_support_registry.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

class DynamicServiceClient : public rclcpp::ClientBase
{
public:
  /// @param sequence the rcl sequence number the request was sent with.
  /// @param response the response instance (valid for the call only).
  using ResponseHandler = std::function<void(std::int64_t sequence, const void * response)>;

  /// Throws std::runtime_error when rcl_client_init fails.
  DynamicServiceClient(
    rclcpp::node_interfaces::NodeBaseInterface * node_base,
    rclcpp::node_interfaces::NodeGraphInterface::SharedPtr node_graph,
    ServiceTypeSupport type_support,
    const std::string & service_name,
    const rmw_qos_profile_t & qos);
  ~DynamicServiceClient() override = default;

  const ServiceTypeSupport & type_support() const { return ts_; }
  void set_response_handler(ResponseHandler handler);

  Result send_request(const DynamicMessage & request, std::int64_t & out_sequence);

  std::shared_ptr<void> create_response() override;
  std::shared_ptr<rmw_request_id_t> create_request_header() override;
  void handle_response(
    std::shared_ptr<rmw_request_id_t> request_header, std::shared_ptr<void> response) override;

private:
  ServiceTypeSupport ts_;
  std::mutex mutex_;
  ResponseHandler handler_;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__DETAIL__DYNAMIC_SERVICE_CLIENT_HPP_
