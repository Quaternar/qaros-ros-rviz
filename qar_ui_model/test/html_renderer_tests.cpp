// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The page is what the operator sees in the headset. These tests pin: the AR
// readability rules (target sizes, no hover, minimum text), determinism, that
// the page is self-contained, and — treated as a real requirement — that no
// string from a ROS graph can inject markup or script.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/html_renderer.hpp"
#include "qar_ui_model/scroll_paging.hpp"
#include "qar_ui_model/ui_model.hpp"

using namespace qar_ui_model;  // NOLINT(build/namespaces): test readability

namespace
{

UiModel
sample_model()
{
  UiModel model;
  model.root = widgets::panel("demo", "Demo panel").with_description("/demo (demo_msgs/msg/Demo)")
    .add(widgets::section("s1", "Controls")
      .add(widgets::row("r1")
        .add(widgets::slider("speed", "speed", "Speed", 0.0, 2.0).with_step(0.1).with_unit("m/s")
          .with_action("publish"))
        .add(widgets::number("count", "count", "Count").with_integer().with_range(0, 10).with_action("set")))
      .add(widgets::toggle("enabled", "enabled", "Enabled").with_action("set"))
      .add(widgets::choice("mode", "mode", "Mode", {{"Auto", "auto"}, {"Manual", "manual"}}).with_action("set"))
      .add(widgets::text_input("name", "name", "Name").with_action("set"))
      .add(widgets::button("go", "Go", "call:/go").with_payload(UiValue::object().set("fast", true)))
      .add(widgets::button("stop", "Stop", "call:/stop").with_confirm()))
    .add(widgets::section("s2", "Readouts")
      .add(widgets::value("v", "speed", "Speed").with_unit("m/s").with_precision(2))
      .add(widgets::vector3("pos", "pose.position", "Position").with_editable(false).with_unit("m"))
      .add(widgets::vector3("vel", "vel", "Velocity"))
      .add(widgets::quaternion("q", "pose.orientation", "Orientation").with_editable(false))
      .add(widgets::quaternion("qe", "pose.orientation", "Set orientation"))
      .add(widgets::gauge("soc", "battery", "Battery", 0.0, 1.0))
      .add(widgets::status_light("st", "status", "Status")
        .with_rule(StatusRule{UiValue(0), std::nullopt, std::nullopt, StatusLevel::Ok, "OK"}))
      .add(widgets::time_series("ch", "speed", "Speed history"))
      .add(widgets::table("tb", "rows", "Rows", {{"Name", "name", "", std::nullopt}, {"Value", "value", "V", 2}}))
      .add(widgets::image("img", "image", "Camera"))
      .add(widgets::label("lbl", "Static text"))
      .add(widgets::json_inspector("raw")));
  model.initial_data = UiValue::object().set("speed", 1.25).set("count", 3).set("enabled", true)
    .set("name", "robot one");
  return model;
}

std::string
render(const UiModel & model, const HtmlRenderOptions & options = {})
{
  std::string html;
  std::vector<UiProblem> problems;
  const bool ok = render_html_page(model, options, html, problems);
  EXPECT_TRUE(ok);
  for (const UiProblem & p : problems) {
    if (p.severity == ProblemSeverity::Error) {
      ADD_FAILURE() << p.widget_id << ": " << p.message;
    }
  }
  return html;
}

std::size_t
count(const std::string & haystack, const std::string & needle)
{
  std::size_t n = 0;
  for (std::size_t pos = haystack.find(needle); pos != std::string::npos;
    pos = haystack.find(needle, pos + needle.size()))
  {
    ++n;
  }
  return n;
}

}  // namespace

// ---- theme ------------------------------------------------------------------

TEST(ArTheme, DefaultsMeetTheArReadabilityFloor)
{
  const ArTheme theme = ArTheme::dark();
  EXPECT_GE(theme.touch_target_px, 64);
  EXPECT_GE(theme.touch_gap_px, 12);
  EXPECT_GE(theme.small_font_px, 18);
  EXPECT_GE(theme.base_font_px, theme.small_font_px);
  EXPECT_GE(theme.value_font_px, theme.base_font_px);
  EXPECT_GE(theme.table_row_px, 48);
  std::string error;
  EXPECT_TRUE(theme.validate(error)) << error;
  EXPECT_TRUE(ArTheme::light().validate(error)) << error;
  EXPECT_EQ(theme.background, "#0F141A");  // dark-first
  EXPECT_NE(ArTheme::light().background, theme.background);
}

TEST(ArTheme, ScaledMultipliesSizesOnly)
{
  const ArTheme big = ArTheme::dark().scaled(1.5);
  EXPECT_EQ(big.touch_target_px, 96);
  EXPECT_EQ(big.base_font_px, 33);
  EXPECT_EQ(big.table_page_rows, ArTheme::dark().table_page_rows);
  EXPECT_EQ(big.accent, ArTheme::dark().accent);
  std::string error;
  EXPECT_TRUE(big.validate(error)) << error;
}

TEST(ArTheme, ValidateRejectsCssInjectionAndNonsense)
{
  std::string error;
  ArTheme theme;
  theme.accent = "red";
  EXPECT_FALSE(theme.validate(error));
  EXPECT_NE(error.find("accent"), std::string::npos);
  theme = ArTheme::dark();
  theme.background = "#000}body{display:none}";
  EXPECT_FALSE(theme.validate(error));
  theme = ArTheme::dark();
  theme.font_family = "x;}</style><script>";
  EXPECT_FALSE(theme.validate(error));
  theme = ArTheme::dark();
  theme.touch_target_px = 0;
  EXPECT_FALSE(theme.validate(error));
  theme = ArTheme::dark();
  theme.table_page_rows = -1;
  EXPECT_FALSE(theme.validate(error));
}

TEST(ThemeCss, EmitsVariablesAndNoHoverRules)
{
  const std::string css = render_theme_css(ArTheme::dark());
  EXPECT_NE(css.find(":root{--qar-font:22px;"), std::string::npos);
  EXPECT_NE(css.find("--qar-touch:64px;"), std::string::npos);
  EXPECT_NE(css.find("--qar-bg:#0F141A;"), std::string::npos);
  EXPECT_NE(css.find("min-height:var(--qar-touch)"), std::string::npos);
  EXPECT_EQ(css.find(":hover"), std::string::npos);  // a mid-air pointer has no reliable hover
  EXPECT_EQ(css.find("@import"), std::string::npos);
  EXPECT_EQ(css.find("url("), std::string::npos);
  EXPECT_EQ(render_theme_css(ArTheme::dark()), css);  // deterministic
  EXPECT_NE(render_theme_css(ArTheme::light()), css);
}

// ---- renderer ---------------------------------------------------------------

TEST(HtmlRenderer, ProducesASelfContainedPage)
{
  const std::string html = render(sample_model());
  EXPECT_EQ(html.rfind("<!DOCTYPE html>", 0), 0u);
  EXPECT_NE(html.find("<html lang=\"en\">"), std::string::npos);
  EXPECT_NE(html.find("<meta charset=\"utf-8\">"), std::string::npos);
  EXPECT_NE(html.find("name=\"viewport\""), std::string::npos);
  EXPECT_NE(html.find("Content-Security-Policy"), std::string::npos);
  EXPECT_NE(html.find("<title>Demo panel</title>"), std::string::npos);
  EXPECT_NE(html.find("<style>:root{"), std::string::npos);
  EXPECT_NE(html.find("id=\"qar-config\""), std::string::npos);
  EXPECT_NE(html.find("id=\"qar-model\""), std::string::npos);
  EXPECT_NE(html.find("id=\"qar-data\""), std::string::npos);
  EXPECT_NE(html.find("qar-ui-runtime/1"), std::string::npos);
  EXPECT_NE(html.find("window.QarPanel"), std::string::npos);
  EXPECT_NE(html.find("</html>"), std::string::npos);
  // Nothing is fetched: no external script/style/font, no src= on anything but the runtime-fed img.
  EXPECT_EQ(html.find("<script src"), std::string::npos);
  EXPECT_EQ(html.find("<link"), std::string::npos);
  EXPECT_EQ(html.find("http://"), std::string::npos);
  EXPECT_EQ(html.find("https://"), std::string::npos);
  EXPECT_EQ(html.find("@import"), std::string::npos);
}

TEST(HtmlRenderer, IsDeterministic)
{
  const UiModel model = sample_model();
  const std::string a = render(model);
  const std::string b = render(model);
  EXPECT_EQ(a, b);
  EXPECT_EQ(render_widget_fragment(model.root), render_widget_fragment(model.root));
  EXPECT_EQ(runtime_javascript(), runtime_javascript());
}

TEST(HtmlRenderer, EveryWidgetAppearsOnceWithItsAttributesInOrder)
{
  const UiModel model = sample_model();
  const std::string html = render(model);
  std::vector<std::string> ids;
  walk_widgets(model.root, [&](const Widget & w, const Widget *, std::size_t) {
    ids.push_back(w.id);
    return WalkControl::Continue;
  });
  std::size_t previous = 0;
  for (const std::string & id : ids) {
    const std::string marker = "data-qar-id=\"" + id + "\"";
    EXPECT_EQ(count(html, marker), 1u) << id;
    const std::size_t at = html.find(marker);
    EXPECT_GT(at, previous) << id << " out of document order";  // rendered in walk order
    previous = at;
  }
  EXPECT_NE(html.find("data-qar-id=\"speed\" data-qar-type=\"slider\" data-qar-bind=\"speed\""),
    std::string::npos);
  EXPECT_NE(html.find("data-qar-id=\"pos\" data-qar-type=\"vector3\" data-qar-bind=\"pose.position\""),
    std::string::npos);
}

TEST(HtmlRenderer, WidgetFragmentsAreStable)
{
  // Golden fragments for the small widgets: a change here is a change to the
  // page runtime's DOM contract and must be deliberate.
  EXPECT_EQ(render_widget_fragment(widgets::label("l", "Hello")),
    "<p class=\"qar-widget qar-label\" data-qar-id=\"l\" data-qar-type=\"label\">Hello</p>");
  EXPECT_EQ(render_widget_fragment(widgets::value("v", "speed", "Speed").with_unit("m/s")),
    "<div class=\"qar-widget qar-value\" data-qar-id=\"v\" data-qar-type=\"value\" data-qar-bind=\"speed\">"
    "<div class=\"qar-caption\">Speed</div><div class=\"qar-readout\">"
    "<span class=\"qar-readout-value\" data-qar-role=\"value\">&mdash;</span>"
    "<span class=\"qar-unit\">m/s</span></div></div>");
  EXPECT_EQ(render_widget_fragment(widgets::toggle("t", "on", "Power")),
    "<div class=\"qar-widget qar-toggle-widget\" data-qar-id=\"t\" data-qar-type=\"toggle\" data-qar-bind=\"on\">"
    "<div class=\"qar-caption\">Power</div>"
    "<button type=\"button\" class=\"qar-toggle\" role=\"switch\" aria-checked=\"false\" data-qar-role=\"toggle\">"
    "<span class=\"qar-toggle-track\"><span class=\"qar-toggle-knob\"></span></span>"
    "<span class=\"qar-toggle-text\" data-qar-role=\"text\">&mdash;</span></button></div>");
  EXPECT_EQ(render_widget_fragment(widgets::button("b", "Go", "call:/go").with_confirm()),
    "<div class=\"qar-widget qar-button-widget\" data-qar-id=\"b\" data-qar-type=\"button\">"
    "<button type=\"button\" class=\"qar-btn qar-action\" data-qar-role=\"button\" data-qar-action=\"call:/go\" "
    "data-qar-confirm=\"1\"><span data-qar-role=\"text\">Go</span></button>"
    "<div class=\"qar-ack\" data-qar-role=\"ack\"></div></div>");
  EXPECT_EQ(render_widget_fragment(widgets::row("r").add(widgets::label("a", "A")).add(widgets::label("b", "B"))),
    "<div class=\"qar-row\" data-qar-id=\"r\" data-qar-type=\"row\">"
    "<p class=\"qar-widget qar-label\" data-qar-id=\"a\" data-qar-type=\"label\">A</p>"
    "<p class=\"qar-widget qar-label\" data-qar-id=\"b\" data-qar-type=\"label\">B</p></div>");
}

TEST(HtmlRenderer, ControlsRenderTheirRolesAndRanges)
{
  const std::string html = render(sample_model());
  // Slider: readout + range input with min/max/step + big stepper buttons.
  EXPECT_NE(html.find("<input class=\"qar-range\" type=\"range\" data-qar-role=\"input\" min=\"0.0\" max=\"2.0\" step=\"0.1\">"),
    std::string::npos);
  EXPECT_GE(count(html, "data-qar-role=\"dec\""), 2u);
  // Integer number input uses a numeric keyboard hint.
  EXPECT_NE(html.find("inputmode=\"numeric\""), std::string::npos);
  EXPECT_NE(html.find("inputmode=\"decimal\""), std::string::npos);
  // Choice: radiogroup of large chips, one per option, indexed.
  EXPECT_NE(html.find("role=\"radiogroup\""), std::string::npos);
  EXPECT_NE(html.find("data-qar-index=\"1\">Manual</button>"), std::string::npos);
  // Text input has an explicit Send.
  EXPECT_NE(html.find("data-qar-role=\"send\">Send</button>"), std::string::npos);
  // Read-only vector shows readouts, editable one shows steppers per component.
  EXPECT_NE(html.find("data-qar-role=\"value\" data-qar-component=\"z\""), std::string::npos);
  EXPECT_NE(html.find("data-qar-role=\"input\" data-qar-component=\"z\""), std::string::npos);
  // Quaternion: raw components are never editable, Euler steppers are.
  EXPECT_NE(html.find("data-qar-role=\"input\" data-qar-component=\"yaw\""), std::string::npos);
  EXPECT_EQ(html.find("data-qar-role=\"input\" data-qar-component=\"w\""), std::string::npos);
  // Table header + empty body + pager; gauge bounds; chart svg; hidden img; json pre.
  EXPECT_NE(html.find("<th>Value <span class=\"qar-unit\">V</span></th>"), std::string::npos);
  EXPECT_NE(html.find("<tbody data-qar-role=\"rows\"></tbody>"), std::string::npos);
  EXPECT_NE(html.find("data-qar-role=\"pageinfo\""), std::string::npos);
  EXPECT_NE(html.find("<div class=\"qar-gauge-bounds\"><span>0.0</span><span>1.0</span></div>"), std::string::npos);
  EXPECT_NE(html.find("<polyline class=\"qar-chart-line\" data-qar-role=\"line\" points=\"\">"), std::string::npos);
  EXPECT_NE(html.find("<img class=\"qar-image\" data-qar-role=\"img\" alt=\"Camera\" hidden>"), std::string::npos);
  EXPECT_NE(html.find("<pre class=\"qar-json\" data-qar-role=\"json\"></pre>"), std::string::npos);
  EXPECT_NE(html.find("data-qar-level=\"stale\""), std::string::npos);  // status starts stale
  // Connection indicator and status line exist for the runtime to drive.
  EXPECT_NE(html.find("id=\"qar-connection\" data-qar-state=\"offline\""), std::string::npos);
  EXPECT_NE(html.find("id=\"qar-status\""), std::string::npos);
}

TEST(HtmlRenderer, EscapesUntrustedStringsInTextAndAttributes)
{
  UiModel model;
  const std::string hostile_topic = "/cmd\"><script>alert('x')</script>";
  model.root = widgets::panel("p", "<b>Title</b> & co").with_description(hostile_topic)
    .add(widgets::section("s", "</section><img src=x onerror=alert(1)>")
      .add(widgets::value("v", "field", hostile_topic).with_unit("<u>"))
      .add(widgets::button("b", "Go", "act\" onclick=\"evil()"))
      .add(widgets::choice("c", "mode", "M", {{"<i>opt</i>", "x"}}))
      .add(widgets::table("t", "rows", "T", {{"<th>", "name", "\"", std::nullopt}}))
      .add(widgets::image("i", "img", "alt\"><script>"))
      .add(widgets::label("l", "<script>alert(1)</script>")));
  const std::string html = render(model);
  EXPECT_EQ(html.find("<script>alert"), std::string::npos);
  EXPECT_EQ(html.find("<img src=x"), std::string::npos);
  EXPECT_NE(html.find("&lt;/section&gt;&lt;img src=x onerror=alert(1)&gt;"), std::string::npos);
  EXPECT_EQ(html.find("<b>Title"), std::string::npos);
  EXPECT_EQ(html.find("<i>opt"), std::string::npos);
  EXPECT_EQ(html.find("<u>"), std::string::npos);
  EXPECT_EQ(html.find("onclick=\"evil"), std::string::npos);
  EXPECT_NE(html.find("&lt;b&gt;Title&lt;/b&gt; &amp; co"), std::string::npos);
  EXPECT_NE(html.find("data-qar-action=\"act&quot; onclick=&quot;evil()\""), std::string::npos);
  EXPECT_NE(html.find("alt=\"alt&quot;&gt;&lt;script&gt;\""), std::string::npos);
  EXPECT_NE(html.find("<title>&lt;b&gt;Title&lt;/b&gt; &amp; co</title>"), std::string::npos);
  // The model JSON block carries the same strings, html-safe escaped: no raw '<' in it.
  const std::size_t model_start = html.find("id=\"qar-model\">");
  const std::size_t model_end = html.find("</script>", model_start);
  ASSERT_NE(model_start, std::string::npos);
  const std::string model_json = html.substr(model_start, model_end - model_start);
  EXPECT_EQ(model_json.find('<'), std::string::npos);
  EXPECT_NE(model_json.find("\\u003cscript\\u003e"), std::string::npos);
}

TEST(HtmlRenderer, InitialDataCannotEndTheScriptElement)
{
  UiModel model;
  model.root = widgets::panel("p", "P").add(widgets::value("v", "s"));
  model.initial_data = UiValue::object().set("s", "</script><script>alert(1)</script>")
    .set("u", "\xE2\x80\xA8line sep");
  const std::string html = render(model);
  // Exactly the expected script elements: config, model, data, runtime.
  EXPECT_EQ(count(html, "</script>"), 4u);
  EXPECT_EQ(count(html, "<script"), 4u);
  EXPECT_NE(html.find("\\u003c/script\\u003e\\u003cscript\\u003ealert(1)\\u003c/script\\u003e"), std::string::npos);
  EXPECT_NE(html.find("\\u2028line sep"), std::string::npos);
}

TEST(HtmlRenderer, OptionsAreHonoured)
{
  UiModel model;
  model.root = widgets::panel("p", "P").add(widgets::value("v", "s"));
  HtmlRenderOptions options;
  options.websocket_url = "ws://robot.local:8765/ws/p";
  options.page_title = "Custom title";
  options.language = "cs";
  options.reconnect_ms = 500;
  options.stale_after_ms = 1234;
  options.ping_interval_ms = 4321;
  options.include_runtime = false;
  options.include_csp = false;
  options.theme = ArTheme::light().scaled(2.0);
  const std::string html = render(model, options);
  EXPECT_NE(html.find("<html lang=\"cs\">"), std::string::npos);
  EXPECT_NE(html.find("<title>Custom title</title>"), std::string::npos);
  EXPECT_NE(html.find("\"websocket_url\":\"ws://robot.local:8765/ws/p\""), std::string::npos);
  EXPECT_NE(html.find("\"reconnect_ms\":500"), std::string::npos);
  EXPECT_NE(html.find("\"stale_after_ms\":1234"), std::string::npos);
  EXPECT_NE(html.find("\"ping_interval_ms\":4321"), std::string::npos);
  EXPECT_NE(html.find("\"panel_id\":\"p\""), std::string::npos);
  EXPECT_NE(html.find("\"protocol_version\":1"), std::string::npos);
  EXPECT_NE(html.find("--qar-touch:128px;"), std::string::npos);
  EXPECT_NE(html.find("--qar-bg:#F4F6F8;"), std::string::npos);
  EXPECT_EQ(html.find("Content-Security-Policy"), std::string::npos);
  EXPECT_EQ(html.find("window.QarPanel"), std::string::npos);
  EXPECT_EQ(count(html, "<script"), 3u);  // config, model, data only
}

TEST(HtmlRenderer, RefusesAnInvalidModelOrTheme)
{
  UiModel model;
  model.root = widgets::panel("p", "P").add(widgets::button("b", "Go", ""));  // button without action
  std::string html;
  std::vector<UiProblem> problems;
  EXPECT_FALSE(render_html_page(model, HtmlRenderOptions{}, html, problems));
  EXPECT_TRUE(html.empty());
  EXPECT_FALSE(is_valid(problems));

  model.root = widgets::panel("p", "P").add(widgets::value("v", "s"));
  HtmlRenderOptions options;
  options.theme.text = "white";
  EXPECT_FALSE(render_html_page(model, options, html, problems));
  EXPECT_TRUE(html.empty());
  bool theme_problem = false;
  for (const UiProblem & p : problems) {
    theme_problem = theme_problem || p.message.find("theme") != std::string::npos;
  }
  EXPECT_TRUE(theme_problem);

  // Warnings alone do not stop rendering, and are reported.
  model.root = widgets::panel("p", "P").add(widgets::toggle("t", "flag"));  // editable, no action
  problems.clear();
  EXPECT_TRUE(render_html_page(model, HtmlRenderOptions{}, html, problems));
  EXPECT_FALSE(html.empty());
  EXPECT_FALSE(problems.empty());
  EXPECT_TRUE(is_valid(problems));
}

TEST(HtmlRenderer, RuntimeScriptKeepsItsPromises)
{
  const std::string js = runtime_javascript();
  EXPECT_NE(js.find("'use strict'"), std::string::npos);
  EXPECT_NE(js.find("window.QarPanel"), std::string::npos);
  EXPECT_NE(js.find("applyUpdate"), std::string::npos);
  EXPECT_NE(js.find("handleMessage"), std::string::npos);
  EXPECT_NE(js.find("PROTOCOL_VERSION = 1"), std::string::npos);
  EXPECT_EQ(js.find("innerHTML"), std::string::npos);      // data never becomes markup
  EXPECT_EQ(js.find("</script"), std::string::npos);       // it lives inside one
  EXPECT_EQ(js.find("eval("), std::string::npos);
  EXPECT_EQ(js.find("document.write"), std::string::npos);
  EXPECT_EQ(js.find("=>"), std::string::npos);             // ES2015 function syntax only
  for (unsigned char c : js) {
    ASSERT_LT(c, 0x80u) << "runtime must be pure ASCII";
  }
}

// ---- the engine that actually has to run this --------------------------------
//
// The panels render in Ultralight 1.3 (WebKit 610.4.3, Safari 14 era), not in
// Chromium; docs/SDK_COMPATIBILITY.md ("The QarOS in-AR browser") has the
// evidence and the matrix. These pin what keeps a headset session diagnosable,
// and the two CSS fallbacks that version needs.

TEST(HtmlRenderer, CarriesABootDiagnosticTheRuntimeHides)
{
  const std::string html = render(sample_model());
  EXPECT_NE(html.find("id=\"qar-diag\""), std::string::npos);
  EXPECT_NE(html.find("id=\"qar-diag-title\""), std::string::npos);
  EXPECT_NE(html.find("id=\"qar-diag-detail\""), std::string::npos);
  EXPECT_EQ(count(html, "qar-diag\" hidden"), 0u);  // visible until the script hides it
  EXPECT_NE(html.find("did not run"), std::string::npos);

  HtmlRenderOptions bare;
  bare.include_runtime = false;
  // No runtime, no promise to hide it again: do not show a message nothing clears.
  EXPECT_EQ(render(sample_model(), bare).find("id=\"qar-diag\""), std::string::npos);
}

TEST(HtmlRenderer, RuntimeProbesTheCapabilitiesItNeeds)
{
  const std::string js = runtime_javascript();
  EXPECT_NE(js.find("missingCapabilities"), std::string::npos);
  EXPECT_NE(js.find("WebSocket (live data and controls)"), std::string::npos);
  EXPECT_NE(js.find("CSS custom properties"), std::string::npos);
  EXPECT_NE(js.find("inline SVG"), std::string::npos);
  EXPECT_NE(js.find("qar-diag-detail"), std::string::npos);
  EXPECT_NE(js.find("probeFlexGap"), std::string::npos);
  EXPECT_NE(js.find("data-qar-nogap"), std::string::npos);
  // Nothing newer than the engine that has to run it.
  EXPECT_EQ(js.find("Number.isInteger"), std::string::npos);
  EXPECT_EQ(js.find("?."), std::string::npos);  // optional chaining
  EXPECT_EQ(js.find("??"), std::string::npos);  // nullish coalescing
  EXPECT_EQ(js.find("`"), std::string::npos);   // template literals
  EXPECT_EQ(js.find("const "), std::string::npos);
  EXPECT_EQ(js.find("let "), std::string::npos);
}

TEST(ThemeCss, CarriesTheFallbacksWebKit610Needs)
{
  const std::string css = render_theme_css(ArTheme::dark());
  EXPECT_NE(css.find(".qar-btn:focus,"), std::string::npos);     // :focus-visible is Safari 15.4
  EXPECT_NE(css.find(":focus-visible"), std::string::npos);      // kept for engines that have it
  EXPECT_NE(css.find("[data-qar-nogap] .qar-row>*"), std::string::npos);
  EXPECT_NE(css.find(".qar-pager[hidden]"), std::string::npos);  // author display beats the UA rule
  EXPECT_EQ(css.find(":has("), std::string::npos);               // Safari 15.4
  EXPECT_EQ(css.find("aspect-ratio"), std::string::npos);        // absent from WebKit 610
  EXPECT_EQ(css.find("backdrop-filter"), std::string::npos);
  EXPECT_EQ(render_theme_css(ArTheme::dark()), css);             // still deterministic
}

// ---- reaching what does not fit ---------------------------------------------
//
// The page is laid out into a viewport the runtime chooses, not one we declare
// (docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser"), so content past its
// fold is not rasterised at all and no panel size brings it back. It is
// reachable by CLICK. The QarOS SDK 2026-09-21 release added DRAG, so the pager
// is a judgement about hitting a thin thumb in mid air rather than a
// constraint: the rail is click-only on purpose.
// See qar_ui_model/scroll_paging.hpp.

TEST(HtmlRenderer, CarriesAClickPagerForContentBelowTheFold)
{
  const std::string html = render(sample_model());
  EXPECT_NE(html.find("id=\"qar-scroll\""), std::string::npos);
  // One click each, and both ends reachable directly.
  EXPECT_NE(html.find("data-qar-role=\"scroll-up\""), std::string::npos);
  EXPECT_NE(html.find("data-qar-role=\"scroll-down\""), std::string::npos);
  EXPECT_NE(html.find("data-qar-role=\"scroll-top\""), std::string::npos);
  EXPECT_NE(html.find("data-qar-role=\"scroll-bottom\""), std::string::npos);
  // Without a bar there is no other cue that more content exists.
  EXPECT_NE(html.find("id=\"qar-scroll-pos\""), std::string::npos);
  EXPECT_NE(html.find("aria-label=\"Page 1 of 1\""), std::string::npos);
  // Real buttons, at the theme's touch size, not decorations.
  EXPECT_EQ(count(html, "class=\"qar-btn qar-scroll-btn"), 4u);
  EXPECT_NE(render_theme_css(ArTheme::dark()).find(
      ".qar-scroll-btn{width:var(--qar-touch);min-width:var(--qar-touch);"
      "min-height:var(--qar-touch)"), std::string::npos);

  // Nothing drives it on a static page, so nothing offers it.
  HtmlRenderOptions bare;
  bare.include_runtime = false;
  EXPECT_EQ(render(sample_model(), bare).find("id=\"qar-scroll\""), std::string::npos);
}

TEST(HtmlRenderer, ThePagerIsPresentButQuietUntilThereIsOverflow)
{
  const std::string html = render(sample_model());
  // PRESENT in the markup — an operator cannot discover a control that is not
  // there, and a hand ray has no hover to reveal one with.
  EXPECT_NE(html.find("<nav class=\"qar-scroll\""), std::string::npos);
  // ...and hidden until the runtime has MEASURED overflow, so a desktop
  // browser where the wheel already works is not cluttered.
  EXPECT_NE(html.find("id=\"qar-scroll\" aria-label=\"Page through the panel\" hidden>"),
    std::string::npos);

  const std::string css = render_theme_css(ArTheme::dark());
  EXPECT_NE(css.find(".qar-scroll[hidden]"), std::string::npos);  // beats display:flex
  EXPECT_EQ(css.find(".qar-scroll:hover"), std::string::npos);    // never hover-revealed
  // Paged layout is opt-in, driven by an attribute the runtime sets only after
  // it has measured: a page that fits is laid out exactly as without a pager.
  EXPECT_NE(css.find("[data-qar-pager] .qar-panel{"), std::string::npos);
  EXPECT_NE(css.find("overflow-y:auto"), std::string::npos);
  // position:fixed is not on the evidenced list for WebKit 610; absolute is.
  EXPECT_EQ(css.find("position:fixed"), std::string::npos);
  EXPECT_NE(css.find(".qar-scroll{position:absolute"), std::string::npos);
}

TEST(HtmlRenderer, RuntimeMeasuresItsOwnViewportAndMirrorsThePagingRules)
{
  const std::string js = runtime_javascript();
  // The page is the only thing that knows the viewport it was given.
  EXPECT_NE(js.find("window.innerWidth"), std::string::npos);
  EXPECT_NE(js.find("window.innerHeight"), std::string::npos);
  EXPECT_NE(js.find("window.devicePixelRatio"), std::string::npos);
  EXPECT_NE(js.find("scrollWidth"), std::string::npos);
  EXPECT_NE(js.find("scrollHeight"), std::string::npos);
  EXPECT_NE(js.find("type: 'viewport'"), std::string::npos);
  EXPECT_NE(js.find("css_w"), std::string::npos);
  EXPECT_NE(js.find("css_h"), std::string::npos);
  // On connect and on resize, per the task and the protocol doc.
  EXPECT_NE(js.find("'resize', function"), std::string::npos);
  EXPECT_NE(js.find("scheduleViewportReport('connect')"), std::string::npos);
  EXPECT_NE(js.find("scheduleViewportReport('resize')"), std::string::npos);

  // The paging constants are the C++ ones; the two mirror each other.
  EXPECT_NE(js.find("OVERFLOW_TOLERANCE_PX = 4"), std::string::npos);
  EXPECT_NE(js.find("PAGER_EXIT_MARGIN_PX = 32"), std::string::npos);
  EXPECT_NE(js.find("PAGE_OVERLAP_FRACTION = 0.12"), std::string::npos);
  EXPECT_NE(js.find("MIN_PAGE_STEP_FRACTION = 0.5"), std::string::npos);
  EXPECT_DOUBLE_EQ(kOverflowTolerancePx, 4.0);
  EXPECT_DOUBLE_EQ(kPagerExitMarginPx, 32.0);
  EXPECT_DOUBLE_EQ(kPageOverlapFraction, 0.12);
  EXPECT_DOUBLE_EQ(kMinPageStepFraction, 0.5);

  // Click, and only click: the rail commits one complete jump per press, and
  // must not grow a drag affordance, although AR offers a drag gesture.
  EXPECT_NE(js.find("addEventListener('click'"), std::string::npos);
  EXPECT_EQ(js.find("mousedown"), std::string::npos);
  EXPECT_EQ(js.find("mousemove"), std::string::npos);
  EXPECT_EQ(js.find("pointerdown"), std::string::npos);
  EXPECT_EQ(js.find("mouseover"), std::string::npos);
}

// ---- fitting the page to the viewport it is given ---------------------------
//
// The QarOS runtime lays a website panel out in a fixed 500 x 500 CSS px box
// whatever metres we declare (measured; docs/SDK_COMPATIBILITY.md, "The QarOS
// in-AR browser"). A page drawn for 756 px does not overflow such a box, it
// SHRINKS into it, until the text collides — so the page scales its own theme
// down and the server grows the panel by the same factor in metres. See
// qar_ui_model/viewport_fit.hpp; these pin the half that ships in the page.

TEST(ThemeCss, TheRowBasisIsAThemeSizeSoTheFitCanScaleIt)
{
  const std::string css = render_theme_css(ArTheme::dark());
  // The flex basis is a custom property, not a literal 260px in the component
  // rules where nothing could reach it, for two reasons: the fit scales it with
  // the rest of the theme, and a single-column layout sets it to 100% so a row
  // cannot put two children side by side.
  EXPECT_NE(css.find("--qar-col:260px;"), std::string::npos);
  EXPECT_NE(
    css.find(".qar-row>*{flex-grow:1;flex-shrink:1;flex-basis:var(--qar-col);min-width:0}"),
    std::string::npos);
  EXPECT_EQ(css.find("260px;min-width:0"), std::string::npos) << "no literal left behind";
  // Longhands, not `flex: 1 1 var(--qar-col)`: var() inside a shorthand is the
  // shakier half of CSS Variables support on the engines this has to run on.
  EXPECT_EQ(css.find("flex:1 1 var("), std::string::npos);

  // It scales with everything else, which is the whole point.
  EXPECT_NE(render_theme_css(ArTheme::dark().scaled(0.5)).find("--qar-col:130px;"),
    std::string::npos);
}

TEST(HtmlRenderer, ThePageCarriesWhatItNeedsToFitItself)
{
  const std::string html = render(sample_model());
  // Everything the browser cannot measure for itself, measured here: how wide
  // this page wants to be at one, two and three columns, what the pager rail
  // costs the content column, and the theme's own pixel sizes.
  EXPECT_NE(html.find("\"fit\":{"), std::string::npos);
  EXPECT_NE(html.find("\"natural_w\":["), std::string::npos);
  EXPECT_NE(html.find("\"reserve_px\":112"), std::string::npos);
  EXPECT_NE(html.find("\"min_scale\":0.25"), std::string::npos);
  EXPECT_NE(html.find("\"max_scale\":1"), std::string::npos);
  EXPECT_NE(html.find("\"deadband\":0.02"), std::string::npos);
  // The variable list is the stylesheet's own, so a size added to the theme
  // cannot be forgotten by the half that scales it.
  EXPECT_NE(html.find("\"vars\":{\"--qar-font\":22,"), std::string::npos);
  EXPECT_NE(html.find("\"--qar-col\":260"), std::string::npos);
  EXPECT_NE(html.find("\"--qar-touch\":64"), std::string::npos);

  // Nothing runs on a static page, so nothing is promised to it.
  HtmlRenderOptions bare;
  bare.include_runtime = false;
  EXPECT_EQ(render(sample_model(), bare).find("\"fit\":{"), std::string::npos);
}

TEST(HtmlRenderer, RuntimeMirrorsTheFitRules)
{
  const std::string js = runtime_javascript();
  EXPECT_NE(js.find("function applyFit("), std::string::npos);
  EXPECT_NE(js.find("function fitColumns("), std::string::npos);
  EXPECT_NE(js.find("function fitScale("), std::string::npos);
  EXPECT_NE(js.find("function fitQuantise("), std::string::npos);

  // The scale is the viewport over the page plus the rail — one division, the
  // same one qar_ui_model::theme_scale_for makes.
  EXPECT_NE(js.find("viewport / (naturalWidth + num(fitConfig.reserve_px))"), std::string::npos);
  // Snapped DOWN onto the whole-pixel body font (quantise_theme_scale), and
  // the per-size rounding mirrors ArTheme::scaled, floor of one pixel included.
  EXPECT_NE(js.find("Math.floor(baseFont * scale + 1e-9)"), std::string::npos);
  EXPECT_NE(js.find("Math.max(1, Math.round(num(vars[name]) * scale))"), std::string::npos);
  // One column means one child per line, whatever the basis rounds to.
  EXPECT_NE(js.find("setProperty('--qar-col', '100%')"), std::string::npos);

  // Applied before anything is registered or drawn, so the first paint is the
  // fitted one, and re-applied on resize BEFORE the pager is re-measured.
  EXPECT_NE(js.find("applyFit('boot');"), std::string::npos);
  EXPECT_NE(js.find("applyFit('resize'); updatePager();"), std::string::npos);
  // Exposed so a harness can drive it without a headset.
  EXPECT_NE(js.find("applyFit: applyFit"), std::string::npos);

  // Still nothing newer than the engine that has to run it.
  EXPECT_EQ(js.find("Object.assign"), std::string::npos);
  EXPECT_EQ(js.find("=>"), std::string::npos);
}
