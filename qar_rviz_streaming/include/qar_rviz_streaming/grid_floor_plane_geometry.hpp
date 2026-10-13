// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__GRID_FLOOR_PLANE_GEOMETRY_HPP_
#define QAR_RVIZ_STREAMING__GRID_FLOOR_PLANE_GEOMETRY_HPP_

/// @file grid_floor_plane_geometry.hpp
/// @brief Pure placement math for the floor-companion plane that mirrors an
/// rviz_default_plugins Grid display. No Ogre/RViz types — see
/// `grid_floor_plane.hpp` for the class that feeds this from the live scene.
///
/// ## Why this needs its own derivation, not just "trust the node parenting"
///
/// The Grid display (`rviz_default_plugins::displays::GridDisplay`) keeps the
/// Plane rotation and the Offset translation on `grid_->getSceneNode()` —
/// rviz_rendering::Grid's OWN scene node, which is private and has no public
/// accessor. The ONLY node GridDisplay exposes publicly is
/// `rviz_common::Display::getSceneNode()`, which carries just the Reference
/// Frame's live pose (Display's generic frame-follow), a sibling of, not the
/// same node as, the Grid's plane/offset node.
///
/// So a companion box parented under the public node inherits the reference
/// frame for free, but Plane and Offset have to be replicated by hand. The
/// values here are copied verbatim from
/// `rviz_default_plugins::displays::GridDisplay::updateOffset()` and
/// `::updatePlane()` (grid_display.cpp in rviz_default_plugins,
/// https://github.com/ros2/rviz/tree/humble), so this stays numerically
/// identical to what the Grid display itself does — it does not re-derive the
/// rotation from first principles.
///
/// `rviz_rendering::Grid` always authors its raw line vertices with X and Z
/// varying and Y constant (`Grid::createGridPlane()`; the class's own doc
/// comment: "a grid with an identity orientation is drawn along the XZ
/// plane") — i.e. the grid's normal, before any Plane rotation, is local Y.
/// `updatePlane()`'s three quaternions rotate that local Y onto:
///   * Plane XZ (identity):  local Y stays local Y.
///   * Plane XY (the Grid display's own default) and Plane YZ: local Y maps
///     onto the Display node's local +Z.
/// A companion box built the same way (X/Z span, Y-thin) and given the SAME
/// rotation ends up coplanar with the grid lines in every Plane setting, no
/// matter which of the two is applied.
///
/// "Below" only has a physical meaning for a Z-up fixed frame (ROS
/// convention), so for the two Plane settings whose normal maps to local Z we
/// push along local -Z (down); the one whose normal stays local Y (Plane XZ,
/// a wall-like grid with no gravity-relative "below") pushes along local +Y —
/// an arbitrary but consistent tie-break, not a physical claim.

#include <cstdint>
#include <string>

namespace qar_rviz_streaming
{

/// @brief Which of the Grid display's three "Plane" options is selected.
enum class GridPlaneKind
{
  kXY,  ///< The Grid display's own default.
  kXZ,
  kYZ,
};

/// @brief A point/offset in the Grid display's Display-node local space (the
/// same space `rviz_common::Display::getSceneNode()` and the Grid's "Offset"
/// property both use). Deliberately not an Ogre::Vector3 — see the file brief.
struct Vec3d
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/// @brief The floor-companion plane's placement + size, in the same local
/// space as @ref Vec3d.
struct GridFloorPlaneGeometry
{
  /// The plane is square: `Plane Cell Count * Cell Size`, matching
  /// `rviz_rendering::Grid::createLines()`'s single `plane_extent`.
  double side_metres = 0.0;
  /// Where to put the plane's own pivot, in the Display node's local space.
  Vec3d local_position;
};

/// @brief False if the Grid's own cell geometry would be degenerate (RViz's
/// IntProperty/FloatProperty minimums already forbid this in practice, but a
/// caller reading properties defensively should not build a zero/negative or
/// non-finite plane).
bool is_valid_grid_extent(std::uint32_t cell_count, double cell_size);

/// @brief Parse the Grid display's "Plane" enum property value ("XY", "XZ" or
/// "YZ", exactly as `EnumProperty`'s option names and the .rviz on-disk
/// serialization spell them) into @ref GridPlaneKind. Anything else
/// (including a read failure upstream) defaults to kXY, the Grid display's
/// own default.
GridPlaneKind parse_grid_plane_kind(const std::string & plane_name);

/// @brief Compute the floor-companion plane's size and local placement.
///
/// @param plane The Grid's "Plane" property.
/// @param cell_count The Grid's "Plane Cell Count" property (>= 1 in RViz).
/// @param cell_size The Grid's "Cell Size" property, in metres (> 0 in RViz).
/// @param grid_offset The Grid's "Offset" property, verbatim.
/// @param push_metres How far to push the plane along the grid's own normal,
/// to the "below" side (see the file brief) — the panel combines the 1 mm
/// geometric drop with half the plane's own thickness here, so the plane's
/// near face (not its center) ends up exactly 1 mm below the grid.
GridFloorPlaneGeometry compute_grid_floor_plane_geometry(
  GridPlaneKind plane,
  std::uint32_t cell_count,
  double cell_size,
  const Vec3d & grid_offset,
  double push_metres);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__GRID_FLOOR_PLANE_GEOMETRY_HPP_
