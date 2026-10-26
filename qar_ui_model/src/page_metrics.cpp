// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ui_model/page_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace qar_ui_model
{

namespace
{

/// One line of text at @p font_px, including `line-height: 1.4`.
double
line(double font_px)
{
  return font_px * kPageLineHeight;
}

/// A `.qar-widget` is a column with `gap: 8px`: N parts cost N-1 gaps.
double
stack(const ArTheme & theme, std::initializer_list<double> parts)
{
  double total = 0.0;
  std::size_t count = 0;
  for (double part : parts) {
    if (part <= 0.0) {
      continue;  // An absent part (no help text, no subtitle) costs no gap.
    }
    total += part;
    ++count;
  }
  if (count > 1) {
    total += static_cast<double>(count - 1) * static_cast<double>(theme.gap_tight_px);
  }
  return total;
}

/// `.qar-comp`: padded box holding a small name over a large value. Editable
/// components put a touch-target-high field where the readout would be.
double
component_height(const ArTheme & theme, bool editable)
{
  const double value = editable
    ? static_cast<double>(theme.touch_target_px)
    : line(static_cast<double>(theme.value_font_px));
  return 2.0 * static_cast<double>(theme.pad_micro_px) +
         line(static_cast<double>(theme.small_font_px)) +
         static_cast<double>(theme.gap_micro_px) + value;
}

/// The caption every leaf but Label carries.
double
caption_height(const ArTheme & theme)
{
  return line(static_cast<double>(theme.small_font_px));
}

double
help_height(const Widget & w, const ArTheme & theme)
{
  return w.description.empty() ? 0.0 : line(static_cast<double>(theme.small_font_px));
}

/// Children of a Column / Section body / Panel body: a vertical stack with
/// `gap: var(--qar-gap)` between siblings.
double
stacked_children(const Widget & w, const ArTheme & theme, std::size_t columns)
{
  double total = 0.0;
  std::size_t counted = 0;
  for (const Widget & child : w.children) {
    const double h = measure_widget_height(child, theme, columns);
    if (h <= 0.0) {
      continue;
    }
    total += h;
    ++counted;
  }
  if (counted > 1) {
    total += static_cast<double>(counted - 1) * static_cast<double>(theme.gap_px);
  }
  return total;
}

/// A Row is horizontal and wraps: its children share @p columns columns, and
/// each wrapped line is as tall as the tallest child in the row. With
/// `columns == 1` the row IS a column, which is what a narrow viewport gets.
double
row_height(const Widget & w, const ArTheme & theme, std::size_t columns)
{
  double tallest = 0.0;
  std::size_t counted = 0;
  for (const Widget & child : w.children) {
    tallest = std::max(tallest, measure_widget_height(child, theme, columns));
    ++counted;
  }
  if (counted == 0) {
    return 0.0;
  }
  const std::size_t lines = (counted + columns - 1) / columns;
  return static_cast<double>(lines) * tallest +
         static_cast<double>(lines - 1) * static_cast<double>(theme.gap_px);
}

/// `.qar-panel`'s header: the title block (and subtitle) beside the connection
/// chip. They sit side by side, so the header is the taller of the two.
double
header_height(const Widget & panel, const ArTheme & theme)
{
  double heading = line(static_cast<double>(theme.title_font_px));
  if (!panel.description.empty()) {
    // `.qar-subtitle { margin: calc(var(--qar-gap-tight) / 2) 0 0 }`.
    heading += 0.5 * static_cast<double>(theme.gap_tight_px) +
      line(static_cast<double>(theme.small_font_px));
  }
  return std::max(heading, static_cast<double>(theme.chip_px));
}

/// The widest row in the tree, before the column cap.
void
scan(
  const Widget & w, std::size_t & widgets, std::size_t & sections, std::size_t & widest_row)
{
  switch (w.type) {
    case WidgetType::Panel:
      break;
    case WidgetType::Section:
      ++sections;
      break;
    case WidgetType::Row:
      widest_row = std::max(widest_row, w.children.size());
      break;
    case WidgetType::Column:
      break;
    default:
      ++widgets;
      break;
  }
  for (const Widget & child : w.children) {
    scan(child, widgets, sections, widest_row);
  }
}

bool
uses_sections(const Widget & w)
{
  if (w.type == WidgetType::Section) {
    return true;
  }
  for (const Widget & child : w.children) {
    if (uses_sections(child)) {
      return true;
    }
  }
  return false;
}

}  // namespace

double
measure_widget_height(const Widget & widget, const ArTheme & theme, std::size_t max_columns)
{
  const std::size_t columns = std::min(std::max<std::size_t>(max_columns, 1), kMaxRowColumns);
  const double small = line(static_cast<double>(theme.small_font_px));
  const double body = line(static_cast<double>(theme.base_font_px));
  const double value = line(static_cast<double>(theme.value_font_px));
  const double touch = static_cast<double>(theme.touch_target_px);
  const double cap = caption_height(theme);
  const double help = help_height(widget, theme);

  switch (widget.type) {
    case WidgetType::Panel: {
      // `padding: var(--qar-pad)` top and bottom, header, then the body. The
      // footer status line is `:empty { display: none }` at rest, so it is not
      // counted: it appears only while a message is showing.
      const double body_children = stacked_children(widget, theme, columns);
      double total = 2.0 * static_cast<double>(theme.padding_px) +
        header_height(widget, theme);
      if (body_children > 0.0) {
        total += static_cast<double>(theme.gap_px) + body_children;
      }
      return total;
    }
    case WidgetType::Section: {
      const double children = stacked_children(widget, theme, columns);
      double total = 2.0 * static_cast<double>(theme.padding_px);
      const double title =
        widget.label.empty() ? 0.0 : line(static_cast<double>(theme.heading_font_px));
      total += title;
      if (children > 0.0) {
        total += (title > 0.0 ? static_cast<double>(theme.gap_px) : 0.0) + children;
      }
      return total;
    }
    case WidgetType::Row:
      return row_height(widget, theme, columns);
    case WidgetType::Column:
      return stacked_children(widget, theme, columns);

    case WidgetType::Label:
      // No caption: the text IS the widget.
      return body;
    case WidgetType::ValueDisplay:
      // `.qar-readout { min-height: calc(var(--qar-font-value) * 1.4) }`.
      return stack(theme, {cap, value, help});
    case WidgetType::NumberInput:
    case WidgetType::TextInput:
    case WidgetType::Toggle:
    case WidgetType::Slider:
    case WidgetType::Choice:
    case WidgetType::StatusLight:
      // One touch-target-high control under its caption. Choice chips and
      // Toggle carry their own label inside the control, not above it, but
      // both still render the caption row.
      return stack(theme, {cap, touch, help});
    case WidgetType::Button:
      // `.qar-action { min-height: calc(var(--qar-touch) + 8px) }` plus the
      // acknowledgement line, which reserves `min-height: 1.4em` at all times.
      return stack(theme, {cap, touch + static_cast<double>(theme.gap_tight_px), small, help});
    case WidgetType::Vector3: {
      // `render_vector3` puts the unit on its own `.qar-readout` line BELOW the
      // component boxes whenever the widget carries one. Leaving it out made
      // every united vector measure a row short, and a panel sized a row short
      // is a panel that pages content the operator expected to see.
      const double unit_row = widget.unit.empty() ? 0.0 : small;
      return stack(theme, {cap, component_height(theme, widget.editable), unit_row, help});
    }
    case WidgetType::Quaternion: {
      const double one = component_height(theme, widget.editable);
      const double rows =
        widget.quaternion_display == QuaternionDisplay::Both
        ? 2.0 * one + static_cast<double>(theme.gap_px)
        : one;
      // Euler always carries its "degrees, roll-pitch-yaw (ZYX)" readout.
      const double unit_row =
        widget.quaternion_display == QuaternionDisplay::Quaternion ? 0.0 : small;
      return stack(theme, {cap, rows, unit_row, help});
    }
    case WidgetType::Table: {
      const std::size_t rows =
        widget.max_rows > 0 ? widget.max_rows : static_cast<std::size_t>(theme.table_page_rows);
      const double grid =
        static_cast<double>(rows + 1) * static_cast<double>(theme.table_row_px);
      return stack(theme, {cap, grid, touch, help});  // header + rows, then the pager.
    }
    case WidgetType::Gauge:
      return stack(theme, {cap, static_cast<double>(theme.dot_px), small, help});  // track, then the bounds row.
    case WidgetType::TimeSeries:
      return stack(theme, {cap, static_cast<double>(theme.chart_height_px), small, help});
    case WidgetType::Image:
      return stack(theme, {cap, static_cast<double>(theme.block_px), help});
    case WidgetType::JsonInspector:
      return stack(theme, {cap, kJsonInspectorBlocks * static_cast<double>(theme.block_px), help});
  }
  return stack(theme, {cap, body, help});
}

PageMetrics
measure_page(const UiModel & model, const ArTheme & theme, std::size_t max_columns)
{
  PageMetrics out;
  scan(model.root, out.widget_count, out.section_count, out.widest_row);
  const std::size_t cap = std::min(std::max<std::size_t>(max_columns, 1), kMaxRowColumns);

  // --- Width ---------------------------------------------------------------
  // The comfortable text measure, and the widest row the CSS keeps on one
  // line; see the header for why 60 characters and why the column cap.
  const double measure =
    kComfortableLineChars * kAverageCharAdvanceEm * static_cast<double>(theme.base_font_px);
  const std::size_t columns = std::min(std::max<std::size_t>(out.widest_row, 1), cap);
  out.columns = columns;
  const double row_demand = static_cast<double>(columns) * static_cast<double>(theme.row_basis_px) +
    static_cast<double>(columns - 1) * static_cast<double>(theme.gap_px);
  double content = std::max(measure, row_demand);

  // Chrome around that column: the panel's own padding always, and one
  // section's padding when the model uses sections (the generated ones all do,
  // and sections never nest).
  content += 2.0 * static_cast<double>(theme.padding_px);
  if (uses_sections(model.root)) {
    content += 2.0 * static_cast<double>(theme.padding_px);
  }

  out.width_px = std::max(1.0, std::round(content));
  out.height_px = std::max(1.0, std::round(measure_widget_height(model.root, theme, cap)));
  out.aspect = out.width_px / out.height_px;
  return out;
}

}  // namespace qar_ui_model
