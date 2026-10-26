// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/panel_ground_rule.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <string>

namespace qar_rviz_streaming
{
namespace
{

bool finite(const QarVector3 & v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

/// The y the top edge has to reach, or @p top_y itself when it is high enough already (also for
/// any number that is not finite, and for a line no float can hold). Never below @p top_y.
float raised_top_y(float top_y, float height_m, float ground_room_y, float margin_m)
{
  if (!std::isfinite(top_y) || !std::isfinite(height_m) || !std::isfinite(ground_room_y) ||
    !std::isfinite(margin_m))
  {
    return top_y;
  }
  const double height = std::max(0.0, static_cast<double>(height_m));
  const double line = static_cast<double>(ground_room_y) + static_cast<double>(margin_m);
  if (static_cast<double>(top_y) - height >= line ||
    line + height >= static_cast<double>(std::numeric_limits<float>::max()))
  {
    return top_y;
  }
  float raised = static_cast<float>(line + height);
  // Rounding to float can leave the lower edge a hair under the line: step up until it is not.
  while (static_cast<double>(raised) - height < line) {
    raised = std::nextafter(raised, std::numeric_limits<float>::infinity());
  }
  return std::max(raised, top_y);
}

}  // namespace

float scene_ground_room_y(
  const qar_ar_core::ScenePlacement & placement, float room_x, float room_z,
  float fallback_room_y)
{
  if (!placement.known || !std::isfinite(room_x) || !std::isfinite(room_z)) {
    return fallback_room_y;
  }
  // The plane through the app origin whose normal is the scene's up axis, both in room space.
  const QarVector3 origin =
    qar_ar_core::scene_point_to_room(placement, QarVector3{0.0f, 0.0f, 0.0f});
  const QarVector3 up =
    qar_ar_core::scene_direction_to_room(placement, QarVector3{0.0f, 1.0f, 0.0f});
  if (!finite(origin) || !finite(up)) {
    return fallback_room_y;
  }
  const double length = std::sqrt(
    static_cast<double>(up.x) * up.x + static_cast<double>(up.y) * up.y +
    static_cast<double>(up.z) * up.z);
  if (!(length > 1e-6) || std::fabs(static_cast<double>(up.y)) / length < kGroundMinNormalY) {
    return fallback_room_y;
  }
  // up . (p - origin) = 0, solved for p.y at the given x and z.
  const double dx = static_cast<double>(room_x) - origin.x;
  const double dz = static_cast<double>(room_z) - origin.z;
  const double y = origin.y - (up.x * dx + up.z * dz) / up.y;
  if (!std::isfinite(y) || std::fabs(y) >= static_cast<double>(std::numeric_limits<float>::max())) {
    return fallback_room_y;
  }
  return static_cast<float>(y);
}

QarPose keep_above_ground(
  const QarPose & top_centre, float height_m, float ground_room_y, float margin_m)
{
  QarPose pose = top_centre;
  pose.position.y = raised_top_y(top_centre.position.y, height_m, ground_room_y, margin_m);
  return pose;
}

float ground_raise_m(
  const QarPose & top_centre, float height_m, float ground_room_y, float margin_m)
{
  const float raised = raised_top_y(top_centre.position.y, height_m, ground_room_y, margin_m);
  return raised > top_centre.position.y ? raised - top_centre.position.y : 0.0f;
}

const char * to_string(FixedPoseMode mode)
{
  return mode == FixedPoseMode::Ground ? "ground" : "room";
}

FixedPoseChoice parse_fixed_pose_mode(const char * value, const char * source)
{
  FixedPoseChoice choice;
  if (value == nullptr) {
    return choice;
  }
  std::string text(value);
  const char * const blanks = " \t\r\n";
  const std::size_t first = text.find_first_not_of(blanks);
  if (first == std::string::npos) {
    return choice;  // set to nothing is the same as not set
  }
  const std::size_t last = text.find_last_not_of(blanks);
  text = text.substr(first, last - first + 1);
  std::string lower;
  for (const char c : text) {
    lower.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
  }
  if (lower == "room") {
    return choice;
  }
  if (lower == "ground") {
    choice.mode = FixedPoseMode::Ground;
    return choice;
  }
  // Only printable ASCII goes into the sentence: it ends up in a log that is read line by line.
  std::string shown;
  for (const char c : text) {
    shown.push_back((c >= 0x20 && c <= 0x7e) ? c : '?');
  }
  choice.warning = std::string(source != nullptr ? source : "QAR_PANEL_FIXED_POSES") + "='" +
    shown + "' is not room or ground; using room.";
  return choice;
}

const char * to_string(ArFrameSource source)
{
  switch (source) {
    case ArFrameSource::Frame:
      return "QAR_PANEL_AR_FRAME";
    case ArFrameSource::Headroom:
      return "QAR_PANEL_AR_HEADROOM (the top inset only)";
    case ArFrameSource::Default:
      break;
  }
  return "default";
}

namespace
{

/// @p value without the blanks around it; empty when it is nothing else.
std::string trimmed(const std::string & value)
{
  const char * const blanks = " \t\r\n";
  const std::size_t first = value.find_first_not_of(blanks);
  if (first == std::string::npos) {
    return std::string();
  }
  return value.substr(first, value.find_last_not_of(blanks) - first + 1);
}

/// Only printable ASCII goes into a sentence: it ends up in a log that is read line by line.
std::string printable(const std::string & text)
{
  std::string shown;
  for (const char c : text) {
    shown.push_back((c >= 0x20 && c <= 0x7e) ? c : '?');
  }
  return shown;
}

/// @p metres with three decimals, a point as the separator whatever the system language is.
std::string metres_text(double metres)
{
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3) << metres;
  return out.str();
}

/// One decimal number of metres from 0 to @p largest_m in @p token, blanks around it allowed.
bool parse_metres(const std::string & token, double largest_m, double & metres)
{
  const std::string text = trimmed(token);
  if (text.empty()) {
    return false;
  }
  // The C locale, always: the separator is a point whatever the system language is.
  std::istringstream in(text);
  in.imbue(std::locale::classic());
  double value = 0.0;
  in >> value;
  if (in.fail() || !in.eof() || !std::isfinite(value) || value < 0.0 || value > largest_m) {
    return false;
  }
  metres = value + 0.0;  // "-0" is 0
  return true;
}

}  // namespace

ArFrameChoice parse_ar_frame(
  const char * frame_text, const char * headroom_text, const ArFrameInsets & defaults)
{
  ArFrameChoice choice;
  choice.insets = defaults;
  const std::string frame = frame_text != nullptr ? trimmed(frame_text) : std::string();
  const std::string headroom = headroom_text != nullptr ? trimmed(headroom_text) : std::string();
  const auto limit_text = [](double largest_m) {
      std::ostringstream out;
      out.imbue(std::locale::classic());
      out << largest_m;
      return out.str();
    };
  const std::string limit = limit_text(kMaxArFrameInsetM);

  if (!frame.empty()) {
    choice.headroom_ignored = !headroom.empty();
    // The four insets, and optionally the fit margin after them.
    double values[5] = {0.0, 0.0, 0.0, 0.0, defaults.fit_margin_m};
    std::size_t count = 0;
    bool acceptable = true;
    std::size_t start = 0;
    while (acceptable) {
      const std::size_t comma = frame.find(',', start);
      const std::string token =
        frame.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
      acceptable = count < 5 &&
        parse_metres(token, count < 4 ? kMaxArFrameInsetM : kMaxArFitMarginM, values[count]);
      ++count;
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }
    if (acceptable && (count == 4 || count == 5)) {
      choice.insets = ArFrameInsets{values[0], values[1], values[2], values[3], values[4]};
      choice.source = ArFrameSource::Frame;
      return choice;
    }
    choice.warning = "QAR_PANEL_AR_FRAME='" + printable(frame) +
      "' is not four numbers of metres from 0 to " + limit + " (top,bottom,left,right) and " +
      "optionally a fit margin from 0 to " + limit_text(kMaxArFitMarginM) + "; using top " +
      metres_text(defaults.top_m) + ", bottom " + metres_text(defaults.bottom_m) + ", left " +
      metres_text(defaults.left_m) + ", right " + metres_text(defaults.right_m) +
      ", fit margin " + metres_text(defaults.fit_margin_m) + " m.";
    return choice;
  }
  if (!headroom.empty()) {
    double top = 0.0;
    if (parse_metres(headroom, kMaxArFrameInsetM, top)) {
      choice.insets.top_m = top;
      choice.source = ArFrameSource::Headroom;
      return choice;
    }
    choice.warning = "QAR_PANEL_AR_HEADROOM='" + printable(headroom) +
      "' is not a number of metres from 0 to " + limit + "; using the top inset " +
      metres_text(defaults.top_m) + " m.";
  }
  return choice;
}

AppVolumeSize choose_app_volume_size(
  std::optional<float> stated_edge_m, std::optional<float> stated_scale, float default_edge_m)
{
  const auto usable = [](const std::optional<float> & value) {
      return value.has_value() && std::isfinite(*value) && *value > 0.0f;
    };
  AppVolumeSize size;
  size.edge_m = default_edge_m;
  if (usable(stated_edge_m)) {
    size.edge_m = *stated_edge_m;
    size.source = AppVolumeSizeSource::Stated;
  } else if (usable(stated_scale)) {
    size.edge_m = default_edge_m * *stated_scale;
    size.source = AppVolumeSizeSource::FromScale;
  }
  return size;
}

std::string render_device_line(const char * render_system, const char * device)
{
  // Only printable ASCII goes into the line: it ends up in a log that is read line by line.
  const auto printable = [](const char * text) {
      std::string shown;
      for (const char * c = text; c != nullptr && *c != '\0'; ++c) {
        shown.push_back((*c >= 0x20 && *c <= 0x7e) ? *c : '?');
      }
      return shown;
    };
  const std::string system = printable(render_system);
  if (system.empty()) {
    return "RENDER: no render system yet";
  }
  const std::string name = printable(device);
  return "RENDER: " + system + ", device '" + (name.empty() ? std::string("<unknown>") : name) +
         "'";
}

float fixed_panel_top_y(
  FixedPoseMode mode, const qar_ar_core::ScenePlacement & placement, float room_x, float room_z,
  float fallback_ground_y)
{
  if (mode != FixedPoseMode::Ground) {
    return kFixedPanelTopM;
  }
  return scene_ground_room_y(placement, room_x, room_z, fallback_ground_y) + kFixedPanelTopM;
}

std::string brightness_field(const qar_ar_core::PanelStreamDiagnostics & panel)
{
  const auto measured = [](double value) {return std::isfinite(value) && value >= 0.0;};
  const auto whole = [](double value) {
      return std::to_string(static_cast<long long>(std::floor(value + 0.5)));
    };
  if (!measured(panel.luminance_all) || !measured(panel.luminance_top) ||
    !measured(panel.luminance_rest))
  {
    return " lum=-";
  }
  std::string field = " lum=" + whole(panel.luminance_all) + "/" + whole(panel.luminance_top) +
    "/" + whole(panel.luminance_rest);
  if (measured(panel.alpha_mean)) {
    field += " a=" + whole(panel.alpha_mean);
  }
  return field;
}

}  // namespace qar_rviz_streaming
