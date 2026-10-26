// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The two optional widget fields that let a fixed page show run-time content: a caption (or a
// button text) taken from the data document, and a visibility switch taken from it. These tests pin
// the JSON shape, the path validation, the markup the renderer adds, the runtime text that reads
// them, and - as a requirement - that a model without the fields renders exactly as it did.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "qar_ui_model/html_renderer.hpp"
#include "qar_ui_model/json.hpp"
#include "qar_ui_model/ui_model.hpp"

using namespace qar_ui_model;  // NOLINT(build/namespaces): test readability

namespace
{

// A model with one widget of every type and the options that change their markup.
UiModel
every_widget_model()
{
  UiModel model;
  model.root = widgets::panel("demo", "Demo panel").with_description("/demo (demo_msgs/msg/Demo)")
    .add(widgets::section("s1", "Controls").with_description("Sliders and buttons")
      .add(widgets::row("r1")
        .add(widgets::slider("speed", "speed", "Speed", 0.0, 2.0).with_step(0.1).with_unit("m/s")
          .with_action("publish"))
        .add(widgets::number("count", "count", "Count").with_integer().with_range(0, 10)
          .with_action("set")))
      .add(widgets::column("c1")
        .add(widgets::toggle("enabled", "enabled", "Enabled").with_action("set"))
        .add(widgets::toggle("fixed", "fixed", "Fixed").with_editable(false)))
      .add(widgets::choice("mode", "mode", "Mode", {{"Auto", "auto"}, {"Manual", "manual"}})
        .with_action("set"))
      .add(widgets::text_input("name", "name", "Name").with_action("set"))
      .add(widgets::button("go", "Go", "call:/go")
        .with_payload(UiValue::object().set("fast", true)))
      .add(widgets::button("stop", "Stop", "call:/stop").with_confirm()))
    .add(widgets::section("s2", "Readouts")
      .add(widgets::value("v", "speed", "Speed").with_unit("m/s").with_precision(2))
      .add(widgets::value("bare", "pose.position.x"))
      .add(widgets::vector3("pos", "pose.position", "Position").with_editable(false).with_unit("m"))
      .add(widgets::vector3("vel", "vel", "Velocity"))
      .add(widgets::quaternion("q", "pose.orientation", "Orientation").with_editable(false))
      .add(widgets::quaternion("qe", "pose.orientation", "Set orientation"))
      .add(widgets::gauge("soc", "battery", "Battery", 0.0, 1.0))
      .add(widgets::status_light("st", "status", "Status")
        .with_rule(StatusRule{UiValue(0), std::nullopt, std::nullopt, StatusLevel::Ok, "OK"}))
      .add(widgets::time_series("ch", "speed", "Speed history"))
      .add(widgets::table("tb", "rows", "Rows",
        {{"Name", "name", "", std::nullopt}, {"Value", "value", "V", 2}}))
      .add(widgets::image("img", "image", "Camera"))
      .add(widgets::label("lbl", "Static text"))
      .add(widgets::json_inspector("raw"))
      .add(widgets::json_inspector("raw2", "pose").with_label("Pose")));
  model.initial_data = UiValue::object().set("speed", 1.25).set("count", 3).set("enabled", true)
    .set("name", "robot one");
  return model;
}

// A short fingerprint of a long, fixed text (FNV-1a, 64 bit).
std::uint64_t
fingerprint(const std::string & text)
{
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : text) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

// The page without its script: the stylesheet, the markup and the three JSON blocks.
std::string
static_page(const UiModel & model)
{
  HtmlRenderOptions options;
  options.include_runtime = false;
  std::string html;
  std::vector<UiProblem> problems;
  EXPECT_TRUE(render_html_page(model, options, html, problems));
  return html;
}

// Sets both fields to the empty text on every widget of the tree.
void
clear_bindings(Widget & widget)
{
  widget.with_label_binding("").with_visible_binding("");
  for (Widget & child : widget.children) {
    clear_bindings(child);
  }
}

std::vector<UiProblem>
problems_of(const Widget & widget)
{
  UiModel model;
  model.root = widgets::panel("p", "P").add(widget);
  return validate(model);
}

bool
has_problem(
  const std::vector<UiProblem> & problems, ProblemSeverity severity, const std::string & id,
  const std::string & fragment)
{
  for (const UiProblem & p : problems) {
    if (p.severity == severity && p.widget_id == id &&
      p.message.find(fragment) != std::string::npos)
    {
      return true;
    }
  }
  return false;
}

// Does @p text contain @p word as a whole word (not inside a longer identifier)?
bool
contains_word(const std::string & text, std::string_view word)
{
  const auto is_ident = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '$';
  };
  for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
    const bool left = at == 0 || !is_ident(text[at - 1]);
    const std::size_t end = at + word.size();
    const bool right = end >= text.size() || !is_ident(text[end]);
    if (left && right) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(UiModelBindings, CodecRoundTrip)
{
  Widget toggle = widgets::toggle("t", "panels.s0.on", "Panel 1").with_action("cp.panel.toggle");
  toggle.with_label_binding("panels.s0.title").with_visible_binding("panels.s0.show");
  Widget tool = widgets::button("b", "Tool 1", "cp.tool.reopen").with_payload(UiValue(0));
  tool.with_label_binding("tools.s0.title").with_visible_binding("tools.s0.show");

  const UiValue value = to_value(toggle);
  ASSERT_NE(value.find("labelBind"), nullptr);
  ASSERT_NE(value.find("showBind"), nullptr);
  EXPECT_EQ(value.find("labelBind")->as_string(), "panels.s0.title");
  EXPECT_EQ(value.find("showBind")->as_string(), "panels.s0.show");

  // Through the text of a whole model and back, and beside "bind" in the text.
  UiModel model;
  model.root = widgets::panel("p", "P").add(toggle).add(tool);
  const std::string text = json_write(to_value(model));
  EXPECT_NE(text.find("\"bind\":\"panels.s0.on\",\"labelBind\":\"panels.s0.title\","
      "\"showBind\":\"panels.s0.show\""), std::string::npos) << text;
  UiValue parsed;
  JsonParseError parse_error;
  ASSERT_TRUE(json_parse(text, parsed, parse_error)) << parse_error.to_string();
  UiModel back;
  std::string error;
  ASSERT_TRUE(from_value(parsed, back, error)) << error;
  EXPECT_EQ(json_write(to_value(back)), text);
  const Widget * back_toggle = back.find("t");
  ASSERT_NE(back_toggle, nullptr);
  EXPECT_EQ(back_toggle->label_binding, "panels.s0.title");
  EXPECT_EQ(back_toggle->visible_binding, "panels.s0.show");
  const Widget * back_tool = back.find("b");
  ASSERT_NE(back_tool, nullptr);
  EXPECT_EQ(back_tool->label_binding, "tools.s0.title");
  EXPECT_EQ(back_tool->visible_binding, "tools.s0.show");
  EXPECT_TRUE(is_valid(validate(back)));

  // A widget without the fields writes neither key, so a model that never heard of them keeps
  // its text, and reading such a text leaves both fields empty.
  EXPECT_EQ(json_write(to_value(widgets::value("v", "a"))),
    "{\"id\":\"v\",\"type\":\"value\",\"bind\":\"a\"}");
  Widget plain;
  ASSERT_TRUE(json_parse("{\"id\":\"v\",\"type\":\"value\",\"bind\":\"a\"}", parsed, parse_error));
  ASSERT_TRUE(from_value(parsed, plain, error)) << error;
  EXPECT_TRUE(plain.label_binding.empty());
  EXPECT_TRUE(plain.visible_binding.empty());

  // Only strings are accepted.
  Widget refused;
  ASSERT_TRUE(json_parse("{\"id\":\"v\",\"type\":\"value\",\"labelBind\":5}", parsed, parse_error));
  EXPECT_FALSE(from_value(parsed, refused, error));
  EXPECT_NE(error.find("labelBind"), std::string::npos);
  ASSERT_TRUE(json_parse(
      "{\"id\":\"v\",\"type\":\"value\",\"showBind\":true}", parsed, parse_error));
  EXPECT_FALSE(from_value(parsed, refused, error));
  EXPECT_NE(error.find("showBind"), std::string::npos);
}

TEST(UiModelBindings, ValidateRejectsBadPaths)
{
  // The same grammar as `bind`: dotted identifiers, each with an optional [index].
  const std::vector<std::string> good = {"a", "a.b", "panels.s0.title", "list[0].name", "_x.y_1"};
  for (const std::string & path : good) {
    const std::vector<UiProblem> problems = problems_of(
      widgets::value("w", "v").with_label_binding(path).with_visible_binding(path));
    EXPECT_TRUE(is_valid(problems)) << path;
    EXPECT_TRUE(problems.empty()) << path;
  }

  const std::vector<std::string> bad = {"a..b", "1a", "a[x]", "a.", ".a", "a b", "a-b", "a[0"};
  for (const std::string & path : bad) {
    const std::vector<UiProblem> label = problems_of(
      widgets::value("w", "v").with_label_binding(path));
    EXPECT_FALSE(is_valid(label)) << path;
    EXPECT_TRUE(has_problem(label, ProblemSeverity::Error, "w", "invalid label binding"))
      << path;
    const std::vector<UiProblem> shown = problems_of(
      widgets::value("w", "v").with_visible_binding(path));
    EXPECT_FALSE(is_valid(shown)) << path;
    EXPECT_TRUE(has_problem(shown, ProblemSeverity::Error, "w", "invalid visibility binding"))
      << path;
  }

  // An empty text is "no binding", not an error.
  EXPECT_TRUE(problems_of(
      widgets::value("w", "v").with_label_binding("").with_visible_binding("")).empty());

  // A visibility binding fits every type; a label binding needs a caption or a button text.
  EXPECT_TRUE(problems_of(
      widgets::section("s", "S").with_visible_binding("s.show")
      .add(widgets::label("l", "L").with_visible_binding("l.show"))).empty());
  EXPECT_TRUE(has_problem(
      problems_of(widgets::label("l", "L").with_label_binding("l.text")),
      ProblemSeverity::Warning, "l", "label binding"));
  EXPECT_TRUE(has_problem(
      problems_of(widgets::section("s", "S").with_label_binding("s.text")),
      ProblemSeverity::Warning, "s", "label binding"));
  EXPECT_TRUE(is_valid(problems_of(widgets::label("l", "L").with_label_binding("l.text"))));
  EXPECT_TRUE(problems_of(
      widgets::button("b", "B", "act").with_label_binding("b.text")).empty());

  // The renderer refuses what validate() refuses.
  UiModel model;
  model.root = widgets::panel("p", "P").add(widgets::value("w", "v").with_label_binding("a..b"));
  std::string html;
  std::vector<UiProblem> problems;
  EXPECT_FALSE(render_html_page(model, HtmlRenderOptions{}, html, problems));
  EXPECT_TRUE(html.empty());
}

TEST(UiModelBindings, RendererUnchangedWithoutBindings)
{
  const UiModel model = every_widget_model();
  const std::string html = static_page(model);

  // The markup, the stylesheet and the JSON blocks of a model that does not use the fields, as
  // one fingerprint: a change here is a change to the DOM contract of every existing page.
  EXPECT_EQ(fingerprint(html), 9720008613085469433ULL) << html.size();

  // Setting the fields to nothing changes nothing.
  UiModel emptied = model;
  clear_bindings(emptied.root);
  EXPECT_EQ(static_page(emptied), html);
  EXPECT_EQ(render_widget_fragment(emptied.root), render_widget_fragment(model.root));

  // None of the new markup appears for such a model.
  EXPECT_EQ(html.find("style=\"display:none\""), std::string::npos);
  EXPECT_EQ(html.find("data-qar-role=\"caption\""), std::string::npos);
  EXPECT_EQ(html.find("labelBind"), std::string::npos);
  EXPECT_EQ(html.find("showBind"), std::string::npos);
  EXPECT_NE(html.find("<div class=\"qar-caption\">Speed</div>"), std::string::npos);
}

TEST(UiModelBindings, RendererMarksCaptionAndStartsHidden)
{
  // A bound caption carries the role the runtime looks for; an unbound one does not.
  EXPECT_EQ(render_widget_fragment(
      widgets::value("v", "speed", "Speed").with_label_binding("cap.v")),
    "<div class=\"qar-widget qar-value\" data-qar-id=\"v\" data-qar-type=\"value\" "
    "data-qar-bind=\"speed\"><div class=\"qar-caption\" data-qar-role=\"caption\">Speed</div>"
    "<div class=\"qar-readout\"><span class=\"qar-readout-value\" data-qar-role=\"value\">"
    "&mdash;</span></div></div>");
  EXPECT_NE(render_widget_fragment(widgets::value("v", "speed", "Speed"))
    .find("<div class=\"qar-caption\">Speed</div>"), std::string::npos);

  // A widget with a visibility binding starts hidden, whatever its type, the binding attribute
  // first and the style after it.
  EXPECT_EQ(render_widget_fragment(
      widgets::toggle("t", "panels.s0.on", "Panel 1").with_visible_binding("panels.s0.show")
      .with_label_binding("panels.s0.title")),
    "<div class=\"qar-widget qar-toggle-widget\" data-qar-id=\"t\" data-qar-type=\"toggle\" "
    "data-qar-bind=\"panels.s0.on\" style=\"display:none\">"
    "<div class=\"qar-caption\" data-qar-role=\"caption\">Panel 1</div>"
    "<button type=\"button\" class=\"qar-toggle\" role=\"switch\" aria-checked=\"false\" "
    "data-qar-role=\"toggle\"><span class=\"qar-toggle-track\"><span class=\"qar-toggle-knob\">"
    "</span></span><span class=\"qar-toggle-text\" data-qar-role=\"text\">&mdash;</span></button>"
    "</div>");
  EXPECT_EQ(render_widget_fragment(
      widgets::button("b", "Tool 1", "cp.tool.reopen").with_label_binding("tools.s0.title")
      .with_visible_binding("tools.s0.show")),
    "<div class=\"qar-widget qar-button-widget\" data-qar-id=\"b\" data-qar-type=\"button\" "
    "style=\"display:none\"><button type=\"button\" class=\"qar-btn qar-action\" "
    "data-qar-role=\"button\" data-qar-action=\"cp.tool.reopen\"><span data-qar-role=\"text\">"
    "Tool 1</span></button><div class=\"qar-ack\" data-qar-role=\"ack\"></div></div>");
  EXPECT_EQ(render_widget_fragment(
      widgets::section("s", "More").with_visible_binding("more.show")),
    "<section class=\"qar-section\" data-qar-id=\"s\" data-qar-type=\"section\" "
    "style=\"display:none\"><h2 class=\"qar-section-title\">More</h2>"
    "<div class=\"qar-section-body\"></div></section>");
  EXPECT_NE(render_widget_fragment(
      widgets::label("l", "Note").with_visible_binding("l.show"))
    .find("data-qar-type=\"label\" style=\"display:none\">Note</p>"), std::string::npos);

  // A JSON inspector draws a caption only when it has a label; a label binding gives it one.
  EXPECT_EQ(render_widget_fragment(widgets::json_inspector("raw")).find("qar-caption"),
    std::string::npos);
  EXPECT_NE(render_widget_fragment(widgets::json_inspector("raw").with_label_binding("raw.title"))
    .find("<div class=\"qar-caption\" data-qar-role=\"caption\">raw</div>"), std::string::npos);

  // The page carries the same, and the model block the runtime reads carries the two paths.
  UiModel model;
  model.root = widgets::panel("p", "P")
    .add(widgets::value("v", "speed", "Speed").with_label_binding("cap.v"))
    .add(widgets::button("b", "Tool 1", "act").with_label_binding("tools.s0.title")
      .with_visible_binding("tools.s0.show"));
  std::string html;
  std::vector<UiProblem> problems;
  ASSERT_TRUE(render_html_page(model, HtmlRenderOptions{}, html, problems));
  EXPECT_NE(html.find("data-qar-role=\"caption\">Speed</div>"), std::string::npos);
  EXPECT_NE(html.find("data-qar-type=\"button\" style=\"display:none\">"), std::string::npos);
  EXPECT_NE(html.find("\"labelBind\":\"cap.v\""), std::string::npos);
  EXPECT_NE(html.find("\"labelBind\":\"tools.s0.title\",\"showBind\":\"tools.s0.show\""),
    std::string::npos);
}

TEST(UiModelBindings, RuntimeChunkIsEs5)
{
  const std::string js = runtime_javascript();

  // The runtime reads both keys, and sets the caption and the display from the document.
  EXPECT_NE(js.find("w.labelBind"), std::string::npos);
  EXPECT_NE(js.find("w.showBind"), std::string::npos);
  EXPECT_NE(js.find("function refreshBound("), std::string::npos);
  EXPECT_NE(js.find("el.style.display"), std::string::npos);
  EXPECT_NE(js.find("q(el, 'caption')"), std::string::npos);
  EXPECT_NE(js.find("=== true ? '' : 'none'"), std::string::npos);
  // A button that waits for a second press keeps its prompt, and goes back to its bound text.
  EXPECT_NE(js.find("function restingText("), std::string::npos);
  EXPECT_NE(js.find("if (!entry.armed) { setText(q(el, 'text'), title); }"), std::string::npos);

  // Nothing newer than the engine that has to run it (ES5: WebKit 610 reads more, but the
  // contract of the runtime is ES5), and nothing that is not ASCII.
  EXPECT_EQ(js.find("=>"), std::string::npos);
  EXPECT_EQ(js.find('`'), std::string::npos);
  EXPECT_EQ(js.find("..."), std::string::npos);
  EXPECT_EQ(js.find("?."), std::string::npos);
  EXPECT_EQ(js.find("??"), std::string::npos);
  for (const char * word : {"let", "const", "class", "async", "await", "yield"}) {
    EXPECT_FALSE(contains_word(js, word)) << word;
  }
  for (const char * call : {"Object.assign", "Number.isInteger", "Array.from", ".includes(",
      ".startsWith(", ".endsWith(", ".repeat(", "Symbol", "Promise", "innerHTML", "eval("})
  {
    EXPECT_EQ(js.find(call), std::string::npos) << call;
  }
  for (unsigned char c : js) {
    ASSERT_LT(c, 0x80u) << "runtime must be pure ASCII";
  }
}
