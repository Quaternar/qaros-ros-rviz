// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_COPY_SCALE_HPP_
#define QAR_RVIZ_STREAMING__AR_COPY_SCALE_HPP_

/// @file ar_copy_scale.hpp
/// @brief How large the AR copy of a Qt panel is: the factor on the metres one logical pixel
/// covers, the supersampling that keeps the larger copy sharp, and the lines the log says about
/// them. Standard library only.
///
/// ## Two ways to enlarge
///
/// A copy is enlarged as a whole (@ref ArCopyMethod::WholeCopy, described next) or, for the RViz
/// panels that have an AR copy of their own, by drawing its controls larger while its text keeps
/// its size (@ref ArCopyMethod::EnlargedControls, @ref ar_copy_controls_scale).
///
/// ## What an enlarged copy is
///
/// The controls of a Qt panel (the arrows of a spin box, the handle of a slider, a check box)
/// are small when the panel is hung in a room at its native size. An enlarged copy covers
/// @ref ArCopyScale::factor times the metres per logical pixel and is captured at the same
/// factor as a render scale, so every control is that much larger in AR and the text keeps
/// the pixels per metre of the native copy. The panel on the desktop does not change.
///
/// ## The height limit
///
/// An enlarged copy is at most @ref kArCopyMaxHeightM tall, frame included. The frame is in
/// metres and does not scale, so the height is a straight line of the factor; the factor is
/// lowered until the copy fits and never below 1.

#include <cstdint>
#include <string>

namespace qar_rviz_streaming
{

/// The factor of an enlarged copy when the settings say nothing else.
constexpr double kArCopyScaleDefault = 2.0;
/// The range of the factor a setting may hold.
constexpr double kArCopyScaleMin = 1.0;
constexpr double kArCopyScaleMax = 3.0;
/// The tallest enlarged copy in metres (video and frame).
constexpr float kArCopyMaxHeightM = 1.8f;

/// The factor on the metres per pixel and on the render scale of an AR copy that draws its own
/// controls larger (the whole copy, text included), when the settings say nothing else.
constexpr double kArCopyOverallScaleDefault = 1.5;
/// The range of that factor a setting may hold.
constexpr double kArCopyOverallScaleMin = 1.0;
constexpr double kArCopyOverallScaleMax = 2.0;

/// The factor of the text of an enlarged AR copy: the copy root's font is this many times the
/// desktop font, and the controls and rows follow the font. The environment variable
/// `QAR_PANEL_AR_COPY_TEXT_SCALE` wins over it and is never written to the settings.
constexpr double kArCopyTextScale = 1.25;
/// The range of the factor the environment may hold.
constexpr double kArCopyTextScaleMin = 1.0;
constexpr double kArCopyTextScaleMax = 2.0;
constexpr const char * kArCopyTextScaleEnv = "QAR_PANEL_AR_COPY_TEXT_SCALE";
/// `default` switches the font hinting of the copy root back to the platform's own.
constexpr const char * kArCopyFontHintingEnv = "QAR_PANEL_AR_COPY_FONT_HINTING";

/// How the glyphs of an AR copy are hinted.
enum class ArCopyHinting : std::uint8_t
{
  None = 0,  ///< no hinting: the glyphs scale smoothly (the default for a copy)
  Default    ///< the platform's own hinting
};

/// @brief The text factor of an enlarged copy from the text of the environment variable: the
/// constant @ref kArCopyTextScale for a null, an empty or an unreadable text or one that is not
/// finite, else the value clamped to @ref kArCopyTextScaleMin .. @ref kArCopyTextScaleMax.
double ar_copy_text_scale(const char * env_text);

/// @brief The hinting from the text of the environment variable: @ref ArCopyHinting::Default
/// for the word `default` (any case, trimmed), else @ref ArCopyHinting::None.
ArCopyHinting ar_copy_font_hinting(const char * env_text);

/// @brief `AR PANEL: '<key>' copy text x<scale>, hinting <none|default>` (two decimals).
std::string ar_copy_text_line(const std::string & key, double scale, ArCopyHinting hinting);

/// The size the operator asks for.
enum class ArCopySize : std::uint8_t
{
  Enlarged = 0,  ///< @ref kArCopyScaleDefault or the factor of the setting
  Native         ///< the metres per logical pixel of the library
};

/// "enlarged" or "native".
const char * to_string(ArCopySize size);
/// Reads the word of @ref to_string, trimmed and without regard to case.
/// @return false (and @p out unchanged) for any other text.
bool ar_copy_size_from_word(const std::string & word, ArCopySize & out);

/// Where the factor in force came from.
enum class ArCopyScaleSource : std::uint8_t
{
  Setting = 0,     ///< the global choice
  PanelChoice,     ///< the choice made for this panel
  LimitedByHeight  ///< the factor was lowered to keep the copy under @ref kArCopyMaxHeightM
};

/// How a copy is enlarged.
enum class ArCopyMethod : std::uint8_t
{
  WholeCopy = 0,    ///< more metres per pixel and a render scale: text and controls grow together
  EnlargedControls  ///< an AR copy whose controls a style draws larger; the text keeps its size
};

/// @brief The scale one copy gets.
struct ArCopyScale
{
  double factor = 1.0;        ///< multiplies the metres per logical pixel; 1.0 is native
  double render_scale = 1.0;  ///< the supersampling that keeps the copy sharp
  ArCopyScaleSource source = ArCopyScaleSource::Setting;
  ArCopyMethod method = ArCopyMethod::WholeCopy;
  double control_factor = 1.0;  ///< EnlargedControls: the factor of the style; 1.0 is no style
};

/// @brief The scale for a copy whose height is a straight line of the factor.
///
/// @param size what the operator asks for
/// @param enlarged_factor the factor of an enlarged copy, @ref kArCopyScaleDefault unless the
/// setting says otherwise; a value that is not finite or lies outside the range is read as the
/// default
/// @param native_height_m the height of the copy at factor 1, frame included
/// @param height_at_wanted_m the height at the factor the size asks for. A copy whose frame
/// does not scale is lower than @p native_height_m times the factor; a value that is not above
/// @p native_height_m (or not finite) is read as "the height scales with the factor"
/// @param panel_choice true when the size is a choice made for this panel
ArCopyScale ar_copy_scale(
  ArCopySize size, double enlarged_factor, float native_height_m, float height_at_wanted_m,
  bool panel_choice);

/// The same for a copy without a frame: the height is the native height times the factor.
ArCopyScale ar_copy_scale(
  ArCopySize size, double enlarged_factor, float native_height_m, bool panel_choice);

/// @brief The scale of an AR copy that draws its controls larger.
///
/// `Native` gives factor 1, render scale 1 and control factor 1 (the copy stays a copy, without
/// the style). `Enlarged` gives the control factor @p control_factor (clamped to
/// @ref kArCopyScaleMin .. @ref kArCopyScaleMax, @ref kArCopyScaleDefault when it is not
/// finite) and the factor @p overall_factor (clamped to @ref kArCopyOverallScaleMin ..
/// @ref kArCopyOverallScaleMax, @ref kArCopyOverallScaleDefault when it is not finite), lowered
/// under @ref kArCopyMaxHeightM exactly as @ref ar_copy_scale lowers its factor; the render
/// scale is that factor. The method is always @ref ArCopyMethod::EnlargedControls.
///
/// @param native_height_m the height of the copy at factor 1, frame included
/// @param height_at_overall_m the height at the overall factor (see @ref ar_copy_scale)
/// @param panel_choice true when the size is a choice made for this panel
ArCopyScale ar_copy_controls_scale(
  ArCopySize size, double control_factor, double overall_factor, float native_height_m,
  float height_at_overall_m, bool panel_choice);

/// @brief The render scale a stream keeps once it ran enlarged: the larger of the two. A switch
/// back to native then changes metres only and makes no further QarOS panel.
double sticky_render_scale(double previous_render_scale, double wanted_render_scale);

/// @brief Which streams are scaled: "qar_streaming_panel", "dock:<title>" and "hidden:Displays".
/// Video panels ("video:<topic>") and every other key are not.
bool ar_copy_scales_key(const std::string & key);

/// @brief `enlarged x<f>` (two decimals) or `native`: the words of both log lines. A copy that
/// draws its controls larger says `controls x<f>`, followed by `, copy x<g>` when its metres
/// are larger too, and `native` when neither is larger.
std::string ar_copy_scale_words(const ArCopyScale & scale);

/// @brief `AR PANEL: '<key>' size in AR: enlarged x<f> (<source>)` or
/// `AR PANEL: '<key>' size in AR: native (<source>)`, with the source `setting`,
/// `panel choice` or `limited to 1.8 m tall`.
std::string ar_copy_scale_line(const std::string & key, const ArCopyScale & scale);

/// @brief `AR PANEL: '<key>' switched to <enlarged x<f>|native> in AR: the copy is resized in
/// place`, followed by ` - a sharper copy follows through the resize ring` when
/// @p sharper_copy_follows is set (the render scale rises). For a copy that draws its controls
/// larger the line says `switched to <words> in AR: the AR copy is drawn again with its controls
/// at that size and fitted again; its stream follows through the resize ring` and
/// @p sharper_copy_follows is not used.
std::string ar_copy_switch_line(
  const std::string & key, const ArCopyScale & scale, bool sharper_copy_follows);

/// @brief What the scale of a dialog's panel is worked out from.
struct ArDialogScaleInputs
{
  /// The dialog window is drawn with the enlarged-controls style (it was made below an AR copy).
  bool styled = false;
  /// The factor of that style (read when @ref styled).
  double control_factor = kArCopyScaleDefault;
  /// The size of the panel that opened the dialog: its own choice, else the setting (read when
  /// not @ref styled; a styled dialog is always enlarged, its style says so).
  ArCopySize size = ArCopySize::Enlarged;
  /// The whole-panel factor of the setting (read when not @ref styled).
  double enlarged_factor = kArCopyScaleDefault;
  /// The overall factor of the AR copies (read when @ref styled).
  double overall_factor = kArCopyOverallScaleDefault;
  /// The height of the dialog's panel at factor 1, frame included.
  float native_height_m = 0.0f;
  /// Its height at the factor the rule uses: @ref overall_factor when styled, else
  /// @ref enlarged_factor.
  float height_at_wanted_m = 0.0f;
  /// The size of the opening panel is a choice made for that panel.
  bool panel_choice = false;
};

/// @brief The scale of the panel of a dialog, by the rule that applies to its window.
///
/// A dialog that is drawn with the enlarged-controls style already shows the large controls of
/// its AR copy, so it is streamed at the size of that copy and never enlarged twice:
/// ar_copy_controls_scale() with the overall factor, whatever the setting or the choice of the
/// owner says (@ref ArCopyScale::method is @ref ArCopyMethod::EnlargedControls). Any other dialog
/// is enlarged as a whole, like a panel without a copy of its own: ar_copy_scale() with the size
/// and the factor of the setting (@ref ArCopyMethod::WholeCopy). Both are lowered under
/// @ref kArCopyMaxHeightM.
ArCopyScale ar_dialog_scale(const ArDialogScaleInputs & inputs);

/// @brief The line for the scale that a dialog's panel got. A dialog is streamed by one of two
/// rules, and the line says which applied.
///
/// A dialog that is not drawn with the style of an AR copy is enlarged as a whole, like a panel
/// without a copy of its own: `AR PANEL: dialog '<title>' opened from '<owner>' size in AR:
/// <words> (<source>)`, with the words and the source of @ref ar_copy_scale_line.
///
/// A dialog that is drawn with the style of the controls of an AR copy (its method is
/// @ref ArCopyMethod::EnlargedControls) is streamed at the size of that copy and is given the
/// text of that copy, so it shows the controls and the text the copy shows: `... size in AR: copy
/// x<f>, text x<t> (styled by '<owner>')`, with `, limited to 1.8 m tall` inside the brackets
/// where the height limit lowered the factor. @p text_scale is the factor of its text (0 leaves
/// `text x<t>` out); it is not used by the first rule.
std::string ar_dialog_scale_line(
  const std::string & title, const std::string & owner_key, const ArCopyScale & scale,
  double text_scale = 0.0);

/// @brief The size text of a switch on the AR control page: `controls 2x` for a copy with
/// larger controls, `2x` for an enlarged whole copy (the factor with no decimal when it is
/// whole, else with one or two) and `native` for @ref ArCopySize::Native.
std::string ar_copy_caption(const ArCopyScale & scale, ArCopySize size);

/// @name The texts of the controls
///
/// The panel's Settings tab has a row `kArCopyRowLabel` with a drop-down of two items
/// (@ref ar_copy_enlarged_item_text and `kArCopyNativeItemText`, whose item data are the words of
/// @ref to_string), and the "AR panels" tab has a button that names what a press does.
/// @{
constexpr const char * kArCopyRowLabel = "Qt panels in AR:";
constexpr const char * kArCopyNativeItemText = "Native size";
constexpr const char * kArCopyRowToolTip =
  "How large the controls of the AR copy of a Qt panel are. Enlarged draws the check boxes, "
  "spin-box arrows, sliders, scroll bars and tree arrows of the copy larger while its text "
  "keeps its size; Displays, Views, Tool Properties, Selection and Help get an AR copy of "
  "their own for this, other panels are enlarged as a whole. A panel enlarged as a whole stays "
  "at most 1.8 m tall: it is enlarged less when it would be taller. The panel on the desktop "
  "does not change.";
/// The button when the selected panel is enlarged: a press makes its copy native.
constexpr const char * kArCopyButtonToNativeText = "Native size in AR";
/// The button when the selected panel is native: a press enlarges its copy.
constexpr const char * kArCopyButtonToEnlargedText = "Enlarged in AR";
constexpr const char * kArCopyButtonToolTip =
  "Switches the AR copy of the panel selected above between enlarged controls and native "
  "size. The panel on the desktop does not change. The setting Qt panels in AR on the Settings "
  "tab decides for the panels that have no choice of their own.";
/// `Enlarged controls (2x)`: the factor with no decimal when it is whole, else with one.
std::string ar_copy_enlarged_item_text(double factor);
/// @}

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_COPY_SCALE_HPP_
