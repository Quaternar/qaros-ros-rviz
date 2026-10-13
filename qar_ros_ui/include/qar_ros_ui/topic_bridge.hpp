// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__TOPIC_BRIDGE_HPP_
#define QAR_ROS_UI__TOPIC_BRIDGE_HPP_

/// @file topic_bridge.hpp
/// @brief Generic subscribe (-> UiValue) and generic publish (<- UiValue).
///
/// A subscription keeps the **latest serialized message** in a mailbox and
/// converts it on demand: `take_latest()` from the UI's own thread costs one
/// deserialize + one conversion per *display* refresh, not per received
/// message, and nothing is converted for a topic nobody is looking at. A
/// push callback exists for hosts that want every message; it runs on the
/// thread that spins the node (in RViz that is the Qt GUI thread, via
/// `VisualizationManager`'s 10 ms `spin_some`), so it must not block.
///
/// QoS: `Adaptive` (the default) derives the profile from the publishers that
/// exist, and `TopicBridge::reconcile()` re-derives it — a subscription is
/// silently re-created when the answer changes, so a BEST_EFFORT sensor that
/// starts after the UI still gets through. Subscriptions and publishers are
/// RAII: drop the shared_ptr and the entity leaves the node.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "rmw/types.h"

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_value.hpp"

#include "qar_ros_ui/node_handles.hpp"
#include "qar_ros_ui/qos_selection.hpp"
#include "qar_ros_ui/type_support_registry.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

struct TopicSubscriptionOptions
{
  QosPreset qos = QosPreset::Adaptive;
  std::size_t depth = 10;
  ConversionLimits limits;
};

/// @param value the converted message; @param sequence 1-based receive count.
using TopicMessageCallback =
  std::function<void(const qar_ui_model::UiValue & value, std::uint64_t sequence)>;

struct TopicSubscriptionDiagnostics
{
  std::string topic;
  std::string type_name;
  std::string qos;  ///< describe_profile() of the active profile.
  std::uint64_t messages_received = 0;
  std::uint64_t messages_converted = 0;
  std::uint64_t conversion_failures = 0;
  std::uint64_t incompatible_qos_events = 0;
  std::uint64_t resubscriptions = 0;
  std::size_t publisher_count = 0;
  bool last_conversion_truncated = false;
  std::string last_error;
};

class TopicSubscription
{
public:
  ~TopicSubscription();
  TopicSubscription(const TopicSubscription &) = delete;
  TopicSubscription & operator=(const TopicSubscription &) = delete;

  const std::string & topic() const;
  const std::string & type_name() const;
  qar_ui_model::MessageDescriptionPtr description() const;

  /// The newest message, converted now, if one arrived since the last take.
  bool take_latest(qar_ui_model::UiValue & out, std::uint64_t * sequence = nullptr);
  /// The newest message whether or not it was taken; false before the first.
  bool latest(qar_ui_model::UiValue & out, std::uint64_t * sequence = nullptr) const;
  /// Messages received so far (the sequence of the newest one).
  std::uint64_t sequence() const;

  /// Push delivery on the executor thread. Null clears.
  void set_callback(TopicMessageCallback callback);

  rmw_qos_profile_t active_profile() const;
  TopicSubscriptionDiagnostics diagnostics() const;

private:
  friend class TopicBridge;
  struct Impl;
  explicit TopicSubscription(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

struct TopicPublisherOptions
{
  QosPreset qos = QosPreset::Adaptive;
  std::size_t depth = 10;
  ConversionOptions conversion;
};

struct TopicPublisherDiagnostics
{
  std::string topic;
  std::string type_name;
  std::string qos;
  std::uint64_t messages_published = 0;
  std::uint64_t conversion_failures = 0;
  std::uint64_t publish_failures = 0;
  std::size_t subscription_count = 0;
  std::string last_error;
};

class TopicPublisher
{
public:
  ~TopicPublisher();
  TopicPublisher(const TopicPublisher &) = delete;
  TopicPublisher & operator=(const TopicPublisher &) = delete;

  const std::string & topic() const;
  const std::string & type_name() const;
  qar_ui_model::MessageDescriptionPtr description() const;

  /// Convert, serialize, publish. Fields absent from @p value take the type's defaults.
  Result publish(const qar_ui_model::UiValue & value);

  std::size_t subscription_count() const;
  rmw_qos_profile_t active_profile() const;
  TopicPublisherDiagnostics diagnostics() const;

private:
  friend class TopicBridge;
  struct Impl;
  explicit TopicPublisher(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
};

class TopicBridge
{
public:
  /// @param registry null = TypeSupportRegistry::global(). @param sink optional.
  explicit TopicBridge(
    NodeHandles handles,
    TypeSupportRegistry * registry = nullptr,
    RosUiDiagnosticsSink * sink = nullptr);
  ~TopicBridge();
  TopicBridge(const TopicBridge &) = delete;
  TopicBridge & operator=(const TopicBridge &) = delete;

  /// @param type_name "" = look the topic up on the graph (NotFound if absent).
  Result subscribe(
    const std::string & topic,
    const std::string & type_name,
    const TopicSubscriptionOptions & options,
    std::shared_ptr<TopicSubscription> & out,
    TopicMessageCallback callback = nullptr);

  Result advertise(
    const std::string & topic,
    const std::string & type_name,
    const TopicPublisherOptions & options,
    std::shared_ptr<TopicPublisher> & out);

  /// First type published on @p topic right now.
  Result resolve_topic_type(const std::string & topic, std::string & out_type) const;

  /// Re-derive every Adaptive profile from the current endpoints and re-create
  /// the entities whose profile changed. Cheap when nothing changed; call it
  /// from a periodic tick (once a second is plenty). Returns how many changed.
  std::size_t reconcile();

  std::size_t subscription_count() const;
  std::size_t publisher_count() const;

private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__TOPIC_BRIDGE_HPP_
