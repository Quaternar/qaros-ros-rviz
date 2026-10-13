// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__PAGE_METRICS_HPP_
#define QAR_UI_MODEL__PAGE_METRICS_HPP_

/// @file page_metrics.hpp
/// @brief How big the generated page WANTS to be, in CSS pixels.
///
/// This is the authoritative natural size of a `UiModel` page, and it lives
/// here because this library is the only one that owns both halves of the
/// answer: @ref ArTheme fixes every size the page uses (22 px body text, 64 px
/// touch targets, 24 px padding, 16 px gaps …) and html_renderer.cpp fixes the
/// box tree those sizes are poured into. Anything downstream that needs a
/// physical size — a QarOS URL panel's `width_meters` / `height_meters`, a
/// browser window for a desktop preview — asks here first and then converts.
///
/// ## Why a measurement and not a constant
///
/// The stylesheet is deliberately FLUID: there is no `width` on `body`, no
/// `max-width` on the panel, and `.qar-row > *` is `flex: 1 1 260px`, so the
/// page reflows into whatever viewport it is given. That is the right
/// behaviour for a page and the wrong input for a panel: a panel has to be
/// committed to a physical size and aspect BEFORE the browser has laid
/// anything out, and if the panel's aspect disagrees with the content's the
/// page overflows its viewport and the operator sees only the part above the
/// fold (see docs/ARCHITECTURE.md, "Panel sizing").
///
/// So we compute what the layout would come to, from the same numbers the
/// stylesheet uses. It is an estimate, not a layout engine: text is not
/// shaped, so a long label that wraps to two lines is counted as one. It is
/// deliberately an estimate of the TALLEST reasonable reading, because being a
/// little too big only wastes panel, while being too small crops.
///
/// ## Width
///
/// Two things ask for width, and the natural width is whichever asks louder:
///
///  * **Text measure.** Typographic practice puts a comfortable line at 45–75
///    characters; 60 is the middle of that range. At an average advance of
///    ~0.5 em for mixed-case Latin, 60 characters of 22 px body text is
///    60 × 0.5 × 22 = 660 px.
///  * **The widest row.** `.qar-row > *` has `ArTheme::row_basis_px` (260 px)
///    as its flex basis and the row wraps below that, so a row of N children
///    wants N × `row_basis_px` + (N − 1) × `gap_px`. N is capped at
///    @ref kMaxRowColumns: a panel wide enough for four columns of content
///    subtends more than the ~30° an operator reads without turning their
///    head, and the CSS already wraps gracefully, so past three columns the
///    better layout is taller, not wider. A caller that has MEASURED the
///    viewport passes a tighter cap — see @ref viewport_fit.hpp — and this
///    measurement follows it on both axes: fewer columns is a narrower and
///    taller page, which is exactly what a narrow viewport wants.
///
/// Panel padding (and section padding, when the model uses sections) is added
/// around that content column.
///
/// ## Height
///
/// Summed structurally, walking the same tree the renderer walks: the panel's
/// padding and header, then each section's padding, title and children, then a
/// per-type nominal height for each leaf built out of the theme's own numbers
/// (a value display is caption + readout, a number input is caption + a
/// touch-target-high stepper, a table is its header row plus a page of rows
/// plus a pager, …). Rows contribute the height of their tallest child times
/// the number of wrapped lines.
///
/// Consequence, and the point of the exercise: a page with more widgets comes
/// out taller, and the panel's aspect follows the content instead of a guess.

#include <cstddef>
#include <string>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/ui_model.hpp"

namespace qar_ui_model
{

/// @brief `line-height` on `body` in the component stylesheet.
constexpr double kPageLineHeight = 1.4;

/// @brief Most columns a row is assumed to keep on one line when the panel is
/// sized. Beyond this the row wraps, which is the better layout in AR. The
/// default cap; @ref measure_page takes a smaller one.
constexpr std::size_t kMaxRowColumns = 3;

/// @brief Middle of the 45–75 character range for a comfortable line.
constexpr double kComfortableLineChars = 60.0;

/// @brief Average advance width of mixed-case Latin, in em.
constexpr double kAverageCharAdvanceEm = 0.5;

/// @brief Nominal height allowed for a JSON inspector, as a multiple of
/// `ArTheme::block_px`. The element is `max-height: calc(var(--qar-block) * 4)`
/// with `overflow: auto`, so it scrolls rather than grows; half the maximum is
/// what a typical elided message fills.
constexpr double kJsonInspectorBlocks = 2.0;

/// @note Every other length of the page — `.qar-widget`'s inner gap, the
/// connection chip, the gauge track, the image placeholder, a component box's
/// padding and gap — is an @ref ArTheme field (`gap_tight_px`, `chip_px`,
/// `dot_px`, `block_px`, `pad_micro_px`, `gap_micro_px`), not a literal here
/// and another in the stylesheet: with literals, a scaled theme
/// (@ref ArTheme::scaled) would shrink the text and leave the boxes around it
/// at full size. The stylesheet and this measurement read the same fields, so
/// a measurement of a scaled page is the measurement of the unscaled page
/// times the scale, and nothing is laid out at two sizes at once.

/// @brief What the page asks for, in CSS pixels.
struct PageMetrics
{
  /// Natural width and height of the page, CSS px. Always >= 1.
  double width_px = 0.0;
  double height_px = 0.0;

  /// width / height. > 1 is landscape. Convenience, so a caller does not have
  /// to guard the division.
  double aspect = 1.0;

  /// Leaf (non-container) widgets counted. Diagnostics, and what the "more
  /// widgets means a taller panel" test asserts against.
  std::size_t widget_count = 0;

  /// Containers counted, for diagnostics.
  std::size_t section_count = 0;

  /// Largest number of children on any Row, BEFORE the @ref kMaxRowColumns
  /// cap. A row wider than the cap wraps; this reports what it asked for.
  std::size_t widest_row = 0;

  /// Columns this measurement assumed a row keeps on one line: the cap the
  /// caller passed, never more than @ref widest_row asked for. 1 is the
  /// single-column layout a narrow viewport gets (@ref viewport_fit.hpp).
  std::size_t columns = 1;
};

/// @brief Measure the page @p model would render to under @p theme.
///
/// Never fails: an empty or malformed model still yields a positive size (the
/// panel chrome alone), because a caller sizing a panel needs a number more
/// than it needs an error — the model is validated by the renderer.
///
/// @param max_columns Most children a Row is assumed to keep on one line.
///   Clamped to 1..@ref kMaxRowColumns. Pass 1 to measure the single-column
///   layout the page is given when the viewport is too narrow for two.
PageMetrics measure_page(
  const UiModel & model, const ArTheme & theme = ArTheme::dark(),
  std::size_t max_columns = kMaxRowColumns);

/// @brief The natural height of one widget subtree, CSS px. Public so the
/// per-type table can be pinned by tests and reused by a partial renderer.
double measure_widget_height(
  const Widget & widget, const ArTheme & theme, std::size_t max_columns = kMaxRowColumns);

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__PAGE_METRICS_HPP_
