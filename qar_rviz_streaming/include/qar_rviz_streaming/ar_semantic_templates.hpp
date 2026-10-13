// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_SEMANTIC_TEMPLATES_HPP_
#define QAR_RVIZ_STREAMING__AR_SEMANTIC_TEMPLATES_HPP_

/// @file ar_semantic_templates.hpp
/// @brief Purpose-built AR panels for the ROS types an operator cares about.
///
/// `GenericFormTemplate` can lay out ANY message, and that is exactly its
/// limitation: it has no opinion. It cannot know that a `BatteryState`'s
/// `power_supply_status` is an enum whose `2` means "discharging", that a
/// `Twist` wants a stop button bigger than everything else on the panel, or that
/// an `Image`'s `data` is four megabytes that must never leave the host. This
/// file is that opinion — one @ref IPanelTemplate per family of ROS types, each
/// registered ahead of the generic form and each producing a layout a human
/// would recognise as designed.
///
/// Three rules hold for every template here:
///
///  1. **Layout only.** The action ids (`ros.set` / `ros.publish` / `ros.call` /
///     `ros.send_goal` / `ros.cancel_goal`) and the document sections
///     (`msg`/`draft`, `request`/`response`, `goal`/`feedback`/`result`/`status`)
///     are the panel generator's fixed contract (ar_panel_generator.hpp). A
///     template chooses widgets and where they go; it never invents an action
///     and never writes outside `draft`/`request`/`goal`.
///  2. **Probe, never assume.** Every widget is emitted only after its binding
///     has been resolved against the document schema that `generate()` will
///     validate against. A field that a distro renamed, dropped or never had
///     costs one missing widget, not a refused panel; a type whose schema is
///     unrecognisable falls all the way back to the generic form. This is what
///     makes the templates safe to ship for types this repo cannot introspect
///     (nav2 is not in every distro).
///  3. **Never stream a bulk payload.** `Image.data`, `PointCloud2.data`,
///     `OccupancyGrid.data` and `LaserScan.ranges` are megabytes per message.
///     No template binds them; the panels show dimensions, encoding, resolution
///     and frame instead, and @ref elide_large_arrays caps what the host pushes
///     so a bound-but-long array (a `Path` with 5000 poses) cannot melt the
///     socket either.
///
/// Pure C++17 + `qar_ui_model`: no Qt, no rclcpp, no rviz, so every layout
/// decision is unit-tested in the plain-CMake build.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "qar_ui_model/ui_value.hpp"

#include "qar_rviz_streaming/ar_panel_generator.hpp"

namespace qar_rviz_streaming
{

/// @brief One entry of the template catalog: what a template is and what it claims.
struct SemanticTemplateInfo
{
  std::string name;                ///< @ref IPanelTemplate::name().
  std::vector<std::string> types;  ///< Canonical ROS type names it matches.
  int score = 0;                   ///< @ref IPanelTemplate::match() on a hit.
  std::string summary;             ///< One line, for the docs table.
};

/// @brief Every semantic template, in registration order.
const std::vector<SemanticTemplateInfo> & semantic_template_catalog();

/// @brief Register all of them into @p registry, ahead of the generic form.
///
/// Scores are chosen so a more specific claim wins: a family template scores
/// @ref kSemanticScore, and nothing here claims the same type twice.
void register_semantic_templates(PanelTemplateRegistry & registry);

/// @brief The template claiming @p type_name, or "" when none does.
std::string semantic_template_for_type(std::string_view type_name);

/// @brief The score a semantic template returns for a type it claims.
/// Well above `GenericFormTemplate`'s 1 and well below anything a downstream
/// integrator might register to override one of these.
constexpr int kSemanticScore = 100;

// ---------------------------------------------------------------------------
// Large payloads
// ---------------------------------------------------------------------------

/// @brief How many elements of one array a panel is willing to stream.
///
/// 256 is above every array a template actually binds (a `PointCloud2`'s
/// `fields`, a `DiagnosticArray`'s `status`, a `JointState`'s `name`) and far
/// below every array that would hurt (a 640x480 `Image` is 307200 bytes, a
/// tenth of a degree `LaserScan` is 3600 floats). It is a cap, not a promise:
/// the array's length is preserved in the elision marker.
constexpr std::size_t kMaxStreamedArrayElements = 256;

/// @brief Truncate every over-long array in @p value, in place, depth first.
///
/// The host pushes a whole `msg` per tick (`QarStreamingPanel::pushArToolData`),
/// so "the template did not bind `data`" is not by itself enough to keep four
/// megabytes off the WebSocket — the bytes would be sent and then ignored by
/// the page. This is the other half: an array longer than @p max_elements keeps
/// its first @p max_elements entries and gains a final string marker
/// `"... N more of M"`, which is what a `JsonInspector` or a `Table` shows if
/// one is ever pointed at it.
///
/// @return how many arrays were truncated.
std::size_t elide_large_arrays(
  qar_ui_model::UiValue & value,
  std::size_t max_elements = kMaxStreamedArrayElements);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_SEMANTIC_TEMPLATES_HPP_
