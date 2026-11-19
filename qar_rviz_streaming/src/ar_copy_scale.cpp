// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_copy_scale.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace qar_rviz_streaming
{

namespace
{

std::string lowered_trimmed(const std::string & text)
{
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
    --end;
  }
  std::string word = text.substr(begin, end - begin);
  std::transform(
    word.begin(), word.end(), word.begin(),
    [](unsigned char c) {return static_cast<char>(std::tolower(c));});
  return word;
}

/// The factor with two decimals, whatever the C locale says.
std::string two_decimals(double value)
{
  const long long hundredths = std::llround(value * 100.0);
  char text[48];
  std::snprintf(
    text, sizeof(text), "%lld.%02lld", hundredths / 100, std::llabs(hundredths % 100));
  return text;
}

/// A factor as the desktop prints it: no decimal when whole, one when a whole tenth, else two.
std::string factor_text(double factor)
{
  const long long hundredths = std::llround(factor * 100.0);
  const long long whole = hundredths / 100;
  const long long fraction = std::llabs(hundredths % 100);
  char text[48];
  if (fraction == 0) {
    std::snprintf(text, sizeof(text), "%lld", whole);
  } else if (fraction % 10 == 0) {
    std::snprintf(text, sizeof(text), "%lld.%lld", whole, fraction / 10);
  } else {
    std::snprintf(text, sizeof(text), "%lld.%02lld", whole, fraction);
  }
  return text;
}

const char * source_words(const ArCopyScale & scale)
{
  switch (scale.source) {
    case ArCopyScaleSource::Setting:
      return "setting";
    case ArCopyScaleSource::PanelChoice:
      return "panel choice";
    case ArCopyScaleSource::LimitedByHeight:
      return "limited to 1.8 m tall";
  }
  return "setting";
}

bool is_native(const ArCopyScale & scale)
{
  return scale.source != ArCopyScaleSource::LimitedByHeight && !(scale.factor > 1.0);
}

}  // namespace

const char * to_string(ArCopySize size)
{
  return size == ArCopySize::Native ? "native" : "enlarged";
}

bool ar_copy_size_from_word(const std::string & word, ArCopySize & out)
{
  const std::string text = lowered_trimmed(word);
  if (text == "enlarged") {
    out = ArCopySize::Enlarged;
    return true;
  }
  if (text == "native") {
    out = ArCopySize::Native;
    return true;
  }
  return false;
}

double ar_copy_text_scale(const char * env_text)
{
  if (env_text == nullptr) {
    return kArCopyTextScale;
  }
  const std::string text = lowered_trimmed(env_text);
  if (text.empty()) {
    return kArCopyTextScale;
  }
  char * end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
    return kArCopyTextScale;
  }
  return std::max(kArCopyTextScaleMin, std::min(kArCopyTextScaleMax, value));
}

ArCopyHinting ar_copy_font_hinting(const char * env_text)
{
  return env_text != nullptr && lowered_trimmed(env_text) == "default" ? ArCopyHinting::Default :
         ArCopyHinting::None;
}

std::string ar_copy_text_line(const std::string & key, double scale, ArCopyHinting hinting)
{
  return "AR PANEL: '" + key + "' copy text x" + two_decimals(scale) + ", hinting " +
         (hinting == ArCopyHinting::Default ? "default" : "none");
}

ArCopyScale ar_copy_scale(
  ArCopySize size, double enlarged_factor, float native_height_m, float height_at_wanted_m,
  bool panel_choice)
{
  ArCopyScale scale;
  scale.source = panel_choice ? ArCopyScaleSource::PanelChoice : ArCopyScaleSource::Setting;
  if (size == ArCopySize::Native) {
    return scale;
  }
  double wanted = enlarged_factor;
  if (!std::isfinite(wanted) || wanted < kArCopyScaleMin || wanted > kArCopyScaleMax) {
    wanted = kArCopyScaleDefault;
  }
  double factor = wanted;
  const double native_h = static_cast<double>(native_height_m);
  const double cap = static_cast<double>(kArCopyMaxHeightM);
  if (std::isfinite(native_h) && native_h > 0.0 && wanted > 1.0) {
    // The height is a straight line of the factor through (1, native) and (wanted, at_wanted).
    double at_wanted = static_cast<double>(height_at_wanted_m);
    if (!std::isfinite(at_wanted) || !(at_wanted > native_h)) {
      at_wanted = native_h * wanted;
    }
    if (at_wanted > cap) {
      const double room = cap - native_h;
      factor = room > 0.0 ? 1.0 + room / (at_wanted - native_h) * (wanted - 1.0) : 1.0;
      factor = std::max(1.0, std::min(wanted, factor));
      scale.source = ArCopyScaleSource::LimitedByHeight;
    }
  }
  // Hundredths, rounded down: the factor is logged with two decimals, and a rounding that
  // keeps the copy under the limit is always downward.
  factor = std::floor(factor * 100.0 + 1e-9) / 100.0;
  scale.factor = std::max(1.0, factor);
  scale.render_scale = scale.factor;
  return scale;
}

ArCopyScale ar_copy_scale(
  ArCopySize size, double enlarged_factor, float native_height_m, bool panel_choice)
{
  return ar_copy_scale(size, enlarged_factor, native_height_m, 0.0f, panel_choice);
}

ArCopyScale ar_copy_controls_scale(
  ArCopySize size, double control_factor, double overall_factor, float native_height_m,
  float height_at_overall_m, bool panel_choice)
{
  ArCopyScale scale;
  scale.method = ArCopyMethod::EnlargedControls;
  scale.source = panel_choice ? ArCopyScaleSource::PanelChoice : ArCopyScaleSource::Setting;
  if (size == ArCopySize::Native) {
    return scale;
  }
  double control = control_factor;
  if (!std::isfinite(control)) {
    control = kArCopyScaleDefault;
  }
  control = std::max(kArCopyScaleMin, std::min(kArCopyScaleMax, control));
  double overall = overall_factor;
  if (!std::isfinite(overall)) {
    overall = kArCopyOverallScaleDefault;
  }
  overall = std::max(kArCopyOverallScaleMin, std::min(kArCopyOverallScaleMax, overall));
  // The metres and the render scale follow the overall factor under the same height limit as a
  // whole copy.
  const ArCopyScale metres = ar_copy_scale(
    ArCopySize::Enlarged, overall, native_height_m, height_at_overall_m, panel_choice);
  scale.factor = metres.factor;
  scale.render_scale = metres.render_scale;
  scale.source = metres.source;
  scale.control_factor = control;
  return scale;
}

double sticky_render_scale(double previous_render_scale, double wanted_render_scale)
{
  const double previous = std::isfinite(previous_render_scale) ? previous_render_scale : 1.0;
  const double wanted = std::isfinite(wanted_render_scale) ? wanted_render_scale : 1.0;
  return std::max(1.0, std::max(previous, wanted));
}

bool ar_copy_scales_key(const std::string & key)
{
  return key == "qar_streaming_panel" || key == "hidden:Displays" ||
         (key.size() > 5 && key.compare(0, 5, "dock:") == 0);
}

std::string ar_copy_enlarged_item_text(double factor)
{
  return "Enlarged controls (" + factor_text(factor) + "x)";
}

std::string ar_copy_scale_words(const ArCopyScale & scale)
{
  if (scale.method == ArCopyMethod::EnlargedControls) {
    std::string words;
    if (scale.control_factor > 1.0) {
      words = "controls x" + two_decimals(scale.control_factor);
    }
    if (scale.factor > 1.0) {
      words += (words.empty() ? "copy x" : ", copy x") + two_decimals(scale.factor);
    }
    return words.empty() ? std::string("native") : words;
  }
  return is_native(scale) ? std::string("native") : "enlarged x" + two_decimals(scale.factor);
}

std::string ar_copy_scale_line(const std::string & key, const ArCopyScale & scale)
{
  return "AR PANEL: '" + key + "' size in AR: " + ar_copy_scale_words(scale) + " (" +
         source_words(scale) + ")";
}

std::string ar_copy_switch_line(
  const std::string & key, const ArCopyScale & scale, bool sharper_copy_follows)
{
  if (scale.method == ArCopyMethod::EnlargedControls) {
    return "AR PANEL: '" + key + "' switched to " + ar_copy_scale_words(scale) +
           " in AR: the AR copy is drawn again with its controls at that size and fitted "
           "again; its stream follows through the resize ring";
  }
  return "AR PANEL: '" + key + "' switched to " + ar_copy_scale_words(scale) +
         " in AR: the copy is resized in place" +
         (sharper_copy_follows ? " - a sharper copy follows through the resize ring" : "");
}

ArCopyScale ar_dialog_scale(const ArDialogScaleInputs & inputs)
{
  if (inputs.styled) {
    return ar_copy_controls_scale(
      ArCopySize::Enlarged, inputs.control_factor, inputs.overall_factor, inputs.native_height_m,
      inputs.height_at_wanted_m, inputs.panel_choice);
  }
  return ar_copy_scale(
    inputs.size, inputs.enlarged_factor, inputs.native_height_m, inputs.height_at_wanted_m,
    inputs.panel_choice);
}

std::string ar_dialog_scale_line(
  const std::string & title, const std::string & owner_key, const ArCopyScale & scale,
  double text_scale)
{
  const std::string head =
    "AR PANEL: dialog '" + title + "' opened from '" + owner_key + "' size in AR: ";
  if (scale.method != ArCopyMethod::EnlargedControls) {
    return head + ar_copy_scale_words(scale) + " (" + source_words(scale) + ")";
  }
  // A dialog of an AR copy: the size and the text of that copy, whatever the setting says.
  std::string words = "copy x" + two_decimals(scale.factor);
  if (text_scale > 0.0) {
    words += ", text x" + two_decimals(text_scale);
  }
  const std::string limit =
    scale.source == ArCopyScaleSource::LimitedByHeight ? ", limited to 1.8 m tall" : "";
  return head + words + " (styled by '" + owner_key + "'" + limit + ")";
}

std::string ar_copy_caption(const ArCopyScale & scale, ArCopySize size)
{
  if (size == ArCopySize::Native) {
    return to_string(ArCopySize::Native);
  }
  if (scale.method == ArCopyMethod::EnlargedControls && scale.control_factor > 1.0) {
    return "controls " + factor_text(scale.control_factor) + "x";
  }
  return factor_text(scale.factor) + "x";
}

}  // namespace qar_rviz_streaming
