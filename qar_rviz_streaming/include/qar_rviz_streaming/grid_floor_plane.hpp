// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__GRID_FLOOR_PLANE_HPP_
#define QAR_RVIZ_STREAMING__GRID_FLOOR_PLANE_HPP_

/// @file grid_floor_plane.hpp
/// @brief Owns the opaque, unlit gray "floor" box(es) that mirror every
/// rviz_default_plugins Grid display in the scene, so the AR depth map sees a
/// solid floor instead of the empty space between grid lines (RViz's Grid
/// draws thin line segments only). See grid_floor_plane_geometry.hpp for the
/// placement math and why the plane is placed the way it is.
///
/// This is the ONE file in qar_rviz_streaming that knows about
/// rviz_default_plugins::displays::GridDisplay; everything Ogre/RViz-specific
/// lives here rather than in qar_streaming_panel.cpp, which only owns and
/// ticks a `GridFloorPlaneManager`.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Ogre
{
class SceneManager;
}  // namespace Ogre

namespace rviz_common
{
class Display;
class DisplayContext;
class DisplayGroup;
}  // namespace rviz_common

namespace rviz_default_plugins
{
namespace displays
{
class GridDisplay;
}  // namespace displays
}  // namespace rviz_default_plugins

namespace qar_rviz_streaming
{

/// @brief Owned and ticked by the panel (one `sync()` call per render tick).
/// Not copyable: it owns live Ogre scene objects keyed by raw Display
/// pointers RViz owns.
class GridFloorPlaneManager
{
public:
  /// @param scene_manager The RViz Ogre scene (from RVizDisplayContextAdapter
  ///   ::getSceneManager()); may be null, in which case sync() is a no-op.
  /// @param display_context The panel's DisplayContext
  ///   (Panel::getDisplayContext()); may be null, in which case sync() is a
  ///   no-op.
  /// @param log One line per creation/re-sync (see sync()); wired to the
  ///   panel's appendLog so it reaches the .err.log the same way every other
  ///   panel diagnostic does.
  GridFloorPlaneManager(
    Ogre::SceneManager * scene_manager,
    rviz_common::DisplayContext * display_context,
    std::function<void (const std::string &)> log);

  ~GridFloorPlaneManager();

  GridFloorPlaneManager(const GridFloorPlaneManager &) = delete;
  GridFloorPlaneManager & operator=(const GridFloorPlaneManager &) = delete;

  /// @brief Re-sync against the CURRENT display tree: find every Grid
  /// display, create/update/hide its companion plane, and drop bookkeeping
  /// for any Grid display that has been deleted. Cheap — a handful of
  /// property reads and Ogre setPosition/setOrientation/setScale/setVisible
  /// calls per tracked Grid — so it is meant to run every panel tick, not to
  /// be gated behind a slower timer.
  ///
  /// Logs (via the constructor's `log`) once when no Grid display exists at
  /// all (and again only if that changes), and once per tracked Grid whenever
  /// its frame/plane/cell-count/cell-size/offset actually changes — never
  /// unconditionally every tick.
  void sync();

private:
  struct Entry;

  Ogre::SceneManager * scene_manager_;
  rviz_common::DisplayContext * display_context_;
  std::function<void (const std::string &)> log_;
  std::map<rviz_default_plugins::displays::GridDisplay *, std::unique_ptr<Entry>> entries_;
  bool logged_no_grid_ = false;

  void collectGridDisplays(
    rviz_common::DisplayGroup * group,
    std::vector<rviz_default_plugins::displays::GridDisplay *> & out) const;
  void syncOne(rviz_default_plugins::displays::GridDisplay * grid);
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__GRID_FLOOR_PLANE_HPP_
