// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#ifdef QAR_ROS_UI_LIVE_TESTS

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "std_srvs/srv/set_bool.hpp"
#include "test_msgs/srv/basic_types.hpp"

#include "qar_ros_ui/service_bridge.hpp"

#include "test_support.hpp"

using qar_ros_ui::Result;
using qar_ros_ui::ServiceBridge;
using qar_ros_ui::ServiceCallOptions;
using qar_ros_ui::ServiceCallOutcome;
using qar_ros_ui::Status;
using qar_ui_model::UiValue;
using qar_ros_ui_test::at;
using qar_ros_ui_test::json;
using qar_ros_ui_test::TestNode;
using qar_ros_ui_test::unique_name;
using BasicTypes = test_msgs::srv::BasicTypes;

namespace
{

rclcpp::Service<BasicTypes>::SharedPtr echo_server(TestNode & node, const std::string & name)
{
  return node.node()->create_service<BasicTypes>(
    name,
    [](std::shared_ptr<BasicTypes::Request> request, std::shared_ptr<BasicTypes::Response> response) {
      response->int32_value = request->int32_value + 1;
      response->string_value = request->string_value;
      response->bool_value = request->bool_value;
      response->float64_value = request->float64_value * 2.0;
    });
}

}  // namespace

TEST(ServiceBridgeLive, CallsATypedServerAndConvertsTheResponse)
{
  TestNode node;
  ServiceBridge bridge(node.handles());
  const std::string name = unique_name("basic");
  auto server = echo_server(node, name);

  std::optional<ServiceCallOutcome> outcome;
  std::int64_t call_id = 0;
  const Result queued = bridge.call(
    name, "", json(R"({"int32_value":41,"string_value":"hi","bool_value":true,"float64_value":1.25})"),
    ServiceCallOptions{}, [&](const ServiceCallOutcome & o) {outcome = o;}, &call_id);
  ASSERT_TRUE(queued.ok()) << queued.message;
  EXPECT_GT(call_id, 0);
  EXPECT_EQ(bridge.pending_count(), 1u);

  ASSERT_TRUE(node.spin_until([&]() {return outcome.has_value();}));
  ASSERT_TRUE(outcome->ok()) << outcome->status.message;
  EXPECT_EQ(outcome->call_id, call_id);
  EXPECT_EQ(outcome->service, name);
  EXPECT_EQ(outcome->type_name, "test_msgs/srv/BasicTypes");
  EXPECT_EQ(*at(outcome->response, "int32_value"), UiValue(std::int64_t{42}));
  EXPECT_EQ(*at(outcome->response, "string_value"), UiValue("hi"));
  EXPECT_EQ(*at(outcome->response, "bool_value"), UiValue(true));
  EXPECT_EQ(*at(outcome->response, "float64_value"), UiValue(2.5));
  EXPECT_EQ(bridge.pending_count(), 0u);
  const auto diagnostics = bridge.diagnostics();
  EXPECT_EQ(diagnostics.clients, 1u);
  EXPECT_EQ(diagnostics.responses, 1u);
  EXPECT_EQ(diagnostics.timeouts, 0u);
  EXPECT_TRUE(bridge.server_available(name, ""));
}

TEST(ServiceBridgeLive, FutureFlavourAndAnotherPackage)
{
  TestNode node;
  ServiceBridge bridge(node.handles());
  const std::string name = unique_name("set_bool");
  auto server = node.node()->create_service<std_srvs::srv::SetBool>(
    name,
    [](std::shared_ptr<std_srvs::srv::SetBool::Request> request, std::shared_ptr<std_srvs::srv::SetBool::Response> response) {
      response->success = request->data;
      response->message = request->data ? "on" : "off";
    });
  std::string type;
  ASSERT_TRUE(node.spin_until([&]() {return bridge.resolve_service_type(name, type).ok();}));
  EXPECT_EQ(type, "std_srvs/srv/SetBool");

  auto future = bridge.call_async(name, type, json(R"({"data":true})"));
  ASSERT_TRUE(node.spin_until([&]() {return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;}));
  const ServiceCallOutcome outcome = future.get();
  ASSERT_TRUE(outcome.ok()) << outcome.status.message;
  EXPECT_EQ(*at(outcome.response, "success"), UiValue(true));
  EXPECT_EQ(*at(outcome.response, "message"), UiValue("on"));

  qar_ui_model::ServiceDescription description;
  ASSERT_TRUE(bridge.describe(type, description).ok());
  EXPECT_EQ(description.response->find_field("message")->kind, qar_ui_model::FieldKind::String);
}

TEST(ServiceBridgeLive, TimesOutWithoutAServer)
{
  TestNode node;
  ServiceBridge bridge(node.handles());
  ServiceCallOptions options;
  options.timeout = std::chrono::milliseconds(300);
  std::optional<ServiceCallOutcome> outcome;
  const auto started = std::chrono::steady_clock::now();
  ASSERT_TRUE(
    bridge.call(
      unique_name("absent"), "test_msgs/srv/BasicTypes", json(R"({})"), options,
      [&](const ServiceCallOutcome & o) {outcome = o;}).ok());
  ASSERT_TRUE(node.spin_until([&]() {return outcome.has_value();}, std::chrono::seconds(10)));
  EXPECT_EQ(outcome->status.status, Status::Timeout) << outcome->status.message;
  EXPECT_NE(outcome->status.message.find("no server"), std::string::npos) << outcome->status.message;
  EXPECT_GE(std::chrono::steady_clock::now() - started, std::chrono::milliseconds(250));
  EXPECT_EQ(bridge.diagnostics().timeouts, 1u);
  EXPECT_EQ(bridge.pending_count(), 0u);

  // The future flavour delivers the same outcome.
  auto future = bridge.call_async(unique_name("absent2"), "test_msgs/srv/BasicTypes", json(R"({})"), options);
  ASSERT_TRUE(node.spin_until([&]() {return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;}, std::chrono::seconds(10)));
  EXPECT_EQ(future.get().status.status, Status::Timeout);
}

TEST(ServiceBridgeLive, DeferredSendWaitsForALateServer)
{
  TestNode node;
  ServiceBridge bridge(node.handles());
  const std::string name = unique_name("late");
  std::optional<ServiceCallOutcome> outcome;
  ASSERT_TRUE(
    bridge.call(
      name, "test_msgs/srv/BasicTypes", json(R"({"int32_value":1})"), ServiceCallOptions{},
      [&](const ServiceCallOutcome & o) {outcome = o;}).ok());
  node.spin_for(std::chrono::milliseconds(200));
  EXPECT_FALSE(outcome.has_value());
  auto server = echo_server(node, name);
  ASSERT_TRUE(node.spin_until([&]() {return outcome.has_value();}));
  ASSERT_TRUE(outcome->ok()) << outcome->status.message;
  EXPECT_EQ(*at(outcome->response, "int32_value"), UiValue(std::int64_t{2}));
}

TEST(ServiceBridgeLive, QueueingFailuresReturnWithoutACallback)
{
  TestNode node;
  ServiceBridge bridge(node.handles());
  bool called = false;
  auto callback = [&](const ServiceCallOutcome &) {called = true;};

  EXPECT_EQ(bridge.call(unique_name("unknown_type"), "", json("{}"), ServiceCallOptions{}, callback).status, Status::NotFound);
  EXPECT_EQ(bridge.call("", "test_msgs/srv/BasicTypes", json("{}"), ServiceCallOptions{}, callback).status, Status::InvalidArgument);
  EXPECT_EQ(bridge.call(unique_name("s"), "test_msgs/srv/BasicTypes", json("{}"), ServiceCallOptions{}, nullptr).status, Status::InvalidArgument);
  EXPECT_EQ(bridge.call(unique_name("s"), "nope/srv/Nope", json("{}"), ServiceCallOptions{}, callback).status, Status::TypeSupportUnavailable);
  const Result conversion = bridge.call(unique_name("s"), "test_msgs/srv/BasicTypes", json(R"({"int32_value":"x"})"), ServiceCallOptions{}, callback);
  EXPECT_EQ(conversion.status, Status::ConversionFailed) << conversion.message;
  ServiceCallOptions no_wait;
  no_wait.wait_for_server = false;
  EXPECT_EQ(bridge.call(unique_name("s"), "test_msgs/srv/BasicTypes", json("{}"), no_wait, callback).status, Status::NotFound);
  node.spin_for(std::chrono::milliseconds(100));
  EXPECT_FALSE(called);
  EXPECT_EQ(bridge.pending_count(), 0u);
  EXPECT_GE(bridge.diagnostics().failures, 3u);
  EXPECT_FALSE(bridge.server_available(unique_name("s"), "test_msgs/srv/BasicTypes"));

  ServiceBridge empty(qar_ros_ui::NodeHandles{});
  EXPECT_EQ(empty.call("/s", "test_msgs/srv/BasicTypes", json("{}"), ServiceCallOptions{}, callback).status, Status::InvalidState);
}

TEST(ServiceBridgeLive, PollDrivesTimeoutsWithoutTheTimer)
{
  TestNode node;
  ServiceBridge bridge(node.handles());
  ServiceCallOptions options;
  options.timeout = std::chrono::milliseconds(50);
  std::optional<ServiceCallOutcome> outcome;
  ASSERT_TRUE(
    bridge.call(
      unique_name("absent"), "test_msgs/srv/BasicTypes", json("{}"), options,
      [&](const ServiceCallOutcome & o) {outcome = o;}).ok());
  std::this_thread::sleep_for(std::chrono::milliseconds(80));
  EXPECT_EQ(bridge.poll(), 1u);  // no spinning at all: poll() alone expires it
  ASSERT_TRUE(outcome.has_value());
  EXPECT_EQ(outcome->status.status, Status::Timeout);
  EXPECT_EQ(bridge.poll(), 0u);
}

#endif  // QAR_ROS_UI_LIVE_TESTS
