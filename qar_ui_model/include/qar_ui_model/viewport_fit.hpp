// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__VIEWPORT_FIT_HPP_
#define QAR_UI_MODEL__VIEWPORT_FIT_HPP_

/// @file viewport_fit.hpp
/// @brief Fitting a page designed in AR pixels into a viewport somebody else
/// chose — without making the text any smaller to the eye.
///
/// ## The measurement this exists to answer
///
/// The QarOS runtime hands a website panel a **fixed 500 x 500 CSS px viewport
/// at devicePixelRatio 2.0**, whatever metres the panel declares (as measured
/// in docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser"). The page the
/// generator produces for a typical topic wants **756 CSS px** of width. 756
/// into 500 is the whole defect — text mixed one on top of another, buttons
/// squeezed almost next to each other — because the stylesheet is fluid: it
/// does not overflow, it SHRINKS, and 22 px text with 64 px touch targets and
/// 24 px padding simply has nowhere to go in a 500 px column.
///
/// Resolution is not the problem (1000 x 1000 device pixels is plenty) and a
/// bigger panel is not the cure either: the viewport does not follow the panel,
/// so growing the panel magnifies the same squeezed layout.
///
/// ## Two knobs that cancel
///
/// The only way to give the content room is to make the design smaller **in
/// CSS pixels**; the only way to keep it readable is to make the panel bigger
/// **in metres**. They are independent, and they cancel exactly:
///
/// @code
///   theme  = base.scaled(k)                       // k < 1: smaller pixels
///   panel  = viewport_css x metres_per_css_px / k // 1/k: bigger metres
/// @endcode
///
/// because `metres_per_css_px` is derived from `base_font_px` alone
/// (`qar_web_panels/url_panel_sizing.hpp`), so scaling the theme by `k`
/// multiplies it by `1/k`. Every angular size on the panel — text, touch
/// targets, gaps, the lot — comes out **unchanged**, while the layout gets
/// `1/k` more room in the pixels it is actually laid out in: the page is
/// rendered much bigger while the DPI to the eye stays the same.
///
/// ### Touch targets scale too, and that is not a mistake
///
/// 64 px is a POINTING size, not a reading size: it comes from 1–2 degrees of
/// hand-ray jitter, which at 1.5 m is a 2.5–5 cm circle on the panel
/// (ar_theme.hpp). Shrinking it in CSS px would be indefensible on its own —
/// but the panel grows by exactly the same factor, so `touch_target_px x
/// metres_per_css_px` is invariant and the target stays ~4.2 cm of real panel.
/// The reasoning holds ONLY while the panel is free to grow, which is what
/// @ref ViewportFitOptions::min_scale exists to guarantee; past that floor the
/// panel hits the bounds of the room, the compensation stops, and both text
/// and targets would start shrinking for real. So the floor is the legibility
/// clamp and the reachability clamp at once, and a caller that knows the
/// panel's metre bounds should derive it from them rather than take the
/// default (`qar_web_panels::size_url_panel_for` does).
///
/// ## Columns
///
/// Shrinking is a last resort; dropping a column is the cheaper fix and buys
/// bigger text, because a one-column page is narrower and therefore needs less
/// shrinking. `.qar-row > *` has `ArTheme::row_basis_px` as its flex basis, so
/// a two-column row asks for `2 x 260 + gap` before padding — and two 260 px
/// columns plus gaps and padding cannot fit 500 px at full size, which is why
/// the buttons collide. So:
///
///   **Use the most columns that still fit at FULL size; if not even one
///   does, take one column and shrink to fit.**
///
/// That makes the column count a function of the measured viewport rather than
/// a constant, gives a 500 px square viewport a single column, and leaves a
/// desktop browser (or any viewport with room) exactly the page it gets
/// without the fit.
///
/// ## The rail is part of the width
///
/// A page that overflows gets the click pager, and the rail takes
/// `pager_content_reserve_px` out of the content column
/// (`scroll_paging.hpp`). Fitting to the bare viewport width would therefore
/// fit to a width that stops existing the moment the rail appears, so the
/// reserve is subtracted first — unconditionally, because being a little
/// narrow only wastes width while being too wide brings the squeeze back.
/// Solving `k x natural + k x reserve <= viewport` for k is one division; no
/// iteration and no invented safety margin.
///
/// ## Stability
///
/// `k` is a pure function of the reported viewport width and the model's own
/// natural width. Restyling the page does not change the viewport the runtime
/// gives it, so there is no feedback path and the scale cannot oscillate; the
/// page settles in one step. @ref kThemeScaleDeadband exists only to stop
/// style churn on a resize storm that moves the width by a pixel.
///
/// ## Both halves
///
/// The page applies this to itself in JavaScript (`html_runtime.cpp`,
/// `applyFit`) because it is the only party that knows its viewport at first
/// paint, and there is no browser in this build to execute that script. So the
/// arithmetic lives here, in C++, and the script mirrors it line for line —
/// the same arrangement `plan_paging` / `planPaging` already uses. The server
/// runs this mirror on the viewport the page reports back, and sizes the panel
/// in metres from the same `k`; the tests pin the mirror and the emitted
/// markup that feeds it.

#include <cstddef>
#include <string>
#include <vector>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/page_metrics.hpp"
#include "qar_ui_model/ui_model.hpp"

namespace qar_ui_model
{

/// @brief The theme is never scaled UP. Its pixel sizes ARE the design, and a
/// viewport with room to spare should get the page as drawn, not a magnified
/// one: enlarging would shrink the panel in metres for no gain.
constexpr double kMaxThemeScale = 1.0;

/// @brief Floor on the theme scale when the caller names no better one.
///
/// At 0.25 the panel has to be four times the viewport's nominal metres to
/// keep the angular sizes, which is 1.31 m for the runtime's 500 px viewport
/// — just inside the 1.30/1.40 m bounds a panel may occupy. Below
/// it the compensation cannot happen, so text and touch targets would shrink
/// for real; above it they are preserved exactly. A caller holding the actual
/// bounds should compute the floor from them instead.
constexpr double kMinThemeScale = 0.25;

/// @brief Re-applying the scale for less than this is churn, not a fit. One
/// pixel of resize noise must not restyle the document.
constexpr double kThemeScaleDeadband = 0.02;

/// @brief What @ref fit_page_to_viewport may not do.
struct ViewportFitOptions
{
  /// See @ref kMinThemeScale. Clamped into (0, @ref kMaxThemeScale].
  double min_scale = kMinThemeScale;

  /// Never use more columns than this, whatever the viewport allows.
  std::size_t max_columns = kMaxRowColumns;
};

/// @brief The layout a page should adopt for one measured viewport.
struct ViewportFit
{
  /// The scale asked for, continuous, before @ref quantise_theme_scale snaps
  /// it to a whole-pixel body font. 1.0 means the page fits as drawn.
  double scale = 1.0;

  /// What @ref theme is actually drawn at: `theme.base_font_px /
  /// base.base_font_px`, the quantised @ref scale. This — not @ref scale — is
  /// the number a panel's metres must be divided by, because it is the one the
  /// page is rendered at and the one `meters_per_css_px` will read back.
  double effective_scale = 1.0;

  /// Columns a row keeps on one line. 1 is the single-column layout.
  std::size_t columns = 1;

  /// The page did not fit as drawn and was scaled down.
  bool shrunk = false;

  /// @ref ViewportFitOptions::min_scale stopped the shrinking short of a fit.
  /// The page will still overflow horizontally; the alternative is text below
  /// the legibility floor, which is worse.
  bool clamped = false;

  /// No viewport was measured: the fit is the identity and @ref theme is the
  /// base theme.
  bool measured = false;

  /// The base theme scaled by @ref effective_scale. What the page renders at.
  ArTheme theme;

  /// The page measured under @ref theme at @ref columns columns: its width
  /// should now be inside the viewport, and its height is what the pager has
  /// to work from.
  PageMetrics metrics;

  /// Natural width at the BASE theme with @ref columns columns — the number
  /// @ref scale was derived from.
  double natural_width_px = 0.0;

  /// Width the click pager's rail is assumed to take, base theme.
  double reserve_px = 0.0;

  /// One line for a log: what was measured, what was chosen, and why.
  std::string note;
};

/// @brief Natural page width, CSS px, at the base theme, for each column count
/// from 1 to @p max_columns. Index 0 is the single-column layout.
///
/// This is the whole input the page's script needs to make the same decision
/// in the browser, which is why it is a vector and why it is embedded in the
/// page's config block.
std::vector<double> natural_page_widths(
  const UiModel & model, const ArTheme & theme, std::size_t max_columns = kMaxRowColumns);

/// @brief Most columns whose natural width still fits @p viewport_css_width at
/// full size, given @p reserve_px for the pager rail. 1 when none do — and 1
/// when the viewport was not measured, since @p natural is then the only
/// honest answer for a page nobody has measured.
std::size_t columns_for_viewport(
  const std::vector<double> & natural, double reserve_px, double viewport_css_width);

/// @brief `viewport / (natural + reserve)`, clamped to
/// [@p min_scale, @ref kMaxThemeScale]. Degenerate inputs give 1.0: a page
/// that cannot be measured is rendered as drawn.
double theme_scale_for(
  double natural_width_px, double reserve_px, double viewport_css_width,
  double min_scale = kMinThemeScale);

/// @brief @p scale snapped DOWN to the nearest scale a whole-pixel body font
/// can express, never below @p min_scale and never above @ref kMaxThemeScale.
///
/// Two reasons, and both matter. Down rather than to-nearest makes the fit a
/// guarantee: every other theme size is then scaled by at most this plus half
/// a pixel, so the page really does come out inside the viewport instead of a
/// few per cent over it. And snapping at all makes
/// `theme.base_font_px / base.base_font_px` exactly the scale, which is what
/// lets the panel's metres invert it precisely — metres per CSS pixel is
/// derived from `base_font_px` alone.
double quantise_theme_scale(double scale, int base_font_px, double min_scale = kMinThemeScale);

/// @brief True when moving from @p applied to @p candidate is worth restyling
/// the document for. Mirrors the script's deadband.
bool theme_scale_differs(double applied, double candidate);

/// @brief Fit @p model, drawn with @p base, into a viewport
/// @p viewport_css_width CSS px wide.
///
/// @p viewport_css_width <= 0 (nothing measured yet) returns the identity fit:
/// the base theme, the full column cap, `measured == false`.
ViewportFit fit_page_to_viewport(
  const UiModel & model, const ArTheme & base, double viewport_css_width,
  const ViewportFitOptions & options = {});

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__VIEWPORT_FIT_HPP_
