// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ui_model/scroll_paging.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace qar_ui_model
{

namespace
{

/// A measurement that is not a finite, non-negative number is not a
/// measurement. Zero is the safe reading: it collapses the plan to one page.
double
sane(double value)
{
  return (std::isfinite(value) && value > 0.0) ? value : 0.0;
}

std::string
count(std::size_t n)
{
  return std::to_string(n);
}

}  // namespace

PagingPlan
plan_paging(double viewport_px, double content_px, double scroll_px)
{
  PagingPlan plan;
  const double viewport = sane(viewport_px);
  const double content = sane(content_px);

  plan.max_scroll_px = std::max(0.0, content - viewport);
  plan.needed = viewport > 0.0 && content > viewport + kOverflowTolerancePx;
  if (!plan.needed) {
    plan.pages = 1;
    plan.page = 1;
    plan.step_px = viewport;
    plan.label = "1 / 1";
    plan.aria_label = "Page 1 of 1";
    return plan;
  }

  plan.step_px = std::max(
    viewport * (1.0 - kPageOverlapFraction), viewport * kMinPageStepFraction);

  // Pages are the first screenful plus one per step needed to bring the tail
  // into view. ceil, because a step that lands short of the bottom still owes
  // the reader one more click.
  const double steps = std::ceil(plan.max_scroll_px / plan.step_px - 1e-9);
  plan.pages = static_cast<std::size_t>(std::max(1.0, steps)) + 1;

  const double scroll = std::min(std::max(sane(scroll_px), 0.0), plan.max_scroll_px);
  // The last page is claimed by REACHING the bottom, not by passing a step
  // boundary: the final step is usually short, so floor() alone would report
  // "page 2 of 3" while the reader is looking at the end of the document.
  std::size_t page;
  if (scroll >= plan.max_scroll_px - 1.0) {
    page = plan.pages;
  } else {
    page = static_cast<std::size_t>(std::floor(scroll / plan.step_px + 1e-9)) + 1;
    page = std::min(page, plan.pages);
  }
  plan.page = std::max<std::size_t>(1, page);

  plan.label = count(plan.page) + " / " + count(plan.pages);
  plan.aria_label = "Page " + count(plan.page) + " of " + count(plan.pages);
  return plan;
}

double
page_up_target(const PagingPlan & plan, double scroll_px)
{
  const double scroll = std::min(std::max(sane(scroll_px), 0.0), plan.max_scroll_px);
  return std::min(std::max(scroll - plan.step_px, 0.0), plan.max_scroll_px);
}

double
page_down_target(const PagingPlan & plan, double scroll_px)
{
  const double scroll = std::min(std::max(sane(scroll_px), 0.0), plan.max_scroll_px);
  return std::min(std::max(scroll + plan.step_px, 0.0), plan.max_scroll_px);
}

double
pager_rail_width_px(const ArTheme & theme)
{
  return static_cast<double>(theme.touch_target_px) + 2.0 * static_cast<double>(theme.gap_px);
}

double
pager_content_reserve_px(const ArTheme & theme)
{
  return static_cast<double>(theme.touch_target_px) + 3.0 * static_cast<double>(theme.gap_px);
}

}  // namespace qar_ui_model
