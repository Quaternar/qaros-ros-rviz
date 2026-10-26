// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__HTML_RENDERER_HPP_
#define QAR_UI_MODEL__HTML_RENDERER_HPP_

/// @file html_renderer.hpp
/// @brief One `UiModel` -> one self-contained HTML page.
///
/// The page carries everything inline — stylesheet, the serialized model, the
/// initial data, the runtime script — because the QarOS browser may have no
/// route to anything but the server that handed it the URL, and a
/// Content-Security-Policy meta forbids the page from fetching anything else
/// even if it could.
///
/// The DOM is rendered server-side (every widget is present in the HTML, with
/// `data-qar-id` / `data-qar-type` / `data-qar-bind` attributes) so a page is
/// complete and inspectable before a line of script runs, and the runtime
/// only ever fills VALUES into it via `textContent` / properties — it never
/// builds markup from data. Every string that reaches the markup passes
/// through @ref html_escape; every value the runtime reads is JSON inside a
/// `<script type="application/json">` element, written with the writer's
/// html_safe escaping so no string can end the element early.
///
/// Rendering is deterministic: same model + options, same bytes.

#include <string>
#include <vector>

#include "qar_ui_model/ar_theme.hpp"
#include "qar_ui_model/ui_model.hpp"

namespace qar_ui_model
{

struct HtmlRenderOptions
{
  ArTheme theme = ArTheme::dark();

  /// WebSocket endpoint. Empty: the runtime derives
  /// `ws(s)://<page host>/ws/<panel id>` from the page's own location, and
  /// stays offline (showing initial data) when the page was opened from a
  /// file. The panel server serves that route or sets this explicitly.
  std::string websocket_url;

  std::string page_title;  ///< Empty: the panel's title.
  std::string language = "en";

  int reconnect_ms = 2000;      ///< Delay before re-dialing a closed socket.
  int ping_interval_ms = 5000;  ///< Application-level ping while idle.
  int stale_after_ms = 5000;    ///< No frame for this long: show "stale".

  bool include_runtime = true;  ///< false: static page (debugging, printing).
  bool include_csp = true;      ///< The Content-Security-Policy meta tag.
};

/// @brief Render the page. Refuses (returns false, html empty) when @ref
/// validate reports an Error or the theme does not validate; warnings are
/// reported and rendering proceeds.
bool render_html_page(
  const UiModel & model,
  const HtmlRenderOptions & options,
  std::string & out_html,
  std::vector<UiProblem> & out_problems);

/// @brief The static markup of one widget subtree, exactly as it appears in
/// the page. Public so golden tests pin it and a server can send partials.
std::string render_widget_fragment(const Widget & widget);

/// @brief The embedded runtime script, for serving separately or testing.
std::string runtime_javascript();

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__HTML_RENDERER_HPP_
