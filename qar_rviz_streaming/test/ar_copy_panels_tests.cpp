// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Which RViz panels get an AR copy of their own, what their classes are called and how wide a
// copy is. Standard library only.

#include "qar_rviz_streaming/ar_copy_panels.hpp"

#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::ar_copy_panel_class;
using qar_rviz_streaming::ar_copy_panel_classes;
using qar_rviz_streaming::ar_copy_start_height_px;
using qar_rviz_streaming::ar_copy_width_px;

}  // namespace

TEST(ArCopyPanels, TheFourSharedModelPanelsAndHelpHaveACopy)
{
  const auto & classes = ar_copy_panel_classes();
  ASSERT_EQ(classes.size(), 5u);
  EXPECT_STREQ(classes[0].class_id, "rviz_common/Displays");
  EXPECT_STREQ(classes[1].class_id, "rviz_common/Views");
  EXPECT_STREQ(classes[2].class_id, "rviz_common/Tool Properties");
  EXPECT_STREQ(classes[3].class_id, "rviz_common/Selection");
  EXPECT_STREQ(classes[4].class_id, "rviz_common/Help");
  EXPECT_STREQ(classes[0].qt_class_name, "rviz_common::DisplaysPanel");
  EXPECT_STREQ(classes[1].qt_class_name, "rviz_common::ViewsPanel");
  EXPECT_STREQ(classes[2].qt_class_name, "rviz_common::ToolPropertiesPanel");
  EXPECT_STREQ(classes[4].qt_class_name, "rviz_common::HelpPanel");
  for (const auto & cls : classes) {
    const auto * found = ar_copy_panel_class(cls.class_id);
    ASSERT_NE(found, nullptr) << cls.class_id;
    EXPECT_EQ(found, &cls) << cls.class_id;
  }
  // Class ids are matched whole: no prefix, no other case.
  EXPECT_EQ(ar_copy_panel_class("rviz_common/Display"), nullptr);
  EXPECT_EQ(ar_copy_panel_class("rviz_common/displays"), nullptr);
  EXPECT_EQ(ar_copy_panel_class(""), nullptr);
}

TEST(ArCopyPanels, TimeTransformationHelpAndPluginClassesHaveNone)
{
  for (const char * id :
    {"rviz_common/Time", "rviz_common/Transformation", "rviz_common/Help ",
      "rviz_common/help", "rviz_common/Displays ", "qar_rviz_streaming/QarStreamingPanel",
      "my_plugin/Panel"})
  {
    EXPECT_EQ(ar_copy_panel_class(id), nullptr) << id;
  }
}

TEST(ArCopyPanels, OnlyDisplaysHidesItsHelpArea)
{
  for (const auto & cls : ar_copy_panel_classes()) {
    EXPECT_EQ(cls.hide_help_area, std::string(cls.class_id) == "rviz_common/Displays")
      << cls.class_id;
  }
}

TEST(ArCopyPanels, OnlyHelpHasNoPropertyTreeAndAHeightOfItsOwn)
{
  for (const auto & cls : ar_copy_panel_classes()) {
    const bool help = std::string(cls.class_id) == "rviz_common/Help";
    EXPECT_EQ(cls.has_property_tree, !help) << cls.class_id;
    EXPECT_EQ(cls.min_height_px, help ? qar_rviz_streaming::kArCopyTextPanelMinHeightPx : 0)
      << cls.class_id;
  }
}

TEST(ArCopyPanels, ACopyStartsAtTheDockHeightOrTheLeastOfItsClassWithinTheFitRange)
{
  const auto * displays = ar_copy_panel_class("rviz_common/Displays");
  const auto * help = ar_copy_panel_class("rviz_common/Help");
  ASSERT_NE(displays, nullptr);
  ASSERT_NE(help, nullptr);
  // A class without a least height takes the dock's, within the range.
  EXPECT_EQ(ar_copy_start_height_px(*displays, 700, 400, 1400), 700);
  EXPECT_EQ(ar_copy_start_height_px(*displays, 100, 400, 1400), 400);
  EXPECT_EQ(ar_copy_start_height_px(*displays, 5000, 400, 1400), 1400);
  // Help is read in a copy of its own: a small dock does not make a small copy.
  EXPECT_EQ(ar_copy_start_height_px(*help, 200, 400, 1400), 640);
  EXPECT_EQ(ar_copy_start_height_px(*help, 900, 400, 1400), 900);
  EXPECT_EQ(ar_copy_start_height_px(*help, 3000, 400, 1400), 1400);
  // A range the wrong way round counts as its least for both ends.
  EXPECT_EQ(ar_copy_start_height_px(*help, 900, 800, 500), 800);
}

TEST(ArCopyPanels, SelectionIsNamedByThePanelBaseClass)
{
  // The Selection panel is declared without Q_OBJECT, so Qt names it by its base class.
  const auto * selection = ar_copy_panel_class("rviz_common/Selection");
  ASSERT_NE(selection, nullptr);
  EXPECT_STREQ(selection->qt_class_name, "rviz_common::Panel");
  // No other class is named by the base class: a made object of that name would pass for it.
  for (const auto & cls : ar_copy_panel_classes()) {
    if (&cls != selection) {
      EXPECT_STRNE(cls.qt_class_name, "rviz_common::Panel") << cls.class_id;
    }
  }
}

TEST(ArCopyPanels, TheWindowTitleNamesThePanel)
{
  const auto & classes = ar_copy_panel_classes();
  EXPECT_EQ(qar_rviz_streaming::ar_copy_window_title(classes[0]), "Displays (AR copy)");
  EXPECT_EQ(qar_rviz_streaming::ar_copy_window_title(classes[1]), "Views (AR copy)");
  EXPECT_EQ(qar_rviz_streaming::ar_copy_window_title(classes[2]), "Tool Properties (AR copy)");
  EXPECT_EQ(qar_rviz_streaming::ar_copy_window_title(classes[3]), "Selection (AR copy)");
  EXPECT_EQ(qar_rviz_streaming::ar_copy_window_title(classes[4]), "Help (AR copy)");
}

TEST(ArCopyPanels, TheWidthGrowsAQuarterPerFactorStep)
{
  EXPECT_EQ(ar_copy_width_px(480, 2.0), 600);
  EXPECT_EQ(ar_copy_width_px(300, 2.0), 600);
  EXPECT_EQ(ar_copy_width_px(700, 1.0), 700);
  EXPECT_EQ(ar_copy_width_px(480, 1.5), 540);
  EXPECT_EQ(ar_copy_width_px(300, 1.0), 480);
  EXPECT_EQ(ar_copy_width_px(700, 2.0), 875);
  EXPECT_EQ(qar_rviz_streaming::kArCopyMinWidthPx, 480);
}

TEST(ArCopyPanels, TheWidthIsNeverBelowTheBaseAndTheFactorIsBounded)
{
  // A factor that is not a number or below 1 counts as 1, one above 4 as 4.
  EXPECT_EQ(ar_copy_width_px(480, 0.5), 480);
  EXPECT_EQ(ar_copy_width_px(480, -3.0), 480);
  EXPECT_EQ(ar_copy_width_px(480, std::numeric_limits<double>::quiet_NaN()), 480);
  EXPECT_EQ(ar_copy_width_px(480, std::numeric_limits<double>::infinity()), 480);
  EXPECT_EQ(ar_copy_width_px(480, 4.0), 840);
  EXPECT_EQ(ar_copy_width_px(480, 40.0), 840);
  for (const int base : {0, 100, 479, 480, 481, 1200}) {
    for (const double factor : {1.0, 1.25, 2.0, 3.0, 4.0}) {
      EXPECT_GE(ar_copy_width_px(base, factor), base) << base << " " << factor;
    }
  }
  EXPECT_GE(ar_copy_width_px(-5, 2.0), 600);
}
