// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The schema -> UiModel seam, which is also where the semantic templates
// register. Pure C++17 + qar_ui_model: no ROS graph, no Qt, no RViz.

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "qar_rviz_streaming/ar_panel_generator.hpp"

using qar_rviz_streaming::ar_actions::kCall;
using qar_rviz_streaming::ar_actions::kCancelGoal;
using qar_rviz_streaming::ar_actions::kPublish;
using qar_rviz_streaming::ar_actions::kSendGoal;
using qar_rviz_streaming::ar_actions::kSetField;
using qar_rviz_streaming::GenericFormTemplate;
using qar_rviz_streaming::IPanelTemplate;
using qar_rviz_streaming::make_document_schema;
using qar_rviz_streaming::make_panel_id;
using qar_rviz_streaming::PanelGenerationRequest;
using qar_rviz_streaming::PanelGenerationResult;
using qar_rviz_streaming::PanelTemplateRegistry;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::RosEntityRef;
using qar_ui_model::FieldDescription;
using qar_ui_model::FieldKind;
using qar_ui_model::MessageDescriptionBuilder;
using qar_ui_model::MessageDescriptionPtr;
using qar_ui_model::UiModel;
using qar_ui_model::Widget;
using qar_ui_model::WidgetType;

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

MessageDescriptionPtr
battery_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/BatteryState")
         .field(FieldDescription::primitive("voltage", FieldKind::Float32).with_unit("V"))
         .field(FieldDescription::primitive("present", FieldKind::Bool))
         .field(FieldDescription::primitive("location", FieldKind::String).string_bound(64))
         .field(FieldDescription::primitive("cell_voltage", FieldKind::Float32).unbounded_array())
         .build();
}

PanelGenerationRequest
topic_request(bool writable)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Topic, "/turtle1/cmd_vel",
    "geometry_msgs/msg/Twist"};
  request.message = twist_type();
  request.writable = writable;
  return request;
}

PanelGenerationRequest
service_request()
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Service, "/spawn", "turtlesim/srv/Spawn"};
  request.service.type_name = "turtlesim/srv/Spawn";
  request.service.request = MessageDescriptionBuilder("turtlesim/srv/Spawn_Request")
    .field(FieldDescription::primitive("x", FieldKind::Float32))
    .field(FieldDescription::primitive("name", FieldKind::String))
    .build();
  request.service.response = MessageDescriptionBuilder("turtlesim/srv/Spawn_Response")
    .field(FieldDescription::primitive("name", FieldKind::String))
    .build();
  return request;
}

PanelGenerationRequest
action_request()
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Action, "/fibonacci",
    "example_interfaces/action/Fibonacci"};
  request.action.type_name = "example_interfaces/action/Fibonacci";
  request.action.goal = MessageDescriptionBuilder("example_interfaces/action/Fibonacci_Goal")
    .field(FieldDescription::primitive("order", FieldKind::Int32))
    .build();
  request.action.feedback =
    MessageDescriptionBuilder("example_interfaces/action/Fibonacci_Feedback")
    .field(FieldDescription::primitive("partial_sequence", FieldKind::Int32).unbounded_array())
    .build();
  request.action.result = MessageDescriptionBuilder("example_interfaces/action/Fibonacci_Result")
    .field(FieldDescription::primitive("sequence", FieldKind::Int32).unbounded_array())
    .build();
  return request;
}

const Widget *
find_by_action(const Widget & root, const std::string & action)
{
  const Widget * hit = nullptr;
  qar_ui_model::walk_widgets(
    root, [&](const Widget & w, const Widget *, std::size_t) {
      if (w.action == action) {
        hit = &w;
        return qar_ui_model::WalkControl::Stop;
      }
      return qar_ui_model::WalkControl::Continue;
    });
  return hit;
}

std::size_t
count_widgets(const Widget & root)
{
  std::size_t count = 0;
  qar_ui_model::walk_widgets(
    root, [&](const Widget &, const Widget *, std::size_t) {
      ++count;
      return qar_ui_model::WalkControl::Continue;
    });
  return count;
}

}  // namespace

// --- Panel ids --------------------------------------------------------------

TEST(ArPanelIdTest, RosNameBecomesAValidWidgetId)
{
  const std::string id = make_panel_id(RosEntityKind::Topic, "/turtle1/cmd_vel");
  EXPECT_EQ(id, "topic._turtle1_cmd_vel");
  EXPECT_TRUE(qar_ui_model::is_valid_widget_id(id));
}

TEST(ArPanelIdTest, KindKeepsSameNamedEntitiesApart)
{
  EXPECT_NE(
    make_panel_id(RosEntityKind::Topic, "/thing"),
    make_panel_id(RosEntityKind::Service, "/thing"));
}

TEST(ArPanelIdTest, HostileCharactersAreReplacedAndTheIdIsBounded)
{
  const std::string id = make_panel_id(RosEntityKind::Topic, "/a b<script>/\"x\"");
  EXPECT_TRUE(qar_ui_model::is_valid_widget_id(id)) << id;
  EXPECT_EQ(id.find('<'), std::string::npos);

  const std::string long_id = make_panel_id(RosEntityKind::Action, std::string(400, 'a'));
  EXPECT_TRUE(qar_ui_model::is_valid_widget_id(long_id));
  EXPECT_LE(long_id.size(), 128u);
}

// --- The document schema ----------------------------------------------------

TEST(ArDocumentSchemaTest, ReadOnlyTopicHasOnlyTheMessageSection)
{
  const auto schema = make_document_schema(topic_request(false));
  ASSERT_TRUE(schema);
  EXPECT_NE(schema->find_field(qar_rviz_streaming::ar_doc::kMessage), nullptr);
  EXPECT_EQ(schema->find_field(qar_rviz_streaming::ar_doc::kDraft), nullptr);
}

TEST(ArDocumentSchemaTest, WritableTopicAddsTheDraftSection)
{
  const auto schema = make_document_schema(topic_request(true));
  ASSERT_TRUE(schema);
  const auto * draft = schema->find_field(qar_rviz_streaming::ar_doc::kDraft);
  ASSERT_NE(draft, nullptr);
  ASSERT_TRUE(draft->message);
  EXPECT_EQ(draft->message->type_name, "geometry_msgs/msg/Twist");
}

TEST(ArDocumentSchemaTest, ActionCarriesGoalFeedbackResultAndAStatusString)
{
  const auto schema = make_document_schema(action_request());
  ASSERT_TRUE(schema);
  EXPECT_NE(schema->find_field(qar_rviz_streaming::ar_doc::kGoal), nullptr);
  EXPECT_NE(schema->find_field(qar_rviz_streaming::ar_doc::kFeedback), nullptr);
  EXPECT_NE(schema->find_field(qar_rviz_streaming::ar_doc::kResult), nullptr);
  const auto * status = schema->find_field(qar_rviz_streaming::ar_doc::kStatus);
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->kind, FieldKind::String);
}

TEST(ArDocumentSchemaTest, OnlyTheStagedSectionsAreWritable)
{
  EXPECT_TRUE(qar_rviz_streaming::is_writable_document_section("draft"));
  EXPECT_TRUE(qar_rviz_streaming::is_writable_document_section("request"));
  EXPECT_TRUE(qar_rviz_streaming::is_writable_document_section("goal"));
  EXPECT_FALSE(qar_rviz_streaming::is_writable_document_section("msg"));
  EXPECT_FALSE(qar_rviz_streaming::is_writable_document_section("response"));
  EXPECT_FALSE(qar_rviz_streaming::is_writable_document_section("feedback"));
  EXPECT_FALSE(qar_rviz_streaming::is_writable_document_section("result"));
  EXPECT_FALSE(qar_rviz_streaming::is_writable_document_section("status"));
}

// --- Generation -------------------------------------------------------------

TEST(ArPanelGeneratorTest, ReadOnlyTopicPanelValidatesAgainstItsOwnSchema)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(topic_request(false));
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_EQ(result.template_name, "generic-form");
  EXPECT_EQ(result.model.root.id, "topic._turtle1_cmd_vel");
  EXPECT_EQ(result.model.root.type, WidgetType::Panel);
  EXPECT_EQ(result.model.root.description, "geometry_msgs/msg/Twist");
  // Read-only means no way to command the robot from the headset.
  EXPECT_EQ(find_by_action(result.model.root, kPublish), nullptr);
  EXPECT_EQ(find_by_action(result.model.root, kSetField), nullptr);
}

TEST(ArPanelGeneratorTest, NestedNumericTripletBecomesAVector3)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(topic_request(false));
  ASSERT_TRUE(result.ok) << result.error;
  const Widget * linear = result.model.find("topic._turtle1_cmd_vel.msg.linear");
  ASSERT_NE(linear, nullptr);
  EXPECT_EQ(linear->type, WidgetType::Vector3);
  EXPECT_EQ(linear->binding, "msg.linear");
  EXPECT_FALSE(linear->editable);
}

TEST(ArPanelGeneratorTest, WritableTopicGetsEditableDraftWidgetsAndAConfirmingPublish)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(topic_request(true));
  ASSERT_TRUE(result.ok) << result.error;

  const Widget * draft = result.model.find("topic._turtle1_cmd_vel.draft.linear");
  ASSERT_NE(draft, nullptr);
  EXPECT_TRUE(draft->editable);
  EXPECT_EQ(draft->action, kSetField);

  const Widget * publish = find_by_action(result.model.root, kPublish);
  ASSERT_NE(publish, nullptr);
  EXPECT_EQ(publish->type, WidgetType::Button);
  EXPECT_TRUE(publish->confirm) << "publishing commands the robot; a ray must not fire it once";

  // The live section stays read-only even when the panel can publish.
  const Widget * live = result.model.find("topic._turtle1_cmd_vel.msg.linear");
  ASSERT_NE(live, nullptr);
  EXPECT_FALSE(live->editable);
}

TEST(ArPanelGeneratorTest, PrimitiveKindsChooseTheMatchingInput)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Topic, "/battery",
    "sensor_msgs/msg/BatteryState"};
  request.message = battery_type();
  request.writable = true;

  const PanelGenerationResult result = PanelTemplateRegistry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;

  const Widget * voltage = result.model.find("topic._battery.draft.voltage");
  ASSERT_NE(voltage, nullptr);
  EXPECT_EQ(voltage->type, WidgetType::NumberInput);
  EXPECT_FALSE(voltage->integer);
  EXPECT_EQ(voltage->unit, "V");

  const Widget * present = result.model.find("topic._battery.draft.present");
  ASSERT_NE(present, nullptr);
  EXPECT_EQ(present->type, WidgetType::Toggle);

  const Widget * location = result.model.find("topic._battery.draft.location");
  ASSERT_NE(location, nullptr);
  EXPECT_EQ(location->type, WidgetType::TextInput);
  ASSERT_TRUE(location->max_length.has_value());
  EXPECT_EQ(*location->max_length, 64u);

  // An array is shown whole and never edited: which array is a chart and which
  // is a table is the semantic judgement the semantic templates own.
  const Widget * cells = result.model.find("topic._battery.draft.cell_voltage");
  ASSERT_NE(cells, nullptr);
  EXPECT_EQ(cells->type, WidgetType::JsonInspector);
  EXPECT_FALSE(cells->editable);
}

TEST(ArPanelGeneratorTest, ReadOnlySectionUsesValueDisplays)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(service_request());
  ASSERT_TRUE(result.ok) << result.error;
  const Widget * response_name = result.model.find("service._spawn.response.name");
  ASSERT_NE(response_name, nullptr);
  EXPECT_EQ(response_name->type, WidgetType::ValueDisplay);
  EXPECT_FALSE(response_name->editable);
}

TEST(ArPanelGeneratorTest, ServicePanelHasARequestFormAndACallButton)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(service_request());
  ASSERT_TRUE(result.ok) << result.error;

  const Widget * x = result.model.find("service._spawn.request.x");
  ASSERT_NE(x, nullptr);
  EXPECT_TRUE(x->editable);
  EXPECT_EQ(x->binding, "request.x");

  const Widget * call = find_by_action(result.model.root, kCall);
  ASSERT_NE(call, nullptr);
  EXPECT_EQ(call->type, WidgetType::Button);
  EXPECT_EQ(find_by_action(result.model.root, kPublish), nullptr);
}

TEST(ArPanelGeneratorTest, ActionPanelSendsCancelsAndShowsStatus)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(action_request());
  ASSERT_TRUE(result.ok) << result.error;

  const Widget * order = result.model.find("action._fibonacci.goal.order");
  ASSERT_NE(order, nullptr);
  EXPECT_TRUE(order->integer);
  EXPECT_TRUE(order->editable);

  const Widget * send = find_by_action(result.model.root, kSendGoal);
  ASSERT_NE(send, nullptr);
  EXPECT_TRUE(send->confirm);

  const Widget * cancel = find_by_action(result.model.root, kCancelGoal);
  ASSERT_NE(cancel, nullptr);
  EXPECT_FALSE(cancel->confirm) << "cancel is the safe direction; it must fire on one press";

  const Widget * status = result.model.find("action._fibonacci.status");
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->binding, "status");
}

TEST(ArPanelGeneratorTest, InitialDataIsTheSchemaDefault)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(topic_request(true));
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.model.initial_data.is_object());
  const qar_ui_model::UiValue * draft = result.model.initial_data.find("draft");
  ASSERT_NE(draft, nullptr);
  const qar_ui_model::UiValue * linear = draft->find("linear");
  ASSERT_NE(linear, nullptr);
  const qar_ui_model::UiValue * x = linear->find("x");
  ASSERT_NE(x, nullptr);
  EXPECT_TRUE(x->is_number());
}

TEST(ArPanelGeneratorTest, MetadataNamesTheEntityAndTheTemplate)
{
  const PanelGenerationResult result = PanelTemplateRegistry().generate(service_request());
  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_TRUE(result.model.metadata.is_object());
  const auto * kind = result.model.metadata.find("entity_kind");
  ASSERT_NE(kind, nullptr);
  EXPECT_EQ(kind->as_string(), "service");
  const auto * tmpl = result.model.metadata.find("template");
  ASSERT_NE(tmpl, nullptr);
  EXPECT_EQ(tmpl->as_string(), "generic-form");
}

TEST(ArPanelGeneratorTest, AFatMessageIsCappedAndStillReachableAsRawJson)
{
  MessageDescriptionBuilder builder("test/msg/Fat");
  for (int i = 0; i < 40; ++i) {
    builder.field(
      FieldDescription::primitive("f" + std::to_string(i), FieldKind::Float64));
  }
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Topic, "/fat", "test/msg/Fat"};
  request.message = builder.build();
  request.max_fields = 5;

  const PanelGenerationResult result = PanelTemplateRegistry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_LT(count_widgets(result.model.root), 12u);
  const Widget * raw = result.model.find("topic._fat.msg.raw");
  ASSERT_NE(raw, nullptr) << "nothing may be silently dropped";
  EXPECT_EQ(raw->type, WidgetType::JsonInspector);
  EXPECT_EQ(raw->binding, "msg");
}

TEST(ArPanelGeneratorTest, DeepNestingFallsBackToAnInspectorRatherThanRecursingForever)
{
  MessageDescriptionPtr leaf = MessageDescriptionBuilder("test/msg/Leaf")
    .field(FieldDescription::primitive("value", FieldKind::Int32))
    .build();
  MessageDescriptionPtr level = leaf;
  for (int i = 0; i < 6; ++i) {
    level = MessageDescriptionBuilder("test/msg/Level" + std::to_string(i))
      .field(FieldDescription::nested("inner", level))
      .build();
  }
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Topic, "/deep", "test/msg/Level5"};
  request.message = level;
  request.max_depth = 2;

  const PanelGenerationResult result = PanelTemplateRegistry().generate(request);
  ASSERT_TRUE(result.ok) << result.error;
  const Widget * inner = result.model.find("topic._deep.msg.inner.inner");
  ASSERT_NE(inner, nullptr);
  EXPECT_EQ(inner->type, WidgetType::JsonInspector);
}

// --- Refusals ---------------------------------------------------------------

TEST(ArPanelGeneratorTest, MissingDescriptionIsRefusedWithAReason)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Topic, "/nope", "pkg/msg/Nope"};
  const PanelGenerationResult result = PanelTemplateRegistry().generate(request);
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("/nope"), std::string::npos);
}

TEST(ArPanelGeneratorTest, UnnamedEntityIsRefused)
{
  PanelGenerationRequest request;
  request.message = twist_type();
  const PanelGenerationResult result = PanelTemplateRegistry().generate(request);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.error.empty());
}

TEST(ArPanelGeneratorTest, AnEmptyMessageProducesNoPanel)
{
  PanelGenerationRequest request;
  request.entity = RosEntityRef{RosEntityKind::Topic, "/empty", "std_msgs/msg/Empty"};
  request.message = MessageDescriptionBuilder("std_msgs/msg/Empty").build();
  const PanelGenerationResult result = PanelTemplateRegistry().generate(request);
  EXPECT_FALSE(result.ok);
}

// --- The semantic-template seam ---------------------------------------------

namespace
{

/// Exactly what a semantic template looks like: it claims a type and lays it
/// out its own way, reusing the action ids and the document sections.
class TwistJoystickTemplate : public IPanelTemplate
{
public:
  std::string name() const override { return "twist-joystick"; }

  int match(const PanelGenerationRequest & request) const override
  {
    return request.entity.type_name == "geometry_msgs/msg/Twist" ? 100 : 0;
  }

  bool build(
    const PanelGenerationRequest & request,
    const qar_ui_model::MessageDescription &,
    UiModel & out_model,
    std::string &) const override
  {
    const std::string id = make_panel_id(request.entity.kind, request.entity.name);
    out_model = UiModel{};
    out_model.root = qar_ui_model::widgets::panel(id, request.entity.name);
    out_model.root.add(qar_ui_model::widgets::vector3(id + ".v", "msg.linear", "Velocity"));
    return true;
  }
};

}  // namespace

TEST(ArPanelTemplateRegistryTest, ASpecificTemplateOutscoresTheGenericFallback)
{
  PanelTemplateRegistry registry;
  registry.add(std::make_shared<TwistJoystickTemplate>());

  const PanelGenerationResult twist = registry.generate(topic_request(false));
  ASSERT_TRUE(twist.ok) << twist.error;
  EXPECT_EQ(twist.template_name, "twist-joystick");

  // Anything it does not claim still gets the generic form.
  const PanelGenerationResult service = registry.generate(service_request());
  ASSERT_TRUE(service.ok) << service.error;
  EXPECT_EQ(service.template_name, "generic-form");
}

TEST(ArPanelTemplateRegistryTest, TheFallbackIsAlwaysAvailable)
{
  const PanelTemplateRegistry registry;
  const auto names = registry.template_names();
  ASSERT_EQ(names.size(), 1u);
  EXPECT_EQ(names.front(), "generic-form");
  EXPECT_NE(registry.select(topic_request(false)), nullptr);
}

TEST(ArPanelTemplateRegistryTest, ATemplateThatProducesAnInvalidModelIsCaughtHere)
{
  class BrokenTemplate : public IPanelTemplate
  {
public:
    std::string name() const override { return "broken"; }
    int match(const PanelGenerationRequest &) const override { return 50; }
    bool build(
      const PanelGenerationRequest &, const qar_ui_model::MessageDescription &,
      UiModel & out_model, std::string &) const override
    {
      out_model = UiModel{};
      out_model.root = qar_ui_model::widgets::panel("broken", "Broken");
      // Binds a field the document schema does not have.
      out_model.root.add(qar_ui_model::widgets::value("broken.x", "msg.nope", "Nope"));
      return true;
    }
  };

  PanelTemplateRegistry registry;
  registry.add(std::make_shared<BrokenTemplate>());
  const PanelGenerationResult result = registry.generate(topic_request(false));
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("broken.x"), std::string::npos) << result.error;
}
