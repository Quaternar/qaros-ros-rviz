// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__AR_THEME_HPP_
#define QAR_UI_MODEL__AR_THEME_HPP_

/// @file ar_theme.hpp
/// @brief Sizes and colours for a page that is read in mid-air and pointed at
/// with a hand ray. Every number below has a reason; change the reason before
/// changing the number.
///
/// ## Angular budget
///
/// QarOS places a panel in the room at a physical size. A page of the default
/// theme covers 0.65 mm per CSS pixel (`meters_per_css_px` of `qar_web_panels`:
/// 1.2 mm per Qt logical pixel x 12 / 22), 1.5 arc-minutes a pixel at 1.5 m, so:
///
///   * **Text.** Comfortable reading needs a cap height of roughly 20–30
///     arc-minutes; AR optics blur small glyphs further. 22 px body text is
///     ~33 arc-minutes tall, 18 px (the smallest size used anywhere) ~27.
///     Nothing on the page is smaller than 18 px. Values that the operator
///     glances at from further away are 30 px.
///   * **Touch targets.** A hand-ray or gaze cursor jitters by about 1–2
///     degrees; at 1.5 m that is a 2.5–5 cm circle on the panel, i.e. 38–76
///     px. So every interactive element is at least 64 x 64 px, and adjacent
///     targets keep a 12 px gap so a jittering ray does not straddle two of
///     them. (Desktop guidance is 44 px for a finger; a ray is worse than a
///     finger.)
///
/// ## Colour
///
/// Optical see-through displays are ADDITIVE: black is transparent and white
/// is the brightest thing in the room. A light page is a glowing slab that
/// occludes the world and fringes at the edges; a dark page reads as a card
/// with only the text emitting light. Hence dark-first, with near-white text
/// (contrast ratio > 15:1 against the background) and accent/state colours
/// each > 7:1 so they survive a bright room. State is never colour alone —
/// every status light also carries text — for colour-blind operators.
///
/// ## Interaction
///
/// There is no `:hover` rule anywhere. A mid-air pointer has no reliable
/// hover state (it flickers as the ray passes), so everything is visible at
/// rest and only `:active` / `aria-checked` / `data-*` states change the
/// look. Native `<select>` popups and tiny number spinners are avoided for the
/// same reason: choices are large segmented buttons, numbers get +/- steppers.

#include <string>
#include <utility>
#include <vector>

namespace qar_ui_model
{

struct ArTheme
{
  // --- Typography (px) -------------------------------------------------------
  int base_font_px = 22;
  int small_font_px = 18;   ///< The minimum anywhere on the page.
  int value_font_px = 30;   ///< Live readouts.
  int heading_font_px = 26; ///< Section titles.
  int title_font_px = 32;   ///< Panel title.

  // --- Targets and spacing (px) ---------------------------------------------
  int touch_target_px = 64;  ///< Minimum size of anything pressable.
  int touch_gap_px = 12;     ///< Minimum gap between two pressable things.
  int gap_px = 16;           ///< Between widgets.
  int padding_px = 24;       ///< Inside sections and the panel.
  int radius_px = 14;
  int focus_ring_px = 4;
  int table_row_px = 56;     ///< Table rows are pressable-sized even when not pressable.
  int table_page_rows = 8;   ///< Rows per page before the pager kicks in.
  int chart_height_px = 220;

  /// `.qar-row > * { flex-basis: <this> }` — the width a row child asks for
  /// before the row wraps. It is a THEME size, not a layout constant: a column
  /// has to hold a caption over a touch target with its padding, so 260 px is
  /// `touch_target_px` plus room for a label at `base_font_px`, and it scales
  /// with the rest of the theme (see @ref scaled) precisely because those do.
  /// @ref viewport_fit.hpp turns it into the column decision.
  int row_basis_px = 260;

  // --- Secondary lengths (px) -----------------------------------------------
  /// @anchor theme_lengths
  /// The stylesheet may not contain a bare pixel literal. Every length it
  /// emits is one of the fields above, one of the fields below, or a `calc()`
  /// over them — because the page rescales itself by rewriting exactly these
  /// custom properties on `:root` (@ref viewport_fit.hpp,
  /// @ref theme_pixel_variables), and a literal the runtime cannot reach stays
  /// at its 1.0 size while the text around it shrinks. Mixed scales are what
  /// collapse vertical rhythm: a box laid out for one font size holding text
  /// at another. `ar_theme.cpp` lists the handful of deliberate exemptions
  /// (pill radii, slider shadow-DOM pseudo-elements, paint-only lengths) with
  /// the reason each one is exempt, and a test pins that list so a new literal
  /// cannot be added silently.
  int gap_tight_px = 8;    ///< Caption to the control it names; inside a button.
  int gap_micro_px = 6;    ///< Inside a vector component; a pill's own padding.
  int pad_tight_px = 16;   ///< Inside fields, table cells, pills and chips.
  int pad_micro_px = 10;   ///< Vertical padding of a component box; value to unit.
  int border_px = 2;       ///< Control outlines, table rules, the rail divider.
  int chip_px = 44;        ///< A NON-pressable pill: the connection chip, the
                           ///< status line, a toggle's track. Below
                           ///< @ref touch_target_px on purpose — nothing this
                           ///< size is a target in its own right.
  int dot_px = 28;         ///< Status dot; gauge track.
  int dot_small_px = 16;   ///< Connection dot; a slider's track.
  int knob_px = 48;        ///< A slider's thumb. It is a TARGET, but a round one
                           ///< inside a `touch_target_px`-high control, so it is
                           ///< sized as decoration and reached through the whole
                           ///< control rather than through its own box.
  int block_px = 120;      ///< The nominal side of a small content block: a
                           ///< vector component's flex basis, a number field,
                           ///< an image placeholder. The JSON inspector is 4x.

  // --- Colours (#rrggbb) ----------------------------------------------------
  std::string background = "#0F141A";
  std::string surface = "#1A222C";
  std::string surface_raised = "#26313D";
  std::string border = "#3A4756";
  std::string text = "#F2F5F7";
  std::string text_muted = "#A9B4BF";
  std::string accent = "#4CC9F0";
  std::string accent_text = "#06202B";
  std::string ok = "#3DDC84";
  std::string warning = "#FFC857";
  std::string error = "#FF5C5C";
  std::string stale = "#8A97A5";
  std::string focus = "#FFD166";

  /// CSS font-family list. System fonts only: the page may not fetch anything.
  std::string font_family = "system-ui, -apple-system, 'Segoe UI', Roboto, 'Noto Sans', sans-serif";

  /// The default. Identical to ArTheme{}.
  static ArTheme dark();
  /// For video-see-through devices, where the world is already a video and a
  /// light card does not glow. Same sizes.
  static ArTheme light();

  /// Every px value multiplied (and rounded, never below 1 px), for panels
  /// placed nearer or further than the 1.5 m the defaults assume — and for
  /// fitting the design into a viewport the runtime fixed for us
  /// (@ref viewport_fit.hpp).
  ///
  /// Shrinking the theme does NOT shrink the text an operator sees, as long as
  /// the panel is grown by the same factor in metres: metres-per-CSS-pixel is
  /// derived from `base_font_px` (`qar_web_panels/url_panel_sizing.hpp`), so
  /// `scaled(k)` multiplies it by `1/k` and every angular size — text, touch
  /// targets, gaps — comes out unchanged. The pixels are the design; the
  /// metres are what the eye and the hand actually meet.
  ArTheme scaled(double factor) const;

  /// Colours must be `#rrggbb`, sizes positive, the font list free of CSS
  /// syntax characters: theme strings are written into the stylesheet as-is.
  bool validate(std::string & out_error) const;
};

/// @brief The PIXEL custom properties the stylesheet defines, name and value,
/// in the order @ref render_theme_css writes them.
///
/// Exists so that exactly one list exists. The page rescales itself by writing
/// these same properties onto `:root` (@ref viewport_fit.hpp), and the values
/// it needs travel to it in the page's config block — from here, so a size
/// added to the theme cannot be forgotten by the half that scales it.
std::vector<std::pair<const char *, int>> theme_pixel_variables(const ArTheme & theme);

/// @brief The complete stylesheet: `:root` custom properties from the theme
/// followed by the component rules. Deterministic for a given theme.
std::string render_theme_css(const ArTheme & theme);

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__AR_THEME_HPP_
