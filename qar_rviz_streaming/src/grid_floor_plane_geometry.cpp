// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/grid_floor_plane_geometry.hpp"

#include <cmath>

namespace qar_rviz_streaming
{

bool is_valid_grid_extent(std::uint32_t cell_count, double cell_size)
{
  return cell_count > 0 && cell_size > 0.0 && std::isfinite(cell_size);
}

GridPlaneKind parse_grid_plane_kind(const std::string & plane_name)
{
  if (plane_name == "XZ") {
    return GridPlaneKind::kXZ;
  }
  if (plane_name == "YZ") {
    return GridPlaneKind::kYZ;
  }
  return GridPlaneKind::kXY;
}

double floor_plane_push_metres(double thickness_metres)
{
  return kFloorPlaneDropMetres + 0.5 * thickness_metres;
}

GridFloorPlaneGeometry compute_grid_floor_plane_geometry(
  GridPlaneKind plane,
  std::uint32_t cell_count,
  double cell_size,
  const Vec3d & grid_offset,
  double push_metres)
{
  GridFloorPlaneGeometry out;
  out.side_metres = static_cast<double>(cell_count) * cell_size;
  out.local_position = grid_offset;

  switch (plane) {
    case GridPlaneKind::kXZ:
      // updatePlane() leaves the node at identity orientation here, so the
      // grid's own normal (local Y, per Grid::createGridPlane()) is still
      // local Y with no sign flip — push along local +Y (see the file brief:
      // an arbitrary but consistent tie-break, this Plane has no gravity-
      // relative "below").
      out.local_position.y += push_metres;
      break;
    case GridPlaneKind::kYZ:
    case GridPlaneKind::kXY:
    default:
      // Both YZ and XY rotate the grid's local-Y normal onto the Display
      // node's local +Z (verified against updatePlane()'s exact
      // quaternions in grid_display.cpp), so "below" a Z-up fixed frame is
      // local -Z.
      out.local_position.z -= push_metres;
      break;
  }

  return out;
}

}  // namespace qar_rviz_streaming
