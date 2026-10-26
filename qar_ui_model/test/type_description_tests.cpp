// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The schema seam qar_ros_ui fills and templates consume. Fixtures are shaped like real
// ROS 2 interfaces (geometry_msgs/Pose, a LaserScan-like scan, a message with
// fixed/bounded arrays, bounded strings and constants) so the traversal, path
// and value rules are exercised on the structures they will meet.

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_value.hpp"

using namespace qar_ui_model;  // NOLINT(build/namespaces): test readability

namespace
{

MessageDescriptionPtr
point_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Point")
         .field(FieldDescription::primitive("x", FieldKind::Float64).with_unit("m"))
         .field(FieldDescription::primitive("y", FieldKind::Float64).with_unit("m"))
         .field(FieldDescription::primitive("z", FieldKind::Float64).with_unit("m"))
         .build();
}

MessageDescriptionPtr
quaternion_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Quaternion")
         .field(FieldDescription::primitive("x", FieldKind::Float64).with_default(0.0))
         .field(FieldDescription::primitive("y", FieldKind::Float64).with_default(0.0))
         .field(FieldDescription::primitive("z", FieldKind::Float64).with_default(0.0))
         .field(FieldDescription::primitive("w", FieldKind::Float64).with_default(1.0))
         .build();
}

MessageDescriptionPtr
pose_type()
{
  return MessageDescriptionBuilder("geometry_msgs/msg/Pose")
         .field(FieldDescription::nested("position", point_type()))
         .field(FieldDescription::nested("orientation", quaternion_type()))
         .build();
}

MessageDescriptionPtr
header_type()
{
  auto time = MessageDescriptionBuilder("builtin_interfaces/msg/Time")
    .field(FieldDescription::primitive("sec", FieldKind::Int32))
    .field(FieldDescription::primitive("nanosec", FieldKind::UInt32))
    .build();
  return MessageDescriptionBuilder("std_msgs/msg/Header")
         .field(FieldDescription::nested("stamp", time))
         .field(FieldDescription::primitive("frame_id", FieldKind::String))
         .build();
}

// A LaserScan-like message plus the array/bound shapes LaserScan lacks.
MessageDescriptionPtr
scan_type()
{
  return MessageDescriptionBuilder("sensor_msgs/msg/LaserScan")
         .description("Single scan from a planar laser range-finder")
         .field(FieldDescription::nested("header", header_type()))
         .field(FieldDescription::primitive("angle_min", FieldKind::Float32).with_unit("rad"))
         .field(FieldDescription::primitive("ranges", FieldKind::Float32).unbounded_array())
         .field(FieldDescription::primitive("covariance", FieldKind::Float64).fixed_array(4))
         .field(FieldDescription::primitive("names", FieldKind::String).bounded_array(2))
         .field(FieldDescription::primitive("short_name", FieldKind::String).string_bound(8))
         .field(FieldDescription::nested("points", point_type()).unbounded_array())
         .field(FieldDescription::primitive("status", FieldKind::UInt8).with_default(std::int64_t{1}))
         .field(FieldDescription::primitive("tags", FieldKind::Int8).fixed_array(2)
           .with_default(UiValue::array().push_back(7).push_back(-7)))
         .constant("STATUS_OK", FieldKind::UInt8, std::int64_t{0})
         .constant("STATUS_WARN", FieldKind::UInt8, std::int64_t{1})
         .build();
}

std::vector<std::string>
walk_paths(const MessageDescription & root)
{
  std::vector<std::string> paths;
  walk_fields(root, [&](const FieldPath & path, const FieldDescription &, std::size_t) {
    paths.push_back(path.to_string());
    return WalkControl::Continue;
  });
  return paths;
}

}  // namespace

// ---- kinds ----------------------------------------------------------------

TEST(FieldKinds, NamesRoundTripThroughRosSpellings)
{
  const FieldKind all[] = {
    FieldKind::Bool, FieldKind::Byte, FieldKind::Char, FieldKind::WChar, FieldKind::Int8,
    FieldKind::UInt8, FieldKind::Int16, FieldKind::UInt16, FieldKind::Int32, FieldKind::UInt32,
    FieldKind::Int64, FieldKind::UInt64, FieldKind::Float32, FieldKind::Float64,
    FieldKind::LongDouble, FieldKind::String, FieldKind::WString};
  for (FieldKind kind : all) {
    FieldKind back = FieldKind::Message;
    ASSERT_TRUE(field_kind_from_ros_name(to_string(kind), back)) << to_string(kind);
    EXPECT_EQ(back, kind);
  }
  FieldKind kind = FieldKind::Message;
  EXPECT_TRUE(field_kind_from_ros_name("boolean", kind));
  EXPECT_EQ(kind, FieldKind::Bool);
  EXPECT_TRUE(field_kind_from_ros_name("octet", kind));
  EXPECT_EQ(kind, FieldKind::Byte);
  EXPECT_TRUE(field_kind_from_ros_name("double", kind));
  EXPECT_EQ(kind, FieldKind::Float64);
  EXPECT_TRUE(field_kind_from_ros_name("float", kind));
  EXPECT_EQ(kind, FieldKind::Float32);
  EXPECT_FALSE(field_kind_from_ros_name("message", kind));
  EXPECT_FALSE(field_kind_from_ros_name("geometry_msgs/msg/Pose", kind));
  EXPECT_FALSE(field_kind_from_ros_name("", kind));
}

TEST(FieldKinds, Predicates)
{
  EXPECT_TRUE(is_boolean(FieldKind::Bool));
  EXPECT_TRUE(is_integer(FieldKind::Byte));
  EXPECT_TRUE(is_integer(FieldKind::UInt64));
  EXPECT_FALSE(is_integer(FieldKind::Float32));
  EXPECT_TRUE(is_signed_integer(FieldKind::Int8));
  EXPECT_FALSE(is_signed_integer(FieldKind::UInt8));
  EXPECT_TRUE(is_floating(FieldKind::LongDouble));
  EXPECT_TRUE(is_numeric(FieldKind::Int16));
  EXPECT_TRUE(is_numeric(FieldKind::Float64));
  EXPECT_FALSE(is_numeric(FieldKind::String));
  EXPECT_TRUE(is_string(FieldKind::WString));
  EXPECT_TRUE(is_primitive(FieldKind::String));
  EXPECT_FALSE(is_primitive(FieldKind::Message));
}

TEST(FieldKinds, NumericBounds)
{
  ASSERT_TRUE(numeric_bounds(FieldKind::Int8));
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::Int8)->min, -128.0);
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::Int8)->max, 127.0);
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::UInt8)->max, 255.0);
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::Byte)->max, 255.0);
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::WChar)->max, 65535.0);
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::Int32)->min, -2147483648.0);
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::UInt32)->max, 4294967295.0);
  EXPECT_DOUBLE_EQ(numeric_bounds(FieldKind::UInt64)->max, 18446744073709551616.0);
  EXPECT_FALSE(numeric_bounds(FieldKind::Float64));
  EXPECT_FALSE(numeric_bounds(FieldKind::Bool));
  EXPECT_FALSE(numeric_bounds(FieldKind::String));
}

// ---- construction ----------------------------------------------------------

TEST(FieldDescription, FactoriesAndChainedSetters)
{
  const FieldDescription f = FieldDescription::primitive("speed", FieldKind::Float32)
    .with_unit("m/s").with_range(0.0, 2.0).with_step(0.1).with_label("Speed")
    .with_description("Forward speed").bounded_array(4);
  EXPECT_EQ(f.name, "speed");
  EXPECT_EQ(f.kind, FieldKind::Float32);
  EXPECT_EQ(f.type_name, "float32");
  EXPECT_EQ(f.unit, "m/s");
  EXPECT_EQ(*f.min, 0.0);
  EXPECT_EQ(*f.max, 2.0);
  EXPECT_EQ(*f.step, 0.1);
  EXPECT_EQ(f.label, "Speed");
  EXPECT_EQ(f.array_kind, ArrayKind::Bounded);
  EXPECT_EQ(f.array_size, 4u);
  EXPECT_TRUE(f.is_array());
  EXPECT_FALSE(f.is_message());

  const FieldDescription n = FieldDescription::nested("position", point_type());
  EXPECT_EQ(n.kind, FieldKind::Message);
  EXPECT_EQ(n.type_name, "geometry_msgs/msg/Point");
  ASSERT_TRUE(n.message);
  EXPECT_EQ(n.message->fields.size(), 3u);
  EXPECT_TRUE(n.is_message());

  // primitive() never yields a Message kind.
  EXPECT_EQ(FieldDescription::primitive("x", FieldKind::Message).kind, FieldKind::Int32);
}

TEST(FieldDescription, DisplayLabelFallsBackToHumanizedName)
{
  EXPECT_EQ(FieldDescription::primitive("linear_velocity", FieldKind::Float64).display_label(),
    "Linear velocity");
  EXPECT_EQ(FieldDescription::primitive("x", FieldKind::Float64).display_label(), "X");
  EXPECT_EQ(FieldDescription::primitive("x", FieldKind::Float64).with_label("East").display_label(),
    "East");
}

TEST(HumanizeIdentifier, Cases)
{
  EXPECT_EQ(humanize_identifier("linear_velocity"), "Linear velocity");
  EXPECT_EQ(humanize_identifier("maxSpeed"), "Max speed");
  EXPECT_EQ(humanize_identifier("frame_id"), "Frame id");
  EXPECT_EQ(humanize_identifier("x"), "X");
  EXPECT_EQ(humanize_identifier("_leading"), "Leading");
  EXPECT_EQ(humanize_identifier("trailing_"), "Trailing");
  EXPECT_EQ(humanize_identifier("ABC"), "ABC");
  EXPECT_EQ(humanize_identifier(""), "");
}

TEST(MessageDescription, BuilderFindAndConstants)
{
  const MessageDescriptionPtr scan = scan_type();
  EXPECT_EQ(scan->type_name, "sensor_msgs/msg/LaserScan");
  EXPECT_EQ(scan->description, "Single scan from a planar laser range-finder");
  ASSERT_EQ(scan->fields.size(), 9u);
  ASSERT_NE(scan->find_field("ranges"), nullptr);
  EXPECT_EQ(scan->find_field("ranges")->array_kind, ArrayKind::Unbounded);
  EXPECT_EQ(scan->find_field("nope"), nullptr);
  ASSERT_EQ(scan->constants.size(), 2u);
  ASSERT_NE(scan->find_constant("STATUS_WARN"), nullptr);
  EXPECT_EQ(scan->find_constant("STATUS_WARN")->value.as_int(), 1);
  EXPECT_EQ(scan->find_constant("STATUS_WARN")->kind, FieldKind::UInt8);
  EXPECT_EQ(scan->find_constant("nope"), nullptr);
}

TEST(MessageDescription, NestedTypesAreSharedNotCopied)
{
  const MessageDescriptionPtr point = point_type();
  const MessageDescriptionPtr twice = MessageDescriptionBuilder("test/msg/Twice")
    .field(FieldDescription::nested("a", point))
    .field(FieldDescription::nested("b", point))
    .build();
  EXPECT_EQ(twice->fields[0].message.get(), twice->fields[1].message.get());
}

TEST(ValidateDescription, WellFormedFixturesPass)
{
  EXPECT_TRUE(validate_description(*pose_type()).empty());
  EXPECT_TRUE(validate_description(*scan_type()).empty());
}

TEST(ValidateDescription, CatchesStructuralMistakes)
{
  FieldDescription no_type;
  no_type.name = "bad";
  no_type.kind = FieldKind::Message;
  const MessageDescription broken = MessageDescriptionBuilder("test/msg/Broken")
    .field(no_type)
    .field(FieldDescription::primitive("x", FieldKind::Float64))
    .field(FieldDescription::primitive("x", FieldKind::Float64))
    .field(FieldDescription::primitive("zero", FieldKind::Int8).fixed_array(0))
    .field(FieldDescription::primitive("bound0", FieldKind::Int8).bounded_array(0))
    .field(FieldDescription::primitive("9lives", FieldKind::Int8))
    .field(FieldDescription::primitive("num", FieldKind::Int8).string_bound(4))
    .field(FieldDescription::primitive("rng", FieldKind::Int8).with_range(5.0, 1.0).with_step(0.0))
    .constant("bad-name", FieldKind::Int8, 1)
    .build_value();
  const std::vector<DescriptionProblem> problems = validate_description(broken);
  ASSERT_GE(problems.size(), 9u);
  const auto has = [&](const std::string & path, const std::string & fragment) {
    for (const DescriptionProblem & p : problems) {
      if (p.path == path && p.message.find(fragment) != std::string::npos) {
        return true;
      }
    }
    return false;
  };
  EXPECT_TRUE(has("bad", "no element type"));
  EXPECT_TRUE(has("x", "duplicate"));
  EXPECT_TRUE(has("zero", "fixed array of size 0"));
  EXPECT_TRUE(has("bound0", "bound 0"));
  EXPECT_TRUE(has("9lives", "not an identifier"));
  EXPECT_TRUE(has("num", "string_upper_bound"));
  EXPECT_TRUE(has("rng", "min is greater than max"));
  EXPECT_TRUE(has("rng", "step must be positive"));
  EXPECT_TRUE(has("bad-name", "not an identifier"));
}

// ---- type names -----------------------------------------------------------

TEST(TypeNames, SplitAndNormalize)
{
  TypeNameParts parts;
  ASSERT_TRUE(split_type_name("geometry_msgs/msg/Pose", parts));
  EXPECT_EQ(parts.package, "geometry_msgs");
  EXPECT_EQ(parts.interface, "msg");
  EXPECT_EQ(parts.name, "Pose");
  ASSERT_TRUE(split_type_name("nav2_msgs/action/NavigateToPose", parts));
  EXPECT_EQ(parts.interface, "action");
  ASSERT_TRUE(split_type_name("std_srvs/srv/Trigger", parts));
  EXPECT_EQ(parts.interface, "srv");
  ASSERT_TRUE(split_type_name("geometry_msgs/Pose", parts));  // two-part form
  EXPECT_EQ(parts.interface, "msg");
  EXPECT_EQ(parts.name, "Pose");

  EXPECT_FALSE(split_type_name("Pose", parts));
  EXPECT_FALSE(split_type_name("a/b/c/d", parts));
  EXPECT_FALSE(split_type_name("pkg/thing/Name", parts));
  EXPECT_FALSE(split_type_name("pkg//Name", parts));
  EXPECT_FALSE(split_type_name("/msg/Name", parts));
  EXPECT_FALSE(split_type_name("", parts));

  EXPECT_EQ(normalize_type_name("geometry_msgs/Pose"), "geometry_msgs/msg/Pose");
  EXPECT_EQ(normalize_type_name("geometry_msgs/msg/Pose"), "geometry_msgs/msg/Pose");
  EXPECT_EQ(normalize_type_name("weird"), "weird");
}

// ---- paths ------------------------------------------------------------------

TEST(FieldPath, ParseAndPrint)
{
  FieldPath path;
  std::string error;
  ASSERT_TRUE(FieldPath::parse("pose.position.x", path, error)) << error;
  ASSERT_EQ(path.segments.size(), 3u);
  EXPECT_EQ(path.segments[1].name, "position");
  EXPECT_FALSE(path.segments[1].index);
  EXPECT_EQ(path.to_string(), "pose.position.x");

  ASSERT_TRUE(FieldPath::parse("points[12].x", path, error)) << error;
  ASSERT_EQ(path.segments.size(), 2u);
  ASSERT_TRUE(path.segments[0].index);
  EXPECT_EQ(*path.segments[0].index, 12u);
  EXPECT_EQ(path.to_string(), "points[12].x");

  ASSERT_TRUE(FieldPath::parse("ranges[0]", path, error));
  EXPECT_EQ(path.to_string(), "ranges[0]");
  ASSERT_TRUE(FieldPath::parse("_private_1", path, error));
  ASSERT_TRUE(FieldPath::parse("", path, error));
  EXPECT_TRUE(path.empty());
  EXPECT_EQ(path.to_string(), "");
}

TEST(FieldPath, RejectsBadSyntax)
{
  FieldPath path;
  std::string error;
  const char * bad[] = {
    ".x", "x.", "a..b", "a[", "a[]", "a[x]", "a[1", "a]", "a[-1]", "a[1][2]", "1a", "a b",
    "a-b", "a/b", "[0]", "a.[0]", "a[1]b", "a[99999999999]"};
  for (const char * text : bad) {
    EXPECT_FALSE(FieldPath::parse(text, path, error)) << "accepted: " << text;
    EXPECT_FALSE(error.empty());
  }
}

TEST(FieldPath, Appended)
{
  FieldPath base;
  std::string error;
  ASSERT_TRUE(FieldPath::parse("a", base, error));
  const FieldPath child = base.appended(PathSegment{"b", 3});
  EXPECT_EQ(child.to_string(), "a.b[3]");
  EXPECT_EQ(base.to_string(), "a");
  EXPECT_NE(base, child);
  EXPECT_EQ(child, child.appended(PathSegment{}).segments.size() == 3 ? child : child);
}

TEST(ResolveFieldPath, NestedAndIndexed)
{
  const MessageDescriptionPtr scan = scan_type();
  ResolvedField resolved;
  std::string error;

  ASSERT_TRUE(resolve_field_path(*scan, "header.stamp.sec", resolved, error)) << error;
  EXPECT_EQ(resolved.field->kind, FieldKind::Int32);
  EXPECT_EQ(resolved.owner->type_name, "builtin_interfaces/msg/Time");
  EXPECT_FALSE(resolved.indexed);
  EXPECT_FALSE(resolved.is_array_value());

  ASSERT_TRUE(resolve_field_path(*scan, "ranges", resolved, error)) << error;
  EXPECT_TRUE(resolved.is_array_value());
  EXPECT_EQ(resolved.field->kind, FieldKind::Float32);

  ASSERT_TRUE(resolve_field_path(*scan, "ranges[1000]", resolved, error)) << error;  // unbounded
  EXPECT_TRUE(resolved.indexed);
  EXPECT_FALSE(resolved.is_array_value());

  ASSERT_TRUE(resolve_field_path(*scan, "points[3].y", resolved, error)) << error;
  EXPECT_EQ(resolved.field->name, "y");
  EXPECT_EQ(resolved.owner->type_name, "geometry_msgs/msg/Point");

  ASSERT_TRUE(resolve_field_path(*scan, "covariance[3]", resolved, error)) << error;
  ASSERT_TRUE(resolve_field_path(*scan, "names[1]", resolved, error)) << error;
}

TEST(ResolveFieldPath, Errors)
{
  const MessageDescriptionPtr scan = scan_type();
  ResolvedField resolved;
  std::string error;

  EXPECT_FALSE(resolve_field_path(*scan, "", resolved, error));
  EXPECT_FALSE(resolve_field_path(*scan, "nope", resolved, error));
  EXPECT_NE(error.find("no field 'nope'"), std::string::npos);
  EXPECT_FALSE(resolve_field_path(*scan, "header.nope", resolved, error));
  EXPECT_FALSE(resolve_field_path(*scan, "angle_min[0]", resolved, error));
  EXPECT_NE(error.find("not an array"), std::string::npos);
  EXPECT_FALSE(resolve_field_path(*scan, "points.x", resolved, error));
  EXPECT_NE(error.find("index it with [i]"), std::string::npos);
  EXPECT_FALSE(resolve_field_path(*scan, "covariance[4]", resolved, error));
  EXPECT_NE(error.find("out of range"), std::string::npos);
  EXPECT_FALSE(resolve_field_path(*scan, "names[2]", resolved, error));
  EXPECT_FALSE(resolve_field_path(*scan, "angle_min.x", resolved, error));
  EXPECT_NE(error.find("not a message"), std::string::npos);
  EXPECT_FALSE(resolve_field_path(*scan, "header..stamp", resolved, error));  // syntax
}

// ---- traversal --------------------------------------------------------------

TEST(WalkFields, DepthFirstDeclarationOrderWithoutDescendingIntoArrays)
{
  const std::vector<std::string> paths = walk_paths(*scan_type());
  const std::vector<std::string> expected = {
    "header", "header.stamp", "header.stamp.sec", "header.stamp.nanosec", "header.frame_id",
    "angle_min", "ranges", "covariance", "names", "short_name", "points", "status", "tags"};
  EXPECT_EQ(paths, expected);
}

TEST(WalkFields, VisitorReceivesDepthAndCanSkipOrStop)
{
  std::vector<std::size_t> depths;
  walk_fields(*pose_type(), [&](const FieldPath &, const FieldDescription &, std::size_t depth) {
    depths.push_back(depth);
    return WalkControl::Continue;
  });
  EXPECT_EQ(depths, (std::vector<std::size_t>{0, 1, 1, 1, 0, 1, 1, 1, 1}));

  std::vector<std::string> skipped;
  walk_fields(*pose_type(), [&](const FieldPath & path, const FieldDescription &, std::size_t) {
    skipped.push_back(path.to_string());
    return path.to_string() == "position" ? WalkControl::SkipChildren : WalkControl::Continue;
  });
  EXPECT_EQ(skipped, (std::vector<std::string>{
    "position", "orientation", "orientation.x", "orientation.y", "orientation.z", "orientation.w"}));

  std::vector<std::string> stopped;
  const bool completed = walk_fields(*pose_type(),
    [&](const FieldPath & path, const FieldDescription &, std::size_t) {
      stopped.push_back(path.to_string());
      return path.to_string() == "position.y" ? WalkControl::Stop : WalkControl::Continue;
    });
  EXPECT_FALSE(completed);
  EXPECT_EQ(stopped, (std::vector<std::string>{"position", "position.x", "position.y"}));
}

// ---- values -----------------------------------------------------------------

TEST(ValuePaths, GetPath)
{
  UiValue doc = UiValue::object()
    .set("pose", UiValue::object().set("position", UiValue::object().set("x", 1.5)))
    .set("ranges", UiValue::array().push_back(0.1).push_back(0.2))
    .set("points", UiValue::array().push_back(UiValue::object().set("x", 9)));
  FieldPath path;
  std::string error;

  ASSERT_TRUE(FieldPath::parse("pose.position.x", path, error));
  ASSERT_NE(get_path(doc, path), nullptr);
  EXPECT_DOUBLE_EQ(get_path(doc, path)->as_double(), 1.5);
  ASSERT_TRUE(FieldPath::parse("ranges[1]", path, error));
  EXPECT_DOUBLE_EQ(get_path(doc, path)->as_double(), 0.2);
  ASSERT_TRUE(FieldPath::parse("points[0].x", path, error));
  EXPECT_EQ(get_path(doc, path)->as_int(), 9);
  ASSERT_TRUE(FieldPath::parse("", path, error));
  EXPECT_EQ(get_path(doc, path), &doc);

  ASSERT_TRUE(FieldPath::parse("ranges[2]", path, error));
  EXPECT_EQ(get_path(doc, path), nullptr);
  ASSERT_TRUE(FieldPath::parse("pose.nope", path, error));
  EXPECT_EQ(get_path(doc, path), nullptr);
  ASSERT_TRUE(FieldPath::parse("pose[0]", path, error));  // index into an object
  EXPECT_EQ(get_path(doc, path), nullptr);
  ASSERT_TRUE(FieldPath::parse("ranges.x", path, error));  // name into an array
  EXPECT_EQ(get_path(doc, path), nullptr);

  const UiValue & const_doc = doc;
  ASSERT_TRUE(FieldPath::parse("pose.position.x", path, error));
  EXPECT_EQ(get_path(const_doc, path), get_path(doc, path));
}

TEST(ValuePaths, SetPathCreatesIntermediatesAndGrowsArrays)
{
  UiValue doc;
  FieldPath path;
  std::string error;

  ASSERT_TRUE(FieldPath::parse("pose.position.x", path, error));
  ASSERT_TRUE(set_path(doc, path, 2.5, error)) << error;
  EXPECT_DOUBLE_EQ(get_path(doc, path)->as_double(), 2.5);
  EXPECT_TRUE(doc.is_object());

  ASSERT_TRUE(FieldPath::parse("ranges[2]", path, error));
  ASSERT_TRUE(set_path(doc, path, 0.7, error)) << error;
  ASSERT_EQ(doc.find("ranges")->size(), 3u);
  EXPECT_TRUE(doc.find("ranges")->at(0)->is_null());
  EXPECT_TRUE(doc.find("ranges")->at(1)->is_null());
  EXPECT_DOUBLE_EQ(doc.find("ranges")->at(2)->as_double(), 0.7);

  ASSERT_TRUE(FieldPath::parse("points[1].y", path, error));
  ASSERT_TRUE(set_path(doc, path, 4, error)) << error;
  EXPECT_TRUE(doc.find("points")->at(0)->is_null());
  EXPECT_EQ(doc.find("points")->at(1)->find("y")->as_int(), 4);

  // Replace an existing leaf, keeping order.
  ASSERT_TRUE(FieldPath::parse("pose.position.x", path, error));
  ASSERT_TRUE(set_path(doc, path, "now a string", error));
  EXPECT_EQ(get_path(doc, path)->as_string(), "now a string");
  EXPECT_EQ(doc.entries()[0].key, "pose");

  // Empty path replaces the root.
  ASSERT_TRUE(FieldPath::parse("", path, error));
  ASSERT_TRUE(set_path(doc, path, UiValue::object().set("fresh", true), error));
  EXPECT_EQ(doc.size(), 1u);
  EXPECT_TRUE(doc.find("fresh")->as_bool());
}

TEST(ValuePaths, SetPathRefusesKindClashesAndLeavesTheDocumentUntouched)
{
  UiValue doc = UiValue::object().set("scalar", 1).set("list", UiValue::array().push_back(1));
  const UiValue before = doc;
  FieldPath path;
  std::string error;

  ASSERT_TRUE(FieldPath::parse("scalar.x", path, error));
  EXPECT_FALSE(set_path(doc, path, 1, error));
  EXPECT_NE(error.find("not an object"), std::string::npos);
  ASSERT_TRUE(FieldPath::parse("scalar[0]", path, error));
  EXPECT_FALSE(set_path(doc, path, 1, error));
  EXPECT_NE(error.find("not an array"), std::string::npos);
  ASSERT_TRUE(FieldPath::parse("list.x", path, error));
  EXPECT_FALSE(set_path(doc, path, 1, error));
  EXPECT_EQ(doc, before);

  UiValue scalar_root(5);
  ASSERT_TRUE(FieldPath::parse("a", path, error));
  EXPECT_FALSE(set_path(scalar_root, path, 1, error));
  EXPECT_EQ(scalar_root.as_int(), 5);
}

TEST(DefaultValues, MessageDefaultsHonourIdlDefaultsAndArrayKinds)
{
  const UiValue pose = make_default_value(*pose_type());
  EXPECT_DOUBLE_EQ(pose.find("position")->find("x")->as_double(), 0.0);
  EXPECT_TRUE(pose.find("position")->find("x")->is_double());
  EXPECT_DOUBLE_EQ(pose.find("orientation")->find("w")->as_double(), 1.0);

  const UiValue scan = make_default_value(*scan_type());
  EXPECT_EQ(scan.find("header")->find("frame_id")->as_string(), "");
  EXPECT_TRUE(scan.find("header")->find("stamp")->find("sec")->is_int());
  EXPECT_TRUE(scan.find("angle_min")->is_double());
  EXPECT_TRUE(scan.find("ranges")->is_array());
  EXPECT_EQ(scan.find("ranges")->size(), 0u);          // unbounded: empty
  EXPECT_EQ(scan.find("covariance")->size(), 4u);      // fixed: filled
  EXPECT_DOUBLE_EQ(scan.find("covariance")->at(3)->as_double(), 0.0);
  EXPECT_EQ(scan.find("names")->size(), 0u);           // bounded: empty
  EXPECT_EQ(scan.find("points")->size(), 0u);
  EXPECT_EQ(scan.find("status")->as_int(), 1);         // IDL default
  ASSERT_EQ(scan.find("tags")->size(), 2u);            // array IDL default
  EXPECT_EQ(scan.find("tags")->at(1)->as_int(), -7);
  // Field order is declaration order.
  EXPECT_EQ(scan.entries()[0].key, "header");
  EXPECT_EQ(scan.entries()[8].key, "tags");
  EXPECT_TRUE(validate_value(*scan_type(), scan).empty());
}

TEST(DefaultValues, FixedArrayOfMessages)
{
  const FieldDescription f = FieldDescription::nested("corners", point_type()).fixed_array(2);
  const UiValue v = make_default_value(f);
  ASSERT_EQ(v.size(), 2u);
  EXPECT_TRUE(v.at(1)->find("z")->is_double());
}

TEST(ValidateValue, AcceptsAWellFormedMessage)
{
  UiValue value = make_default_value(*scan_type());
  value.find("ranges")->push_back(1.5f).push_back(2);       // float32 accepts an integral Int
  value.find("names")->push_back("a").push_back("b");
  value.find("short_name")->as_string();
  *value.find("short_name") = UiValue("12345678");           // exactly at the bound
  *value.find("status") = UiValue(255.0);                    // integral double is fine
  value.find("points")->push_back(UiValue::object().set("x", 1).set("y", 2.0).set("z", 3));
  EXPECT_TRUE(validate_value(*scan_type(), value).empty());
}

TEST(ValidateValue, ReportsEveryKindOfMismatch)
{
  UiValue value = make_default_value(*scan_type());
  *value.find("angle_min") = UiValue("not a number");
  *value.find("header")->find("frame_id") = UiValue(3);
  *value.find("header")->find("stamp")->find("nanosec") = UiValue(-1);
  *value.find("status") = UiValue(256);
  *value.find("covariance") = UiValue::array().push_back(1.0);
  value.find("names")->push_back("a").push_back("b").push_back("c");
  *value.find("short_name") = UiValue("123456789");
  *value.find("ranges") = UiValue(1.0);
  *value.find("tags") = UiValue::array().push_back(1.5).push_back(200);
  *value.find("points") = UiValue::array().push_back(UiValue::object().set("x", "no"))
    .push_back(UiValue(7));
  value.set("extra", 1);

  const std::vector<ValueProblem> problems = validate_value(*scan_type(), value);
  const auto has = [&](const std::string & path, const std::string & fragment) {
    for (const ValueProblem & p : problems) {
      if (p.path == path && p.message.find(fragment) != std::string::npos) {
        return true;
      }
    }
    return false;
  };
  EXPECT_TRUE(has("angle_min", "expected float32, got string"));
  EXPECT_TRUE(has("header.frame_id", "expected string, got int"));
  EXPECT_TRUE(has("header.stamp.nanosec", "out of range"));
  EXPECT_TRUE(has("status", "out of range"));
  EXPECT_TRUE(has("covariance", "fixed array of size 4 has 1"));
  EXPECT_TRUE(has("names", "bounded array (<=2) has 3"));
  EXPECT_TRUE(has("short_name", "longer than its bound"));
  EXPECT_TRUE(has("ranges", "expected an array"));
  EXPECT_TRUE(has("tags[0]", "non-integral"));
  EXPECT_TRUE(has("tags[1]", "out of range"));
  EXPECT_TRUE(has("points[0].x", "expected float64, got string"));
  EXPECT_TRUE(has("points[1]", "expected an object"));
  EXPECT_TRUE(has("extra", "unknown field"));
}

TEST(ValidateValue, OptionsControlMissingAndUnknownFields)
{
  const UiValue partial = UiValue::object().set("angle_min", 0.5).set("extra", 1);
  ValidateValueOptions lenient;
  lenient.reject_unknown_fields = false;
  EXPECT_TRUE(validate_value(*scan_type(), partial, lenient).empty());

  ValidateValueOptions strict;
  strict.require_all_fields = true;
  const std::vector<ValueProblem> problems = validate_value(*scan_type(), partial, strict);
  bool missing_header = false;
  for (const ValueProblem & p : problems) {
    if (p.path == "header" && p.message == "missing field") {
      missing_header = true;
    }
  }
  EXPECT_TRUE(missing_header);
  EXPECT_FALSE(validate_value(*scan_type(), UiValue(5)).empty());
}

TEST(ValidateValue, Int64AndUInt64Extremes)
{
  const MessageDescriptionPtr wide = MessageDescriptionBuilder("test/msg/Wide")
    .field(FieldDescription::primitive("s", FieldKind::Int64))
    .field(FieldDescription::primitive("u", FieldKind::UInt64))
    .build();
  UiValue ok = UiValue::object()
    .set("s", std::int64_t{-9223372036854775807})
    .set("u", std::int64_t{9223372036854775807});
  EXPECT_TRUE(validate_value(*wide, ok).empty());
  UiValue negative_unsigned = UiValue::object().set("u", -1);
  EXPECT_EQ(validate_value(*wide, negative_unsigned).size(), 1u);
  // The largest double below 2^64 is a valid uint64; 2^64 itself is not.
  UiValue huge_unsigned = UiValue::object().set("u", 18446744073709549568.0);
  EXPECT_TRUE(validate_value(*wide, huge_unsigned).empty());
  UiValue two_to_64 = UiValue::object().set("u", 18446744073709551616.0);
  EXPECT_EQ(validate_value(*wide, two_to_64).size(), 1u);
  UiValue two_to_63_signed = UiValue::object().set("s", 9223372036854775808.0);
  EXPECT_EQ(validate_value(*wide, two_to_63_signed).size(), 1u);
}

TEST(ServiceAndAction, HoldTheirHalves)
{
  ServiceDescription service;
  service.type_name = "std_srvs/srv/SetBool";
  service.request = MessageDescriptionBuilder("std_srvs/srv/SetBool_Request")
    .field(FieldDescription::primitive("data", FieldKind::Bool)).build();
  service.response = MessageDescriptionBuilder("std_srvs/srv/SetBool_Response")
    .field(FieldDescription::primitive("success", FieldKind::Bool))
    .field(FieldDescription::primitive("message", FieldKind::String)).build();
  EXPECT_EQ(service.request->fields.size(), 1u);
  EXPECT_EQ(service.response->fields.size(), 2u);

  ActionDescription action;
  action.type_name = "nav2_msgs/action/NavigateToPose";
  action.goal = MessageDescriptionBuilder("nav2_msgs/action/NavigateToPose_Goal")
    .field(FieldDescription::nested("pose", pose_type())).build();
  action.feedback = MessageDescriptionBuilder("nav2_msgs/action/NavigateToPose_Feedback")
    .field(FieldDescription::primitive("distance_remaining", FieldKind::Float32).with_unit("m"))
    .build();
  action.result = MessageDescriptionBuilder("nav2_msgs/action/NavigateToPose_Result").build();
  ResolvedField resolved;
  std::string error;
  ASSERT_TRUE(resolve_field_path(*action.goal, "pose.orientation.w", resolved, error)) << error;
  EXPECT_DOUBLE_EQ(resolved.field->default_value.as_double(), 1.0);
  EXPECT_TRUE(action.result->fields.empty());
}
