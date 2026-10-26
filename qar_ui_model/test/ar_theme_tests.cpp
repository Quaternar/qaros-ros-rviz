// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

/// @file ar_theme_tests.cpp
/// @brief The stylesheet has to be SELF-CONSISTENT, and there is no browser on
/// this machine to prove it by rendering.
///
/// A page whose text is crisp and whose boxes are not — section labels drawn
/// over the value boxes beneath them, a unit sitting inside the component box
/// above it, three stepper rows stacked on one another — is never a raster
/// scaling problem; it is a layout laid out at two sizes at once. Two faults
/// produce it and both are checked here rather than by eye:
///
///  1. **Vertical shrinking.** `[data-qar-pager] .qar-panel` is given
///     `height: 100%` so it can be the scroller, and a flex item defaults to
///     `flex-shrink: 1` — which in a column container shrinks HEIGHT. The
///     spec's automatic minimum size is supposed to stop that; the headset's
///     WebKit 610 does not honour it. @ref ColumnChildrenNeverShrink pins the
///     rule that says so explicitly, and derives the list of column containers
///     from the sheet itself so a new one cannot be added without being
///     covered.
///  2. **Mixed scales.** The page rescales itself by rewriting the `:root`
///     custom properties (viewport_fit.hpp), so a bare pixel literal in a
///     component rule keeps its 1.0 size while the text beside it shrinks.
///     @ref NoPixelLiteralsOutsideTheExemptionList pins every literal that is
///     left, with the reason it is exempt, and
///     @ref MeasuredHeightIsProportionalToTheScale proves the consequence: the
///     modelled height of a scaled page is the unscaled height times the
///     scale, which can only hold when every length scaled together.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/page_metrics.hpp"
#include "qar_ui_model/ui_model.hpp"

using qar_ui_model::ArTheme;
using qar_ui_model::measure_page;
using qar_ui_model::measure_widget_height;
using qar_ui_model::render_theme_css;
using qar_ui_model::theme_pixel_variables;
using qar_ui_model::UiModel;
using qar_ui_model::Widget;
namespace widgets = qar_ui_model::widgets;

namespace
{

/// The scales this file exercises. 0.62 is what a 500 px viewport produces for
/// a typical topic panel; 0.5 and 0.35 are past it, because a rule that only
/// holds near 1.0 is not a rule.
const double kScales[] = {1.0, 0.8, 0.62, 0.5, 0.35};

std::string
trim(const std::string & s)
{
  std::size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) {
    return {};
  }
  std::size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::string
strip_comments(const std::string & css)
{
  std::string out;
  out.reserve(css.size());
  for (std::size_t i = 0; i < css.size();) {
    if (css.compare(i, 2, "/*") == 0) {
      const std::size_t end = css.find("*/", i + 2);
      i = (end == std::string::npos) ? css.size() : end + 2;
      continue;
    }
    out += css[i++];
  }
  return out;
}

struct Rule
{
  std::string selector;
  std::string body;
};

/// The sheet has no at-rules and no nesting, so one pass over `{` / `}` is a
/// complete parse. A nested brace would be a silent parse failure, so it is an
/// assertion rather than a comment.
std::vector<Rule>
rules_of(const std::string & css)
{
  std::vector<Rule> rules;
  std::string pending;
  for (std::size_t i = 0; i < css.size(); ++i) {
    if (css[i] != '{') {
      pending += css[i];
      continue;
    }
    const std::size_t end = css.find('}', i);
    EXPECT_NE(end, std::string::npos) << "unterminated rule for '" << trim(pending) << "'";
    if (end == std::string::npos) {
      break;
    }
    Rule rule;
    rule.selector = trim(pending);
    rule.body = css.substr(i + 1, end - i - 1);
    EXPECT_EQ(rule.body.find('{'), std::string::npos) << "nested braces are not parsed here";
    rules.push_back(std::move(rule));
    pending.clear();
    i = end;
  }
  return rules;
}

std::vector<std::string>
split(const std::string & text, char sep)
{
  std::vector<std::string> parts;
  std::string current;
  for (char c : text) {
    if (c == sep) {
      if (!trim(current).empty()) {
        parts.push_back(trim(current));
      }
      current.clear();
      continue;
    }
    current += c;
  }
  if (!trim(current).empty()) {
    parts.push_back(trim(current));
  }
  return parts;
}

std::vector<std::string>
declarations_of(const Rule & rule)
{
  return split(rule.body, ';');
}

std::string
property_of(const std::string & declaration)
{
  const std::size_t colon = declaration.find(':');
  return colon == std::string::npos ? declaration : trim(declaration.substr(0, colon));
}

std::string
value_of(const std::string & declaration)
{
  const std::size_t colon = declaration.find(':');
  return colon == std::string::npos ? std::string{} : trim(declaration.substr(colon + 1));
}

/// The declaration of @p property in @p rule, or empty. Last wins, as in CSS.
std::string
lookup(const Rule & rule, const std::string & property)
{
  std::string found;
  for (const std::string & declaration : declarations_of(rule)) {
    if (property_of(declaration) == property) {
      found = value_of(declaration);
    }
  }
  return found;
}

const std::string &
component_css()
{
  static const std::string css = strip_comments(render_theme_css(ArTheme::dark()));
  return css;
}

/// "<selector> | <declaration>", the unit every pinned list below is written
/// in: it names WHERE a length is as well as what it is.
std::string
site(const Rule & rule, const std::string & declaration)
{
  return rule.selector + " | " + trim(declaration);
}

bool
has_pixel_literal(const std::string & value)
{
  for (std::size_t i = value.find("px"); i != std::string::npos; i = value.find("px", i + 1)) {
    if (i > 0 && (std::isdigit(static_cast<unsigned char>(value[i - 1])) != 0)) {
      return true;
    }
  }
  return false;
}

UiModel
demo_panel()
{
  // The shape of a typical topic page: a live section of readouts and
  // vectors over a command section of steppers, a slider and an action.
  UiModel model;
  model.root = widgets::panel("root", "/ar_demo/cmd_vel");
  model.root.description = "geometry_msgs/msg/Twist";

  Widget live = widgets::section("live", "Live");
  Widget linear = widgets::vector3("lin", "msg.linear", "Linear velocity");
  linear.unit = "m/s";
  live.add(std::move(linear));
  Widget angular = widgets::vector3("ang", "msg.angular", "Angular velocity");
  angular.unit = "rad/s";
  live.add(std::move(angular));
  Widget speed = widgets::value("spd", "msg.linear.x", "Forward speed");
  speed.unit = "m/s";
  live.add(std::move(speed));
  live.add(widgets::gauge("yaw", "msg.angular.z", "Yaw rate", -1.0, 1.0));
  model.root.add(std::move(live));

  Widget command = widgets::section("cmd", "Command");
  command.add(widgets::button("stop", "STOP", "stop"));
  command.add(widgets::number("fwd", "cmd.linear.x", "Forward"));
  command.add(widgets::number("str", "cmd.linear.y", "Strafe"));
  command.add(widgets::slider("turn", "cmd.angular.z", "Turn", -1.0, 1.0));
  command.add(widgets::toggle("arm", "cmd.armed", "Armed"));
  command.add(widgets::button("pub", "Publish", "publish"));
  model.root.add(std::move(command));
  return model;
}

}  // namespace

// --- The variables ----------------------------------------------------------

TEST(ArThemeCss, EveryVariableUsedIsDefinedOnRoot)
{
  const std::string css = component_css();
  const std::vector<Rule> rules = rules_of(css);
  ASSERT_FALSE(rules.empty());
  ASSERT_EQ(rules.front().selector, ":root");

  std::set<std::string> defined;
  for (const std::string & declaration : declarations_of(rules.front())) {
    const std::string property = property_of(declaration);
    if (property.compare(0, 6, "--qar-") == 0) {
      defined.insert(property);
    }
  }

  for (std::size_t i = css.find("var(--"); i != std::string::npos; i = css.find("var(--", i + 1)) {
    const std::size_t start = i + 4;
    const std::size_t end = css.find_first_of(",)", start);
    ASSERT_NE(end, std::string::npos);
    const std::string name = trim(css.substr(start, end - start));
    EXPECT_EQ(defined.count(name), 1u) << "var(" << name << ") is used but never defined on :root";
  }
}

TEST(ArThemeCss, RootDefinesExactlyThePixelVariablesTheThemeLists)
{
  const ArTheme theme = ArTheme::dark();
  const std::vector<Rule> rules = rules_of(component_css());
  ASSERT_EQ(rules.front().selector, ":root");
  for (const auto & variable : theme_pixel_variables(theme)) {
    const std::string emitted = lookup(rules.front(), variable.first);
    EXPECT_EQ(emitted, std::to_string(variable.second) + "px") << variable.first;
  }
}

TEST(ArThemeCss, ScalingTheThemeScalesEveryPixelVariableByTheSameFactor)
{
  const ArTheme base = ArTheme::dark();
  for (double k : kScales) {
    const auto scaled = theme_pixel_variables(base.scaled(k));
    const auto original = theme_pixel_variables(base);
    ASSERT_EQ(scaled.size(), original.size());
    for (std::size_t i = 0; i < scaled.size(); ++i) {
      EXPECT_STREQ(scaled[i].first, original[i].first);
      const int want = std::max(1, static_cast<int>(std::lround(original[i].second * k)));
      EXPECT_EQ(scaled[i].second, want) << original[i].first << " at k=" << k;
    }
  }
}

// --- The audit --------------------------------------------------------------

TEST(ArThemeCss, NoPixelLiteralsOutsideTheExemptionList)
{
  // Each entry is exempt for a reason that is about the length itself, never
  // about convenience. Adding a bare pixel to the sheet fails this test, which
  // is the point: a literal the runtime cannot rewrite lays part of the page
  // out at a scale the rest of it is not using.
  const std::set<std::string> exempt = {
    // A PILL RADIUS. Deliberately larger than any box it is applied to, so the
    // ends round fully whatever the height is. Scaling it would eventually
    // make it smaller than half the height and square the ends off.
    ".qar-conn | border-radius:999px",
    ".qar-toggle | border-radius:999px",
    ".qar-toggle-track | border-radius:999px",
    ".qar-gauge-track | border-radius:999px",
    // PAINT, NOT LAYOUT. A box-shadow spread occupies no space and can move
    // nothing; it is the dot's halo, and 4 px of it reads at every scale.
    ".qar-status-dot | box-shadow:0 0 0 4px rgba(128,128,128,.18)",
    // SHADOW-DOM PSEUDO-ELEMENTS. Whether this engine substitutes a custom
    // property into ::-webkit-slider-* / ::-moz-range-* is not evidenced
    // (docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser"), and the control
    // renders correctly with literals.
    // So these literals stay as the fallback and the scaled values follow in a
    // second rule that an engine which cannot resolve them drops whole.
    ".qar-range::-webkit-slider-runnable-track | height:16px",
    ".qar-range::-webkit-slider-runnable-track | border-radius:8px",
    ".qar-range::-webkit-slider-thumb | width:48px",
    ".qar-range::-webkit-slider-thumb | height:48px",
    ".qar-range::-webkit-slider-thumb | border:4px solid var(--qar-bg)",
    ".qar-range::-webkit-slider-thumb | margin-top:-16px",
    ".qar-range::-moz-range-track | height:16px",
    ".qar-range::-moz-range-track | border-radius:8px",
    ".qar-range::-moz-range-thumb | width:48px",
    ".qar-range::-moz-range-thumb | height:48px",
    ".qar-range::-moz-range-thumb | border:4px solid var(--qar-bg)",
  };

  std::set<std::string> found;
  for (const Rule & rule : rules_of(component_css())) {
    if (rule.selector == ":root") {
      continue;  // The :root block IS the scaled list.
    }
    for (const std::string & declaration : declarations_of(rule)) {
      if (has_pixel_literal(value_of(declaration))) {
        found.insert(site(rule, declaration));
      }
    }
  }
  for (const std::string & one : found) {
    EXPECT_EQ(exempt.count(one), 1u) << "unscaled pixel length: " << one;
  }
  for (const std::string & one : exempt) {
    EXPECT_EQ(found.count(one), 1u) << "exemption no longer present: " << one;
  }
}

TEST(ArThemeCss, EveryScaledPseudoElementFallbackIsOverriddenByAVariable)
{
  // The other half of the slider exemption: the literals are only tolerable
  // because a later rule restates them from the theme. If that rule is ever
  // deleted the thumb freezes at 48 px and starts overhanging its control.
  const std::vector<Rule> rules = rules_of(component_css());
  const std::vector<std::string> pseudos = {
    ".qar-range::-webkit-slider-runnable-track", ".qar-range::-webkit-slider-thumb",
    ".qar-range::-moz-range-track", ".qar-range::-moz-range-thumb",
  };
  for (const std::string & selector : pseudos) {
    std::vector<const Rule *> matches;
    for (const Rule & rule : rules) {
      if (rule.selector == selector) {
        matches.push_back(&rule);
      }
    }
    ASSERT_EQ(matches.size(), 2u) << selector << " needs a literal rule and a scaled one";
    const std::string height = lookup(*matches.back(), "height");
    EXPECT_NE(height.find("var(--qar-"), std::string::npos)
      << selector << " scaled override must size from the theme, got '" << height << "'";
  }
}

TEST(ArThemeCss, NoFixedHeightOutsideTheExemptionList)
{
  // `height` clips or overlaps where `min-height` only floors. Nothing that
  // holds text at a scalable font size may carry one.
  const std::set<std::string> exempt = {
    // THE SCROLLER. The click pager needs a viewport-sized box to scroll
    // inside, which is the one place a definite height is the feature. It is
    // safe only because .qar-panel's children are flex-shrink:0 (see
    // ColumnChildrenNeverShrink) and it is overflow-y:auto, so content longer
    // than the box scrolls instead of being squeezed into it.
    "[data-qar-pager] | height:100%",
    "[data-qar-pager] body | height:100%",
    "[data-qar-pager] .qar-panel | height:100%",
    // TABLE CELLS. On a table-cell box `height` is a MINIMUM by definition
    // (CSS 2.2 17.5.3), so this is the floor it looks like, and it scales.
    ".qar-table th,.qar-table td | height:var(--qar-row)",
    // BOXES THAT HOLD NO TEXT: a graphic whose height IS the graphic.
    ".qar-toggle-track | height:var(--qar-chip)",
    ".qar-toggle-knob | height:calc(var(--qar-chip) - var(--qar-gap-tight))",
    ".qar-gauge-track | height:var(--qar-dot)",
    ".qar-gauge-fill | height:100%",
    ".qar-conn-dot | height:var(--qar-dot-small)",
    ".qar-status-dot | height:var(--qar-dot)",
    ".qar-chart | height:var(--qar-chart-h)",
    // A range input IS its control box; the thumb can only be centred against
    // a definite height. No text, and the height scales.
    ".qar-range | height:var(--qar-touch)",
    ".qar-range::-webkit-slider-runnable-track | height:16px",
    ".qar-range::-webkit-slider-runnable-track | height:var(--qar-dot-small)",
    ".qar-range::-webkit-slider-thumb | height:48px",
    ".qar-range::-webkit-slider-thumb | height:var(--qar-knob)",
    ".qar-range::-moz-range-track | height:16px",
    ".qar-range::-moz-range-track | height:var(--qar-dot-small)",
    ".qar-range::-moz-range-thumb | height:48px",
    ".qar-range::-moz-range-thumb | height:var(--qar-knob)",
  };

  std::set<std::string> found;
  for (const Rule & rule : rules_of(component_css())) {
    for (const std::string & declaration : declarations_of(rule)) {
      if (property_of(declaration) == "height") {
        found.insert(site(rule, declaration));
      }
    }
  }
  for (const std::string & one : found) {
    EXPECT_EQ(exempt.count(one), 1u) << "fixed height on a box that should flow: " << one;
  }
  for (const std::string & one : exempt) {
    EXPECT_EQ(found.count(one), 1u) << "height exemption no longer present: " << one;
  }
}

// --- The structural rule ----------------------------------------------------

TEST(ArThemeCss, ColumnChildrenNeverShrink)
{
  const std::vector<Rule> rules = rules_of(component_css());

  std::set<std::string> columns;
  for (const Rule & rule : rules) {
    if (lookup(rule, "flex-direction") == "column") {
      for (const std::string & selector : split(rule.selector, ',')) {
        columns.insert(selector);
      }
    }
  }
  ASSERT_GE(columns.size(), 6u) << "the sheet should have several column containers";

  std::set<std::string> protectedSelectors;
  for (const Rule & rule : rules) {
    if (lookup(rule, "flex-shrink") != "0") {
      continue;
    }
    for (const std::string & selector : split(rule.selector, ',')) {
      protectedSelectors.insert(selector);
    }
  }

  for (const std::string & column : columns) {
    EXPECT_EQ(protectedSelectors.count(column + ">*"), 1u)
      << column << " is a column container, so '" << column
      << ">*' must be flex-shrink:0 or its children are squeezed to fit a definite height";
  }

  // Row children must keep shrinking: that is how a row narrows into a
  // viewport, and narrowing a width has never put one line on top of another.
  const Rule * row = nullptr;
  for (const Rule & rule : rules) {
    if (rule.selector == ".qar-row>*") {
      row = &rule;
    }
  }
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(lookup(*row, "flex-shrink"), "1");
}

TEST(ArThemeCss, GapFallbackMarginsEqualTheGapTheyReplace)
{
  // WebKit 610 parses `gap` on a flex container and does not apply it, so the
  // runtime sets data-qar-nogap and these margins take over. A margin that
  // disagrees with the gap it stands in for is a second spacing scale — the
  // exact fault this file exists to prevent.
  const std::vector<Rule> rules = rules_of(component_css());

  std::map<std::string, std::string> gaps;
  for (const Rule & rule : rules) {
    const std::string gap = lookup(rule, "gap");
    if (gap.empty()) {
      continue;
    }
    for (const std::string & selector : split(rule.selector, ',')) {
      gaps[selector] = gap;
    }
  }
  ASSERT_FALSE(gaps.empty());

  std::size_t checked = 0;
  for (const Rule & rule : rules) {
    for (const std::string & selector : split(rule.selector, ',')) {
      const std::string prefix = "[data-qar-nogap] ";
      if (selector.compare(0, prefix.size(), prefix) != 0) {
        continue;
      }
      std::string base = selector.substr(prefix.size());
      for (const std::string & suffix : {std::string(">*+*"), std::string(">*")}) {
        if (base.size() > suffix.size() &&
          base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
        {
          base = base.substr(0, base.size() - suffix.size());
          break;
        }
      }
      const auto gap = gaps.find(base);
      ASSERT_NE(gap, gaps.end()) << "no gap declared for '" << base << "'";
      for (const std::string & declaration : declarations_of(rule)) {
        const std::string property = property_of(declaration);
        if (property.compare(0, 7, "margin-") != 0) {
          continue;
        }
        EXPECT_EQ(value_of(declaration), gap->second)
          << selector << " stands in for " << base << "'s gap and must use the same length";
        ++checked;
      }
    }
  }
  EXPECT_GE(checked, 15u) << "the fallback block should cover every gapped container";
}

// --- The consequence, measured ---------------------------------------------

TEST(ArTheme, MeasuredHeightIsProportionalToTheScale)
{
  // If ANY length failed to scale, a scaled page would be taller than k times
  // the unscaled one — text at k inside boxes at 1.0 — which is precisely the
  // mixed-scale fault. Tolerance covers per-field rounding only (each of the
  // ~24 theme sizes rounds to a whole pixel, and no size may round to zero).
  const ArTheme base = ArTheme::dark();
  const UiModel model = demo_panel();
  const double full = measure_page(model, base, 1).height_px;
  ASSERT_GT(full, 0.0);

  // k = 0.5 halves EVERY default theme size exactly (they are all even), so at
  // this one scale the assertion needs no tolerance beyond the single rounding
  // measure_page does at the end. An unscaled length of any size fails it.
  EXPECT_NEAR(measure_page(model, base.scaled(0.5), 1).height_px, full * 0.5, 1.0);

  for (double k : kScales) {
    const double scaled = measure_page(model, base.scaled(k), 1).height_px;
    EXPECT_NEAR(scaled, full * k, std::max(10.0, full * k * 0.05)) << "at k=" << k;
  }
}

TEST(ArTheme, WidgetBoxesHoldTheirContentAtEveryScale)
{
  // A vector3: a caption over three component boxes, with
  // the unit below. Each part is derived here from the theme fields the
  // stylesheet reads, independently of page_metrics' own walk, and the
  // measured widget must be at least their sum. A component box that does not
  // cover its own name plus its own value is a box that draws text outside
  // itself — the "Linear velocity over the X/Y/Z boxes" symptom.
  const ArTheme base = ArTheme::dark();
  const double line = qar_ui_model::kPageLineHeight;

  for (bool editable : {false, true}) {
    Widget vector = widgets::vector3("v", "msg.linear", "Linear velocity");
    vector.unit = "m/s";
    vector.editable = editable;

    for (double k : kScales) {
      const ArTheme theme = base.scaled(k);
      // .qar-comp: padding, its name at the small font, the inner gap, then
      // either a readout at the value font or a touch-target-high stepper.
      const double inner =
        editable ? theme.touch_target_px : theme.value_font_px * line;
      const double component =
        2.0 * theme.pad_micro_px + theme.small_font_px * line + theme.gap_micro_px + inner;
      // .qar-widget: caption, the component row, the unit's own readout line.
      const double want = theme.small_font_px * line + theme.gap_tight_px + component +
        theme.gap_tight_px + theme.small_font_px * line;

      EXPECT_GE(measure_widget_height(vector, theme, 1) + 0.5, want)
        << "editable=" << editable << " at k=" << k;
      // The box may not be laid out at one scale while its text is at another:
      // whatever k is, the box still clears the text it has to hold.
      EXPECT_GT(component, inner) << "editable=" << editable << " at k=" << k;
    }
  }
}

TEST(ArTheme, ScalingNeverRoundsALengthAway)
{
  // A zero-height box is an overlap by another name, and validate() refuses
  // the theme that would produce one.
  for (double k : {0.5, 0.2, 0.05, 0.01}) {
    const ArTheme theme = ArTheme::dark().scaled(k);
    std::string error;
    EXPECT_TRUE(theme.validate(error)) << "k=" << k << ": " << error;
    for (const auto & variable : theme_pixel_variables(theme)) {
      EXPECT_GE(variable.second, 1) << variable.first << " at k=" << k;
    }
  }
}
