// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/service_bridge.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "rclcpp/create_timer.hpp"
#include "rclcpp/timer.hpp"
#include "rmw/qos_profiles.h"

#include "qar_ros_ui/detail/dynamic_service_client.hpp"
#include "qar_ros_ui/dynamic_message.hpp"
#include "qar_ros_ui/introspection.hpp"

namespace qar_ros_ui
{

using Clock = std::chrono::steady_clock;

namespace
{

constexpr std::chrono::milliseconds kPollPeriod{50};

void report(RosUiDiagnosticsSink * sink, DiagnosticSeverity severity, const std::string & message)
{
  if (sink != nullptr) {
    sink->on_message(severity, "service", message);
  }
}

}  // namespace

struct ServiceBridge::Impl : public std::enable_shared_from_this<ServiceBridge::Impl>
{
  struct Pending
  {
    std::int64_t call_id = 0;
    std::string service;
    std::string type_name;
    std::shared_ptr<DynamicServiceClient> client;
    DynamicMessage request;
    bool sent = false;
    std::int64_t sequence = -1;
    Clock::time_point start;
    Clock::time_point deadline;
    ServiceResponseCallback callback;
    ConversionLimits limits;
  };
  using Delivery = std::pair<ServiceResponseCallback, ServiceCallOutcome>;

  NodeHandles handles;
  TypeSupportRegistry * registry = nullptr;
  RosUiDiagnosticsSink * sink = nullptr;

  mutable std::mutex mutex;
  std::map<std::string, std::shared_ptr<DynamicServiceClient>> clients;
  std::vector<std::unique_ptr<Pending>> pending;
  std::int64_t next_call_id = 1;
  rclcpp::TimerBase::SharedPtr timer;
  bool timer_active = false;
  std::uint64_t calls_started = 0;
  std::uint64_t responses = 0;
  std::uint64_t timeouts = 0;
  std::uint64_t failures = 0;
  std::string last_error;

  ~Impl()
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (timer) {
      timer->cancel();
    }
    pending.clear();
    clients.clear();
  }

  static std::string client_key(const std::string & service, const std::string & type_name)
  {
    return service + "|" + type_name;
  }

  Result client_for_locked(
    const std::string & service, const std::string & type_name,
    std::shared_ptr<DynamicServiceClient> & out)
  {
    const std::string key = client_key(service, type_name);
    const auto cached = clients.find(key);
    if (cached != clients.end()) {
      out = cached->second;
      return Result::success();
    }
    ServiceTypeSupport ts;
    Result result = registry->service(type_name, ts);
    if (!result.ok()) {
      return result;
    }
    std::shared_ptr<DynamicServiceClient> client;
    try {
      client = std::make_shared<DynamicServiceClient>(
        handles.base.get(), handles.graph, ts, service, rmw_qos_profile_services_default);
      handles.services->add_client(client, nullptr);
    } catch (const std::exception & e) {
      return Result::error(Status::RclError, e.what());
    }
    std::weak_ptr<Impl> weak = weak_from_this();
    DynamicServiceClient * raw = client.get();
    client->set_response_handler(
      [weak, raw](std::int64_t sequence, const void * response) {
        if (auto self = weak.lock()) {
          self->on_response(raw, sequence, response);
        }
      });
    clients[key] = client;
    out = client;
    return Result::success();
  }

  static bool ready(DynamicServiceClient & client)
  {
    try {
      return client.service_is_ready();
    } catch (const std::exception &) {
      return false;
    }
  }

  static ServiceCallOutcome outcome_for(const Pending & call, Result status)
  {
    ServiceCallOutcome outcome;
    outcome.status = std::move(status);
    outcome.service = call.service;
    outcome.type_name = call.type_name;
    outcome.call_id = call.call_id;
    outcome.elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - call.start);
    return outcome;
  }

  void ensure_timer_locked()
  {
    if (timer) {
      if (!timer_active) {
        timer->reset();
        timer_active = true;
      }
      return;
    }
    std::weak_ptr<Impl> weak = weak_from_this();
    try {
      timer = rclcpp::create_wall_timer(
        kPollPeriod,
        [weak]() {
          if (auto self = weak.lock()) {
            self->poll();
          }
        },
        nullptr, handles.base.get(), handles.timers.get());
      timer_active = true;
    } catch (const std::exception & e) {
      report(
        sink, DiagnosticSeverity::Warning,
        std::string("no wall timer for call timeouts (") + e.what() + "); call poll() periodically");
    }
  }

  void idle_timer_locked()
  {
    if (pending.empty() && timer && timer_active) {
      timer->cancel();
      timer_active = false;
    }
  }

  static void deliver(std::vector<Delivery> & deliveries)
  {
    for (Delivery & delivery : deliveries) {
      if (delivery.first) {
        delivery.first(delivery.second);
      }
    }
  }

  void on_response(DynamicServiceClient * client, std::int64_t sequence, const void * response)
  {
    std::vector<Delivery> deliveries;
    {
      std::lock_guard<std::mutex> lock(mutex);
      const auto it = std::find_if(
        pending.begin(), pending.end(),
        [client, sequence](const std::unique_ptr<Pending> & call) {
          return call->sent && call->client.get() == client && call->sequence == sequence;
        });
      if (it == pending.end()) {
        return;  // a response after its timeout: nobody is waiting
      }
      Pending & call = **it;
      ServiceCallOutcome outcome = outcome_for(call, Result::success());
      const MessageMembers * members = client->type_support().response.members();
      if (members != nullptr) {
        ConversionReport conversion;
        outcome.response = message_to_ui_value(response, *members, call.limits, &conversion);
      } else {
        outcome.status = Result::error(Status::TypeSupportUnavailable, "no response introspection");
      }
      ++responses;
      deliveries.emplace_back(std::move(call.callback), std::move(outcome));
      pending.erase(it);
      idle_timer_locked();
    }
    deliver(deliveries);
  }

  std::size_t poll()
  {
    std::vector<Delivery> deliveries;
    {
      std::lock_guard<std::mutex> lock(mutex);
      const Clock::time_point now = Clock::now();
      for (auto it = pending.begin(); it != pending.end();) {
        Pending & call = **it;
        if (!call.sent && ready(*call.client)) {
          std::int64_t sequence = -1;
          Result sent = call.client->send_request(call.request, sequence);
          if (sent.ok()) {
            call.sent = true;
            call.sequence = sequence;
          } else {
            ++failures;
            last_error = sent.message;
            deliveries.emplace_back(std::move(call.callback), outcome_for(call, sent));
            it = pending.erase(it);
            continue;
          }
        }
        if (now >= call.deadline) {
          ++timeouts;
          const std::string reason = call.sent ?
            "no response from '" + call.service + "'" :
            "no server for '" + call.service + "'";
          deliveries.emplace_back(
            std::move(call.callback), outcome_for(call, Result::error(Status::Timeout, reason)));
          it = pending.erase(it);
          continue;
        }
        ++it;
      }
      idle_timer_locked();
    }
    deliver(deliveries);
    return deliveries.size();
  }
};

ServiceBridge::ServiceBridge(
  NodeHandles handles, TypeSupportRegistry * registry, RosUiDiagnosticsSink * sink)
: impl_(std::make_shared<Impl>())
{
  impl_->handles = std::move(handles);
  impl_->registry = registry ? registry : &TypeSupportRegistry::global();
  impl_->sink = sink;
}

ServiceBridge::~ServiceBridge() = default;

Result ServiceBridge::resolve_service_type(const std::string & service, std::string & out_type) const
{
  if (!impl_->handles.valid()) {
    return Result::error(Status::InvalidState, "node handles are empty");
  }
  try {
    const std::string resolved = impl_->handles.base->resolve_topic_or_service_name(service, true);
    const auto names_and_types = impl_->handles.graph->get_service_names_and_types();
    const auto it = names_and_types.find(resolved);
    if (it == names_and_types.end() || it->second.empty()) {
      return Result::error(Status::NotFound, "service '" + resolved + "' is not on the graph");
    }
    std::vector<std::string> types = it->second;
    std::sort(types.begin(), types.end());
    out_type = types.front();
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, "service lookup for '" + service + "' failed: " + e.what());
  }
  return Result::success();
}

Result ServiceBridge::describe(
  const std::string & type_name, qar_ui_model::ServiceDescription & out) const
{
  return introspect_service(type_name, out, impl_->registry);
}

bool ServiceBridge::server_available(const std::string & service, const std::string & type_name)
{
  std::string type = type_name;
  if (type.empty() && !resolve_service_type(service, type).ok()) {
    return false;
  }
  std::shared_ptr<DynamicServiceClient> client;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->client_for_locked(service, type, client).ok()) {
      return false;
    }
  }
  return Impl::ready(*client);
}

Result ServiceBridge::call(
  const std::string & service,
  const std::string & type_name,
  const qar_ui_model::UiValue & request,
  const ServiceCallOptions & options,
  ServiceResponseCallback callback,
  std::int64_t * out_call_id)
{
  Impl & impl = *impl_;
  if (!impl.handles.valid()) {
    return Result::error(Status::InvalidState, "node handles are empty");
  }
  if (service.empty()) {
    return Result::error(Status::InvalidArgument, "service name is empty");
  }
  if (!callback) {
    return Result::error(Status::InvalidArgument, "a response callback is required");
  }
  std::string type = type_name;
  if (type.empty()) {
    Result resolved = resolve_service_type(service, type);
    if (!resolved.ok()) {
      return resolved;
    }
  }

  std::lock_guard<std::mutex> lock(impl.mutex);
  std::shared_ptr<DynamicServiceClient> client;
  Result result = impl.client_for_locked(service, type, client);
  if (!result.ok()) {
    ++impl.failures;
    impl.last_error = result.message;
    return result;
  }

  auto call = std::make_unique<Impl::Pending>();
  call->call_id = impl.next_call_id++;
  call->service = service;
  call->type_name = client->type_support().type_name;
  call->client = client;
  call->request = DynamicMessage(client->type_support().request);
  result = call->request.from_ui_value(request, options.conversion);
  if (!result.ok()) {
    ++impl.failures;
    impl.last_error = result.message;
    return result;
  }
  call->start = Clock::now();
  call->deadline = call->start + options.timeout;
  call->callback = std::move(callback);
  call->limits = options.limits;
  if (out_call_id != nullptr) {
    *out_call_id = call->call_id;
  }

  if (Impl::ready(*client)) {
    std::int64_t sequence = -1;
    result = client->send_request(call->request, sequence);
    if (!result.ok()) {
      ++impl.failures;
      impl.last_error = result.message;
      return result;
    }
    call->sent = true;
    call->sequence = sequence;
  } else if (!options.wait_for_server) {
    ++impl.failures;
    impl.last_error = "no server for '" + service + "'";
    return Result::error(Status::NotFound, impl.last_error);
  }
  ++impl.calls_started;
  impl.pending.push_back(std::move(call));
  impl.ensure_timer_locked();
  return Result::success();
}

std::shared_future<ServiceCallOutcome> ServiceBridge::call_async(
  const std::string & service,
  const std::string & type_name,
  const qar_ui_model::UiValue & request,
  const ServiceCallOptions & options)
{
  auto promise = std::make_shared<std::promise<ServiceCallOutcome>>();
  std::shared_future<ServiceCallOutcome> future = promise->get_future().share();
  std::int64_t call_id = 0;
  Result queued = call(
    service, type_name, request, options,
    [promise](const ServiceCallOutcome & outcome) {promise->set_value(outcome);}, &call_id);
  if (!queued.ok()) {
    ServiceCallOutcome outcome;
    outcome.status = queued;
    outcome.service = service;
    outcome.type_name = type_name;
    outcome.call_id = call_id;
    promise->set_value(outcome);
  }
  return future;
}

std::size_t ServiceBridge::poll()
{
  return impl_->poll();
}

std::size_t ServiceBridge::pending_count() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->pending.size();
}

ServiceBridgeDiagnostics ServiceBridge::diagnostics() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ServiceBridgeDiagnostics out;
  out.clients = impl_->clients.size();
  out.pending = impl_->pending.size();
  out.calls_started = impl_->calls_started;
  out.responses = impl_->responses;
  out.timeouts = impl_->timeouts;
  out.failures = impl_->failures;
  out.last_error = impl_->last_error;
  return out;
}

}  // namespace qar_ros_ui
