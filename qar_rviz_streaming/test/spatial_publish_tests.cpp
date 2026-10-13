// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Pick -> ROS message. The conventions a robot acts on (the goal quaternion,
// the initialpose covariance diagonal, the polygon points) are asserted here,
// because the alternative is discovering them on a robot.

#include "qar_rviz_streaming/spatial_publish.hpp"

#include <cmath>

#include <gtest/gtest.h>

namespace
{

using qar_ar_core::AreaShape;
using qar_ar_core::NavGoal;
using qar_ar_core::PickTarget;
using qar_ar_core::PosePickResult;
using qar_ar_core::SpatialPoint;
using qar_ar_core::make_circle;
using qar_ar_core::make_rectangle;
using qar_ar_core::yaw_to_quaternion;
using qar_rviz_streaming::SpatialPublishConfig;
using qar_rviz_streaming::SpatialPublishTarget;
using qar_rviz_streaming::default_publish_config;
using qar_rviz_streaming::make_area_publish;
using qar_rviz_streaming::make_nav_goal_publish;
using qar_rviz_streaming::make_point_publish;
using qar_rviz_streaming::make_pose_publish;
using qar_ui_model::UiValue;

SpatialPoint picked_point(float x, float y, float z)
{
  SpatialPoint point;
  point.valid = true;
  point.position = QarVector3{x, y, z};
  return point;
}

PosePickResult picked_pose(float x, float y, float yaw)
{
  PosePickResult pose;
  pose.valid = true;
  pose.position = QarVector3{x, y, 0.0f};
  pose.yaw_rad = yaw;
  pose.orientation = yaw_to_quaternion(yaw);
  pose.heading_from_drag = true;
  return pose;
}

const UiValue & at(const UiValue & value, const char * key)
{
  static const UiValue kNull;
  const UiValue * found = value.find(key);
  return found != nullptr ? *found : kNull;
}

}  // namespace

// --- defaults ---------------------------------------------------------------

TEST(SpatialPublishDefaults, MatchTheConventionalRvizEndpoints)
{
  const auto point = default_publish_config(SpatialPublishTarget::ClickedPoint);
  EXPECT_EQ(point.topic, "/clicked_point");
  EXPECT_EQ(point.type_name, "geometry_msgs/msg/PointStamped");

  const auto goal = default_publish_config(SpatialPublishTarget::GoalPose);
  EXPECT_EQ(goal.topic, "/goal_pose");
  EXPECT_EQ(goal.type_name, "geometry_msgs/msg/PoseStamped");

  const auto initial = default_publish_config(SpatialPublishTarget::InitialPose);
  EXPECT_EQ(initial.topic, "/initialpose");
  EXPECT_EQ(initial.type_name, "geometry_msgs/msg/PoseWithCovarianceStamped");

  const auto area = default_publish_config(SpatialPublishTarget::Area);
  EXPECT_EQ(area.type_name, "geometry_msgs/msg/PolygonStamped");

  const auto custom = default_publish_config(SpatialPublishTarget::Custom);
  EXPECT_TRUE(custom.topic.empty());
  EXPECT_TRUE(custom.type_name.empty());
}

TEST(SpatialPublishDefaults, FrameIdDefaultsToMap)
{
  EXPECT_EQ(default_publish_config(SpatialPublishTarget::GoalPose).frame_id, "map");
}

// --- points -----------------------------------------------------------------

TEST(SpatialPointPublish, BuildsAPointStamped)
{
  auto config = default_publish_config(SpatialPublishTarget::ClickedPoint);
  config.stamp.sec = 12;
  config.stamp.nanosec = 34;
  const auto request = make_point_publish(config, picked_point(1.5f, -2.5f, 0.0f));
  ASSERT_TRUE(request.ok) << request.error;
  EXPECT_EQ(request.topic, "/clicked_point");
  EXPECT_EQ(request.type_name, "geometry_msgs/msg/PointStamped");

  const UiValue & header = at(request.message, "header");
  EXPECT_EQ(at(header, "frame_id").as_string(), "map");
  EXPECT_EQ(at(at(header, "stamp"), "sec").as_int(), 12);
  EXPECT_EQ(at(at(header, "stamp"), "nanosec").as_int(), 34);

  const UiValue & point = at(request.message, "point");
  EXPECT_DOUBLE_EQ(at(point, "x").as_double(), 1.5);
  EXPECT_DOUBLE_EQ(at(point, "y").as_double(), -2.5);
  EXPECT_DOUBLE_EQ(at(point, "z").as_double(), 0.0);
}

TEST(SpatialPointPublish, BuildsABarePoint)
{
  auto config = default_publish_config(SpatialPublishTarget::Custom);
  config.topic = "/my/point";
  config.type_name = "geometry_msgs/msg/Point";
  const auto request = make_point_publish(config, picked_point(1.0f, 2.0f, 3.0f));
  ASSERT_TRUE(request.ok) << request.error;
  EXPECT_FALSE(request.message.contains("header"));
  EXPECT_DOUBLE_EQ(at(request.message, "z").as_double(), 3.0);
}

TEST(SpatialPointPublish, RefusesAnInvalidPickWithItsOwnReason)
{
  SpatialPoint point;
  point.error = "ray is parallel to the target plane";
  const auto request =
    make_point_publish(default_publish_config(SpatialPublishTarget::ClickedPoint), point);
  EXPECT_FALSE(request.ok);
  EXPECT_EQ(request.error, "ray is parallel to the target plane");
}

TEST(SpatialPointPublish, RefusesAnUnconfiguredCustomTarget)
{
  const auto request =
    make_point_publish(default_publish_config(SpatialPublishTarget::Custom), picked_point(0, 0, 0));
  EXPECT_FALSE(request.ok);
  EXPECT_EQ(request.error, "no topic configured for this spatial target");
}

TEST(SpatialPointPublish, RefusesATypeItCannotBuild)
{
  auto config = default_publish_config(SpatialPublishTarget::ClickedPoint);
  config.type_name = "std_msgs/msg/String";
  const auto request = make_point_publish(config, picked_point(0, 0, 0));
  EXPECT_FALSE(request.ok);
  EXPECT_NE(request.error.find("std_msgs/msg/String"), std::string::npos);
}

TEST(SpatialPointPublish, RefusesAStampedMessageWithNoFrame)
{
  auto config = default_publish_config(SpatialPublishTarget::ClickedPoint);
  config.frame_id.clear();
  const auto request = make_point_publish(config, picked_point(0, 0, 0));
  EXPECT_FALSE(request.ok);
  EXPECT_EQ(request.error, "a stamped message needs a frame id");
}

// --- poses ------------------------------------------------------------------

TEST(SpatialPosePublish, BuildsAPoseStamped)
{
  const auto config = default_publish_config(SpatialPublishTarget::GoalPose);
  const auto request = make_pose_publish(config, picked_pose(2.0f, 3.0f, 1.57079632679f));
  ASSERT_TRUE(request.ok) << request.error;
  const UiValue & pose = at(request.message, "pose");
  EXPECT_DOUBLE_EQ(at(at(pose, "position"), "x").as_double(), 2.0);
  EXPECT_NEAR(at(at(pose, "orientation"), "z").as_double(), 0.70710678, 1e-6);
  EXPECT_NEAR(at(at(pose, "orientation"), "w").as_double(), 0.70710678, 1e-6);
  EXPECT_DOUBLE_EQ(at(at(pose, "orientation"), "x").as_double(), 0.0);
}

TEST(SpatialPosePublish, InitialPoseCarriesTheRvizCovarianceDiagonal)
{
  const auto config = default_publish_config(SpatialPublishTarget::InitialPose);
  const auto request = make_pose_publish(config, picked_pose(0.0f, 0.0f, 0.0f));
  ASSERT_TRUE(request.ok) << request.error;
  const UiValue & inner = at(request.message, "pose");
  ASSERT_TRUE(inner.contains("pose"));
  const UiValue & covariance = at(inner, "covariance");
  ASSERT_EQ(covariance.size(), 36u);
  EXPECT_DOUBLE_EQ(covariance.at(0)->as_double(), 0.25);
  EXPECT_DOUBLE_EQ(covariance.at(7)->as_double(), 0.25);
  EXPECT_DOUBLE_EQ(covariance.at(35)->as_double(), 0.06853891945200942);
  EXPECT_DOUBLE_EQ(covariance.at(1)->as_double(), 0.0);
  EXPECT_DOUBLE_EQ(covariance.at(14)->as_double(), 0.0);
}

TEST(SpatialPosePublish, CovarianceIsConfigurable)
{
  auto config = default_publish_config(SpatialPublishTarget::InitialPose);
  config.position_variance = 4.0;
  config.yaw_variance = 0.5;
  const auto request = make_pose_publish(config, picked_pose(0.0f, 0.0f, 0.0f));
  ASSERT_TRUE(request.ok) << request.error;
  const UiValue & covariance = at(at(request.message, "pose"), "covariance");
  EXPECT_DOUBLE_EQ(covariance.at(0)->as_double(), 4.0);
  EXPECT_DOUBLE_EQ(covariance.at(35)->as_double(), 0.5);
}

TEST(SpatialPosePublish, RefusesAnInvalidPose)
{
  const auto request =
    make_pose_publish(default_publish_config(SpatialPublishTarget::GoalPose), PosePickResult{});
  EXPECT_FALSE(request.ok);
  EXPECT_EQ(request.error, "no pose was picked");
}

// --- navigation goals -------------------------------------------------------

TEST(SpatialNavGoalPublish, UsesTheGoalsOwnFrame)
{
  NavGoal goal;
  goal.valid = true;
  goal.frame_id = "odom";
  goal.position = QarVector3{1.0f, 2.0f, 0.0f};
  goal.orientation = yaw_to_quaternion(0.0f);
  const auto request =
    make_nav_goal_publish(default_publish_config(SpatialPublishTarget::GoalPose), goal);
  ASSERT_TRUE(request.ok) << request.error;
  EXPECT_EQ(at(at(request.message, "header"), "frame_id").as_string(), "odom");
}

TEST(SpatialNavGoalPublish, CanAlsoGoOutAsAnInitialPose)
{
  NavGoal goal;
  goal.valid = true;
  goal.frame_id = "map";
  goal.position = QarVector3{5.0f, 6.0f, 0.0f};
  goal.orientation = yaw_to_quaternion(3.14159265f);
  const auto request =
    make_nav_goal_publish(default_publish_config(SpatialPublishTarget::InitialPose), goal);
  ASSERT_TRUE(request.ok) << request.error;
  EXPECT_EQ(request.topic, "/initialpose");
  EXPECT_EQ(at(at(request.message, "pose"), "covariance").size(), 36u);
}

TEST(SpatialNavGoalPublish, RefusesAnInvalidGoalWithItsOwnReason)
{
  NavGoal goal;
  goal.error = "a navigation goal needs a frame id";
  const auto request =
    make_nav_goal_publish(default_publish_config(SpatialPublishTarget::GoalPose), goal);
  EXPECT_FALSE(request.ok);
  EXPECT_EQ(request.error, "a navigation goal needs a frame id");
}

// --- areas ------------------------------------------------------------------

TEST(SpatialAreaPublish, RectangleBecomesFourPolygonPoints)
{
  const auto area = make_rectangle(
    PickTarget::ground(), QarVector3{0.0f, 0.0f, 0.0f}, QarVector3{2.0f, 3.0f, 0.0f});
  ASSERT_TRUE(area.valid) << area.error;
  const auto request =
    make_area_publish(default_publish_config(SpatialPublishTarget::Area), area);
  ASSERT_TRUE(request.ok) << request.error;
  const UiValue & points = at(at(request.message, "polygon"), "points");
  ASSERT_EQ(points.size(), 4u);
  EXPECT_DOUBLE_EQ(at(*points.at(1), "x").as_double(), 2.0);
  EXPECT_DOUBLE_EQ(at(*points.at(2), "y").as_double(), 3.0);
}

TEST(SpatialAreaPublish, CircleIsSampledIntoSegments)
{
  const auto area = make_circle(
    PickTarget::ground(), QarVector3{0.0f, 0.0f, 0.0f}, QarVector3{2.0f, 0.0f, 0.0f});
  ASSERT_TRUE(area.valid) << area.error;
  auto config = default_publish_config(SpatialPublishTarget::Area);
  config.circle_segments = 8;
  const auto request = make_area_publish(config, area);
  ASSERT_TRUE(request.ok) << request.error;
  const UiValue & points = at(at(request.message, "polygon"), "points");
  ASSERT_EQ(points.size(), 8u);
  // Every sampled point sits on the rim, in the plane the circle was picked on.
  for (std::size_t i = 0; i < points.size(); ++i) {
    const double x = at(*points.at(i), "x").as_double();
    const double y = at(*points.at(i), "y").as_double();
    EXPECT_NEAR(std::sqrt(x * x + y * y), 2.0, 1e-4);
    EXPECT_NEAR(at(*points.at(i), "z").as_double(), 0.0, 1e-6);
  }
  // The first sample starts at the rim point the operator dragged to.
  EXPECT_NEAR(at(*points.at(0), "x").as_double(), 2.0, 1e-4);
}

TEST(SpatialAreaPublish, RefusesTooFewSegments)
{
  const auto area = make_circle(
    PickTarget::ground(), QarVector3{0.0f, 0.0f, 0.0f}, QarVector3{2.0f, 0.0f, 0.0f});
  auto config = default_publish_config(SpatialPublishTarget::Area);
  config.circle_segments = 2;
  const auto request = make_area_publish(config, area);
  EXPECT_FALSE(request.ok);
}

TEST(SpatialAreaPublish, RefusesAnInvalidArea)
{
  const auto area = make_rectangle(
    PickTarget::ground(), QarVector3{1.0f, 1.0f, 0.0f}, QarVector3{1.0f, 4.0f, 0.0f});
  ASSERT_FALSE(area.valid);
  const auto request =
    make_area_publish(default_publish_config(SpatialPublishTarget::Area), area);
  EXPECT_FALSE(request.ok);
  EXPECT_EQ(request.error, "rectangle has zero extent in one direction");
}

TEST(SpatialAreaPublish, BarePolygonHasNoHeader)
{
  const auto area = make_rectangle(
    PickTarget::ground(), QarVector3{0.0f, 0.0f, 0.0f}, QarVector3{1.0f, 1.0f, 0.0f});
  auto config = default_publish_config(SpatialPublishTarget::Area);
  config.type_name = "geometry_msgs/msg/Polygon";
  const auto request = make_area_publish(config, area);
  ASSERT_TRUE(request.ok) << request.error;
  EXPECT_FALSE(request.message.contains("header"));
  EXPECT_EQ(at(request.message, "points").size(), 4u);
}
