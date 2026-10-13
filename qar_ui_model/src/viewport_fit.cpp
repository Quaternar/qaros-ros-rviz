// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ui_model/viewport_fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "qar_ui_model/scroll_paging.hpp"

namespace qar_ui_model
{

namespace
{

bool
positive(double value)
{
  return std::isfinite(value) && value > 0.0;
}

std::string
number(double value, int decimals = 2)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return buffer;
}

}  // namespace

std::vector<double>
natural_page_widths(const UiModel & model, const ArTheme & theme, std::size_t max_columns)
{
  const std::size_t cap = std::min(std::max<std::size_t>(max_columns, 1), kMaxRowColumns);
  std::vector<double> widths;
  widths.reserve(cap);
  for (std::size_t columns = 1; columns <= cap; ++columns) {
    widths.push_back(measure_page(model, theme, columns).width_px);
  }
  return widths;
}

std::size_t
columns_for_viewport(
  const std::vector<double> & natural, double reserve_px, double viewport_css_width)
{
  if (natural.empty()) {
    return 1;
  }
  const double reserve = positive(reserve_px) ? reserve_px : 0.0;
  if (!positive(viewport_css_width)) {
    return 1;
  }
  // Walk up while the layout still fits AS DRAWN. The first one that does not
  // stops the walk: natural width is monotonic in the column count, and a
  // column bought by shrinking the theme costs more text size than it is
  // worth (see the header).
  std::size_t columns = 1;
  for (std::size_t i = 1; i < natural.size(); ++i) {
    if (natural[i] + reserve > viewport_css_width) {
      break;
    }
    columns = i + 1;
  }
  return columns;
}

double
quantise_theme_scale(double scale, int base_font_px, double min_scale)
{
  if (base_font_px <= 0 || !std::isfinite(scale)) {
    return kMaxThemeScale;
  }
  const double base = static_cast<double>(base_font_px);
  const double floor_scale =
    std::min(positive(min_scale) ? min_scale : kMinThemeScale, kMaxThemeScale);
  // DOWN, not to nearest. A font size is a whole number of pixels, so there
  // are only `base_font_px` scales to choose from below 1; taking the next one
  // down is what turns "the page should fit" into "the page fits", because
  // every other size is then scaled by no more than this scale plus half a
  // pixel of rounding. Nearest would round the body font UP as often as not
  // and hand back a page a few per cent wider than the viewport it was fitted
  // to — which is the whole defect, in miniature.
  long font = static_cast<long>(std::floor(base * scale + 1e-9));
  const long floor_font = static_cast<long>(std::ceil(base * floor_scale - 1e-9));
  font = std::max<long>(font, std::max<long>(1, floor_font));
  font = std::min<long>(font, static_cast<long>(base_font_px));
  return static_cast<double>(font) / base;
}

double
theme_scale_for(
  double natural_width_px, double reserve_px, double viewport_css_width, double min_scale)
{
  if (!positive(natural_width_px) || !positive(viewport_css_width)) {
    return kMaxThemeScale;  // Nothing measured: render the page as drawn.
  }
  const double reserve = positive(reserve_px) ? reserve_px : 0.0;
  const double floor_scale =
    std::min(positive(min_scale) ? min_scale : kMinThemeScale, kMaxThemeScale);

  // Everything on the page scales with k, the rail included, so the fit is one
  // division rather than a search: k * natural + k * reserve <= viewport.
  const double fit = viewport_css_width / (natural_width_px + reserve);
  return std::min(std::max(fit, floor_scale), kMaxThemeScale);
}

bool
theme_scale_differs(double applied, double candidate)
{
  if (!std::isfinite(applied) || !std::isfinite(candidate)) {
    return true;
  }
  return std::abs(candidate - applied) > kThemeScaleDeadband;
}

ViewportFit
fit_page_to_viewport(
  const UiModel & model, const ArTheme & base, double viewport_css_width,
  const ViewportFitOptions & options)
{
  const std::size_t cap = std::min(std::max<std::size_t>(options.max_columns, 1), kMaxRowColumns);

  ViewportFit fit;
  fit.theme = base;
  fit.reserve_px = pager_content_reserve_px(base);

  if (!positive(viewport_css_width)) {
    // No page has measured itself yet: the estimate is all there is, and it is
    // the column cap and the full-size theme — exactly what shipped before the
    // page learned to report.
    fit.measured = false;
    fit.metrics = measure_page(model, base, cap);
    fit.columns = fit.metrics.columns;
    fit.natural_width_px = fit.metrics.width_px;
    fit.note = "no viewport measured yet - page drawn at full size";
    return fit;
  }

  fit.measured = true;
  const std::vector<double> natural = natural_page_widths(model, base, cap);
  fit.columns = columns_for_viewport(natural, fit.reserve_px, viewport_css_width);
  fit.natural_width_px = natural[fit.columns - 1];
  fit.scale =
    theme_scale_for(fit.natural_width_px, fit.reserve_px, viewport_css_width, options.min_scale);
  // Quantised to the body font's own pixel grid before anything is scaled by
  // it, so that `theme.base_font_px / base.base_font_px` IS the scale rather
  // than an approximation of it. That matters beyond tidiness: metres per CSS
  // pixel is derived from `base_font_px` alone, so the body text comes out at
  // exactly the angular size it had before the fit, by construction.
  fit.effective_scale =
    quantise_theme_scale(fit.scale, base.base_font_px, options.min_scale);
  fit.theme = base.scaled(fit.effective_scale);
  fit.shrunk = fit.effective_scale < 1.0;
  // Clamped means the floor bit, i.e. the page still does not fit. Recompute
  // the unclamped answer rather than remembering a flag: the floor may also be
  // exactly where the fit landed, and that is not a clamp.
  const double unclamped = viewport_css_width / (fit.natural_width_px + fit.reserve_px);
  fit.clamped = unclamped < fit.scale - 1e-9;

  fit.metrics = measure_page(model, fit.theme, fit.columns);

  fit.note = "viewport " + number(viewport_css_width, 0) + " css px wide, page wants " +
    number(fit.natural_width_px, 0) + " at " + std::to_string(fit.columns) +
    (fit.columns == 1 ? " column" : " columns") + " -> theme x" +
    number(fit.effective_scale) + ", page now " + number(fit.metrics.width_px, 0) + "x" +
    number(fit.metrics.height_px, 0) + " css px";
  if (fit.clamped) {
    fit.note += " [held at the legibility floor - the page will still overflow]";
  }
  return fit;
}

}  // namespace qar_ui_model
