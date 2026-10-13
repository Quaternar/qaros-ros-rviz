// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "rosidl_typesupport_introspection_cpp/field_types.hpp"

#include "qar_ui_model/type_description.hpp"

#include "qar_ros_ui/dynamic_message.hpp"
#include "qar_ros_ui/introspection.hpp"

#include "test_support.hpp"

using qar_ros_ui::DynamicMessage;
using qar_ros_ui::IntrospectionOptions;
using qar_ros_ui::Result;
using qar_ros_ui::Status;
using qar_ui_model::ArrayKind;
using qar_ui_model::FieldDescription;
using qar_ui_model::FieldKind;
using qar_ui_model::MessageDescription;
using qar_ui_model::MessageDescriptionPtr;
using qar_ui_model::UiValue;
using qar_ros_ui_test::dump;

namespace ric = rosidl_typesupport_introspection_cpp;

namespace
{

MessageDescriptionPtr describe(const std::string & type, const IntrospectionOptions & options = {})
{
  MessageDescriptionPtr description;
  const Result result = qar_ros_ui::introspect_message(type, description, nullptr, options);
  if (!result.ok()) {
    ADD_FAILURE() << type << ": " << result.message;
    return nullptr;
  }
  return description;
}

const FieldDescription * field(const MessageDescription & root, const std::string & path)
{
  qar_ui_model::ResolvedField resolved;
  std::string error;
  if (!qar_ui_model::resolve_field_path(root, path, resolved, error)) {
    ADD_FAILURE() << root.type_name << "." << path << ": " << error;
    return nullptr;
  }
  return resolved.field;
}

void expect_valid(const MessageDescription & description)
{
  for (const auto & problem : qar_ui_model::validate_description(description)) {
    ADD_FAILURE() << description.type_name << ": " << problem.path << ": " << problem.message;
  }
}

}  // namespace

TEST(Introspection, EveryRosidlTypeIdHasASchemaKind)
{
  const std::vector<std::pair<std::uint8_t, FieldKind>> expected = {
    {ric::ROS_TYPE_FLOAT, FieldKind::Float32}, {ric::ROS_TYPE_DOUBLE, FieldKind::Float64},
    {ric::ROS_TYPE_LONG_DOUBLE, FieldKind::LongDouble}, {ric::ROS_TYPE_CHAR, FieldKind::Char},
    {ric::ROS_TYPE_WCHAR, FieldKind::WChar}, {ric::ROS_TYPE_BOOLEAN, FieldKind::Bool},
    {ric::ROS_TYPE_OCTET, FieldKind::Byte}, {ric::ROS_TYPE_UINT8, FieldKind::UInt8},
    {ric::ROS_TYPE_INT8, FieldKind::Int8}, {ric::ROS_TYPE_UINT16, FieldKind::UInt16},
    {ric::ROS_TYPE_INT16, FieldKind::Int16}, {ric::ROS_TYPE_UINT32, FieldKind::UInt32},
    {ric::ROS_TYPE_INT32, FieldKind::Int32}, {ric::ROS_TYPE_UINT64, FieldKind::UInt64},
    {ric::ROS_TYPE_INT64, FieldKind::Int64}, {ric::ROS_TYPE_STRING, FieldKind::String},
    {ric::ROS_TYPE_WSTRING, FieldKind::WString}, {ric::ROS_TYPE_MESSAGE, FieldKind::Message},
  };
  for (const auto & [id, kind] : expected) {
    FieldKind out = FieldKind::Message;
    EXPECT_TRUE(qar_ros_ui::field_kind_from_type_id(id, out)) << int(id);
    EXPECT_EQ(out, kind) << int(id);
  }
  FieldKind out = FieldKind::Message;
  EXPECT_FALSE(qar_ros_ui::field_kind_from_type_id(0, out));
  EXPECT_FALSE(qar_ros_ui::field_kind_from_type_id(99, out));
  EXPECT_EQ(qar_ros_ui::primitive_size(ric::ROS_TYPE_INT64), 8u);
  EXPECT_EQ(qar_ros_ui::primitive_size(ric::ROS_TYPE_BOOLEAN), 1u);
  EXPECT_EQ(qar_ros_ui::primitive_size(ric::ROS_TYPE_MESSAGE), 0u);
}

TEST(Introspection, PoseStampedIsMappedFieldForField)
{
  auto description = describe("geometry_msgs/msg/PoseStamped");
  ASSERT_NE(description, nullptr);
  EXPECT_EQ(description->type_name, "geometry_msgs/msg/PoseStamped");
  ASSERT_EQ(description->fields.size(), 2u);
  EXPECT_EQ(description->fields[0].name, "header");
  EXPECT_EQ(description->fields[0].kind, FieldKind::Message);
  EXPECT_EQ(description->fields[0].type_name, "std_msgs/msg/Header");
  EXPECT_EQ(description->fields[1].name, "pose");

  const FieldDescription * sec = field(*description, "header.stamp.sec");
  ASSERT_NE(sec, nullptr);
  EXPECT_EQ(sec->kind, FieldKind::Int32);
  const FieldDescription * nanosec = field(*description, "header.stamp.nanosec");
  ASSERT_NE(nanosec, nullptr);
  EXPECT_EQ(nanosec->kind, FieldKind::UInt32);
  const FieldDescription * frame = field(*description, "header.frame_id");
  ASSERT_NE(frame, nullptr);
  EXPECT_EQ(frame->kind, FieldKind::String);
  EXPECT_EQ(frame->string_upper_bound, 0u);
  const FieldDescription * x = field(*description, "pose.position.x");
  ASSERT_NE(x, nullptr);
  EXPECT_EQ(x->kind, FieldKind::Float64);
  EXPECT_FALSE(x->is_array());
  const FieldDescription * w = field(*description, "pose.orientation.w");
  ASSERT_NE(w, nullptr);
  EXPECT_EQ(w->kind, FieldKind::Float64);
  expect_valid(*description);
}

TEST(Introspection, NestedDescriptionsAreSharedAcrossTypes)
{
  auto pose = describe("geometry_msgs/msg/PoseStamped");
  auto battery = describe("sensor_msgs/msg/BatteryState");
  ASSERT_NE(pose, nullptr);
  ASSERT_NE(battery, nullptr);
  const FieldDescription * pose_header = pose->find_field("header");
  const FieldDescription * battery_header = battery->find_field("header");
  ASSERT_NE(pose_header, nullptr);
  ASSERT_NE(battery_header, nullptr);
  EXPECT_EQ(pose_header->message, battery_header->message);
  // And a second introspection of the same type hands out the same instance.
  EXPECT_EQ(describe("geometry_msgs/msg/PoseStamped"), pose);
}

TEST(Introspection, BatteryStateCoversPrimitivesAndUnboundedArrays)
{
  auto description = describe("sensor_msgs/msg/BatteryState");
  ASSERT_NE(description, nullptr);
  const FieldDescription * voltage = description->find_field("voltage");
  ASSERT_NE(voltage, nullptr);
  EXPECT_EQ(voltage->kind, FieldKind::Float32);
  const FieldDescription * cells = description->find_field("cell_voltage");
  ASSERT_NE(cells, nullptr);
  EXPECT_EQ(cells->kind, FieldKind::Float32);
  EXPECT_EQ(cells->array_kind, ArrayKind::Unbounded);
  const FieldDescription * status = description->find_field("power_supply_status");
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->kind, FieldKind::UInt8);
  const FieldDescription * present = description->find_field("present");
  ASSERT_NE(present, nullptr);
  EXPECT_EQ(present->kind, FieldKind::Bool);
  const FieldDescription * location = description->find_field("location");
  ASSERT_NE(location, nullptr);
  EXPECT_EQ(location->kind, FieldKind::String);
  expect_valid(*description);
}

TEST(Introspection, DiagnosticArrayCoversByteAndArraysOfMessages)
{
  auto description = describe("diagnostic_msgs/msg/DiagnosticArray");
  ASSERT_NE(description, nullptr);
  const FieldDescription * status = description->find_field("status");
  ASSERT_NE(status, nullptr);
  EXPECT_EQ(status->kind, FieldKind::Message);
  EXPECT_EQ(status->array_kind, ArrayKind::Unbounded);
  EXPECT_EQ(status->type_name, "diagnostic_msgs/msg/DiagnosticStatus");
  ASSERT_NE(status->message, nullptr);
  const FieldDescription * level = status->message->find_field("level");
  ASSERT_NE(level, nullptr);
  EXPECT_EQ(level->kind, FieldKind::Byte);  // `byte level` in the .msg
  const FieldDescription * values = status->message->find_field("values");
  ASSERT_NE(values, nullptr);
  EXPECT_EQ(values->type_name, "diagnostic_msgs/msg/KeyValue");
  EXPECT_EQ(values->array_kind, ArrayKind::Unbounded);
  const FieldDescription * key = field(*description, "status[0].values[0].key");
  ASSERT_NE(key, nullptr);
  EXPECT_EQ(key->kind, FieldKind::String);
  expect_valid(*description);
}

TEST(Introspection, FixedBoundedAndUnboundedArraysAndStringBounds)
{
  auto arrays = describe("test_msgs/msg/Arrays");
  ASSERT_NE(arrays, nullptr);
  const FieldDescription * bools = arrays->find_field("bool_values");
  ASSERT_NE(bools, nullptr);
  EXPECT_EQ(bools->kind, FieldKind::Bool);
  EXPECT_EQ(bools->array_kind, ArrayKind::Fixed);
  EXPECT_EQ(bools->array_size, 3u);
  const FieldDescription * nested = arrays->find_field("basic_types_values");
  ASSERT_NE(nested, nullptr);
  EXPECT_EQ(nested->kind, FieldKind::Message);
  EXPECT_EQ(nested->array_kind, ArrayKind::Fixed);
  EXPECT_EQ(nested->type_name, "test_msgs/msg/BasicTypes");
  expect_valid(*arrays);

  auto bounded = describe("test_msgs/msg/BoundedSequences");
  ASSERT_NE(bounded, nullptr);
  const FieldDescription * bounded_strings = bounded->find_field("string_values");
  ASSERT_NE(bounded_strings, nullptr);
  EXPECT_EQ(bounded_strings->array_kind, ArrayKind::Bounded);
  EXPECT_EQ(bounded_strings->array_size, 3u);
  expect_valid(*bounded);

  auto unbounded = describe("test_msgs/msg/UnboundedSequences");
  ASSERT_NE(unbounded, nullptr);
  const FieldDescription * u64 = unbounded->find_field("uint64_values");
  ASSERT_NE(u64, nullptr);
  EXPECT_EQ(u64->kind, FieldKind::UInt64);
  EXPECT_EQ(u64->array_kind, ArrayKind::Unbounded);
  EXPECT_EQ(u64->array_size, 0u);
  expect_valid(*unbounded);

  auto strings = describe("test_msgs/msg/Strings");
  ASSERT_NE(strings, nullptr);
  const FieldDescription * bounded_string = strings->find_field("bounded_string_value");
  ASSERT_NE(bounded_string, nullptr);
  EXPECT_EQ(bounded_string->kind, FieldKind::String);
  EXPECT_EQ(bounded_string->string_upper_bound, 22u);
  EXPECT_EQ(strings->find_field("string_value")->string_upper_bound, 0u);
  expect_valid(*strings);

  auto wstrings = describe("test_msgs/msg/WStrings");
  ASSERT_NE(wstrings, nullptr);
  EXPECT_EQ(wstrings->find_field("wstring_value")->kind, FieldKind::WString);
  const FieldDescription * warray = wstrings->find_field("array_of_wstrings");
  ASSERT_NE(warray, nullptr);
  EXPECT_EQ(warray->kind, FieldKind::WString);
  EXPECT_EQ(warray->array_kind, ArrayKind::Fixed);
  EXPECT_EQ(warray->array_size, 3u);
  expect_valid(*wstrings);
}

TEST(Introspection, MultiNestedArraysOfArrays)
{
  auto description = describe("test_msgs/msg/MultiNested");
  ASSERT_NE(description, nullptr);
  const FieldDescription * fixed = description->find_field("array_of_arrays");
  ASSERT_NE(fixed, nullptr);
  EXPECT_EQ(fixed->array_kind, ArrayKind::Fixed);
  EXPECT_EQ(fixed->type_name, "test_msgs/msg/Arrays");
  const FieldDescription * bounded = description->find_field("bounded_sequence_of_bounded_sequences");
  ASSERT_NE(bounded, nullptr);
  EXPECT_EQ(bounded->array_kind, ArrayKind::Bounded);
  EXPECT_EQ(bounded->type_name, "test_msgs/msg/BoundedSequences");
  const FieldDescription * unbounded =
    description->find_field("unbounded_sequence_of_unbounded_sequences");
  ASSERT_NE(unbounded, nullptr);
  EXPECT_EQ(unbounded->array_kind, ArrayKind::Unbounded);
  EXPECT_EQ(unbounded->type_name, "test_msgs/msg/UnboundedSequences");
  const FieldDescription * deep = field(*description, "array_of_arrays[1].basic_types_values[2].int8_value");
  ASSERT_NE(deep, nullptr);
  EXPECT_EQ(deep->kind, FieldKind::Int8);
  expect_valid(*description);
}

TEST(Introspection, IdlDefaultsAreRecoveredFromTheGeneratedConstructor)
{
  auto defaults = describe("test_msgs/msg/Defaults");
  ASSERT_NE(defaults, nullptr);
  EXPECT_EQ(defaults->find_field("bool_value")->default_value, UiValue(true));
  EXPECT_EQ(defaults->find_field("byte_value")->default_value, UiValue(std::int64_t{50}));
  EXPECT_EQ(defaults->find_field("char_value")->default_value, UiValue(std::int64_t{100}));
  EXPECT_EQ(defaults->find_field("float32_value")->default_value, UiValue(1.125));
  EXPECT_EQ(defaults->find_field("float64_value")->default_value, UiValue(1.125));
  EXPECT_EQ(defaults->find_field("int8_value")->default_value, UiValue(std::int64_t{-50}));
  EXPECT_EQ(defaults->find_field("uint8_value")->default_value, UiValue(std::int64_t{200}));
  EXPECT_EQ(defaults->find_field("int64_value")->default_value, UiValue(std::int64_t{-40000000}));
  EXPECT_EQ(defaults->find_field("uint64_value")->default_value, UiValue(std::int64_t{50000000}));

  // No default in the IDL -> null, and the schema zero-fills.
  auto basic = describe("test_msgs/msg/BasicTypes");
  ASSERT_NE(basic, nullptr);
  for (const FieldDescription & f : basic->fields) {
    EXPECT_TRUE(f.default_value.is_null()) << f.name;
  }

  auto arrays = describe("test_msgs/msg/Arrays");
  ASSERT_NE(arrays, nullptr);
  EXPECT_EQ(arrays->find_field("int8_values_default")->default_value, qar_ros_ui_test::json("[0,127,-128]"));
  EXPECT_EQ(arrays->find_field("bool_values_default")->default_value, qar_ros_ui_test::json("[false,true,false]"));
  EXPECT_EQ(
    arrays->find_field("string_values_default")->default_value,
    qar_ros_ui_test::json("[\"\",\"max value\",\"min value\"]"));
  EXPECT_TRUE(arrays->find_field("int8_values")->default_value.is_null());

  auto bounded = describe("test_msgs/msg/BoundedSequences");
  ASSERT_NE(bounded, nullptr);
  EXPECT_EQ(bounded->find_field("float64_values_default")->default_value, qar_ros_ui_test::json("[3.1415,0.0,-3.1415]"));

  auto strings = describe("test_msgs/msg/Strings");
  ASSERT_NE(strings, nullptr);
  EXPECT_EQ(strings->find_field("string_value_default1")->default_value, UiValue("Hello world!"));
  EXPECT_EQ(strings->find_field("bounded_string_value_default3")->default_value, UiValue("Hello\"world!"));
  EXPECT_TRUE(strings->find_field("string_value")->default_value.is_null());

  auto wstrings = describe("test_msgs/msg/WStrings");
  ASSERT_NE(wstrings, nullptr);
  EXPECT_EQ(
    wstrings->find_field("wstring_value_default2")->default_value,
    UiValue("Hell\xC3\xB6 w\xC3\xB6rld!"));
}

TEST(Introspection, DefaultRecoveryCanBeSwitchedOff)
{
  IntrospectionOptions options;
  options.record_defaults = false;
  auto defaults = describe("test_msgs/msg/Defaults", options);
  ASSERT_NE(defaults, nullptr);
  EXPECT_TRUE(defaults->find_field("bool_value")->default_value.is_null());
  // A different option set is a different cached description.
  EXPECT_NE(defaults, describe("test_msgs/msg/Defaults"));
}

TEST(Introspection, SchemaDefaultsAgreeWithAFreshInstance)
{
  for (const char * type : {"test_msgs/msg/Defaults", "test_msgs/msg/Arrays",
      "test_msgs/msg/BoundedSequences", "test_msgs/msg/UnboundedSequences", "test_msgs/msg/Strings",
      "test_msgs/msg/WStrings", "test_msgs/msg/MultiNested", "test_msgs/msg/Builtins",
      "geometry_msgs/msg/PoseStamped", "sensor_msgs/msg/BatteryState",
      "diagnostic_msgs/msg/DiagnosticArray"})
  {
    auto description = describe(type);
    ASSERT_NE(description, nullptr) << type;
    DynamicMessage instance;
    const Result created = DynamicMessage::create(type, instance);
    ASSERT_TRUE(created.ok()) << type << ": " << created.message;
    qar_ros_ui::ConversionLimits unlimited;
    unlimited.max_array_elements = 0;
    const UiValue from_instance = instance.to_ui_value(unlimited);
    const UiValue from_schema = qar_ui_model::make_default_value(*description);
    EXPECT_EQ(from_instance, from_schema) << type << "\n instance: " << dump(from_instance) <<
      "\n schema:   " << dump(from_schema);
  }
}

TEST(Introspection, ConstantsAreNotAvailableAtRunTime)
{
  auto constants = describe("test_msgs/msg/Constants");
  ASSERT_NE(constants, nullptr);
  EXPECT_TRUE(constants->constants.empty());  // documented Humble limit
  // A message with no fields has one placeholder member (rosidl adds it).
  ASSERT_EQ(constants->fields.size(), 1u);
  EXPECT_EQ(constants->fields[0].name, "structure_needs_at_least_one_member");
  EXPECT_EQ(constants->fields[0].kind, FieldKind::UInt8);
  auto empty = describe("test_msgs/msg/Empty");
  ASSERT_NE(empty, nullptr);
  ASSERT_EQ(empty->fields.size(), 1u);
  EXPECT_EQ(empty->fields[0].name, "structure_needs_at_least_one_member");
}

TEST(Introspection, ServicesAreTwoMessages)
{
  qar_ui_model::ServiceDescription basic;
  Result result = qar_ros_ui::introspect_service("test_msgs/srv/BasicTypes", basic);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(basic.type_name, "test_msgs/srv/BasicTypes");
  ASSERT_NE(basic.request, nullptr);
  ASSERT_NE(basic.response, nullptr);
  EXPECT_EQ(basic.request->type_name, "test_msgs/srv/BasicTypes_Request");
  EXPECT_EQ(basic.response->type_name, "test_msgs/srv/BasicTypes_Response");
  EXPECT_EQ(basic.request->fields.size(), 14u);
  EXPECT_EQ(basic.request->find_field("string_value")->kind, FieldKind::String);
  expect_valid(*basic.request);
  expect_valid(*basic.response);

  qar_ui_model::ServiceDescription set_bool;
  result = qar_ros_ui::introspect_service("std_srvs/srv/SetBool", set_bool);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(set_bool.request->find_field("data")->kind, FieldKind::Bool);
  EXPECT_EQ(set_bool.response->find_field("success")->kind, FieldKind::Bool);
  EXPECT_EQ(set_bool.response->find_field("message")->kind, FieldKind::String);
}

TEST(Introspection, ActionsAreThreeMessages)
{
  qar_ui_model::ActionDescription fibonacci;
  Result result = qar_ros_ui::introspect_action("test_msgs/action/Fibonacci", fibonacci);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(fibonacci.type_name, "test_msgs/action/Fibonacci");
  ASSERT_NE(fibonacci.goal, nullptr);
  EXPECT_EQ(fibonacci.goal->type_name, "test_msgs/action/Fibonacci_Goal");
  EXPECT_EQ(fibonacci.goal->find_field("order")->kind, FieldKind::Int32);
  ASSERT_NE(fibonacci.result, nullptr);
  const FieldDescription * sequence = fibonacci.result->find_field("sequence");
  ASSERT_NE(sequence, nullptr);
  EXPECT_EQ(sequence->kind, FieldKind::Int32);
  EXPECT_EQ(sequence->array_kind, ArrayKind::Unbounded);
  ASSERT_NE(fibonacci.feedback, nullptr);
  EXPECT_EQ(fibonacci.feedback->find_field("sequence")->array_kind, ArrayKind::Unbounded);

  qar_ui_model::ActionDescription nested;
  result = qar_ros_ui::introspect_action("test_msgs/action/NestedMessage", nested);
  ASSERT_TRUE(result.ok()) << result.message;
  EXPECT_EQ(nested.goal->find_field("nested_field_no_pkg")->type_name, "test_msgs/msg/Builtins");
  EXPECT_EQ(nested.goal->find_field("nested_field")->type_name, "test_msgs/msg/BasicTypes");
  EXPECT_EQ(
    nested.goal->find_field("nested_different_pkg")->type_name, "builtin_interfaces/msg/Time");
  expect_valid(*nested.goal);
}

TEST(Introspection, TableHelpers)
{
  qar_ros_ui::MessageTypeSupport ts;
  ASSERT_TRUE(qar_ros_ui::TypeSupportRegistry::global().message("geometry_msgs/msg/PoseStamped", ts).ok());
  EXPECT_EQ(qar_ros_ui::type_name_from_members(*ts.members()), "geometry_msgs/msg/PoseStamped");
  EXPECT_NE(qar_ros_ui::find_member(*ts.members(), "header"), nullptr);
  EXPECT_NE(qar_ros_ui::find_member(*ts.members(), "pose"), nullptr);
  EXPECT_EQ(qar_ros_ui::find_member(*ts.members(), "nope"), nullptr);
}

TEST(Introspection, UnknownTypesAreReportedNotThrown)
{
  MessageDescriptionPtr description;
  const Result result = qar_ros_ui::introspect_message("nope_pkg/msg/Nope", description);
  EXPECT_EQ(result.status, Status::TypeSupportUnavailable);
  EXPECT_EQ(description, nullptr);
  qar_ui_model::ActionDescription action;
  EXPECT_EQ(
    qar_ros_ui::introspect_action("geometry_msgs/msg/Pose", action).status, Status::InvalidArgument);
}
