// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for qar_ros_ui: graph discovery, introspection, dynamic messages and the bridges for topics,
// services and actions. The functions are compiled and linked and never run. Their parameters stand for what the
// host owns: a node that its executor spins.

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_value.hpp"

#include "qar_ros_ui/qar_ros_ui.hpp"

#include "rclcpp/rclcpp.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <future>
#include <memory>

namespace ui = qar_ui_model;
namespace ros_ui = qar_ros_ui;

void snippet_graph(const std::shared_ptr<rclcpp::Node> & node)
{
  // [qar-snippet:graph]
  const ros_ui::NodeHandles handles = ros_ui::NodeHandles::from(node);   // any node type: the library never names rclcpp::Node
  ros_ui::GraphDiscovery discovery(handles);
  ros_ui::GraphDelta delta;
  const ros_ui::Result refreshed = discovery.refresh(&delta);          // one pass, which never waits for discovery
  if (refreshed.ok() && !delta.empty()) {                              // the revision grows only when something changed
    for (const ros_ui::GraphEntity & topic : discovery.current().topics) {
      std::printf("%s [%s]\n", topic.name.c_str(), topic.primary_type().c_str());
    }
    // An action is recovered from its hidden services and topics.
    const ros_ui::GraphEntity * action = discovery.current().find_action("/fib");
    std::printf("%s\n", action != nullptr ? action->primary_type().c_str() : "no such action");
  }
  // [qar-snippet-end:graph]
}

void snippet_introspection()
{
  // [qar-snippet:introspection]
  ui::MessageDescriptionPtr description;
  const ros_ui::Result found = ros_ui::introspect_message("geometry_msgs/msg/Pose", description);   // loads the typesupport library
  if (!found.ok()) {
    std::printf("%s: %s\n", ros_ui::to_string(found.status), found.message.c_str());
    return;
  }
  const ui::UiValue defaults = ui::make_default_value(*description);   // what a fresh message holds

  ros_ui::DynamicMessage message;                                      // an instance without a C++ type
  if (ros_ui::DynamicMessage::create("geometry_msgs/msg/Pose", message).ok()) {
    const ros_ui::Result converted = message.from_ui_value(defaults);  // atomic: a failure leaves the instance as it was
    rclcpp::SerializedMessage bytes;
    if (converted.ok() && message.serialize(bytes).ok()) {             // CDR, with the handle of the rmw layer
      const ui::UiValue again = message.to_ui_value();                 // bounded by ConversionLimits
      std::printf("%zu bytes, %zu fields\n", bytes.size(), again.size());
    }
  }
  // [qar-snippet-end:introspection]
}

void snippet_topics(const std::shared_ptr<rclcpp::Node> & node)
{
  // [qar-snippet:topics]
  ros_ui::TopicBridge bridge(ros_ui::NodeHandles::from(node));
  std::shared_ptr<ros_ui::TopicSubscription> subscription;
  const ros_ui::Result subscribed =
    bridge.subscribe("/odom", "", ros_ui::TopicSubscriptionOptions{}, subscription);   // "": look the type up on the graph
  std::shared_ptr<ros_ui::TopicPublisher> publisher;
  const ros_ui::Result advertised =
    bridge.advertise("/cmd_vel", "geometry_msgs/msg/Twist", ros_ui::TopicPublisherOptions{}, publisher);
  if (!subscribed.ok() || !advertised.ok()) {
    return;
  }

  ui::UiValue message;
  std::uint64_t sequence = 0;
  if (subscription->take_latest(message, &sequence)) {                 // true once for each newest message
    std::printf("message %llu\n", static_cast<unsigned long long>(sequence));
  }
  const ui::UiValue twist = ui::UiValue::object().set("linear", ui::UiValue::object().set("x", 0.2));
  publisher->publish(twist);                                           // an absent field takes its default
  bridge.reconcile();                                                  // about once a second: follows publishers that come and go
  // [qar-snippet-end:topics]
}

void snippet_service(const std::shared_ptr<rclcpp::Node> & node)
{
  // [qar-snippet:service]
  ros_ui::ServiceBridge bridge(ros_ui::NodeHandles::from(node));
  ros_ui::ServiceCallOptions options;                                  // 5 s timeout, and wait for a server that is not up yet
  options.timeout = std::chrono::milliseconds(2000);
  const ui::UiValue request = ui::UiValue::object().set("data", true);
  const ros_ui::Result queued = bridge.call("/set_bool", "std_srvs/srv/SetBool", request, options,
    [](const ros_ui::ServiceCallOutcome & outcome) {                   // exactly once, on the thread that spins the node
      std::printf("%s: %s\n", outcome.service.c_str(), outcome.ok() ? "ok" : outcome.status.message.c_str());
    });
  std::shared_future<ros_ui::ServiceCallOutcome> pending =
    bridge.call_async("/set_bool", "std_srvs/srv/SetBool", request, options);   // the same with a future; nothing waits on it
  std::printf("%s, %zu pending\n", queued.ok() ? "queued" : queued.message.c_str(), bridge.pending_count());
  bridge.poll();                                                       // only for a host that spins no timers
  (void)pending;
  // [qar-snippet-end:service]
}

void snippet_action(const std::shared_ptr<rclcpp::Node> & node)
{
  // [qar-snippet:action]
  ros_ui::ActionBridge bridge(ros_ui::NodeHandles::from(node));
  ros_ui::ActionGoalOptions options;
  options.response_timeout = std::chrono::milliseconds(3000);          // for the answer of the server; the execution has none
  options.on_event = [](const std::shared_ptr<ros_ui::ActionGoalHandle> & handle, const ros_ui::GoalEvent & event) {
    std::printf("%s: %s\n", handle->goal_id_string().c_str(), ros_ui::to_string(event.kind));   // Accepted, Feedback, Result ...
  };
  std::shared_ptr<ros_ui::ActionGoalHandle> goal;
  const ros_ui::Result sent =
    bridge.send_goal("/fib", "", ui::UiValue::object().set("order", 8), options, goal);
  if (sent.ok()) {
    ui::UiValue feedback;
    const bool news = goal->take_feedback(feedback);                   // the newest feedback since the last take
    std::printf("%s, %s\n", ros_ui::to_string(goal->status()), news || goal->finished() ? "news" : "quiet");
    bridge.cancel(goal);                                               // the answer arrives as a CancelResponse event
  }
  // [qar-snippet-end:action]
}
