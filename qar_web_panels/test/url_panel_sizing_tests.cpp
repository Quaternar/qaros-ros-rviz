// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

/// @file url_panel_sizing_tests.cpp
/// @brief CSS pixels -> metres for a browser-backed AR panel.
///
/// Pure arithmetic over a measured page: no socket, no session, no headset.
/// What these pin is the reasoning in url_panel_sizing.hpp — that a web panel's
/// text ends up the same angular size as a QWidget panel's, that the panel's
/// aspect is the page's aspect, that the bounds scale uniformly instead of
/// cropping, and that the environment overrides are honoured.

#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <map>
#include <string>

#include "qar_ui_model/page_metrics.hpp"
#include "qar_ui_model/ui_model.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"

using qar_ui_model::ArTheme;
using qar_ui_model::measure_page;
using qar_ui_model::UiModel;
using qar_ui_model::Widget;
using qar_web_panels::apply_sizing_overrides;
using qar_web_panels::EnvLookup;
using qar_web_panels::kQtBodyFontPx;
using qar_web_panels::kQtMetersPerLogicalPx;
using qar_web_panels::meters_per_css_px;
using qar_web_panels::min_theme_scale;
using qar_web_panels::size_url_panel;
using qar_web_panels::size_url_panel_for;
using qar_web_panels::UrlPanelSizeResult;
using qar_web_panels::UrlPanelSizing;
namespace widgets = qar_ui_model::widgets;

namespace
{

/// The shape every generated topic panel has: a section of N value displays.
UiModel form(std::size_t n)
{
  UiModel model;
  model.root = widgets::panel("root", "/topic");
  Widget section = widgets::section("s", "Message");
  for (std::size_t i = 0; i < n; ++i) {
    section.add(widgets::value("v" + std::to_string(i), "msg.f" + std::to_string(i), "Field"));
  }
  model.root.add(std::move(section));
  return model;
}

/// A fake environment, so the overrides are tested without touching the real one.
EnvLookup env(std::map<std::string, std::string> values)
{
  return [values = std::move(values)](const char * name) -> const char * {
           const auto it = values.find(name);
           return it == values.end() ? nullptr : it->second.c_str();
         };
}

double aspect_of(const UrlPanelSizeResult & r)
{
  return static_cast<double>(r.size.width_meters) / static_cast<double>(r.size.height_meters);
}

}  // namespace

// ---------------------------------------------------------------------------
// The shared readability derivation
// ---------------------------------------------------------------------------

TEST(UrlPanelSizing, TextEndsUpTheSameAngularSizeAsAQWidgetPanel)
{
  const ArTheme theme;
  const double mpx = meters_per_css_px(theme);

  // The published derivation: the ratio of the two body font sizes.
  EXPECT_NEAR(mpx, kQtMetersPerLogicalPx * kQtBodyFontPx / theme.base_font_px, 1e-12);
  EXPECT_NEAR(mpx, 6.5454545e-4, 1e-9);

  // And the reason it is that ratio: identical cap height in metres, hence an
  // identical angle from any distance. 0.70 em is the cap-height ratio
  // ar_readability.hpp uses for common UI faces; it cancels, so any value does.
  constexpr double kCapRatio = 0.70;
  const double web_cap_m = theme.base_font_px * kCapRatio * mpx;
  const double qt_cap_m = kQtBodyFontPx * kCapRatio * kQtMetersPerLogicalPx;
  EXPECT_NEAR(web_cap_m, qt_cap_m, 1e-12);
  EXPECT_NEAR(web_cap_m, 0.01008, 1e-5) << "about 10 mm of capital height";
}

TEST(UrlPanelSizing, AScaledThemeKeepsThePhysicalSizeAndGainsPixels)
{
  const UiModel model = form(6);
  const ArTheme normal;
  const ArTheme doubled = normal.scaled(2.0);

  EXPECT_NEAR(meters_per_css_px(doubled), meters_per_css_px(normal) / 2.0, 1e-9);

  // Same physical panel, twice the pixels behind it — the invariant
  // panel_geometry.hpp holds for HiDPI. Within a few per cent: the stylesheet
  // has pixel values a theme cannot scale (see page_metrics_tests).
  const UrlPanelSizeResult a = size_url_panel_for(model, normal);
  const UrlPanelSizeResult b = size_url_panel_for(model, doubled);
  EXPECT_NEAR(b.size.width_meters, a.size.width_meters, 0.005f);
  EXPECT_NEAR(b.size.height_meters, a.size.height_meters, 0.05f);
  EXPECT_GT(b.metrics.width_px, a.metrics.width_px) << "same metres, twice the pixels";
}

// ---------------------------------------------------------------------------
// Size and aspect follow the content
// ---------------------------------------------------------------------------

TEST(UrlPanelSizing, MoreWidgetsMeansATallerPanel)
{
  const UrlPanelSizeResult few = size_url_panel_for(form(3), ArTheme{});
  const UrlPanelSizeResult many = size_url_panel_for(form(12), ArTheme{});

  EXPECT_GT(many.size.height_meters, few.size.height_meters);
  EXPECT_FLOAT_EQ(many.size.width_meters, few.size.width_meters);
  EXPECT_FALSE(few.clamped);
  EXPECT_FALSE(many.clamped);
}

TEST(UrlPanelSizing, TheAspectIsThePagesAspectNotAFixedRatio)
{
  for (std::size_t n : {4u, 8u, 14u}) {
    const UrlPanelSizeResult r = size_url_panel_for(form(n), ArTheme{});
    ASSERT_FALSE(r.clamped) << "with " << n << " widgets";
    EXPECT_NEAR(aspect_of(r), r.metrics.aspect, 0.01) << "with " << n << " widgets";
  }

  // Different content, different shape — which a fixed ratio could not do.
  EXPECT_GT(
    aspect_of(size_url_panel_for(form(4), ArTheme{})),
    aspect_of(size_url_panel_for(form(14), ArTheme{})));
}

TEST(UrlPanelSizing, AGeneratedFormIsPortraitAndBiggerThanTheOldGuess)
{
  // A guessed fixed size (0.5 x 0.35 m, or 0.6 x 0.45 m) is landscape, which
  // is what crops a portrait page.
  const UrlPanelSizeResult r = size_url_panel_for(form(8), ArTheme{});
  EXPECT_GT(r.size.height_meters, r.size.width_meters) << "a generated form is portrait";
  EXPECT_GT(r.size.height_meters, 0.45f);
  EXPECT_GT(
    r.size.width_meters * r.size.height_meters, 0.6f * 0.45f) << "and covers more panel";
}

TEST(UrlPanelSizing, ContentScaleIsOneSoThePageFitsTheSurface)
{
  // Per the SDK header: content_scale is a zoom, and 1.0 "fits the content
  // across the full panel surface". Anything else would crop or shrink the
  // page, and a browser panel has no raster whose density needs declaring.
  const UrlPanelSizeResult r = size_url_panel_for(form(5), ArTheme{});
  EXPECT_FLOAT_EQ(r.size.content_scale, 1.0f);
  EXPECT_FLOAT_EQ(UrlPanelSizing{}.content_scale, 1.0f);
}

// ---------------------------------------------------------------------------
// Bounds
// ---------------------------------------------------------------------------

TEST(UrlPanelSizing, AnOversizedPageKeepsItsWidthAndReportsWhatFits)
{
  const ArTheme theme;
  const UrlPanelSizing sizing;
  const qar_ui_model::PageMetrics metrics = measure_page(form(60), theme);
  const double natural_height = metrics.height_px * meters_per_css_px(theme);
  ASSERT_GT(natural_height, sizing.max_height_m) << "the fixture must exceed the bound";

  const UrlPanelSizeResult r = size_url_panel(metrics, theme, sizing);
  EXPECT_TRUE(r.clamped);
  EXPECT_FLOAT_EQ(r.size.height_meters, static_cast<float>(sizing.max_height_m));

  // The WIDTH is untouched. Narrowing the panel would narrow the browser's
  // viewport, reflow the fluid page TALLER and shrink the text — the opposite
  // of a fix. So the page keeps its layout width and full-size text...
  const UrlPanelSizeResult unclamped = size_url_panel(measure_page(form(4), theme), theme, sizing);
  EXPECT_FLOAT_EQ(r.size.width_meters, unclamped.size.width_meters);
  EXPECT_NEAR(r.effective_meters_per_css_px, meters_per_css_px(theme), 1e-5);

  // ...and what does not fit is REPORTED rather than silently lost.
  EXPECT_NEAR(r.visible_fraction, sizing.max_height_m / natural_height, 0.01);
  EXPECT_LT(r.visible_fraction, 1.0);
  EXPECT_NE(r.note.find("clamped"), std::string::npos);
  EXPECT_NE(r.note.find("scroll"), std::string::npos);
}

TEST(UrlPanelSizing, AnUndersizedPageIsLiftedToTheFloor)
{
  const ArTheme theme;
  const UrlPanelSizing sizing;
  UiModel tiny;
  tiny.root = widgets::panel("root", "P");
  tiny.root.add(widgets::label("l", "Waiting"));
  const qar_ui_model::PageMetrics metrics = measure_page(tiny, theme);
  ASSERT_LT(metrics.height_px * meters_per_css_px(theme), sizing.min_height_m);

  const UrlPanelSizeResult r = size_url_panel(metrics, theme, sizing);
  EXPECT_TRUE(r.clamped);
  EXPECT_FLOAT_EQ(r.size.height_meters, static_cast<float>(sizing.min_height_m));
  // Nothing is hidden — `.qar-panel { min-height: 100vh }` simply stretches the
  // page into the extra room — so the whole page still counts as visible.
  EXPECT_DOUBLE_EQ(r.visible_fraction, 1.0);
  EXPECT_NEAR(r.effective_meters_per_css_px, meters_per_css_px(theme), 1e-5) << "same text size";
}

TEST(UrlPanelSizing, AnAbsurdlyWidePageIsNarrowedToTheRoomAndTheTextShrinks)
{
  const ArTheme theme;
  const UrlPanelSizing sizing;
  qar_ui_model::PageMetrics metrics;
  metrics.width_px = 8000.0;
  metrics.height_px = 600.0;
  metrics.aspect = metrics.width_px / metrics.height_px;

  const UrlPanelSizeResult r = size_url_panel(metrics, theme, sizing);
  EXPECT_TRUE(r.clamped);
  EXPECT_FLOAT_EQ(r.size.width_meters, static_cast<float>(sizing.max_width_m));
  // Narrowing a panel IS a change of pixel density, and the result says so.
  EXPECT_LT(r.effective_meters_per_css_px, meters_per_css_px(theme));
  EXPECT_NEAR(r.effective_meters_per_css_px, sizing.max_width_m / metrics.width_px, 1e-9);
}

TEST(UrlPanelSizing, BothAxesCanBeClampedAtOnce)
{
  const ArTheme theme;
  const UrlPanelSizing sizing;
  qar_ui_model::PageMetrics metrics;
  metrics.width_px = 60.0;      // narrower than min_width
  metrics.height_px = 6000.0;   // taller than max_height
  metrics.aspect = metrics.width_px / metrics.height_px;

  const UrlPanelSizeResult r = size_url_panel(metrics, theme, sizing);
  EXPECT_TRUE(r.clamped);
  EXPECT_FLOAT_EQ(r.size.width_meters, static_cast<float>(sizing.min_width_m));
  EXPECT_FLOAT_EQ(r.size.height_meters, static_cast<float>(sizing.max_height_m));
  EXPECT_LT(r.visible_fraction, 1.0);
  EXPECT_GT(aspect_of(r), 0.0);
}

// ---------------------------------------------------------------------------
// Overrides
// ---------------------------------------------------------------------------

TEST(UrlPanelSizing, ScaleOverrideMultipliesTheDerivedSize)
{
  UrlPanelSizing sizing;
  std::string note;
  EXPECT_TRUE(apply_sizing_overrides(sizing, env({{"QAR_PANEL_WEB_SCALE", "1.5"}}), note));
  EXPECT_DOUBLE_EQ(sizing.scale, 1.5);
  EXPECT_NE(note.find("QAR_PANEL_WEB_SCALE"), std::string::npos);

  const ArTheme theme;
  const UiModel model = form(4);
  const UrlPanelSizeResult plain = size_url_panel_for(model, theme);
  const UrlPanelSizeResult scaled = size_url_panel_for(model, theme, sizing);
  EXPECT_NEAR(scaled.size.width_meters / plain.size.width_meters, 1.5f, 0.02f);
  EXPECT_NEAR(scaled.size.height_meters / plain.size.height_meters, 1.5f, 0.02f);
  EXPECT_NEAR(aspect_of(scaled), aspect_of(plain), 0.01);
}

TEST(UrlPanelSizing, SizeOverridePinsThePanelAndSkipsTheMeasurement)
{
  UrlPanelSizing sizing;
  std::string note;
  EXPECT_TRUE(apply_sizing_overrides(sizing, env({{"QAR_PANEL_WEB_SIZE", "0.9x1.2"}}), note));
  EXPECT_DOUBLE_EQ(sizing.fixed_width_m, 0.9);
  EXPECT_DOUBLE_EQ(sizing.fixed_height_m, 1.2);

  // Two very different pages come out the same size, which is the point of a pin.
  const ArTheme theme;
  const UrlPanelSizeResult a = size_url_panel_for(form(2), theme, sizing);
  const UrlPanelSizeResult b = size_url_panel_for(form(30), theme, sizing);
  EXPECT_FLOAT_EQ(a.size.width_meters, 0.9f);
  EXPECT_FLOAT_EQ(a.size.height_meters, 1.2f);
  EXPECT_FLOAT_EQ(b.size.width_meters, 0.9f);
  EXPECT_FLOAT_EQ(b.size.height_meters, 1.2f);
  EXPECT_NE(a.note.find("pinned"), std::string::npos);

  // A pin outranks the default bounds: 1.2 m is over max_height_m (1.30 is not,
  // but 1.9 is) and the operator still gets what they asked for.
  UrlPanelSizing tall;
  ASSERT_TRUE(apply_sizing_overrides(tall, env({{"QAR_PANEL_WEB_SIZE", "1.6X2.4"}}), note));
  const UrlPanelSizeResult r = size_url_panel_for(form(4), theme, tall);
  EXPECT_FLOAT_EQ(r.size.width_meters, 1.6f);
  EXPECT_FLOAT_EQ(r.size.height_meters, 2.4f);
}

TEST(UrlPanelSizing, AMalformedOverrideIsIgnoredAndExplained)
{
  const ArTheme theme;
  const UrlPanelSizeResult reference = size_url_panel_for(form(4), theme);

  for (const auto & bad : {"big", "0.9", "0.9x", "x1.2", "0.9xbig", "-0.5x1.0", "0.9 x 1.2m"}) {
    UrlPanelSizing sizing;
    std::string note;
    EXPECT_FALSE(apply_sizing_overrides(sizing, env({{"QAR_PANEL_WEB_SIZE", bad}}), note))
      << "accepted '" << bad << "'";
    EXPECT_NE(note.find("ignored"), std::string::npos) << "silent about '" << bad << "'";
    // And the panel still gets a sensible size rather than a zero one.
    const UrlPanelSizeResult r = size_url_panel_for(form(4), theme, sizing);
    EXPECT_FLOAT_EQ(r.size.width_meters, reference.size.width_meters);
    EXPECT_FLOAT_EQ(r.size.height_meters, reference.size.height_meters);
  }

  for (const auto & bad : {"", "0", "-2", "nan", "1.5x"}) {
    UrlPanelSizing sizing;
    std::string note;
    EXPECT_FALSE(apply_sizing_overrides(sizing, env({{"QAR_PANEL_WEB_SCALE", bad}}), note));
    EXPECT_DOUBLE_EQ(sizing.scale, 1.0);
  }

  // Whitespace around a good value is tolerated.
  UrlPanelSizing padded;
  std::string note;
  EXPECT_TRUE(apply_sizing_overrides(padded, env({{"QAR_PANEL_WEB_SCALE", "  1.25 "}}), note));
  EXPECT_DOUBLE_EQ(padded.scale, 1.25);
}

TEST(UrlPanelSizing, AnEmptyEnvironmentChangesNothing)
{
  UrlPanelSizing sizing;
  std::string note;
  EXPECT_FALSE(apply_sizing_overrides(sizing, env({}), note));
  EXPECT_TRUE(note.empty());
  EXPECT_DOUBLE_EQ(sizing.scale, 1.0);
  EXPECT_DOUBLE_EQ(sizing.fixed_width_m, 0.0);
  EXPECT_FALSE(apply_sizing_overrides(sizing, EnvLookup{}, note)) << "and a null lookup is safe";
}

TEST(UrlPanelSizing, TheNoteIsOneReadableLine)
{
  const UrlPanelSizeResult r = size_url_panel_for(form(6), ArTheme{});
  EXPECT_NE(r.note.find("css px"), std::string::npos);
  EXPECT_NE(r.note.find("6 widgets"), std::string::npos);
  EXPECT_NE(r.note.find(" m"), std::string::npos);
  EXPECT_EQ(r.note.find('\n'), std::string::npos);

  // The note is what the operator reads in the RViz log, so print what it says
  // for a few real shapes: a maintainer changing a theme number or a bound sees
  // the consequence here without starting a session.
  for (std::size_t n : {1u, 4u, 8u, 16u, 40u}) {
    std::cout << "  [ sizing  ] " << size_url_panel_for(form(n), ArTheme{}).note << "\n";
  }
}

// ---------------------------------------------------------------------------
// A viewport the page reported. The browser viewport does NOT take the
// panel's shape, so a panel whose aspect disagrees with it is stretched
// non-uniformly — a horizontal squeeze. Once the page says what it was given,
// the panel simply becomes that viewport in metres.
// ---------------------------------------------------------------------------

namespace
{

UrlPanelSizing reported(double css_w, double css_h, double content_h = 0.0)
{
  UrlPanelSizing sizing;
  sizing.viewport_css_width = css_w;
  sizing.viewport_css_height = css_h;
  sizing.content_css_height = content_h;
  return sizing;
}

}  // namespace

TEST(ReportedViewport, ThePanelTakesTheViewportsAspectNotThePages)
{
  const ArTheme theme;
  // A tall generated form: the estimate says portrait.
  const qar_ui_model::PageMetrics metrics = measure_page(form(16), theme);
  ASSERT_LT(metrics.aspect, 1.0) << "the page itself is portrait";

  // The runtime hands it a landscape viewport instead.
  const UrlPanelSizeResult r = size_url_panel(metrics, theme, reported(1024.0, 640.0));
  EXPECT_TRUE(r.from_reported_viewport);
  EXPECT_NEAR(r.aspect, 1024.0 / 640.0, 1e-3);
  EXPECT_GT(r.aspect, 1.0) << "landscape viewport -> landscape panel";
  EXPECT_NE(r.note.find("aspect matched to the viewport"), std::string::npos);
  EXPECT_NE(r.note.find("reported by the page"), std::string::npos);
}

TEST(ReportedViewport, SizeIsTheViewportAtTheReadableRate)
{
  const ArTheme theme;
  const UrlPanelSizeResult r =
    size_url_panel(measure_page(form(4), theme), theme, reported(800.0, 600.0));
  // No estimate left in it: the panel IS the viewport, converted once.
  EXPECT_NEAR(r.size.width_meters, 800.0 * meters_per_css_px(theme), 1e-3);
  EXPECT_NEAR(r.size.height_meters, 600.0 * meters_per_css_px(theme), 1e-3);
  // ...so 22 px of body text subtends exactly what the derivation asks for.
  EXPECT_NEAR(r.effective_meters_per_css_px, meters_per_css_px(theme), 1e-6);
  EXPECT_FALSE(r.clamped);
}

TEST(ReportedViewport, TheBoundsScaleBothAxesTogetherSoNothingIsSqueezed)
{
  const ArTheme theme;
  // A viewport far too big for the room on both axes.
  const UrlPanelSizing sizing = reported(4000.0, 1200.0);
  const UrlPanelSizeResult r = size_url_panel(measure_page(form(4), theme), theme, sizing);
  EXPECT_TRUE(r.clamped);
  EXPECT_LE(r.size.width_meters, static_cast<float>(sizing.max_width_m) + 1e-3f);
  EXPECT_LE(r.size.height_meters, static_cast<float>(sizing.max_height_m) + 1e-3f);
  // The one thing a clamp must not do here: change the shape. Per-axis
  // clamping is exactly how the squeeze gets reintroduced.
  EXPECT_NEAR(r.aspect, 4000.0 / 1200.0, 1e-2);
  EXPECT_NE(r.note.find("scaled to the bounds"), std::string::npos);

  // A viewport far too small comes up to the floor the same way.
  const UrlPanelSizeResult tiny =
    size_url_panel(measure_page(form(4), theme), theme, reported(120.0, 90.0));
  EXPECT_TRUE(tiny.clamped);
  EXPECT_NEAR(tiny.aspect, 120.0 / 90.0, 1e-2);
  EXPECT_GE(tiny.size.width_meters, static_cast<float>(UrlPanelSizing{}.min_width_m) - 1e-3f);
}

TEST(ReportedViewport, TheOperatorsScaleKnobStillAppliesAndStillKeepsTheAspect)
{
  const ArTheme theme;
  UrlPanelSizing sizing = reported(600.0, 400.0);
  const UrlPanelSizeResult one = size_url_panel(measure_page(form(4), theme), theme, sizing);
  sizing.scale = 1.5;
  const UrlPanelSizeResult bigger = size_url_panel(measure_page(form(4), theme), theme, sizing);
  EXPECT_NEAR(bigger.size.width_meters, one.size.width_meters * 1.5f, 1e-3);
  EXPECT_NEAR(bigger.size.height_meters, one.size.height_meters * 1.5f, 1e-3);
  EXPECT_NEAR(bigger.aspect, one.aspect, 1e-6);
}

TEST(ReportedViewport, VisibleFractionBecomesTheBrowsersFoldNotThePanels)
{
  const ArTheme theme;
  // 1580 px of page in a 640 px viewport: a bigger panel cannot help, because
  // the runtime never rasterises what is past a fixed viewport.
  const UrlPanelSizeResult r =
    size_url_panel(measure_page(form(16), theme), theme, reported(1024.0, 640.0, 1580.0));
  EXPECT_NEAR(r.visible_fraction, 640.0 / 1580.0, 1e-9);
  EXPECT_NE(r.note.find("page to the rest"), std::string::npos);

  // A page that fits its viewport reports the whole of itself.
  const UrlPanelSizeResult fits =
    size_url_panel(measure_page(form(2), theme), theme, reported(1024.0, 640.0, 500.0));
  EXPECT_DOUBLE_EQ(fits.visible_fraction, 1.0);
}

TEST(ReportedViewport, NoReportMeansTheEstimateAsBefore)
{
  const ArTheme theme;
  const qar_ui_model::PageMetrics metrics = measure_page(form(8), theme);
  const UrlPanelSizeResult estimated = size_url_panel(metrics, theme, UrlPanelSizing{});
  EXPECT_FALSE(estimated.from_reported_viewport);
  EXPECT_NE(estimated.note.find("css px ("), std::string::npos) << "the page measurement";

  // Half a report is no report: a panel must never be sized from one axis.
  EXPECT_FALSE(size_url_panel(metrics, theme, reported(1024.0, 0.0)).from_reported_viewport);
  EXPECT_FALSE(size_url_panel(metrics, theme, reported(0.0, 640.0)).from_reported_viewport);
}

TEST(ReportedViewport, APinnedSizeStillOverrulesEverything)
{
  const ArTheme theme;
  UrlPanelSizing sizing = reported(1024.0, 640.0);
  sizing.fixed_width_m = 0.9;
  sizing.fixed_height_m = 1.2;
  sizing.max_height_m = 1.3;
  const UrlPanelSizeResult r = size_url_panel(measure_page(form(4), theme), theme, sizing);
  EXPECT_FALSE(r.from_reported_viewport);
  EXPECT_FLOAT_EQ(r.size.width_meters, 0.9f);
  EXPECT_FLOAT_EQ(r.size.height_meters, 1.2f);
  EXPECT_NE(r.note.find("[pinned]"), std::string::npos);
}

TEST(ReportedViewport, UseReportedViewportCopiesWhatThePageSent)
{
  qar_ui_model::ViewportBody body;
  body.css_width = 1024.0;
  body.css_height = 640.0;
  body.content_height = 1580.0;
  body.device_pixel_ratio = 2.0;
  UrlPanelSizing sizing;
  qar_web_panels::use_reported_viewport(sizing, body);
  EXPECT_DOUBLE_EQ(sizing.viewport_css_width, 1024.0);
  EXPECT_DOUBLE_EQ(sizing.viewport_css_height, 640.0);
  EXPECT_DOUBLE_EQ(sizing.content_css_height, 1580.0);
  // devicePixelRatio is a diagnostic: it says how much raster is behind the
  // page, not how big the page is, and it must not reach the metres.
  const ArTheme theme;
  const UrlPanelSizeResult r = size_url_panel(measure_page(form(4), theme), theme, sizing);
  EXPECT_NEAR(r.size.width_meters, 1024.0 * meters_per_css_px(theme), 1e-3);
}

// ---------------------------------------------------------------------------
// Fitting the page to the viewport, and paying for it in metres
// ---------------------------------------------------------------------------
//
// Matching the aspect is necessary and not sufficient. The runtime's viewport
// is 500 x 500 CSS px, fixed; a generated page wants 756. Matching that gives a
// 0.33 m square panel holding a layout squeezed into two thirds of the width it
// was drawn for: text mixed one on top of another, buttons squeezed almost next
// to each other, a panel too small to read. The page shrinks its own theme by k
// so the layout fits; this file grows the panel by 1/k so nothing an operator
// sees gets any smaller. qar_ui_model/viewport_fit.hpp.

namespace
{

/// The runtime's measured viewport: 500 x 500 CSS px, devicePixelRatio 2.0.
constexpr double kLiveViewportPx = 500.0;

}  // namespace

TEST(ViewportFitSizing, TheLiveCaseGetsOneColumnAndAPanelAlmostTwiceTheSize)
{
  const ArTheme theme;
  const UrlPanelSizeResult r =
    size_url_panel_for(form(16), theme, reported(kLiveViewportPx, kLiveViewportPx));

  EXPECT_TRUE(r.from_reported_viewport);
  EXPECT_EQ(r.columns, 1u) << "500 px cannot hold two 260 px columns and their padding";
  EXPECT_DOUBLE_EQ(r.theme_scale, 12.0 / 22.0);

  // 500 x 6.545e-4 / (12/22) = 0.60 m, against the 0.33 m the same viewport
  // produces without the fit: bigger, and still square.
  EXPECT_NEAR(r.size.width_meters, 0.600f, 1e-3);
  EXPECT_NEAR(r.size.height_meters, 0.600f, 1e-3);
  EXPECT_NEAR(r.aspect, 1.0, 1e-6);
  EXPECT_NEAR(
    static_cast<double>(r.size.width_meters),
    kLiveViewportPx * meters_per_css_px(theme) / r.theme_scale, 1e-3);
  EXPECT_GT(r.size.width_meters, 1.7f * static_cast<float>(
      kLiveViewportPx * meters_per_css_px(theme)));
  EXPECT_NE(r.note.find("theme x0.55"), std::string::npos);
  EXPECT_NE(r.note.find("1 column"), std::string::npos);
}

TEST(ViewportFitSizing, TheTextAndTheTouchTargetsEndUpExactlyWhereTheyWere)
{
  // The one invariant the whole design rests on. The panel's metres per CSS
  // pixel go UP by exactly the factor the theme's pixels went down by, so the
  // page is drawn smaller and seen identically.
  const ArTheme base;
  const UrlPanelSizeResult r =
    size_url_panel_for(form(16), base, reported(kLiveViewportPx, kLiveViewportPx));
  const ArTheme drawn = base.scaled(r.theme_scale);

  const double before = base.base_font_px * meters_per_css_px(base);
  const double after = drawn.base_font_px * r.effective_meters_per_css_px;
  EXPECT_NEAR(after, before, 1e-4) << "22 px at 0.65 mm is 12 px at 1.20 mm";

  // Touch targets are sized from ray jitter, not from reading, so they may
  // only shrink in CSS px because the panel grows by the same factor and the
  // physical target — about 42 mm — is preserved.
  EXPECT_NEAR(
    drawn.touch_target_px * r.effective_meters_per_css_px,
    base.touch_target_px * meters_per_css_px(base), 5e-4);
}

TEST(ViewportFitSizing, AViewportWithRoomIsSizedExactlyAsBefore)
{
  const ArTheme theme;
  const UrlPanelSizeResult r = size_url_panel_for(form(4), theme, reported(1024.0, 640.0));
  EXPECT_DOUBLE_EQ(r.theme_scale, 1.0);
  EXPECT_NEAR(r.size.width_meters, 1024.0 * meters_per_css_px(theme), 1e-3);
  EXPECT_NEAR(r.size.height_meters, 640.0 * meters_per_css_px(theme), 1e-3);
  EXPECT_EQ(r.note.find("theme x"), std::string::npos) << "nothing to say when nothing changed";

  // And with no report at all it is the plain estimate, untouched.
  const UrlPanelSizeResult estimated = size_url_panel_for(form(8), theme, UrlPanelSizing{});
  EXPECT_FALSE(estimated.from_reported_viewport);
  EXPECT_DOUBLE_EQ(estimated.theme_scale, 1.0);
  EXPECT_FLOAT_EQ(
    estimated.size.width_meters,
    size_url_panel(measure_page(form(8), theme), theme, UrlPanelSizing{}).size.width_meters);
}

TEST(ViewportFitSizing, TheLegibilityFloorIsWhereThePanelStopsBeingAbleToPay)
{
  const ArTheme theme;
  UrlPanelSizing sizing = reported(kLiveViewportPx, kLiveViewportPx);
  // Each axis asks "how small may the theme go before the panel that pays for
  // it would not fit the room": viewport x rate / bound.
  EXPECT_NEAR(
    min_theme_scale(theme, sizing),
    kLiveViewportPx * meters_per_css_px(theme) / sizing.max_height_m, 1e-9);
  EXPECT_LT(min_theme_scale(theme, sizing), 12.0 / 22.0) << "the live case is clear of it";
  // Nothing measured, nothing to fit.
  EXPECT_DOUBLE_EQ(min_theme_scale(theme, UrlPanelSizing{}), 1.0);

  // A room that cannot hold the panel the fit would need: the theme stops
  // shrinking, the page stays a little squeezed, and the text keeps its size.
  sizing.max_width_m = 0.4;
  sizing.max_height_m = 0.4;
  const UrlPanelSizeResult r = size_url_panel_for(form(16), theme, sizing);
  EXPECT_NEAR(r.theme_scale, 18.0 / 22.0, 1e-9);
  EXPECT_LE(r.size.width_meters, 0.4f + 1e-3f);
  EXPECT_NEAR(
    theme.base_font_px * meters_per_css_px(theme),
    theme.scaled(r.theme_scale).base_font_px * r.effective_meters_per_css_px, 1e-4);
  EXPECT_NE(r.note.find("legibility floor"), std::string::npos);
}

TEST(ViewportFitSizing, TheOperatorsScaleKnobIsNotPartOfTheFloor)
{
  // x3.00 is a magnification, not a legibility requirement. Folding it into the
  // floor would stop the theme shrinking — leaving the layout squeezed to
  // protect text already three times bigger than it needs to be.
  const ArTheme theme;
  UrlPanelSizing sizing = reported(kLiveViewportPx, kLiveViewportPx);
  const double plain = min_theme_scale(theme, sizing);
  sizing.scale = 3.0;
  EXPECT_DOUBLE_EQ(min_theme_scale(theme, sizing), plain);

  const UrlPanelSizeResult r = size_url_panel_for(form(16), theme, sizing);
  EXPECT_DOUBLE_EQ(r.theme_scale, 12.0 / 22.0) << "the page still fits its viewport";
  EXPECT_TRUE(r.clamped) << "1.80 m of panel does not fit the room; the bounds say so";
  EXPECT_NEAR(r.aspect, 1.0, 1e-6) << "and they say it on both axes at once";
}

TEST(ViewportFitSizing, ThePanelStillFollowsTheViewportsAspect)
{
  // The fit changes the scale, never the shape: a stretched panel is the
  // squeeze this sizing exists to prevent, and nothing here may allow it.
  const ArTheme theme;
  const UrlPanelSizeResult r = size_url_panel_for(form(16), theme, reported(420.0, 900.0));
  EXPECT_NEAR(r.aspect, 420.0 / 900.0, 1e-2);
  EXPECT_LT(r.theme_scale, 1.0);
  EXPECT_NEAR(
    static_cast<double>(r.size.width_meters),
    420.0 * meters_per_css_px(theme) / r.theme_scale, 1e-3);
}
