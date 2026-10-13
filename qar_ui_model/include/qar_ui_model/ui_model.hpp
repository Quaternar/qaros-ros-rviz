// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__UI_MODEL_HPP_
#define QAR_UI_MODEL__UI_MODEL_HPP_

/// @file ui_model.hpp
/// @brief Declarative widget tree for one AR panel.
///
/// A panel is a tree of @ref Widget: containers (panel, section, row, column)
/// and leaves (displays and controls). Every leaf that shows or edits data
/// carries a BINDING — a dotted path (see type_description.hpp) into the
/// panel's single data document, a @ref UiValue that the server keeps current
/// over the wire protocol. Controls that do something carry an ACTION id, an
/// opaque string the server maps to a ROS publish / service call / action goal.
///
/// The model is deliberately one flat struct with a type tag rather than a
/// class hierarchy: it has to be serialized to JSON for the page's runtime,
/// diffed in tests, and built by generators that are easier to write
/// against fields than against virtual methods. @ref validate is what keeps a
/// fat struct honest — every type-specific rule lives there, and a generated
/// model that breaks one fails in a test, not in a headset.
///
/// Widget ids are stable, unique per panel and restricted to
/// `[A-Za-z_][A-Za-z0-9_.-]*`: they become DOM ids and wire identifiers.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_value.hpp"

namespace qar_ui_model
{

enum class WidgetType : std::uint8_t
{
  Panel = 0,     ///< Root only. label = title, description = subtitle.
  Section,       ///< Titled group.
  Row,           ///< Children laid out horizontally (wrapping).
  Column,        ///< Children laid out vertically.
  Label,         ///< Static text.
  ValueDisplay,  ///< Read-only scalar with unit and precision.
  NumberInput,   ///< Editable number: stepper buttons + keyboard field.
  TextInput,     ///< Editable string with an explicit send.
  Toggle,        ///< Editable bool.
  Slider,        ///< Editable number within [min, max].
  Choice,        ///< One of N values, shown as large segmented buttons.
  Button,        ///< Fires an action, optionally with a payload.
  Vector3,       ///< {x, y, z} display or input.
  Quaternion,    ///< {x, y, z, w} display or input, with roll/pitch/yaw.
  Table,         ///< Array of rows, paged.
  Gauge,         ///< Number within [min, max] as a bar, colored by rules.
  StatusLight,   ///< Value -> (level, text) via rules.
  TimeSeries,    ///< Scrolling line chart of a number.
  Image,         ///< Bound string URL (data: or http(s):).
  JsonInspector  ///< Pretty-printed subtree; empty binding = whole document.
};

/// @brief Wire/CSS spelling: "panel", "section", ..., "json".
const char * to_string(WidgetType type);
bool widget_type_from_string(std::string_view text, WidgetType & out_type);

bool is_container(WidgetType type);  ///< Panel, Section, Row, Column
bool is_input(WidgetType type);      ///< The editable kinds.
bool is_bindable(WidgetType type);   ///< Everything but containers and Label.

enum class StatusLevel : std::uint8_t
{
  Neutral = 0,
  Ok,
  Warning,
  Error,
  Stale
};
const char * to_string(StatusLevel level);
bool status_level_from_string(std::string_view text, StatusLevel & out_level);

/// @brief One option of a Choice.
struct ChoiceOption
{
  std::string label;
  UiValue value;
};

/// @brief Maps a value to a status level. `equals` matches exactly (Null =
/// unused); otherwise a numeric value must satisfy min <= v <= max (each
/// optional). Rules are tried in order; the first match wins.
struct StatusRule
{
  UiValue equals;
  std::optional<double> min;
  std::optional<double> max;
  StatusLevel level = StatusLevel::Neutral;
  std::string text;  ///< Shown next to the light; empty = the value itself.
};

/// @brief One column of a Table. `path` is relative to the row element; empty
/// means the row value itself (an array of scalars).
struct TableColumn
{
  std::string header;
  std::string path;
  std::string unit;
  std::optional<int> precision;
};

enum class QuaternionDisplay : std::uint8_t
{
  Both = 0,    ///< x y z w and roll/pitch/yaw.
  Quaternion,
  Euler
};
const char * to_string(QuaternionDisplay display);
bool quaternion_display_from_string(std::string_view text, QuaternionDisplay & out);

struct Widget
{
  std::string id;
  WidgetType type = WidgetType::Label;
  std::string label;
  std::string description;  ///< Subtitle (Panel/Section) or help text (leaves).
  std::string binding;      ///< Dotted path into the data document. See is_bindable.
  std::string unit;
  std::optional<double> min;
  std::optional<double> max;
  std::optional<double> step;
  std::optional<int> precision;  ///< Decimals shown. Default: 3 for floats.
  bool editable = false;         ///< Honoured by is_input types only.
  std::string action;            ///< Sent with control events. Required for Button.
  UiValue payload;               ///< Button: sent as the event value.
  bool confirm = false;          ///< Button: press twice within a short window.
  bool integer = false;          ///< NumberInput/Slider: integer semantics.
  std::optional<std::size_t> max_length;   ///< TextInput.
  std::vector<ChoiceOption> choices;       ///< Choice.
  std::vector<TableColumn> columns;        ///< Table.
  std::size_t max_rows = 0;                ///< Table page size; 0 = theme default.
  std::vector<StatusRule> rules;           ///< StatusLight, Gauge.
  double window_seconds = 30.0;            ///< TimeSeries.
  std::size_t max_points = 600;            ///< TimeSeries.
  std::vector<std::string> components;     ///< Vector3; empty = x, y, z.
  QuaternionDisplay quaternion_display = QuaternionDisplay::Both;
  std::vector<Widget> children;            ///< Containers only.

  // Chainable setters, for generators and tests.
  Widget & with_binding(std::string path);
  Widget & with_label(std::string text);
  Widget & with_description(std::string text);
  Widget & with_unit(std::string text);
  Widget & with_range(double min_value, double max_value);
  Widget & with_step(double step_value);
  Widget & with_precision(int decimals);
  Widget & with_editable(bool value);
  Widget & with_action(std::string action_id);
  Widget & with_payload(UiValue value);
  Widget & with_confirm(bool value = true);
  Widget & with_integer(bool value = true);
  Widget & with_rule(StatusRule rule);
  Widget & with_choice(std::string label, UiValue value);
  Widget & with_column(TableColumn column);
  Widget & add(Widget child);

  /// label, else the humanized last binding segment, else the id.
  std::string display_label() const;
};

/// @brief Factories with the right defaults per type (inputs start editable).
namespace widgets
{
Widget panel(std::string id, std::string title);
Widget section(std::string id, std::string label);
Widget row(std::string id);
Widget column(std::string id);
Widget label(std::string id, std::string text);
Widget value(std::string id, std::string binding, std::string label = {});
Widget number(std::string id, std::string binding, std::string label = {});
Widget text_input(std::string id, std::string binding, std::string label = {});
Widget toggle(std::string id, std::string binding, std::string label = {});
Widget slider(std::string id, std::string binding, std::string label, double min, double max);
Widget choice(std::string id, std::string binding, std::string label, std::vector<ChoiceOption> options);
Widget button(std::string id, std::string label, std::string action);
Widget vector3(std::string id, std::string binding, std::string label = {});
Widget quaternion(std::string id, std::string binding, std::string label = {});
Widget table(std::string id, std::string binding, std::string label, std::vector<TableColumn> columns);
Widget gauge(std::string id, std::string binding, std::string label, double min, double max);
Widget status_light(std::string id, std::string binding, std::string label = {});
Widget time_series(std::string id, std::string binding, std::string label = {});
Widget image(std::string id, std::string binding, std::string label = {});
Widget json_inspector(std::string id, std::string binding = {});
}  // namespace widgets

/// @brief One panel: the widget tree plus the data it starts with.
struct UiModel
{
  /// Serialized-shape version, bumped when the JSON layout of this struct
  /// changes incompatibly. Independent of the wire protocol version.
  static constexpr int kModelVersion = 1;

  Widget root;            ///< Must be a Panel; the only Panel in the tree.
  UiValue initial_data;   ///< Rendered into the page so it is never blank. May be Null.
  UiValue metadata;       ///< Free-form: topic names, type names, generator info.

  const Widget * find(std::string_view id) const;
  Widget * find(std::string_view id);
};

/// @brief Widget traversal: depth-first, children in order.
using WidgetVisitor =
  std::function<WalkControl(const Widget & widget, const Widget * parent, std::size_t depth)>;
bool walk_widgets(const Widget & root, const WidgetVisitor & visitor);

/// @brief Ids are `[A-Za-z_][A-Za-z0-9_.-]*`, at most 128 bytes.
bool is_valid_widget_id(std::string_view id);

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

enum class ProblemSeverity
{
  Warning,  ///< Renders, but probably not what was meant.
  Error     ///< Refused by the renderer.
};
const char * to_string(ProblemSeverity severity);

struct UiProblem
{
  ProblemSeverity severity = ProblemSeverity::Error;
  std::string widget_id;  ///< Empty for model-level problems.
  std::string message;
};

/// @brief Every structural rule, plus binding/kind checks against @p schema
/// when one is given (the data document's description; nullptr = syntax only).
std::vector<UiProblem> validate(const UiModel & model, const MessageDescription * schema = nullptr);

/// @brief No Error-severity problems.
bool is_valid(const std::vector<UiProblem> & problems);

// ---------------------------------------------------------------------------
// Serialization (the shape the page runtime reads)
// ---------------------------------------------------------------------------

UiValue to_value(const Widget & widget);
UiValue to_value(const UiModel & model);
bool from_value(const UiValue & value, Widget & out_widget, std::string & out_error);
bool from_value(const UiValue & value, UiModel & out_model, std::string & out_error);

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__UI_MODEL_HPP_
