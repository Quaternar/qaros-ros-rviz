// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The semantic AR templates. Pure C++17 + qar_ui_model — no ROS graph, no Qt, no
// RViz — so every layout decision is testable without a robot.
//
// The message descriptions below are hand-written to match the real IDL field
// for field. `ar_semantic_templates_ros_tests.cpp` re-runs the same contract
// against the descriptions `qar_ros_ui::introspect_message` reads out of the
// distro's own typesupport, so drift between these two is a build failure
// rather than a surprise in a headset.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "qar_rviz_streaming/ar_semantic_templates.hpp"

using qar_rviz_streaming::elide_large_arrays;
using qar_rviz_streaming::kMaxStreamedArrayElements;
using qar_rviz_streaming::make_panel_id;
using qar_rviz_streaming::PanelGenerationRequest;
using qar_rviz_streaming::PanelGenerationResult;
using qar_rviz_streaming::PanelTemplateRegistry;
using qar_rviz_streaming::register_semantic_templates;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::RosEntityRef;
using qar_rviz_streaming::semantic_template_catalog;
using qar_rviz_streaming::semantic_template_for_type;
using qar_ui_model::FieldDescription;
using qar_ui_model::FieldKind;
using qar_ui_model::MessageDescriptionBuilder;
using qar_ui_model::MessageDescriptionPtr;
using qar_ui_model::UiValue;
using qar_ui_model::Widget;
using qar_ui_model::WidgetType;

namespace ar_actions = qar_rviz_streaming::ar_actions;
namespace ar_doc = qar_rviz_streaming::ar_doc;

namespace
{

// --- Message descriptions, field for field with the real IDL -----------------

MessageDescriptionPtr time_type()
{
  return MessageDescriptionBuilder("builtin_interfaces/msg/Time")
         .field(FieldDescription::primitive("sec", FieldKind::Int32))
         .field(FieldDescription::primitive("nanosec", FieldKind::UInt32))
         .build();
}

MessageDescriptionPtr header_type()
{
  return MessageDescriptionBuilder("std_msgs/msg/Header")
         .field(FieldDescription::nested("stamp", time_type()))
         .field(FieldDescription::primitive("frame_id", FieldKind::String))
         .build();
}

MessageDescriptionPtr vector3_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Vector3")
         .field(FieldDescription::primitive("x", FieldKind::Float64))
         .field(FieldDescription::primitive("y", FieldKind::Float64))
         .field(FieldDescription::primitive("z", FieldKind::Float64))
         .build();
}

MessageDescriptionPtr point_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Point")
         .field(FieldDescription::primitive("x", FieldKind::Float64))
         .field(FieldDescription::primitive("y", FieldKind::Float64))
         .field(FieldDescription::primitive("z", FieldKind::Float64))
         .build();
}

MessageDescriptionPtr quaternion_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Quaternion")
         .field(FieldDescription::primitive("x", FieldKind::Float64))
         .field(FieldDescription::primitive("y", FieldKind::Float64))
         .field(FieldDescription::primitive("z", FieldKind::Float64))
         .field(FieldDescription::primitive("w", FieldKind::Float64))
         .build();
}

MessageDescriptionPtr pose_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Pose")
         .field(FieldDescription::nested("position", point_type()))
         .field(FieldDescription::nested("orientation", quaternion_type()))
         .build();
}

MessageDescriptionPtr pose_stamped_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/PoseStamped")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::nested("pose", pose_type()))
         .build();
}

MessageDescriptionPtr pose_with_covariance_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/PoseWithCovariance")
         .field(FieldDescription::nested("pose", pose_type()))
         .field(FieldDescription::primitive("covariance", FieldKind::Float64).fixed_array(36))
         .build();
}

MessageDescriptionPtr transform_stamped_type()
{
  MessageDescriptionPtr transform = MessageDescriptionBuilder("geometry_msgs/msg/Transform")
    .field(FieldDescription::nested("translation", vector3_type()))
    .field(FieldDescription::nested("rotation", quaternion_type()))
    .build();
  return MessageDescriptionBuilder("geometry_msgs/msg/TransformStamped")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("child_frame_id", FieldKind::String))
         .field(FieldDescription::nested("transform", transform))
         .build();
}

MessageDescriptionPtr twist_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Twist")
         .field(FieldDescription::nested("linear", vector3_type()))
         .field(FieldDescription::nested("angular", vector3_type()))
         .build();
}

MessageDescriptionPtr twist_with_covariance_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/TwistWithCovariance")
         .field(FieldDescription::nested("twist", twist_type()))
         .field(FieldDescription::primitive("covariance", FieldKind::Float64).fixed_array(36))
         .build();
}

MessageDescriptionPtr battery_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/BatteryState")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("voltage", FieldKind::Float32))
         .field(FieldDescription::primitive("temperature", FieldKind::Float32))
         .field(FieldDescription::primitive("current", FieldKind::Float32))
         .field(FieldDescription::primitive("charge", FieldKind::Float32))
         .field(FieldDescription::primitive("capacity", FieldKind::Float32))
         .field(FieldDescription::primitive("design_capacity", FieldKind::Float32))
         .field(FieldDescription::primitive("percentage", FieldKind::Float32))
         .field(FieldDescription::primitive("power_supply_status", FieldKind::UInt8))
         .field(FieldDescription::primitive("power_supply_health", FieldKind::UInt8))
         .field(FieldDescription::primitive("power_supply_technology", FieldKind::UInt8))
         .field(FieldDescription::primitive("present", FieldKind::Bool))
         .field(FieldDescription::primitive("cell_voltage", FieldKind::Float32).unbounded_array())
         .field(
    FieldDescription::primitive("cell_temperature", FieldKind::Float32).unbounded_array())
         .field(FieldDescription::primitive("location", FieldKind::String))
         .field(FieldDescription::primitive("serial_number", FieldKind::String))
         .build();
}

MessageDescriptionPtr diagnostic_status_type()
{
  MessageDescriptionPtr key_value = MessageDescriptionBuilder("diagnostic_msgs/msg/KeyValue")
    .field(FieldDescription::primitive("key", FieldKind::String))
    .field(FieldDescription::primitive("value", FieldKind::String))
    .build();
  return MessageDescriptionBuilder("diagnostic_msgs/msg/DiagnosticStatus")
         .field(FieldDescription::primitive("level", FieldKind::Byte))
         .field(FieldDescription::primitive("name", FieldKind::String))
         .field(FieldDescription::primitive("message", FieldKind::String))
         .field(FieldDescription::primitive("hardware_id", FieldKind::String))
         .field(FieldDescription::nested("values", key_value).unbounded_array())
         .build();
}

MessageDescriptionPtr diagnostic_array_type()
{
  return MessageDescriptionBuilder("diagnostic_msgs/msg/DiagnosticArray")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::nested("status", diagnostic_status_type()).unbounded_array())
         .build();
}

MessageDescriptionPtr odometry_type()
{
  return MessageDescriptionBuilder("nav_msgs/msg/Odometry")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("child_frame_id", FieldKind::String))
         .field(FieldDescription::nested("pose", pose_with_covariance_type()))
         .field(FieldDescription::nested("twist", twist_with_covariance_type()))
         .build();
}

MessageDescriptionPtr path_type()
{
  return MessageDescriptionBuilder("nav_msgs/msg/Path")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::nested("poses", pose_stamped_type()).unbounded_array())
         .build();
}

MessageDescriptionPtr tf_message_type()
{
  return MessageDescriptionBuilder("tf2_msgs/msg/TFMessage")
         .field(
    FieldDescription::nested("transforms", transform_stamped_type()).unbounded_array())
         .build();
}

MessageDescriptionPtr occupancy_grid_type()
{
  MessageDescriptionPtr meta = MessageDescriptionBuilder("nav_msgs/msg/MapMetaData")
    .field(FieldDescription::nested("map_load_time", time_type()))
    .field(FieldDescription::primitive("resolution", FieldKind::Float32))
    .field(FieldDescription::primitive("width", FieldKind::UInt32))
    .field(FieldDescription::primitive("height", FieldKind::UInt32))
    .field(FieldDescription::nested("origin", pose_type()))
    .build();
  return MessageDescriptionBuilder("nav_msgs/msg/OccupancyGrid")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::nested("info", meta))
         .field(FieldDescription::primitive("data", FieldKind::Int8).unbounded_array())
         .build();
}

MessageDescriptionPtr laser_scan_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/LaserScan")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("angle_min", FieldKind::Float32))
         .field(FieldDescription::primitive("angle_max", FieldKind::Float32))
         .field(FieldDescription::primitive("angle_increment", FieldKind::Float32))
         .field(FieldDescription::primitive("time_increment", FieldKind::Float32))
         .field(FieldDescription::primitive("scan_time", FieldKind::Float32))
         .field(FieldDescription::primitive("range_min", FieldKind::Float32))
         .field(FieldDescription::primitive("range_max", FieldKind::Float32))
         .field(FieldDescription::primitive("ranges", FieldKind::Float32).unbounded_array())
         .field(FieldDescription::primitive("intensities", FieldKind::Float32).unbounded_array())
         .build();
}

MessageDescriptionPtr image_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/Image")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("height", FieldKind::UInt32))
         .field(FieldDescription::primitive("width", FieldKind::UInt32))
         .field(FieldDescription::primitive("encoding", FieldKind::String))
         .field(FieldDescription::primitive("is_bigendian", FieldKind::UInt8))
         .field(FieldDescription::primitive("step", FieldKind::UInt32))
         .field(FieldDescription::primitive("data", FieldKind::UInt8).unbounded_array())
         .build();
}

MessageDescriptionPtr compressed_image_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/CompressedImage")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("format", FieldKind::String))
         .field(FieldDescription::primitive("data", FieldKind::UInt8).unbounded_array())
         .build();
}

MessageDescriptionPtr point_cloud_type()
{
  MessageDescriptionPtr field_type = MessageDescriptionBuilder("sensor_msgs/msg/PointField")
    .field(FieldDescription::primitive("name", FieldKind::String))
    .field(FieldDescription::primitive("offset", FieldKind::UInt32))
    .field(FieldDescription::primitive("datatype", FieldKind::UInt8))
    .field(FieldDescription::primitive("count", FieldKind::UInt32))
    .build();
  return MessageDescriptionBuilder("sensor_msgs/msg/PointCloud2")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("height", FieldKind::UInt32))
         .field(FieldDescription::primitive("width", FieldKind::UInt32))
         .field(FieldDescription::nested("fields", field_type).unbounded_array())
         .field(FieldDescription::primitive("is_bigendian", FieldKind::Bool))
         .field(FieldDescription::primitive("point_step", FieldKind::UInt32))
         .field(FieldDescription::primitive("row_step", FieldKind::UInt32))
         .field(FieldDescription::primitive("data", FieldKind::UInt8).unbounded_array())
         .field(FieldDescription::primitive("is_dense", FieldKind::Bool))
         .build();
}

MessageDescriptionPtr imu_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/Imu")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::nested("orientation", quaternion_type()))
         .field(
    FieldDescription::primitive("orientation_covariance", FieldKind::Float64).fixed_array(9))
         .field(FieldDescription::nested("angular_velocity", vector3_type()))
         .field(
    FieldDescription::primitive("angular_velocity_covariance", FieldKind::Float64)
    .fixed_array(9))
         .field(FieldDescription::nested("linear_acceleration", vector3_type()))
         .field(
    FieldDescription::primitive("linear_acceleration_covariance", FieldKind::Float64)
    .fixed_array(9))
         .build();
}

MessageDescriptionPtr navsat_type()
{
  MessageDescriptionPtr status = MessageDescriptionBuilder("sensor_msgs/msg/NavSatStatus")
    .field(FieldDescription::primitive("status", FieldKind::Int8))
    .field(FieldDescription::primitive("service", FieldKind::UInt16))
    .build();
  return MessageDescriptionBuilder("sensor_msgs/msg/NavSatFix")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::nested("status", status))
         .field(FieldDescription::primitive("latitude", FieldKind::Float64))
         .field(FieldDescription::primitive("longitude", FieldKind::Float64))
         .field(FieldDescription::primitive("altitude", FieldKind::Float64))
         .field(
    FieldDescription::primitive("position_covariance", FieldKind::Float64).fixed_array(9))
         .field(FieldDescription::primitive("position_covariance_type", FieldKind::UInt8))
         .build();
}

MessageDescriptionPtr joint_state_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/JointState")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("name", FieldKind::String).unbounded_array())
         .field(FieldDescription::primitive("position", FieldKind::Float64).unbounded_array())
         .field(FieldDescription::primitive("velocity", FieldKind::Float64).unbounded_array())
         .field(FieldDescription::primitive("effort", FieldKind::Float64).unbounded_array())
         .build();
}

MessageDescriptionPtr std_msgs_type(const std::string & name, FieldKind kind)
{
  return MessageDescriptionBuilder("std_msgs/msg/" + name)
         .field(FieldDescription::primitive("data", kind))
         .build();
}

// --- Requests ----------------------------------------------------------------

PanelGenerationRequest topic(
  const std::string & name, const std::string & type, MessageDescriptionPtr description,
  bool writable = false)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Topic, name, type};
  request.message = std::move(description);
  request.writable = writable;
  return request;
}

PanelTemplateRegistry semantic_registry()
{
  PanelTemplateRegistry registry;
  register_semantic_templates(registry);
  return registry;
}

PanelGenerationResult generate(const PanelGenerationRequest & request)
{
  return semantic_registry().generate(request);
}

// --- Contract checks reused by every template --------------------------------

std::vector<const Widget *> flatten(const Widget & root)
{
  std::vector<const Widget *> out;
  qar_ui_model::walk_widgets(
    root, [&](const Widget & widget, const Widget *, std::size_t) {
      out.push_back(&widget);
      return qar_ui_model::WalkControl::Continue;
    });
  return out;
}

const Widget * find_bound(const Widget & root, const std::string & binding)
{
  for (const Widget * widget : flatten(root)) {
    if (widget->binding == binding) {
      return widget;
    }
  }
  return nullptr;
}

bool binds_anything_under(const Widget & root, const std::string & prefix)
{
  for (const Widget * widget : flatten(root)) {
    if (widget->binding == prefix || widget->binding.rfind(prefix + ".", 0) == 0 ||
      widget->binding.rfind(prefix + "[", 0) == 0)
    {
      return true;
    }
  }
  return false;
}

std::size_t count_type(const Widget & root, WidgetType type)
{
  std::size_t count = 0;
  for (const Widget * widget : flatten(root)) {
    count += (widget->type == type) ? 1u : 0u;
  }
  return count;
}

/// Everything the panel generator fixes and a semantic template may not bend:
/// only the five action ids exist, each applies to this entity kind, and
/// nothing editable writes outside a writable section.
void expect_contract(const PanelGenerationResult & result, const PanelGenerationRequest & request)
{
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.document_schema) << "no document schema";
  for (const Widget * widget : flatten(result.model.root)) {
    if (!widget->action.empty()) {
      const bool known = widget->action == ar_actions::kSetField ||
        widget->action == ar_actions::kPublish || widget->action == ar_actions::kCall ||
        widget->action == ar_actions::kSendGoal || widget->action == ar_actions::kCancelGoal ||
        widget->action == ar_actions::kOpenVideo;
      EXPECT_TRUE(known) << widget->id << " invented the action '" << widget->action << "'";
      if (widget->action == ar_actions::kPublish) {
        EXPECT_EQ(request.entity.kind, RosEntityKind::Topic) << widget->id;
      }
      if (widget->action == ar_actions::kCall) {
        EXPECT_EQ(request.entity.kind, RosEntityKind::Service) << widget->id;
      }
      if (widget->action == ar_actions::kSendGoal || widget->action == ar_actions::kCancelGoal) {
        EXPECT_EQ(request.entity.kind, RosEntityKind::Action) << widget->id;
      }
      if (widget->action == ar_actions::kOpenVideo) {
        // Host-only: no ROS operation, so it is exempt from the editable/
        // writable-section rule below (it carries no binding at all).
        EXPECT_EQ(request.entity.kind, RosEntityKind::Topic) << widget->id;
        EXPECT_FALSE(widget->editable) << widget->id;
        continue;
      }
    }
    if (widget->editable) {
      ASSERT_FALSE(widget->binding.empty()) << widget->id << " is editable with no binding";
      const std::string section = widget->binding.substr(0, widget->binding.find('.'));
      EXPECT_TRUE(qar_rviz_streaming::is_writable_document_section(section))
        << widget->id << " edits the read-only section '" << section << "'";
      EXPECT_EQ(widget->action, ar_actions::kSetField) << widget->id;
    }
  }
}

}  // namespace

// --- The catalog -------------------------------------------------------------

TEST(ArSemanticCatalogTest, EveryTypeIsClaimedByExactlyOneTemplate)
{
  std::vector<std::string> seen;
  for (const auto & info : semantic_template_catalog()) {
    EXPECT_FALSE(info.name.empty());
    EXPECT_FALSE(info.summary.empty()) << info.name;
    EXPECT_FALSE(info.types.empty()) << info.name;
    EXPECT_GT(info.score, 1) << info.name << " must outscore the generic fallback";
    for (const std::string & type : info.types) {
      EXPECT_EQ(type, qar_ui_model::normalize_type_name(type)) << type << " is not canonical";
      EXPECT_EQ(std::count(seen.begin(), seen.end(), type), 0) << type << " is claimed twice";
      seen.push_back(type);
      EXPECT_EQ(semantic_template_for_type(type), info.name);
    }
  }
  EXPECT_GT(seen.size(), 40u);
}

TEST(ArSemanticCatalogTest, RegistrationKeepsTheGenericFallbackLast)
{
  const PanelTemplateRegistry registry = semantic_registry();
  const std::vector<std::string> names = registry.template_names();
  ASSERT_FALSE(names.empty());
  EXPECT_EQ(names.front(), "generic-form") << "the fallback is registered first and never wins";
  EXPECT_EQ(names.size(), semantic_template_catalog().size() + 1u);
}

TEST(ArSemanticCatalogTest, AnUnclaimedTypeStillGetsTheGenericForm)
{
  const PanelGenerationResult result = generate(
    topic("/custom", "my_pkg/msg/Widget", std_msgs_type("Custom", FieldKind::Float64)));
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.template_name, "generic-form");
  EXPECT_TRUE(semantic_template_for_type("my_pkg/msg/Widget").empty());
}

TEST(ArSemanticCatalogTest, TheLegacyTwoPartTypeNameStillMatches)
{
  const PanelGenerationResult result =
    generate(topic("/cmd_vel", "geometry_msgs/Twist", twist_type()));
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.template_name, "velocity-command");
}

// --- 1. Pose and orientation -------------------------------------------------

TEST(ArPoseTemplateTest, PoseStampedShowsPositionOrientationFrameAndStamp)
{
  const PanelGenerationRequest request =
    topic("/goal_pose", "geometry_msgs/msg/PoseStamped", pose_stamped_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "pose");

  const Widget * position = find_bound(result.model.root, "msg.pose.position");
  ASSERT_NE(position, nullptr);
  EXPECT_EQ(position->type, WidgetType::Vector3);
  EXPECT_EQ(position->unit, "m");

  const Widget * orientation = find_bound(result.model.root, "msg.pose.orientation");
  ASSERT_NE(orientation, nullptr);
  EXPECT_EQ(orientation->type, WidgetType::Quaternion);
  // Roll/pitch/yaw next to x/y/z/w: an operator never types a quaternion.
  EXPECT_EQ(orientation->quaternion_display, qar_ui_model::QuaternionDisplay::Both);

  EXPECT_NE(find_bound(result.model.root, "msg.header.frame_id"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.header.stamp.sec"), nullptr);
}

TEST(ArPoseTemplateTest, AWritableGoalPoseIsEditedThroughTheDraftSectionOnly)
{
  const PanelGenerationRequest request =
    topic("/goal_pose", "geometry_msgs/msg/PoseStamped", pose_stamped_type(), true);
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));

  const Widget * draft = find_bound(result.model.root, "draft.pose.orientation");
  ASSERT_NE(draft, nullptr);
  EXPECT_TRUE(draft->editable);
  EXPECT_EQ(draft->action, ar_actions::kSetField);

  const Widget * live = find_bound(result.model.root, "msg.pose.orientation");
  ASSERT_NE(live, nullptr);
  EXPECT_FALSE(live->editable);

  const Widget * publish = result.model.find(make_panel_id(RosEntityKind::Topic, "/goal_pose") +
    ".publish");
  ASSERT_NE(publish, nullptr);
  EXPECT_EQ(publish->action, ar_actions::kPublish);
  EXPECT_TRUE(publish->confirm) << "a robot command is confirmed";
}

TEST(ArPoseTemplateTest, ABareQuaternionIsAnOrientationAndNotAPosition)
{
  const PanelGenerationRequest request =
    topic("/imu_orientation", "geometry_msgs/msg/Quaternion", quaternion_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  const Widget * orientation = find_bound(result.model.root, "msg");
  ASSERT_NE(orientation, nullptr);
  EXPECT_EQ(orientation->type, WidgetType::Quaternion);
  EXPECT_EQ(count_type(result.model.root, WidgetType::Vector3), 0u);
}

TEST(ArPoseTemplateTest, ABarePointIsAPosition)
{
  const PanelGenerationRequest request =
    topic("/clicked_point", "geometry_msgs/msg/Point", point_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  const Widget * position = find_bound(result.model.root, "msg");
  ASSERT_NE(position, nullptr);
  EXPECT_EQ(position->type, WidgetType::Vector3);
}

TEST(ArPoseTemplateTest, TransformStampedNamesBothFrames)
{
  const PanelGenerationRequest request =
    topic("/tf_one", "geometry_msgs/msg/TransformStamped", transform_stamped_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_NE(find_bound(result.model.root, "msg.header.frame_id"), nullptr) << "parent frame";
  EXPECT_NE(find_bound(result.model.root, "msg.child_frame_id"), nullptr) << "child frame";
  EXPECT_NE(find_bound(result.model.root, "msg.transform.translation"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.transform.rotation"), nullptr);
}

TEST(ArPoseTemplateTest, CovarianceIsAnInspectorRatherThanThirtySixNumberFields)
{
  const PanelGenerationRequest request = topic(
    "/amcl_pose", "geometry_msgs/msg/PoseWithCovarianceStamped",
    MessageDescriptionBuilder("geometry_msgs/msg/PoseWithCovarianceStamped")
    .field(FieldDescription::nested("header", header_type()))
    .field(FieldDescription::nested("pose", pose_with_covariance_type()))
    .build());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_NE(find_bound(result.model.root, "msg.pose.pose.position"), nullptr);
  const Widget * covariance = find_bound(result.model.root, "msg.pose.covariance");
  ASSERT_NE(covariance, nullptr);
  EXPECT_EQ(covariance->type, WidgetType::JsonInspector);
}

// --- 2. Velocity command -----------------------------------------------------

TEST(ArTwistTemplateTest, AReadOnlyTwistChartsTheSpeedItShows)
{
  const PanelGenerationRequest request = topic("/odom_vel", "geometry_msgs/msg/Twist",
    twist_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "velocity-command");
  EXPECT_NE(find_bound(result.model.root, "msg.linear"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.angular"), nullptr);
  EXPECT_EQ(count_type(result.model.root, WidgetType::TimeSeries), 2u);
  EXPECT_EQ(count_type(result.model.root, WidgetType::Slider), 0u) << "read-only: no controls";
}

TEST(ArTwistTemplateTest, TheCommandFormIsSlidersAndAOnePressStop)
{
  const PanelGenerationRequest request =
    topic("/cmd_vel", "geometry_msgs/msg/Twist", twist_type(), true);
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));

  const Widget * forward = find_bound(result.model.root, "draft.linear.x");
  ASSERT_NE(forward, nullptr);
  EXPECT_EQ(forward->type, WidgetType::Slider);
  EXPECT_TRUE(forward->min && forward->max);
  EXPECT_EQ(forward->unit, "m/s");
  EXPECT_NE(find_bound(result.model.root, "draft.angular.z"), nullptr);

  const Widget * stop = result.model.find(
    make_panel_id(RosEntityKind::Topic, "/cmd_vel") + ".stop");
  ASSERT_NE(stop, nullptr) << "a velocity panel without a stop is not a velocity panel";
  EXPECT_EQ(stop->action, ar_actions::kPublish);
  EXPECT_FALSE(stop->confirm) << "stop is the safe direction; it fires on the first press";
  // The preset the router publishes INSTEAD of whatever happens to be staged.
  ASSERT_TRUE(stop->payload.is_object());
  const UiValue * linear = stop->payload.find("linear");
  ASSERT_NE(linear, nullptr);
  const UiValue * x = linear->find("x");
  ASSERT_NE(x, nullptr);
  EXPECT_DOUBLE_EQ(x->as_double(-1.0), 0.0);
}

TEST(ArTwistTemplateTest, ADocumentedIdlRangeBeatsTheTemplateDefault)
{
  MessageDescriptionPtr limited_vector =
    MessageDescriptionBuilder("geometry_msgs/msg/Vector3")
    .field(FieldDescription::primitive("x", FieldKind::Float64).with_range(-2.5, 2.5).with_step(0.5))
    .field(FieldDescription::primitive("y", FieldKind::Float64))
    .field(FieldDescription::primitive("z", FieldKind::Float64))
    .build();
  MessageDescriptionPtr limited = MessageDescriptionBuilder("geometry_msgs/msg/Twist")
    .field(FieldDescription::nested("linear", limited_vector))
    .field(FieldDescription::nested("angular", vector3_type()))
    .build();
  const PanelGenerationResult result =
    generate(topic("/cmd_vel", "geometry_msgs/msg/Twist", limited, true));
  ASSERT_TRUE(result.ok) << result.error;
  const Widget * forward = find_bound(result.model.root, "draft.linear.x");
  ASSERT_NE(forward, nullptr);
  EXPECT_DOUBLE_EQ(*forward->max, 2.5);
  EXPECT_DOUBLE_EQ(*forward->step, 0.5);
}

TEST(ArTwistTemplateTest, TwistStampedFindsTheNestedTwist)
{
  MessageDescriptionPtr stamped = MessageDescriptionBuilder("geometry_msgs/msg/TwistStamped")
    .field(FieldDescription::nested("header", header_type()))
    .field(FieldDescription::nested("twist", twist_type()))
    .build();
  const PanelGenerationRequest request =
    topic("/cmd_vel_stamped", "geometry_msgs/msg/TwistStamped", stamped, true);
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_NE(find_bound(result.model.root, "draft.twist.linear.x"), nullptr);
}

// --- 3. Battery --------------------------------------------------------------

TEST(ArBatteryTemplateTest, ChargeIsAGaugeAndTheEnumsAreDecoded)
{
  const PanelGenerationRequest request =
    topic("/battery", "sensor_msgs/msg/BatteryState", battery_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "battery");

  const Widget * gauge = find_bound(result.model.root, "msg.percentage");
  ASSERT_NE(gauge, nullptr);
  EXPECT_EQ(gauge->type, WidgetType::Gauge);
  EXPECT_DOUBLE_EQ(*gauge->min, 0.0);
  EXPECT_DOUBLE_EQ(*gauge->max, 1.0);
  EXPECT_FALSE(gauge->rules.empty()) << "a gauge with no thresholds is just a number";

  const Widget * status = find_bound(result.model.root, "msg.power_supply_status");
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->type, WidgetType::StatusLight);
  bool decoded = false;
  for (const auto & rule : status->rules) {
    decoded = decoded || rule.text == "Discharging";
  }
  EXPECT_TRUE(decoded) << "power_supply_status 2 must read as text, not as '2'";

  const Widget * health = find_bound(result.model.root, "msg.power_supply_health");
  ASSERT_NE(health, nullptr);
  EXPECT_GE(health->rules.size(), 9u) << "every documented health value is decoded";

  EXPECT_NE(find_bound(result.model.root, "msg.voltage"), nullptr);
  const Widget * cells = find_bound(result.model.root, "msg.cell_voltage");
  ASSERT_NE(cells, nullptr);
  EXPECT_EQ(cells->type, WidgetType::Table);
}

// --- 4. Diagnostics ----------------------------------------------------------

TEST(ArDiagnosticsTemplateTest, EachComponentGetsALightDrivenByItsLevel)
{
  const PanelGenerationRequest request =
    topic("/diagnostics", "diagnostic_msgs/msg/DiagnosticArray", diagnostic_array_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "diagnostics");

  const Widget * first = find_bound(result.model.root, "msg.status[0].level");
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(first->type, WidgetType::StatusLight);
  ASSERT_EQ(first->rules.size(), 4u);
  EXPECT_EQ(first->rules[0].level, qar_ui_model::StatusLevel::Ok);
  EXPECT_EQ(first->rules[1].level, qar_ui_model::StatusLevel::Warning);
  EXPECT_EQ(first->rules[2].level, qar_ui_model::StatusLevel::Error);
  EXPECT_EQ(first->rules[3].level, qar_ui_model::StatusLevel::Stale);
  EXPECT_NE(find_bound(result.model.root, "msg.status[5].level"), nullptr);

  const Widget * table = find_bound(result.model.root, "msg.status");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->type, WidgetType::Table);
}

TEST(ArDiagnosticsTemplateTest, ASingleStatusGetsItsKeyValueTable)
{
  const PanelGenerationRequest request = topic(
    "/one_status", "diagnostic_msgs/msg/DiagnosticStatus", diagnostic_status_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_NE(find_bound(result.model.root, "msg.level"), nullptr);
  const Widget * values = find_bound(result.model.root, "msg.values");
  ASSERT_NE(values, nullptr);
  EXPECT_EQ(values->type, WidgetType::Table);
  ASSERT_EQ(values->columns.size(), 2u);
  EXPECT_EQ(values->columns[0].path, "key");
  EXPECT_EQ(values->columns[1].path, "value");
}

// --- 5. Navigation -----------------------------------------------------------

TEST(ArNavigationTemplateTest, OdometryReachesThroughBothCovarianceWrappers)
{
  const PanelGenerationRequest request = topic("/odom", "nav_msgs/msg/Odometry", odometry_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "odometry");
  EXPECT_NE(find_bound(result.model.root, "msg.pose.pose.position"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.pose.pose.orientation"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.twist.twist.linear"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.child_frame_id"), nullptr);
  EXPECT_EQ(count_type(result.model.root, WidgetType::TimeSeries), 1u);
}

TEST(ArNavigationTemplateTest, APathIsAPagedTableNotAThousandWidgets)
{
  const PanelGenerationRequest request = topic("/plan", "nav_msgs/msg/Path", path_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "path");
  const Widget * table = find_bound(result.model.root, "msg.poses");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->type, WidgetType::Table);
  ASSERT_EQ(table->columns.size(), 4u);
  EXPECT_EQ(table->columns[0].path, "pose.position.x");
  EXPECT_EQ(table->columns[3].path, "header.frame_id");
}

// --- 6. TF -------------------------------------------------------------------

TEST(ArTfTemplateTest, TfMessageListsParentToChild)
{
  const PanelGenerationRequest request = topic("/tf", "tf2_msgs/msg/TFMessage", tf_message_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "tf");
  const Widget * table = find_bound(result.model.root, "msg.transforms");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->type, WidgetType::Table);
  ASSERT_GE(table->columns.size(), 2u);
  EXPECT_EQ(table->columns[0].header, "Parent");
  EXPECT_EQ(table->columns[0].path, "header.frame_id");
  EXPECT_EQ(table->columns[1].header, "Child");
  EXPECT_EQ(table->columns[1].path, "child_frame_id");
}

// --- 7. Robot and system status ----------------------------------------------

TEST(ArScalarTemplateTest, ABoolIsALightAndANumberIsAChart)
{
  const PanelGenerationRequest bool_request =
    topic("/estop", "std_msgs/msg/Bool", std_msgs_type("Bool", FieldKind::Bool));
  const PanelGenerationResult as_light = generate(bool_request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(as_light, bool_request));
  EXPECT_EQ(as_light.template_name, "scalar-status");
  const Widget * light = find_bound(as_light.model.root, "msg.data");
  ASSERT_NE(light, nullptr);
  EXPECT_EQ(light->type, WidgetType::StatusLight);

  const PanelGenerationRequest number_request =
    topic("/cpu", "std_msgs/msg/Float64", std_msgs_type("Float64", FieldKind::Float64));
  const PanelGenerationResult as_chart = generate(number_request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(as_chart, number_request));
  EXPECT_EQ(count_type(as_chart.model.root, WidgetType::TimeSeries), 1u);
}

TEST(ArScalarTemplateTest, AWritableBoolBecomesAToggleInTheDraftSection)
{
  const PanelGenerationRequest request =
    topic("/estop", "std_msgs/msg/Bool", std_msgs_type("Bool", FieldKind::Bool), true);
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  const Widget * toggle = find_bound(result.model.root, "draft.data");
  ASSERT_NE(toggle, nullptr);
  EXPECT_EQ(toggle->type, WidgetType::Toggle);
  EXPECT_TRUE(toggle->editable);
}

TEST(ArJointStateTemplateTest, ParallelArraysAreLinedUpPerJoint)
{
  const PanelGenerationRequest request =
    topic("/joint_states", "sensor_msgs/msg/JointState", joint_state_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "joint-state");
  EXPECT_NE(find_bound(result.model.root, "msg.name[0]"), nullptr);
  const Widget * position = find_bound(result.model.root, "msg.position[0]");
  ASSERT_NE(position, nullptr);
  EXPECT_EQ(position->unit, "rad");
  EXPECT_NE(find_bound(result.model.root, "msg.effort[7]"), nullptr);
  const Widget * all = find_bound(result.model.root, "msg.name");
  ASSERT_NE(all, nullptr);
  EXPECT_EQ(all->type, WidgetType::Table);
}

TEST(ArServiceTemplateTest, TriggerIsOneButtonAndASuccessLight)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Service, "/reset", "std_srvs/srv/Trigger"};
  request.service.type_name = "std_srvs/srv/Trigger";
  request.service.request = MessageDescriptionBuilder("std_srvs/srv/Trigger_Request").build();
  request.service.response = MessageDescriptionBuilder("std_srvs/srv/Trigger_Response")
    .field(FieldDescription::primitive("success", FieldKind::Bool))
    .field(FieldDescription::primitive("message", FieldKind::String))
    .build();

  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_EQ(result.template_name, "robot-service");
  const Widget * success = find_bound(result.model.root, "response.success");
  ASSERT_NE(success, nullptr);
  EXPECT_EQ(success->type, WidgetType::StatusLight);
  EXPECT_NE(find_bound(result.model.root, "response.message"), nullptr);
  const Widget * call =
    result.model.find(make_panel_id(RosEntityKind::Service, "/reset") + ".call");
  ASSERT_NE(call, nullptr);
  EXPECT_EQ(call->action, ar_actions::kCall);
}

TEST(ArServiceTemplateTest, SetBoolPutsTheSwitchInTheRequest)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Service, "/enable", "std_srvs/srv/SetBool"};
  request.service.type_name = "std_srvs/srv/SetBool";
  request.service.request = MessageDescriptionBuilder("std_srvs/srv/SetBool_Request")
    .field(FieldDescription::primitive("data", FieldKind::Bool))
    .build();
  request.service.response = MessageDescriptionBuilder("std_srvs/srv/SetBool_Response")
    .field(FieldDescription::primitive("success", FieldKind::Bool))
    .field(FieldDescription::primitive("message", FieldKind::String))
    .build();

  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  const Widget * data = find_bound(result.model.root, "request.data");
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->type, WidgetType::Toggle);
  EXPECT_TRUE(data->editable);
  EXPECT_EQ(data->action, ar_actions::kSetField);
}

// --- 8. Maps and sensors: the hard no-bulk rule ------------------------------

TEST(ArSensorTemplateTest, NoSensorPanelEverBindsItsBulkPayload)
{
  struct Case
  {
    const char * name;
    const char * type;
    MessageDescriptionPtr description;
    const char * bulk;
    const char * expected_template;
  };
  const std::vector<Case> cases = {
    {"/map", "nav_msgs/msg/OccupancyGrid", occupancy_grid_type(), "msg.data", "occupancy-grid"},
    {"/scan", "sensor_msgs/msg/LaserScan", laser_scan_type(), "msg.ranges", "laser-scan"},
    {"/image_raw", "sensor_msgs/msg/Image", image_type(), "msg.data", "image"},
    {"/points", "sensor_msgs/msg/PointCloud2", point_cloud_type(), "msg.data", "point-cloud"},
  };
  for (const Case & test_case : cases) {
    const PanelGenerationRequest request =
      topic(test_case.name, test_case.type, test_case.description);
    const PanelGenerationResult result = generate(request);
    ASSERT_NO_FATAL_FAILURE(expect_contract(result, request)) << test_case.type;
    EXPECT_EQ(result.template_name, test_case.expected_template);
    EXPECT_FALSE(binds_anything_under(result.model.root, test_case.bulk))
      << test_case.type << " must never stream " << test_case.bulk;
    // ... and it is not a blank panel either: the summary is the point.
    EXPECT_GE(result.model.root.children.size(), 1u);
  }
}

TEST(ArSensorTemplateTest, TheOccupancyGridSummaryIsResolutionSizeAndOrigin)
{
  const PanelGenerationRequest request =
    topic("/map", "nav_msgs/msg/OccupancyGrid", occupancy_grid_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  EXPECT_NE(find_bound(result.model.root, "msg.info.resolution"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.info.width"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.info.height"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.info.origin.position"), nullptr);
  EXPECT_GE(count_type(result.model.root, WidgetType::Label), 1u) << "say where the map is drawn";
}

TEST(ArSensorTemplateTest, TheLaserScanSummaryIsItsGeometry)
{
  const PanelGenerationRequest request =
    topic("/scan", "sensor_msgs/msg/LaserScan", laser_scan_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  for (const char * binding :
    {"msg.angle_min", "msg.angle_max", "msg.angle_increment", "msg.range_min", "msg.range_max",
      "msg.scan_time"})
  {
    EXPECT_NE(find_bound(result.model.root, binding), nullptr) << binding;
  }
  EXPECT_FALSE(binds_anything_under(result.model.root, "msg.intensities"));
}

TEST(ArImageTemplateTest, BothImageAndCompressedImageOfferAnOpenVideoButtonAndSayPixelsArentSent)
{
  struct Case
  {
    const char * type;
    MessageDescriptionPtr description;
  };
  const std::vector<Case> cases = {
    {"sensor_msgs/msg/Image", image_type()},
    {"sensor_msgs/msg/CompressedImage", compressed_image_type()},
  };
  for (const Case & test_case : cases) {
    const PanelGenerationRequest request = topic("/cam/image_raw", test_case.type,
      test_case.description);
    const PanelGenerationResult result = generate(request);
    ASSERT_NO_FATAL_FAILURE(expect_contract(result, request)) << test_case.type;
    EXPECT_EQ(result.template_name, "image") << test_case.type;

    const Widget * button = nullptr;
    for (const Widget * widget : flatten(result.model.root)) {
      if (widget->action == ar_actions::kOpenVideo) {
        button = widget;
        break;
      }
    }
    ASSERT_NE(button, nullptr) << test_case.type << ": no 'Open video in AR' button";
    EXPECT_EQ(button->type, WidgetType::Button) << test_case.type;
    EXPECT_TRUE(button->binding.empty()) << test_case.type << ": a host action binds nothing";

    bool found_note = false;
    for (const Widget * widget : flatten(result.model.root)) {
      if (widget->type == WidgetType::Label &&
        widget->label.find("Open video in AR") != std::string::npos)
      {
        found_note = true;
        break;
      }
    }
    EXPECT_TRUE(found_note) << test_case.type << ": note no longer points at the button";
  }
}

TEST(ArImageTemplateTest, TheOpenVideoButtonIsAbsentFromAServiceOrActionPanel)
{
  // image_layout only ever runs for a Topic in practice (Image/CompressedImage
  // are message types, never a service or action type), but the template's own
  // gate is what this pins: a hypothetically-misrouted entity kind still gets
  // no button.
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Service, "/probe", "sensor_msgs/msg/Image"};
  request.service.request = image_type();
  request.service.response = MessageDescriptionBuilder("test/srv/Response").build();
  const PanelGenerationResult result = semantic_registry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  for (const Widget * widget : flatten(result.model.root)) {
    EXPECT_NE(widget->action, ar_actions::kOpenVideo) << widget->id;
  }
}

TEST(ArSensorTemplateTest, PointCloudShowsItsChannelsButNotItsPoints)
{
  const PanelGenerationRequest request =
    topic("/points", "sensor_msgs/msg/PointCloud2", point_cloud_type());
  const PanelGenerationResult result = generate(request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(result, request));
  const Widget * fields = find_bound(result.model.root, "msg.fields");
  ASSERT_NE(fields, nullptr);
  EXPECT_EQ(fields->type, WidgetType::Table);
  EXPECT_NE(find_bound(result.model.root, "msg.point_step"), nullptr);
}

TEST(ArSensorTemplateTest, ImuAndNavSatReadAsInstrumentsNotAsForms)
{
  const PanelGenerationRequest imu_request = topic("/imu", "sensor_msgs/msg/Imu", imu_type());
  const PanelGenerationResult imu = generate(imu_request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(imu, imu_request));
  EXPECT_EQ(count_type(imu.model.root, WidgetType::Quaternion), 1u);
  EXPECT_EQ(count_type(imu.model.root, WidgetType::Vector3), 2u);
  EXPECT_FALSE(binds_anything_under(imu.model.root, "msg.orientation_covariance"));

  const PanelGenerationRequest fix_request = topic("/fix", "sensor_msgs/msg/NavSatFix",
    navsat_type());
  const PanelGenerationResult fix = generate(fix_request);
  ASSERT_NO_FATAL_FAILURE(expect_contract(fix, fix_request));
  const Widget * status = find_bound(fix.model.root, "msg.status.status");
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->type, WidgetType::StatusLight);
  const Widget * latitude = find_bound(fix.model.root, "msg.latitude");
  ASSERT_NE(latitude, nullptr);
  EXPECT_EQ(*latitude->precision, 7) << "1 cm of latitude, or the readout is useless";
}

// --- Degrading rather than failing -------------------------------------------

TEST(ArSemanticFallbackTest, AClaimedTypeWithAnUnrecognisableSchemaFallsBackToTheForm)
{
  // Someone's fork of BatteryState with none of the fields the template probes.
  MessageDescriptionPtr strange = MessageDescriptionBuilder("sensor_msgs/msg/BatteryState")
    .field(FieldDescription::primitive("volts_times_ten", FieldKind::Int32))
    .build();
  const PanelGenerationRequest request =
    topic("/battery", "sensor_msgs/msg/BatteryState", strange);
  const PanelGenerationResult result = generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.template_name, "battery") << "the template still claimed it";
  EXPECT_NE(find_bound(result.model.root, "msg.volts_times_ten"), nullptr)
    << "and produced the generic form rather than a blank panel";
  const UiValue * fallback = result.model.metadata.find("fallback");
  ASSERT_NE(fallback, nullptr);
  EXPECT_TRUE(fallback->as_bool(false)) << "and said so in the metadata";
}

TEST(ArSemanticFallbackTest, EveryTemplateSurvivesAnEmptyDescriptionOfItsOwnType)
{
  for (const auto & info : semantic_template_catalog()) {
    for (const std::string & type : info.types) {
      if (type.find("/msg/") == std::string::npos) {
        continue;  // services and actions need their own halves
      }
      PanelGenerationRequest request;
      request.entity = RosEntityRef{RosEntityKind::Topic, "/probe", type};
      request.message = MessageDescriptionBuilder(type)
        .field(FieldDescription::primitive("unrelated", FieldKind::Int32))
        .build();
      const PanelGenerationResult result = generate(request);
      EXPECT_TRUE(result.ok) << type << ": " << result.error;
    }
  }
}

// --- Large payload elision ---------------------------------------------------

TEST(ArElideLargeArraysTest, AShortArrayIsUntouched)
{
  UiValue value = UiValue::object();
  qar_ui_model::UiArray ranges;
  for (int i = 0; i < 10; ++i) {
    ranges.push_back(UiValue(static_cast<double>(i)));
  }
  value.set("ranges", UiValue(ranges));
  EXPECT_EQ(elide_large_arrays(value, 256), 0u);
  EXPECT_EQ(value.find("ranges")->size(), 10u);
}

TEST(ArElideLargeArraysTest, AnImageWorthOfBytesIsCappedAndSaysHowMuchWasDropped)
{
  UiValue message = UiValue::object();
  qar_ui_model::UiArray data;
  for (int i = 0; i < 5000; ++i) {
    data.push_back(UiValue(static_cast<std::int64_t>(i % 256)));
  }
  message.set("width", UiValue(static_cast<std::int64_t>(100)));
  message.set("data", UiValue(data));

  EXPECT_EQ(elide_large_arrays(message, kMaxStreamedArrayElements), 1u);
  const UiValue * elided = message.find("data");
  ASSERT_NE(elided, nullptr);
  EXPECT_EQ(elided->size(), kMaxStreamedArrayElements + 1u) << "the marker is one extra element";
  const UiValue * marker = elided->at(kMaxStreamedArrayElements);
  ASSERT_NE(marker, nullptr);
  EXPECT_NE(marker->as_string().find("5000"), std::string::npos) << marker->as_string();
  EXPECT_EQ(message.find("width")->as_int(0), 100) << "scalars are untouched";
}

TEST(ArElideLargeArraysTest, NestedArraysAreCappedToo)
{
  UiValue root = UiValue::object();
  UiValue inner = UiValue::object();
  qar_ui_model::UiArray poses;
  for (int i = 0; i < 400; ++i) {
    poses.push_back(UiValue(static_cast<double>(i)));
  }
  inner.set("poses", UiValue(poses));
  root.set("msg", inner);
  EXPECT_EQ(elide_large_arrays(root, 100), 1u);
  EXPECT_EQ(root.find("msg")->find("poses")->size(), 101u);
}

TEST(ArElideLargeArraysTest, AnArrayOfMessagesKeepsItsElementsIntact)
{
  qar_ui_model::UiArray rows;
  for (int i = 0; i < 5; ++i) {
    UiValue row = UiValue::object();
    row.set("key", UiValue(std::string("k")));
    rows.push_back(row);
  }
  UiValue value = UiValue(rows);
  EXPECT_EQ(elide_large_arrays(value, 256), 0u);
  ASSERT_EQ(value.size(), 5u);
  EXPECT_TRUE(value.at(0)->is_object());
}
