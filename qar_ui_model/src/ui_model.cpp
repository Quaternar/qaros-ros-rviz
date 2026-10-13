// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ui_model/ui_model.hpp"

#include <cmath>
#include <set>
#include <utility>

namespace qar_ui_model
{

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

namespace
{

struct WidgetTypeName
{
  WidgetType type;
  const char * name;
};

const WidgetTypeName kWidgetTypeNames[] = {
  {WidgetType::Panel, "panel"},
  {WidgetType::Section, "section"},
  {WidgetType::Row, "row"},
  {WidgetType::Column, "column"},
  {WidgetType::Label, "label"},
  {WidgetType::ValueDisplay, "value"},
  {WidgetType::NumberInput, "number"},
  {WidgetType::TextInput, "text"},
  {WidgetType::Toggle, "toggle"},
  {WidgetType::Slider, "slider"},
  {WidgetType::Choice, "choice"},
  {WidgetType::Button, "button"},
  {WidgetType::Vector3, "vector3"},
  {WidgetType::Quaternion, "quaternion"},
  {WidgetType::Table, "table"},
  {WidgetType::Gauge, "gauge"},
  {WidgetType::StatusLight, "status"},
  {WidgetType::TimeSeries, "chart"},
  {WidgetType::Image, "image"},
  {WidgetType::JsonInspector, "json"},
};

}  // namespace

const char *
to_string(WidgetType type)
{
  for (const WidgetTypeName & entry : kWidgetTypeNames) {
    if (entry.type == type) {
      return entry.name;
    }
  }
  return "unknown";
}

bool
widget_type_from_string(std::string_view text, WidgetType & out_type)
{
  for (const WidgetTypeName & entry : kWidgetTypeNames) {
    if (text == entry.name) {
      out_type = entry.type;
      return true;
    }
  }
  return false;
}

bool
is_container(WidgetType type)
{
  return type == WidgetType::Panel || type == WidgetType::Section || type == WidgetType::Row ||
         type == WidgetType::Column;
}

bool
is_input(WidgetType type)
{
  switch (type) {
    case WidgetType::NumberInput:
    case WidgetType::TextInput:
    case WidgetType::Toggle:
    case WidgetType::Slider:
    case WidgetType::Choice:
    case WidgetType::Vector3:
    case WidgetType::Quaternion:
      return true;
    default:
      return false;
  }
}

bool
is_bindable(WidgetType type)
{
  return !is_container(type) && type != WidgetType::Label;
}

const char *
to_string(StatusLevel level)
{
  switch (level) {
    case StatusLevel::Neutral:
      return "neutral";
    case StatusLevel::Ok:
      return "ok";
    case StatusLevel::Warning:
      return "warning";
    case StatusLevel::Error:
      return "error";
    case StatusLevel::Stale:
      return "stale";
  }
  return "unknown";
}

bool
status_level_from_string(std::string_view text, StatusLevel & out_level)
{
  static const StatusLevel kLevels[] = {
    StatusLevel::Neutral, StatusLevel::Ok, StatusLevel::Warning, StatusLevel::Error,
    StatusLevel::Stale};
  for (StatusLevel level : kLevels) {
    if (text == to_string(level)) {
      out_level = level;
      return true;
    }
  }
  return false;
}

const char *
to_string(QuaternionDisplay display)
{
  switch (display) {
    case QuaternionDisplay::Both:
      return "both";
    case QuaternionDisplay::Quaternion:
      return "quaternion";
    case QuaternionDisplay::Euler:
      return "euler";
  }
  return "unknown";
}

bool
quaternion_display_from_string(std::string_view text, QuaternionDisplay & out)
{
  static const QuaternionDisplay kValues[] = {
    QuaternionDisplay::Both, QuaternionDisplay::Quaternion, QuaternionDisplay::Euler};
  for (QuaternionDisplay value : kValues) {
    if (text == to_string(value)) {
      out = value;
      return true;
    }
  }
  return false;
}

const char *
to_string(ProblemSeverity severity)
{
  return severity == ProblemSeverity::Error ? "error" : "warning";
}

// ---------------------------------------------------------------------------
// Widget
// ---------------------------------------------------------------------------

Widget &
Widget::with_binding(std::string path)
{
  binding = std::move(path);
  return *this;
}

Widget &
Widget::with_label(std::string text)
{
  label = std::move(text);
  return *this;
}

Widget &
Widget::with_description(std::string text)
{
  description = std::move(text);
  return *this;
}

Widget &
Widget::with_unit(std::string text)
{
  unit = std::move(text);
  return *this;
}

Widget &
Widget::with_range(double min_value, double max_value)
{
  min = min_value;
  max = max_value;
  return *this;
}

Widget &
Widget::with_step(double step_value)
{
  step = step_value;
  return *this;
}

Widget &
Widget::with_precision(int decimals)
{
  precision = decimals;
  return *this;
}

Widget &
Widget::with_editable(bool value)
{
  editable = value;
  return *this;
}

Widget &
Widget::with_action(std::string action_id)
{
  action = std::move(action_id);
  return *this;
}

Widget &
Widget::with_payload(UiValue value)
{
  payload = std::move(value);
  return *this;
}

Widget &
Widget::with_confirm(bool value)
{
  confirm = value;
  return *this;
}

Widget &
Widget::with_integer(bool value)
{
  integer = value;
  return *this;
}

Widget &
Widget::with_rule(StatusRule rule)
{
  rules.push_back(std::move(rule));
  return *this;
}

Widget &
Widget::with_choice(std::string choice_label, UiValue value)
{
  choices.push_back(ChoiceOption{std::move(choice_label), std::move(value)});
  return *this;
}

Widget &
Widget::with_column(TableColumn column)
{
  columns.push_back(std::move(column));
  return *this;
}

Widget &
Widget::add(Widget child)
{
  children.push_back(std::move(child));
  return *this;
}

std::string
Widget::display_label() const
{
  if (!label.empty()) {
    return label;
  }
  FieldPath path;
  std::string error;
  if (!binding.empty() && FieldPath::parse(binding, path, error) && !path.empty()) {
    return humanize_identifier(path.segments.back().name);
  }
  return id;
}

namespace widgets
{

namespace
{

Widget
make(WidgetType type, std::string id)
{
  Widget widget;
  widget.type = type;
  widget.id = std::move(id);
  widget.editable = is_input(type);
  return widget;
}

}  // namespace

Widget
panel(std::string id, std::string title)
{
  return make(WidgetType::Panel, std::move(id)).with_label(std::move(title));
}

Widget
section(std::string id, std::string label)
{
  return make(WidgetType::Section, std::move(id)).with_label(std::move(label));
}

Widget
row(std::string id)
{
  return make(WidgetType::Row, std::move(id));
}

Widget
column(std::string id)
{
  return make(WidgetType::Column, std::move(id));
}

Widget
label(std::string id, std::string text)
{
  return make(WidgetType::Label, std::move(id)).with_label(std::move(text));
}

Widget
value(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::ValueDisplay, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
number(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::NumberInput, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
text_input(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::TextInput, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
toggle(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::Toggle, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
slider(std::string id, std::string binding, std::string label, double min, double max)
{
  return make(WidgetType::Slider, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label)).with_range(min, max);
}

Widget
choice(std::string id, std::string binding, std::string label, std::vector<ChoiceOption> options)
{
  Widget widget = make(WidgetType::Choice, std::move(id))
    .with_binding(std::move(binding)).with_label(std::move(label));
  widget.choices = std::move(options);
  return widget;
}

Widget
button(std::string id, std::string label, std::string action)
{
  return make(WidgetType::Button, std::move(id))
         .with_label(std::move(label)).with_action(std::move(action));
}

Widget
vector3(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::Vector3, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
quaternion(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::Quaternion, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
table(std::string id, std::string binding, std::string label, std::vector<TableColumn> columns)
{
  Widget widget = make(WidgetType::Table, std::move(id))
    .with_binding(std::move(binding)).with_label(std::move(label));
  widget.columns = std::move(columns);
  return widget;
}

Widget
gauge(std::string id, std::string binding, std::string label, double min, double max)
{
  return make(WidgetType::Gauge, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label)).with_range(min, max);
}

Widget
status_light(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::StatusLight, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
time_series(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::TimeSeries, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
image(std::string id, std::string binding, std::string label)
{
  return make(WidgetType::Image, std::move(id))
         .with_binding(std::move(binding)).with_label(std::move(label));
}

Widget
json_inspector(std::string id, std::string binding)
{
  return make(WidgetType::JsonInspector, std::move(id)).with_binding(std::move(binding));
}

}  // namespace widgets

// ---------------------------------------------------------------------------
// UiModel + traversal
// ---------------------------------------------------------------------------

namespace
{

bool
walk_widgets_impl(
  const Widget & widget, const Widget * parent, std::size_t depth, const WidgetVisitor & visitor)
{
  const WalkControl control = visitor(widget, parent, depth);
  if (control == WalkControl::Stop) {
    return false;
  }
  if (control == WalkControl::SkipChildren || depth > 64) {
    return true;
  }
  for (const Widget & child : widget.children) {
    if (!walk_widgets_impl(child, &widget, depth + 1, visitor)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool
walk_widgets(const Widget & root, const WidgetVisitor & visitor)
{
  return walk_widgets_impl(root, nullptr, 0, visitor);
}

const Widget *
UiModel::find(std::string_view id) const
{
  const Widget * found = nullptr;
  walk_widgets(root, [&](const Widget & widget, const Widget *, std::size_t) {
    if (widget.id == id) {
      found = &widget;
      return WalkControl::Stop;
    }
    return WalkControl::Continue;
  });
  return found;
}

Widget *
UiModel::find(std::string_view id)
{
  return const_cast<Widget *>(static_cast<const UiModel &>(*this).find(id));
}

bool
is_valid_widget_id(std::string_view id)
{
  if (id.empty() || id.size() > 128) {
    return false;
  }
  const auto is_alpha = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  if (!is_alpha(id[0])) {
    return false;
  }
  for (char c : id) {
    if (!is_alpha(c) && !(c >= '0' && c <= '9') && c != '.' && c != '-') {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

namespace
{

class Validator
{
public:
  Validator(const MessageDescription * schema, std::vector<UiProblem> & problems)
  : schema_(schema), problems_(problems) {}

  void run(const UiModel & model)
  {
    if (model.root.type != WidgetType::Panel) {
      error(model.root.id, "root widget must be a panel");
    }
    if (!model.metadata.is_null() && !model.metadata.is_object()) {
      error("", "metadata must be an object or null");
    }
    if (!model.initial_data.is_null() && !model.initial_data.is_object()) {
      error("", "initial_data must be an object or null");
    }
    walk_widgets(model.root, [this](const Widget & widget, const Widget * parent, std::size_t depth) {
      check(widget, parent, depth);
      return WalkControl::Continue;
    });
  }

private:
  void error(const std::string & id, std::string message)
  {
    problems_.push_back({ProblemSeverity::Error, id, std::move(message)});
  }

  void warning(const std::string & id, std::string message)
  {
    problems_.push_back({ProblemSeverity::Warning, id, std::move(message)});
  }

  void check(const Widget & w, const Widget * parent, std::size_t depth)
  {
    const std::string & id = w.id;
    const std::string type_name = to_string(w.type);

    if (!is_valid_widget_id(id)) {
      error(id, "invalid widget id '" + id + "' (want [A-Za-z_][A-Za-z0-9_.-]*, <= 128 bytes)");
    } else if (!seen_ids_.insert(id).second) {
      error(id, "duplicate widget id '" + id + "'");
    }
    if (w.type == WidgetType::Panel && parent != nullptr) {
      error(id, "a panel may only be the root");
    }
    if (depth > 32) {
      error(id, "widget tree deeper than 32 levels");
    }
    if (!is_container(w.type) && !w.children.empty()) {
      error(id, type_name + " cannot have children");
    }
    if (is_container(w.type) && !w.binding.empty()) {
      error(id, type_name + " cannot have a binding");
    }
    if (w.editable && !is_input(w.type)) {
      error(id, type_name + " cannot be editable");
    }
    if (w.type == WidgetType::Label && w.label.empty()) {
      warning(id, "label has no text");
    }
    if (w.min && w.max && *w.min > *w.max) {
      error(id, "min is greater than max");
    }
    if (w.step && !(*w.step > 0.0)) {
      error(id, "step must be positive");
    }
    if (w.precision && (*w.precision < 0 || *w.precision > 17)) {
      error(id, "precision must be within 0..17");
    }
    if (!w.action.empty() && w.action.size() > 256) {
      error(id, "action id longer than 256 bytes");
    }
    if (w.confirm && w.type != WidgetType::Button) {
      warning(id, "confirm is only honoured by buttons");
    }

    // Binding syntax; required for every bindable type except the inspector
    // (empty = whole document) and the button (may have none).
    FieldPath path;
    bool has_path = false;
    if (is_bindable(w.type)) {
      std::string path_error;
      if (!FieldPath::parse(w.binding, path, path_error)) {
        error(id, "invalid binding: " + path_error);
      } else if (path.empty()) {
        if (w.type != WidgetType::JsonInspector && w.type != WidgetType::Button) {
          error(id, type_name + " requires a binding");
        }
      } else {
        has_path = true;
      }
    }

    switch (w.type) {
      case WidgetType::Button:
        if (w.action.empty()) {
          error(id, "button requires an action");
        }
        break;
      case WidgetType::Slider:
        if (!w.min || !w.max) {
          error(id, "slider requires min and max");
        }
        break;
      case WidgetType::Gauge:
        if (!w.min || !w.max) {
          error(id, "gauge requires min and max");
        }
        break;
      case WidgetType::Choice:
        if (w.choices.empty()) {
          error(id, "choice requires at least one option");
        }
        for (std::size_t i = 0; i < w.choices.size(); ++i) {
          if (w.choices[i].label.empty()) {
            warning(id, "choice option " + std::to_string(i) + " has no label");
          }
          if (!w.choices[i].value.is_bool() && !w.choices[i].value.is_number() &&
            !w.choices[i].value.is_string())
          {
            error(id, "choice option " + std::to_string(i) + " must be a scalar");
          }
          for (std::size_t j = 0; j < i; ++j) {
            if (w.choices[j].value == w.choices[i].value) {
              error(id, "choice options " + std::to_string(j) + " and " + std::to_string(i) +
                " have the same value");
              break;
            }
          }
        }
        break;
      case WidgetType::Table:
        if (w.columns.empty()) {
          error(id, "table requires at least one column");
        }
        for (std::size_t i = 0; i < w.columns.size(); ++i) {
          FieldPath column_path;
          std::string column_error;
          if (!FieldPath::parse(w.columns[i].path, column_path, column_error)) {
            error(id, "column " + std::to_string(i) + ": " + column_error);
          }
          if (w.columns[i].precision && (*w.columns[i].precision < 0 || *w.columns[i].precision > 17)) {
            error(id, "column " + std::to_string(i) + ": precision must be within 0..17");
          }
        }
        break;
      case WidgetType::TimeSeries:
        if (!(w.window_seconds > 0.0) || !std::isfinite(w.window_seconds)) {
          error(id, "chart window_seconds must be positive");
        }
        if (w.max_points < 2) {
          error(id, "chart max_points must be at least 2");
        }
        break;
      case WidgetType::Vector3:
        if (!w.components.empty() && w.components.size() != 3) {
          error(id, "vector3 needs exactly three component names");
        }
        for (const std::string & component : w.components) {
          FieldPath component_path;
          std::string component_error;
          if (!FieldPath::parse(component, component_path, component_error) ||
            component_path.segments.size() != 1 || component_path.segments[0].index)
          {
            error(id, "vector3 component '" + component + "' is not a field name");
          }
        }
        break;
      default:
        break;
    }
    for (std::size_t i = 0; i < w.rules.size(); ++i) {
      const StatusRule & rule = w.rules[i];
      if (rule.equals.is_null() && !rule.min && !rule.max) {
        warning(id, "rule " + std::to_string(i) + " matches everything (no equals/min/max)");
      }
      if (rule.min && rule.max && *rule.min > *rule.max) {
        error(id, "rule " + std::to_string(i) + ": min is greater than max");
      }
    }
    if (is_input(w.type) && w.editable && w.action.empty()) {
      warning(id, type_name + " is editable but has no action; the server must map it by id");
    }

    if (schema_ != nullptr && has_path) {
      check_against_schema(w, path);
    }
  }

  void check_against_schema(const Widget & w, const FieldPath & path)
  {
    const std::string & id = w.id;
    ResolvedField resolved;
    std::string resolve_error;
    if (!resolve_field_path(*schema_, path, resolved, resolve_error)) {
      error(id, "binding does not resolve: " + resolve_error);
      return;
    }
    const FieldDescription & field = *resolved.field;
    const bool array_value = resolved.is_array_value();
    const FieldKind kind = field.kind;
    const std::string type_name = to_string(w.type);
    const std::string field_desc = "'" + path.to_string() + "' (" + std::string(to_string(kind)) +
      (array_value ? "[]" : "") + ")";

    const auto require_scalar_number = [&]() {
      if (array_value || !is_numeric(kind)) {
        error(id, type_name + " must bind a number, not " + field_desc);
      }
    };
    const auto require_message_with = [&](const std::vector<std::string> & names) {
      if (array_value || kind != FieldKind::Message || !field.message) {
        error(id, type_name + " must bind a message, not " + field_desc);
        return;
      }
      for (const std::string & name : names) {
        const FieldDescription * member = field.message->find_field(name);
        if (member == nullptr || !is_numeric(member->kind) || member->is_array()) {
          error(id, type_name + " needs a numeric field '" + name + "' in " + field.message->type_name);
        }
      }
    };

    switch (w.type) {
      case WidgetType::Toggle:
        if (array_value || kind != FieldKind::Bool) {
          error(id, "toggle must bind a bool, not " + field_desc);
        }
        break;
      case WidgetType::NumberInput:
      case WidgetType::Slider:
      case WidgetType::Gauge:
      case WidgetType::TimeSeries:
        require_scalar_number();
        if (w.integer && is_floating(kind)) {
          warning(id, "integer semantics on a floating-point field " + field_desc);
        }
        if (const auto bounds = numeric_bounds(kind)) {
          if ((w.min && *w.min < bounds->min) || (w.max && *w.max > bounds->max)) {
            warning(id, "range exceeds what " + std::string(to_string(kind)) + " can hold");
          }
        }
        break;
      case WidgetType::TextInput:
        if (array_value || !is_string(kind)) {
          error(id, "text input must bind a string, not " + field_desc);
        }
        break;
      case WidgetType::Image:
        if (array_value || !is_string(kind)) {
          error(id, "image must bind a string URL, not " + field_desc);
        }
        break;
      case WidgetType::ValueDisplay:
      case WidgetType::StatusLight:
      case WidgetType::Choice:
        if (array_value || kind == FieldKind::Message) {
          error(id, type_name + " must bind a scalar, not " + field_desc);
        }
        break;
      case WidgetType::Vector3: {
        std::vector<std::string> names = w.components;
        if (names.empty()) {
          names = {"x", "y", "z"};
        }
        require_message_with(names);
        break;
      }
      case WidgetType::Quaternion:
        require_message_with({"x", "y", "z", "w"});
        break;
      case WidgetType::Table:
        if (!array_value) {
          error(id, "table must bind an array, not " + field_desc);
          break;
        }
        for (std::size_t i = 0; i < w.columns.size(); ++i) {
          const TableColumn & column = w.columns[i];
          if (kind == FieldKind::Message && field.message) {
            if (column.path.empty()) {
              error(id, "column " + std::to_string(i) + " needs a path into " + field.message->type_name);
              continue;
            }
            ResolvedField column_resolved;
            std::string column_error;
            if (!resolve_field_path(*field.message, column.path, column_resolved, column_error)) {
              error(id, "column " + std::to_string(i) + ": " + column_error);
            }
          } else if (!column.path.empty()) {
            error(id, "column " + std::to_string(i) + ": elements of " + field_desc +
              " are scalars; use an empty path");
          }
        }
        break;
      default:
        break;
    }
  }

  const MessageDescription * schema_;
  std::vector<UiProblem> & problems_;
  std::set<std::string> seen_ids_;
};

}  // namespace

std::vector<UiProblem>
validate(const UiModel & model, const MessageDescription * schema)
{
  std::vector<UiProblem> problems;
  Validator validator(schema, problems);
  validator.run(model);
  return problems;
}

bool
is_valid(const std::vector<UiProblem> & problems)
{
  for (const UiProblem & problem : problems) {
    if (problem.severity == ProblemSeverity::Error) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

UiValue
to_value(const Widget & w)
{
  // Only non-default members are written: the page embeds this, so it should
  // be compact, and from_value restores the defaults.
  UiValue out = UiValue::object();
  out.set("id", w.id);
  out.set("type", to_string(w.type));
  if (!w.label.empty()) {
    out.set("label", w.label);
  }
  if (!w.description.empty()) {
    out.set("description", w.description);
  }
  if (!w.binding.empty()) {
    out.set("bind", w.binding);
  }
  if (!w.unit.empty()) {
    out.set("unit", w.unit);
  }
  if (w.min) {
    out.set("min", *w.min);
  }
  if (w.max) {
    out.set("max", *w.max);
  }
  if (w.step) {
    out.set("step", *w.step);
  }
  if (w.precision) {
    out.set("precision", *w.precision);
  }
  if (w.editable) {
    out.set("editable", true);
  }
  if (!w.action.empty()) {
    out.set("action", w.action);
  }
  if (!w.payload.is_null()) {
    out.set("payload", w.payload);
  }
  if (w.confirm) {
    out.set("confirm", true);
  }
  if (w.integer) {
    out.set("integer", true);
  }
  if (w.max_length) {
    out.set("max_length", static_cast<std::int64_t>(*w.max_length));
  }
  if (!w.choices.empty()) {
    UiValue choices = UiValue::array();
    for (const ChoiceOption & option : w.choices) {
      UiValue entry = UiValue::object();
      entry.set("label", option.label);
      entry.set("value", option.value);
      choices.push_back(std::move(entry));
    }
    out.set("choices", std::move(choices));
  }
  if (!w.columns.empty()) {
    UiValue columns = UiValue::array();
    for (const TableColumn & column : w.columns) {
      UiValue entry = UiValue::object();
      entry.set("header", column.header);
      entry.set("path", column.path);
      if (!column.unit.empty()) {
        entry.set("unit", column.unit);
      }
      if (column.precision) {
        entry.set("precision", *column.precision);
      }
      columns.push_back(std::move(entry));
    }
    out.set("columns", std::move(columns));
  }
  if (w.max_rows != 0) {
    out.set("max_rows", static_cast<std::int64_t>(w.max_rows));
  }
  if (!w.rules.empty()) {
    UiValue rules = UiValue::array();
    for (const StatusRule & rule : w.rules) {
      UiValue entry = UiValue::object();
      if (!rule.equals.is_null()) {
        entry.set("equals", rule.equals);
      }
      if (rule.min) {
        entry.set("min", *rule.min);
      }
      if (rule.max) {
        entry.set("max", *rule.max);
      }
      entry.set("level", to_string(rule.level));
      if (!rule.text.empty()) {
        entry.set("text", rule.text);
      }
      rules.push_back(std::move(entry));
    }
    out.set("rules", std::move(rules));
  }
  if (w.window_seconds != 30.0) {
    out.set("window_seconds", w.window_seconds);
  }
  if (w.max_points != 600) {
    out.set("max_points", static_cast<std::int64_t>(w.max_points));
  }
  if (!w.components.empty()) {
    UiValue components = UiValue::array();
    for (const std::string & component : w.components) {
      components.push_back(component);
    }
    out.set("components", std::move(components));
  }
  if (w.quaternion_display != QuaternionDisplay::Both) {
    out.set("quaternion_display", to_string(w.quaternion_display));
  }
  if (!w.children.empty()) {
    UiValue children = UiValue::array();
    for (const Widget & child : w.children) {
      children.push_back(to_value(child));
    }
    out.set("children", std::move(children));
  }
  return out;
}

UiValue
to_value(const UiModel & model)
{
  UiValue out = UiValue::object();
  out.set("version", static_cast<std::int64_t>(UiModel::kModelVersion));
  out.set("root", to_value(model.root));
  if (!model.metadata.is_null()) {
    out.set("metadata", model.metadata);
  }
  if (!model.initial_data.is_null()) {
    out.set("data", model.initial_data);
  }
  return out;
}

namespace
{

bool
read_optional_double(const UiValue & object, const char * key, std::optional<double> & out,
  const std::string & where, std::string & error)
{
  const UiValue * v = object.find(key);
  if (v == nullptr || v->is_null()) {
    out.reset();
    return true;
  }
  if (!v->is_number()) {
    error = where + ": '" + key + "' must be a number";
    return false;
  }
  out = v->as_double();
  return true;
}

bool
read_string(const UiValue & object, const char * key, std::string & out,
  const std::string & where, std::string & error)
{
  const UiValue * v = object.find(key);
  if (v == nullptr || v->is_null()) {
    out.clear();
    return true;
  }
  if (!v->is_string()) {
    error = where + ": '" + key + "' must be a string";
    return false;
  }
  out = v->as_string();
  return true;
}

bool
read_bool(const UiValue & object, const char * key, bool & out,
  const std::string & where, std::string & error)
{
  const UiValue * v = object.find(key);
  if (v == nullptr || v->is_null()) {
    out = false;
    return true;
  }
  if (!v->is_bool()) {
    error = where + ": '" + key + "' must be a bool";
    return false;
  }
  out = v->as_bool();
  return true;
}

bool
from_value_impl(const UiValue & value, Widget & out, std::size_t depth, std::string & error)
{
  if (!value.is_object()) {
    error = "widget must be an object";
    return false;
  }
  if (depth > 64) {
    error = "widget tree deeper than 64 levels";
    return false;
  }
  Widget w;
  const UiValue * id = value.find("id");
  if (id == nullptr || !id->is_string()) {
    error = "widget without a string 'id'";
    return false;
  }
  w.id = id->as_string();
  const std::string where = "widget '" + w.id + "'";
  const UiValue * type = value.find("type");
  if (type == nullptr || !type->is_string() || !widget_type_from_string(type->as_string(), w.type)) {
    error = where + ": unknown or missing 'type'";
    return false;
  }
  if (!read_string(value, "label", w.label, where, error) ||
    !read_string(value, "description", w.description, where, error) ||
    !read_string(value, "bind", w.binding, where, error) ||
    !read_string(value, "unit", w.unit, where, error) ||
    !read_string(value, "action", w.action, where, error) ||
    !read_optional_double(value, "min", w.min, where, error) ||
    !read_optional_double(value, "max", w.max, where, error) ||
    !read_optional_double(value, "step", w.step, where, error) ||
    !read_bool(value, "editable", w.editable, where, error) ||
    !read_bool(value, "confirm", w.confirm, where, error) ||
    !read_bool(value, "integer", w.integer, where, error))
  {
    return false;
  }
  if (const UiValue * v = value.find("precision"); v != nullptr && !v->is_null()) {
    if (!v->is_number()) {
      error = where + ": 'precision' must be a number";
      return false;
    }
    w.precision = static_cast<int>(v->as_int());
  }
  if (const UiValue * v = value.find("payload"); v != nullptr) {
    w.payload = *v;
  }
  if (const UiValue * v = value.find("max_length"); v != nullptr && !v->is_null()) {
    if (!v->is_number() || v->as_int() < 0) {
      error = where + ": 'max_length' must be a non-negative number";
      return false;
    }
    w.max_length = static_cast<std::size_t>(v->as_int());
  }
  if (const UiValue * v = value.find("choices"); v != nullptr && !v->is_null()) {
    if (!v->is_array()) {
      error = where + ": 'choices' must be an array";
      return false;
    }
    for (const UiValue & entry : v->elements()) {
      ChoiceOption option;
      if (!entry.is_object() || !read_string(entry, "label", option.label, where, error)) {
        error = where + ": each choice must be an object with a string label";
        return false;
      }
      if (const UiValue * ov = entry.find("value"); ov != nullptr) {
        option.value = *ov;
      }
      w.choices.push_back(std::move(option));
    }
  }
  if (const UiValue * v = value.find("columns"); v != nullptr && !v->is_null()) {
    if (!v->is_array()) {
      error = where + ": 'columns' must be an array";
      return false;
    }
    for (const UiValue & entry : v->elements()) {
      TableColumn column;
      if (!entry.is_object() || !read_string(entry, "header", column.header, where, error) ||
        !read_string(entry, "path", column.path, where, error) ||
        !read_string(entry, "unit", column.unit, where, error))
      {
        error = where + ": each column must be an object with string header/path";
        return false;
      }
      if (const UiValue * pv = entry.find("precision"); pv != nullptr && pv->is_number()) {
        column.precision = static_cast<int>(pv->as_int());
      }
      w.columns.push_back(std::move(column));
    }
  }
  if (const UiValue * v = value.find("max_rows"); v != nullptr && v->is_number()) {
    w.max_rows = v->as_int() < 0 ? 0 : static_cast<std::size_t>(v->as_int());
  }
  if (const UiValue * v = value.find("rules"); v != nullptr && !v->is_null()) {
    if (!v->is_array()) {
      error = where + ": 'rules' must be an array";
      return false;
    }
    for (const UiValue & entry : v->elements()) {
      StatusRule rule;
      if (!entry.is_object()) {
        error = where + ": each rule must be an object";
        return false;
      }
      if (const UiValue * ev = entry.find("equals"); ev != nullptr) {
        rule.equals = *ev;
      }
      if (!read_optional_double(entry, "min", rule.min, where, error) ||
        !read_optional_double(entry, "max", rule.max, where, error) ||
        !read_string(entry, "text", rule.text, where, error))
      {
        return false;
      }
      std::string level;
      if (!read_string(entry, "level", level, where, error)) {
        return false;
      }
      if (!level.empty() && !status_level_from_string(level, rule.level)) {
        error = where + ": unknown rule level '" + level + "'";
        return false;
      }
      w.rules.push_back(std::move(rule));
    }
  }
  if (const UiValue * v = value.find("window_seconds"); v != nullptr && v->is_number()) {
    w.window_seconds = v->as_double();
  }
  if (const UiValue * v = value.find("max_points"); v != nullptr && v->is_number()) {
    w.max_points = v->as_int() < 0 ? 0 : static_cast<std::size_t>(v->as_int());
  }
  if (const UiValue * v = value.find("components"); v != nullptr && !v->is_null()) {
    if (!v->is_array()) {
      error = where + ": 'components' must be an array of strings";
      return false;
    }
    for (const UiValue & entry : v->elements()) {
      if (!entry.is_string()) {
        error = where + ": 'components' must be an array of strings";
        return false;
      }
      w.components.push_back(entry.as_string());
    }
  }
  std::string quaternion_display;
  if (!read_string(value, "quaternion_display", quaternion_display, where, error)) {
    return false;
  }
  if (!quaternion_display.empty() &&
    !quaternion_display_from_string(quaternion_display, w.quaternion_display))
  {
    error = where + ": unknown quaternion_display '" + quaternion_display + "'";
    return false;
  }
  if (const UiValue * v = value.find("children"); v != nullptr && !v->is_null()) {
    if (!v->is_array()) {
      error = where + ": 'children' must be an array";
      return false;
    }
    for (const UiValue & entry : v->elements()) {
      Widget child;
      if (!from_value_impl(entry, child, depth + 1, error)) {
        return false;
      }
      w.children.push_back(std::move(child));
    }
  }
  out = std::move(w);
  return true;
}

}  // namespace

bool
from_value(const UiValue & value, Widget & out_widget, std::string & out_error)
{
  return from_value_impl(value, out_widget, 0, out_error);
}

bool
from_value(const UiValue & value, UiModel & out_model, std::string & out_error)
{
  if (!value.is_object()) {
    out_error = "model must be an object";
    return false;
  }
  const UiValue * version = value.find("version");
  if (version == nullptr || !version->is_number()) {
    out_error = "model without a numeric 'version'";
    return false;
  }
  if (version->as_int() != UiModel::kModelVersion) {
    out_error = "unsupported model version " + std::to_string(version->as_int()) +
      " (this library reads " + std::to_string(UiModel::kModelVersion) + ")";
    return false;
  }
  const UiValue * root = value.find("root");
  if (root == nullptr) {
    out_error = "model without a 'root' widget";
    return false;
  }
  UiModel model;
  if (!from_value_impl(*root, model.root, 0, out_error)) {
    return false;
  }
  if (const UiValue * metadata = value.find("metadata"); metadata != nullptr) {
    model.metadata = *metadata;
  }
  if (const UiValue * data = value.find("data"); data != nullptr) {
    model.initial_data = *data;
  }
  out_model = std::move(model);
  out_error.clear();
  return true;
}

}  // namespace qar_ui_model
