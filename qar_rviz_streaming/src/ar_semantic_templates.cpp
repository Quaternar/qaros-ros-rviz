// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_semantic_templates.hpp"

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qar_rviz_streaming
{
namespace
{
using qar_ui_model::FieldDescription;
using qar_ui_model::FieldKind;
using qar_ui_model::MessageDescription;
using qar_ui_model::ResolvedField;
using qar_ui_model::StatusLevel;
using qar_ui_model::StatusRule;
using qar_ui_model::TableColumn;
using qar_ui_model::UiModel;
using qar_ui_model::UiValue;
using qar_ui_model::Widget;
namespace widgets = qar_ui_model::widgets;

// ---------------------------------------------------------------------------
// Sections and context
// ---------------------------------------------------------------------------

/// One section of the panel's data document, as the panel generator fixes it.
struct SectionRef
{
  const char * key = ar_doc::kMessage;
  bool writable = false;
  const char * title = "Live";
};

std::vector<SectionRef> panel_sections(const PanelGenerationRequest & request)
{
  switch (request.entity.kind) {
    case RosEntityKind::Topic: {
      std::vector<SectionRef> out{{ar_doc::kMessage, false, "Live"}};
      if (request.writable) {
        out.push_back({ar_doc::kDraft, true, "Command"});
      }
      return out;
    }
    case RosEntityKind::Service:
      return {{ar_doc::kRequest, true, "Request"}, {ar_doc::kResponse, false, "Response"}};
    case RosEntityKind::Action:
      return {
        {ar_doc::kGoal, true, "Goal"},
        {ar_doc::kFeedback, false, "Progress"},
        {ar_doc::kResult, false, "Result"}};
  }
  return {};
}

/// Everything a layout needs: the request, the document schema every binding is
/// checked against, and the panel id every widget id hangs off.
struct Ctx
{
  const PanelGenerationRequest * request = nullptr;
  const MessageDescription * doc = nullptr;
  std::string panel_id;

  /// Widget ids are `[A-Za-z_][A-Za-z0-9_.-]*` and at most 128 bytes; a binding
  /// with `[i]` in it is not one, so the suffix is sanitized and bounded here
  /// rather than relied upon to be well formed.
  std::string id(const std::string & suffix) const
  {
    std::string out = panel_id;
    out += '.';
    for (const char c : suffix) {
      const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
      out += ok ? c : '_';
    }
    if (out.size() > 128) {
      out.resize(128);
    }
    return out;
  }
};

std::string bind(const SectionRef & section, const std::string & relative)
{
  return relative.empty() ? std::string(section.key) : std::string(section.key) + "." + relative;
}

/// The widget-id suffix for a binding.
///
/// A widget bound to the section ROOT (a bare `geometry_msgs/msg/Point` is its
/// own position) would otherwise take the Section widget's own id.
std::string widget_suffix(const SectionRef & section, const std::string & relative)
{
  return relative.empty()
         ? std::string(section.key) + "-value"
         : std::string(section.key) + "." + relative;
}

/// The id of a widget that is NOT one field: a layout row, a note, a chart
/// beside its readout. A ROS field name is `[A-Za-z_][A-Za-z0-9_]*`, so a `-`
/// makes these unable to collide with a field-derived id — which
/// `sensor_msgs/msg/CompressedImage`, whose `format` field sat exactly where the
/// format row wanted to be, is the standing proof of.
std::string synthetic_suffix(const SectionRef & section, const std::string & role)
{
  return std::string(section.key) + "-" + role;
}

// ---------------------------------------------------------------------------
// Schema probes — nothing is emitted before its binding is known to resolve
// ---------------------------------------------------------------------------

const FieldDescription * resolve(const Ctx & c, const std::string & binding, bool & out_indexed)
{
  ResolvedField resolved;
  std::string error;
  if (!qar_ui_model::resolve_field_path(*c.doc, binding, resolved, error)) {
    return nullptr;
  }
  out_indexed = resolved.indexed;
  return resolved.field;
}

/// A single value a ValueDisplay / NumberInput / Toggle / StatusLight can bind.
const FieldDescription * scalar_at(const Ctx & c, const std::string & binding)
{
  bool indexed = false;
  const FieldDescription * field = resolve(c, binding, indexed);
  if (field == nullptr || field->is_message()) {
    return nullptr;
  }
  if (field->is_array() && !indexed) {
    return nullptr;  // a whole array is not a scalar
  }
  return field;
}

const FieldDescription * numeric_at(const Ctx & c, const std::string & binding)
{
  const FieldDescription * field = scalar_at(c, binding);
  return (field != nullptr && qar_ui_model::is_numeric(field->kind)) ? field : nullptr;
}

bool has_numeric_members(
  const MessageDescription & message, std::initializer_list<const char *> names)
{
  for (const char * name : names) {
    const FieldDescription * member = message.find_field(name);
    if (member == nullptr || member->is_array() || !qar_ui_model::is_numeric(member->kind)) {
      return false;
    }
  }
  return true;
}

/// A nested message with the given numeric members (a Vector3, a Quaternion).
const MessageDescription * message_at(
  const Ctx & c, const std::string & binding, std::initializer_list<const char *> members)
{
  bool indexed = false;
  const FieldDescription * field = resolve(c, binding, indexed);
  if (field == nullptr || !field->is_message() || !field->message) {
    return nullptr;
  }
  if (field->is_array() && !indexed) {
    return nullptr;
  }
  return has_numeric_members(*field->message, members) ? field->message.get() : nullptr;
}

/// A whole array (a Table binds one of these; a JsonInspector may too).
const FieldDescription * array_at(const Ctx & c, const std::string & binding)
{
  bool indexed = false;
  const FieldDescription * field = resolve(c, binding, indexed);
  if (field == nullptr || !field->is_array() || indexed) {
    return nullptr;
  }
  return field;
}

/// A column path resolves inside the element type of @p array.
bool column_resolves(const FieldDescription & array, const std::string & path)
{
  if (!array.is_message() || !array.message) {
    return path.empty();
  }
  ResolvedField resolved;
  std::string error;
  return qar_ui_model::resolve_field_path(*array.message, path, resolved, error);
}

// ---------------------------------------------------------------------------
// Status rules
// ---------------------------------------------------------------------------

StatusRule eq_rule(std::int64_t value, StatusLevel level, std::string text)
{
  StatusRule rule;
  rule.equals = UiValue(value);
  rule.level = level;
  rule.text = std::move(text);
  return rule;
}

StatusRule bool_rule(bool value, StatusLevel level, std::string text)
{
  StatusRule rule;
  rule.equals = UiValue(value);
  rule.level = level;
  rule.text = std::move(text);
  return rule;
}

StatusRule range_rule(double min, double max, StatusLevel level, std::string text)
{
  StatusRule rule;
  rule.min = min;
  rule.max = max;
  rule.level = level;
  rule.text = std::move(text);
  return rule;
}

// ---------------------------------------------------------------------------
// Widget emitters. Each returns false and emits nothing when the field is not
// in this distro's version of the message.
// ---------------------------------------------------------------------------

/// Read-only in a read-only section, an input in a writable one.
bool add_scalar(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label, const std::string & unit = {},
  std::optional<int> precision = std::nullopt)
{
  const std::string binding = bind(section, relative);
  const FieldDescription * field = scalar_at(c, binding);
  if (field == nullptr) {
    return false;
  }
  const std::string id = c.id(widget_suffix(section, relative));

  Widget widget = [&]() {
      if (!section.writable) {
        return widgets::value(id, binding, label);
      }
      if (qar_ui_model::is_boolean(field->kind)) {
        return widgets::toggle(id, binding, label);
      }
      if (qar_ui_model::is_string(field->kind)) {
        Widget text = widgets::text_input(id, binding, label);
        if (field->string_upper_bound != 0) {
          text.max_length = field->string_upper_bound;
        }
        return text;
      }
      Widget number = widgets::number(id, binding, label);
      number.integer = qar_ui_model::is_integer(field->kind);
      return number;
    }();

  if (!unit.empty()) {
    widget.with_unit(unit);
  } else if (!field->unit.empty()) {
    widget.with_unit(field->unit);
  }
  if (precision) {
    widget.with_precision(*precision);
  }
  if (field->min && field->max) {
    widget.with_range(*field->min, *field->max);
  }
  if (field->step) {
    widget.with_step(*field->step);
  }
  if (section.writable) {
    widget.with_editable(true).with_action(ar_actions::kSetField);
  }
  parent.add(std::move(widget));
  return true;
}

bool add_status(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label, std::vector<StatusRule> rules)
{
  const std::string binding = bind(section, relative);
  if (scalar_at(c, binding) == nullptr) {
    return false;
  }
  Widget light = widgets::status_light(c.id(widget_suffix(section, relative)), binding, label);
  for (StatusRule & rule : rules) {
    light.with_rule(std::move(rule));
  }
  parent.add(std::move(light));
  return true;
}

bool add_gauge(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label, double min, double max, std::vector<StatusRule> rules,
  const std::string & unit = {}, std::optional<int> precision = std::nullopt)
{
  const std::string binding = bind(section, relative);
  if (numeric_at(c, binding) == nullptr) {
    return false;
  }
  Widget gauge =
    widgets::gauge(c.id(widget_suffix(section, relative)), binding, label, min, max);
  for (StatusRule & rule : rules) {
    gauge.with_rule(std::move(rule));
  }
  if (!unit.empty()) {
    gauge.with_unit(unit);
  }
  if (precision) {
    gauge.with_precision(*precision);
  }
  parent.add(std::move(gauge));
  return true;
}

bool add_chart(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label, const std::string & unit = {})
{
  const std::string binding = bind(section, relative);
  if (numeric_at(c, binding) == nullptr) {
    return false;
  }
  Widget chart =
    widgets::time_series(c.id(widget_suffix(section, relative) + "-chart"), binding, label);
  if (!unit.empty()) {
    chart.with_unit(unit);
  }
  parent.add(std::move(chart));
  return true;
}

bool add_vector3(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label, const std::string & unit = {}, const std::string & help = {})
{
  const std::string binding = bind(section, relative);
  if (message_at(c, binding, {"x", "y", "z"}) == nullptr) {
    return false;
  }
  Widget vector = widgets::vector3(c.id(widget_suffix(section, relative)), binding, label);
  if (!unit.empty()) {
    vector.with_unit(unit);
  }
  if (!help.empty()) {
    vector.with_description(help);
  }
  if (section.writable) {
    vector.with_editable(true).with_action(ar_actions::kSetField);
  } else {
    vector.with_editable(false);
  }
  parent.add(std::move(vector));
  return true;
}

bool add_quaternion(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label)
{
  const std::string binding = bind(section, relative);
  if (message_at(c, binding, {"x", "y", "z", "w"}) == nullptr) {
    return false;
  }
  Widget quaternion = widgets::quaternion(c.id(widget_suffix(section, relative)), binding, label);
  // Both: the raw x/y/z/w AND roll/pitch/yaw in degrees. The page runtime
  // converts in both directions, so an operator sets an orientation by typing
  // a yaw, never by typing a quaternion.
  quaternion.quaternion_display = qar_ui_model::QuaternionDisplay::Both;
  quaternion.with_description("Roll / pitch / yaw in degrees");
  if (section.writable) {
    quaternion.with_editable(true).with_action(ar_actions::kSetField);
  } else {
    quaternion.with_editable(false);
  }
  parent.add(std::move(quaternion));
  return true;
}

bool add_slider(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label, double min, double max, double step, const std::string & unit)
{
  const std::string binding = bind(section, relative);
  const FieldDescription * field = numeric_at(c, binding);
  if (field == nullptr) {
    return false;
  }
  // A documented range in the IDL beats this template's default: the robot's
  // own limits are better than ours.
  if (field->min && field->max) {
    min = *field->min;
    max = *field->max;
  }
  Widget slider =
    widgets::slider(c.id(widget_suffix(section, relative)), binding, label, min, max);
  slider.with_step(field->step ? *field->step : step).with_unit(unit).with_precision(2);
  slider.with_editable(true).with_action(ar_actions::kSetField);
  parent.add(std::move(slider));
  return true;
}

bool add_table(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label, std::vector<TableColumn> columns)
{
  const std::string binding = bind(section, relative);
  const FieldDescription * array = array_at(c, binding);
  if (array == nullptr) {
    return false;
  }
  std::vector<TableColumn> kept;
  for (TableColumn & column : columns) {
    if (column_resolves(*array, column.path)) {
      kept.push_back(std::move(column));
    }
  }
  if (kept.empty()) {
    return false;
  }
  parent.add(
    widgets::table(c.id(widget_suffix(section, relative)), binding, label, std::move(kept)));
  return true;
}

bool add_inspector(
  Widget & parent, const Ctx & c, const SectionRef & section, const std::string & relative,
  const std::string & label)
{
  const std::string binding = bind(section, relative);
  bool indexed = false;
  if (resolve(c, binding, indexed) == nullptr) {
    return false;
  }
  parent.add(
    widgets::json_inspector(c.id(widget_suffix(section, relative)), binding).with_label(label));
  return true;
}

bool add_note(Widget & parent, const Ctx & c, const std::string & suffix, const std::string & text)
{
  parent.add(widgets::label(c.id(suffix), text));
  return true;
}

/// frame_id + stamp, uniformly, wherever a `std_msgs/msg/Header` sits.
///
/// The stamp is shown as its raw `sec` field: the widget model has no
/// time-formatting hint, so "12 s ago" is not expressible from here (see the
/// docs/ARCHITECTURE.md, "AR tools pipeline"). Liveness is instead carried by
/// the page's own connection pill and by every StatusLight, which the runtime
/// renders `stale` the moment its value goes null.
bool add_header(Widget & parent, const Ctx & c, const SectionRef & section)
{
  Widget row = widgets::row(c.id(synthetic_suffix(section, "header")));
  // `frame_id` is the one part of a header an operator legitimately sets, so it
  // follows the section's writability. The STAMP does not: a hand-typed epoch
  // second is not a thing anyone wants in mid-air, and a staged 0 is what most
  // of the stack already reads as "now". So it is shown only where it means
  // something — on the message that arrived.
  bool any = add_scalar(row, c, section, "header.frame_id", "Frame");
  if (!section.writable) {
    any = add_scalar(row, c, section, "header.stamp.sec", "Stamp", "s", 0) || any;
  }
  if (any) {
    parent.add(std::move(row));
  }
  return any;
}

/// The button that turns a staged section into a ROS operation. Identical ids
/// and confirm policy to the generic form: a robot command is confirmed, a
/// cancel fires on the first press.
void add_submit(Widget & section_widget, const Ctx & c)
{
  switch (c.request->entity.kind) {
    case RosEntityKind::Topic:
      section_widget.add(
        widgets::button(c.id("publish"), "Publish", ar_actions::kPublish).with_confirm(true));
      break;
    case RosEntityKind::Service:
      section_widget.add(widgets::button(c.id("call"), "Call service", ar_actions::kCall));
      break;
    case RosEntityKind::Action:
      section_widget.add(
        widgets::button(c.id("send"), "Send goal", ar_actions::kSendGoal).with_confirm(true));
      section_widget.add(
        widgets::button(c.id("cancel"), "Cancel goal", ar_actions::kCancelGoal));
      break;
  }
}

/// The zeroed message for a writable section, as a button preset. The router
/// accepts an object payload on a publish/call/goal button and sends THAT
/// instead of what is staged, which is what makes a one-press stop possible.
std::optional<UiValue> zeroed_section(const Ctx & c, const SectionRef & section)
{
  const FieldDescription * field = c.doc->find_field(section.key);
  if (field == nullptr || !field->message) {
    return std::nullopt;
  }
  return qar_ui_model::make_default_value(*field->message);
}

/// First candidate whose binding resolves to a message with @p members.
std::optional<std::string> first_message(
  const Ctx & c, const SectionRef & section, const std::vector<const char *> & candidates,
  std::initializer_list<const char *> members, bool reject_quaternion = false)
{
  for (const char * candidate : candidates) {
    const std::string relative = candidate;
    const MessageDescription * message = message_at(c, bind(section, relative), members);
    if (message == nullptr) {
      continue;
    }
    if (reject_quaternion && has_numeric_members(*message, {"w"})) {
      continue;  // x/y/z/w is an orientation, not a position
    }
    return relative;
  }
  return std::nullopt;
}

const std::vector<const char *> & position_candidates()
{
  static const std::vector<const char *> kPaths = {
    "position", "translation", "point", "pose.position", "transform.translation",
    "pose.pose.position", "current_pose.pose.position", ""};
  return kPaths;
}

const std::vector<const char *> & orientation_candidates()
{
  static const std::vector<const char *> kPaths = {
    "orientation", "rotation", "quaternion", "pose.orientation", "transform.rotation",
    "pose.pose.orientation", "current_pose.pose.orientation", ""};
  return kPaths;
}

const std::vector<const char *> & twist_candidates()
{
  static const std::vector<const char *> kPaths = {"twist.twist", "twist", ""};
  return kPaths;
}

/// A Twist-shaped message: `linear` and `angular`, both x/y/z.
std::optional<std::string> first_twist(const Ctx & c, const SectionRef & section)
{
  for (const char * candidate : twist_candidates()) {
    const std::string relative = candidate;
    const std::string base = bind(section, relative);
    if (message_at(c, base + ".linear", {"x", "y", "z"}) != nullptr &&
      message_at(c, base + ".angular", {"x", "y", "z"}) != nullptr)
    {
      return relative;
    }
  }
  return std::nullopt;
}

std::string join(const std::string & prefix, const char * leaf)
{
  return prefix.empty() ? std::string(leaf) : prefix + "." + leaf;
}

// ---------------------------------------------------------------------------
// Layouts
// ---------------------------------------------------------------------------

using SectionLayout = bool (*)(const Ctx &, const SectionRef &, Widget &);

/// The common shape: one Section widget per document section, laid out by
/// @p layout, with the submit buttons appended to the writable ones.
bool sectioned(const Ctx & c, Widget & root, SectionLayout layout)
{
  bool any = false;
  if (c.request->entity.kind == RosEntityKind::Action) {
    root.add(widgets::value(c.id("status"), ar_doc::kStatus, "Goal status"));
  }
  for (const SectionRef & section : panel_sections(*c.request)) {
    if (c.doc->find_field(section.key) == nullptr) {
      continue;
    }
    Widget widget = widgets::section(c.id(section.key), section.title);
    const bool filled = layout(c, section, widget);
    if (section.writable) {
      add_submit(widget, c);
    }
    if (filled || section.writable) {
      root.add(std::move(widget));
      any = any || filled;
    }
  }
  return any;
}

// --- 1. Pose, orientation and transforms ------------------------------------

bool pose_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  any = add_scalar(out, c, section, "child_frame_id", "Child frame") || any;

  const auto orientation = first_message(c, section, orientation_candidates(), {"x", "y", "z", "w"});
  const auto position =
    first_message(c, section, position_candidates(), {"x", "y", "z"}, /*reject_quaternion=*/true);

  if (position && (!orientation || *position != *orientation)) {
    any = add_vector3(out, c, section, *position, "Position", "m") || any;
  }
  if (orientation) {
    any = add_quaternion(out, c, section, *orientation, "Orientation") || any;
  }
  for (const char * covariance : {"covariance", "pose.covariance"}) {
    if (add_inspector(out, c, section, covariance, "Covariance (6x6)")) {
      any = true;
      break;
    }
  }
  return any;
}

bool pose_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &pose_section); }

// --- 2. Velocity command ----------------------------------------------------

bool twist_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  const auto prefix = first_twist(c, section);
  if (!prefix) {
    return false;
  }
  if (!section.writable) {
    bool any = add_header(out, c, section);
    any = add_vector3(out, c, section, join(*prefix, "linear"), "Linear velocity", "m/s") || any;
    any = add_vector3(out, c, section, join(*prefix, "angular"), "Angular velocity", "rad/s") || any;
    any = add_chart(out, c, section, join(*prefix, "linear.x"), "Forward speed", "m/s") || any;
    any = add_chart(out, c, section, join(*prefix, "angular.z"), "Yaw rate", "rad/s") || any;
    return any;
  }

  // Stop is the whole reason a velocity panel is different from a form: it is
  // the FIRST thing on the section — above the frame and the sliders — it needs
  // no confirmation, and it publishes an all-zero message outright rather than
  // staging three fields first.
  bool any = false;
  if (const auto zero = zeroed_section(c, section)) {
    out.add(
      widgets::button(c.id("stop"), "STOP", ar_actions::kPublish)
      .with_payload(*zero)
      .with_description("Publishes an all-zero command immediately"));
    any = true;
  }
  any = add_header(out, c, section) || any;
  // Defaults for a ground robot; a documented IDL range overrides them. The
  // unused axes (linear.z, angular.x/y) stay at their zero default rather than
  // taking three more sliders an operator would have to keep at zero.
  any = add_slider(out, c, section, join(*prefix, "linear.x"), "Forward", -0.5, 0.5, 0.05, "m/s") ||
    any;
  any = add_slider(out, c, section, join(*prefix, "linear.y"), "Strafe", -0.25, 0.25, 0.05, "m/s") ||
    any;
  any = add_slider(out, c, section, join(*prefix, "angular.z"), "Turn", -1.0, 1.0, 0.1, "rad/s") ||
    any;
  return any;
}

bool twist_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &twist_section); }

// --- 3. Battery -------------------------------------------------------------

std::vector<StatusRule> power_supply_status_rules()
{
  return {
    eq_rule(0, StatusLevel::Neutral, "Unknown"),
    eq_rule(1, StatusLevel::Ok, "Charging"),
    eq_rule(2, StatusLevel::Neutral, "Discharging"),
    eq_rule(3, StatusLevel::Warning, "Not charging"),
    eq_rule(4, StatusLevel::Ok, "Full")};
}

std::vector<StatusRule> power_supply_health_rules()
{
  return {
    eq_rule(0, StatusLevel::Neutral, "Unknown"),
    eq_rule(1, StatusLevel::Ok, "Good"),
    eq_rule(2, StatusLevel::Error, "Overheat"),
    eq_rule(3, StatusLevel::Error, "Dead"),
    eq_rule(4, StatusLevel::Error, "Overvoltage"),
    eq_rule(5, StatusLevel::Error, "Unspecified failure"),
    eq_rule(6, StatusLevel::Warning, "Cold"),
    eq_rule(7, StatusLevel::Error, "Watchdog timer expired"),
    eq_rule(8, StatusLevel::Error, "Safety timer expired")};
}

std::vector<StatusRule> power_supply_technology_rules()
{
  return {
    eq_rule(0, StatusLevel::Neutral, "Unknown chemistry"),
    eq_rule(1, StatusLevel::Neutral, "NiMH"),
    eq_rule(2, StatusLevel::Neutral, "Li-ion"),
    eq_rule(3, StatusLevel::Neutral, "LiPo"),
    eq_rule(4, StatusLevel::Neutral, "LiFe"),
    eq_rule(5, StatusLevel::Neutral, "NiCd"),
    eq_rule(6, StatusLevel::Neutral, "LiMn")};
}

bool battery_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  // sensor_msgs/BatteryState documents `percentage` as a 0..1 fraction. It is
  // shown as one, rather than guessed at and multiplied: a driver that really
  // publishes 0..100 saturates the gauge visibly instead of lying quietly.
  any = add_gauge(
    out, c, section, "percentage", "Charge", 0.0, 1.0,
    {range_rule(0.0, 0.15, StatusLevel::Error, "Critical"),
      range_rule(0.15, 0.30, StatusLevel::Warning, "Low"),
      range_rule(0.30, 1.0, StatusLevel::Ok, "OK")},
    {}, 2) || any;
  any = add_status(out, c, section, "power_supply_status", "Supply", power_supply_status_rules()) ||
    any;
  any = add_status(out, c, section, "power_supply_health", "Health", power_supply_health_rules()) ||
    any;
  any = add_status(
    out, c, section, "present", "Battery present",
    {bool_rule(true, StatusLevel::Ok, "Present"),
      bool_rule(false, StatusLevel::Error, "Absent")}) || any;

  Widget row = widgets::row(c.id(synthetic_suffix(section, "electrical")));
  bool row_any = add_scalar(row, c, section, "voltage", "Voltage", "V", 2);
  row_any = add_scalar(row, c, section, "current", "Current", "A", 2) || row_any;
  row_any = add_scalar(row, c, section, "charge", "Charge", "Ah", 2) || row_any;
  row_any = add_scalar(row, c, section, "capacity", "Capacity", "Ah", 2) || row_any;
  row_any = add_scalar(row, c, section, "design_capacity", "Design capacity", "Ah", 2) || row_any;
  row_any = add_scalar(row, c, section, "temperature", "Temperature", "degC", 1) || row_any;
  if (row_any) {
    out.add(std::move(row));
    any = true;
  }

  any = add_chart(out, c, section, "voltage", "Voltage", "V") || any;
  any = add_status(
    out, c, section, "power_supply_technology", "Chemistry",
    power_supply_technology_rules()) || any;

  Widget identity = widgets::row(c.id(synthetic_suffix(section, "identity")));
  bool identity_any = add_scalar(identity, c, section, "location", "Location");
  identity_any = add_scalar(identity, c, section, "serial_number", "Serial") || identity_any;
  if (identity_any) {
    out.add(std::move(identity));
    any = true;
  }

  any = add_table(
    out, c, section, "cell_voltage", "Cell voltage",
    {TableColumn{"V", "", "V", 3}}) || any;
  return any;
}

bool battery_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &battery_section); }

// --- 4. Diagnostics ---------------------------------------------------------

std::vector<StatusRule> diagnostic_level_rules()
{
  return {
    eq_rule(0, StatusLevel::Ok, "OK"),
    eq_rule(1, StatusLevel::Warning, "WARN"),
    eq_rule(2, StatusLevel::Error, "ERROR"),
    eq_rule(3, StatusLevel::Stale, "STALE")};
}

/// How many array entries get their own status light before the table takes
/// over. Eight rows is the theme's page size and about what fits in a headset.
constexpr std::size_t kDiagnosticLights = 6;

bool diagnostic_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);

  if (array_at(c, bind(section, "status")) != nullptr) {
    // DiagnosticArray: a light per component, then the whole array as a table.
    // A light bound past the end of the array goes null, and the page runtime
    // renders a null status as `stale` — which is exactly right for a component
    // that stopped reporting.
    for (std::size_t i = 0; i < kDiagnosticLights; ++i) {
      const std::string element = "status[" + std::to_string(i) + "]";
      Widget row = widgets::row(c.id(synthetic_suffix(section, "status" + std::to_string(i))));
      bool row_any =
        add_status(row, c, section, element + ".level", {}, diagnostic_level_rules());
      row_any = add_scalar(row, c, section, element + ".name", "Component") || row_any;
      row_any = add_scalar(row, c, section, element + ".message", "Message") || row_any;
      if (!row_any) {
        break;
      }
      out.add(std::move(row));
      any = true;
    }
    any = add_table(
      out, c, section, "status", "All components",
      {TableColumn{"Level", "level", "", 0},
        TableColumn{"Component", "name", "", {}},
        TableColumn{"Message", "message", "", {}},
        TableColumn{"Hardware", "hardware_id", "", {}}}) || any;
    return any;
  }

  // A single DiagnosticStatus.
  any = add_status(out, c, section, "level", "Level", diagnostic_level_rules()) || any;
  any = add_scalar(out, c, section, "name", "Component") || any;
  any = add_scalar(out, c, section, "message", "Message") || any;
  any = add_scalar(out, c, section, "hardware_id", "Hardware id") || any;
  any = add_table(
    out, c, section, "values", "Values",
    {TableColumn{"Key", "key", "", {}}, TableColumn{"Value", "value", "", {}}}) || any;
  return any;
}

bool diagnostic_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &diagnostic_section);
}

// --- 5. Navigation ----------------------------------------------------------

bool odometry_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  any = add_scalar(out, c, section, "child_frame_id", "Body frame") || any;
  any = add_vector3(out, c, section, "pose.pose.position", "Position", "m") || any;
  any = add_quaternion(out, c, section, "pose.pose.orientation", "Orientation") || any;
  any = add_vector3(out, c, section, "twist.twist.linear", "Linear velocity", "m/s") || any;
  any = add_vector3(out, c, section, "twist.twist.angular", "Angular velocity", "rad/s") || any;
  any = add_chart(out, c, section, "twist.twist.linear.x", "Forward speed", "m/s") || any;
  return any;
}

bool odometry_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &odometry_section);
}

bool path_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  // Paged, not dumped: `poses` is routinely a few thousand entries, and the
  // table shows one theme page at a time. What the host streams is capped by
  // elide_large_arrays.
  any = add_table(
    out, c, section, "poses", "Waypoints",
    {TableColumn{"X", "pose.position.x", "m", 2},
      TableColumn{"Y", "pose.position.y", "m", 2},
      TableColumn{"Z", "pose.position.z", "m", 2},
      TableColumn{"Frame", "header.frame_id", "", {}}}) || any;
  return any;
}

bool path_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &path_section); }

bool nav_goal_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = pose_section(c, section, out);
  any = add_scalar(out, c, section, "behavior_tree", "Behavior tree") || any;
  any = add_scalar(out, c, section, "distance_remaining", "Distance remaining", "m", 2) || any;
  any = add_scalar(out, c, section, "navigation_time.sec", "Elapsed", "s", 0) || any;
  any = add_scalar(out, c, section, "estimated_time_remaining.sec", "ETA", "s", 0) || any;
  any = add_scalar(out, c, section, "number_of_recoveries", "Recoveries") || any;
  any = add_scalar(out, c, section, "number_of_poses_remaining", "Poses remaining") || any;
  any = add_scalar(out, c, section, "error_code", "Error code") || any;
  return any;
}

bool nav_action_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &nav_goal_section);
}

// --- 6. TF ------------------------------------------------------------------

bool tf_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  return add_table(
    out, c, section, "transforms", "Transforms",
    {TableColumn{"Parent", "header.frame_id", "", {}},
      TableColumn{"Child", "child_frame_id", "", {}},
      TableColumn{"X", "transform.translation.x", "m", 3},
      TableColumn{"Y", "transform.translation.y", "m", 3},
      TableColumn{"Z", "transform.translation.z", "m", 3}});
}

bool tf_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &tf_section); }

// --- 7. Robot and system status ---------------------------------------------

bool std_msgs_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  const FieldDescription * data = scalar_at(c, bind(section, "data"));
  if (data == nullptr) {
    return false;
  }
  bool any = false;
  if (qar_ui_model::is_boolean(data->kind) && !section.writable) {
    any = add_status(
      out, c, section, "data", "Value",
      {bool_rule(true, StatusLevel::Ok, "True"),
        bool_rule(false, StatusLevel::Neutral, "False")});
  } else {
    any = add_scalar(out, c, section, "data", "Value");
  }
  if (qar_ui_model::is_numeric(data->kind) && !section.writable) {
    any = add_chart(out, c, section, "data", "Value") || any;
  }
  return any;
}

bool std_msgs_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &std_msgs_section);
}

/// How many joints get their own aligned row. `JointState` is four PARALLEL
/// arrays, not an array of structs, so a table cannot line them up — indexed
/// bindings can.
constexpr std::size_t kJointRows = 8;

bool joint_state_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  for (std::size_t i = 0; i < kJointRows; ++i) {
    const std::string index = "[" + std::to_string(i) + "]";
    Widget row = widgets::row(c.id(synthetic_suffix(section, "joint" + std::to_string(i))));
    bool row_any = add_scalar(row, c, section, "name" + index, "Joint");
    row_any = add_scalar(row, c, section, "position" + index, "Position", "rad", 3) || row_any;
    row_any = add_scalar(row, c, section, "velocity" + index, "Velocity", "rad/s", 3) || row_any;
    row_any = add_scalar(row, c, section, "effort" + index, "Effort", "Nm", 3) || row_any;
    if (!row_any) {
      break;
    }
    out.add(std::move(row));
    any = true;
  }
  any = add_table(out, c, section, "name", "All joints", {TableColumn{"Joint", "", "", {}}}) || any;
  return any;
}

bool joint_state_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &joint_state_section);
}

bool service_status_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_scalar(out, c, section, "data", "Enable");
  any = add_status(
    out, c, section, "success", "Outcome",
    {bool_rule(true, StatusLevel::Ok, "Succeeded"),
      bool_rule(false, StatusLevel::Error, "Failed")}) || any;
  any = add_scalar(out, c, section, "message", "Message") || any;
  return any;
}

bool service_status_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &service_status_section);
}

// --- 8. Maps and sensors ----------------------------------------------------
//
// Every layout below is a SUMMARY. `data` on an OccupancyGrid, an Image or a
// PointCloud2, and `ranges` on a LaserScan, are hundreds of kilobytes to
// megabytes per message; none of them is bound by any widget here, and the
// note on each panel says where the real rendering lives.

bool occupancy_grid_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  Widget row = widgets::row(c.id(synthetic_suffix(section, "info")));
  bool row_any = add_scalar(row, c, section, "info.resolution", "Resolution", "m/cell", 3);
  row_any = add_scalar(row, c, section, "info.width", "Width", "cells") || row_any;
  row_any = add_scalar(row, c, section, "info.height", "Height", "cells") || row_any;
  row_any = add_scalar(row, c, section, "info.map_load_time.sec", "Loaded", "s", 0) || row_any;
  if (row_any) {
    out.add(std::move(row));
    any = true;
  }
  any = add_vector3(out, c, section, "info.origin.position", "Origin", "m") || any;
  any = add_quaternion(out, c, section, "info.origin.orientation", "Origin orientation") || any;
  if (any) {
    add_note(
      out, c, synthetic_suffix(section, "note"),
      "Occupancy data is not streamed to this panel; the grid itself is drawn by the "
      "RViz Map display.");
  }
  return any;
}

bool occupancy_grid_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &occupancy_grid_section);
}

bool laser_scan_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  Widget angles = widgets::row(c.id(synthetic_suffix(section, "angles")));
  bool row_any = add_scalar(angles, c, section, "angle_min", "Angle min", "rad", 3);
  row_any = add_scalar(angles, c, section, "angle_max", "Angle max", "rad", 3) || row_any;
  row_any = add_scalar(angles, c, section, "angle_increment", "Step", "rad", 4) || row_any;
  if (row_any) {
    out.add(std::move(angles));
    any = true;
  }
  Widget ranges = widgets::row(c.id(synthetic_suffix(section, "ranges")));
  bool ranges_any = add_scalar(ranges, c, section, "range_min", "Range min", "m", 2);
  ranges_any = add_scalar(ranges, c, section, "range_max", "Range max", "m", 2) || ranges_any;
  ranges_any = add_scalar(ranges, c, section, "scan_time", "Scan time", "s", 4) || ranges_any;
  ranges_any =
    add_scalar(ranges, c, section, "time_increment", "Time step", "s", 6) || ranges_any;
  if (ranges_any) {
    out.add(std::move(ranges));
    any = true;
  }
  if (any) {
    add_note(
      out, c, synthetic_suffix(section, "note"),
      "Beam ranges are not streamed to this panel; the scan itself is drawn by the "
      "RViz LaserScan display.");
  }
  return any;
}

bool laser_scan_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &laser_scan_section);
}

bool image_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  Widget row = widgets::row(c.id(synthetic_suffix(section, "format")));
  bool row_any = add_scalar(row, c, section, "width", "Width", "px");
  row_any = add_scalar(row, c, section, "height", "Height", "px") || row_any;
  row_any = add_scalar(row, c, section, "encoding", "Encoding") || row_any;
  row_any = add_scalar(row, c, section, "step", "Row stride", "B") || row_any;
  row_any = add_scalar(row, c, section, "is_bigendian", "Big endian") || row_any;
  row_any = add_scalar(row, c, section, "format", "Format") || row_any;  // CompressedImage
  if (row_any) {
    out.add(std::move(row));
    any = true;
  }
  if (any) {
    // HOST-side action, not a document binding: no probe needed, just gate it
    // on the entity actually being a topic (the only kind Image/CompressedImage
    // are ever the type of, but a template never assumes what it can check).
    if (c.request->entity.kind == RosEntityKind::Topic) {
      out.add(
        widgets::button(
          c.id(synthetic_suffix(section, "open-video")), "Open video in AR",
          ar_actions::kOpenVideo));
    }
    add_note(
      out, c, synthetic_suffix(section, "note"),
      "Pixels are not sent to this page. Open video in AR streams this topic as its own "
      "AR panel.");
  }
  return any;
}

bool image_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &image_section); }

bool point_cloud_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  Widget row = widgets::row(c.id(synthetic_suffix(section, "shape")));
  bool row_any = add_scalar(row, c, section, "width", "Width", "points");
  row_any = add_scalar(row, c, section, "height", "Height", "rows") || row_any;
  row_any = add_scalar(row, c, section, "point_step", "Point stride", "B") || row_any;
  row_any = add_scalar(row, c, section, "row_step", "Row stride", "B") || row_any;
  row_any = add_scalar(row, c, section, "is_dense", "Dense") || row_any;
  if (row_any) {
    out.add(std::move(row));
    any = true;
  }
  any = add_table(
    out, c, section, "fields", "Channels",
    {TableColumn{"Name", "name", "", {}},
      TableColumn{"Offset", "offset", "B", 0},
      TableColumn{"Type", "datatype", "", 0},
      TableColumn{"Count", "count", "", 0}}) || any;
  if (any) {
    add_note(
      out, c, synthetic_suffix(section, "note"),
      "Points are not streamed to this panel; the cloud itself is drawn by the RViz "
      "PointCloud2 display.");
  }
  return any;
}

bool point_cloud_layout(const Ctx & c, Widget & root)
{
  return sectioned(c, root, &point_cloud_section);
}

bool imu_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  any = add_quaternion(out, c, section, "orientation", "Orientation") || any;
  any = add_vector3(out, c, section, "angular_velocity", "Angular velocity", "rad/s") || any;
  any = add_vector3(
    out, c, section, "linear_acceleration", "Linear acceleration", "m/s^2") || any;
  any = add_chart(out, c, section, "angular_velocity.z", "Yaw rate", "rad/s") || any;
  return any;
}

bool imu_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &imu_section); }

bool navsat_section(const Ctx & c, const SectionRef & section, Widget & out)
{
  bool any = add_header(out, c, section);
  any = add_status(
    out, c, section, "status.status", "Fix",
    {eq_rule(-1, StatusLevel::Error, "No fix"),
      eq_rule(0, StatusLevel::Ok, "Fix"),
      eq_rule(1, StatusLevel::Ok, "SBAS fix"),
      eq_rule(2, StatusLevel::Ok, "GBAS fix")}) || any;
  Widget row = widgets::row(c.id(synthetic_suffix(section, "fix")));
  // Seven decimals is about 1 cm of latitude: fewer and the readout is useless
  // for anything a robot does.
  bool row_any = add_scalar(row, c, section, "latitude", "Latitude", "deg", 7);
  row_any = add_scalar(row, c, section, "longitude", "Longitude", "deg", 7) || row_any;
  row_any = add_scalar(row, c, section, "altitude", "Altitude", "m", 2) || row_any;
  if (row_any) {
    out.add(std::move(row));
    any = true;
  }
  any = add_scalar(out, c, section, "status.service", "Constellations") || any;
  return any;
}

bool navsat_layout(const Ctx & c, Widget & root) { return sectioned(c, root, &navsat_section); }

// ---------------------------------------------------------------------------
// The template
// ---------------------------------------------------------------------------

using LayoutFn = bool (*)(const Ctx &, Widget &);

class SemanticPanelTemplate : public IPanelTemplate
{
public:
  SemanticPanelTemplate(const SemanticTemplateInfo & info, LayoutFn layout)
  : info_(&info), layout_(layout) {}

  std::string name() const override { return info_->name; }

  int match(const PanelGenerationRequest & request) const override
  {
    const std::string canonical = qar_ui_model::normalize_type_name(request.entity.type_name);
    for (const std::string & claimed : info_->types) {
      if (claimed == canonical) {
        return info_->score;
      }
    }
    return 0;
  }

  bool build(
    const PanelGenerationRequest & request,
    const MessageDescription & schema,
    UiModel & out_model,
    std::string & out_error) const override
  {
    Ctx context;
    context.request = &request;
    context.doc = &schema;
    context.panel_id = make_panel_id(request.entity.kind, request.entity.name);

    Widget root = widgets::panel(context.panel_id, request.entity.name);
    root.with_description(request.entity.type_name);
    const bool laid_out = layout_(context, root);

    // The safety net that makes claiming a type this repo cannot introspect
    // safe: a schema none of the probes recognised is not a broken panel, it is
    // a generic one. (A distro that renames a field, a vendor's fork of
    // BatteryState, a nav2 whose IDL moved on.)
    if (!laid_out || root.children.empty()) {
      if (!GenericFormTemplate().build(request, schema, out_model, out_error)) {
        return false;
      }
      out_model.metadata.set("template", UiValue(info_->name));
      out_model.metadata.set("fallback", UiValue(true));
      return true;
    }

    out_model = UiModel{};
    out_model.root = std::move(root);
    out_model.initial_data = qar_ui_model::make_default_value(schema);

    UiValue metadata = UiValue::object();
    metadata.set("entity_kind", UiValue(std::string(to_string(request.entity.kind))));
    metadata.set("entity_name", UiValue(request.entity.name));
    metadata.set("type_name", UiValue(request.entity.type_name));
    metadata.set("writable", UiValue(request.writable));
    metadata.set("template", UiValue(info_->name));
    out_model.metadata = std::move(metadata);
    return true;
  }

private:
  const SemanticTemplateInfo * info_;
  LayoutFn layout_;
};

// ---------------------------------------------------------------------------
// The catalog
// ---------------------------------------------------------------------------

struct CatalogEntry
{
  SemanticTemplateInfo info;
  LayoutFn layout;
};

const std::vector<CatalogEntry> & catalog()
{
  static const std::vector<CatalogEntry> kCatalog = {
    {{"pose",
      {"geometry_msgs/msg/Pose", "geometry_msgs/msg/PoseStamped",
        "geometry_msgs/msg/PoseWithCovariance", "geometry_msgs/msg/PoseWithCovarianceStamped",
        "geometry_msgs/msg/Point", "geometry_msgs/msg/Point32", "geometry_msgs/msg/Quaternion",
        "geometry_msgs/msg/QuaternionStamped", "geometry_msgs/msg/PointStamped",
        "geometry_msgs/msg/Transform", "geometry_msgs/msg/TransformStamped"},
      kSemanticScore,
      "Position as x/y/z, orientation as roll/pitch/yaw AND quaternion, frame and stamp."},
      &pose_layout},
    {{"velocity-command",
      {"geometry_msgs/msg/Twist", "geometry_msgs/msg/TwistStamped",
        "geometry_msgs/msg/TwistWithCovariance",
        "geometry_msgs/msg/TwistWithCovarianceStamped"},
      kSemanticScore,
      "Forward/strafe/turn sliders and a one-press STOP; charts when read-only."},
      &twist_layout},
    {{"battery",
      {"sensor_msgs/msg/BatteryState"},
      kSemanticScore,
      "Charge gauge, decoded supply/health/chemistry enums, cell table."},
      &battery_layout},
    {{"diagnostics",
      {"diagnostic_msgs/msg/DiagnosticArray", "diagnostic_msgs/msg/DiagnosticStatus"},
      kSemanticScore,
      "OK/WARN/ERROR/STALE lights per component plus the key/value table."},
      &diagnostic_layout},
    {{"odometry",
      {"nav_msgs/msg/Odometry"},
      kSemanticScore,
      "Pose, body-frame velocity and a forward-speed chart."},
      &odometry_layout},
    {{"path",
      {"nav_msgs/msg/Path", "nav_msgs/msg/Goals"},
      kSemanticScore,
      "Waypoints as a paged table; never the whole array at once."},
      &path_layout},
    {{"navigation-goal",
      {"nav2_msgs/action/NavigateToPose", "nav2_msgs/action/NavigateThroughPoses",
        "nav2_msgs/action/FollowPath", "nav2_msgs/action/ComputePathToPose",
        "nav2_msgs/action/FollowWaypoints"},
      kSemanticScore,
      "Goal pose in, live distance/ETA out, goal status and cancel."},
      &nav_action_layout},
    {{"tf",
      {"tf2_msgs/msg/TFMessage"},
      kSemanticScore,
      "Parent -> child listing with the translation, paged."},
      &tf_layout},
    {{"scalar-status",
      {"std_msgs/msg/Bool", "std_msgs/msg/String", "std_msgs/msg/Byte", "std_msgs/msg/Char",
        "std_msgs/msg/Int8", "std_msgs/msg/UInt8", "std_msgs/msg/Int16", "std_msgs/msg/UInt16",
        "std_msgs/msg/Int32", "std_msgs/msg/UInt32", "std_msgs/msg/Int64", "std_msgs/msg/UInt64",
        "std_msgs/msg/Float32", "std_msgs/msg/Float64"},
      kSemanticScore,
      "One value, big: a light for a bool, a chart for a number."},
      &std_msgs_layout},
    {{"joint-state",
      {"sensor_msgs/msg/JointState"},
      kSemanticScore,
      "Name/position/velocity/effort aligned per joint across the parallel arrays."},
      &joint_state_layout},
    {{"robot-service",
      {"std_srvs/srv/Trigger", "std_srvs/srv/SetBool", "std_srvs/srv/Empty"},
      kSemanticScore,
      "One switch or one button, and the success light with its message."},
      &service_status_layout},
    {{"occupancy-grid",
      {"nav_msgs/msg/OccupancyGrid"},
      kSemanticScore,
      "Resolution, size and origin. The cells are never streamed."},
      &occupancy_grid_layout},
    {{"laser-scan",
      {"sensor_msgs/msg/LaserScan"},
      kSemanticScore,
      "Angular span, range limits and timing. The beams are never streamed."},
      &laser_scan_layout},
    {{"image",
      {"sensor_msgs/msg/Image", "sensor_msgs/msg/CompressedImage"},
      kSemanticScore,
      "Dimensions, encoding and stride. The pixels are never streamed."},
      &image_layout},
    {{"point-cloud",
      {"sensor_msgs/msg/PointCloud2"},
      kSemanticScore,
      "Shape, strides and the channel table. The points are never streamed."},
      &point_cloud_layout},
    {{"imu",
      {"sensor_msgs/msg/Imu"},
      kSemanticScore,
      "Orientation, angular rate, acceleration and a yaw-rate chart."},
      &imu_layout},
    {{"navsat",
      {"sensor_msgs/msg/NavSatFix"},
      kSemanticScore,
      "Fix light, latitude/longitude to 7 decimals, altitude."},
      &navsat_layout},
  };
  return kCatalog;
}

}  // namespace

const std::vector<SemanticTemplateInfo> & semantic_template_catalog()
{
  static const std::vector<SemanticTemplateInfo> kInfos = []() {
      std::vector<SemanticTemplateInfo> out;
      out.reserve(catalog().size());
      for (const CatalogEntry & entry : catalog()) {
        out.push_back(entry.info);
      }
      return out;
    }();
  return kInfos;
}

void register_semantic_templates(PanelTemplateRegistry & registry)
{
  for (const CatalogEntry & entry : catalog()) {
    registry.add(std::make_shared<SemanticPanelTemplate>(entry.info, entry.layout));
  }
}

std::string semantic_template_for_type(std::string_view type_name)
{
  const std::string canonical = qar_ui_model::normalize_type_name(type_name);
  for (const CatalogEntry & entry : catalog()) {
    for (const std::string & claimed : entry.info.types) {
      if (claimed == canonical) {
        return entry.info.name;
      }
    }
  }
  return {};
}

std::size_t elide_large_arrays(UiValue & value, std::size_t max_elements)
{
  std::size_t elided = 0;
  if (value.is_array()) {
    qar_ui_model::UiArray & elements = value.elements();
    const std::size_t original = elements.size();
    if (max_elements == 0) {
      if (original != 0) {
        elements.clear();
        elements.push_back(UiValue(std::string("... " + std::to_string(original) + " elided")));
        ++elided;
      }
    } else if (original > max_elements) {
      elements.resize(max_elements);
      elements.push_back(
        UiValue(
          std::string(
            "... " + std::to_string(original - max_elements) + " more of " +
            std::to_string(original))));
      ++elided;
    }
    for (UiValue & element : elements) {
      elided += elide_large_arrays(element, max_elements);
    }
    return elided;
  }
  if (value.is_object()) {
    for (qar_ui_model::UiObjectEntry & entry : value.entries()) {
      elided += elide_large_arrays(entry.value, max_elements);
    }
  }
  return elided;
}

}  // namespace qar_rviz_streaming
