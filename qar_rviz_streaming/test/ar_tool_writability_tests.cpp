// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Which AR tools publish by default: the table of keyboard-free topic types, the reasons, and
// the cross-check that the writable rows are claimed by the templates the table names.

#include "qar_rviz_streaming/ar_tool_writability.hpp"

#include <string>

#include <gtest/gtest.h>

#include "qar_rviz_streaming/ar_semantic_templates.hpp"

namespace
{

using qar_rviz_streaming::ArToolWriteDefault;
using qar_rviz_streaming::default_write_for;
using qar_rviz_streaming::keyboard_free_topic_types;
using qar_rviz_streaming::read_only_reason;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::semantic_template_for_type;

ArToolWriteDefault topic(const std::string & type_name)
{
  return default_write_for(RosEntityKind::Topic, type_name);
}

}  // namespace

TEST(ArToolWritability, BoolAndTwistAreWritable)
{
  const ArToolWriteDefault bool_row = topic("std_msgs/msg/Bool");
  EXPECT_TRUE(bool_row.writable);
  EXPECT_EQ(bool_row.reason, "one switch");

  for (const char * type : {"geometry_msgs/msg/Twist", "geometry_msgs/msg/TwistStamped"}) {
    const ArToolWriteDefault twist = topic(type);
    EXPECT_TRUE(twist.writable) << type;
    EXPECT_EQ(twist.reason, "sliders and a stop button") << type;
  }
  // The table lists exactly the writable rows, so a new row is a one-line change.
  EXPECT_EQ(keyboard_free_topic_types().size(), 3u);
}

// The generated page of std_msgs/msg/Empty keeps the placeholder member as a number field (see
// ArPanelGeneratorTest.EmptyMessageKeepsItsPlaceholderInputBesidePublish), so it is not a button
// only and stays read-only until the generator hides that member.
TEST(ArToolWritability, EmptyIsReadOnlyBecauseItsPageKeepsAPlaceholderField)
{
  const ArToolWriteDefault empty = topic("std_msgs/msg/Empty");
  EXPECT_FALSE(empty.writable);
  EXPECT_EQ(empty.reason, "its page has a placeholder number field");
}

TEST(ArToolWritability, NumbersStringsPosesAreReadOnly)
{
  for (const char * type :
    {"std_msgs/msg/Byte", "std_msgs/msg/Char", "std_msgs/msg/Int8", "std_msgs/msg/UInt8",
      "std_msgs/msg/Int16", "std_msgs/msg/UInt16", "std_msgs/msg/Int32", "std_msgs/msg/UInt32",
      "std_msgs/msg/Int64", "std_msgs/msg/UInt64", "std_msgs/msg/Float32",
      "std_msgs/msg/Float64"})
  {
    const ArToolWriteDefault number = topic(type);
    EXPECT_FALSE(number.writable) << type;
    EXPECT_EQ(number.reason, "a number needs a keyboard") << type;
  }

  const ArToolWriteDefault text = topic("std_msgs/msg/String");
  EXPECT_FALSE(text.writable);
  EXPECT_EQ(text.reason, "text needs a keyboard");

  // Every type the pose template claims is read-only with the pose reason.
  const std::string pose_reason = "coordinates need a keyboard; use Picking to place a pose";
  for (const char * type :
    {"geometry_msgs/msg/Pose", "geometry_msgs/msg/PoseStamped", "geometry_msgs/msg/Point",
      "geometry_msgs/msg/PointStamped", "geometry_msgs/msg/Quaternion",
      "geometry_msgs/msg/Transform", "geometry_msgs/msg/TransformStamped",
      "geometry_msgs/msg/PoseWithCovarianceStamped"})
  {
    const ArToolWriteDefault pose = topic(type);
    EXPECT_FALSE(pose.writable) << type;
    EXPECT_EQ(pose.reason, pose_reason) << type;
  }
  for (const auto & info : qar_rviz_streaming::semantic_template_catalog()) {
    if (info.name != "pose") {
      continue;
    }
    for (const std::string & type : info.types) {
      EXPECT_FALSE(topic(type).writable) << type;
      EXPECT_EQ(topic(type).reason, pose_reason) << type;
    }
  }

  for (const char * type :
    {"sensor_msgs/msg/BatteryState", "nav_msgs/msg/Odometry", "sensor_msgs/msg/Image",
      "sensor_msgs/msg/PointCloud2", "tf2_msgs/msg/TFMessage", "sensor_msgs/msg/Imu"})
  {
    const ArToolWriteDefault status = topic(type);
    EXPECT_FALSE(status.writable) << type;
    EXPECT_EQ(status.reason, "a status topic: read-only") << type;
  }
}

TEST(ArToolWritability, CovarianceTwistIsReadOnly)
{
  for (const char * type :
    {"geometry_msgs/msg/TwistWithCovariance", "geometry_msgs/msg/TwistWithCovarianceStamped"})
  {
    const ArToolWriteDefault twist = topic(type);
    EXPECT_FALSE(twist.writable) << type;
    EXPECT_EQ(twist.reason, "a covariance needs a keyboard") << type;
    EXPECT_EQ(read_only_reason(type), "a covariance needs a keyboard") << type;
  }
}

TEST(ArToolWritability, ServicesAndActionsKeepTheirForms)
{
  // A service or an action is not governed by the table, whatever its type is called.
  const ArToolWriteDefault service =
    default_write_for(RosEntityKind::Service, "std_srvs/srv/Trigger");
  EXPECT_TRUE(service.writable);
  EXPECT_EQ(service.reason, "its request form");

  const ArToolWriteDefault action =
    default_write_for(RosEntityKind::Action, "example_interfaces/action/Fibonacci");
  EXPECT_TRUE(action.writable);
  EXPECT_EQ(action.reason, "its goal form");

  // Not even an unknown type changes that.
  EXPECT_TRUE(default_write_for(RosEntityKind::Service, "").writable);
  EXPECT_TRUE(default_write_for(RosEntityKind::Action, "").writable);
}

TEST(ArToolWritability, AnEmptyTypeIsReadOnly)
{
  const ArToolWriteDefault none = topic("");
  EXPECT_FALSE(none.writable);
  EXPECT_FALSE(none.reason.empty());
  // The name has to match exactly: nothing is guessed from a similar spelling.
  EXPECT_FALSE(topic("std_msgs/Bool").writable);
  EXPECT_FALSE(topic("std_msgs/msg/bool").writable);
  EXPECT_FALSE(topic("my_pkg/msg/Twist").writable);
}

TEST(ArToolWritability, EveryWritableTypeIsClaimedByItsTemplate)
{
  // The table names the template of each writable row: Bool is a switch of the scalar-status
  // template, the two Twists are sliders and a stop button of the velocity-command template.
  for (const auto & row : keyboard_free_topic_types()) {
    const std::string type = row.type_name;
    const std::string template_name = semantic_template_for_type(type);
    if (type == "std_msgs/msg/Bool") {
      EXPECT_EQ(template_name, "scalar-status") << type;
    } else if (type == "geometry_msgs/msg/Twist" || type == "geometry_msgs/msg/TwistStamped") {
      EXPECT_EQ(template_name, "velocity-command") << type;
    } else {
      ADD_FAILURE() << "a writable row the test does not know: " << type;
    }
  }
  // The covariance twists share the velocity template but are not writable by default.
  EXPECT_EQ(semantic_template_for_type("geometry_msgs/msg/TwistWithCovariance"),
    "velocity-command");
  EXPECT_FALSE(topic("geometry_msgs/msg/TwistWithCovariance").writable);
}
