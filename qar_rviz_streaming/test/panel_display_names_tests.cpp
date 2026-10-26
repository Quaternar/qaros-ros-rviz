// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The display name a new QarOS panel is created under (panel_display_names.hpp): the first
// start under a name keeps it, every later start under the same name gets the next number,
// and each name that a caller asks for has a count of its own. Pure standard-library logic:
// no QarOS, no Qt, no ROS.

#include <gtest/gtest.h>

#include "qar_rviz_streaming/panel_display_names.hpp"

#include <string>

using qar_rviz_streaming::PanelDisplayNames;
using qar_rviz_streaming::numbered_display_name;

TEST(PanelDisplayNames, TheFirstStartUnderANameKeepsTheName)
{
  PanelDisplayNames names;
  EXPECT_EQ(names.started("Displays (AR)"), 0);

  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR)");
  EXPECT_EQ(names.started("Displays (AR)"), 1);
}

TEST(PanelDisplayNames, EveryLaterStartUnderTheSameNameGetsTheNextNumber)
{
  PanelDisplayNames names;
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR)");
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (2)");
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (3)");
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (4)");
  EXPECT_EQ(names.started("Displays (AR)"), 4);

  // The pure function behind it: the number is the count of earlier starts plus one, and a
  // name with no earlier start (or a nonsensical negative count) is passed on as it is.
  EXPECT_EQ(numbered_display_name("Displays (AR)", 0), "Displays (AR)");
  EXPECT_EQ(numbered_display_name("Displays (AR)", 1), "Displays (AR) (2)");
  EXPECT_EQ(numbered_display_name("Displays (AR)", 2), "Displays (AR) (3)");
  EXPECT_EQ(numbered_display_name("Displays (AR)", 9), "Displays (AR) (10)");
  EXPECT_EQ(numbered_display_name("Displays (AR)", -1), "Displays (AR)");
  EXPECT_EQ(numbered_display_name("Displays (AR)", -7), "Displays (AR)");
}

TEST(PanelDisplayNames, EachNameHasItsOwnCounter)
{
  PanelDisplayNames names;
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR)");
  EXPECT_EQ(names.claim("/camera/image"), "/camera/image");
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (2)");
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (3)");
  EXPECT_EQ(names.claim("/camera/image"), "/camera/image (2)");
  EXPECT_EQ(names.claim("/camera/image"), "/camera/image (3)");
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (4)");

  EXPECT_EQ(names.started("Displays (AR)"), 4);
  EXPECT_EQ(names.started("/camera/image"), 3);
  EXPECT_EQ(names.started("/camera/depth"), 0);
}

TEST(PanelDisplayNames, ANameThatAlreadyEndsInANumberIsAnotherName)
{
  PanelDisplayNames names;
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR)");
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (2)");

  // Asking for "Displays (AR) (2)" itself is the first start under THAT name, so it keeps
  // it, and the next start under it gets the number after it on top of what was asked for.
  EXPECT_EQ(names.claim("Displays (AR) (2)"), "Displays (AR) (2)");
  EXPECT_EQ(names.claim("Displays (AR) (2)"), "Displays (AR) (2) (2)");
  EXPECT_EQ(names.started("Displays (AR) (2)"), 2);

  // The counter of "Displays (AR)" did not move: its next start is the third.
  EXPECT_EQ(names.started("Displays (AR)"), 2);
  EXPECT_EQ(names.claim("Displays (AR)"), "Displays (AR) (3)");
}

TEST(PanelDisplayNames, AnEmptyNameStaysEmpty)
{
  PanelDisplayNames names;
  EXPECT_EQ(names.claim(""), "");
  EXPECT_EQ(names.claim(""), "");
  EXPECT_EQ(names.claim(""), "");
  EXPECT_EQ(numbered_display_name("", 5), "");
}
