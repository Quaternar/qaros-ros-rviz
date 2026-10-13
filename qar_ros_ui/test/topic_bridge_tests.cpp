// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#ifdef QAR_ROS_UI_LIVE_TESTS

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "test_msgs/msg/unbounded_sequences.hpp"

#include "qar_ros_ui/topic_bridge.hpp"

#include "test_support.hpp"

using geometry_msgs::msg::PoseStamped;
using qar_ros_ui::QosPreset;
using qar_ros_ui::Result;
using qar_ros_ui::Status;
using qar_ros_ui::TopicBridge;
using qar_ros_ui::TopicPublisher;
using qar_ros_ui::TopicPublisherOptions;
using qar_ros_ui::TopicSubscription;
using qar_ros_ui::TopicSubscriptionOptions;
using qar_ui_model::UiValue;
using qar_ros_ui_test::at;
using qar_ros_ui_test::json;
using qar_ros_ui_test::TestNode;
using qar_ros_ui_test::unique_name;

namespace
{

class RecordingSink : public qar_ros_ui::RosUiDiagnosticsSink
{
public:
  void on_message(qar_ros_ui::DiagnosticSeverity, const std::string & category, const std::string & message) override
  {
    messages.push_back(category + ": " + message);
  }
  std::vector<std::string> messages;
};

PoseStamped pose(const std::string & frame, double x)
{
  PoseStamped message;
  message.header.frame_id = frame;
  message.pose.position.x = x;
  message.pose.orientation.w = 1.0;
  return message;
}

/// Publishes @p message on every spin until @p subscription has taken one.
bool publish_until_received(
  TestNode & node, rclcpp::Publisher<PoseStamped> & publisher, const PoseStamped & message,
  TopicSubscription & subscription, UiValue & out)
{
  return node.spin_until(
    [&]() {
      publisher.publish(message);
      return subscription.take_latest(out);
    });
}

}  // namespace

TEST(TopicBridgeLive, AdaptiveSubscriptionHearsABestEffortPublisher)
{
  TestNode node;
  RecordingSink sink;
  TopicBridge bridge(node.handles(), nullptr, &sink);
  const std::string topic = unique_name("scan");
  auto publisher = node.node()->create_publisher<PoseStamped>(topic, rclcpp::SensorDataQoS());
  ASSERT_TRUE(node.spin_until([&]() {return node.node()->get_publishers_info_by_topic(topic).size() == 1;}));

  std::shared_ptr<TopicSubscription> subscription;
  const Result result = bridge.subscribe(topic, "", TopicSubscriptionOptions{}, subscription);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_NE(subscription, nullptr);
  EXPECT_EQ(subscription->type_name(), "geometry_msgs/msg/PoseStamped");
  EXPECT_EQ(subscription->active_profile().reliability, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
  ASSERT_NE(subscription->description(), nullptr);
  EXPECT_EQ(subscription->description()->type_name, "geometry_msgs/msg/PoseStamped");

  UiValue value;
  ASSERT_TRUE(publish_until_received(node, *publisher, pose("laser", 3.5), *subscription, value));
  EXPECT_EQ(*at(value, "header.frame_id"), UiValue("laser"));
  EXPECT_EQ(*at(value, "pose.position.x"), UiValue(3.5));
  EXPECT_GE(subscription->sequence(), 1u);
  EXPECT_FALSE(subscription->take_latest(value));  // nothing newer than the take
  UiValue peek;
  EXPECT_TRUE(subscription->latest(peek));
  EXPECT_EQ(peek, value);
  const auto diagnostics = subscription->diagnostics();
  EXPECT_EQ(diagnostics.qos, "best_effort/volatile/keep_last(10)");
  EXPECT_EQ(diagnostics.incompatible_qos_events, 0u);
  EXPECT_GE(diagnostics.messages_received, 1u);
  EXPECT_EQ(bridge.subscription_count(), 1u);
  EXPECT_FALSE(sink.messages.empty());
}

TEST(TopicBridgeLive, AdaptiveSubscriptionIsReliableForAReliablePublisher)
{
  TestNode node;
  TopicBridge bridge(node.handles());
  const std::string topic = unique_name("odom");
  auto publisher = node.node()->create_publisher<PoseStamped>(topic, rclcpp::QoS(10).reliable());
  ASSERT_TRUE(node.spin_until([&]() {return node.node()->get_publishers_info_by_topic(topic).size() == 1;}));

  std::shared_ptr<TopicSubscription> subscription;
  ASSERT_TRUE(bridge.subscribe(topic, "geometry_msgs/msg/PoseStamped", TopicSubscriptionOptions{}, subscription).ok());
  EXPECT_EQ(subscription->active_profile().reliability, RMW_QOS_POLICY_RELIABILITY_RELIABLE);
  UiValue value;
  ASSERT_TRUE(publish_until_received(node, *publisher, pose("odom", -1.0), *subscription, value));
  EXPECT_EQ(*at(value, "pose.position.x"), UiValue(-1.0));
}

TEST(TopicBridgeLive, LatchedMessageArrivesThroughTransientLocal)
{
  TestNode node;
  TopicBridge bridge(node.handles());
  const std::string topic = unique_name("map");
  auto publisher = node.node()->create_publisher<PoseStamped>(topic, rclcpp::QoS(1).transient_local());
  publisher->publish(pose("map", 42.0));
  ASSERT_TRUE(node.spin_until([&]() {return node.node()->get_publishers_info_by_topic(topic).size() == 1;}));

  std::shared_ptr<TopicSubscription> subscription;
  ASSERT_TRUE(bridge.subscribe(topic, "", TopicSubscriptionOptions{}, subscription).ok());
  EXPECT_EQ(subscription->active_profile().durability, RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
  UiValue value;
  ASSERT_TRUE(node.spin_until([&]() {return subscription->take_latest(value);}));
  EXPECT_EQ(*at(value, "pose.position.x"), UiValue(42.0));
}

TEST(TopicBridgeLive, ReconcileResubscribesWhenAPublisherAppears)
{
  TestNode node;
  RecordingSink sink;
  TopicBridge bridge(node.handles(), nullptr, &sink);
  const std::string topic = unique_name("late");

  std::shared_ptr<TopicSubscription> subscription;
  ASSERT_TRUE(bridge.subscribe(topic, "geometry_msgs/msg/PoseStamped", TopicSubscriptionOptions{}, subscription).ok());
  EXPECT_EQ(subscription->active_profile().reliability, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
  EXPECT_EQ(bridge.reconcile(), 0u);

  auto publisher = node.node()->create_publisher<PoseStamped>(topic, rclcpp::QoS(10).reliable());
  ASSERT_TRUE(node.spin_until([&]() {return node.node()->get_publishers_info_by_topic(topic).size() == 1;}));
  EXPECT_EQ(bridge.reconcile(), 1u);
  EXPECT_EQ(subscription->active_profile().reliability, RMW_QOS_POLICY_RELIABILITY_RELIABLE);
  EXPECT_EQ(subscription->diagnostics().resubscriptions, 1u);
  EXPECT_EQ(bridge.reconcile(), 0u);

  UiValue value;
  ASSERT_TRUE(publish_until_received(node, *publisher, pose("late", 7.0), *subscription, value));
  EXPECT_EQ(*at(value, "header.frame_id"), UiValue("late"));
  bool announced = false;
  for (const std::string & message : sink.messages) {
    announced = announced || message.find("re-subscribed") != std::string::npos;
  }
  EXPECT_TRUE(announced);
}

TEST(TopicBridgeLive, PushCallbackRunsOnTheExecutorThread)
{
  TestNode node;
  TopicBridge bridge(node.handles());
  const std::string topic = unique_name("push");
  auto publisher = node.node()->create_publisher<PoseStamped>(topic, 10);
  ASSERT_TRUE(node.spin_until([&]() {return node.node()->get_publishers_info_by_topic(topic).size() == 1;}));

  std::atomic<int> delivered{0};
  std::uint64_t last_sequence = 0;
  double last_x = 0.0;
  std::shared_ptr<TopicSubscription> subscription;
  ASSERT_TRUE(
    bridge.subscribe(
      topic, "", TopicSubscriptionOptions{}, subscription,
      [&](const UiValue & value, std::uint64_t sequence) {
        ++delivered;
        last_sequence = sequence;
        last_x = at(value, "pose.position.x")->as_double();
      }).ok());
  ASSERT_TRUE(node.spin_until([&]() {return publisher->get_subscription_count() == 1;}));
  for (int i = 1; i <= 3; ++i) {
    publisher->publish(pose("push", i));
  }
  ASSERT_TRUE(node.spin_until([&]() {return delivered >= 3;}));
  EXPECT_EQ(last_sequence, subscription->sequence());
  EXPECT_EQ(last_x, 3.0);
  EXPECT_GE(subscription->diagnostics().messages_converted, 3u);
  subscription->set_callback(nullptr);
}

TEST(TopicBridgeLive, PublishFromUiValueReachesATypedSubscriber)
{
  TestNode node;
  TopicBridge bridge(node.handles());
  const std::string topic = unique_name("goal");
  PoseStamped received;
  std::atomic<int> count{0};
  auto typed = node.node()->create_subscription<PoseStamped>(
    topic, 10, [&](PoseStamped::ConstSharedPtr message) {received = *message; ++count;});

  std::shared_ptr<TopicPublisher> publisher;
  const Result result = bridge.advertise(topic, "geometry_msgs/msg/PoseStamped", TopicPublisherOptions{}, publisher);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(publisher->active_profile().reliability, RMW_QOS_POLICY_RELIABILITY_RELIABLE);
  EXPECT_EQ(publisher->active_profile().durability, RMW_QOS_POLICY_DURABILITY_VOLATILE);
  ASSERT_TRUE(node.spin_until([&]() {return publisher->subscription_count() == 1;}));

  const UiValue value = json(R"({"header":{"frame_id":"map"},"pose":{"position":{"x":1.5,"y":2.5},"orientation":{"w":1.0}}})");
  ASSERT_TRUE(
    node.spin_until(
      [&]() {
        EXPECT_TRUE(publisher->publish(value).ok());
        return count > 0;
      }));
  EXPECT_EQ(received.header.frame_id, "map");
  EXPECT_DOUBLE_EQ(received.pose.position.x, 1.5);
  EXPECT_DOUBLE_EQ(received.pose.position.y, 2.5);
  EXPECT_DOUBLE_EQ(received.pose.orientation.w, 1.0);
  EXPECT_GE(publisher->diagnostics().messages_published, 1u);
  EXPECT_EQ(bridge.publisher_count(), 1u);

  const Result bad = publisher->publish(json(R"({"header":{"frame_id":5}})"));
  EXPECT_EQ(bad.status, Status::ConversionFailed);
  EXPECT_EQ(publisher->diagnostics().conversion_failures, 1u);
}

TEST(TopicBridgeLive, PublisherFollowsATransientLocalSubscriber)
{
  TestNode node;
  TopicBridge bridge(node.handles());
  const std::string topic = unique_name("latched");
  auto typed = node.node()->create_subscription<PoseStamped>(
    topic, rclcpp::QoS(1).transient_local(), [](PoseStamped::ConstSharedPtr) {});
  ASSERT_TRUE(node.spin_until([&]() {return node.node()->get_subscriptions_info_by_topic(topic).size() == 1;}));
  std::shared_ptr<TopicPublisher> publisher;
  ASSERT_TRUE(bridge.advertise(topic, "geometry_msgs/msg/PoseStamped", TopicPublisherOptions{}, publisher).ok());
  EXPECT_EQ(publisher->active_profile().durability, RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
}

TEST(TopicBridgeLive, ErrorsAreReportedNotThrown)
{
  TestNode node;
  TopicBridge bridge(node.handles());
  std::shared_ptr<TopicSubscription> subscription;
  EXPECT_EQ(bridge.subscribe(unique_name("nobody"), "", TopicSubscriptionOptions{}, subscription).status, Status::NotFound);
  EXPECT_EQ(bridge.subscribe("", "geometry_msgs/msg/Pose", TopicSubscriptionOptions{}, subscription).status, Status::InvalidArgument);
  EXPECT_EQ(
    bridge.subscribe(unique_name("x"), "nope/msg/Nope", TopicSubscriptionOptions{}, subscription).status,
    Status::TypeSupportUnavailable);
  std::shared_ptr<TopicPublisher> publisher;
  EXPECT_EQ(bridge.advertise(unique_name("x"), "", TopicPublisherOptions{}, publisher).status, Status::InvalidArgument);
  EXPECT_EQ(bridge.subscribe("not a valid topic!", "geometry_msgs/msg/Pose", TopicSubscriptionOptions{}, subscription).status, Status::RclError);
  TopicBridge empty(qar_ros_ui::NodeHandles{});
  EXPECT_EQ(empty.subscribe("/x", "geometry_msgs/msg/Pose", TopicSubscriptionOptions{}, subscription).status, Status::InvalidState);
}

TEST(TopicBridgeLive, TruncationIsReportedAndSubscriptionsAreRaii)
{
  TestNode node;
  TopicBridge bridge(node.handles());
  const std::string topic = unique_name("wide");
  auto publisher = node.node()->create_publisher<test_msgs::msg::UnboundedSequences>(topic, 10);
  ASSERT_TRUE(node.spin_until([&]() {return node.node()->get_publishers_info_by_topic(topic).size() == 1;}));

  TopicSubscriptionOptions options;
  options.limits.max_array_elements = 4;
  std::shared_ptr<TopicSubscription> subscription;
  ASSERT_TRUE(bridge.subscribe(topic, "", options, subscription).ok());
  test_msgs::msg::UnboundedSequences message;
  for (int i = 0; i < 10; ++i) {
    message.int32_values.push_back(i);
  }
  UiValue value;
  ASSERT_TRUE(
    node.spin_until(
      [&]() {
        publisher->publish(message);
        return subscription->take_latest(value);
      }));
  EXPECT_EQ(at(value, "int32_values")->size(), 4u);
  EXPECT_TRUE(subscription->diagnostics().last_conversion_truncated);

  EXPECT_EQ(bridge.subscription_count(), 1u);
  subscription.reset();
  EXPECT_EQ(bridge.subscription_count(), 0u);
  ASSERT_TRUE(node.spin_until([&]() {return publisher->get_subscription_count() == 0;}));
}

#endif  // QAR_ROS_UI_LIVE_TESTS
