// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The semantic templates against the REAL IDL.
//
// `ar_semantic_templates_tests.cpp` checks the layouts against hand-written
// descriptions, which proves the rules but not the assumptions: a template that
// probes `pose.pose.position` is only right if `nav_msgs/msg/Odometry` really
// nests a `PoseWithCovariance`. This file closes that gap by asking
// `qar_ros_ui::introspect_message` for the distro's own description — read out
// of the interface package's introspection typesupport, the same way the panel
// does at run time — and generating against that.
//
// A type the distro does not ship (nav2 is not in every install) is SKIPPED,
// not failed: the catalog deliberately claims types this repo cannot see.

#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "qar_ui_model/html_renderer.hpp"

#include "qar_ros_ui/introspection.hpp"

#include "qar_rviz_streaming/ar_semantic_templates.hpp"

using qar_rviz_streaming::PanelGenerationRequest;
using qar_rviz_streaming::PanelGenerationResult;
using qar_rviz_streaming::PanelTemplateRegistry;
using qar_rviz_streaming::register_semantic_templates;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::RosEntityRef;
using qar_rviz_streaming::semantic_template_catalog;
using qar_ui_model::Widget;
using qar_ui_model::WidgetType;

namespace ar_actions = qar_rviz_streaming::ar_actions;

namespace
{

PanelTemplateRegistry semantic_registry()
{
  PanelTemplateRegistry registry;
  register_semantic_templates(registry);
  return registry;
}

/// Fill a request from the distro. False when the type is not installed.
bool introspect(const std::string & type_name, bool writable, PanelGenerationRequest & out)
{
  out = PanelGenerationRequest{};
  out.writable = writable;
  if (type_name.find("/srv/") != std::string::npos) {
    out.entity = RosEntityRef{RosEntityKind::Service, "/probe", type_name};
    return qar_ros_ui::introspect_service(type_name, out.service).ok();
  }
  if (type_name.find("/action/") != std::string::npos) {
    out.entity = RosEntityRef{RosEntityKind::Action, "/probe", type_name};
    return qar_ros_ui::introspect_action(type_name, out.action).ok();
  }
  out.entity = RosEntityRef{RosEntityKind::Topic, "/probe", type_name};
  return qar_ros_ui::introspect_message(type_name, out.message).ok();
}

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

const Widget * find_bound(const Widget & root, const std::string & binding)
{
  for (const Widget * widget : flatten(root)) {
    if (widget->binding == binding) {
      return widget;
    }
  }
  return nullptr;
}

/// Only the five `ros.*` action ids plus the host-only `ar.open_video`, each
/// applying to this entity kind, and nothing editable outside `draft` /
/// `request` / `goal`.
void expect_contract(const PanelGenerationResult & result, const PanelGenerationRequest & request)
{
  ASSERT_TRUE(result.ok) << result.error;
  for (const Widget * widget : flatten(result.model.root)) {
    if (!widget->action.empty()) {
      const bool known = widget->action == ar_actions::kSetField ||
        widget->action == ar_actions::kPublish || widget->action == ar_actions::kCall ||
        widget->action == ar_actions::kSendGoal || widget->action == ar_actions::kCancelGoal ||
        widget->action == ar_actions::kOpenVideo;
      EXPECT_TRUE(known) << widget->id << " invented '" << widget->action << "'";
    }
    if (widget->editable) {
      ASSERT_FALSE(widget->binding.empty()) << widget->id;
      const std::string section = widget->binding.substr(0, widget->binding.find('.'));
      EXPECT_TRUE(qar_rviz_streaming::is_writable_document_section(section))
        << widget->id << " edits the read-only section '" << section << "'";
    }
  }
  (void)request;
}

/// Types that are in the distro but have no field a semantic layout can use, so
/// the generic form is the RIGHT answer for them.
bool expected_to_fall_back(const std::string & type_name)
{
  return type_name == "std_srvs/srv/Empty";
}

}  // namespace

TEST(ArSemanticRosTest, EveryClaimedTypeInThisDistroGeneratesAValidPanel)
{
  const PanelTemplateRegistry registry = semantic_registry();
  std::size_t checked = 0;
  std::size_t skipped = 0;
  for (const auto & info : semantic_template_catalog()) {
    for (const std::string & type : info.types) {
      PanelGenerationRequest request;
      if (!introspect(type, false, request)) {
        ++skipped;
        continue;
      }
      ++checked;
      const PanelGenerationResult result = registry.generate(request);
      ASSERT_NO_FATAL_FAILURE(expect_contract(result, request)) << type;
      EXPECT_EQ(result.template_name, info.name) << type << " lost its own template";
      for (const auto & problem : result.problems) {
        EXPECT_NE(problem.severity, qar_ui_model::ProblemSeverity::Error)
          << type << ": " << problem.widget_id << " " << problem.message;
      }
    }
  }
  // If this drops to nothing the distro moved and the whole file is vacuous.
  EXPECT_GE(checked, 30u) << "only " << checked << " claimed types were introspectable";
  std::cout << "[ INFO     ] introspected " << checked << " claimed types, skipped " << skipped
            << " not installed in this distro\n";
}

TEST(ArSemanticRosTest, EveryClaimedTopicTypeAlsoGeneratesAValidCommandForm)
{
  const PanelTemplateRegistry registry = semantic_registry();
  for (const auto & info : semantic_template_catalog()) {
    for (const std::string & type : info.types) {
      if (type.find("/msg/") == std::string::npos) {
        continue;
      }
      PanelGenerationRequest request;
      if (!introspect(type, true, request)) {
        continue;
      }
      const PanelGenerationResult result = registry.generate(request);
      ASSERT_NO_FATAL_FAILURE(expect_contract(result, request)) << type;
    }
  }
}

TEST(ArSemanticRosTest, TheProbesMatchTheRealIdlAndDoNotSilentlyFallBack)
{
  const PanelTemplateRegistry registry = semantic_registry();
  for (const auto & info : semantic_template_catalog()) {
    for (const std::string & type : info.types) {
      if (expected_to_fall_back(type)) {
        continue;
      }
      PanelGenerationRequest request;
      if (!introspect(type, false, request)) {
        continue;
      }
      const PanelGenerationResult result = registry.generate(request);
      ASSERT_TRUE(result.ok) << type << ": " << result.error;
      const qar_ui_model::UiValue * fallback = result.model.metadata.find("fallback");
      const bool fell_back = fallback != nullptr && fallback->as_bool(false);
      EXPECT_FALSE(fell_back)
        << type << " produced the generic form: the template's field names have drifted "
        << "from this distro's IDL";
    }
  }
}

TEST(ArSemanticRosTest, TheRealBatteryStateGetsItsGaugeAndItsDecodedEnums)
{
  PanelGenerationRequest request;
  if (!introspect("sensor_msgs/msg/BatteryState", false, request)) {
    GTEST_SKIP() << "sensor_msgs is not installed";
  }
  const PanelGenerationResult result = semantic_registry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  const Widget * gauge = find_bound(result.model.root, "msg.percentage");
  ASSERT_NE(gauge, nullptr) << "sensor_msgs/BatteryState no longer has `percentage`";
  EXPECT_EQ(gauge->type, WidgetType::Gauge);
  const Widget * status = find_bound(result.model.root, "msg.power_supply_status");
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->type, WidgetType::StatusLight);
  EXPECT_FALSE(status->rules.empty());
  const Widget * cells = find_bound(result.model.root, "msg.cell_voltage");
  ASSERT_NE(cells, nullptr);
  EXPECT_EQ(cells->type, WidgetType::Table);
}

TEST(ArSemanticRosTest, TheRealOdometryNestsPoseAndTwistWhereTheTemplateLooks)
{
  PanelGenerationRequest request;
  if (!introspect("nav_msgs/msg/Odometry", false, request)) {
    GTEST_SKIP() << "nav_msgs is not installed";
  }
  const PanelGenerationResult result = semantic_registry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_NE(find_bound(result.model.root, "msg.pose.pose.position"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.pose.pose.orientation"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.twist.twist.linear"), nullptr);
  EXPECT_NE(find_bound(result.model.root, "msg.twist.twist.angular"), nullptr);
}

TEST(ArSemanticRosTest, TheRealCmdVelFormStagesIntoDraftAndCanStopInOnePress)
{
  PanelGenerationRequest request;
  if (!introspect("geometry_msgs/msg/Twist", true, request)) {
    GTEST_SKIP() << "geometry_msgs is not installed";
  }
  const PanelGenerationResult result = semantic_registry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  const Widget * forward = find_bound(result.model.root, "draft.linear.x");
  ASSERT_NE(forward, nullptr);
  EXPECT_EQ(forward->type, WidgetType::Slider);
  EXPECT_EQ(forward->action, ar_actions::kSetField);

  const Widget * stop = result.model.find(
    qar_rviz_streaming::make_panel_id(RosEntityKind::Topic, "/probe") + ".stop");
  ASSERT_NE(stop, nullptr);
  EXPECT_EQ(stop->action, ar_actions::kPublish);
  EXPECT_FALSE(stop->confirm);
  // The preset is a complete, schema-valid Twist, so the router accepts it as
  // the message rather than refusing it field by field.
  const auto * draft = result.document_schema->find_field("draft");
  ASSERT_NE(draft, nullptr);
  ASSERT_TRUE(draft->message);
  const auto problems = qar_ui_model::validate_value(*draft->message, stop->payload);
  EXPECT_TRUE(problems.empty())
    << (problems.empty() ? "" : problems.front().path + ": " + problems.front().message);
}

TEST(ArSemanticRosTest, TheRealTfMessageColumnsResolveInsideTransformStamped)
{
  PanelGenerationRequest request;
  if (!introspect("tf2_msgs/msg/TFMessage", false, request)) {
    GTEST_SKIP() << "tf2_msgs is not installed";
  }
  const PanelGenerationResult result = semantic_registry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  const Widget * table = find_bound(result.model.root, "msg.transforms");
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->type, WidgetType::Table);
  // A column that did not resolve is dropped at build time, so five surviving
  // columns means all five paths exist in the real TransformStamped.
  EXPECT_EQ(table->columns.size(), 5u);
}

TEST(ArSemanticRosTest, NoRealSensorPanelBindsItsBulkPayload)
{
  const PanelTemplateRegistry registry = semantic_registry();
  const std::vector<std::pair<std::string, std::string>> cases = {
    {"sensor_msgs/msg/Image", "msg.data"},
    {"sensor_msgs/msg/CompressedImage", "msg.data"},
    {"sensor_msgs/msg/PointCloud2", "msg.data"},
    {"sensor_msgs/msg/LaserScan", "msg.ranges"},
    {"nav_msgs/msg/OccupancyGrid", "msg.data"},
    {"sensor_msgs/msg/Imu", "msg.orientation_covariance"},
    {"sensor_msgs/msg/NavSatFix", "msg.position_covariance"},
  };
  for (const auto & test_case : cases) {
    PanelGenerationRequest request;
    if (!introspect(test_case.first, false, request)) {
      continue;
    }
    const PanelGenerationResult result = registry.generate(request);
    ASSERT_TRUE(result.ok) << test_case.first << ": " << result.error;
    EXPECT_FALSE(binds_anything_under(result.model.root, test_case.second))
      << test_case.first << " must never stream " << test_case.second;
  }
}

// NOTE on what is NOT tested here: a live in-process RosImageFrameSource. This
// executable is deliberately Qt-free (rclcpp + qar_ui_model + qar_ros_ui only,
// per its CMakeLists.txt comment), and RosImageFrameSource needs
// qar_qt_streaming (QImage decode, LatestVideoFrame) — pulling that in here
// would mean linking Qt5::Gui, staging its DLLs next to this exe (as
// qar_qt_streaming_tests does) and combining that with this suite's existing
// "prepend the ROS distro's bin to PATH" dance. Its subscription/worker-thread
// path is exercised in its own executable
// (ros_image_frame_source_timing_tests.cpp), the two ingredients
// (video_frame.hpp's decoders, LatestVideoFrame's mailbox) by
// qar_qt_streaming_tests, and choose_image_transport by the stdlib suite
// (image_topic_transport_tests.cpp).
TEST(ArSemanticRosTest, TheRealImageAndCompressedImageTemplatesOfferAnOpenVideoButton)
{
  const PanelTemplateRegistry registry = semantic_registry();
  for (const char * type_name :
    {"sensor_msgs/msg/Image", "sensor_msgs/msg/CompressedImage"})
  {
    PanelGenerationRequest request;
    if (!introspect(type_name, false, request)) {
      continue;  // sensor_msgs not installed; nothing to check against
    }
    const PanelGenerationResult result = registry.generate(request);
    ASSERT_TRUE(result.ok) << type_name << ": " << result.error;
    const Widget * button = nullptr;
    for (const Widget * widget : flatten(result.model.root)) {
      if (widget->action == ar_actions::kOpenVideo) {
        button = widget;
        break;
      }
    }
    ASSERT_NE(button, nullptr) << type_name << ": no 'Open video in AR' button against the "
      "real distro IDL";
    EXPECT_TRUE(button->binding.empty()) << type_name;
  }
}

TEST(ArSemanticRosTest, TheRealTriggerAndSetBoolReadAsOneSwitchAndOneOutcome)
{
  const PanelTemplateRegistry registry = semantic_registry();
  PanelGenerationRequest trigger;
  if (!introspect("std_srvs/srv/Trigger", false, trigger)) {
    GTEST_SKIP() << "std_srvs is not installed";
  }
  const PanelGenerationResult trigger_result = registry.generate(trigger);
  ASSERT_TRUE(trigger_result.ok) << trigger_result.error;
  const Widget * success = find_bound(trigger_result.model.root, "response.success");
  ASSERT_NE(success, nullptr);
  EXPECT_EQ(success->type, WidgetType::StatusLight);

  PanelGenerationRequest set_bool;
  ASSERT_TRUE(introspect("std_srvs/srv/SetBool", false, set_bool));
  const PanelGenerationResult set_bool_result = registry.generate(set_bool);
  ASSERT_TRUE(set_bool_result.ok) << set_bool_result.error;
  const Widget * data = find_bound(set_bool_result.model.root, "request.data");
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->type, WidgetType::Toggle);
  EXPECT_TRUE(data->editable);
}

TEST(ArSemanticRosTest, AnUnclaimedRealTypeStillGetsTheGenericForm)
{
  PanelGenerationRequest request;
  if (!introspect("visualization_msgs/msg/Marker", false, request)) {
    GTEST_SKIP() << "visualization_msgs is not installed";
  }
  const PanelGenerationResult result = semantic_registry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.template_name, "generic-form");
}

TEST(ArSemanticRosTest, EveryGeneratedPanelActuallyRendersAsAnArPage)
{
  // generate() proves the model validates; this proves the AR renderer accepts
  // it and produces a page. A layout that passes validation but trips a
  // renderer rule would otherwise only show up in a headset.
  const PanelTemplateRegistry registry = semantic_registry();
  for (const auto & info : semantic_template_catalog()) {
    for (const std::string & type : info.types) {
      for (const bool writable : {false, true}) {
        PanelGenerationRequest request;
        if (!introspect(type, writable, request)) {
          continue;
        }
        const PanelGenerationResult result = registry.generate(request);
        ASSERT_TRUE(result.ok) << type << ": " << result.error;
        std::string html;
        std::vector<qar_ui_model::UiProblem> problems;
        ASSERT_TRUE(
          qar_ui_model::render_html_page(result.model, {}, html, problems))
          << type << (writable ? " (writable)" : "")
          << (problems.empty() ? std::string() : ": " + problems.front().message);
        EXPECT_GT(html.size(), 1000u) << type;
        EXPECT_NE(html.find(result.model.root.id), std::string::npos) << type;
      }
    }
  }
}
