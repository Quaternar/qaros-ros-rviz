// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

#include "rclcpp/serialization.hpp"
#include "rclcpp/serialized_message.hpp"

#include "qar_ui_model/type_description.hpp"

#include "qar_ros_ui/dynamic_message.hpp"
#include "qar_ros_ui/introspection.hpp"

#ifdef QAR_ROS_UI_LIVE_TESTS
#include "test_msgs/msg/basic_types.hpp"
#endif

#include "test_support.hpp"

using qar_ros_ui::ConversionLimits;
using qar_ros_ui::ConversionOptions;
using qar_ros_ui::ConversionReport;
using qar_ros_ui::DynamicMessage;
using qar_ros_ui::Result;
using qar_ros_ui::Status;
using qar_ui_model::UiValue;
using qar_ros_ui_test::at;
using qar_ros_ui_test::dump;
using qar_ros_ui_test::json;

namespace
{

DynamicMessage make(const std::string & type)
{
  DynamicMessage message;
  const Result result = DynamicMessage::create(type, message);
  if (!result.ok()) {
    ADD_FAILURE() << type << ": " << result.message;
  }
  return message;
}

/// @p input is a (possibly partial) view of @p actual: objects may omit
/// fields, doubles may differ by float32 rounding, everything else is exact.
void expect_subset(const UiValue & input, const UiValue & actual, const std::string & path)
{
  if (input.is_object()) {
    ASSERT_TRUE(actual.is_object()) << path << ": " << dump(actual);
    for (const auto & entry : input.entries()) {
      const UiValue * member = actual.find(entry.key);
      ASSERT_NE(member, nullptr) << path << "." << entry.key;
      expect_subset(entry.value, *member, path + "." + entry.key);
    }
    return;
  }
  if (input.is_array()) {
    ASSERT_TRUE(actual.is_array()) << path << ": " << dump(actual);
    ASSERT_EQ(input.size(), actual.size()) << path;
    for (std::size_t i = 0; i < input.size(); ++i) {
      expect_subset(*input.at(i), *actual.at(i), path + "[" + std::to_string(i) + "]");
    }
    return;
  }
  if (input.is_double()) {
    ASSERT_TRUE(actual.is_number()) << path << ": " << dump(actual);
    const double expected = input.as_double();
    EXPECT_NEAR(actual.as_double(), expected, 1e-6 * std::max(1.0, std::fabs(expected))) << path;
    return;
  }
  EXPECT_EQ(actual, input) << path << ": " << dump(actual);
}

/// UiValue -> instance -> serialize -> new instance -> UiValue, asserting equality.
void expect_round_trip(const std::string & type, const UiValue & input)
{
  DynamicMessage first = make(type);
  ASSERT_TRUE(first.valid());
  const Result written = first.from_ui_value(input);
  ASSERT_TRUE(written.ok()) << type << ": " << written.message;
  ConversionLimits unlimited;
  unlimited.max_array_elements = 0;
  const UiValue before = first.to_ui_value(unlimited);

  rclcpp::SerializedMessage bytes;
  const Result serialized = first.serialize(bytes);
  ASSERT_TRUE(serialized.ok()) << serialized.message;
  EXPECT_GT(bytes.size(), 0u);

  DynamicMessage second = make(type);
  const Result deserialized = second.deserialize(bytes);
  ASSERT_TRUE(deserialized.ok()) << deserialized.message;
  const UiValue after = second.to_ui_value(unlimited);
  EXPECT_EQ(before, after) << type << "\n before: " << dump(before) << "\n after:  " << dump(after);

  // Every input field survived (the instance may carry more: defaults).
  expect_subset(input, after, type);
}

}  // namespace

TEST(DynamicMessage, FreshInstanceCarriesIdlDefaults)
{
  DynamicMessage message = make("test_msgs/msg/Defaults");
  ASSERT_TRUE(message.valid());
  EXPECT_EQ(message.type_name(), "test_msgs/msg/Defaults");
  const UiValue value = message.to_ui_value();
  EXPECT_EQ(*at(value, "bool_value"), UiValue(true));
  EXPECT_EQ(*at(value, "byte_value"), UiValue(std::int64_t{50}));
  EXPECT_EQ(*at(value, "char_value"), UiValue(std::int64_t{100}));
  EXPECT_EQ(*at(value, "float32_value"), UiValue(1.125));
  EXPECT_EQ(*at(value, "int64_value"), UiValue(std::int64_t{-40000000}));
  EXPECT_EQ(*at(value, "uint64_value"), UiValue(std::int64_t{50000000}));
}

TEST(DynamicMessage, EmptyInstanceIsInert)
{
  DynamicMessage message;
  EXPECT_FALSE(message.valid());
  EXPECT_TRUE(message.to_ui_value().is_null());
  EXPECT_EQ(message.from_ui_value(json("{}")).status, Status::InvalidState);
  rclcpp::SerializedMessage bytes;
  EXPECT_EQ(message.serialize(bytes).status, Status::InvalidState);
  EXPECT_EQ(message.members().member_count_, 0u);
  DynamicMessage unknown;
  EXPECT_EQ(DynamicMessage::create("nope/msg/Nope", unknown).status, Status::TypeSupportUnavailable);
}

TEST(DynamicMessage, BasicTypesRoundTripThroughSerialization)
{
  // The service's request half: BasicTypes.msg has no string, the .srv adds one.
  expect_round_trip(
    "test_msgs/srv/BasicTypes_Request",
    json(R"({"bool_value":true,"byte_value":255,"char_value":65,"float32_value":1.5,
      "float64_value":-2.25,"int8_value":-128,"uint8_value":200,"int16_value":-1000,
      "uint16_value":60000,"int32_value":-2147483648,"uint32_value":4294967295,
      "int64_value":-9223372036854775807,"uint64_value":1234567,"string_value":"héllo"})"));
}

TEST(DynamicMessage, PartialObjectKeepsDefaultsForAbsentFields)
{
  DynamicMessage message = make("test_msgs/msg/Defaults");
  ASSERT_TRUE(message.from_ui_value(json(R"({"int8_value":7})")).ok());
  const UiValue value = message.to_ui_value();
  EXPECT_EQ(*at(value, "int8_value"), UiValue(std::int64_t{7}));
  EXPECT_EQ(*at(value, "bool_value"), UiValue(true));
  EXPECT_EQ(*at(value, "float32_value"), UiValue(1.125));

  // Atomic: a failing conversion leaves the instance as it was.
  const Result failed = message.from_ui_value(json(R"({"int8_value":9,"bool_value":"no"})"));
  EXPECT_EQ(failed.status, Status::ConversionFailed);
  EXPECT_EQ(*at(message.to_ui_value(), "int8_value"), UiValue(std::int64_t{7}));
}

TEST(DynamicMessage, UnknownFieldsRejectedUnlessAsked)
{
  DynamicMessage message = make("test_msgs/msg/BasicTypes");
  const Result strict = message.from_ui_value(json(R"({"int32_value":1,"bogus":2})"));
  EXPECT_EQ(strict.status, Status::ConversionFailed);
  EXPECT_NE(strict.message.find("bogus"), std::string::npos) << strict.message;

  ConversionOptions lenient;
  lenient.reject_unknown_fields = false;
  ASSERT_TRUE(message.from_ui_value(json(R"({"int32_value":1,"bogus":2})"), lenient).ok());
  EXPECT_EQ(*at(message.to_ui_value(), "int32_value"), UiValue(std::int64_t{1}));

  ConversionOptions complete;
  complete.require_all_fields = true;
  const Result missing = message.from_ui_value(json(R"({"int32_value":1})"), complete);
  EXPECT_EQ(missing.status, Status::ConversionFailed);
  EXPECT_NE(missing.message.find("missing field"), std::string::npos) << missing.message;
}

TEST(DynamicMessage, KindAndRangeMismatchesAreReportedWithPaths)
{
  DynamicMessage message = make("test_msgs/srv/BasicTypes_Request");
  struct Case
  {
    const char * input;
    const char * expect_in_message;
  };
  const Case cases[] = {
    {R"({"bool_value":1})", "bool_value: expected bool"},
    {R"({"int32_value":"x"})", "int32_value: expected int32"},
    {R"({"string_value":5})", "string_value: expected string"},
    {R"({"uint8_value":256})", "uint8_value: value 256 out of range"},
    {R"({"int8_value":1.5})", "int8_value: expected int8, got a non-integral double"},
    {R"({"float64_value":"1"})", "float64_value: expected float64"},
    {R"({"uint64_value":-1})", "uint64_value: value -1 out of range"},
    {R"({"int16_value":40000})", "int16_value: value 40000 out of range"},
    {R"({"byte_value":-1})", "byte_value: value -1 out of range"},
  };
  for (const Case & c : cases) {
    const Result result = message.from_ui_value(json(c.input));
    EXPECT_EQ(result.status, Status::ConversionFailed) << c.input;
    EXPECT_NE(result.message.find(c.expect_in_message), std::string::npos)
      << c.input << " -> " << result.message;
  }
  EXPECT_EQ(message.from_ui_value(json("[1,2]")).status, Status::ConversionFailed);
}

TEST(DynamicMessage, IntegralDoublesAreAcceptedForIntegers)
{
  DynamicMessage message = make("test_msgs/msg/BasicTypes");
  ASSERT_TRUE(message.from_ui_value(json(R"({"int32_value":5.0,"uint8_value":200.0,"int64_value":-3.0})")).ok());
  const UiValue value = message.to_ui_value();
  EXPECT_EQ(*at(value, "int32_value"), UiValue(std::int64_t{5}));
  EXPECT_EQ(*at(value, "uint8_value"), UiValue(std::int64_t{200}));
  EXPECT_EQ(*at(value, "int64_value"), UiValue(std::int64_t{-3}));
  // And an Int is fine for a float.
  ASSERT_TRUE(message.from_ui_value(json(R"({"float32_value":2,"float64_value":-7})")).ok());
  EXPECT_EQ(*at(message.to_ui_value(), "float64_value"), UiValue(-7.0));
}

TEST(DynamicMessage, Uint64AboveInt64MaxTravelsAsDoubleAndSaturates)
{
  DynamicMessage message = make("test_msgs/msg/BasicTypes");
  UiValue input = UiValue::object();
  input.set("uint64_value", UiValue(std::numeric_limits<std::uint64_t>::max()));
  ASSERT_TRUE(input.find("uint64_value")->is_double());
  ASSERT_TRUE(message.from_ui_value(input).ok());
  EXPECT_EQ(*static_cast<const std::uint64_t *>(
      static_cast<const void *>(static_cast<const std::uint8_t *>(message.data()) +
      qar_ros_ui::find_member(message.members(), "uint64_value")->offset_)),
    std::numeric_limits<std::uint64_t>::max());
  EXPECT_EQ(*at(message.to_ui_value(), "uint64_value"), *input.find("uint64_value"));
  expect_round_trip("test_msgs/msg/BasicTypes", input);

  // 2^63 as a double is what INT64_MAX rounds to: saturate, do not overflow.
  UiValue big = UiValue::object();
  big.set("int64_value", UiValue(9223372036854775808.0));
  ASSERT_TRUE(message.from_ui_value(big).ok());
  EXPECT_EQ(*at(message.to_ui_value(), "int64_value"), UiValue(std::numeric_limits<std::int64_t>::max()));
  UiValue too_big = UiValue::object();
  too_big.set("uint64_value", UiValue(1e20));
  EXPECT_EQ(message.from_ui_value(too_big).status, Status::ConversionFailed);
}

TEST(DynamicMessage, NanAndInfinityBecomeNullAndNullBecomesNan)
{
  DynamicMessage message = make("sensor_msgs/msg/BatteryState");
  UiValue input = UiValue::object();
  input.set("percentage", UiValue(std::numeric_limits<double>::quiet_NaN()));
  input.set("voltage", UiValue(std::numeric_limits<double>::infinity()));
  input.set("charge", UiValue(nullptr));
  ASSERT_TRUE(message.from_ui_value(input).ok());
  const UiValue value = message.to_ui_value();
  EXPECT_TRUE(at(value, "percentage")->is_null());
  EXPECT_TRUE(at(value, "voltage")->is_null());
  EXPECT_TRUE(at(value, "charge")->is_null());
  EXPECT_TRUE(std::isnan(*static_cast<const float *>(
      static_cast<const void *>(static_cast<const std::uint8_t *>(message.data()) +
      qar_ros_ui::find_member(message.members(), "charge")->offset_))));
  // Null is not a number for an integer field.
  UiValue bad = UiValue::object();
  bad.set("power_supply_status", UiValue(nullptr));
  EXPECT_EQ(message.from_ui_value(bad).status, Status::ConversionFailed);
}

TEST(DynamicMessage, FixedArraysDemandTheExactLength)
{
  const std::string type = "test_msgs/msg/Arrays";
  expect_round_trip(
    type,
    json(R"({"bool_values":[true,false,true],"int8_values":[1,-2,3],"string_values":["a","","c"],
      "float64_values":[0.5,-1.5,2.0],"uint64_values":[0,1,2],
      "basic_types_values":[{"int32_value":1},{"uint16_value":2},{}]})"));
  DynamicMessage message = make(type);
  const Result short_array = message.from_ui_value(json(R"({"int8_values":[1,2]})"));
  EXPECT_EQ(short_array.status, Status::ConversionFailed);
  EXPECT_NE(short_array.message.find("fixed array of size 3 has 2 elements"), std::string::npos)
    << short_array.message;
  EXPECT_EQ(message.from_ui_value(json(R"({"int8_values":5})")).status, Status::ConversionFailed);
  EXPECT_EQ(message.from_ui_value(json(R"({"int8_values":[1,2,"x"]})")).status, Status::ConversionFailed);
  const Result nested_bad = message.from_ui_value(json(R"({"basic_types_values":[{},{"int8_value":999},{}]})"));
  EXPECT_EQ(nested_bad.status, Status::ConversionFailed);
  EXPECT_NE(nested_bad.message.find("basic_types_values[1].int8_value"), std::string::npos) << nested_bad.message;
}

TEST(DynamicMessage, BoundedSequencesIncludingVectorOfBool)
{
  const std::string type = "test_msgs/msg/BoundedSequences";
  expect_round_trip(
    type,
    json(R"({"bool_values":[true,false],"int8_values":[1],"string_values":["x","y","z"],
      "basic_types_values":[{"int32_value":3}],"defaults_values":[{}],"float32_values":[]})"));
  DynamicMessage message = make(type);
  const Result too_many = message.from_ui_value(json(R"({"int8_values":[1,2,3,4]})"));
  EXPECT_EQ(too_many.status, Status::ConversionFailed);
  EXPECT_NE(too_many.message.find("bounded array (<=3) has 4 elements"), std::string::npos) << too_many.message;
  // A nested default survives the resize: the element is constructed, then filled.
  ASSERT_TRUE(message.from_ui_value(json(R"({"defaults_values":[{"int8_value":1},{}]})")).ok());
  const UiValue value = message.to_ui_value();
  EXPECT_EQ(*at(value, "defaults_values[0].int8_value"), UiValue(std::int64_t{1}));
  EXPECT_EQ(*at(value, "defaults_values[1].int8_value"), UiValue(std::int64_t{-50}));
  EXPECT_EQ(*at(value, "defaults_values[1].bool_value"), UiValue(true));
  // Shrinking drops the tail.
  ASSERT_TRUE(message.from_ui_value(json(R"({"bool_values":[true,true,false]})")).ok());
  ASSERT_TRUE(message.from_ui_value(json(R"({"bool_values":[false]})")).ok());
  EXPECT_EQ(*at(message.to_ui_value(), "bool_values"), json("[false]"));
}

TEST(DynamicMessage, UnboundedSequencesGrowAsNeeded)
{
  std::string ints = "[";
  for (int i = 0; i < 300; ++i) {
    ints += (i ? "," : "") + std::to_string(i * 7 - 1000);
  }
  ints += "]";
  expect_round_trip(
    "test_msgs/msg/UnboundedSequences",
    json(R"({"bool_values":[true,true,false,true],"int32_values":)" + ints +
    R"(,"float64_values":[1.5,2.5,3.5],"string_values":["one","two"],
      "defaults_values":[{},{"bool_value":false}],"uint8_values":[0,255]})"));
}

TEST(DynamicMessage, StringBoundsAreEnforcedInBytes)
{
  DynamicMessage message = make("test_msgs/msg/Strings");
  ASSERT_TRUE(message.from_ui_value(json(R"({"bounded_string_value":"0123456789012345678901"})")).ok());
  const Result too_long = message.from_ui_value(json(R"({"bounded_string_value":"01234567890123456789012"})"));
  EXPECT_EQ(too_long.status, Status::ConversionFailed);
  EXPECT_NE(too_long.message.find("bound of 22 bytes"), std::string::npos) << too_long.message;
  expect_round_trip(
    "test_msgs/msg/Strings",
    json(R"({"string_value":"héllo wörld 😀","bounded_string_value":"","string_value_default1":"changed"})"));
}

TEST(DynamicMessage, WStringsTranscodeBetweenUtf16AndUtf8)
{
  expect_round_trip(
    "test_msgs/msg/WStrings",
    json(R"({"wstring_value":"Hellö wörld ハロー 😀",
      "array_of_wstrings":["a","é","😀"],"bounded_sequence_of_wstrings":["😀"],
      "unbounded_sequence_of_wstrings":["","x"]})"));
  DynamicMessage message = make("test_msgs/msg/WStrings");
  UiValue input = UiValue::object();
  input.set("wstring_value", UiValue(std::string("ok\xFFno")));  // ill-formed UTF-8 byte
  ASSERT_TRUE(message.from_ui_value(input).ok());
  EXPECT_EQ(*at(message.to_ui_value(), "wstring_value"), UiValue("ok\xEF\xBF\xBDno"));
  EXPECT_EQ(qar_ros_ui::utf8_to_utf16("\xF0\x9F\x98\x80").size(), 2u);
  EXPECT_EQ(qar_ros_ui::utf16_to_utf8(u"\xD83D"), "\xEF\xBF\xBD");  // lone high surrogate
  EXPECT_EQ(qar_ros_ui::utf16_to_utf8(u"\xD83D\xDE00"), "\xF0\x9F\x98\x80");
}

TEST(DynamicMessage, MultiNestedArraysOfMessagesRoundTrip)
{
  expect_round_trip(
    "test_msgs/msg/MultiNested",
    json(R"({"array_of_arrays":[{"int8_values":[1,2,3]},{},{"string_values":["a","b","c"]}],
      "bounded_sequence_of_bounded_sequences":[{"int8_values":[1]}],
      "unbounded_sequence_of_unbounded_sequences":[{},{"float64_values":[1.0,2.0]}],
      "unbounded_sequence_of_arrays":[]})"));
}

TEST(DynamicMessage, RealWorldTypesRoundTrip)
{
  expect_round_trip(
    "geometry_msgs/msg/PoseStamped",
    json(R"({"header":{"stamp":{"sec":12,"nanosec":345},"frame_id":"map"},
      "pose":{"position":{"x":1.5,"y":-2.0,"z":0.25},"orientation":{"x":0.0,"y":0.0,"z":0.7071,"w":0.7071}}})"));
  expect_round_trip(
    "diagnostic_msgs/msg/DiagnosticArray",
    json(R"({"header":{"frame_id":""},"status":[
      {"level":2,"name":"motor","message":"hot","hardware_id":"m1","values":[{"key":"temp","value":"91"}]},
      {"level":0,"name":"battery","message":"ok","hardware_id":"","values":[]}]})"));
  expect_round_trip(
    "sensor_msgs/msg/BatteryState",
    json(R"({"voltage":12.5,"current":-1.25,"percentage":0.5,"power_supply_status":3,
      "cell_voltage":[4.1,4.2,4.15],"present":true,"location":"bay 1","serial_number":"SN-1"})"));
  expect_round_trip("test_msgs/msg/Builtins", json(R"({"duration_value":{"sec":1,"nanosec":2},"time_value":{"sec":3,"nanosec":4}})"));
}

TEST(DynamicMessage, ConversionLimitsTruncateLongArraysAndReport)
{
  DynamicMessage message = make("test_msgs/msg/UnboundedSequences");
  ASSERT_TRUE(message.from_ui_value(json(R"({"int32_values":[1,2,3,4,5,6,7,8,9,10]})")).ok());
  ConversionLimits limits;
  limits.max_array_elements = 4;
  ConversionReport report;
  const UiValue value = message.to_ui_value(limits, &report);
  EXPECT_EQ(*at(value, "int32_values"), json("[1,2,3,4]"));
  EXPECT_EQ(report.truncated_arrays, 1u);
  EXPECT_EQ(report.elements_dropped, 6u);
  EXPECT_FALSE(report.clean());

  ConversionReport clean;
  ConversionLimits unlimited;
  unlimited.max_array_elements = 0;
  EXPECT_EQ(at(message.to_ui_value(unlimited, &clean), "int32_values")->size(), 10u);
  EXPECT_TRUE(clean.clean());
}

TEST(DynamicMessage, DepthLimitStopsDescent)
{
  DynamicMessage message = make("test_msgs/msg/MultiNested");
  ConversionLimits limits;
  limits.max_depth = 1;
  ConversionReport report;
  const UiValue value = message.to_ui_value(limits, &report);
  EXPECT_TRUE(report.depth_exceeded);
  // Elements of the top-level arrays sit at depth 1 and are still objects;
  // the messages they embed (depth 2) are what got cut.
  const UiValue * first = at(value, "array_of_arrays[0]");
  ASSERT_NE(first, nullptr);
  EXPECT_TRUE(first->is_object());
  EXPECT_TRUE(at(value, "array_of_arrays[0].basic_types_values[0]")->is_null());
}

TEST(DynamicMessage, ResetAndReleaseToShared)
{
  DynamicMessage message = make("test_msgs/msg/Defaults");
  ASSERT_TRUE(message.from_ui_value(json(R"({"int8_value":7})")).ok());
  message.reset();
  EXPECT_EQ(*at(message.to_ui_value(), "int8_value"), UiValue(std::int64_t{-50}));

  void * data = message.data();
  std::shared_ptr<void> shared = DynamicMessage::release_to_shared(std::move(message));
  ASSERT_NE(shared, nullptr);
  EXPECT_EQ(shared.get(), data);
  EXPECT_FALSE(message.valid());  // NOLINT(bugprone-use-after-move): the point of the test
  EXPECT_EQ(DynamicMessage::release_to_shared(DynamicMessage()), nullptr);
}

TEST(DynamicMessage, TypeErasedConversionsWorkOnSubMessages)
{
  DynamicMessage message = make("geometry_msgs/msg/PoseStamped");
  const qar_ros_ui::MessageMember * header = qar_ros_ui::find_member(message.members(), "header");
  ASSERT_NE(header, nullptr);
  const auto * header_members =
    static_cast<const qar_ros_ui::MessageMembers *>(header->members_->data);
  void * header_instance = static_cast<std::uint8_t *>(message.data()) + header->offset_;
  ASSERT_TRUE(
    qar_ros_ui::ui_value_to_message(json(R"({"frame_id":"odom"})"), header_instance, *header_members).ok());
  EXPECT_EQ(*at(message.to_ui_value(), "header.frame_id"), UiValue("odom"));
  const UiValue header_value = qar_ros_ui::message_to_ui_value(header_instance, *header_members);
  EXPECT_EQ(*at(header_value, "frame_id"), UiValue("odom"));
  EXPECT_TRUE(qar_ros_ui::message_to_ui_value(nullptr, *header_members).is_null());
}

#ifdef QAR_ROS_UI_LIVE_TESTS
TEST(DynamicMessage, SerializedBytesMatchTheCompileTimeType)
{
  test_msgs::msg::BasicTypes typed;
  typed.bool_value = true;
  typed.byte_value = 9;
  typed.int32_value = -7;
  typed.uint64_value = 99;
  typed.float64_value = 2.5;
  typed.int16_value = -300;
  rclcpp::Serialization<test_msgs::msg::BasicTypes> serializer;
  rclcpp::SerializedMessage typed_bytes;
  serializer.serialize_message(&typed, &typed_bytes);

  DynamicMessage dynamic = make("test_msgs/msg/BasicTypes");
  ASSERT_TRUE(
    dynamic.from_ui_value(
      json(R"({"bool_value":true,"byte_value":9,"int32_value":-7,"uint64_value":99,"float64_value":2.5,"int16_value":-300})")).ok());
  rclcpp::SerializedMessage dynamic_bytes;
  ASSERT_TRUE(dynamic.serialize(dynamic_bytes).ok());

  // Same CDR length. Not compared byte for byte on purpose: Fast-CDR skips
  // alignment padding without writing it, so two independently allocated
  // buffers differ in those bytes while decoding to the same message.
  ASSERT_EQ(typed_bytes.size(), dynamic_bytes.size());

  // The typed bytes read back through introspection.
  DynamicMessage decoded = make("test_msgs/msg/BasicTypes");
  ASSERT_TRUE(decoded.deserialize(typed_bytes).ok());
  const UiValue value = decoded.to_ui_value();
  EXPECT_EQ(*at(value, "int16_value"), UiValue(std::int64_t{-300}));
  EXPECT_EQ(*at(value, "int32_value"), UiValue(std::int64_t{-7}));
  EXPECT_EQ(*at(value, "float64_value"), UiValue(2.5));
  EXPECT_EQ(*at(value, "bool_value"), UiValue(true));

  // The other direction: the dynamic bytes deserialize into the typed struct.
  test_msgs::msg::BasicTypes back;
  serializer.deserialize_message(&dynamic_bytes, &back);
  EXPECT_EQ(back.int16_value, -300);
  EXPECT_EQ(back.int32_value, -7);
  EXPECT_EQ(back.uint64_value, 99u);
  EXPECT_EQ(back.byte_value, 9u);
}
#endif
