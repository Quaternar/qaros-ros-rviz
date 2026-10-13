// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Control event -> ROS action. This is the half of the ROS graph -> AR panel
// path that carries authority — a frame from a browser in a headset decides
// whether the robot moves — so every refusal is tested as carefully as every
// success. Pure C++17: no session, no ROS graph, no Qt.

#include <gtest/gtest.h>

#include <string>

#include "qar_rviz_streaming/ar_control_router.hpp"
#include "qar_rviz_streaming/ar_panel_generator.hpp"

using qar_rviz_streaming::ar_actions::kCall;
using qar_rviz_streaming::ar_actions::kCancelGoal;
using qar_rviz_streaming::ar_actions::kOpenVideo;
using qar_rviz_streaming::ar_actions::kPublish;
using qar_rviz_streaming::ar_actions::kSendGoal;
using qar_rviz_streaming::ar_actions::kSetField;
using qar_rviz_streaming::ArControlRouter;
using qar_rviz_streaming::ControlRouting;
using qar_rviz_streaming::PanelGenerationRequest;
using qar_rviz_streaming::PanelGenerationResult;
using qar_rviz_streaming::PanelTemplateRegistry;
using qar_rviz_streaming::RosActionKind;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::RosEntityRef;
using qar_ui_model::ControlBody;
using qar_ui_model::FieldDescription;
using qar_ui_model::FieldKind;
using qar_ui_model::MessageDescriptionBuilder;
using qar_ui_model::MessageDescriptionPtr;
using qar_ui_model::UiValue;

namespace
{

MessageDescriptionPtr
vector3_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Vector3")
         .field(FieldDescription::primitive("x", FieldKind::Float64))
         .field(FieldDescription::primitive("y", FieldKind::Float64))
         .field(FieldDescription::primitive("z", FieldKind::Float64))
         .build();
}

MessageDescriptionPtr
twist_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Twist")
         .field(FieldDescription::nested("linear", vector3_type()))
         .field(FieldDescription::nested("angular", vector3_type()))
         .build();
}

/// A message with an integer, a bounded string and a bool, so range/kind/length
/// mismatches all have somewhere to happen.
MessageDescriptionPtr
image_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/Image")
         .field(FieldDescription::primitive("width", FieldKind::UInt32))
         .build();
}

MessageDescriptionPtr
mixed_type()
{
  return MessageDescriptionBuilder("test/msg/Mixed")
         .field(FieldDescription::primitive("count", FieldKind::Int8))
         .field(FieldDescription::primitive("name", FieldKind::String).string_bound(4))
         .field(FieldDescription::primitive("enabled", FieldKind::Bool))
         .build();
}

PanelGenerationResult
generate(RosEntityKind kind, const char * name, const char * type, bool writable,
  MessageDescriptionPtr message = nullptr)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{kind, name, type};
  request.writable = writable;
  switch (kind) {
    case RosEntityKind::Topic:
      request.message = message ? message : twist_type();
      break;
    case RosEntityKind::Service:
      request.service.type_name = type;
      request.service.request = message ? message : mixed_type();
      request.service.response = MessageDescriptionBuilder("test/srv/S_Response")
        .field(FieldDescription::primitive("ok", FieldKind::Bool))
        .build();
      break;
    case RosEntityKind::Action:
      request.action.type_name = type;
      request.action.goal = message ? message : mixed_type();
      request.action.result = MessageDescriptionBuilder("test/action/A_Result")
        .field(FieldDescription::primitive("total", FieldKind::Int32))
        .build();
      break;
  }
  return PanelTemplateRegistry().generate(request);
}

/// A router over a freshly generated panel for @p kind.
ArControlRouter
router_for(RosEntityKind kind, const char * name, const char * type, bool writable,
  MessageDescriptionPtr message = nullptr)
{
  PanelGenerationResult result = generate(kind, name, type, writable, std::move(message));
  EXPECT_TRUE(result.ok) << result.error;
  return ArControlRouter(
    RosEntityRef{kind, name, type}, result.model, result.document_schema);
}

ControlBody
control(const std::string & widget, const std::string & action, const std::string & path,
  UiValue value = {})
{
  ControlBody body;
  body.control = widget;
  body.action = action;
  body.path = path;
  body.value = std::move(value);
  return body;
}

}  // namespace

// --- Staging a field --------------------------------------------------------

TEST(ArControlRouterTest, SetFieldStagesTheValueAndReportsThePath)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);

  const ControlRouting routing = router.route(
    control("topic._cmd_vel.draft.linear", kSetField, "draft.linear.x", UiValue(0.4)));
  ASSERT_TRUE(routing.ok) << routing.error;
  EXPECT_EQ(routing.intent.kind, RosActionKind::SetField);
  EXPECT_EQ(routing.intent.document_path, "draft.linear.x");

  const UiValue * draft = router.document().find("draft");
  ASSERT_NE(draft, nullptr);
  const UiValue * linear = draft->find("linear");
  ASSERT_NE(linear, nullptr);
  const UiValue * x = linear->find("x");
  ASSERT_NE(x, nullptr);
  EXPECT_DOUBLE_EQ(x->as_double(), 0.4);
}

TEST(ArControlRouterTest, AnEmptyFramePathFallsBackToTheWidgetsBinding)
{
  ArControlRouter router = router_for(RosEntityKind::Service, "/svc", "test/srv/S", true,
      mixed_type());
  const ControlRouting routing =
    router.route(control("service._svc.request.count", kSetField, "", UiValue(7)));
  ASSERT_TRUE(routing.ok) << routing.error;
  EXPECT_EQ(routing.intent.document_path, "request.count");
}

TEST(ArControlRouterTest, StagedValuesAccumulateIntoTheOutboundMessage)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  ASSERT_TRUE(
    router.route(control("topic._cmd_vel.draft.linear", kSetField, "draft.linear.x",
    UiValue(1.5))).ok);
  ASSERT_TRUE(
    router.route(control("topic._cmd_vel.draft.angular", kSetField, "draft.angular.z",
    UiValue(-0.25))).ok);

  const ControlRouting publish =
    router.route(control("topic._cmd_vel.publish", kPublish, ""));
  ASSERT_TRUE(publish.ok) << publish.error;
  EXPECT_EQ(publish.intent.kind, RosActionKind::PublishTopic);
  ASSERT_TRUE(publish.intent.payload.is_object());
  EXPECT_DOUBLE_EQ(publish.intent.payload.find("linear")->find("x")->as_double(), 1.5);
  EXPECT_DOUBLE_EQ(publish.intent.payload.find("angular")->find("z")->as_double(), -0.25);
}

// --- Type mismatches --------------------------------------------------------

TEST(ArControlRouterTest, AStringWhereANumberBelongsIsRefusedAndStagesNothing)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  const UiValue before = router.document();

  const ControlRouting routing = router.route(
    control("topic._cmd_vel.draft.linear", kSetField, "draft.linear.x", UiValue("fast")));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("linear.x"), std::string::npos) << routing.error;
  EXPECT_EQ(router.document(), before) << "a refused edit must leave the draft untouched";
}

TEST(ArControlRouterTest, AnOutOfRangeIntegerIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Action, "/act", "test/action/A", true,
      mixed_type());
  const ControlRouting routing =
    router.route(control("action._act.goal.count", kSetField, "goal.count", UiValue(999)));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("count"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, AnOverLongBoundedStringIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Action, "/act", "test/action/A", true,
      mixed_type());
  const ControlRouting routing = router.route(
    control("action._act.goal.name", kSetField, "goal.name", UiValue("far too long")));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("name"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, AWholeMessageValueIsAcceptedWhenItMatchesTheSchema)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  UiValue vector = UiValue::object();
  vector.set("x", UiValue(1.0));
  vector.set("y", UiValue(2.0));
  vector.set("z", UiValue(3.0));

  const ControlRouting routing = router.route(
    control("topic._cmd_vel.draft.linear", kSetField, "draft.linear", vector));
  ASSERT_TRUE(routing.ok) << routing.error;
  EXPECT_DOUBLE_EQ(
    router.document().find("draft")->find("linear")->find("y")->as_double(), 2.0);
}

TEST(ArControlRouterTest, AnUnknownFieldInsideAMessageValueIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  UiValue vector = UiValue::object();
  vector.set("x", UiValue(1.0));
  vector.set("nope", UiValue(2.0));

  const ControlRouting routing = router.route(
    control("topic._cmd_vel.draft.linear", kSetField, "draft.linear", vector));
  EXPECT_FALSE(routing.ok);
}

// --- Read-only sections -----------------------------------------------------

TEST(ArControlRouterTest, WritingWhatRosSaidIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  const ControlRouting routing = router.route(
    control("topic._cmd_vel.draft.linear", kSetField, "msg.linear.x", UiValue(9.0)));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("not editable"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, WritingAServiceResponseIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Service, "/svc", "test/srv/S", true,
      mixed_type());
  const ControlRouting routing = router.route(
    control("service._svc.request.enabled", kSetField, "response.ok", UiValue(true)));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("not editable"), std::string::npos) << routing.error;
}

// --- Malformed and hostile frames -------------------------------------------

TEST(ArControlRouterTest, AnUnknownWidgetIdIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  const ControlRouting routing = router.route(control("no.such.widget", kPublish, ""));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("unknown control"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, AnEmptyWidgetIdIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  EXPECT_FALSE(router.route(control("", kPublish, "")).ok);
}

TEST(ArControlRouterTest, AnUnknownActionIdIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  // A widget with no declared action of its own, so the frame's id is used.
  const ControlRouting routing =
    router.route(control("topic._cmd_vel.msg.linear", "ros.launch_missiles", ""));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("unknown action"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, AFrameMayNotRelabelAWidgetsAction)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  // The staging widget declares ros.set; the frame claims it is a publish.
  const ControlRouting routing =
    router.route(control("topic._cmd_vel.draft.linear", kPublish, "draft.linear.x",
      UiValue(1.0)));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("declares action"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, AnActionThatDoesNotApplyToTheEntityIsRefused)
{
  ArControlRouter service = router_for(RosEntityKind::Service, "/svc", "test/srv/S", true,
      mixed_type());
  const ControlRouting routing =
    service.route(control("service._svc.request.count", kPublish, ""));
  EXPECT_FALSE(routing.ok);

  ArControlRouter topic = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  EXPECT_FALSE(topic.route(control("topic._cmd_vel.msg.linear", kSendGoal, "")).ok);
  EXPECT_FALSE(topic.route(control("topic._cmd_vel.msg.linear", kCall, "")).ok);
}

TEST(ArControlRouterTest, AControlWithNoActionAtAllIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", false);
  const ControlRouting routing = router.route(control("topic._cmd_vel.msg.linear", "", ""));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("no action"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, ABindingThatDoesNotResolveIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  const ControlRouting routing = router.route(
    control("topic._cmd_vel.draft.linear", kSetField, "draft.nowhere", UiValue(1.0)));
  EXPECT_FALSE(routing.ok);
  EXPECT_NE(routing.error.find("does not resolve"), std::string::npos) << routing.error;
}

TEST(ArControlRouterTest, AMalformedPathIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  const ControlRouting routing = router.route(
    control("topic._cmd_vel.draft.linear", kSetField, "draft..x[", UiValue(1.0)));
  EXPECT_FALSE(routing.ok);
}

// --- Services and actions ---------------------------------------------------

TEST(ArControlRouterTest, CallCarriesTheStagedRequest)
{
  ArControlRouter router = router_for(RosEntityKind::Service, "/svc", "test/srv/S", true,
      mixed_type());
  ASSERT_TRUE(
    router.route(control("service._svc.request.count", kSetField, "request.count",
    UiValue(3))).ok);

  const ControlRouting routing = router.route(control("service._svc.call", kCall, ""));
  ASSERT_TRUE(routing.ok) << routing.error;
  EXPECT_EQ(routing.intent.kind, RosActionKind::CallService);
  EXPECT_EQ(routing.intent.payload.find("count")->as_int(), 3);
}

TEST(ArControlRouterTest, SendGoalCarriesTheStagedGoalAndCancelCarriesNothing)
{
  ArControlRouter router = router_for(RosEntityKind::Action, "/act", "test/action/A", true,
      mixed_type());
  const ControlRouting send = router.route(control("action._act.send", kSendGoal, ""));
  ASSERT_TRUE(send.ok) << send.error;
  EXPECT_EQ(send.intent.kind, RosActionKind::SendGoal);
  EXPECT_TRUE(send.intent.payload.is_object());

  const ControlRouting cancel = router.route(control("action._act.cancel", kCancelGoal, ""));
  ASSERT_TRUE(cancel.ok) << cancel.error;
  EXPECT_EQ(cancel.intent.kind, RosActionKind::CancelGoal);
  EXPECT_TRUE(cancel.intent.payload.is_null());
}

// --- Button payloads --------------------------------------------------------

TEST(ArControlRouterTest, AButtonPayloadOverridesTheStagedMessage)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  ASSERT_TRUE(
    router.route(control("topic._cmd_vel.draft.linear", kSetField, "draft.linear.x",
    UiValue(2.0))).ok);

  UiValue stop = UiValue::object();
  UiValue zero = UiValue::object();
  zero.set("x", UiValue(0.0));
  stop.set("linear", zero);

  const ControlRouting routing =
    router.route(control("topic._cmd_vel.publish", kPublish, "", stop));
  ASSERT_TRUE(routing.ok) << routing.error;
  EXPECT_DOUBLE_EQ(routing.intent.payload.find("linear")->find("x")->as_double(), 0.0);
}

TEST(ArControlRouterTest, AButtonPayloadTheSchemaRejectsIsRefused)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", true);
  UiValue bad = UiValue::object();
  bad.set("linear", UiValue("not a vector"));

  const ControlRouting routing =
    router.route(control("topic._cmd_vel.publish", kPublish, "", bad));
  EXPECT_FALSE(routing.ok);
}

// --- Mirroring ROS back into the document -----------------------------------

TEST(ArControlRouterTest, InboundValuesCanBeMirroredIntoTheDocument)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", false);
  UiValue message = UiValue::object();
  UiValue linear = UiValue::object();
  linear.set("x", UiValue(5.0));
  message.set("linear", linear);

  std::string error;
  ASSERT_TRUE(router.set_document_value("msg", message, &error)) << error;
  EXPECT_DOUBLE_EQ(router.document().find("msg")->find("linear")->find("x")->as_double(), 5.0);
}

TEST(ArControlRouterTest, MirroringAMalformedPathFails)
{
  ArControlRouter router = router_for(RosEntityKind::Topic, "/cmd_vel",
      "geometry_msgs/msg/Twist", false);
  std::string error;
  EXPECT_FALSE(router.set_document_value("msg..[", UiValue(1), &error));
  EXPECT_FALSE(error.empty());
}

// --- The outbound key -------------------------------------------------------

TEST(ArControlRouterTest, EachSendingActionNamesItsStagedSection)
{
  EXPECT_STREQ(ArControlRouter::outbound_key(RosActionKind::PublishTopic), "draft");
  EXPECT_STREQ(ArControlRouter::outbound_key(RosActionKind::CallService), "request");
  EXPECT_STREQ(ArControlRouter::outbound_key(RosActionKind::SendGoal), "goal");
  EXPECT_EQ(ArControlRouter::outbound_key(RosActionKind::CancelGoal), nullptr);
  EXPECT_EQ(ArControlRouter::outbound_key(RosActionKind::SetField), nullptr);
  EXPECT_EQ(ArControlRouter::outbound_key(RosActionKind::OpenVideoPanel), nullptr);
}

// --- Host-only: open video panel ---------------------------------------------

TEST(ArControlRouterTest, OpenVideoAppliesToAnImageOrCompressedImageTopic)
{
  for (const char * type : {"sensor_msgs/msg/Image", "sensor_msgs/msg/CompressedImage"}) {
    ArControlRouter router =
      router_for(RosEntityKind::Topic, "/image_raw", type, false, image_type());
    const ControlRouting routing =
      router.route(control("topic._image_raw.msg.width", kOpenVideo, ""));
    ASSERT_TRUE(routing.ok) << type << ": " << routing.error;
    EXPECT_EQ(routing.intent.kind, RosActionKind::OpenVideoPanel);
    EXPECT_EQ(routing.intent.entity.type_name, type);
    EXPECT_TRUE(routing.intent.document_path.empty()) << "nothing is staged";
  }
}

TEST(ArControlRouterTest, OpenVideoRefusesAWrongTypeTopicAndNonTopicEntities)
{
  // Right kind (Topic), wrong type.
  ArControlRouter twist =
    router_for(RosEntityKind::Topic, "/cmd_vel", "geometry_msgs/msg/Twist", false);
  const ControlRouting wrong_type =
    twist.route(control("topic._cmd_vel.msg.linear", kOpenVideo, ""));
  EXPECT_FALSE(wrong_type.ok);

  // Right type name, wrong kind: response/result sections are read-only, so
  // their widgets carry no action of their own and defer to the event's.
  ArControlRouter service = router_for(RosEntityKind::Service, "/probe", "test/srv/S", false);
  EXPECT_FALSE(service.route(control("service._probe.response.ok", kOpenVideo, "")).ok);

  ArControlRouter action = router_for(RosEntityKind::Action, "/probe", "test/action/A", false);
  EXPECT_FALSE(action.route(control("action._probe.result.total", kOpenVideo, "")).ok);
}
