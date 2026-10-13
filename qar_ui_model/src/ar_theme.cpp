// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_ui_model/ar_theme.hpp"

#include <cmath>
#include <string_view>

namespace qar_ui_model
{

ArTheme
ArTheme::dark()
{
  return ArTheme{};
}

ArTheme
ArTheme::light()
{
  ArTheme theme;
  theme.background = "#F4F6F8";
  theme.surface = "#FFFFFF";
  theme.surface_raised = "#E8EDF2";
  theme.border = "#B8C2CC";
  theme.text = "#0E141B";
  theme.text_muted = "#4A5661";
  theme.accent = "#0369A1";
  theme.accent_text = "#FFFFFF";
  theme.ok = "#1B7F3B";
  theme.warning = "#9A6700";
  theme.error = "#B42318";
  theme.stale = "#6B7785";
  theme.focus = "#C2410C";
  return theme;
}

ArTheme
ArTheme::scaled(double factor) const
{
  ArTheme out = *this;
  // Never below 1 px: validate() refuses a zero size, and a scale small enough
  // to round the 4 px focus ring away must still leave a theme that renders.
  const auto scale = [factor](int px) {
    const long scaled_px = std::lround(px * factor);
    return static_cast<int>(scaled_px < 1 ? 1 : scaled_px);
  };
  out.base_font_px = scale(base_font_px);
  out.small_font_px = scale(small_font_px);
  out.value_font_px = scale(value_font_px);
  out.heading_font_px = scale(heading_font_px);
  out.title_font_px = scale(title_font_px);
  out.touch_target_px = scale(touch_target_px);
  out.touch_gap_px = scale(touch_gap_px);
  out.gap_px = scale(gap_px);
  out.padding_px = scale(padding_px);
  out.radius_px = scale(radius_px);
  out.focus_ring_px = scale(focus_ring_px);
  out.table_row_px = scale(table_row_px);
  out.chart_height_px = scale(chart_height_px);
  out.row_basis_px = scale(row_basis_px);
  out.gap_tight_px = scale(gap_tight_px);
  out.gap_micro_px = scale(gap_micro_px);
  out.pad_tight_px = scale(pad_tight_px);
  out.pad_micro_px = scale(pad_micro_px);
  out.border_px = scale(border_px);
  out.chip_px = scale(chip_px);
  out.dot_px = scale(dot_px);
  out.dot_small_px = scale(dot_small_px);
  out.knob_px = scale(knob_px);
  out.block_px = scale(block_px);
  return out;
}

namespace
{

bool
is_hex_colour(std::string_view text)
{
  if (text.size() != 7 || text[0] != '#') {
    return false;
  }
  for (std::size_t i = 1; i < text.size(); ++i) {
    const char c = text[i];
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool
ArTheme::validate(std::string & out_error) const
{
  struct Named
  {
    const char * name;
    const std::string * value;
  };
  const Named colours[] = {
    {"background", &background}, {"surface", &surface}, {"surface_raised", &surface_raised},
    {"border", &border}, {"text", &text}, {"text_muted", &text_muted}, {"accent", &accent},
    {"accent_text", &accent_text}, {"ok", &ok}, {"warning", &warning}, {"error", &error},
    {"stale", &stale}, {"focus", &focus},
  };
  for (const Named & colour : colours) {
    if (!is_hex_colour(*colour.value)) {
      out_error = std::string("theme colour '") + colour.name + "' must be #rrggbb, got '" +
        *colour.value + "'";
      return false;
    }
  }
  struct Sized
  {
    const char * name;
    int value;
  };
  const Sized sizes[] = {
    {"base_font_px", base_font_px}, {"small_font_px", small_font_px},
    {"value_font_px", value_font_px}, {"heading_font_px", heading_font_px},
    {"title_font_px", title_font_px}, {"touch_target_px", touch_target_px},
    {"touch_gap_px", touch_gap_px}, {"gap_px", gap_px}, {"padding_px", padding_px},
    {"radius_px", radius_px}, {"focus_ring_px", focus_ring_px}, {"table_row_px", table_row_px},
    {"table_page_rows", table_page_rows}, {"chart_height_px", chart_height_px},
    {"row_basis_px", row_basis_px}, {"gap_tight_px", gap_tight_px},
    {"gap_micro_px", gap_micro_px}, {"pad_tight_px", pad_tight_px},
    {"pad_micro_px", pad_micro_px}, {"border_px", border_px}, {"chip_px", chip_px},
    {"dot_px", dot_px}, {"dot_small_px", dot_small_px}, {"knob_px", knob_px},
    {"block_px", block_px},
  };
  for (const Sized & size : sizes) {
    if (size.value <= 0 || size.value > 4096) {
      out_error = std::string("theme size '") + size.name + "' must be within 1..4096";
      return false;
    }
  }
  if (font_family.empty() || font_family.size() > 256) {
    out_error = "theme font_family must be 1..256 characters";
    return false;
  }
  for (char c : font_family) {
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
      c == ' ' || c == ',' || c == '\'' || c == '-' || c == '_';
    if (!allowed) {
      out_error = "theme font_family may only contain letters, digits, spaces, commas, quotes and dashes";
      return false;
    }
  }
  out_error.clear();
  return true;
}

namespace
{

// Component rules. Sizes and colours come from the :root block so a theme
// change never touches this text. Deliberately NO :hover selector anywhere —
// see ar_theme.hpp. Kept under MSVC's 16 KB single-literal limit.
const char kComponentCssBase[] = R"QARCSS(
*{box-sizing:border-box}
html,body{margin:0;padding:0;background:var(--qar-bg);color:var(--qar-text);font-family:var(--qar-font-family);font-size:var(--qar-font);line-height:1.4}
body{-webkit-text-size-adjust:100%;overscroll-behavior:none;-webkit-user-select:none;user-select:none}
button,input{font:inherit;color:inherit;margin:0}
.qar-panel{padding:var(--qar-pad);display:flex;flex-direction:column;gap:var(--qar-gap);min-height:100vh}
.qar-header{display:flex;align-items:flex-start;justify-content:space-between;gap:var(--qar-gap);flex-wrap:wrap}
.qar-title{font-size:var(--qar-font-h1);margin:0;font-weight:700;word-break:break-word}
.qar-subtitle{margin:calc(var(--qar-gap-tight) / 2) 0 0;color:var(--qar-muted);font-size:var(--qar-font-small);word-break:break-word}
.qar-conn{display:inline-flex;align-items:center;gap:var(--qar-pad-micro);min-height:var(--qar-chip);padding:var(--qar-gap-micro) var(--qar-pad-tight);border-radius:999px;background:var(--qar-surface);font-size:var(--qar-font-small);white-space:nowrap}
.qar-conn-dot{width:var(--qar-dot-small);height:var(--qar-dot-small);border-radius:50%;background:var(--qar-stale);flex:none}
.qar-conn[data-qar-state="online"] .qar-conn-dot{background:var(--qar-ok)}
.qar-conn[data-qar-state="stale"] .qar-conn-dot{background:var(--qar-warn)}
.qar-conn[data-qar-state="offline"] .qar-conn-dot{background:var(--qar-err)}
.qar-section{background:var(--qar-surface);border-radius:var(--qar-radius);padding:var(--qar-pad);display:flex;flex-direction:column;gap:var(--qar-gap)}
.qar-section-title{font-size:var(--qar-font-h2);margin:0;font-weight:600;word-break:break-word}
.qar-panel-body,.qar-section-body,.qar-column{display:flex;flex-direction:column;gap:var(--qar-gap);min-width:0}
.qar-row{display:flex;flex-wrap:wrap;gap:var(--qar-gap);align-items:flex-start}
.qar-row>*{flex-grow:1;flex-shrink:1;flex-basis:var(--qar-col);min-width:0}
.qar-widget{display:flex;flex-direction:column;gap:var(--qar-gap-tight);min-width:0}
)QARCSS";

// NOTHING STACKED VERTICALLY MAY BE SHRUNK — the rule that opens the second
// chunk, and the reason a page without it draws text over text.
//
// A flex item defaults to `flex-shrink: 1`, and in a COLUMN container that
// shrinks its HEIGHT. The panel is given a definite height the moment the
// click pager turns it into the scroller (`[data-qar-pager] .qar-panel` below,
// `height: 100%`), so without this rule every section, widget, caption and
// component box is squeezed to make a viewport's worth of content fit the
// viewport — while the text inside keeps the size its font asks for. That is
// text over text.
//
// The spec's automatic minimum size (`min-height: auto` on a flex item) is
// supposed to prevent exactly this, and in a compliant engine it does; on the
// headset's WebKit 610 it does not. The evidence is a page rendered by that
// engine: the ONLY part of it that comes out right is the paging rail, which is
// the only thing on it that is positioned rather than laid out as a flex item
// — and its buttons keep their size because they carry an EXPLICIT min-height,
// which the same engine honours. So this rule states explicitly what
// `min-height: auto` is supposed to imply.
//
// Row containers are deliberately NOT listed: `.qar-row > *` shrinks on the
// INLINE axis, which is how a row narrows into a viewport instead of
// overflowing it, and narrowing a width has never put one line on top of
// another.
//
// (The split is also what keeps each literal under MSVC's 16 KB limit, and it
// keeps a page of rationale out of the bytes every panel puts on the wire.)
const char kComponentCssFlow[] = R"QARCSS(
.qar-panel>*,.qar-panel-body>*,.qar-section>*,.qar-section-body>*,.qar-column>*,.qar-widget>*,.qar-comp>*,.qar-scroll>*{flex-shrink:0}
.qar-caption{font-size:var(--qar-font-small);color:var(--qar-muted);letter-spacing:.02em;word-break:break-word}
.qar-help{margin:0;font-size:var(--qar-font-small);color:var(--qar-muted)}
.qar-label{margin:0;word-break:break-word}
.qar-readout{display:flex;align-items:baseline;gap:var(--qar-pad-micro);flex-wrap:wrap;min-height:calc(var(--qar-font-value) * 1.4)}
.qar-readout-value{font-size:var(--qar-font-value);font-weight:600;font-variant-numeric:tabular-nums;word-break:break-word}
.qar-unit{color:var(--qar-muted);font-size:var(--qar-font-small)}
.qar-btn{min-width:var(--qar-touch);min-height:var(--qar-touch);padding:0 var(--qar-pad-tight);border:var(--qar-border-w) solid var(--qar-border);border-radius:var(--qar-radius);background:var(--qar-raised);color:var(--qar-text);font-size:var(--qar-font);font-weight:600;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;gap:var(--qar-gap-tight);-webkit-tap-highlight-color:transparent;touch-action:manipulation;word-break:break-word}
.qar-btn:active{background:var(--qar-accent);color:var(--qar-accent-text);border-color:var(--qar-accent)}
.qar-btn:focus,.qar-field:focus,.qar-toggle:focus,.qar-range:focus{outline:var(--qar-focus) solid var(--qar-focus-color);outline-offset:var(--qar-border-w)}
.qar-btn:focus-visible,.qar-field:focus-visible,.qar-toggle:focus-visible,.qar-range:focus-visible{outline:var(--qar-focus) solid var(--qar-focus-color);outline-offset:var(--qar-border-w)}
.qar-btn[disabled]{opacity:.45;cursor:default}
.qar-step{flex:none;font-size:var(--qar-font-value)}
.qar-action{width:100%;min-height:calc(var(--qar-touch) + var(--qar-gap-tight))}
.qar-action[data-qar-armed="1"]{background:var(--qar-warn);color:var(--qar-accent-text);border-color:var(--qar-warn)}
.qar-action[data-qar-ack="ok"]{border-color:var(--qar-ok)}
.qar-action[data-qar-ack="error"]{border-color:var(--qar-err)}
.qar-stepper,.qar-sliderrow,.qar-textrow{display:flex;align-items:center;gap:var(--qar-touch-gap)}
.qar-field{flex-grow:1;flex-shrink:1;flex-basis:var(--qar-block);min-width:0;min-height:var(--qar-touch);padding:0 var(--qar-pad-tight);border:var(--qar-border-w) solid var(--qar-border);border-radius:var(--qar-radius);background:var(--qar-bg);color:var(--qar-text);font-size:var(--qar-font-value);font-variant-numeric:tabular-nums;text-align:center;-webkit-user-select:text;user-select:text}
.qar-field[readonly]{border-style:dashed}
input[type=number]::-webkit-inner-spin-button,input[type=number]::-webkit-outer-spin-button{-webkit-appearance:none;margin:0}
input[type=number]{-moz-appearance:textfield}
.qar-toggle{display:inline-flex;align-items:center;gap:var(--qar-pad-tight);min-height:var(--qar-touch);padding:0 var(--qar-pad-tight) 0 var(--qar-gap-tight);border:var(--qar-border-w) solid var(--qar-border);border-radius:999px;background:var(--qar-raised);cursor:pointer;width:100%;text-align:left;-webkit-tap-highlight-color:transparent;touch-action:manipulation}
.qar-toggle-track{position:relative;width:calc(var(--qar-chip) * 2);height:var(--qar-chip);border-radius:999px;background:var(--qar-border);flex:none}
.qar-toggle-knob{position:absolute;top:calc(var(--qar-gap-tight) / 2);left:calc(var(--qar-gap-tight) / 2);width:calc(var(--qar-chip) - var(--qar-gap-tight));height:calc(var(--qar-chip) - var(--qar-gap-tight));border-radius:50%;background:var(--qar-text)}
.qar-toggle[aria-checked="true"] .qar-toggle-track{background:var(--qar-ok)}
.qar-toggle[aria-checked="true"] .qar-toggle-knob{left:calc(var(--qar-chip) + var(--qar-gap-tight) / 2);background:var(--qar-accent-text)}
.qar-toggle[disabled]{cursor:default;opacity:.7}
.qar-toggle-text{font-weight:600}
.qar-range{flex-grow:1;flex-shrink:1;flex-basis:var(--qar-block);min-width:0;height:var(--qar-touch);-webkit-appearance:none;appearance:none;background:transparent;cursor:pointer}
)QARCSS";

// EXEMPT from the no-bare-pixels rule, and the only such lengths left in the
// sheet besides the pill radii. A range input's track and thumb are shadow-DOM
// pseudo-elements, and whether this engine substitutes a custom property into
// one is not evidenced (docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser").
// So the literals come first as the fallback that renders correctly, and the
// scaled values follow as a second rule that any engine unable to resolve them
// drops whole. The thumb MUST scale where it can: the control is `--qar-touch`
// high, and a thumb frozen at 48 px overhangs it far enough to reach the
// caption above once the theme scale drops past ~0.6.
const char kComponentCssControls[] = R"QARCSS(
.qar-range::-webkit-slider-runnable-track{height:16px;border-radius:8px;background:var(--qar-border)}
.qar-range::-webkit-slider-thumb{-webkit-appearance:none;width:48px;height:48px;border-radius:50%;background:var(--qar-accent);border:4px solid var(--qar-bg);margin-top:-16px}
.qar-range::-moz-range-track{height:16px;border-radius:8px;background:var(--qar-border)}
.qar-range::-moz-range-thumb{width:48px;height:48px;border-radius:50%;background:var(--qar-accent);border:4px solid var(--qar-bg)}
.qar-range::-webkit-slider-runnable-track{height:var(--qar-dot-small);border-radius:calc(var(--qar-dot-small) / 2)}
.qar-range::-webkit-slider-thumb{width:var(--qar-knob);height:var(--qar-knob);border-width:var(--qar-focus);margin-top:calc((var(--qar-dot-small) - var(--qar-knob)) / 2)}
.qar-range::-moz-range-track{height:var(--qar-dot-small);border-radius:calc(var(--qar-dot-small) / 2)}
.qar-range::-moz-range-thumb{width:var(--qar-knob);height:var(--qar-knob);border-width:var(--qar-focus)}
.qar-range[disabled]{cursor:default;opacity:.7}
.qar-choice{display:flex;flex-wrap:wrap;gap:var(--qar-touch-gap)}
.qar-chip{flex:1 1 auto}
.qar-chip[aria-checked="true"]{background:var(--qar-accent);color:var(--qar-accent-text);border-color:var(--qar-accent)}
.qar-vector,.qar-quat,.qar-euler{display:flex;flex-wrap:wrap;gap:var(--qar-touch-gap)}
.qar-comp{flex-grow:1;flex-shrink:1;flex-basis:var(--qar-block);display:flex;flex-direction:column;gap:var(--qar-gap-micro);padding:var(--qar-pad-micro) var(--qar-pad-tight);border-radius:var(--qar-radius);background:var(--qar-raised);min-width:0}
.qar-comp-name{font-size:var(--qar-font-small);color:var(--qar-muted);text-transform:uppercase;letter-spacing:.06em}
.qar-comp .qar-stepper{flex-wrap:wrap}
.qar-tablewrap{overflow-x:auto;border-radius:var(--qar-radius);background:var(--qar-raised)}
.qar-table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}
.qar-table th,.qar-table td{height:var(--qar-row);padding:0 var(--qar-pad-tight);text-align:left;border-bottom:var(--qar-border-w) solid var(--qar-border);white-space:nowrap}
.qar-table th{font-size:var(--qar-font-small);color:var(--qar-muted);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
.qar-table td.qar-num{text-align:right}
.qar-table tr:last-child td{border-bottom:0}
.qar-pager{display:flex;align-items:center;justify-content:space-between;gap:var(--qar-touch-gap)}
.qar-pager-info{color:var(--qar-muted);font-size:var(--qar-font-small)}
.qar-gauge-track{height:var(--qar-dot);border-radius:999px;background:var(--qar-border);overflow:hidden}
.qar-gauge-fill{height:100%;width:0;background:var(--qar-accent)}
.qar-gauge-fill[data-qar-level="ok"]{background:var(--qar-ok)}
.qar-gauge-fill[data-qar-level="warning"]{background:var(--qar-warn)}
.qar-gauge-fill[data-qar-level="error"]{background:var(--qar-err)}
.qar-gauge-fill[data-qar-level="stale"]{background:var(--qar-stale)}
.qar-gauge-bounds,.qar-chart-bounds{display:flex;justify-content:space-between;color:var(--qar-muted);font-size:var(--qar-font-small)}
.qar-status{display:flex;align-items:center;gap:var(--qar-pad-tight);min-height:var(--qar-touch);padding:0 var(--qar-pad-tight);border-radius:var(--qar-radius);background:var(--qar-raised)}
.qar-status-dot{width:var(--qar-dot);height:var(--qar-dot);border-radius:50%;background:var(--qar-stale);flex:none;box-shadow:0 0 0 4px rgba(128,128,128,.18)}
.qar-status[data-qar-level="ok"] .qar-status-dot{background:var(--qar-ok)}
.qar-status[data-qar-level="warning"] .qar-status-dot{background:var(--qar-warn)}
.qar-status[data-qar-level="error"] .qar-status-dot{background:var(--qar-err)}
.qar-status[data-qar-level="stale"] .qar-status-dot{background:var(--qar-stale)}
.qar-status[data-qar-level="neutral"] .qar-status-dot{background:var(--qar-accent)}
.qar-status-text{font-weight:600;font-size:var(--qar-font-value);word-break:break-word}
.qar-chart{width:100%;height:var(--qar-chart-h);display:block;border-radius:var(--qar-radius);background:var(--qar-raised)}
.qar-chart-line{fill:none;stroke:var(--qar-accent);stroke-width:4;stroke-linejoin:round;stroke-linecap:round;vector-effect:non-scaling-stroke}
.qar-chart-grid{stroke:var(--qar-border);stroke-width:1;vector-effect:non-scaling-stroke}
.qar-imagewrap{border-radius:var(--qar-radius);background:var(--qar-raised);overflow:hidden;min-height:var(--qar-block);display:flex;align-items:center;justify-content:center}
.qar-image{max-width:100%;display:block}
.qar-image[hidden]{display:none}
.qar-json{margin:0;padding:var(--qar-pad-tight);border-radius:var(--qar-radius);background:var(--qar-raised);font-family:ui-monospace,Consolas,'Roboto Mono',monospace;font-size:var(--qar-font-small);white-space:pre-wrap;word-break:break-word;max-height:calc(var(--qar-block) * 4);overflow:auto;-webkit-user-select:text;user-select:text}
.qar-footer{margin-top:auto}
.qar-statusline{min-height:var(--qar-chip);display:flex;align-items:center;padding:var(--qar-gap-tight) var(--qar-pad-tight);border-radius:var(--qar-radius);background:var(--qar-surface);color:var(--qar-muted);font-size:var(--qar-font-small);word-break:break-word}
.qar-statusline[data-qar-level="warning"]{color:var(--qar-warn)}
.qar-statusline[data-qar-level="error"]{color:var(--qar-err)}
.qar-statusline[data-qar-level="ok"]{color:var(--qar-ok)}
.qar-statusline:empty{display:none}
.qar-ack{font-size:var(--qar-font-small);color:var(--qar-muted);min-height:1.4em}
.qar-ack[data-qar-level="error"]{color:var(--qar-err)}
.qar-ack[data-qar-level="ok"]{color:var(--qar-ok)}

/* Boot diagnostic. Rendered visible; the runtime hides it once it has started, so
   an engine that cannot run the page says why instead of showing a blank rectangle. */
.qar-diag{padding:var(--qar-pad);background:var(--qar-surface);color:var(--qar-text);border-left:var(--qar-gap-tight) solid var(--qar-err);font-size:var(--qar-font);word-break:break-word}
.qar-diag-title{display:block;margin:0 0 var(--qar-gap-tight);font-size:var(--qar-font-h2);font-weight:700;color:var(--qar-err)}
.qar-diag-detail{display:block}
.qar-diag[hidden],.qar-pager[hidden],.qar-scroll[hidden]{display:none}

/* Click-to-page rail. Content below the fold has to be REACHABLE in AR, and a
   scrollbar is a poor way to do it: a thumb is a few millimetres an operator
   must hit and hold in mid air. (The QarOS SDK 2026-09-21 release added DRAG,
   so this is a judgement rather than a constraint -- see scroll_paging.hpp.)
   Five buttons, one click each, VISIBLE - never hover-revealed, which is
   already a rule everywhere else on this page.

   The rail is absolutely positioned in a body that does not scroll, rather
   than viewport-fixed: the headset engine is WebKit 610, where `absolute` and
   `overflow:auto` are on the evidenced list and fixed positioning is not
   (docs/SDK_COMPATIBILITY.md). So when the rail is up the SCROLLER becomes
   .qar-panel and the rail is pinned to the body around it, which does not move.
   The runtime sets data-qar-pager on <html> only after it has
   measured real overflow, so a page that fits - the usual case in a desktop
   browser, where the wheel works anyway - is laid out unchanged. */
.qar-scroll{position:absolute;top:0;right:0;bottom:0;width:calc(var(--qar-touch) + var(--qar-gap) * 2);display:flex;flex-direction:column;align-items:center;justify-content:center;gap:var(--qar-touch-gap);padding:var(--qar-gap) 0;background:var(--qar-surface);border-left:var(--qar-border-w) solid var(--qar-border)}
.qar-scroll-btn{width:var(--qar-touch);min-width:var(--qar-touch);min-height:var(--qar-touch);padding:0;font-size:var(--qar-font-value)}
.qar-scroll-btn.qar-scroll-end{font-size:var(--qar-font-small);letter-spacing:.04em}
.qar-scroll-pos{font-size:var(--qar-font-small);color:var(--qar-muted);text-align:center;line-height:1.2;font-variant-numeric:tabular-nums}
[data-qar-pager]{height:100%}
[data-qar-pager] body{height:100%;overflow:hidden;position:relative}
[data-qar-pager] .qar-panel{height:100%;min-height:0;overflow-y:auto;padding-right:calc(var(--qar-touch) + var(--qar-gap) * 3)}

/* Fallback spacing for engines that parse `gap` but do not apply it to flex
   containers (WebKit < 611, i.e. Safari < 14.1 - which is what Ultralight 1.3
   reports). The runtime measures real gap support at boot and only then sets
   data-qar-nogap on <html>; on an engine that honours gap nothing below matches. */
[data-qar-nogap] .qar-panel>*+*,[data-qar-nogap] .qar-section>*+*,[data-qar-nogap] .qar-panel-body>*+*,[data-qar-nogap] .qar-section-body>*+*,[data-qar-nogap] .qar-column>*+*{margin-top:var(--qar-gap)}
[data-qar-nogap] .qar-widget>*+*{margin-top:var(--qar-gap-tight)}
[data-qar-nogap] .qar-comp>*+*{margin-top:var(--qar-gap-micro)}
[data-qar-nogap] .qar-header>*+*{margin-left:var(--qar-gap)}
[data-qar-nogap] .qar-conn>*+*,[data-qar-nogap] .qar-readout>*+*{margin-left:var(--qar-pad-micro)}
[data-qar-nogap] .qar-btn>*+*{margin-left:var(--qar-gap-tight)}
[data-qar-nogap] .qar-stepper>*+*,[data-qar-nogap] .qar-sliderrow>*+*,[data-qar-nogap] .qar-textrow>*+*,[data-qar-nogap] .qar-pager>*+*{margin-left:var(--qar-touch-gap)}
[data-qar-nogap] .qar-toggle>*+*{margin-left:var(--qar-pad-tight)}
[data-qar-nogap] .qar-status>*+*{margin-left:var(--qar-pad-tight)}
[data-qar-nogap] .qar-scroll>*+*{margin-top:var(--qar-touch-gap)}
[data-qar-nogap] .qar-row>*{margin-right:var(--qar-gap);margin-bottom:var(--qar-gap)}
[data-qar-nogap] .qar-choice>*,[data-qar-nogap] .qar-vector>*,[data-qar-nogap] .qar-quat>*,[data-qar-nogap] .qar-euler>*{margin-right:var(--qar-touch-gap);margin-bottom:var(--qar-touch-gap)}
)QARCSS";

void
append_px(std::string & out, const char * name, int value)
{
  out += name;
  out += ':';
  out += std::to_string(value);
  out += "px;";
}

void
append_colour(std::string & out, const char * name, const std::string & value)
{
  out += name;
  out += ':';
  out += value;
  out += ';';
}

}  // namespace

std::vector<std::pair<const char *, int>>
theme_pixel_variables(const ArTheme & theme)
{
  return {
    {"--qar-font", theme.base_font_px},
    {"--qar-font-small", theme.small_font_px},
    {"--qar-font-value", theme.value_font_px},
    {"--qar-font-h2", theme.heading_font_px},
    {"--qar-font-h1", theme.title_font_px},
    {"--qar-touch", theme.touch_target_px},
    {"--qar-touch-gap", theme.touch_gap_px},
    {"--qar-gap", theme.gap_px},
    {"--qar-pad", theme.padding_px},
    {"--qar-radius", theme.radius_px},
    {"--qar-focus", theme.focus_ring_px},
    {"--qar-row", theme.table_row_px},
    {"--qar-chart-h", theme.chart_height_px},
    {"--qar-col", theme.row_basis_px},
    {"--qar-gap-tight", theme.gap_tight_px},
    {"--qar-gap-micro", theme.gap_micro_px},
    {"--qar-pad-tight", theme.pad_tight_px},
    {"--qar-pad-micro", theme.pad_micro_px},
    {"--qar-border-w", theme.border_px},
    {"--qar-chip", theme.chip_px},
    {"--qar-dot", theme.dot_px},
    {"--qar-dot-small", theme.dot_small_px},
    {"--qar-knob", theme.knob_px},
    {"--qar-block", theme.block_px},
  };
}

std::string
render_theme_css(const ArTheme & theme)
{
  std::string out;
  out.reserve(
    sizeof(kComponentCssBase) + sizeof(kComponentCssFlow) + sizeof(kComponentCssControls) + 1024);
  out += ":root{";
  for (const auto & variable : theme_pixel_variables(theme)) {
    append_px(out, variable.first, variable.second);
  }
  append_colour(out, "--qar-bg", theme.background);
  append_colour(out, "--qar-surface", theme.surface);
  append_colour(out, "--qar-raised", theme.surface_raised);
  append_colour(out, "--qar-border", theme.border);
  append_colour(out, "--qar-text", theme.text);
  append_colour(out, "--qar-muted", theme.text_muted);
  append_colour(out, "--qar-accent", theme.accent);
  append_colour(out, "--qar-accent-text", theme.accent_text);
  append_colour(out, "--qar-ok", theme.ok);
  append_colour(out, "--qar-warn", theme.warning);
  append_colour(out, "--qar-err", theme.error);
  append_colour(out, "--qar-stale", theme.stale);
  append_colour(out, "--qar-focus-color", theme.focus);
  out += "--qar-font-family:";
  out += theme.font_family;
  out += "}";
  out += kComponentCssBase;
  out += kComponentCssFlow;
  out += kComponentCssControls;
  return out;
}

}  // namespace qar_ui_model
