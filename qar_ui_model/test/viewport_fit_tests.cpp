// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Fitting a page into a viewport the runtime chose for it. The page applies
// this to itself in JavaScript and there is no browser in this build, so these
// functions are the pinned half and html_runtime.cpp's applyFit mirrors them —
// the same arrangement plan_paging/planPaging already uses.
//
// The numbers that appear as literals here are measured ones: a 500 x 500 CSS
// px viewport at devicePixelRatio 2.0 (docs/SDK_COMPATIBILITY.md, "The QarOS
// in-AR browser"), and a generated topic page that wants 756 CSS px of width.

#include <gtest/gtest.h>

#include <cmath>
#include <string>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/page_metrics.hpp"
#include "qar_ui_model/scroll_paging.hpp"
#include "qar_ui_model/viewport_fit.hpp"

using namespace qar_ui_model;  // NOLINT(build/namespaces): test readability

namespace
{

/// The shape every generated topic panel has: a section of N value displays.
/// No Row, so its width is the comfortable text measure, not a row demand.
UiModel form(std::size_t n)
{
  UiModel model;
  model.root = widgets::panel("root", "/ar_demo/cmd_vel");
  Widget section = widgets::section("s", "Message");
  for (std::size_t i = 0; i < n; ++i) {
    section.add(widgets::value("v" + std::to_string(i), "msg.f" + std::to_string(i), "Field"));
  }
  model.root.add(std::move(section));
  return model;
}

/// A page with a three-across row, so the column count actually changes the
/// natural width: three 260 px columns ask for more than the text measure.
UiModel wide_form()
{
  UiModel model;
  model.root = widgets::panel("root", "/ar_demo/pose");
  Widget section = widgets::section("s", "Pose");
  Widget row = widgets::row("r");
  row.add(widgets::number("x", "msg.x", "X"));
  row.add(widgets::number("y", "msg.y", "Y"));
  row.add(widgets::number("z", "msg.z", "Z"));
  section.add(std::move(row));
  model.root.add(std::move(section));
  return model;
}

/// The live viewport, both axes.
constexpr double kLiveViewportPx = 500.0;

}  // namespace

// ---------------------------------------------------------------------------
// The measurement this whole file exists for
// ---------------------------------------------------------------------------

TEST(ViewportFit, TheGeneratedPageWantsMoreWidthThanTheRuntimeGivesIt)
{
  const ArTheme theme;
  // 60 characters of 22 px body text, plus the panel's and the section's
  // padding: the 756 px the live log reported.
  EXPECT_DOUBLE_EQ(measure_page(form(16), theme, 1).width_px, 756.0);
  EXPECT_GT(measure_page(form(16), theme, 1).width_px, kLiveViewportPx);
}

TEST(ViewportFit, TheLiveFiveHundredSquareViewportGetsOneColumnAndTwelvePixelText)
{
  const ArTheme theme;
  const ViewportFit fit = fit_page_to_viewport(form(16), theme, kLiveViewportPx);

  EXPECT_TRUE(fit.measured);
  EXPECT_TRUE(fit.shrunk);
  EXPECT_FALSE(fit.clamped) << "0.545 is well clear of the legibility floor";
  EXPECT_EQ(fit.columns, 1u);
  EXPECT_DOUBLE_EQ(fit.natural_width_px, 756.0);
  // 64 px of rail plus the 3 x 16 px of gap around it: the content column the
  // page will actually have once it overflows and the pager appears.
  EXPECT_DOUBLE_EQ(fit.reserve_px, 112.0);
  // 500 / (756 + 112) = 0.576, snapped down onto the whole-pixel body font.
  EXPECT_NEAR(fit.scale, 500.0 / 868.0, 1e-9);
  EXPECT_DOUBLE_EQ(fit.effective_scale, 12.0 / 22.0);
  EXPECT_EQ(fit.theme.base_font_px, 12);
  EXPECT_EQ(fit.theme.small_font_px, 10);
  EXPECT_EQ(fit.theme.touch_target_px, 35);
}

TEST(ViewportFit, TheFittedPageActuallyFitsRailIncluded)
{
  const ArTheme theme;
  const ViewportFit fit = fit_page_to_viewport(form(16), theme, kLiveViewportPx);
  // The whole point: page plus rail inside the viewport, not a few per cent
  // over it. This is what snapping the scale DOWN buys.
  EXPECT_LE(fit.metrics.width_px + pager_content_reserve_px(fit.theme), kLiveViewportPx);
  EXPECT_LT(fit.metrics.width_px, measure_page(form(16), theme).width_px)
    << "the fitted page is narrower than the page as drawn";
}

TEST(ViewportFit, AViewportWithRoomChangesNothingAtAll)
{
  const ArTheme theme;
  // A desktop browser, or any runtime that gives the page the width it asks
  // for: the fit must be the identity, or every existing page changes.
  const ViewportFit fit = fit_page_to_viewport(form(8), theme, 1600.0);
  EXPECT_TRUE(fit.measured);
  EXPECT_FALSE(fit.shrunk);
  EXPECT_DOUBLE_EQ(fit.effective_scale, 1.0);
  EXPECT_EQ(fit.theme.base_font_px, theme.base_font_px);
  EXPECT_EQ(fit.theme.touch_target_px, theme.touch_target_px);
}

TEST(ViewportFit, NoViewportMeansTheEstimateExactlyAsBefore)
{
  const ArTheme theme;
  const ViewportFit fit = fit_page_to_viewport(form(8), theme, 0.0);
  EXPECT_FALSE(fit.measured);
  EXPECT_DOUBLE_EQ(fit.effective_scale, 1.0);
  EXPECT_EQ(fit.theme.base_font_px, theme.base_font_px);
  EXPECT_DOUBLE_EQ(fit.metrics.width_px, measure_page(form(8), theme).width_px);
  EXPECT_DOUBLE_EQ(fit.metrics.height_px, measure_page(form(8), theme).height_px);
  EXPECT_NE(fit.note.find("no viewport"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Columns: a function of the measured viewport, not a constant
// ---------------------------------------------------------------------------

TEST(ViewportFit, ANarrowViewportGetsOneColumnAndAWideOneGetsThree)
{
  const ArTheme theme;
  const std::vector<double> natural = natural_page_widths(wide_form(), theme);
  ASSERT_EQ(natural.size(), 3u);
  EXPECT_DOUBLE_EQ(natural[0], 756.0);              // the text measure wins
  EXPECT_DOUBLE_EQ(natural[1], 756.0);              // 2 x 260 + 16 is still less
  EXPECT_DOUBLE_EQ(natural[2], 908.0);              // 3 x 260 + 32 = 812, + padding
  EXPECT_LT(natural[0], natural[2]) << "more columns is a wider page";

  const double reserve = pager_content_reserve_px(theme);
  EXPECT_EQ(columns_for_viewport(natural, reserve, 500.0), 1u);
  EXPECT_EQ(columns_for_viewport(natural, reserve, 900.0), 2u);
  EXPECT_EQ(columns_for_viewport(natural, reserve, 1200.0), 3u);

  // And the whole fit follows it.
  EXPECT_EQ(fit_page_to_viewport(wide_form(), theme, 500.0).columns, 1u);
  EXPECT_EQ(fit_page_to_viewport(wide_form(), theme, 1200.0).columns, 3u);
}

TEST(ViewportFit, DroppingAColumnIsPreferredToShrinkingForIt)
{
  const ArTheme theme;
  // 1000 px: three columns (1020 with the rail) would need the theme shrunk,
  // two fit as drawn. Two at full size reads better than three at 98 %.
  const ViewportFit fit = fit_page_to_viewport(wide_form(), theme, 1000.0);
  EXPECT_EQ(fit.columns, 2u);
  EXPECT_DOUBLE_EQ(fit.effective_scale, 1.0);
}

TEST(ViewportFit, OneColumnIsTallerWhichIsWhatThePagerHasToCopeWith)
{
  const ArTheme theme;
  // The same three widgets, side by side or stacked. A square viewport gets
  // the stacked one, so the page is taller than the estimate ever said and
  // paging has to be planned from the FITTED height, not the drawn one.
  EXPECT_GT(
    measure_page(wide_form(), theme, 1).height_px, measure_page(wide_form(), theme, 3).height_px);
}

// ---------------------------------------------------------------------------
// The scale itself
// ---------------------------------------------------------------------------

TEST(ViewportFit, TheScaleIsTheViewportOverThePagePlusTheRail)
{
  EXPECT_NEAR(theme_scale_for(756.0, 112.0, 500.0), 500.0 / 868.0, 1e-12);
  // A page that fits is never magnified: the theme's pixels are the design.
  EXPECT_DOUBLE_EQ(theme_scale_for(400.0, 112.0, 2000.0), 1.0);
  EXPECT_DOUBLE_EQ(theme_scale_for(756.0, 112.0, 868.0), 1.0);
  // Nothing measurable: render as drawn rather than guess.
  EXPECT_DOUBLE_EQ(theme_scale_for(756.0, 112.0, 0.0), 1.0);
  EXPECT_DOUBLE_EQ(theme_scale_for(0.0, 112.0, 500.0), 1.0);
}

TEST(ViewportFit, TheScaleIsSnappedDownOntoAWholePixelBodyFont)
{
  // 22 px of body text has 22 usable scales below 1, and the fit takes the one
  // below rather than the nearest: rounding up is how a "fitted" page comes
  // out wider than the box it was fitted to.
  EXPECT_DOUBLE_EQ(quantise_theme_scale(0.576, 22, 0.25), 12.0 / 22.0);
  EXPECT_DOUBLE_EQ(quantise_theme_scale(0.99, 22, 0.25), 21.0 / 22.0);
  EXPECT_DOUBLE_EQ(quantise_theme_scale(1.0, 22, 0.25), 1.0);
  EXPECT_DOUBLE_EQ(quantise_theme_scale(2.0, 22, 0.25), 1.0) << "never scaled up";
  // The floor is a floor even when snapping would step below it.
  EXPECT_GE(quantise_theme_scale(0.26, 22, 0.25), 0.25);
}

TEST(ViewportFit, TheLegibilityFloorStopsTheShrinking)
{
  const ArTheme theme;
  ViewportFitOptions options;
  options.min_scale = 0.5;
  // A viewport far too narrow for the page: without a floor the theme would go
  // to 0.29 and the panel would have to be 2.1 m wide to pay for it.
  const ViewportFit fit = fit_page_to_viewport(form(16), theme, 250.0, options);
  EXPECT_TRUE(fit.clamped);
  EXPECT_DOUBLE_EQ(fit.effective_scale, 0.5);
  EXPECT_EQ(fit.theme.base_font_px, 11);
  EXPECT_GT(fit.metrics.width_px, 250.0) << "it still does not fit, and says so";
  EXPECT_NE(fit.note.find("legibility floor"), std::string::npos);

  // Landing exactly on the floor is not a clamp.
  ViewportFitOptions exact;
  exact.min_scale = 500.0 / 868.0;
  EXPECT_FALSE(fit_page_to_viewport(form(16), theme, kLiveViewportPx, exact).clamped);
}

TEST(ViewportFit, TheDeadbandIgnoresResizeNoise)
{
  // The scale is a pure function of the viewport width and the page's own
  // natural width, so it cannot oscillate; the deadband exists only to stop a
  // resize storm restyling the document for a pixel.
  EXPECT_FALSE(theme_scale_differs(0.5455, 0.5460));
  EXPECT_TRUE(theme_scale_differs(0.5455, 0.6000));
  EXPECT_TRUE(theme_scale_differs(0.5455, std::nan("")));
}

TEST(ViewportFit, TheSameViewportAlwaysGivesTheSameAnswer)
{
  const ArTheme theme;
  const ViewportFit first = fit_page_to_viewport(form(16), theme, kLiveViewportPx);
  // Re-running the fit on the fit's own output must not move: the runtime's
  // viewport does not follow the panel, so restyling cannot feed back.
  const ViewportFit again = fit_page_to_viewport(form(16), theme, kLiveViewportPx);
  EXPECT_DOUBLE_EQ(again.effective_scale, first.effective_scale);
  EXPECT_EQ(again.columns, first.columns);
}

// ---------------------------------------------------------------------------
// The invariant the whole design rests on
// ---------------------------------------------------------------------------

TEST(ViewportFit, ShrinkingTheThemeDoesNotShrinkAnythingTheOperatorSees)
{
  // Restating url_panel_sizing.hpp's derivation locally, because this library
  // must not depend on that one: metres per CSS pixel is 0.0012 x 12 / body.
  const auto metres_per_px = [](const ArTheme & t) {
    return 0.0012 * 12.0 / static_cast<double>(t.base_font_px);
  };
  const ArTheme base;
  const ViewportFit fit = fit_page_to_viewport(form(16), base, kLiveViewportPx);

  // Body text: exact, by construction of the quantisation.
  EXPECT_DOUBLE_EQ(
    fit.theme.base_font_px * metres_per_px(fit.theme),
    base.base_font_px * metres_per_px(base));

  // Touch targets: 64 px is a POINTING size, from ray jitter, not a reading
  // size — so shrinking it in CSS px is only defensible because the panel
  // grows by the same factor and the physical target is preserved. Half a
  // millimetre of rounding on a 42 mm target.
  EXPECT_NEAR(
    fit.theme.touch_target_px * metres_per_px(fit.theme),
    base.touch_target_px * metres_per_px(base), 0.0005);

  // And the smallest text on the page never loses ground either.
  EXPECT_GE(
    fit.theme.small_font_px * metres_per_px(fit.theme),
    base.small_font_px * metres_per_px(base) - 1e-9);
}

// ---------------------------------------------------------------------------
// Paging, after the scale has moved
// ---------------------------------------------------------------------------
//
// The fit changes the page's height, so anything planned from the drawn page
// is stale the moment it is applied. The runtime re-measures (applyFit runs
// before updatePager on both boot and resize); these pin what that buys.

TEST(ViewportFit, PagingIsPlannedFromTheFittedPageNotTheDrawnOne)
{
  const ArTheme theme;
  const double drawn_height = measure_page(form(16), theme).height_px;
  const ViewportFit fit = fit_page_to_viewport(form(16), theme, kLiveViewportPx);
  EXPECT_LT(fit.metrics.height_px, drawn_height);

  const PagingPlan drawn = plan_paging(kLiveViewportPx, drawn_height);
  const PagingPlan fitted = plan_paging(kLiveViewportPx, fit.metrics.height_px);
  EXPECT_TRUE(fitted.needed) << "a 16-field form still does not fit a 500 px fold";
  EXPECT_LT(fitted.pages, drawn.pages) << "fewer clicks to read the same form";
}

TEST(ViewportFit, ASmallFormStopsNeedingThePagerOnceItIsFitted)
{
  const ArTheme theme;
  const ViewportFit fit = fit_page_to_viewport(form(4), theme, kLiveViewportPx);
  EXPECT_TRUE(plan_paging(kLiveViewportPx, measure_page(form(4), theme).height_px).needed);
  EXPECT_FALSE(plan_paging(kLiveViewportPx, fit.metrics.height_px).needed);
}

TEST(ViewportFit, ASquareViewportIsShortAndThePagerCopes)
{
  // The live log's other number: content 500x548 against a 500 px fold. Only
  // 48 px hang over, and the plan must be two pages whose second click lands
  // ON the end rather than a full step past it — the layout the pager was
  // written for was tall and portrait, and a square fold is neither.
  const PagingPlan plan = plan_paging(kLiveViewportPx, 548.0);
  EXPECT_TRUE(plan.needed);
  EXPECT_EQ(plan.pages, 2u);
  EXPECT_DOUBLE_EQ(plan.max_scroll_px, 48.0);
  EXPECT_DOUBLE_EQ(page_down_target(plan, 0.0), 48.0);
  EXPECT_EQ(plan_paging(kLiveViewportPx, 548.0, 48.0).page, 2u) << "the end is the last page";
  EXPECT_EQ(plan.label, "1 / 2");
}
