// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_web_panels/url_panel_sizing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <string_view>

namespace qar_web_panels
{

namespace
{

/// Round to millimetres. QarOS takes floats; a panel whose width differs in
/// the seventh decimal between two runs is noise in every log that prints it.
float
to_mm(double meters)
{
  return static_cast<float>(std::round(meters * 1000.0) / 1000.0);
}

bool
parse_positive_double(std::string_view text, double & out)
{
  // Trim, then require the WHOLE token to be the number: "0.9m" and "0.9 0.8"
  // are typos, not values, and silently taking the 0.9 would hide them.
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.remove_prefix(1);
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.remove_suffix(1);
  }
  if (text.empty() || text.size() > 32) {
    return false;
  }
  const std::string token(text);
  char * end = nullptr;
  const double value = std::strtod(token.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  if (!std::isfinite(value) || value <= 0.0) {
    return false;
  }
  out = value;
  return true;
}

std::string
metres(double value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  return buffer;
}

std::string
percent(double fraction)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.0f", fraction * 100.0);
  return buffer;
}

std::string
pixels(double value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.0f", value);
  return buffer;
}

}  // namespace

double
meters_per_css_px(const qar_ui_model::ArTheme & theme)
{
  const double body = static_cast<double>(theme.base_font_px);
  if (!(body > 0.0)) {
    // validate() refuses such a theme, but this function must not divide by
    // zero for a caller that has not validated yet.
    return kQtMetersPerLogicalPx;
  }
  return kQtMetersPerLogicalPx * kQtBodyFontPx / body;
}

UrlPanelSizeResult
size_url_panel(
  const qar_ui_model::PageMetrics & metrics,
  const qar_ui_model::ArTheme & theme,
  const UrlPanelSizing & sizing)
{
  UrlPanelSizeResult out;
  out.metrics = metrics;
  out.size.content_scale = sizing.content_scale;

  const double scale =
    (std::isfinite(sizing.scale) && sizing.scale > 0.0) ? sizing.scale : 1.0;
  // The page may be drawn at a smaller theme so its layout fits the viewport
  // it was given (qar_ui_model/viewport_fit.hpp). Dividing the rate by that
  // scale is what makes the trade even: the design loses pixels, the panel
  // gains metres, and the text subtends the same angle as without the fit. It is
  // arithmetically identical to deriving the rate from `theme.scaled(k)`.
  const double theme_scale =
    (std::isfinite(sizing.theme_scale) && sizing.theme_scale > 0.0) ? sizing.theme_scale : 1.0;
  const double base = ((std::isfinite(sizing.meters_per_css_px) && sizing.meters_per_css_px > 0.0)
    ? sizing.meters_per_css_px
    : meters_per_css_px(theme)) / theme_scale;
  out.theme_scale = theme_scale;

  double width = 0.0;
  double height = 0.0;
  bool pinned = false;
  const bool reported =
    std::isfinite(sizing.viewport_css_width) && sizing.viewport_css_width > 0.0 &&
    std::isfinite(sizing.viewport_css_height) && sizing.viewport_css_height > 0.0;
  if (sizing.fixed_width_m > 0.0 && sizing.fixed_height_m > 0.0 &&
    std::isfinite(sizing.fixed_width_m) && std::isfinite(sizing.fixed_height_m))
  {
    width = sizing.fixed_width_m;
    height = sizing.fixed_height_m;
    pinned = true;
  } else if (reported) {
    // The page measured the viewport it was actually given, so there is nothing
    // left to estimate: the panel IS that viewport, converted at the one rate
    // that makes the body text subtend the right angle. Aspect follows for
    // free, which is what stops the runtime stretching the page non-uniformly.
    width = sizing.viewport_css_width * base * scale;
    height = sizing.viewport_css_height * base * scale;
  } else {
    width = std::max(1.0, metrics.width_px) * base * scale;
    height = std::max(1.0, metrics.height_px) * base * scale;
  }
  out.from_reported_viewport = reported && !pinned;

  // Bounds. The two branches below are NOT a style choice: they follow from
  // whether the viewport is known, and they must disagree.
  //
  // From the ESTIMATE the bounds are PER AXIS, because the estimate assumes the
  // viewport follows the panel and under that assumption the axes mean
  // different things:
  //
  //  * WIDTH is the layout width — narrowing the panel would narrow the CSS
  //    viewport, making rows wrap and text reflow, so the page comes out TALLER
  //    and a uniform shrink is self-defeating. Width keeps its measured value
  //    unless it is itself out of bounds.
  //  * HEIGHT is how much of that layout is on the surface. A page taller than
  //    the room gets the room, and the tail sits below the bottom edge — what
  //    is not acceptable is doing that SILENTLY, so @ref UrlPanelSizeResult
  //    reports the fraction that fits and the note says so.
  //
  // From a REPORTED viewport the assumption is gone — the runtime's viewport is
  // fixed and does not follow the panel — and the rule inverts: see the branch
  // below.
  const double natural_width = width;
  const double natural_height = height;
  if (out.from_reported_viewport) {
    // UNIFORM, and this is the inversion: the reasoning above only holds
    // while the viewport follows the panel. It does not. The
    // page is laid out into a viewport of the runtime's choosing and then
    // mapped onto our surface, so a per-axis clamp does not reflow anything —
    // it just breaks the aspect we came here to fix, and the operator sees the
    // squeeze again. One factor, both axes, aspect preserved exactly.
    double factor = 1.0;
    if (width * factor < sizing.min_width_m) {
      factor = sizing.min_width_m / width;
    }
    if (height * factor < sizing.min_height_m) {
      factor = std::max(factor, sizing.min_height_m / height);
    }
    // The room wins over the floor: a panel that does not fit is worse than one
    // whose text is a little small.
    if (width * factor > sizing.max_width_m) {
      factor = std::min(factor, sizing.max_width_m / width);
    }
    if (height * factor > sizing.max_height_m) {
      factor = std::min(factor, sizing.max_height_m / height);
    }
    width *= factor;
    height *= factor;
    out.clamped = std::abs(factor - 1.0) > 1e-9;
  } else {
    width = std::min(std::max(width, sizing.min_width_m), sizing.max_width_m);
    height = std::min(std::max(height, sizing.min_height_m), sizing.max_height_m);
    out.clamped =
      std::abs(width - natural_width) > 1e-9 || std::abs(height - natural_height) > 1e-9;
  }

  out.size.width_meters = to_mm(width);
  out.size.height_meters = to_mm(height);
  out.aspect = height > 0.0 ? width / height : 1.0;
  if (out.from_reported_viewport) {
    out.effective_meters_per_css_px = width / sizing.viewport_css_width;
    // The honest fold is the browser's, not the panel's: past a fixed viewport
    // nothing is rasterised, so no panel size brings it back. The page's click
    // pager is what reaches it.
    out.visible_fraction =
      (std::isfinite(sizing.content_css_height) &&
      sizing.content_css_height > sizing.viewport_css_height)
      ? sizing.viewport_css_height / sizing.content_css_height
      : 1.0;
  } else {
    // The width clamp is the only thing that changes how many metres a CSS pixel
    // covers; the height clamp changes how many of them are on the surface.
    out.effective_meters_per_css_px = width / std::max(1.0, metrics.width_px);
    out.visible_fraction = natural_height > 0.0 ? std::min(1.0, height / natural_height) : 1.0;
  }

  if (out.from_reported_viewport) {
    out.note = "viewport " + pixels(sizing.viewport_css_width) + "x" +
      pixels(sizing.viewport_css_height) + " css px (reported by the page) -> panel ";
  } else {
    out.note = "page " + pixels(metrics.width_px) + "x" + pixels(metrics.height_px) +
      " css px (" + std::to_string(metrics.widget_count) + " widgets) -> panel ";
  }
  out.note += metres(static_cast<double>(out.size.width_meters)) + "x" +
    metres(static_cast<double>(out.size.height_meters)) + " m";
  if (pinned) {
    out.note += " [pinned]";
  } else if (std::abs(scale - 1.0) > 1e-9) {
    out.note += " [scale x" + metres(scale) + "]";
  }
  if (std::abs(theme_scale - 1.0) > 1e-9) {
    out.note += " [page drawn at theme x" + metres(theme_scale) + "]";
  }
  if (out.from_reported_viewport) {
    out.note += " [aspect matched to the viewport]";
    if (out.clamped) {
      out.note += " [scaled to the bounds]";
    }
    if (out.visible_fraction < 0.999) {
      out.note += " [" + percent(out.visible_fraction) +
        "% of the page is above the fold, page to the rest]";
    }
  } else if (out.visible_fraction < 0.999) {
    out.note += " [clamped: " + percent(out.visible_fraction) +
      "% of the page fits, scroll in AR for the rest]";
  } else if (out.clamped) {
    out.note += " [clamped to the bounds]";
  }
  return out;
}

void
use_reported_viewport(UrlPanelSizing & sizing, const qar_ui_model::ViewportBody & viewport)
{
  sizing.viewport_css_width = viewport.css_width;
  sizing.viewport_css_height = viewport.css_height;
  sizing.content_css_height = viewport.content_height;
}

double
min_theme_scale(const qar_ui_model::ArTheme & theme, const UrlPanelSizing & sizing)
{
  if (!(std::isfinite(sizing.viewport_css_width) && sizing.viewport_css_width > 0.0) ||
    !(std::isfinite(sizing.viewport_css_height) && sizing.viewport_css_height > 0.0))
  {
    return qar_ui_model::kMaxThemeScale;  // Nothing measured: nothing to fit.
  }
  const double rate = (std::isfinite(sizing.meters_per_css_px) && sizing.meters_per_css_px > 0.0)
    ? sizing.meters_per_css_px
    : meters_per_css_px(theme);

  // panel = viewport x rate / k, and the panel may not exceed the room. Solve
  // each axis for k and take the stricter: below it the panel is clamped, the
  // compensation stops, and the shrinking becomes real.
  //
  // @ref UrlPanelSizing::scale is deliberately NOT in this. It is the
  // operator's magnification, not part of the design's legibility floor:
  // folding it in would demand that a x3.00 request be honoured in full, and
  // the only way to refuse a x3.00 that the room cannot hold would be to stop
  // shrinking the theme — which would leave the layout squeezed to protect
  // text that is already three times the size it needs to be.
  double floor_scale = 0.0;
  if (sizing.max_width_m > 0.0) {
    floor_scale = sizing.viewport_css_width * rate / sizing.max_width_m;
  }
  if (sizing.max_height_m > 0.0) {
    floor_scale = std::max(floor_scale, sizing.viewport_css_height * rate / sizing.max_height_m);
  }
  if (!std::isfinite(floor_scale) || floor_scale <= 0.0) {
    return qar_ui_model::kMinThemeScale;
  }
  return std::min(floor_scale, qar_ui_model::kMaxThemeScale);
}

UrlPanelSizeResult
size_url_panel_for(
  const qar_ui_model::UiModel & model,
  const qar_ui_model::ArTheme & theme,
  const UrlPanelSizing & sizing)
{
  // Columns and theme scale first: they decide what the page IS, and the page
  // is what the panel is sized for. The page's own script makes the identical
  // decision from the identical numbers (html_runtime.cpp `applyFit`), so this
  // is a mirror, not an instruction — nothing is sent to the page.
  qar_ui_model::ViewportFitOptions fit_options;
  fit_options.min_scale = min_theme_scale(theme, sizing);
  const qar_ui_model::ViewportFit fit =
    qar_ui_model::fit_page_to_viewport(model, theme, sizing.viewport_css_width, fit_options);

  UrlPanelSizing fitted = sizing;
  fitted.theme_scale = fit.effective_scale;

  UrlPanelSizeResult out = size_url_panel(fit.metrics, theme, fitted);
  out.columns = fit.columns;
  if (fit.measured && (fit.shrunk || fit.columns < qar_ui_model::kMaxRowColumns)) {
    out.note += " [" + std::to_string(fit.columns) +
      (fit.columns == 1 ? " column" : " columns") + "]";
  }
  if (fit.clamped) {
    out.note += " [theme held at the legibility floor]";
  }
  return out;
}

const EnvLookup &
process_environment()
{
  static const EnvLookup lookup = [](const char * name) -> const char * {
#ifdef _MSC_VER
      // getenv is "unsafe" only in the sense that the returned pointer is
      // invalidated by a later putenv. Nothing here keeps it: the value is
      // parsed into a double before this function returns.
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
      return std::getenv(name);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
    };
  return lookup;
}

bool
apply_sizing_overrides(UrlPanelSizing & sizing, const EnvLookup & lookup, std::string & out_note)
{
  out_note.clear();
  if (!lookup) {
    return false;
  }
  bool applied = false;

  if (const char * raw = lookup("QAR_PANEL_WEB_SIZE")) {
    const std::string_view text(raw);
    const std::size_t split = text.find_first_of("xX");
    double w = 0.0;
    double h = 0.0;
    if (split != std::string_view::npos &&
      parse_positive_double(text.substr(0, split), w) &&
      parse_positive_double(text.substr(split + 1), h))
    {
      sizing.fixed_width_m = w;
      sizing.fixed_height_m = h;
      // A pinned size is the operator overruling every rule in this file, so
      // the bounds move out of the way rather than quietly undoing the pin.
      sizing.min_width_m = std::min(sizing.min_width_m, w);
      sizing.max_width_m = std::max(sizing.max_width_m, w);
      sizing.min_height_m = std::min(sizing.min_height_m, h);
      sizing.max_height_m = std::max(sizing.max_height_m, h);
      applied = true;
      out_note = "QAR_PANEL_WEB_SIZE pins panels to " + metres(w) + "x" + metres(h) + " m";
    } else if (!text.empty()) {
      out_note = "QAR_PANEL_WEB_SIZE='" + std::string(text) +
        "' is not <width>x<height> in metres - ignored";
    }
  }

  if (const char * raw = lookup("QAR_PANEL_WEB_SCALE")) {
    const std::string_view text(raw);
    double factor = 0.0;
    if (parse_positive_double(text, factor)) {
      sizing.scale = factor;
      applied = true;
      if (!out_note.empty()) {
        out_note += "; ";
      }
      out_note += "QAR_PANEL_WEB_SCALE scales panels by x" + metres(factor);
    } else if (!text.empty()) {
      if (!out_note.empty()) {
        out_note += "; ";
      }
      out_note += "QAR_PANEL_WEB_SCALE='" + std::string(text) +
        "' is not a positive number - ignored";
    }
  }

  return applied;
}

}  // namespace qar_web_panels
