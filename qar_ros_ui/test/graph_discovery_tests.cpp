// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "qar_ros_ui/graph_discovery.hpp"

#ifdef QAR_ROS_UI_LIVE_TESTS
#include "geometry_msgs/msg/pose_stamped.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "std_srvs/srv/set_bool.hpp"
#include "test_msgs/action/fibonacci.hpp"
#endif

#include "test_support.hpp"

using qar_ros_ui::diff_graph;
using qar_ros_ui::GraphDelta;
using qar_ros_ui::GraphDiscovery;
using qar_ros_ui::GraphEntity;
using qar_ros_ui::GraphSnapshot;
using qar_ros_ui::is_hidden_name;
using qar_ros_ui::recover_actions;
using qar_ros_ui::Result;

namespace
{

GraphEntity entity(const std::string & name, std::vector<std::string> types)
{
  GraphEntity out;
  out.name = name;
  out.types = std::move(types);
  return out;
}

}  // namespace

TEST(GraphDiscovery, HiddenNamesFollowRosConvention)
{
  EXPECT_TRUE(is_hidden_name("/fib/_action/feedback"));
  EXPECT_TRUE(is_hidden_name("/_ros2cli_1234/get_parameters"));
  EXPECT_TRUE(is_hidden_name("_hidden"));
  EXPECT_FALSE(is_hidden_name("/camera/image_raw"));
  EXPECT_FALSE(is_hidden_name("/tf_static"));
  EXPECT_FALSE(is_hidden_name("/a_b/c_d"));
  EXPECT_FALSE(is_hidden_name(""));
}

TEST(GraphDiscovery, ActionNamesAndTypesAreRecoveredFromTheProtocol)
{
  std::string out;
  EXPECT_TRUE(qar_ros_ui::action_name_from_send_goal_service("/fib/_action/send_goal", out));
  EXPECT_EQ(out, "/fib");
  EXPECT_TRUE(qar_ros_ui::action_name_from_send_goal_service("/ns/nav/_action/send_goal", out));
  EXPECT_EQ(out, "/ns/nav");
  EXPECT_FALSE(qar_ros_ui::action_name_from_send_goal_service("/fib/_action/cancel_goal", out));
  EXPECT_FALSE(qar_ros_ui::action_name_from_send_goal_service("/_action/send_goal", out));
  EXPECT_TRUE(qar_ros_ui::action_name_from_feedback_topic("/fib/_action/feedback", out));
  EXPECT_EQ(out, "/fib");
  EXPECT_FALSE(qar_ros_ui::action_name_from_feedback_topic("/fib/_action/status", out));
  EXPECT_TRUE(qar_ros_ui::action_type_from_send_goal_type("test_msgs/action/Fibonacci_SendGoal", out));
  EXPECT_EQ(out, "test_msgs/action/Fibonacci");
  EXPECT_FALSE(qar_ros_ui::action_type_from_send_goal_type("test_msgs/srv/Foo_SendGoal", out));
  EXPECT_TRUE(qar_ros_ui::action_type_from_feedback_type("test_msgs/action/Fibonacci_FeedbackMessage", out));
  EXPECT_EQ(out, "test_msgs/action/Fibonacci");
  EXPECT_FALSE(qar_ros_ui::action_type_from_feedback_type("test_msgs/action/Fibonacci_Feedback", out));
}

TEST(GraphDiscovery, RecoverActionsFoldsServicesAndTopics)
{
  const std::vector<GraphEntity> topics = {
    entity("/camera/image_raw", {"sensor_msgs/msg/Image"}),
    entity("/fib/_action/feedback", {"test_msgs/action/Fibonacci_FeedbackMessage"}),
    entity("/fib/_action/status", {"action_msgs/msg/GoalStatusArray"}),
    entity("/nav/_action/feedback", {"nav_msgs/action/NavigateToPose_FeedbackMessage"}),
  };
  const std::vector<GraphEntity> services = {
    entity("/fib/_action/send_goal", {"test_msgs/action/Fibonacci_SendGoal"}),
    entity("/fib/_action/cancel_goal", {"action_msgs/srv/CancelGoal"}),
    entity("/fib/_action/get_result", {"test_msgs/action/Fibonacci_GetResult"}),
    entity("/only_service/_action/send_goal", {"pkg/action/Only_SendGoal"}),
    entity("/set_bool", {"std_srvs/srv/SetBool"}),
  };
  const std::vector<GraphEntity> actions = recover_actions(topics, services);
  ASSERT_EQ(actions.size(), 3u);
  EXPECT_EQ(actions[0].name, "/fib");
  EXPECT_EQ(actions[0].types, std::vector<std::string>{"test_msgs/action/Fibonacci"});
  EXPECT_EQ(actions[1].name, "/nav");
  EXPECT_EQ(actions[1].types, std::vector<std::string>{"nav_msgs/action/NavigateToPose"});
  EXPECT_EQ(actions[2].name, "/only_service");
  EXPECT_EQ(actions[2].types, std::vector<std::string>{"pkg/action/Only"});
  EXPECT_TRUE(recover_actions({}, {}).empty());
}

TEST(GraphDiscovery, DiffReportsAddedRemovedAndChanged)
{
  GraphSnapshot before;
  before.topics = {entity("/a", {"pkg/msg/A"}), entity("/b", {"pkg/msg/B"}), entity("/c", {"pkg/msg/C"})};
  before.services = {entity("/s", {"pkg/srv/S"})};
  GraphSnapshot after;
  after.topics = {entity("/a", {"pkg/msg/A"}), entity("/b", {"pkg/msg/B2"}), entity("/d", {"pkg/msg/D"})};
  after.actions = {entity("/act", {"pkg/action/Act"})};

  const GraphDelta delta = diff_graph(before, after);
  EXPECT_FALSE(delta.empty());
  ASSERT_EQ(delta.added_topics.size(), 1u);
  EXPECT_EQ(delta.added_topics[0].name, "/d");
  ASSERT_EQ(delta.removed_topics.size(), 1u);
  EXPECT_EQ(delta.removed_topics[0].name, "/c");
  ASSERT_EQ(delta.changed_topics.size(), 1u);
  EXPECT_EQ(delta.changed_topics[0].name, "/b");
  EXPECT_EQ(delta.changed_topics[0].primary_type(), "pkg/msg/B2");
  ASSERT_EQ(delta.removed_services.size(), 1u);
  ASSERT_EQ(delta.added_actions.size(), 1u);
  EXPECT_TRUE(diff_graph(after, after).empty());
  EXPECT_TRUE(diff_graph(GraphSnapshot{}, GraphSnapshot{}).empty());

  EXPECT_NE(after.find_topic("/d"), nullptr);
  EXPECT_EQ(after.find_topic("/c"), nullptr);
  EXPECT_NE(after.find_action("/act"), nullptr);
  EXPECT_EQ(after.find_service("/s"), nullptr);
  EXPECT_EQ(GraphEntity{}.primary_type(), "");
}

TEST(GraphDiscovery, SnapshotWithoutHandlesFails)
{
  GraphSnapshot snapshot;
  EXPECT_EQ(qar_ros_ui::take_graph_snapshot(qar_ros_ui::NodeHandles{}, snapshot).status, qar_ros_ui::Status::InvalidArgument);
  std::vector<GraphEntity> actions;
  EXPECT_EQ(qar_ros_ui::rcl_action_names_and_types(qar_ros_ui::NodeHandles{}, actions).status, qar_ros_ui::Status::InvalidArgument);
}

#ifdef QAR_ROS_UI_LIVE_TESTS
TEST(GraphDiscoveryLive, SeesTopicsServicesAndActionsWithTypes)
{
  using Fibonacci = test_msgs::action::Fibonacci;
  qar_ros_ui_test::TestNode node;
  const std::string topic = qar_ros_ui_test::unique_name("pose");
  const std::string service = qar_ros_ui_test::unique_name("set_bool");
  const std::string action = qar_ros_ui_test::unique_name("fib");

  auto publisher = node.node()->create_publisher<geometry_msgs::msg::PoseStamped>(topic, 10);
  auto server = node.node()->create_service<std_srvs::srv::SetBool>(
    service, [](std::shared_ptr<std_srvs::srv::SetBool::Request>, std::shared_ptr<std_srvs::srv::SetBool::Response>) {});
  auto action_server = rclcpp_action::create_server<Fibonacci>(
    node.node(), action,
    [](const rclcpp_action::GoalUUID &, std::shared_ptr<const Fibonacci::Goal>) {
      return rclcpp_action::GoalResponse::REJECT;
    },
    [](std::shared_ptr<rclcpp_action::ServerGoalHandle<Fibonacci>>) {
      return rclcpp_action::CancelResponse::REJECT;
    },
    [](std::shared_ptr<rclcpp_action::ServerGoalHandle<Fibonacci>>) {});

  GraphSnapshot snapshot;
  const bool visible = node.spin_until(
    [&]() {
      return qar_ros_ui::take_graph_snapshot(node.handles(), snapshot).ok() &&
             snapshot.find_topic(topic) != nullptr && snapshot.find_service(service) != nullptr &&
             snapshot.find_action(action) != nullptr;
    });
  ASSERT_TRUE(visible) << "own entities never showed up on the graph";
  EXPECT_EQ(snapshot.find_topic(topic)->primary_type(), "geometry_msgs/msg/PoseStamped");
  EXPECT_EQ(snapshot.find_service(service)->primary_type(), "std_srvs/srv/SetBool");
  EXPECT_EQ(snapshot.find_action(action)->primary_type(), "test_msgs/action/Fibonacci");

  // Protocol internals are hidden unless asked for.
  for (const GraphEntity & t : snapshot.topics) {
    EXPECT_FALSE(is_hidden_name(t.name)) << t.name;
  }
  for (const GraphEntity & s : snapshot.services) {
    EXPECT_FALSE(is_hidden_name(s.name)) << s.name;
  }
  qar_ros_ui::GraphDiscoveryOptions with_hidden;
  with_hidden.include_hidden = true;
  GraphSnapshot everything;
  ASSERT_TRUE(qar_ros_ui::take_graph_snapshot(node.handles(), everything, with_hidden).ok());
  EXPECT_NE(everything.find_topic(action + "/_action/feedback"), nullptr);
  EXPECT_NE(everything.find_service(action + "/_action/send_goal"), nullptr);

  // rcl_action derives the same answer from the same graph.
  std::vector<GraphEntity> from_rcl;
  ASSERT_TRUE(qar_ros_ui::rcl_action_names_and_types(node.handles(), from_rcl).ok());
  bool found = false;
  for (const GraphEntity & a : from_rcl) {
    if (a.name == action) {
      found = true;
      EXPECT_EQ(a.types, std::vector<std::string>{"test_msgs/action/Fibonacci"});
    }
  }
  EXPECT_TRUE(found);
  EXPECT_EQ(
    recover_actions(everything.topics, everything.services).size(), snapshot.actions.size());
}

TEST(GraphDiscoveryLive, ChangeDetectionAcrossRefreshes)
{
  qar_ros_ui_test::TestNode node;
  GraphDiscovery discovery(node.handles());
  EXPECT_EQ(discovery.revision(), 0u);
  GraphDelta first;
  ASSERT_TRUE(discovery.refresh(&first).ok());
  EXPECT_EQ(discovery.revision(), 1u);

  const std::string topic = qar_ros_ui_test::unique_name("late");
  auto publisher = node.node()->create_publisher<geometry_msgs::msg::PoseStamped>(topic, 10);
  GraphDelta delta;
  const bool appeared = node.spin_until(
    [&]() {
      return discovery.refresh(&delta).ok() && discovery.current().find_topic(topic) != nullptr;
    });
  ASSERT_TRUE(appeared);
  EXPECT_EQ(discovery.revision(), 2u);
  bool listed = false;
  for (const GraphEntity & t : delta.added_topics) {
    listed = listed || t.name == topic;
  }
  EXPECT_TRUE(listed);

  // A refresh with nothing new leaves the revision alone.
  GraphDelta none;
  ASSERT_TRUE(discovery.refresh(&none).ok());
  EXPECT_TRUE(none.empty());
  EXPECT_EQ(discovery.revision(), 2u);

  publisher.reset();
  const bool gone = node.spin_until(
    [&]() {
      return discovery.refresh(&delta).ok() && discovery.current().find_topic(topic) == nullptr;
    });
  ASSERT_TRUE(gone);
  EXPECT_EQ(discovery.revision(), 3u);
}
#endif
