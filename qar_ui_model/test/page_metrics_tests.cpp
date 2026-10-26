// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

/// @file page_metrics_tests.cpp
/// @brief The natural size of a generated page, in CSS pixels.
///
/// These pin the RULES, not a golden number: that the measurement follows the
/// theme it is given, that more content means a taller page, that a row makes
/// a page wider only up to the column cap, and that the per-type table is
/// ordered the way the stylesheet is (a table is taller than a chart is taller
/// than a stepper is taller than a label).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <string>

#include "qar_ui_model/page_metrics.hpp"
#include "qar_ui_model/ui_model.hpp"

using qar_ui_model::ArTheme;
using qar_ui_model::measure_page;
using qar_ui_model::measure_widget_height;
using qar_ui_model::PageMetrics;
using qar_ui_model::QuaternionDisplay;
using qar_ui_model::UiModel;
using qar_ui_model::Widget;
using qar_ui_model::WidgetType;
namespace widgets = qar_ui_model::widgets;

namespace
{

/// A panel with one section holding @p n value displays — the shape every
/// generated topic panel has.
UiModel form(std::size_t n, const char * title = "Topic")
{
  UiModel model;
  model.root = widgets::panel("root", title);
  Widget section = widgets::section("s", "Message");
  for (std::size_t i = 0; i < n; ++i) {
    const std::string id = "v" + std::to_string(i);
    section.add(widgets::value(id, "msg.f" + std::to_string(i), "Field"));
  }
  model.root.add(std::move(section));
  return model;
}

}  // namespace

TEST(PageMetrics, EmptyPanelIsTheChromeAlone)
{
  UiModel model;
  model.root = widgets::panel("root", "Nothing");
  const PageMetrics m = measure_page(model);

  const ArTheme theme;
  // padding top + bottom, plus the header (title line vs the connection chip).
  const double expected = 2.0 * theme.padding_px +
    std::max(
      theme.title_font_px * qar_ui_model::kPageLineHeight,
      static_cast<double>(theme.chip_px));
  EXPECT_NEAR(m.height_px, expected, 1.0);
  EXPECT_EQ(m.widget_count, 0u);
  EXPECT_EQ(m.section_count, 0u);
  EXPECT_GT(m.width_px, 0.0);
}

TEST(PageMetrics, MoreWidgetsMeansATallerPage)
{
  const PageMetrics small = measure_page(form(2));
  const PageMetrics big = measure_page(form(12));

  EXPECT_EQ(small.widget_count, 2u);
  EXPECT_EQ(big.widget_count, 12u);
  EXPECT_GT(big.height_px, small.height_px);
  // Same content column: only the height grew.
  EXPECT_DOUBLE_EQ(big.width_px, small.width_px);
  // Ten more value displays, each a caption + a readout + a gap.
  const ArTheme theme;
  const double per_widget =
    measure_widget_height(widgets::value("x", "msg.x", "Field"), theme) + theme.gap_px;
  EXPECT_NEAR(big.height_px - small.height_px, 10.0 * per_widget, 1.5);
}

TEST(PageMetrics, AspectFollowsTheContentRatherThanAFixedRatio)
{
  const PageMetrics tiny = measure_page(form(1));
  const PageMetrics medium = measure_page(form(8));
  const PageMetrics large = measure_page(form(24));

  // Every one of these is a different shape: the aspect is measured, not fixed.
  EXPECT_GT(tiny.aspect, medium.aspect);
  EXPECT_GT(medium.aspect, large.aspect);

  // And a generated form of any real size is PORTRAIT — which is exactly what
  // a fixed landscape size such as 0.6 x 0.45 m gets wrong.
  EXPECT_LT(medium.aspect, 1.0);
  EXPECT_LT(large.aspect, 1.0);
  EXPECT_NEAR(medium.aspect, medium.width_px / medium.height_px, 1e-9);
}

TEST(PageMetrics, AWideRowWidensThePageUpToTheColumnCap)
{
  const auto with_row = [](std::size_t children) {
      UiModel model;
      model.root = widgets::panel("root", "Topic");
      Widget section = widgets::section("s", "Message");
      Widget row = widgets::row("r");
      for (std::size_t i = 0; i < children; ++i) {
        row.add(widgets::value("v" + std::to_string(i), "msg.f" + std::to_string(i), "F"));
      }
      section.add(std::move(row));
      model.root.add(std::move(section));
      return measure_page(model);
    };

  const PageMetrics one = with_row(1);
  const PageMetrics two = with_row(2);
  const PageMetrics three = with_row(3);
  const PageMetrics five = with_row(5);

  EXPECT_EQ(five.widest_row, 5u);
  // One and two columns both fit inside the 60-character text measure, so they
  // do not widen the page; three columns exceed it.
  EXPECT_DOUBLE_EQ(two.width_px, one.width_px);
  EXPECT_GT(three.width_px, two.width_px);
  // Past the cap the page stops widening and starts wrapping instead.
  EXPECT_DOUBLE_EQ(five.width_px, three.width_px);
  EXPECT_GT(five.height_px, three.height_px) << "the 4th and 5th children wrap to a second line";
}

TEST(PageMetrics, TheWidthIsTheTextMeasurePlusTheChrome)
{
  const ArTheme theme;
  const PageMetrics m = measure_page(form(3));
  const double measure =
    qar_ui_model::kComfortableLineChars * qar_ui_model::kAverageCharAdvanceEm * theme.base_font_px;
  // Panel padding plus one section's padding around the text column.
  EXPECT_NEAR(m.width_px, measure + 4.0 * theme.padding_px, 0.5);

  // No section: only the panel's own padding.
  UiModel bare;
  bare.root = widgets::panel("root", "Bare");
  bare.root.add(widgets::value("v", "msg.a", "A"));
  EXPECT_NEAR(measure_page(bare).width_px, measure + 2.0 * theme.padding_px, 0.5);
}

TEST(PageMetrics, FollowsTheThemeItIsGiven)
{
  const UiModel model = form(6);
  const ArTheme normal;
  const ArTheme doubled = normal.scaled(2.0);

  const PageMetrics a = measure_page(model, normal);
  const PageMetrics b = measure_page(model, doubled);

  // Every size in the theme doubled, so the page roughly doubles in both
  // directions and keeps its shape. (url_panel_sizing halves metres-per-pixel
  // to match, so the panel stays the same physical size and gains pixels.)
  //
  // "Roughly", because a handful of pixel values are written into the
  // stylesheet rather than into the theme — `.qar-widget { gap: 8px }`, the
  // 44 px connection chip, the 28 px gauge track — and a theme cannot scale
  // what it does not own. Width, which is driven entirely by theme values,
  // doubles exactly; height carries those fixed pixels and falls a few per
  // cent short.
  EXPECT_NEAR(b.width_px / a.width_px, 2.0, 0.01);
  EXPECT_NEAR(b.height_px / a.height_px, 2.0, 0.1);
  EXPECT_NEAR(b.aspect, a.aspect, 0.1);
}

TEST(PageMetrics, PerTypeHeightsAreOrderedLikeTheStylesheet)
{
  const ArTheme theme;
  const auto h = [&theme](const Widget & w) { return measure_widget_height(w, theme); };

  const double label = h(widgets::label("l", "Text"));
  const double value = h(widgets::value("v", "msg.a", "A"));
  const double number = h(widgets::number("n", "msg.a", "A"));
  const double gauge = h(widgets::gauge("g", "msg.a", "A", 0.0, 1.0));
  const double chart = h(widgets::time_series("t", "msg.a", "A"));
  const double table = h(widgets::table("tb", "msg.rows", "Rows", {}));

  EXPECT_LT(label, value);
  EXPECT_LT(value, number) << "a stepper is a 64 px touch target, a readout is one line";
  EXPECT_LT(gauge, chart);
  EXPECT_LT(chart, table) << "a table is a header row plus a page of rows plus a pager";

  // A label is exactly one line of body text; nothing else is that short.
  EXPECT_NEAR(label, theme.base_font_px * qar_ui_model::kPageLineHeight, 1e-9);
  // A touch-target control is never shorter than the target itself.
  EXPECT_GE(number, static_cast<double>(theme.touch_target_px));
}

TEST(PageMetrics, OptionalPartsCostTheirOwnHeight)
{
  const ArTheme theme;
  Widget plain = widgets::value("v", "msg.a", "A");
  Widget helped = widgets::value("v", "msg.a", "A");
  helped.with_description("What this field means.");
  EXPECT_GT(measure_widget_height(helped, theme), measure_widget_height(plain, theme));

  // A Both-display quaternion shows two component rows, one display shows one.
  Widget both = widgets::quaternion("q", "msg.q", "Q");
  both.quaternion_display = QuaternionDisplay::Both;
  Widget euler = widgets::quaternion("q", "msg.q", "Q");
  euler.quaternion_display = QuaternionDisplay::Euler;
  EXPECT_GT(measure_widget_height(both, theme), measure_widget_height(euler, theme));

  // A panel subtitle grows the header only once it passes the chip's height.
  UiModel titled;
  titled.root = widgets::panel("root", "Topic");
  titled.root.with_description("/cmd_vel — geometry_msgs/msg/Twist");
  UiModel untitled;
  untitled.root = widgets::panel("root", "Topic");
  EXPECT_GT(measure_page(titled).height_px, measure_page(untitled).height_px);
}

TEST(PageMetrics, AColumnStacksAndARowDoesNot)
{
  UiModel column_model;
  column_model.root = widgets::panel("root", "P");
  Widget column = widgets::column("c");
  column.add(widgets::value("a", "msg.a", "A"));
  column.add(widgets::value("b", "msg.b", "B"));
  column_model.root.add(std::move(column));

  UiModel row_model;
  row_model.root = widgets::panel("root", "P");
  Widget row = widgets::row("r");
  row.add(widgets::value("a", "msg.a", "A"));
  row.add(widgets::value("b", "msg.b", "B"));
  row_model.root.add(std::move(row));

  EXPECT_GT(measure_page(column_model).height_px, measure_page(row_model).height_px);
  EXPECT_EQ(measure_page(row_model).widget_count, 2u);
}
