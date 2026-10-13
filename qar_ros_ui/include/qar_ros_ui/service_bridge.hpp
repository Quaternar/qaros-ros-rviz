// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__SERVICE_BRIDGE_HPP_
#define QAR_ROS_UI__SERVICE_BRIDGE_HPP_

/// @file service_bridge.hpp
/// @brief Dynamic service calls: request from a UiValue, response as a UiValue.
///
/// Humble has no `rclcpp::GenericClient`; the bridge subclasses
/// `rclcpp::ClientBase` with a typesupport handle obtained at run time, so the
/// client is a first-class entity of the node — the node's executor takes and
/// dispatches responses exactly as for a typed client, and nothing here waits.
///
/// Every call is asynchronous: `call()` returns as soon as the request is
/// queued and the outcome arrives through the callback (executor thread) or a
/// future. Timeouts and deferred sends (server not up yet) are driven by a
/// wall timer on the node while calls are pending, and by `poll()` for hosts
/// that spin no timers.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_value.hpp"

#include "qar_ros_ui/node_handles.hpp"
#include "qar_ros_ui/type_support_registry.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

struct ServiceCallOptions
{
  std::chrono::milliseconds timeout{5000};
  /// With no server yet, keep the request until one appears (within timeout)
  /// instead of failing at once.
  bool wait_for_server = true;
  ConversionOptions conversion;
  ConversionLimits limits;
};

struct ServiceCallOutcome
{
  Result status;  ///< Ok with a response, or Timeout / RclError / ConversionFailed / ...
  qar_ui_model::UiValue response;
  std::string service;
  std::string type_name;
  std::int64_t call_id = 0;
  std::chrono::milliseconds elapsed{0};

  bool ok() const { return status.ok(); }
};

using ServiceResponseCallback = std::function<void(const ServiceCallOutcome & outcome)>;

struct ServiceBridgeDiagnostics
{
  std::size_t clients = 0;
  std::size_t pending = 0;
  std::uint64_t calls_started = 0;
  std::uint64_t responses = 0;
  std::uint64_t timeouts = 0;
  std::uint64_t failures = 0;
  std::string last_error;
};

class ServiceBridge
{
public:
  explicit ServiceBridge(
    NodeHandles handles,
    TypeSupportRegistry * registry = nullptr,
    RosUiDiagnosticsSink * sink = nullptr);
  /// Pending calls are dropped without a callback.
  ~ServiceBridge();
  ServiceBridge(const ServiceBridge &) = delete;
  ServiceBridge & operator=(const ServiceBridge &) = delete;

  /// Queue a call. Returns at once; @p callback receives the outcome exactly
  /// once (unless the bridge is destroyed first). @p type_name "" = look the
  /// service up on the graph. @p out_call_id identifies the call in diagnostics.
  Result call(
    const std::string & service,
    const std::string & type_name,
    const qar_ui_model::UiValue & request,
    const ServiceCallOptions & options,
    ServiceResponseCallback callback,
    std::int64_t * out_call_id = nullptr);

  /// Same, outcome through a future. A queueing failure is delivered as the
  /// future's outcome, so the future is always valid.
  std::shared_future<ServiceCallOutcome> call_async(
    const std::string & service,
    const std::string & type_name,
    const qar_ui_model::UiValue & request,
    const ServiceCallOptions & options = {});

  Result resolve_service_type(const std::string & service, std::string & out_type) const;
  Result describe(const std::string & type_name, qar_ui_model::ServiceDescription & out) const;
  /// True when a server for @p service (of @p type_name) is matched right now.
  bool server_available(const std::string & service, const std::string & type_name);

  /// Expire timed-out calls and send deferred ones. Returns outcomes delivered.
  std::size_t poll();
  std::size_t pending_count() const;
  ServiceBridgeDiagnostics diagnostics() const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__SERVICE_BRIDGE_HPP_
