// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The arithmetic behind the page's click pager. The page does the scrolling in
// JavaScript and there is no browser in this build, so these functions are the
// pinned half and html_runtime.cpp's planPaging mirrors them — the same
// arrangement FieldPath/parsePath already uses.

#include <gtest/gtest.h>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/scroll_paging.hpp"

using namespace qar_ui_model;  // NOLINT(build/namespaces): test readability

TEST(ScrollPaging, APageThatFitsHasNoPager)
{
  const PagingPlan plan = plan_paging(640.0, 600.0);
  EXPECT_FALSE(plan.needed);
  EXPECT_EQ(plan.pages, 1u);
  EXPECT_EQ(plan.page, 1u);
  EXPECT_DOUBLE_EQ(plan.max_scroll_px, 0.0);
  EXPECT_EQ(plan.label, "1 / 1");
  EXPECT_EQ(plan.aria_label, "Page 1 of 1");
}

TEST(ScrollPaging, ExactlyFullIsNotOverflow)
{
  // Sub-pixel layout rounding reports a pixel or two of "overflow" on a page
  // that plainly fits; a rail that appears then is a lie about the content.
  EXPECT_FALSE(plan_paging(640.0, 640.0).needed);
  EXPECT_FALSE(plan_paging(640.0, 640.0 + kOverflowTolerancePx).needed);
  EXPECT_TRUE(plan_paging(640.0, 640.0 + kOverflowTolerancePx + 1.0).needed);
}

TEST(ScrollPaging, OverflowSplitsIntoPagesWithOverlap)
{
  // 1580 px of page in a 640 px viewport: 940 px of scroll, a step of
  // 640 x 0.88 = 563.2, so two steps and three pages.
  const PagingPlan plan = plan_paging(640.0, 1580.0);
  EXPECT_TRUE(plan.needed);
  EXPECT_DOUBLE_EQ(plan.max_scroll_px, 940.0);
  EXPECT_NEAR(plan.step_px, 563.2, 1e-9);
  EXPECT_EQ(plan.pages, 3u);
  EXPECT_EQ(plan.page, 1u);
  EXPECT_EQ(plan.label, "1 / 3");
  EXPECT_EQ(plan.aria_label, "Page 1 of 3");
}

TEST(ScrollPaging, TheStepKeepsAStripOfThePreviousPage)
{
  const PagingPlan plan = plan_paging(1000.0, 5000.0);
  // A reader who loses the line they were on has to find their place again.
  EXPECT_LT(plan.step_px, 1000.0);
  EXPECT_GE(plan.step_px, 1000.0 * kMinPageStepFraction);
  EXPECT_NEAR(plan.step_px, 1000.0 * (1.0 - kPageOverlapFraction), 1e-9);
}

TEST(ScrollPaging, PageNumberTracksTheScrollOffset)
{
  const PagingPlan top = plan_paging(640.0, 1580.0, 0.0);
  EXPECT_EQ(top.page, 1u);
  const PagingPlan middle = plan_paging(640.0, 1580.0, 563.2);
  EXPECT_EQ(middle.page, 2u);
  // At the very bottom the reader is on the LAST page, even though the final
  // step is short and floor() alone would still say 2.
  const PagingPlan bottom = plan_paging(640.0, 1580.0, 940.0);
  EXPECT_EQ(bottom.page, 3u);
  EXPECT_EQ(bottom.label, "3 / 3");
}

TEST(ScrollPaging, ClicksWalkTheWholePageAndStopAtTheEnds)
{
  const PagingPlan plan = plan_paging(640.0, 1580.0);
  double at = 0.0;
  EXPECT_DOUBLE_EQ(page_up_target(plan, at), 0.0);  // already at the top
  at = page_down_target(plan, at);
  EXPECT_NEAR(at, 563.2, 1e-9);
  at = page_down_target(plan, at);
  EXPECT_DOUBLE_EQ(at, 940.0);  // clamped to the bottom, not past it
  at = page_down_target(plan, at);
  EXPECT_DOUBLE_EQ(at, 940.0);
  at = page_up_target(plan, at);
  EXPECT_NEAR(at, 940.0 - 563.2, 1e-9);
  at = page_up_target(plan, at);
  EXPECT_DOUBLE_EQ(at, 0.0);
  // Every page is reachable in (pages - 1) clicks, which is what makes the
  // readout honest.
  EXPECT_EQ(plan_paging(640.0, 1580.0, 940.0).page, plan.pages);
}

TEST(ScrollPaging, DegradesRatherThanFailsOnNonsense)
{
  // A page that cannot be measured still has to render.
  for (const PagingPlan & plan : {
      plan_paging(0.0, 1000.0), plan_paging(640.0, 0.0), plan_paging(-1.0, -1.0),
      plan_paging(640.0, 1580.0, -50.0)})
  {
    EXPECT_GE(plan.pages, 1u);
    EXPECT_GE(plan.page, 1u);
    EXPECT_LE(plan.page, plan.pages);
    EXPECT_GE(plan.max_scroll_px, 0.0);
    EXPECT_FALSE(plan.label.empty());
  }
  EXPECT_FALSE(plan_paging(0.0, 1000.0).needed);
  EXPECT_EQ(plan_paging(640.0, 1580.0, -50.0).page, 1u);
}

TEST(ScrollPaging, ATallerPageNeedsMoreClicks)
{
  const PagingPlan few = plan_paging(640.0, 1200.0);
  const PagingPlan many = plan_paging(640.0, 6000.0);
  EXPECT_LT(few.pages, many.pages);
  EXPECT_DOUBLE_EQ(few.step_px, many.step_px);  // the step is the viewport's
}

TEST(ScrollPaging, TheRailIsOneTouchTargetWideWithTheThemesGap)
{
  const ArTheme theme = ArTheme::dark();
  EXPECT_DOUBLE_EQ(
    pager_rail_width_px(theme),
    static_cast<double>(theme.touch_target_px) + 2.0 * static_cast<double>(theme.gap_px));
  // Doubling the theme doubles the rail, exactly like every other size: the
  // panel stays the same number of metres with twice the pixels behind it.
  EXPECT_DOUBLE_EQ(pager_rail_width_px(ArTheme::dark().scaled(2.0)),
    2.0 * pager_rail_width_px(theme));
}
