// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>

#include "qar_ros_ui/action_bridge.hpp"

#include "test_support.hpp"

using qar_ros_ui::ActionBridge;
using qar_ros_ui::ActionGoalHandle;
using qar_ros_ui::ActionGoalOptions;
using qar_ros_ui::GoalEvent;
using qar_ros_ui::GoalEventKind;
using qar_ros_ui::GoalId;
using qar_ros_ui::GoalStatus;
using qar_ros_ui::Result;
using qar_ros_ui::Status;
using qar_ui_model::UiValue;
using qar_ros_ui_test::at;
using qar_ros_ui_test::json;

TEST(ActionBridge, GoalIdAndStatusHelpers)
{
  GoalId id{};
  id[0] = 0xAB;
  id[15] = 0x01;
  EXPECT_EQ(qar_ros_ui::goal_id_to_string(id), "ab000000000000000000000000000001");
  EXPECT_TRUE(qar_ros_ui::is_terminal(GoalStatus::Succeeded));
  EXPECT_TRUE(qar_ros_ui::is_terminal(GoalStatus::Canceled));
  EXPECT_TRUE(qar_ros_ui::is_terminal(GoalStatus::Aborted));
  EXPECT_FALSE(qar_ros_ui::is_terminal(GoalStatus::Executing));
  EXPECT_STREQ(qar_ros_ui::to_string(GoalStatus::Canceling), "canceling");
  EXPECT_STREQ(qar_ros_ui::to_string(GoalEventKind::CancelResponse), "cancel_response");
}

#ifdef QAR_ROS_UI_LIVE_TESTS

#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "rclcpp_action/rclcpp_action.hpp"
#include "test_msgs/action/fibonacci.hpp"

using Fibonacci = test_msgs::action::Fibonacci;
using GoalHandleFibonacci = rclcpp_action::ServerGoalHandle<Fibonacci>;
using qar_ros_ui_test::TestNode;
using qar_ros_ui_test::unique_name;

namespace
{

/// A Fibonacci server that rejects orders above 10, executes on a worker
/// thread (feedback per step, honouring cancel) and joins its workers on exit.
class FibonacciServer
{
public:
  FibonacciServer(TestNode & node, const std::string & name, std::chrono::milliseconds step)
  : step_(step)
  {
    server_ = rclcpp_action::create_server<Fibonacci>(
      node.node(), name,
      [](const rclcpp_action::GoalUUID &, std::shared_ptr<const Fibonacci::Goal> goal) {
        return goal->order > 10 ?
               rclcpp_action::GoalResponse::REJECT : rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
      },
      [](std::shared_ptr<GoalHandleFibonacci>) {return rclcpp_action::CancelResponse::ACCEPT;},
      [this](std::shared_ptr<GoalHandleFibonacci> goal_handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        workers_.emplace_back([this, goal_handle]() {execute(goal_handle);});
      });
  }

  ~FibonacciServer()
  {
    std::vector<std::thread> workers;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      workers.swap(workers_);
    }
    for (std::thread & worker : workers) {
      worker.join();
    }
  }

private:
  void execute(std::shared_ptr<GoalHandleFibonacci> goal_handle)
  {
    auto feedback = std::make_shared<Fibonacci::Feedback>();
    auto result = std::make_shared<Fibonacci::Result>();
    std::vector<int32_t> & sequence = feedback->sequence;
    sequence.push_back(0);
    sequence.push_back(1);
    const int order = goal_handle->get_goal()->order;
    for (int i = 1; i < order && rclcpp::ok(); ++i) {
      if (goal_handle->is_canceling()) {
        result->sequence = sequence;
        goal_handle->canceled(result);
        return;
      }
      sequence.push_back(sequence[i] + sequence[i - 1]);
      goal_handle->publish_feedback(feedback);
      std::this_thread::sleep_for(step_);
    }
    result->sequence = sequence;
    goal_handle->succeed(result);
  }

  std::chrono::milliseconds step_;
  rclcpp_action::Server<Fibonacci>::SharedPtr server_;
  std::mutex mutex_;
  std::vector<std::thread> workers_;
};

struct EventLog
{
  std::vector<GoalEvent> events;
  bool saw(GoalEventKind kind) const
  {
    for (const GoalEvent & event : events) {
      if (event.kind == kind) {
        return true;
      }
    }
    return false;
  }
  const GoalEvent * last(GoalEventKind kind) const
  {
    const GoalEvent * found = nullptr;
    for (const GoalEvent & event : events) {
      if (event.kind == kind) {
        found = &event;
      }
    }
    return found;
  }
};

}  // namespace

TEST(ActionBridgeLive, GoalRunsToSuccessWithFeedbackAndResult)
{
  TestNode node;
  ActionBridge bridge(node.handles());
  const std::string name = unique_name("fib");
  FibonacciServer server(node, name, std::chrono::milliseconds(10));

  EventLog log;
  ActionGoalOptions options;
  options.on_event = [&](const std::shared_ptr<ActionGoalHandle> &, const GoalEvent & event) {
      log.events.push_back(event);
    };
  std::shared_ptr<ActionGoalHandle> handle;
  const Result sent = bridge.send_goal(name, "", json(R"({"order":6})"), options, handle);
  ASSERT_TRUE(sent.ok()) << sent.message;
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(handle->action_name(), name);
  EXPECT_EQ(handle->type_name(), "test_msgs/action/Fibonacci");
  EXPECT_EQ(handle->goal_id_string().size(), 32u);

  ASSERT_TRUE(node.spin_until([&]() {return handle->finished();}));
  EXPECT_TRUE(handle->accepted());
  EXPECT_FALSE(handle->rejected());
  EXPECT_TRUE(handle->error().ok()) << handle->error().message;
  EXPECT_EQ(handle->status(), GoalStatus::Succeeded);
  UiValue result;
  GoalStatus terminal = GoalStatus::Unknown;
  ASSERT_TRUE(handle->result(result, &terminal));
  EXPECT_EQ(terminal, GoalStatus::Succeeded);
  EXPECT_EQ(*at(result, "sequence"), json("[0,1,1,2,3,5,8]"));
  EXPECT_GE(handle->feedback_count(), 1u);
  UiValue feedback;
  ASSERT_TRUE(handle->latest_feedback(feedback));
  EXPECT_EQ(at(feedback, "sequence")->size(), 7u);

  EXPECT_TRUE(log.saw(GoalEventKind::Accepted));
  EXPECT_TRUE(log.saw(GoalEventKind::Feedback));
  const GoalEvent * result_event = log.last(GoalEventKind::Result);
  ASSERT_NE(result_event, nullptr);
  EXPECT_EQ(result_event->status, GoalStatus::Succeeded);
  EXPECT_EQ(*at(result_event->value, "sequence"), json("[0,1,1,2,3,5,8]"));
  EXPECT_FALSE(log.saw(GoalEventKind::Rejected));
  EXPECT_FALSE(log.saw(GoalEventKind::Error));

  const auto diagnostics = bridge.diagnostics();
  EXPECT_EQ(diagnostics.goals_sent, 1u);
  EXPECT_EQ(diagnostics.goals_accepted, 1u);
  EXPECT_EQ(diagnostics.results, 1u);
  EXPECT_EQ(diagnostics.active_goals, 0u);
  EXPECT_EQ(bridge.active_goal_count(), 0u);
  EXPECT_TRUE(bridge.server_available(name, ""));
  EXPECT_EQ(handle->diagnostics().status, GoalStatus::Succeeded);
}

TEST(ActionBridgeLive, RejectedGoalFinishesAtOnce)
{
  TestNode node;
  ActionBridge bridge(node.handles());
  const std::string name = unique_name("fib");
  FibonacciServer server(node, name, std::chrono::milliseconds(10));

  EventLog log;
  ActionGoalOptions options;
  options.on_event = [&](const std::shared_ptr<ActionGoalHandle> &, const GoalEvent & event) {
      log.events.push_back(event);
    };
  std::shared_ptr<ActionGoalHandle> handle;
  ASSERT_TRUE(bridge.send_goal(name, "test_msgs/action/Fibonacci", json(R"({"order":100})"), options, handle).ok());
  ASSERT_TRUE(node.spin_until([&]() {return handle->finished();}));
  EXPECT_TRUE(handle->rejected());
  EXPECT_FALSE(handle->accepted());
  EXPECT_TRUE(log.saw(GoalEventKind::Rejected));
  UiValue result;
  EXPECT_FALSE(handle->result(result));
  EXPECT_EQ(bridge.diagnostics().goals_rejected, 1u);
}

TEST(ActionBridgeLive, CancelStopsAnExecutingGoal)
{
  TestNode node;
  ActionBridge bridge(node.handles());
  const std::string name = unique_name("fib");
  FibonacciServer server(node, name, std::chrono::milliseconds(100));

  EventLog log;
  ActionGoalOptions options;
  options.on_event = [&](const std::shared_ptr<ActionGoalHandle> &, const GoalEvent & event) {
      log.events.push_back(event);
    };
  std::shared_ptr<ActionGoalHandle> handle;
  ASSERT_TRUE(bridge.send_goal(name, "", json(R"({"order":10})"), options, handle).ok());
  UiValue feedback;
  ASSERT_TRUE(node.spin_until([&]() {return handle->take_feedback(feedback);}));
  // Feedback can overtake the goal response (see ActionBridgeImpl::find_goal_locked).
  ASSERT_TRUE(node.spin_until([&]() {return handle->accepted();}));
  EXPECT_EQ(bridge.active_goal_count(), 1u);

  const Result cancel = bridge.cancel(handle);
  ASSERT_TRUE(cancel.ok()) << cancel.message;
  ASSERT_TRUE(node.spin_until([&]() {return handle->finished();}));
  EXPECT_EQ(handle->status(), GoalStatus::Canceled);
  UiValue result;
  GoalStatus terminal = GoalStatus::Unknown;
  ASSERT_TRUE(handle->result(result, &terminal));
  EXPECT_EQ(terminal, GoalStatus::Canceled);
  EXPECT_LT(at(result, "sequence")->size(), 11u);

  const GoalEvent * cancel_event = log.last(GoalEventKind::CancelResponse);
  ASSERT_NE(cancel_event, nullptr);
  EXPECT_TRUE(cancel_event->error.ok()) << cancel_event->error.message;
  EXPECT_EQ(*at(cancel_event->value, "return_code"), UiValue(std::int64_t{0}));
  EXPECT_EQ(at(cancel_event->value, "goals_canceling")->size(), 1u);
  EXPECT_EQ(*at(cancel_event->value, "goals_canceling[0]"), UiValue(handle->goal_id_string()));
  EXPECT_EQ(bridge.cancel(handle).status, Status::InvalidState);  // already finished
}

TEST(ActionBridgeLive, TimesOutWithoutAServer)
{
  TestNode node;
  ActionBridge bridge(node.handles());
  EventLog log;
  ActionGoalOptions options;
  options.response_timeout = std::chrono::milliseconds(300);
  options.on_event = [&](const std::shared_ptr<ActionGoalHandle> &, const GoalEvent & event) {
      log.events.push_back(event);
    };
  std::shared_ptr<ActionGoalHandle> handle;
  ASSERT_TRUE(bridge.send_goal(unique_name("absent"), "test_msgs/action/Fibonacci", json(R"({"order":3})"), options, handle).ok());
  EXPECT_EQ(bridge.active_goal_count(), 1u);
  ASSERT_TRUE(node.spin_until([&]() {return handle->finished();}, std::chrono::seconds(10)));
  EXPECT_EQ(handle->error().status, Status::Timeout) << handle->error().message;
  EXPECT_FALSE(handle->accepted());
  const GoalEvent * error = log.last(GoalEventKind::Error);
  ASSERT_NE(error, nullptr);
  EXPECT_EQ(error->error.status, Status::Timeout);
  EXPECT_EQ(bridge.diagnostics().timeouts, 1u);
  EXPECT_EQ(bridge.active_goal_count(), 0u);
}

TEST(ActionBridgeLive, DeferredGoalWaitsForALateServer)
{
  TestNode node;
  ActionBridge bridge(node.handles());
  const std::string name = unique_name("late");
  std::shared_ptr<ActionGoalHandle> handle;
  ASSERT_TRUE(bridge.send_goal(name, "test_msgs/action/Fibonacci", json(R"({"order":3})"), ActionGoalOptions{}, handle).ok());
  node.spin_for(std::chrono::milliseconds(150));
  EXPECT_FALSE(handle->accepted());
  FibonacciServer server(node, name, std::chrono::milliseconds(5));
  ASSERT_TRUE(node.spin_until([&]() {return handle->finished();}));
  EXPECT_EQ(handle->status(), GoalStatus::Succeeded);
  UiValue result;
  ASSERT_TRUE(handle->result(result));
  EXPECT_EQ(*at(result, "sequence"), json("[0,1,1,2]"));
}

TEST(ActionBridgeLive, FailuresAreReportedNotThrown)
{
  TestNode node;
  ActionBridge bridge(node.handles());
  std::shared_ptr<ActionGoalHandle> handle;
  EXPECT_EQ(bridge.send_goal(unique_name("x"), "", json("{}"), ActionGoalOptions{}, handle).status, Status::NotFound);
  EXPECT_EQ(bridge.send_goal("", "test_msgs/action/Fibonacci", json("{}"), ActionGoalOptions{}, handle).status, Status::InvalidArgument);
  EXPECT_EQ(bridge.send_goal(unique_name("x"), "geometry_msgs/msg/Pose", json("{}"), ActionGoalOptions{}, handle).status, Status::InvalidArgument);
  EXPECT_EQ(bridge.send_goal(unique_name("x"), "nope/action/Nope", json("{}"), ActionGoalOptions{}, handle).status, Status::TypeSupportUnavailable);
  const Result conversion = bridge.send_goal(unique_name("x"), "test_msgs/action/Fibonacci", json(R"({"order":"x"})"), ActionGoalOptions{}, handle);
  EXPECT_EQ(conversion.status, Status::ConversionFailed) << conversion.message;
  EXPECT_NE(conversion.message.find("goal.order"), std::string::npos) << conversion.message;
  ActionGoalOptions no_wait;
  no_wait.wait_for_server = false;
  EXPECT_EQ(bridge.send_goal(unique_name("x"), "test_msgs/action/Fibonacci", json(R"({"order":1})"), no_wait, handle).status, Status::NotFound);
  EXPECT_EQ(handle, nullptr);
  EXPECT_EQ(bridge.cancel(nullptr).status, Status::InvalidArgument);
  EXPECT_FALSE(bridge.server_available(unique_name("x"), "test_msgs/action/Fibonacci"));
  qar_ui_model::ActionDescription description;
  ASSERT_TRUE(bridge.describe("test_msgs/action/Fibonacci", description).ok());
  EXPECT_EQ(description.goal->find_field("order")->kind, qar_ui_model::FieldKind::Int32);
  ActionBridge empty(qar_ros_ui::NodeHandles{});
  EXPECT_EQ(empty.send_goal("/x", "test_msgs/action/Fibonacci", json("{}"), ActionGoalOptions{}, handle).status, Status::InvalidState);
}

TEST(ActionBridgeLive, ActionClientsAndTimersJoinTheHostsCallbackGroup)
{
  TestNode node;
  const std::string name = unique_name("fib");
  FibonacciServer server(node, name, std::chrono::milliseconds(10));
  const qar_ros_ui_test::GroupCounts default_before =
    qar_ros_ui_test::count_entities(node.default_group());
  rclcpp::CallbackGroup::SharedPtr group = node.make_group();
  qar_ros_ui::NodeHandles handles = node.handles();
  handles.callback_group = group;
  auto bridge = std::make_unique<ActionBridge>(handles);

  std::shared_ptr<ActionGoalHandle> handle;
  ASSERT_TRUE(
    bridge->send_goal(
      name, "test_msgs/action/Fibonacci", json(R"({"order":3})"), ActionGoalOptions{},
      handle).ok());
  const qar_ros_ui_test::GroupCounts in_group = qar_ros_ui_test::count_entities(group);
  EXPECT_EQ(in_group.waitables, 1u) << "the action client";
  EXPECT_EQ(in_group.timers, 1u) << "the timer that expires overdue goals";
  ASSERT_TRUE(node.spin_until([&]() {return handle->finished();}));
  EXPECT_EQ(handle->status(), GoalStatus::Succeeded);
  const qar_ros_ui_test::GroupCounts default_after =
    qar_ros_ui_test::count_entities(node.default_group());
  EXPECT_EQ(default_after.waitables, default_before.waitables);
  EXPECT_EQ(default_after.timers, default_before.timers);

  // The client's deleter takes it out of the group.
  std::weak_ptr<rclcpp::CallbackGroup> weak_group = group;
  handle.reset();
  bridge.reset();
  EXPECT_EQ(qar_ros_ui_test::count_entities(group).waitables, 0u);
  handles.callback_group.reset();  // the host's own copy of the handles holds it too
  group.reset();
  EXPECT_TRUE(weak_group.expired()) << "neither the node nor the executor keeps the group";

  node.spin_for(std::chrono::milliseconds(50));  // the executor forgets the group
}

#endif  // QAR_ROS_UI_LIVE_TESTS
