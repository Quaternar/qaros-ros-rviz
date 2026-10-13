// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_WEB_PANELS__URL_PANEL_SIZING_HPP_
#define QAR_WEB_PANELS__URL_PANEL_SIZING_HPP_

/// @file url_panel_sizing.hpp
/// @brief CSS pixels -> metres for a browser-backed AR panel.
///
/// `qar_ui_model::measure_page` says how big the generated page wants to be in
/// CSS pixels; this header turns that into the `QarGuiPanelSize` a
/// @ref UrlPanelPlacement carries. It is the URL route's answer to what
/// panel_geometry.hpp of qar_qt_streaming does for the stream route, and the two
/// deliberately agree on the only question that matters for legibility: how
/// many metres one logical/CSS pixel is worth.
///
/// ## Why the panel is sized from the page
///
/// A generated form is PORTRAIT: a column of sections, each a stack of
/// caption+control pairs. A panel declared with a guessed landscape shape
/// (0.5 x 0.35 m, say) crops it: `.qar-panel` is `min-height: 100vh`, the
/// content runs past the fold, and what the operator sees is the top of the
/// page. Sizing from the page's own measurement fixes the aspect; because the
/// measurement is in the AR theme's own pixels (22 px body text, 64 px touch
/// targets) it fixes the physical size at the same time.
///
/// ## The viewport does NOT follow the panel, so the page is asked
///
/// The browser viewport is not the panel's metres times some pixel density:
/// declaring a portrait panel does not give the page a portrait viewport. A
/// page sized to a portrait panel comes out HORIZONTALLY SQUEEZED, and the
/// squeeze goes away only when the size multiplier drives both axes into their
/// bounds (1.40 x 1.30 m), which makes the panel almost square. A page that
/// looks right in a square panel and squeezed in a portrait one is being
/// rendered into a viewport of its own shape and then stretched non-uniformly
/// onto whatever surface we declared.
///
/// There is no header field that says what that shape is, so the page is asked
/// instead: `qar-ui/1` has a `viewport` frame (`ui_protocol.hpp`), the page
/// measures `window.innerWidth`/`innerHeight` and reports them, and
/// @ref UrlPanelSizing::viewport_css_width / height carry the answer back here
/// (docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser", records the
/// measurements).
///
/// **What we do with it: match the panel's physical aspect to the reported
/// viewport, exactly.** The alternative — re-rendering the page to a shape we
/// choose — is not available: we do not own the viewport, the runtime does, and
/// the page is already fluid and lays itself out into whatever it is handed.
/// Fighting that would mean guessing. Matching it is also the sharper
/// answer, because once the viewport is known the panel is not an
/// estimate at all: it is `viewport_css x meters_per_css_px` on both axes, so
/// the body text subtends exactly the angle the readability derivation asks
/// for, and nothing is stretched.
///
/// One consequence: with the viewport INDEPENDENT of the panel, narrowing the
/// panel does not reflow the page, so the bounds are applied as a UNIFORM shrink
/// that preserves the aspect. Per-axis clamping is what the estimate needs and
/// what a reported viewport must never get — clamping one axis of a known-good
/// aspect is precisely how the squeeze is reintroduced.
///
/// What a reported viewport does NOT fix is content below the fold: the
/// viewport is a fixed number of CSS pixels, so a page taller than it is simply
/// not rasterised past the fold, and no panel size reaches the rest. That is
/// what the page's click pager (`qar_ui_model/scroll_paging.hpp`) is for, and
/// what @ref UrlPanelSizeResult::visible_fraction reports.
///
/// ## Matching the aspect is necessary and not sufficient: the FIT
///
/// The viewport is **500 x 500 CSS px at devicePixelRatio 2.0**, fixed, while a
/// typical generated page wants **756 CSS px** of width. Matching the aspect of
/// a square viewport gives a square panel — and a 500 px square one, 0.33 m on
/// a side, holds a layout squeezed into two thirds of the width it was drawn
/// for. Text overlaps, buttons touch, and the panel is too small to read: both
/// faults, one cause.
///
/// The cure is not more metres and not more pixels; it is the pair of knobs in
/// `qar_ui_model/viewport_fit.hpp`, which cancel. The PAGE scales its theme
/// down by `k` so the layout has `1/k` more room in the pixels it is actually
/// laid out in, and this file scales the PANEL up by the same `k` so the text
/// subtends the same angle it does without the fit:
///
/// @code
///   panel_m = viewport_css x meters_per_css_px(theme) / k
/// @endcode
///
/// which needs no special case at all: `meters_per_css_px` is derived from
/// `base_font_px`, so passing @ref UrlPanelSizing::theme_scale is arithmetically
/// the same as passing `theme.scaled(k)`, and the invariant stated below for
/// `ArTheme::scaled` is exactly the one being used. For the 500 x 500 case `k`
/// comes out at 12/22 = 0.545 and the panel at 0.60 m square — 1.8x the 0.33 m
/// of the plain square, with the body text subtending the same angle.
///
/// ## Metres per CSS pixel, derived from the SAME reasoning as QWidget panels
///
/// ar_readability.hpp of qar_qt_streaming is the source of truth: legibility is
/// ANGULAR, so a panel is sized by how many arc-minutes its text subtends. This
/// library cannot include that header — `qar_web_panels` is Qt Core/Network
/// only and must never gain a Qt Widgets / streaming dependency for a constant
/// — so the derivation is restated here, and it restates to something much
/// simpler than a copy of the arithmetic, because everything angular cancels:
///
/// @code
///   cap height (metres) = body_font_px x cap_height_ratio x metres_per_px
///   arcmin subtended    = cap_height_m / viewing_distance_m / kRadiansPerArcmin
/// @endcode
///
/// Ask for the SAME arc-minutes at the SAME distance with the SAME typeface,
/// and `cap_height_ratio`, `viewing_distance_m` and the target arc-minutes all
/// drop out. What is left is a ratio of font sizes:
///
/// @code
///   metres_per_css_px = kQtMetersPerLogicalPx x kQtBodyFontPx / theme.base_font_px
///                     = 0.0012 x 12 / 22
///                     = 6.545e-4 m  (0.65 mm)
/// @endcode
///
/// Check it: 22 px body text has a ~15.4 px cap (0.70 em), so a cap is
/// 15.4 x 6.545e-4 = 10.1 mm, which at 1.5 m subtends 23.1 arc-minutes — inside
/// ISO 9241-303's 20–22 recommended band and identical to what a QWidget panel
/// gets from 12 px text at 0.0012 m/px. Both kinds of AR panel are therefore
/// legible to the same standard, which is the whole point of not inventing a
/// second constant. Change 0.0012 in panel_geometry.hpp and this follows.
///
/// A pleasant consequence of expressing it against the theme rather than as a
/// literal: `ArTheme::scaled(2.0)` doubles every pixel size, halves
/// metres-per-pixel, and leaves the panel exactly the same physical size with
/// twice the pixels behind it — sharper, not bigger, the same invariant
/// panel_geometry.hpp holds for HiDPI.
///
/// ## `content_scale`
///
/// The vendored SDK header (`third_party/qar/include/qar_streaming.h`) documents
/// it: *"Content-space-per-panel-space zoom of the rendered raster. 1.0 fits the
/// content across the full panel surface, 2.0 shows it at twice the size so only
/// half of it fits, and 0.5 shows it at half size."*
///
/// For a browser panel that settles it at **1.0**, and 1.0 is the only value
/// that can be right here: the page has no intrinsic pixel raster whose density
/// needs declaring (it reflows into whatever viewport the renderer gives it),
/// so "fit the content to the surface" is exactly the contract this file is
/// built on. 2.0 would crop the page and 0.5 would shrink the page into half the
/// panel and halve the angular text size.
///
/// The field's other documented job, mapping a finger contact back to content
/// pixels, does not apply: the header says every panel, URL panels included,
/// reports gestures, but this stack subscribes to none for a URL panel. The
/// page handles pointer input itself and reports it over the WebSocket of its
/// own server, so nothing on the URL route reads a contact back through it.
///
/// The stream route sends the same 1.0: panel_geometry.hpp of qar_qt_streaming
/// carries its supersample ratio (`dpr x render_scale`) separately, in
/// `effective_pixel_ratio`, for diagnostics only (qar_ogre_adapter
/// docs/QAROS_INTEGRATION_NOTES.md, "Panel pixel coordinates and content_scale").

#include <cstddef>
#include <functional>
#include <string>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/page_metrics.hpp"
#include "qar_ui_model/ui_model.hpp"
#include "qar_ui_model/ui_protocol.hpp"
#include "qar_ui_model/viewport_fit.hpp"
#include "qar_ar_core/qar_streaming_c.hpp"

namespace qar_web_panels
{

/// @brief `qar_qt_streaming::kDefaultMetersPerLogicalPx`. Restated, not
/// included — see the file comment. Keep in step with panel_geometry.hpp.
constexpr double kQtMetersPerLogicalPx = 0.0012;

/// @brief `qar_qt_streaming::kDefaultBodyFontPx`: Qt's 9 pt body font at 96 dpi.
constexpr double kQtBodyFontPx = 12.0;

/// @brief Metres one CSS pixel of an @ref qar_ui_model::ArTheme page covers, so
/// that its body text subtends the same angle as a QWidget panel's.
double meters_per_css_px(const qar_ui_model::ArTheme & theme);

/// @brief Bounds and knobs for @ref size_url_panel.
struct UrlPanelSizing
{
  /// Metres per CSS pixel. <= 0 means "derive from the theme", which is what
  /// every caller should do; a positive value overrides the derivation.
  double meters_per_css_px = 0.0;

  /// Uniform multiplier on the derived size — the operator's "a bit bigger
  /// please" knob (`QAR_PANEL_WEB_SCALE`). Applied before the bounds.
  double scale = 1.0;

  /// A size the operator pinned outright (`QAR_PANEL_WEB_SIZE=WxH`), in
  /// metres. Both > 0 skips the measurement entirely; the bounds still apply.
  double fixed_width_m = 0.0;
  double fixed_height_m = 0.0;

  /// Narrower than this and a generated form cannot hold a 64 px touch target
  /// at a readable text size; wider than ~1.4 m the panel subtends more than
  /// 50 deg at 1.5 m and the operator has to turn their head to read a line.
  double min_width_m = 0.25;
  double max_width_m = 1.40;

  /// A panel hangs BELOW its pose (`QarGuiPanelSize`: y in [-height, 0]), and
  /// the default pose puts the top edge at eye height, 1.5 m. 1.30 m of height
  /// therefore stops the bottom edge about 20 cm above the floor.
  double min_height_m = 0.25;
  double max_height_m = 1.30;

  /// See the file comment: 1.0 is "fit the page across the panel surface".
  float content_scale = 1.0f;

  /// The theme scale the PAGE is drawn at, relative to the @p theme passed to
  /// @ref size_url_panel — `qar_ui_model::ViewportFit::effective_scale`. The
  /// panel's metres are divided by it, because a page drawn at `k` needs a
  /// panel `1/k` bigger for its text to subtend the angle the readability
  /// derivation asks for. 1.0 is a page drawn as designed.
  ///
  /// @ref size_url_panel_for derives this from the reported viewport and the
  /// model; a caller that already holds a @ref qar_ui_model::ViewportFit sets
  /// it directly. Note the @p theme argument stays the BASE theme in both
  /// cases — passing an already-scaled theme AND this would apply `k` twice.
  double theme_scale = 1.0;

  /// The CSS viewport the page reported being laid out into (`viewport` frame,
  /// `window.innerWidth` / `innerHeight`). Both > 0 makes this the AUTHORITY
  /// for the panel's size and aspect and replaces the estimate: see the file
  /// comment. 0 means no page has measured itself yet — a panel opening for
  /// the first time, or a runtime that does not send the frame — and the
  /// measured estimate is used.
  double viewport_css_width = 0.0;
  double viewport_css_height = 0.0;

  /// `document.documentElement.scrollHeight` from the same report: how much
  /// page there is. Only feeds @ref UrlPanelSizeResult::visible_fraction — it
  /// must not change the panel's size, because growing the panel cannot bring
  /// content below a fixed viewport's fold into view.
  double content_css_height = 0.0;
};

/// @brief Copy a page's reported viewport into @p sizing.
void use_reported_viewport(
  UrlPanelSizing & sizing, const qar_ui_model::ViewportBody & viewport);

/// @brief What @ref size_url_panel decided, and why.
struct UrlPanelSizeResult
{
  QarGuiPanelSize size{0.0f, 0.0f, 1.0f};

  /// Metres per CSS pixel actually used, after @ref UrlPanelSizing::scale and
  /// the width bound. Below @ref meters_per_css_px means the panel was
  /// narrowed to fit the room and its text is correspondingly smaller.
  double effective_meters_per_css_px = 0.0;

  /// How much of the page the operator can see at once. 1.0 is the whole page;
  /// less means the rest is out of view and has to be paged to.
  ///
  /// Which fold it refers to depends on where the size came from. From the
  /// ESTIMATE it is the panel's own bottom edge (the height clamp). From a
  /// REPORTED viewport it is the browser's fold — `css_height /
  /// content_height` — which is the honest one, because a fixed viewport does
  /// not rasterise what is past it however large the panel is made.
  double visible_fraction = 1.0;

  /// A bound changed the size on either axis.
  bool clamped = false;

  /// The measurement this came from; empty widget_count for a pinned size.
  qar_ui_model::PageMetrics metrics;

  /// The size came from a viewport the page reported, not from the estimate.
  bool from_reported_viewport = false;

  /// The theme scale the page is drawn at (@ref UrlPanelSizing::theme_scale).
  /// Below 1 means the design was shrunk to fit the viewport and this panel
  /// was grown by the inverse to pay for it.
  double theme_scale = 1.0;

  /// Columns a row keeps on one line in the fitted layout. 1 is the
  /// single-column page a narrow viewport gets.
  std::size_t columns = 1;

  /// width / height of the panel produced. With a reported viewport this
  /// equals the viewport's aspect to within millimetre rounding, which is the
  /// whole point — a test asserts it.
  double aspect = 1.0;

  /// One line for the log: the page size, the panel size, and any clamp.
  std::string note;
};

/// @brief Physical size for a page whose natural size is @p metrics.
///
/// Bounds are per axis, and they mean different things: the width is the
/// page's LAYOUT width (narrow it and the fluid page reflows taller, which is
/// self-defeating), the height is how much of that layout reaches the surface.
/// See the implementation comment, and @ref UrlPanelSizeResult::visible_fraction.
UrlPanelSizeResult size_url_panel(
  const qar_ui_model::PageMetrics & metrics,
  const qar_ui_model::ArTheme & theme,
  const UrlPanelSizing & sizing = {});

/// @brief Lowest theme scale that still leaves room to compensate for it.
///
/// The angular invariant holds only while the panel can grow by `1/k`. Past
/// the bounds of the room it cannot, so `k` below this would shrink the text
/// and the touch targets for real rather than trading pixels for metres. With
/// no reported viewport there is nothing to fit and the answer is 1.0.
///
/// @ref UrlPanelSizing::scale is not part of it: the operator's magnification
/// is not the design's legibility floor. See the implementation comment.
double min_theme_scale(const qar_ui_model::ArTheme & theme, const UrlPanelSizing & sizing);

/// @brief Fit @p model to the reported viewport, then size a panel for the
/// result. With no viewport reported this is the plain measurement.
///
/// This is the whole chain in one call: choose the column count and theme
/// scale the page will apply to itself (`qar_ui_model::fit_page_to_viewport`,
/// which the page's script mirrors), measure the page at that scale, and turn
/// the reported viewport into metres divided by it.
UrlPanelSizeResult size_url_panel_for(
  const qar_ui_model::UiModel & model,
  const qar_ui_model::ArTheme & theme,
  const UrlPanelSizing & sizing = {});

/// @brief Looks up one environment variable; returns nullptr when unset.
using EnvLookup = std::function<const char *(const char *)>;

/// @brief The process environment. Injectable so the overrides are testable.
const EnvLookup & process_environment();

/// @brief Apply the `QAR_PANEL_WEB_*` sizing overrides to @p sizing.
///
/// Same family and spelling as `QAR_PANEL_WEB_HOST` / `QAR_PANEL_WEB_PORT`:
///
///  * `QAR_PANEL_WEB_SIZE=<w>x<h>` — metres, e.g. `0.9x1.2`. Pins the size and
///    skips the measurement. `x` or `X` separates; whitespace is tolerated.
///  * `QAR_PANEL_WEB_SCALE=<f>` — multiplier on the derived size, e.g. `1.25`.
///
/// A malformed value is IGNORED and reported through @p out_note rather than
/// failing the panel: a typo in an environment variable must not cost the
/// operator their AR tool.
/// @return true when at least one override was applied.
bool apply_sizing_overrides(
  UrlPanelSizing & sizing, const EnvLookup & lookup, std::string & out_note);

}  // namespace qar_web_panels

#endif  // QAR_WEB_PANELS__URL_PANEL_SIZING_HPP_
