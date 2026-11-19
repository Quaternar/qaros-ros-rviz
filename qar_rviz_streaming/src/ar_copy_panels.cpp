// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_copy_panels.hpp"

#include <algorithm>
#include <cmath>

namespace qar_rviz_streaming
{

namespace
{

constexpr double kMaxControlFactor = 4.0;
/// The growth of a copy's width for every step of the control factor above 1.
constexpr double kWidthPerFactorStep = 0.25;

}  // namespace

const std::vector<ArCopyPanelClass> & ar_copy_panel_classes()
{
  // The Selection panel is declared without Q_OBJECT in RViz, so Qt names it by its base class.
  static const std::vector<ArCopyPanelClass> classes = {
    {"rviz_common/Displays", "rviz_common::DisplaysPanel", true, true, 0},
    {"rviz_common/Views", "rviz_common::ViewsPanel", false, true, 0},
    {"rviz_common/Tool Properties", "rviz_common::ToolPropertiesPanel", false, true, 0},
    {"rviz_common/Selection", "rviz_common::Panel", false, true, 0},
    // A text browser and nothing else: no tree, and a dock that is small for reading.
    {"rviz_common/Help", "rviz_common::HelpPanel", false, false, kArCopyTextPanelMinHeightPx}};
  return classes;
}

const ArCopyPanelClass * ar_copy_panel_class(const std::string & class_id)
{
  for (const ArCopyPanelClass & cls : ar_copy_panel_classes()) {
    if (class_id == cls.class_id) {
      return &cls;
    }
  }
  return nullptr;
}

std::string ar_copy_window_title(const ArCopyPanelClass & cls)
{
  const std::string id = cls.class_id;
  const std::size_t slash = id.rfind('/');
  const std::string name = slash == std::string::npos ? id : id.substr(slash + 1);
  return name + " (AR copy)";
}

int ar_copy_width_px(int base_width, double control_factor)
{
  double factor = std::isfinite(control_factor) ? control_factor : 1.0;
  factor = std::max(1.0, std::min(kMaxControlFactor, factor));
  const int least = std::max(base_width, kArCopyMinWidthPx);
  const double grown = static_cast<double>(least) * (1.0 + kWidthPerFactorStep * (factor - 1.0));
  return std::max(base_width, static_cast<int>(std::lround(grown)));
}

int ar_copy_start_height_px(const ArCopyPanelClass & cls, int dock_height, int least, int most)
{
  const int upper = std::max(least, most);
  return std::clamp(std::max(dock_height, cls.min_height_px), least, upper);
}

}  // namespace qar_rviz_streaming
