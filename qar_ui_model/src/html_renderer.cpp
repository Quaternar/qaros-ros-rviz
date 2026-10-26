// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ui_model/html_renderer.hpp"

#include <string_view>

#include "qar_ui_model/html_escape.hpp"
#include "qar_ui_model/json.hpp"
#include "qar_ui_model/scroll_paging.hpp"
#include "qar_ui_model/ui_protocol.hpp"
#include "qar_ui_model/viewport_fit.hpp"

namespace qar_ui_model
{

namespace
{

// Every attribute value and text node goes through here; there is no other
// way to get a string into the markup in this file.
void
text(std::string & out, std::string_view value)
{
  html_escape_to(out, value);
}

void
attr(std::string & out, const char * name, std::string_view value)
{
  out += ' ';
  out += name;
  out += "=\"";
  html_escape_to(out, value);
  out += '"';
}

void
attr_number(std::string & out, const char * name, double value)
{
  attr(out, name, json_format_double(value));
}

// Common opening tag: class, then the three data attributes, in this order. A widget with a
// visibility binding starts hidden; the runtime shows it once the document says so.
void
open_widget(std::string & out, const Widget & w, const char * tag, const std::string & classes,
  bool with_binding = true)
{
  out += '<';
  out += tag;
  attr(out, "class", classes);
  attr(out, "data-qar-id", w.id);
  attr(out, "data-qar-type", to_string(w.type));
  if (with_binding && !w.binding.empty()) {
    attr(out, "data-qar-bind", w.binding);
  }
  if (!w.visible_binding.empty()) {
    out += " style=\"display:none\"";
  }
  out += '>';
}

void
close_tag(std::string & out, const char * tag)
{
  out += "</";
  out += tag;
  out += '>';
}

// A caption the document can replace carries a role, so the runtime finds it (a button's text
// already has one).
void
caption(std::string & out, const Widget & w)
{
  out += "<div class=\"qar-caption\"";
  if (!w.label_binding.empty()) {
    out += " data-qar-role=\"caption\"";
  }
  out += '>';
  text(out, w.display_label());
  out += "</div>";
}

void
help(std::string & out, const Widget & w)
{
  if (!w.description.empty()) {
    out += "<p class=\"qar-help\">";
    text(out, w.description);
    out += "</p>";
  }
}

void
unit(std::string & out, const std::string & unit_text)
{
  if (!unit_text.empty()) {
    out += "<span class=\"qar-unit\">";
    text(out, unit_text);
    out += "</span>";
  }
}

void
readout(std::string & out, const Widget & w, const char * role = "value")
{
  out += "<div class=\"qar-readout\"><span class=\"qar-readout-value\"";
  attr(out, "data-qar-role", role);
  out += ">&mdash;</span>";
  unit(out, w.unit);
  out += "</div>";
}

void
step_button(std::string & out, const char * role, const char * label, const char * glyph,
  const std::string & component = {})
{
  out += "<button type=\"button\" class=\"qar-btn qar-step\"";
  attr(out, "data-qar-role", role);
  if (!component.empty()) {
    attr(out, "data-qar-component", component);
  }
  attr(out, "aria-label", label);
  out += '>';
  out += glyph;
  out += "</button>";
}

// Stepper row: [-] [number field] [+] unit. Shared by NumberInput and the
// editable Vector3/Quaternion components (which tag every part with the
// component name so the runtime can find them).
void
stepper(std::string & out, const Widget & w, const std::string & component = {})
{
  out += "<div class=\"qar-stepper\">";
  step_button(out, "dec", "Decrease", "&minus;", component);
  out += "<input class=\"qar-field\" type=\"number\"";
  attr(out, "inputmode", w.integer ? "numeric" : "decimal");
  attr(out, "data-qar-role", "input");
  if (!component.empty()) {
    attr(out, "data-qar-component", component);
  }
  if (w.min) {
    attr_number(out, "min", *w.min);
  }
  if (w.max) {
    attr_number(out, "max", *w.max);
  }
  attr(out, "step", w.step ? json_format_double(*w.step) : "any");
  if (!w.editable) {
    out += " readonly";
  }
  out += '>';
  step_button(out, "inc", "Increase", "+", component);
  if (component.empty()) {
    unit(out, w.unit);
  }
  out += "</div>";
}

void render_widget(std::string & out, const Widget & w);

void
render_children(std::string & out, const Widget & w)
{
  for (const Widget & child : w.children) {
    render_widget(out, child);
  }
}

void
render_panel(std::string & out, const Widget & w)
{
  open_widget(out, w, "main", "qar-panel", false);
  out += "<header class=\"qar-header\"><div class=\"qar-heading\"><h1 class=\"qar-title\">";
  text(out, w.display_label());
  out += "</h1>";
  if (!w.description.empty()) {
    out += "<p class=\"qar-subtitle\">";
    text(out, w.description);
    out += "</p>";
  }
  out += "</div><div class=\"qar-conn\" id=\"qar-connection\" data-qar-state=\"offline\">"
    "<span class=\"qar-conn-dot\"></span><span class=\"qar-conn-text\" id=\"qar-connection-text\">"
    "Offline</span></div></header><div class=\"qar-panel-body\">";
  render_children(out, w);
  out += "</div><footer class=\"qar-footer\"><div class=\"qar-statusline\" id=\"qar-status\" "
    "data-qar-level=\"neutral\"></div></footer>";
  close_tag(out, "main");
}

void
render_section(std::string & out, const Widget & w)
{
  open_widget(out, w, "section", "qar-section", false);
  out += "<h2 class=\"qar-section-title\">";
  text(out, w.display_label());
  out += "</h2>";
  if (!w.description.empty()) {
    out += "<p class=\"qar-subtitle\">";
    text(out, w.description);
    out += "</p>";
  }
  out += "<div class=\"qar-section-body\">";
  render_children(out, w);
  out += "</div>";
  close_tag(out, "section");
}

void
render_container(std::string & out, const Widget & w, const char * classes)
{
  open_widget(out, w, "div", classes, false);
  render_children(out, w);
  close_tag(out, "div");
}

void
render_label(std::string & out, const Widget & w)
{
  open_widget(out, w, "p", "qar-widget qar-label", false);
  text(out, w.label);
  close_tag(out, "p");
}

void
render_value(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-value");
  caption(out, w);
  readout(out, w);
  help(out, w);
  close_tag(out, "div");
}

void
render_number(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", w.editable ? "qar-widget qar-number" : "qar-widget qar-number qar-readonly");
  caption(out, w);
  stepper(out, w);
  help(out, w);
  close_tag(out, "div");
}

void
render_text(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-text");
  caption(out, w);
  out += "<div class=\"qar-textrow\"><input class=\"qar-field\" type=\"text\" data-qar-role=\"input\"";
  if (w.max_length) {
    attr(out, "maxlength", std::to_string(*w.max_length));
  }
  if (!w.editable) {
    out += " readonly";
  }
  out += '>';
  if (w.editable) {
    out += "<button type=\"button\" class=\"qar-btn qar-send\" data-qar-role=\"send\">Send</button>";
  }
  out += "</div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_toggle(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-toggle-widget");
  caption(out, w);
  out += "<button type=\"button\" class=\"qar-toggle\" role=\"switch\" aria-checked=\"false\" "
    "data-qar-role=\"toggle\"";
  if (!w.editable) {
    out += " disabled";
  }
  out += "><span class=\"qar-toggle-track\"><span class=\"qar-toggle-knob\"></span></span>"
    "<span class=\"qar-toggle-text\" data-qar-role=\"text\">&mdash;</span></button>";
  help(out, w);
  close_tag(out, "div");
}

void
render_slider(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-slider");
  caption(out, w);
  readout(out, w);
  out += "<div class=\"qar-sliderrow\">";
  step_button(out, "dec", "Decrease", "&minus;");
  out += "<input class=\"qar-range\" type=\"range\" data-qar-role=\"input\"";
  attr_number(out, "min", w.min.value_or(0.0));
  attr_number(out, "max", w.max.value_or(1.0));
  attr(out, "step", w.step ? json_format_double(*w.step) : "any");
  if (!w.editable) {
    out += " disabled";
  }
  out += '>';
  step_button(out, "inc", "Increase", "+");
  out += "</div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_choice(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-choice-widget");
  caption(out, w);
  out += "<div class=\"qar-choice\" role=\"radiogroup\" data-qar-role=\"group\">";
  for (std::size_t i = 0; i < w.choices.size(); ++i) {
    out += "<button type=\"button\" class=\"qar-btn qar-chip\" role=\"radio\" aria-checked=\"false\" "
      "data-qar-role=\"option\"";
    attr(out, "data-qar-index", std::to_string(i));
    if (!w.editable) {
      out += " disabled";
    }
    out += '>';
    text(out, w.choices[i].label);
    out += "</button>";
  }
  out += "</div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_button(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-button-widget");
  out += "<button type=\"button\" class=\"qar-btn qar-action\" data-qar-role=\"button\"";
  attr(out, "data-qar-action", w.action);
  if (w.confirm) {
    attr(out, "data-qar-confirm", "1");
  }
  out += "><span data-qar-role=\"text\">";
  text(out, w.display_label());
  out += "</span></button><div class=\"qar-ack\" data-qar-role=\"ack\"></div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_components(std::string & out, const Widget & w, const std::vector<std::string> & names,
  const char * container_class, bool editable)
{
  out += "<div";
  attr(out, "class", container_class);
  out += '>';
  for (const std::string & name : names) {
    out += "<div class=\"qar-comp\"";
    attr(out, "data-qar-component", name);
    out += "><span class=\"qar-comp-name\">";
    text(out, name);
    out += "</span>";
    if (editable) {
      stepper(out, w, name);
    } else {
      out += "<span class=\"qar-readout-value\" data-qar-role=\"value\"";
      attr(out, "data-qar-component", name);
      out += ">&mdash;</span>";
    }
    out += "</div>";
  }
  out += "</div>";
}

void
render_vector3(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-vector3");
  caption(out, w);
  const std::vector<std::string> names = w.components.empty()
    ? std::vector<std::string>{"x", "y", "z"} : w.components;
  render_components(out, w, names, "qar-vector", w.editable);
  if (!w.unit.empty()) {
    out += "<div class=\"qar-readout\">";
    unit(out, w.unit);
    out += "</div>";
  }
  help(out, w);
  close_tag(out, "div");
}

void
render_quaternion(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-quaternion");
  attr(out, "data-qar-display", to_string(w.quaternion_display));
  caption(out, w);
  if (w.quaternion_display != QuaternionDisplay::Euler) {
    // The raw components are never edited directly: a human edits Euler angles.
    render_components(out, w, {"x", "y", "z", "w"}, "qar-quat", false);
  }
  if (w.quaternion_display != QuaternionDisplay::Quaternion) {
    Widget euler = w;
    euler.min = -180.0;
    euler.max = 180.0;
    euler.step = w.step ? w.step : std::optional<double>(5.0);
    euler.unit = "deg";
    render_components(out, euler, {"roll", "pitch", "yaw"}, "qar-euler", w.editable);
    out += "<div class=\"qar-readout\"><span class=\"qar-unit\">degrees, roll-pitch-yaw (ZYX)</span></div>";
  }
  help(out, w);
  close_tag(out, "div");
}

void
render_table(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-table-widget");
  if (w.max_rows != 0) {
    attr(out, "data-qar-max-rows", std::to_string(w.max_rows));
  }
  caption(out, w);
  out += "<div class=\"qar-tablewrap\"><table class=\"qar-table\"><thead><tr>";
  for (const TableColumn & column : w.columns) {
    out += "<th>";
    text(out, column.header);
    if (!column.unit.empty()) {
      out += " <span class=\"qar-unit\">";
      text(out, column.unit);
      out += "</span>";
    }
    out += "</th>";
  }
  out += "</tr></thead><tbody data-qar-role=\"rows\"></tbody></table></div>"
    "<div class=\"qar-pager\"><button type=\"button\" class=\"qar-btn\" data-qar-role=\"prev\" "
    "aria-label=\"Previous page\">&lsaquo;</button><span class=\"qar-pager-info\" "
    "data-qar-role=\"pageinfo\"></span><button type=\"button\" class=\"qar-btn\" "
    "data-qar-role=\"next\" aria-label=\"Next page\">&rsaquo;</button></div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_gauge(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-gauge");
  caption(out, w);
  readout(out, w);
  out += "<div class=\"qar-gauge-track\"><div class=\"qar-gauge-fill\" data-qar-role=\"fill\" "
    "data-qar-level=\"neutral\"></div></div><div class=\"qar-gauge-bounds\"><span>";
  text(out, json_format_double(w.min.value_or(0.0)));
  out += "</span><span>";
  text(out, json_format_double(w.max.value_or(1.0)));
  out += "</span></div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_status(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-status-widget");
  caption(out, w);
  out += "<div class=\"qar-status\" data-qar-role=\"status\" data-qar-level=\"stale\">"
    "<span class=\"qar-status-dot\"></span><span class=\"qar-status-text\" data-qar-role=\"text\">"
    "&mdash;</span></div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_chart(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-chart-widget");
  caption(out, w);
  readout(out, w);
  out += "<svg class=\"qar-chart\" viewBox=\"0 0 600 220\" preserveAspectRatio=\"none\" "
    "data-qar-role=\"svg\" aria-hidden=\"true\"><line class=\"qar-chart-grid\" x1=\"0\" y1=\"110\" "
    "x2=\"600\" y2=\"110\"></line><polyline class=\"qar-chart-line\" data-qar-role=\"line\" "
    "points=\"\"></polyline></svg><div class=\"qar-chart-bounds\"><span data-qar-role=\"ymin\"></span>"
    "<span data-qar-role=\"ymax\"></span></div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_image(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-image-widget");
  caption(out, w);
  out += "<div class=\"qar-imagewrap\"><img class=\"qar-image\" data-qar-role=\"img\"";
  attr(out, "alt", w.display_label());
  out += " hidden></div>";
  help(out, w);
  close_tag(out, "div");
}

void
render_json(std::string & out, const Widget & w)
{
  open_widget(out, w, "div", "qar-widget qar-json-widget");
  if (!w.label.empty() || !w.label_binding.empty()) {
    caption(out, w);
  }
  out += "<pre class=\"qar-json\" data-qar-role=\"json\"></pre>";
  help(out, w);
  close_tag(out, "div");
}

void
render_widget(std::string & out, const Widget & w)
{
  switch (w.type) {
    case WidgetType::Panel:
      render_panel(out, w);
      return;
    case WidgetType::Section:
      render_section(out, w);
      return;
    case WidgetType::Row:
      render_container(out, w, "qar-row");
      return;
    case WidgetType::Column:
      render_container(out, w, "qar-column");
      return;
    case WidgetType::Label:
      render_label(out, w);
      return;
    case WidgetType::ValueDisplay:
      render_value(out, w);
      return;
    case WidgetType::NumberInput:
      render_number(out, w);
      return;
    case WidgetType::TextInput:
      render_text(out, w);
      return;
    case WidgetType::Toggle:
      render_toggle(out, w);
      return;
    case WidgetType::Slider:
      render_slider(out, w);
      return;
    case WidgetType::Choice:
      render_choice(out, w);
      return;
    case WidgetType::Button:
      render_button(out, w);
      return;
    case WidgetType::Vector3:
      render_vector3(out, w);
      return;
    case WidgetType::Quaternion:
      render_quaternion(out, w);
      return;
    case WidgetType::Table:
      render_table(out, w);
      return;
    case WidgetType::Gauge:
      render_gauge(out, w);
      return;
    case WidgetType::StatusLight:
      render_status(out, w);
      return;
    case WidgetType::TimeSeries:
      render_chart(out, w);
      return;
    case WidgetType::Image:
      render_image(out, w);
      return;
    case WidgetType::JsonInspector:
      render_json(out, w);
      return;
  }
}

// JSON inside a <script type="application/json"> element: the only thing that
// can end it early is "</script" (or "<!--"), and html_safe escapes every '<'.
void
json_block(std::string & out, const char * id, const UiValue & value)
{
  out += "<script type=\"application/json\"";
  attr(out, "id", id);
  out += '>';
  JsonWriteOptions options;
  options.html_safe = true;
  json_write_to(out, value, options);
  out += "</script>\n";
}

}  // namespace

std::string
render_widget_fragment(const Widget & widget)
{
  std::string out;
  render_widget(out, widget);
  return out;
}

bool
render_html_page(
  const UiModel & model,
  const HtmlRenderOptions & options,
  std::string & out_html,
  std::vector<UiProblem> & out_problems)
{
  out_html.clear();
  out_problems = validate(model);
  std::string theme_error;
  if (!options.theme.validate(theme_error)) {
    out_problems.push_back({ProblemSeverity::Error, "", "theme: " + theme_error});
  }
  if (!is_valid(out_problems)) {
    return false;
  }

  std::string & out = out_html;
  out.reserve(64 * 1024);
  out += "<!DOCTYPE html>\n<html";
  attr(out, "lang", options.language);
  out += ">\n<head>\n<meta charset=\"utf-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  if (options.include_csp) {
    out += "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
      "script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: http: https:; "
      "connect-src ws: wss: http: https:; base-uri 'none'; form-action 'none'\">\n";
  }
  out += "<title>";
  text(out, options.page_title.empty() ? model.root.display_label() : options.page_title);
  out += "</title>\n<style>";
  out += render_theme_css(options.theme);
  out += "</style>\n</head>\n<body class=\"qar-body\">\n";
  if (options.include_runtime) {
    // Rendered visible and hidden again by the runtime's last boot step. An
    // embedded browser that cannot parse or run the script leaves this standing,
    // so the headset shows a sentence instead of a blank rectangle.
    out += "<div class=\"qar-diag\" id=\"qar-diag\" role=\"alert\">"
      "<strong class=\"qar-diag-title\" id=\"qar-diag-title\">"
      "This panel's script did not run</strong>"
      "<span class=\"qar-diag-detail\" id=\"qar-diag-detail\">"
      "The page loaded but the embedded browser never started the panel "
      "runtime. Nothing below will update or respond."
      "</span></div>\n";
  }
  render_widget(out, model.root);
  out += '\n';

  if (options.include_runtime) {
    // The click pager (scroll_paging.hpp). Rendered ALWAYS, hidden until the
    // runtime measures real overflow — present, not hover-revealed, because a
    // hand ray has no hover and an operator cannot discover a control that is
    // not there. It is page chrome rather than a widget, so it sits beside the
    // panel like the boot diagnostic does, and it is omitted from a static
    // page (include_runtime = false) where nothing could drive it.
    out += "<nav class=\"qar-scroll\" id=\"qar-scroll\" aria-label=\"Page through the panel\" "
      "hidden>"
      "<button type=\"button\" class=\"qar-btn qar-scroll-btn qar-scroll-end\" "
      "data-qar-role=\"scroll-top\" aria-label=\"Jump to the top of the page\">TOP</button>"
      "<button type=\"button\" class=\"qar-btn qar-scroll-btn\" data-qar-role=\"scroll-up\" "
      "aria-label=\"Page up\">&#9650;</button>"
      "<span class=\"qar-scroll-pos\" id=\"qar-scroll-pos\" data-qar-role=\"scroll-pos\" "
      "aria-label=\"Page 1 of 1\">1 / 1</span>"
      "<button type=\"button\" class=\"qar-btn qar-scroll-btn\" data-qar-role=\"scroll-down\" "
      "aria-label=\"Page down\">&#9660;</button>"
      "<button type=\"button\" class=\"qar-btn qar-scroll-btn qar-scroll-end\" "
      "data-qar-role=\"scroll-bottom\" aria-label=\"Jump to the end of the page\">END</button>"
      "</nav>\n";
  }

  UiValue config = UiValue::object();
  config.set("protocol_version", static_cast<std::int64_t>(kProtocolVersion));
  config.set("runtime", kRuntimeName);
  config.set("panel_id", model.root.id);
  config.set("websocket_url", options.websocket_url);
  config.set("reconnect_ms", static_cast<std::int64_t>(options.reconnect_ms));
  config.set("ping_interval_ms", static_cast<std::int64_t>(options.ping_interval_ms));
  config.set("stale_after_ms", static_cast<std::int64_t>(options.stale_after_ms));
  config.set("table_page_rows", static_cast<std::int64_t>(options.theme.table_page_rows));
  // What the page needs to fit ITSELF into the viewport the runtime gives it
  // (viewport_fit.hpp). The browser is the only party that knows that viewport
  // at first paint, so the decision has to be made there; everything it cannot
  // measure for itself — how wide this page would like to be at one, two and
  // three columns, what the pager rail costs, and the theme's own pixel sizes
  // — is measured here and travels with the page.
  if (options.include_runtime) {
    UiValue fit = UiValue::object();
    UiValue natural = UiValue::array();
    for (double width : natural_page_widths(model, options.theme, kMaxRowColumns)) {
      natural.push_back(UiValue(width));
    }
    fit.set("natural_w", std::move(natural));
    fit.set("reserve_px", UiValue(pager_content_reserve_px(options.theme)));
    fit.set("min_scale", UiValue(kMinThemeScale));
    fit.set("max_scale", UiValue(kMaxThemeScale));
    fit.set("deadband", UiValue(kThemeScaleDeadband));
    UiValue variables = UiValue::object();
    for (const auto & variable : theme_pixel_variables(options.theme)) {
      variables.set(variable.first, static_cast<std::int64_t>(variable.second));
    }
    fit.set("vars", std::move(variables));
    config.set("fit", std::move(fit));
  }
  json_block(out, "qar-config", config);

  UiValue model_value = to_value(model);
  model_value.erase("data");  // The data document has its own block below.
  json_block(out, "qar-model", model_value);
  json_block(out, "qar-data", model.initial_data.is_null() ? UiValue::object() : model.initial_data);

  if (options.include_runtime) {
    out += "<script>\n";
    out += runtime_javascript();
    out += "\n</script>\n";
  }
  out += "</body>\n</html>\n";
  return true;
}

}  // namespace qar_ui_model
