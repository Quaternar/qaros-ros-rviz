// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/topic_bridge.hpp"

#include <algorithm>
#include <exception>
#include <mutex>
#include <utility>
#include <vector>

#include "rclcpp/generic_publisher.hpp"
#include "rclcpp/generic_subscription.hpp"
#include "rclcpp/publisher_options.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/subscription_options.hpp"

#include "qar_ros_ui/dynamic_message.hpp"
#include "qar_ros_ui/introspection.hpp"

namespace qar_ros_ui
{

using qar_ui_model::UiValue;

namespace
{

rclcpp::QoS to_qos(const rmw_qos_profile_t & profile)
{
  return rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(profile), profile);
}

void report(RosUiDiagnosticsSink * sink, DiagnosticSeverity severity, const std::string & message)
{
  if (sink != nullptr) {
    sink->on_message(severity, "topic", message);
  }
}

struct EndpointProfiles
{
  std::vector<rmw_qos_profile_t> own_node;
  std::vector<rmw_qos_profile_t> other_nodes;

  std::vector<rmw_qos_profile_t> all() const
  {
    std::vector<rmw_qos_profile_t> out = other_nodes;
    out.insert(out.end(), own_node.begin(), own_node.end());
    return out;
  }
};

EndpointProfiles split_by_node(
  const NodeHandles & handles, const std::vector<rclcpp::TopicEndpointInfo> & infos)
{
  EndpointProfiles out;
  const std::string node_name = handles.base->get_name();
  const std::string node_namespace = handles.base->get_namespace();
  for (const rclcpp::TopicEndpointInfo & info : infos) {
    const bool own = info.node_name() == node_name && info.node_namespace() == node_namespace;
    (own ? out.own_node : out.other_nodes).push_back(info.qos_profile().get_rmw_qos_profile());
  }
  return out;
}

Result publisher_profiles(const NodeHandles & handles, const std::string & topic, EndpointProfiles & out)
{
  try {
    out = split_by_node(handles, handles.graph->get_publishers_info_by_topic(topic));
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, "publisher lookup for '" + topic + "' failed: " + e.what());
  }
  return Result::success();
}

Result subscription_profiles(
  const NodeHandles & handles, const std::string & topic, EndpointProfiles & out)
{
  try {
    out = split_by_node(handles, handles.graph->get_subscriptions_info_by_topic(topic));
  } catch (const std::exception & e) {
    return Result::error(
      Status::RclError, "subscription lookup for '" + topic + "' failed: " + e.what());
  }
  return Result::success();
}

}  // namespace

// --- TopicSubscription ------------------------------------------------------

struct TopicSubscription::Impl : public std::enable_shared_from_this<TopicSubscription::Impl>
{
  NodeHandles handles;
  RosUiDiagnosticsSink * sink = nullptr;
  std::string topic;
  std::string type_name;
  MessageTypeSupport ts;
  qar_ui_model::MessageDescriptionPtr description;
  TopicSubscriptionOptions options;

  mutable std::mutex mutex;
  std::shared_ptr<rclcpp::GenericSubscription> subscription;
  rmw_qos_profile_t profile{};
  std::shared_ptr<rclcpp::SerializedMessage> latest;
  std::uint64_t sequence = 0;
  std::uint64_t last_taken = 0;
  TopicMessageCallback callback;
  std::uint64_t messages_converted = 0;
  std::uint64_t conversion_failures = 0;
  std::uint64_t incompatible_qos_events = 0;
  std::uint64_t resubscriptions = 0;
  bool last_truncated = false;
  std::string last_error;

  // Conversion scratch: one deserialize target reused across takes.
  mutable std::mutex convert_mutex;
  mutable DynamicMessage scratch;

  Result create(
    const rmw_qos_profile_t & new_profile, std::shared_ptr<rclcpp::GenericSubscription> & out);
  void on_serialized(std::shared_ptr<rclcpp::SerializedMessage> message);
  bool convert(
    const rclcpp::SerializedMessage & message, UiValue & out, bool & truncated,
    std::string & error) const;
};

Result TopicSubscription::Impl::create(
  const rmw_qos_profile_t & new_profile, std::shared_ptr<rclcpp::GenericSubscription> & out)
{
  std::weak_ptr<Impl> weak = weak_from_this();
  auto on_message = [weak](std::shared_ptr<rclcpp::SerializedMessage> message) {
      if (auto self = weak.lock()) {
        self->on_serialized(std::move(message));
      }
    };
  auto build = [&](bool with_events) -> std::shared_ptr<rclcpp::GenericSubscription> {
      rclcpp::SubscriptionOptions sub_options;
      sub_options.callback_group = handles.callback_group;
      if (with_events) {
        sub_options.event_callbacks.incompatible_qos_callback =
          [weak](rclcpp::QOSRequestedIncompatibleQoSInfo & info) {
            if (auto self = weak.lock()) {
              std::lock_guard<std::mutex> lock(self->mutex);
              ++self->incompatible_qos_events;
              self->last_error = "incompatible QoS: " + std::to_string(info.total_count) +
                " publisher(s) cannot be matched (last policy kind " +
                std::to_string(static_cast<int>(info.last_policy_kind)) + ")";
              report(self->sink, DiagnosticSeverity::Warning, self->topic + ": " + self->last_error);
            }
          };
      }
      auto created = std::make_shared<rclcpp::GenericSubscription>(
        handles.base.get(), ts.rmw_library, topic, type_name, to_qos(new_profile), on_message,
        sub_options);
      handles.topics->add_subscription(created, sub_options.callback_group);
      return created;
    };
  try {
    out = build(true);
    return Result::success();
  } catch (const std::exception & first) {
    // An rmw without QoS events refuses the callback; the subscription itself is what matters.
    try {
      out = build(false);
      report(
        sink, DiagnosticSeverity::Info,
        topic + ": subscribed without QoS event callbacks (" + first.what() + ")");
      return Result::success();
    } catch (const std::exception & e) {
      return Result::error(Status::RclError, "cannot subscribe to '" + topic + "': " + e.what());
    }
  }
}

void TopicSubscription::Impl::on_serialized(std::shared_ptr<rclcpp::SerializedMessage> message)
{
  if (!message) {
    return;
  }
  TopicMessageCallback push;
  std::uint64_t seq = 0;
  {
    std::lock_guard<std::mutex> lock(mutex);
    latest = message;
    seq = ++sequence;
    push = callback;
  }
  if (!push) {
    return;
  }
  UiValue value;
  bool truncated = false;
  std::string error;
  if (convert(*message, value, truncated, error)) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      ++messages_converted;
      last_truncated = truncated;
    }
    push(value, seq);
  } else {
    std::lock_guard<std::mutex> lock(mutex);
    ++conversion_failures;
    last_error = error;
    report(sink, DiagnosticSeverity::Warning, topic + ": " + error);
  }
}

bool TopicSubscription::Impl::convert(
  const rclcpp::SerializedMessage & message, UiValue & out, bool & truncated,
  std::string & error) const
{
  std::lock_guard<std::mutex> lock(convert_mutex);
  if (!scratch.valid()) {
    scratch = DynamicMessage(ts);
    if (!scratch.valid()) {
      error = "cannot instantiate " + type_name;
      return false;
    }
  }
  Result result = scratch.deserialize(message);
  if (!result.ok()) {
    error = result.message;
    return false;
  }
  ConversionReport report;
  out = scratch.to_ui_value(options.limits, &report);
  truncated = !report.clean();
  return true;
}

TopicSubscription::TopicSubscription(std::shared_ptr<Impl> impl)
: impl_(std::move(impl))
{
}

TopicSubscription::~TopicSubscription() = default;

const std::string & TopicSubscription::topic() const {return impl_->topic;}
const std::string & TopicSubscription::type_name() const {return impl_->type_name;}
qar_ui_model::MessageDescriptionPtr TopicSubscription::description() const
{
  return impl_->description;
}

bool TopicSubscription::take_latest(UiValue & out, std::uint64_t * sequence)
{
  std::shared_ptr<rclcpp::SerializedMessage> message;
  std::uint64_t seq = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->latest || impl_->sequence == impl_->last_taken) {
      return false;
    }
    message = impl_->latest;
    seq = impl_->sequence;
    impl_->last_taken = seq;
  }
  bool truncated = false;
  std::string error;
  if (!impl_->convert(*message, out, truncated, error)) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->conversion_failures;
    impl_->last_error = error;
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->messages_converted;
    impl_->last_truncated = truncated;
  }
  if (sequence != nullptr) {
    *sequence = seq;
  }
  return true;
}

bool TopicSubscription::latest(UiValue & out, std::uint64_t * sequence) const
{
  std::shared_ptr<rclcpp::SerializedMessage> message;
  std::uint64_t seq = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->latest) {
      return false;
    }
    message = impl_->latest;
    seq = impl_->sequence;
  }
  bool truncated = false;
  std::string error;
  if (!impl_->convert(*message, out, truncated, error)) {
    return false;
  }
  if (sequence != nullptr) {
    *sequence = seq;
  }
  return true;
}

std::uint64_t TopicSubscription::sequence() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->sequence;
}

void TopicSubscription::set_callback(TopicMessageCallback callback)
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->callback = std::move(callback);
}

rmw_qos_profile_t TopicSubscription::active_profile() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->profile;
}

TopicSubscriptionDiagnostics TopicSubscription::diagnostics() const
{
  TopicSubscriptionDiagnostics out;
  std::shared_ptr<rclcpp::GenericSubscription> subscription;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    out.topic = impl_->topic;
    out.type_name = impl_->type_name;
    out.qos = describe_profile(impl_->profile);
    out.messages_received = impl_->sequence;
    out.messages_converted = impl_->messages_converted;
    out.conversion_failures = impl_->conversion_failures;
    out.incompatible_qos_events = impl_->incompatible_qos_events;
    out.resubscriptions = impl_->resubscriptions;
    out.last_conversion_truncated = impl_->last_truncated;
    out.last_error = impl_->last_error;
    subscription = impl_->subscription;
  }
  if (subscription) {
    try {
      out.publisher_count = subscription->get_publisher_count();
    } catch (const std::exception &) {
      out.publisher_count = 0;
    }
  }
  return out;
}

// --- TopicPublisher ---------------------------------------------------------

struct TopicPublisher::Impl
{
  NodeHandles handles;
  RosUiDiagnosticsSink * sink = nullptr;
  std::string topic;
  std::string type_name;
  MessageTypeSupport ts;
  qar_ui_model::MessageDescriptionPtr description;
  TopicPublisherOptions options;

  mutable std::mutex mutex;
  std::shared_ptr<rclcpp::GenericPublisher> publisher;
  rmw_qos_profile_t profile{};
  std::uint64_t messages_published = 0;
  std::uint64_t conversion_failures = 0;
  std::uint64_t publish_failures = 0;
  std::string last_error;

  Result create(const rmw_qos_profile_t & new_profile, std::shared_ptr<rclcpp::GenericPublisher> & out);
};

Result TopicPublisher::Impl::create(
  const rmw_qos_profile_t & new_profile, std::shared_ptr<rclcpp::GenericPublisher> & out)
{
  try {
    rclcpp::PublisherOptions pub_options;
    pub_options.callback_group = handles.callback_group;
    auto created = std::make_shared<rclcpp::GenericPublisher>(
      handles.base.get(), ts.rmw_library, topic, type_name, to_qos(new_profile), pub_options);
    handles.topics->add_publisher(created, pub_options.callback_group);
    out = created;
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, "cannot advertise '" + topic + "': " + e.what());
  }
  return Result::success();
}

TopicPublisher::TopicPublisher(std::shared_ptr<Impl> impl)
: impl_(std::move(impl))
{
}

TopicPublisher::~TopicPublisher() = default;

const std::string & TopicPublisher::topic() const {return impl_->topic;}
const std::string & TopicPublisher::type_name() const {return impl_->type_name;}
qar_ui_model::MessageDescriptionPtr TopicPublisher::description() const {return impl_->description;}

Result TopicPublisher::publish(const UiValue & value)
{
  DynamicMessage message(impl_->ts);
  if (!message.valid()) {
    return Result::error(Status::InvalidState, "cannot instantiate " + impl_->type_name);
  }
  Result result = message.from_ui_value(value, impl_->options.conversion);
  if (!result.ok()) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->conversion_failures;
    impl_->last_error = result.message;
    return result;
  }
  rclcpp::SerializedMessage serialized;
  result = message.serialize(serialized);
  if (!result.ok()) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->conversion_failures;
    impl_->last_error = result.message;
    return result;
  }
  std::shared_ptr<rclcpp::GenericPublisher> publisher;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    publisher = impl_->publisher;
  }
  if (!publisher) {
    return Result::error(Status::InvalidState, "publisher for '" + impl_->topic + "' is gone");
  }
  try {
    publisher->publish(serialized);
  } catch (const std::exception & e) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ++impl_->publish_failures;
    impl_->last_error = e.what();
    return Result::error(Status::RclError, "publish on '" + impl_->topic + "' failed: " + e.what());
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  ++impl_->messages_published;
  return Result::success();
}

std::size_t TopicPublisher::subscription_count() const
{
  std::shared_ptr<rclcpp::GenericPublisher> publisher;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    publisher = impl_->publisher;
  }
  if (!publisher) {
    return 0;
  }
  try {
    return publisher->get_subscription_count();
  } catch (const std::exception &) {
    return 0;
  }
}

rmw_qos_profile_t TopicPublisher::active_profile() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->profile;
}

TopicPublisherDiagnostics TopicPublisher::diagnostics() const
{
  TopicPublisherDiagnostics out;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    out.topic = impl_->topic;
    out.type_name = impl_->type_name;
    out.qos = describe_profile(impl_->profile);
    out.messages_published = impl_->messages_published;
    out.conversion_failures = impl_->conversion_failures;
    out.publish_failures = impl_->publish_failures;
    out.last_error = impl_->last_error;
  }
  out.subscription_count = subscription_count();
  return out;
}

// --- TopicBridge ------------------------------------------------------------

struct TopicBridge::Impl
{
  NodeHandles handles;
  TypeSupportRegistry * registry = nullptr;
  RosUiDiagnosticsSink * sink = nullptr;
  mutable std::mutex mutex;
  std::vector<std::weak_ptr<TopicSubscription::Impl>> subscriptions;
  std::vector<std::weak_ptr<TopicPublisher::Impl>> publishers;

  template<typename T>
  static std::vector<std::shared_ptr<T>> collect_live(std::vector<std::weak_ptr<T>> & weak_list)
  {
    std::vector<std::shared_ptr<T>> live;
    weak_list.erase(
      std::remove_if(
        weak_list.begin(), weak_list.end(),
        [&live](const std::weak_ptr<T> & weak) {
          if (auto strong = weak.lock()) {
            live.push_back(strong);
            return false;
          }
          return true;
        }),
      weak_list.end());
    return live;
  }
};

TopicBridge::TopicBridge(NodeHandles handles, TypeSupportRegistry * registry, RosUiDiagnosticsSink * sink)
: impl_(std::make_shared<Impl>())
{
  impl_->handles = std::move(handles);
  impl_->registry = registry ? registry : &TypeSupportRegistry::global();
  impl_->sink = sink;
}

TopicBridge::~TopicBridge() = default;

Result TopicBridge::resolve_topic_type(const std::string & topic, std::string & out_type) const
{
  if (!impl_->handles.valid()) {
    return Result::error(Status::InvalidState, "node handles are empty");
  }
  try {
    const std::string resolved = impl_->handles.base->resolve_topic_or_service_name(topic, false);
    const auto names_and_types = impl_->handles.graph->get_topic_names_and_types();
    const auto it = names_and_types.find(resolved);
    if (it == names_and_types.end() || it->second.empty()) {
      return Result::error(Status::NotFound, "topic '" + resolved + "' is not on the graph");
    }
    std::vector<std::string> types = it->second;
    std::sort(types.begin(), types.end());
    if (types.size() > 1) {
      report(
        impl_->sink, DiagnosticSeverity::Warning,
        resolved + " carries " + std::to_string(types.size()) + " types; using " + types.front());
    }
    out_type = types.front();
  } catch (const std::exception & e) {
    return Result::error(Status::RclError, "topic lookup for '" + topic + "' failed: " + e.what());
  }
  return Result::success();
}

Result TopicBridge::subscribe(
  const std::string & topic,
  const std::string & type_name,
  const TopicSubscriptionOptions & options,
  std::shared_ptr<TopicSubscription> & out,
  TopicMessageCallback callback)
{
  if (!impl_->handles.valid()) {
    return Result::error(Status::InvalidState, "node handles are empty");
  }
  if (topic.empty()) {
    return Result::error(Status::InvalidArgument, "topic name is empty");
  }
  std::string type = type_name;
  if (type.empty()) {
    Result resolved = resolve_topic_type(topic, type);
    if (!resolved.ok()) {
      return resolved;
    }
  }
  MessageTypeSupport ts;
  Result result = impl_->registry->message(type, ts);
  if (!result.ok()) {
    return result;
  }
  const MessageMembers * members = ts.members();
  if (members == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "no introspection table for " + type);
  }

  auto impl = std::make_shared<TopicSubscription::Impl>();
  impl->handles = impl_->handles;
  impl->sink = impl_->sink;
  impl->topic = topic;
  impl->type_name = ts.type_name;
  impl->ts = ts;
  impl->description = describe_message(*members);
  impl->options = options;
  impl->callback = std::move(callback);

  EndpointProfiles publishers;
  if (options.qos == QosPreset::Adaptive) {
    Result lookup = publisher_profiles(impl_->handles, topic, publishers);
    if (!lookup.ok()) {
      report(impl_->sink, DiagnosticSeverity::Warning, lookup.message);
    }
  }
  impl->profile = choose_subscription_profile(publishers.all(), options.qos, options.depth);

  std::shared_ptr<rclcpp::GenericSubscription> subscription;
  result = impl->create(impl->profile, subscription);
  if (!result.ok()) {
    return result;
  }
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->subscription = subscription;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::collect_live(impl_->subscriptions);
    impl_->subscriptions.push_back(impl);
  }
  out = std::shared_ptr<TopicSubscription>(new TopicSubscription(impl));
  report(
    impl_->sink, DiagnosticSeverity::Info,
    "subscribed to " + topic + " [" + ts.type_name + "] as " + describe_profile(impl->profile));
  return Result::success();
}

Result TopicBridge::advertise(
  const std::string & topic,
  const std::string & type_name,
  const TopicPublisherOptions & options,
  std::shared_ptr<TopicPublisher> & out)
{
  if (!impl_->handles.valid()) {
    return Result::error(Status::InvalidState, "node handles are empty");
  }
  if (topic.empty() || type_name.empty()) {
    return Result::error(Status::InvalidArgument, "topic name and type name are required");
  }
  MessageTypeSupport ts;
  Result result = impl_->registry->message(type_name, ts);
  if (!result.ok()) {
    return result;
  }
  const MessageMembers * members = ts.members();
  if (members == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "no introspection table for " + type_name);
  }

  auto impl = std::make_shared<TopicPublisher::Impl>();
  impl->handles = impl_->handles;
  impl->sink = impl_->sink;
  impl->topic = topic;
  impl->type_name = ts.type_name;
  impl->ts = ts;
  impl->description = describe_message(*members);
  impl->options = options;

  EndpointProfiles subscriptions;
  EndpointProfiles publishers;
  if (options.qos == QosPreset::Adaptive) {
    (void)subscription_profiles(impl_->handles, topic, subscriptions);
    (void)publisher_profiles(impl_->handles, topic, publishers);
  }
  impl->profile = choose_publisher_profile(
    subscriptions.all(), publishers.other_nodes, options.qos, options.depth);

  std::shared_ptr<rclcpp::GenericPublisher> publisher;
  result = impl->create(impl->profile, publisher);
  if (!result.ok()) {
    return result;
  }
  {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->publisher = publisher;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Impl::collect_live(impl_->publishers);
    impl_->publishers.push_back(impl);
  }
  out = std::shared_ptr<TopicPublisher>(new TopicPublisher(impl));
  report(
    impl_->sink, DiagnosticSeverity::Info,
    "advertising " + topic + " [" + ts.type_name + "] as " + describe_profile(impl->profile));
  return Result::success();
}

std::size_t TopicBridge::reconcile()
{
  std::vector<std::shared_ptr<TopicSubscription::Impl>> subscriptions;
  std::vector<std::shared_ptr<TopicPublisher::Impl>> publishers;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    subscriptions = Impl::collect_live(impl_->subscriptions);
    publishers = Impl::collect_live(impl_->publishers);
  }
  std::size_t changed = 0;

  for (const auto & sub : subscriptions) {
    if (sub->options.qos != QosPreset::Adaptive) {
      continue;
    }
    EndpointProfiles endpoints;
    if (!publisher_profiles(impl_->handles, sub->topic, endpoints).ok()) {
      continue;
    }
    const rmw_qos_profile_t wanted =
      choose_subscription_profile(endpoints.all(), QosPreset::Adaptive, sub->options.depth);
    rmw_qos_profile_t current;
    {
      std::lock_guard<std::mutex> lock(sub->mutex);
      current = sub->profile;
    }
    if (same_matching_policies(wanted, current)) {
      continue;
    }
    std::shared_ptr<rclcpp::GenericSubscription> fresh;
    Result result = sub->create(wanted, fresh);
    if (!result.ok()) {
      report(impl_->sink, DiagnosticSeverity::Warning, "re-subscribe failed: " + result.message);
      continue;
    }
    std::shared_ptr<rclcpp::GenericSubscription> old;
    {
      std::lock_guard<std::mutex> lock(sub->mutex);
      old = std::move(sub->subscription);
      sub->subscription = fresh;
      sub->profile = wanted;
      ++sub->resubscriptions;
    }
    old.reset();
    ++changed;
    report(
      impl_->sink, DiagnosticSeverity::Info,
      "re-subscribed to " + sub->topic + " as " + describe_profile(wanted) + " (was " +
      describe_profile(current) + ")");
  }

  for (const auto & pub : publishers) {
    if (pub->options.qos != QosPreset::Adaptive) {
      continue;
    }
    EndpointProfiles subscriptions_on_topic;
    EndpointProfiles publishers_on_topic;
    if (!subscription_profiles(impl_->handles, pub->topic, subscriptions_on_topic).ok() ||
      !publisher_profiles(impl_->handles, pub->topic, publishers_on_topic).ok())
    {
      continue;
    }
    const rmw_qos_profile_t wanted = choose_publisher_profile(
      subscriptions_on_topic.all(), publishers_on_topic.other_nodes, QosPreset::Adaptive,
      pub->options.depth);
    rmw_qos_profile_t current;
    {
      std::lock_guard<std::mutex> lock(pub->mutex);
      current = pub->profile;
    }
    if (same_matching_policies(wanted, current)) {
      continue;
    }
    std::shared_ptr<rclcpp::GenericPublisher> fresh;
    Result result = pub->create(wanted, fresh);
    if (!result.ok()) {
      report(impl_->sink, DiagnosticSeverity::Warning, "re-advertise failed: " + result.message);
      continue;
    }
    std::shared_ptr<rclcpp::GenericPublisher> old;
    {
      std::lock_guard<std::mutex> lock(pub->mutex);
      old = std::move(pub->publisher);
      pub->publisher = fresh;
      pub->profile = wanted;
    }
    old.reset();
    ++changed;
    report(
      impl_->sink, DiagnosticSeverity::Info,
      "re-advertised " + pub->topic + " as " + describe_profile(wanted) + " (was " +
      describe_profile(current) + ")");
  }
  return changed;
}

std::size_t TopicBridge::subscription_count() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return Impl::collect_live(impl_->subscriptions).size();
}

std::size_t TopicBridge::publisher_count() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return Impl::collect_live(impl_->publishers).size();
}

}  // namespace qar_ros_ui
