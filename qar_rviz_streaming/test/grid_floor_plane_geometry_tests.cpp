// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The floor-companion plane's placement is pure geometry (extent from cell
// count x size, the 1 mm-equivalent push along the grid's own normal for each
// Plane setting, offset pass-through) and is asserted here rather than only
// in the real RViz/Ogre build, per grid_floor_plane_geometry.hpp's derivation
// from GridDisplay::updatePlane()/updateOffset().

#include "qar_rviz_streaming/grid_floor_plane_geometry.hpp"

#include <limits>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::GridFloorPlaneGeometry;
using qar_rviz_streaming::GridPlaneKind;
using qar_rviz_streaming::Vec3d;
using qar_rviz_streaming::compute_grid_floor_plane_geometry;
using qar_rviz_streaming::is_valid_grid_extent;
using qar_rviz_streaming::parse_grid_plane_kind;

constexpr double kEps = 1e-9;

TEST(GridFloorPlaneGeometry, SideIsCellCountTimesCellSize)
{
  const GridFloorPlaneGeometry geom = compute_grid_floor_plane_geometry(
    GridPlaneKind::kXY, 10, 0.5, Vec3d{}, 0.001);
  EXPECT_NEAR(geom.side_metres, 5.0, kEps);
}

TEST(GridFloorPlaneGeometry, SideScalesWithLargerCellCountAndSize)
{
  const GridFloorPlaneGeometry geom = compute_grid_floor_plane_geometry(
    GridPlaneKind::kXY, 40, 0.25, Vec3d{}, 0.001);
  EXPECT_NEAR(geom.side_metres, 10.0, kEps);
}

TEST(GridFloorPlaneGeometry, XyPlanePushesAlongLocalMinusZ)
{
  // The Grid display's own default Plane; the common "floor" case.
  const GridFloorPlaneGeometry geom = compute_grid_floor_plane_geometry(
    GridPlaneKind::kXY, 10, 1.0, Vec3d{1.0, 2.0, 3.0}, 0.001);
  EXPECT_NEAR(geom.local_position.x, 1.0, kEps);
  EXPECT_NEAR(geom.local_position.y, 2.0, kEps);
  EXPECT_NEAR(geom.local_position.z, 3.0 - 0.001, kEps);
}

TEST(GridFloorPlaneGeometry, YzPlanePushesAlongLocalMinusZ)
{
  // updatePlane()'s YZ quaternion also rotates the grid's local-Y normal onto
  // local +Z, so YZ pushes the same way XY does.
  const GridFloorPlaneGeometry geom = compute_grid_floor_plane_geometry(
    GridPlaneKind::kYZ, 10, 1.0, Vec3d{1.0, 2.0, 3.0}, 0.001);
  EXPECT_NEAR(geom.local_position.x, 1.0, kEps);
  EXPECT_NEAR(geom.local_position.y, 2.0, kEps);
  EXPECT_NEAR(geom.local_position.z, 3.0 - 0.001, kEps);
}

TEST(GridFloorPlaneGeometry, XzPlanePushesAlongLocalPlusY)
{
  // Identity orientation: the grid's local-Y normal is untouched, so XZ is
  // the one case that pushes along Y instead of Z.
  const GridFloorPlaneGeometry geom = compute_grid_floor_plane_geometry(
    GridPlaneKind::kXZ, 10, 1.0, Vec3d{1.0, 2.0, 3.0}, 0.001);
  EXPECT_NEAR(geom.local_position.x, 1.0, kEps);
  EXPECT_NEAR(geom.local_position.y, 2.0 + 0.001, kEps);
  EXPECT_NEAR(geom.local_position.z, 3.0, kEps);
}

TEST(GridFloorPlaneGeometry, ZeroOffsetStaysAtOriginPlusPush)
{
  const GridFloorPlaneGeometry geom = compute_grid_floor_plane_geometry(
    GridPlaneKind::kXY, 10, 1.0, Vec3d{}, 0.001);
  EXPECT_NEAR(geom.local_position.x, 0.0, kEps);
  EXPECT_NEAR(geom.local_position.y, 0.0, kEps);
  EXPECT_NEAR(geom.local_position.z, -0.001, kEps);
}

TEST(GridFloorPlaneGeometry, LargerPushMovesFurther)
{
  const GridFloorPlaneGeometry near_push = compute_grid_floor_plane_geometry(
    GridPlaneKind::kXY, 10, 1.0, Vec3d{}, 0.001);
  const GridFloorPlaneGeometry far_push = compute_grid_floor_plane_geometry(
    GridPlaneKind::kXY, 10, 1.0, Vec3d{}, 0.003);
  EXPECT_LT(far_push.local_position.z, near_push.local_position.z);
}

TEST(ParseGridPlaneKind, RecognizesAllThreeOptionStrings)
{
  EXPECT_EQ(parse_grid_plane_kind("XY"), GridPlaneKind::kXY);
  EXPECT_EQ(parse_grid_plane_kind("XZ"), GridPlaneKind::kXZ);
  EXPECT_EQ(parse_grid_plane_kind("YZ"), GridPlaneKind::kYZ);
}

TEST(ParseGridPlaneKind, DefaultsToXyForAnythingElse)
{
  EXPECT_EQ(parse_grid_plane_kind(""), GridPlaneKind::kXY);
  EXPECT_EQ(parse_grid_plane_kind("nonsense"), GridPlaneKind::kXY);
}

TEST(IsValidGridExtent, AcceptsPositiveCountAndSize)
{
  EXPECT_TRUE(is_valid_grid_extent(10, 1.0));
  EXPECT_TRUE(is_valid_grid_extent(1, 0.0001));
}

TEST(IsValidGridExtent, RejectsZeroOrNegativeOrNonFinite)
{
  EXPECT_FALSE(is_valid_grid_extent(0, 1.0));
  EXPECT_FALSE(is_valid_grid_extent(10, 0.0));
  EXPECT_FALSE(is_valid_grid_extent(10, -1.0));
  EXPECT_FALSE(is_valid_grid_extent(10, std::numeric_limits<double>::infinity()));
  EXPECT_FALSE(is_valid_grid_extent(10, std::numeric_limits<double>::quiet_NaN()));
}

}  // namespace
