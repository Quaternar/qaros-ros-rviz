// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The widget model is a fat struct kept honest by validate(): every rule a
// generated UI can break is pinned here, with and without a schema, plus the
// JSON shape the page runtime reads.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "qar_ui_model/json.hpp"
#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_model.hpp"

using namespace qar_ui_model;  // NOLINT(build/namespaces): test readability

namespace
{

MessageDescriptionPtr
twist_type()
{
  auto vector3 = MessageDescriptionBuilder("geometry_msgs/msg/Vector3")
    .field(FieldDescription::primitive("x", FieldKind::Float64))
    .field(FieldDescription::primitive("y", FieldKind::Float64))
    .field(FieldDescription::primitive("z", FieldKind::Float64))
    .build();
  return MessageDescriptionBuilder("geometry_msgs/msg/Twist")
         .field(FieldDescription::nested("linear", vector3))
         .field(FieldDescription::nested("angular", vector3))
         .build();
}

// A composite root: what a panel aggregating several topics binds against.
MessageDescriptionPtr
robot_root()
{
  auto quaternion = MessageDescriptionBuilder("geometry_msgs/msg/Quaternion")
    .field(FieldDescription::primitive("x", FieldKind::Float64))
    .field(FieldDescription::primitive("y", FieldKind::Float64))
    .field(FieldDescription::primitive("z", FieldKind::Float64))
    .field(FieldDescription::primitive("w", FieldKind::Float64))
    .build();
  auto battery = MessageDescriptionBuilder("sensor_msgs/msg/BatteryState")
    .field(FieldDescription::primitive("percentage", FieldKind::Float32))
    .field(FieldDescription::primitive("present", FieldKind::Bool))
    .field(FieldDescription::primitive("location", FieldKind::String))
    .field(FieldDescription::primitive("power_supply_status", FieldKind::UInt8))
    .field(FieldDescription::primitive("cell_voltage", FieldKind::Float32).unbounded_array())
    .build();
  auto status = MessageDescriptionBuilder("diagnostic_msgs/msg/DiagnosticStatus")
    .field(FieldDescription::primitive("level", FieldKind::Byte))
    .field(FieldDescription::primitive("name", FieldKind::String))
    .field(FieldDescription::primitive("message", FieldKind::String))
    .build();
  return MessageDescriptionBuilder("qar/panel/RobotRoot")
         .field(FieldDescription::nested("cmd_vel", twist_type()))
         .field(FieldDescription::nested("battery", battery))
         .field(FieldDescription::nested("orientation", quaternion))
         .field(FieldDescription::nested("diagnostics", status).unbounded_array())
         .field(FieldDescription::primitive("image_url", FieldKind::String))
         .build();
}

UiModel
robot_model()
{
  UiModel model;
  model.root = widgets::panel("robot", "Robot").with_description("/robot namespace")
    .add(widgets::section("drive", "Drive")
      .add(widgets::row("drive_row")
        .add(widgets::slider("speed", "cmd_vel.linear.x", "Speed", -1.0, 1.0)
          .with_step(0.1).with_unit("m/s").with_action("publish:/cmd_vel"))
        .add(widgets::number("turn", "cmd_vel.angular.z", "Turn").with_range(-2.0, 2.0)
          .with_step(0.1).with_action("publish:/cmd_vel")))
      .add(widgets::vector3("lin", "cmd_vel.linear", "Linear").with_editable(false))
      .add(widgets::button("stop", "STOP", "call:/emergency_stop").with_confirm()))
    .add(widgets::section("power", "Power")
      .add(widgets::gauge("soc", "battery.percentage", "Charge", 0.0, 1.0)
        .with_rule(StatusRule{UiValue(), std::nullopt, 0.2, StatusLevel::Error, "Low"})
        .with_rule(StatusRule{UiValue(), 0.2, std::nullopt, StatusLevel::Ok, ""}))
      .add(widgets::toggle("present", "battery.present", "Present").with_editable(false))
      .add(widgets::choice("psu", "battery.power_supply_status", "Supply",
        {{"Unknown", 0}, {"Charging", 1}, {"Discharging", 2}}))
      .add(widgets::text_input("loc", "battery.location", "Location").with_action("set"))
      .add(widgets::time_series("cells", "battery.cell_voltage[0]", "Cell 0"))
      .add(widgets::status_light("diag", "diagnostics[0].level", "Diagnostics")
        .with_rule(StatusRule{UiValue(0), std::nullopt, std::nullopt, StatusLevel::Ok, "OK"})
        .with_rule(StatusRule{UiValue(2), std::nullopt, std::nullopt, StatusLevel::Error, "ERROR"})))
    .add(widgets::section("pose", "Pose")
      .add(widgets::quaternion("q", "orientation", "Orientation"))
      .add(widgets::table("diags", "diagnostics", "All diagnostics",
        {{"Level", "level", "", 0}, {"Name", "name", "", std::nullopt}}))
      .add(widgets::image("cam", "image_url", "Camera"))
      .add(widgets::value("named", "diagnostics[1].message", "Second message"))
      .add(widgets::label("note", "Values update live"))
      .add(widgets::json_inspector("raw")));
  model.metadata = UiValue::object().set("generator", "test");
  return model;
}

std::vector<std::string>
messages(const std::vector<UiProblem> & problems, ProblemSeverity severity)
{
  std::vector<std::string> out;
  for (const UiProblem & p : problems) {
    if (p.severity == severity) {
      out.push_back(p.widget_id + ": " + p.message);
    }
  }
  return out;
}

bool
has_error(const std::vector<UiProblem> & problems, const std::string & id, const std::string & fragment)
{
  for (const UiProblem & p : problems) {
    if (p.severity == ProblemSeverity::Error && p.widget_id == id &&
      p.message.find(fragment) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

bool
has_warning(const std::vector<UiProblem> & problems, const std::string & id, const std::string & fragment)
{
  for (const UiProblem & p : problems) {
    if (p.severity == ProblemSeverity::Warning && p.widget_id == id &&
      p.message.find(fragment) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(WidgetType, NamesRoundTrip)
{
  const WidgetType all[] = {
    WidgetType::Panel, WidgetType::Section, WidgetType::Row, WidgetType::Column, WidgetType::Label,
    WidgetType::ValueDisplay, WidgetType::NumberInput, WidgetType::TextInput, WidgetType::Toggle,
    WidgetType::Slider, WidgetType::Choice, WidgetType::Button, WidgetType::Vector3,
    WidgetType::Quaternion, WidgetType::Table, WidgetType::Gauge, WidgetType::StatusLight,
    WidgetType::TimeSeries, WidgetType::Image, WidgetType::JsonInspector};
  for (WidgetType type : all) {
    WidgetType back = WidgetType::Label;
    ASSERT_TRUE(widget_type_from_string(to_string(type), back)) << to_string(type);
    EXPECT_EQ(back, type);
  }
  WidgetType none = WidgetType::Label;
  EXPECT_FALSE(widget_type_from_string("dropdown", none));
  EXPECT_STREQ(to_string(WidgetType::StatusLight), "status");
  EXPECT_STREQ(to_string(WidgetType::TimeSeries), "chart");
  EXPECT_STREQ(to_string(WidgetType::JsonInspector), "json");
}

TEST(WidgetType, Predicates)
{
  EXPECT_TRUE(is_container(WidgetType::Panel));
  EXPECT_TRUE(is_container(WidgetType::Row));
  EXPECT_FALSE(is_container(WidgetType::Table));
  EXPECT_TRUE(is_input(WidgetType::Slider));
  EXPECT_TRUE(is_input(WidgetType::Quaternion));
  EXPECT_FALSE(is_input(WidgetType::Button));
  EXPECT_FALSE(is_input(WidgetType::Gauge));
  EXPECT_TRUE(is_bindable(WidgetType::Button));
  EXPECT_TRUE(is_bindable(WidgetType::JsonInspector));
  EXPECT_FALSE(is_bindable(WidgetType::Label));
  EXPECT_FALSE(is_bindable(WidgetType::Section));
}

TEST(Widget, FactoriesSetEditableForInputsOnly)
{
  EXPECT_TRUE(widgets::number("n", "a").editable);
  EXPECT_TRUE(widgets::toggle("t", "a").editable);
  EXPECT_TRUE(widgets::vector3("v", "a").editable);
  EXPECT_FALSE(widgets::value("d", "a").editable);
  EXPECT_FALSE(widgets::button("b", "Go", "act").editable);
  EXPECT_FALSE(widgets::gauge("g", "a", "G", 0, 1).editable);
  EXPECT_EQ(widgets::slider("s", "a", "S", 0, 10).min, 0.0);
  EXPECT_EQ(widgets::slider("s", "a", "S", 0, 10).max, 10.0);
  EXPECT_EQ(widgets::button("b", "Go", "act").action, "act");
  EXPECT_EQ(widgets::choice("c", "a", "C", {{"x", 1}}).choices.size(), 1u);
  EXPECT_EQ(widgets::table("t", "a", "T", {{"H", "p", "", std::nullopt}}).columns.size(), 1u);
}

TEST(Widget, DisplayLabelFallbacks)
{
  EXPECT_EQ(widgets::value("id", "pose.linear_velocity").display_label(), "Linear velocity");
  EXPECT_EQ(widgets::value("id", "ranges[2]").display_label(), "Ranges");
  EXPECT_EQ(widgets::value("some_id", "").display_label(), "some_id");
  EXPECT_EQ(widgets::value("id", "x", "Given").display_label(), "Given");
}

TEST(WidgetIds, Rules)
{
  EXPECT_TRUE(is_valid_widget_id("a"));
  EXPECT_TRUE(is_valid_widget_id("_x.y-z9"));
  EXPECT_FALSE(is_valid_widget_id(""));
  EXPECT_FALSE(is_valid_widget_id("9a"));
  EXPECT_FALSE(is_valid_widget_id("a b"));
  EXPECT_FALSE(is_valid_widget_id("a\"b"));
  EXPECT_FALSE(is_valid_widget_id("a<b"));
  EXPECT_FALSE(is_valid_widget_id("a/b"));
  EXPECT_FALSE(is_valid_widget_id(std::string(129, 'a')));
  EXPECT_TRUE(is_valid_widget_id(std::string(128, 'a')));
}

TEST(UiModel, FindAndWalk)
{
  UiModel model = robot_model();
  ASSERT_NE(model.find("speed"), nullptr);
  EXPECT_EQ(model.find("speed")->type, WidgetType::Slider);
  EXPECT_EQ(model.find("nope"), nullptr);
  model.find("speed")->label = "Velocity";
  EXPECT_EQ(static_cast<const UiModel &>(model).find("speed")->label, "Velocity");

  std::vector<std::string> order;
  std::size_t max_depth = 0;
  walk_widgets(model.root, [&](const Widget & w, const Widget * parent, std::size_t depth) {
    order.push_back(w.id);
    max_depth = depth > max_depth ? depth : max_depth;
    if (w.id == "robot") {
      EXPECT_EQ(parent, nullptr);
    } else {
      EXPECT_NE(parent, nullptr);
    }
    return WalkControl::Continue;
  });
  ASSERT_GE(order.size(), 5u);
  EXPECT_EQ(order[0], "robot");
  EXPECT_EQ(order[1], "drive");
  EXPECT_EQ(order[2], "drive_row");
  EXPECT_EQ(order[3], "speed");
  EXPECT_EQ(max_depth, 3u);

  std::vector<std::string> skipped;
  walk_widgets(model.root, [&](const Widget & w, const Widget *, std::size_t) {
    skipped.push_back(w.id);
    return w.id == "drive" ? WalkControl::SkipChildren : WalkControl::Continue;
  });
  EXPECT_EQ(skipped[1], "drive");
  EXPECT_EQ(skipped[2], "power");
}

TEST(Validate, TheReferenceModelIsValidWithAndWithoutSchema)
{
  const UiModel model = robot_model();
  const std::vector<UiProblem> without = validate(model);
  EXPECT_TRUE(is_valid(without)) << messages(without, ProblemSeverity::Error).size();
  const MessageDescriptionPtr schema = robot_root();
  const std::vector<UiProblem> with = validate(model, schema.get());
  for (const std::string & m : messages(with, ProblemSeverity::Error)) {
    ADD_FAILURE() << m;
  }
  EXPECT_TRUE(is_valid(with));
  // The toggle is read-only, so no "editable without action" warning for it; the
  // number input has an action. Only the editable quaternion lacks an action.
  EXPECT_TRUE(has_warning(with, "q", "no action"));
  EXPECT_FALSE(has_warning(with, "present", "no action"));
}

TEST(Validate, DuplicateAndInvalidIds)
{
  UiModel model;
  model.root = widgets::panel("p", "P")
    .add(widgets::value("dup", "a"))
    .add(widgets::value("dup", "b"))
    .add(widgets::value("bad id", "c"))
    .add(widgets::value("", "d"));
  const std::vector<UiProblem> problems = validate(model);
  EXPECT_FALSE(is_valid(problems));
  EXPECT_TRUE(has_error(problems, "dup", "duplicate widget id"));
  EXPECT_TRUE(has_error(problems, "bad id", "invalid widget id"));
  EXPECT_TRUE(has_error(problems, "", "invalid widget id"));
}

TEST(Validate, StructuralRules)
{
  UiModel model;
  model.root = widgets::section("s", "not a panel")
    .add(widgets::panel("inner", "nested panel"))
    .add(widgets::value("leaf", "a").add(widgets::label("child", "x")))
    .add(widgets::row("bound_row").with_binding("a"))
    .add(widgets::value("edit", "a").with_editable(true))
    .add(widgets::label("empty", ""))
    .add(widgets::value("range", "a").with_range(2.0, 1.0).with_step(0.0).with_precision(20))
    .add(widgets::value("confirm", "a").with_confirm());
  model.metadata = UiValue(5);
  model.initial_data = UiValue::array();
  const std::vector<UiProblem> problems = validate(model);
  EXPECT_TRUE(has_error(problems, "s", "root widget must be a panel"));
  EXPECT_TRUE(has_error(problems, "inner", "only be the root"));
  EXPECT_TRUE(has_error(problems, "leaf", "cannot have children"));
  EXPECT_TRUE(has_error(problems, "bound_row", "cannot have a binding"));
  EXPECT_TRUE(has_error(problems, "edit", "cannot be editable"));
  EXPECT_TRUE(has_warning(problems, "empty", "no text"));
  EXPECT_TRUE(has_error(problems, "range", "min is greater than max"));
  EXPECT_TRUE(has_error(problems, "range", "step must be positive"));
  EXPECT_TRUE(has_error(problems, "range", "precision"));
  EXPECT_TRUE(has_warning(problems, "confirm", "only honoured by buttons"));
  EXPECT_TRUE(has_error(problems, "", "metadata must be an object"));
  EXPECT_TRUE(has_error(problems, "", "initial_data must be an object"));
}

TEST(Validate, BindingRules)
{
  UiModel model;
  model.root = widgets::panel("p", "P")
    .add(widgets::value("nobind", ""))
    .add(widgets::value("badbind", "a..b"))
    .add(widgets::button("nobind_button", "Go", "act"))
    .add(widgets::json_inspector("root_json"))
    .add(widgets::toggle("ok", "flag"));
  const std::vector<UiProblem> problems = validate(model);
  EXPECT_TRUE(has_error(problems, "nobind", "requires a binding"));
  EXPECT_TRUE(has_error(problems, "badbind", "invalid binding"));
  EXPECT_FALSE(has_error(problems, "nobind_button", "binding"));
  EXPECT_FALSE(has_error(problems, "root_json", "binding"));
  EXPECT_TRUE(has_warning(problems, "ok", "no action"));
}

TEST(Validate, TypeSpecificRules)
{
  UiModel model;
  Widget slider = widgets::number("slider", "a");
  slider.type = WidgetType::Slider;  // a slider with no range
  Widget gauge = widgets::value("gauge", "a");
  gauge.type = WidgetType::Gauge;
  Widget v3 = widgets::vector3("v3", "a");
  v3.components = {"x", "y"};
  Widget v3bad = widgets::vector3("v3bad", "a");
  v3bad.components = {"x", "y.z", "w"};
  Widget chart = widgets::time_series("chart", "a");
  chart.window_seconds = 0.0;
  chart.max_points = 1;
  model.root = widgets::panel("p", "P")
    .add(widgets::button("nobutton", "Go", ""))
    .add(slider)
    .add(gauge)
    .add(widgets::choice("nochoice", "a", "C", {}))
    .add(widgets::choice("dupchoice", "a", "C", {{"A", 1}, {"", 1}, {"C", UiValue::array()}}))
    .add(widgets::table("nocols", "a", "T", {}))
    .add(widgets::table("badcol", "a", "T", {{"H", "x..y", "", 99}}))
    .add(v3)
    .add(v3bad)
    .add(chart)
    .add(widgets::status_light("rules", "a")
      .with_rule(StatusRule{UiValue(), std::nullopt, std::nullopt, StatusLevel::Ok, ""})
      .with_rule(StatusRule{UiValue(), 5.0, 1.0, StatusLevel::Ok, ""}));
  const std::vector<UiProblem> problems = validate(model);
  EXPECT_TRUE(has_error(problems, "nobutton", "requires an action"));
  EXPECT_TRUE(has_error(problems, "slider", "requires min and max"));
  EXPECT_TRUE(has_error(problems, "gauge", "requires min and max"));
  EXPECT_TRUE(has_error(problems, "nochoice", "at least one option"));
  EXPECT_TRUE(has_warning(problems, "dupchoice", "has no label"));
  EXPECT_TRUE(has_error(problems, "dupchoice", "same value"));
  EXPECT_TRUE(has_error(problems, "dupchoice", "must be a scalar"));
  EXPECT_TRUE(has_error(problems, "nocols", "at least one column"));
  EXPECT_TRUE(has_error(problems, "badcol", "column 0"));
  EXPECT_TRUE(has_error(problems, "badcol", "precision"));
  EXPECT_TRUE(has_error(problems, "v3", "exactly three"));
  EXPECT_TRUE(has_error(problems, "v3bad", "not a field name"));
  EXPECT_TRUE(has_error(problems, "chart", "window_seconds"));
  EXPECT_TRUE(has_error(problems, "chart", "max_points"));
  EXPECT_TRUE(has_warning(problems, "rules", "matches everything"));
  EXPECT_TRUE(has_error(problems, "rules", "rule 1"));
}

TEST(Validate, SchemaAwareBindingChecks)
{
  const MessageDescriptionPtr schema = robot_root();
  UiModel model;
  Widget int_on_float = widgets::number("intfloat", "cmd_vel.linear.x").with_integer().with_range(-1e12, 1e12);
  model.root = widgets::panel("p", "P")
    .add(widgets::value("missing", "battery.voltage"))
    .add(widgets::toggle("toggle_num", "battery.percentage"))
    .add(widgets::slider("slider_str", "battery.location", "S", 0, 1))
    .add(widgets::text_input("text_num", "battery.percentage"))
    .add(widgets::image("img_num", "battery.percentage"))
    .add(widgets::value("value_msg", "cmd_vel"))
    .add(widgets::value("value_arr", "battery.cell_voltage"))
    .add(widgets::vector3("v3_scalar", "battery.percentage"))
    .add(widgets::vector3("v3_partial", "battery"))
    .add(widgets::quaternion("q_v3", "cmd_vel.linear"))
    .add(widgets::table("table_scalar", "battery.percentage", "T", {{"H", "", "", std::nullopt}}))
    .add(widgets::table("table_badcol", "diagnostics", "T", {{"H", "nope", "", std::nullopt}, {"H2", "", "", std::nullopt}}))
    .add(widgets::table("table_scalar_rows", "battery.cell_voltage", "T", {{"H", "x", "", std::nullopt}}))
    .add(widgets::table("table_ok", "battery.cell_voltage", "T", {{"V", "", "V", 2}}))
    .add(widgets::gauge("gauge_str", "battery.location", "G", 0, 1))
    .add(widgets::status_light("status_msg", "cmd_vel"))
    .add(widgets::number("num_ok", "battery.power_supply_status").with_range(-5, 300))
    .add(int_on_float);
  const std::vector<UiProblem> problems = validate(model, schema.get());
  EXPECT_TRUE(has_error(problems, "missing", "does not resolve"));
  EXPECT_TRUE(has_error(problems, "toggle_num", "must bind a bool"));
  EXPECT_TRUE(has_error(problems, "slider_str", "must bind a number"));
  EXPECT_TRUE(has_error(problems, "text_num", "must bind a string"));
  EXPECT_TRUE(has_error(problems, "img_num", "string URL"));
  EXPECT_TRUE(has_error(problems, "value_msg", "must bind a scalar"));
  EXPECT_TRUE(has_error(problems, "value_arr", "must bind a scalar"));
  EXPECT_TRUE(has_error(problems, "v3_scalar", "must bind a message"));
  EXPECT_TRUE(has_error(problems, "v3_partial", "numeric field 'x'"));
  EXPECT_TRUE(has_error(problems, "q_v3", "numeric field 'w'"));
  EXPECT_TRUE(has_error(problems, "table_scalar", "must bind an array"));
  EXPECT_TRUE(has_error(problems, "table_badcol", "column 0"));
  EXPECT_TRUE(has_error(problems, "table_badcol", "column 1"));
  EXPECT_TRUE(has_error(problems, "table_scalar_rows", "use an empty path"));
  EXPECT_FALSE(has_error(problems, "table_ok", ""));
  EXPECT_TRUE(has_error(problems, "gauge_str", "must bind a number"));
  EXPECT_TRUE(has_error(problems, "status_msg", "must bind a scalar"));
  EXPECT_TRUE(has_warning(problems, "num_ok", "exceeds what uint8"));
  EXPECT_TRUE(has_warning(problems, "intfloat", "integer semantics"));
}

TEST(Serialization, WidgetRoundTripsThroughValueAndJson)
{
  const UiModel model = robot_model();
  const UiValue value = to_value(model);
  EXPECT_EQ(value.find("version")->as_int(), UiModel::kModelVersion);
  EXPECT_EQ(value.find("root")->find("type")->as_string(), "panel");
  EXPECT_EQ(value.find("metadata")->find("generator")->as_string(), "test");
  EXPECT_EQ(value.find("data"), nullptr);  // initial_data is Null: omitted

  const std::string text = json_write(value);
  UiValue parsed;
  JsonParseError error;
  ASSERT_TRUE(json_parse(text, parsed, error)) << error.to_string();
  UiModel back;
  std::string from_error;
  ASSERT_TRUE(from_value(parsed, back, from_error)) << from_error;
  EXPECT_EQ(to_value(back), value);
  EXPECT_EQ(json_write(to_value(back)), text);

  const Widget * stop = back.find("stop");
  ASSERT_NE(stop, nullptr);
  EXPECT_TRUE(stop->confirm);
  EXPECT_EQ(stop->action, "call:/emergency_stop");
  const Widget * soc = back.find("soc");
  ASSERT_NE(soc, nullptr);
  ASSERT_EQ(soc->rules.size(), 2u);
  EXPECT_EQ(soc->rules[0].level, StatusLevel::Error);
  EXPECT_EQ(*soc->rules[0].max, 0.2);
  EXPECT_FALSE(soc->rules[0].min);
  const Widget * psu = back.find("psu");
  ASSERT_NE(psu, nullptr);
  ASSERT_EQ(psu->choices.size(), 3u);
  EXPECT_EQ(psu->choices[2].value.as_int(), 2);
  const Widget * diags = back.find("diags");
  ASSERT_NE(diags, nullptr);
  ASSERT_EQ(diags->columns.size(), 2u);
  EXPECT_EQ(*diags->columns[0].precision, 0);
  EXPECT_FALSE(diags->columns[1].precision);
  EXPECT_TRUE(is_valid(validate(back)));
}

TEST(Serialization, OnlyNonDefaultMembersAreWritten)
{
  const UiValue v = to_value(widgets::value("v", "a"));
  EXPECT_EQ(json_write(v), "{\"id\":\"v\",\"type\":\"value\",\"bind\":\"a\"}");
  const UiValue s = to_value(widgets::slider("s", "a", "S", 0.0, 1.0).with_step(0.25));
  EXPECT_EQ(json_write(s),
    "{\"id\":\"s\",\"type\":\"slider\",\"label\":\"S\",\"bind\":\"a\",\"min\":0.0,\"max\":1.0,"
    "\"step\":0.25,\"editable\":true}");
  Widget chart = widgets::time_series("c", "a");
  chart.window_seconds = 60.0;
  chart.max_points = 100;
  chart.quaternion_display = QuaternionDisplay::Euler;
  const UiValue c = to_value(chart);
  EXPECT_EQ(c.find("window_seconds")->as_double(), 60.0);
  EXPECT_EQ(c.find("max_points")->as_int(), 100);
  EXPECT_EQ(c.find("quaternion_display")->as_string(), "euler");
  EXPECT_EQ(c.find("children"), nullptr);
}

TEST(Serialization, FromValueRejectsMalformedInput)
{
  UiModel model;
  Widget widget;
  std::string error;
  UiValue parsed;
  JsonParseError json_error;

  EXPECT_FALSE(from_value(UiValue(5), model, error));
  EXPECT_FALSE(from_value(UiValue::object(), model, error));  // no version
  EXPECT_NE(error.find("version"), std::string::npos);
  EXPECT_FALSE(from_value(UiValue::object().set("version", 99).set("root", UiValue::object()), model, error));
  EXPECT_NE(error.find("unsupported model version"), std::string::npos);
  EXPECT_FALSE(from_value(UiValue::object().set("version", 1), model, error));
  EXPECT_NE(error.find("root"), std::string::npos);

  ASSERT_TRUE(json_parse("{\"type\":\"panel\"}", parsed, json_error));
  EXPECT_FALSE(from_value(parsed, widget, error));
  EXPECT_NE(error.find("id"), std::string::npos);
  ASSERT_TRUE(json_parse("{\"id\":\"x\",\"type\":\"dropdown\"}", parsed, json_error));
  EXPECT_FALSE(from_value(parsed, widget, error));
  EXPECT_NE(error.find("type"), std::string::npos);
  ASSERT_TRUE(json_parse("{\"id\":\"x\",\"type\":\"value\",\"min\":\"low\"}", parsed, json_error));
  EXPECT_FALSE(from_value(parsed, widget, error));
  ASSERT_TRUE(json_parse("{\"id\":\"x\",\"type\":\"value\",\"choices\":5}", parsed, json_error));
  EXPECT_FALSE(from_value(parsed, widget, error));
  ASSERT_TRUE(json_parse("{\"id\":\"x\",\"type\":\"status\",\"rules\":[{\"level\":\"purple\"}]}", parsed, json_error));
  EXPECT_FALSE(from_value(parsed, widget, error));
  EXPECT_NE(error.find("purple"), std::string::npos);
  ASSERT_TRUE(json_parse("{\"id\":\"x\",\"type\":\"row\",\"children\":[{\"id\":\"y\"}]}", parsed, json_error));
  EXPECT_FALSE(from_value(parsed, widget, error));
  ASSERT_TRUE(json_parse("{\"id\":\"x\",\"type\":\"quaternion\",\"quaternion_display\":\"polar\"}", parsed, json_error));
  EXPECT_FALSE(from_value(parsed, widget, error));
}

TEST(Serialization, FromValueRestoresDefaultsForAbsentKeys)
{
  UiValue parsed;
  JsonParseError json_error;
  ASSERT_TRUE(json_parse("{\"id\":\"c\",\"type\":\"chart\",\"bind\":\"a\"}", parsed, json_error));
  Widget widget;
  std::string error;
  ASSERT_TRUE(from_value(parsed, widget, error)) << error;
  EXPECT_EQ(widget.window_seconds, 30.0);
  EXPECT_EQ(widget.max_points, 600u);
  EXPECT_FALSE(widget.editable);
  EXPECT_FALSE(widget.min);
  EXPECT_TRUE(widget.payload.is_null());
  EXPECT_EQ(widget.quaternion_display, QuaternionDisplay::Both);
}
