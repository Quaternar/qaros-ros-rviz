// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__SCROLL_PAGING_HPP_
#define QAR_UI_MODEL__SCROLL_PAGING_HPP_

/// @file scroll_paging.hpp
/// @brief Reaching the part of a page that is below the fold, by CLICKING.
///
/// ## Why there is no scrollbar here
///
/// The obvious answer to "the page is taller than the panel" is a scrollbar,
/// and it is the wrong one in AR. A scrollbar is a DRAG affordance: press on
/// the thumb, hold, move, release. The QarOS SDK has a DRAG gesture with a full
/// lifecycle (qar_qt_streaming docs/ARCHITECTURE.md, "Input path"), so the API
/// can express dragging a thumb; paging is a CHOICE,
/// not a workaround: a thumb is a few millimetres of panel that an operator has
/// to hit and hold in mid air with an unsupported arm, which is a poor target
/// even when the API can express it. Discrete jumps that are each complete in
/// themselves are the better fit. A WEBSITE panel's contacts are handled by the
/// renderer's own browser anyway, so this page never sees our gesture path.
///
/// So overflow is reached by paging: discrete jumps, one click each, each one
/// complete in itself. That is a worse fit for a mouse and the only thing that
/// works for a hand ray, and it is why the rail also carries a "page 2 of 3"
/// readout — without a bar there is no other cue that more content exists or
/// where in it you are.
///
/// ## What this header is
///
/// The page does the scrolling, in JavaScript, and JavaScript cannot be
/// executed in this build (no browser links into the tests). So the arithmetic
/// lives here, in C++, and `html_runtime.cpp` mirrors it line for line — the
/// same arrangement `FieldPath` / `parsePath` and `set_path` / `setPath`
/// already use. These functions are what the tests pin; the script is checked
/// against them by reading, and by the golden markup that wires them up.
///
/// Everything is in CSS pixels, in the page's own coordinates: `viewport_px`
/// is the height of the box that scrolls, `content_px` is the height of what
/// is inside it, `scroll_px` is how far down it currently sits.

#include <cstddef>
#include <string>

#include "qar_ui_model/ar_theme.hpp"

namespace qar_ui_model
{

/// @brief Content must exceed the viewport by at least this much before the
/// pager appears. Sub-pixel layout rounding routinely reports one or two
/// pixels of "overflow" on a page that plainly fits, and a rail that flickers
/// into existence on a page with nothing below the fold is a lie.
constexpr double kOverflowTolerancePx = 4.0;

/// @brief Hysteresis for leaving paged mode. Turning the pager on adds a rail,
/// which narrows the content column, which can reflow the page TALLER — so the
/// condition that turns it off must be clear of the condition that turned it
/// on, or a page sitting exactly on the boundary oscillates forever.
constexpr double kPagerExitMarginPx = 32.0;

/// @brief How much of the previous page stays on screen after a page jump, as
/// a fraction of the viewport. A reader who loses the line they were on has to
/// re-find their place; keeping a strip of the old page is what a page-down
/// key has always done.
constexpr double kPageOverlapFraction = 0.12;

/// @brief A page step is never smaller than this fraction of the viewport,
/// whatever the overlap works out to.
constexpr double kMinPageStepFraction = 0.5;

/// @brief The state of one scrollable page: how many clicks it takes to see
/// all of it, and which click we are on.
struct PagingPlan
{
  /// The pager is worth showing: content exceeds the viewport by more than
  /// @ref kOverflowTolerancePx. False means every other member is the trivial
  /// one-page answer.
  bool needed = false;

  /// Total pages, >= 1. 1 when @ref needed is false.
  std::size_t pages = 1;

  /// Current page, 1-based, in [1, pages].
  std::size_t page = 1;

  /// How far one page-up / page-down click moves, CSS px.
  double step_px = 0.0;

  /// Largest meaningful scroll offset: content_px - viewport_px, >= 0.
  double max_scroll_px = 0.0;

  /// What the rail shows: "2 / 3". Short because the rail is one touch target
  /// wide; the sentence is in @ref aria_label.
  std::string label;

  /// "Page 2 of 3" — the accessible name of the readout, and what a log or a
  /// status line should say.
  std::string aria_label;
};

/// @brief Plan the pager for a box @p viewport_px tall holding @p content_px
/// of content, currently scrolled to @p scroll_px.
///
/// Tolerant of nonsense (negative, NaN, content shorter than the viewport):
/// a page that cannot be measured must still render, so the answer degrades to
/// "one page, no pager" rather than failing.
PagingPlan plan_paging(double viewport_px, double content_px, double scroll_px = 0.0);

/// @brief Scroll offset one page-up click lands on, clamped to [0, max].
double page_up_target(const PagingPlan & plan, double scroll_px);

/// @brief Scroll offset one page-down click lands on, clamped to [0, max].
double page_down_target(const PagingPlan & plan, double scroll_px);

/// @brief Width the pager rail occupies, CSS px: one touch target plus the
/// theme's gap on each side. The panel reserves exactly this much padding on
/// its right when the rail is up, which is why both sides compute it from the
/// same place rather than agreeing on a literal.
double pager_rail_width_px(const ArTheme & theme);

/// @brief CSS px the rail costs the CONTENT column: the rail itself plus one
/// more gap between it and the text, i.e. the stylesheet's
/// `[data-qar-pager] .qar-panel { padding-right: calc(touch + gap * 3) }`.
///
/// A page that overflows gets a rail, and a rail narrows the column it was
/// measured against, so anything fitting a page to a known viewport has to
/// subtract this first or it fits the page to a width that will not exist.
/// @ref viewport_fit.hpp is the caller; reserving it unconditionally costs a
/// page that does not overflow a little unused width, which is the cheap way
/// to be wrong.
double pager_content_reserve_px(const ArTheme & theme);

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__SCROLL_PAGING_HPP_
