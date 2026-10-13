// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for the schema of qar_ui_model: the builder of a description, paths and the checks of a value.
// The functions are compiled and linked and never run.

#include "qar_ui_model/type_description.hpp"

#include <cstdio>
#include <string>

namespace ui = qar_ui_model;

ui::MessageDescriptionPtr snippet_schema()
{
  // [qar-snippet:schema]
  ui::MessageDescriptionBuilder position("geometry_msgs/msg/Point");
  position.field(ui::FieldDescription::primitive("x", ui::FieldKind::Float64).with_unit("m"))
    .field(ui::FieldDescription::primitive("y", ui::FieldKind::Float64).with_unit("m"))
    .field(ui::FieldDescription::primitive("z", ui::FieldKind::Float64).with_unit("m"));
  const ui::MessageDescriptionPtr point = position.build();     // immutable, and shared by every message that embeds it

  ui::MessageDescriptionBuilder battery("sensor_msgs/msg/BatteryState");
  battery.description("State of a battery")
    .field(ui::FieldDescription::primitive("percentage", ui::FieldKind::Float32).with_range(0.0, 1.0))
    .field(ui::FieldDescription::primitive("present", ui::FieldKind::Bool))
    .field(ui::FieldDescription::primitive("cell_voltage", ui::FieldKind::Float32).unbounded_array().with_unit("V"))
    .field(ui::FieldDescription::primitive("serial", ui::FieldKind::String).string_bound(16))   // string<=16
    .field(ui::FieldDescription::nested("position", point))
    .constant("POWER_SUPPLY_STATUS_CHARGING", ui::FieldKind::UInt8, 1);
  // [qar-snippet-end:schema]
  return battery.build();
}

void snippet_paths(const ui::MessageDescription & schema)
{
  // [qar-snippet:paths]
  ui::ResolvedField resolved;
  std::string error;
  if (!ui::resolve_field_path(schema, "cell_voltage[2]", resolved, error)) {   // an array has to be indexed before it is entered
    std::printf("%s\n", error.c_str());
  }

  ui::UiValue document = ui::make_default_value(schema);   // every field at its default: the IDL default, else zero
  ui::FieldPath path;
  if (ui::FieldPath::parse("position.x", path, error)) {
    ui::set_path(document, path, 1.5, error);              // creates what is missing; a kind clash fails and changes nothing
  }
  for (const ui::ValueProblem & problem : ui::validate_value(schema, document)) {   // kinds, ranges, lengths, unknown keys
    std::printf("%s: %s\n", problem.path.c_str(), problem.message.c_str());
  }
  // [qar-snippet-end:paths]
}
