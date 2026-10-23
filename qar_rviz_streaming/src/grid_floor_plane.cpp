// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/grid_floor_plane.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#include <QString>
#include <QVariant>

#include <OgreEntity.h>
#include <OgreMaterial.h>
#include <OgrePass.h>
#include <OgreQuaternion.h>
#include <OgreRenderQueue.h>
#include <OgreSceneManager.h>
#include <OgreSceneNode.h>
#include <OgreTechnique.h>
#include <OgreVector3.h>

#include "rviz_common/display_context.hpp"
#include "rviz_common/display_group.hpp"
#include "rviz_default_plugins/displays/grid/grid_display.hpp"
#include "rviz_rendering/objects/shape.hpp"

#include "qar_rviz_streaming/grid_floor_plane_geometry.hpp"

namespace qar_rviz_streaming
{

namespace
{

// The floor is an opaque gray rectangle 1 mm under the grid plane
// (kFloorPlaneDropMetres, grid_floor_plane_geometry.hpp). The grid lines are 1 mm
// nearer to the camera than the plane's near face, and the plane's pass pushes the
// plane away in depth (below), so the grid is drawn over it at a distance as well.

// The box needs some real thickness to be a solid, well-behaved piece of
// geometry rather than a zero-volume sliver; it is never seen (nothing in
// this feature samples the depth map from underneath the floor), so its exact
// value is not load-bearing the way the 1 mm drop is.
constexpr double kThicknessMetres = 0.004;

// The pass carries the plane's negative depth bias (grid_floor_plane_geometry.hpp), which
// pushes it away from the camera.
constexpr float kDepthBiasConstant = kFloorPlaneDepthBiasConstant;
constexpr float kDepthBiasSlopeScale = kFloorPlaneDepthBiasSlopeScale;

// Unlit neutral gray, in the range 0.35-0.45.
constexpr float kGreyLevel = 0.4f;

std::string planeOptionName(rviz_common::properties::EnumProperty * plane_property)
{
  if (plane_property == nullptr) {
    return "XY";
  }
  // Matches rviz_default_plugins::displays::GridDisplay::Plane's declaration
  // order (grid_display.hpp) and the addOption() calls in its constructor
  // (grid_display.cpp): 0 = XY, 1 = XZ, 2 = YZ.
  switch (plane_property->getOptionInt()) {
    case 1:
      return "XZ";
    case 2:
      return "YZ";
    case 0:
    default:
      return "XY";
  }
}

/// Verbatim copy of GridDisplay::updatePlane()'s three quaternions (grid_display.cpp
/// in rviz_default_plugins) — deliberately not re-derived, so this can never
/// silently drift from what the Grid display itself does.
Ogre::Quaternion planeOrientation(GridPlaneKind plane)
{
  switch (plane) {
    case GridPlaneKind::kXZ:
      return Ogre::Quaternion(1, 0, 0, 0);
    case GridPlaneKind::kYZ:
      return Ogre::Quaternion(
        Ogre::Vector3(0, -1, 0), Ogre::Vector3(0, 0, 1), Ogre::Vector3(1, 0, 0));
    case GridPlaneKind::kXY:
    default:
      return Ogre::Quaternion(
        Ogre::Vector3(1, 0, 0), Ogre::Vector3(0, 0, -1), Ogre::Vector3(0, 1, 0));
  }
}

}  // namespace

/// One tracked Grid display's companion box + the signature its last log line
/// described (so sync() only logs on an actual change).
struct GridFloorPlaneManager::Entry
{
  std::unique_ptr<rviz_rendering::Shape> shape;
  std::string last_signature;
};

GridFloorPlaneManager::GridFloorPlaneManager(
  Ogre::SceneManager * scene_manager,
  rviz_common::DisplayContext * display_context,
  std::function<void (const std::string &)> log)
: scene_manager_(scene_manager), display_context_(display_context), log_(std::move(log))
{
}

GridFloorPlaneManager::~GridFloorPlaneManager() = default;

void GridFloorPlaneManager::collectGridDisplays(
  rviz_common::DisplayGroup * group,
  std::vector<rviz_default_plugins::displays::GridDisplay *> & out) const
{
  if (group == nullptr) {
    return;
  }
  for (int i = 0; i < group->numDisplays(); ++i) {
    rviz_common::Display * child = group->getDisplayAt(i);
    if (auto * nested_group = dynamic_cast<rviz_common::DisplayGroup *>(child)) {
      collectGridDisplays(nested_group, out);
    }
    if (auto * grid = dynamic_cast<rviz_default_plugins::displays::GridDisplay *>(child)) {
      out.push_back(grid);
    }
  }
}

void GridFloorPlaneManager::sync()
{
  if (scene_manager_ == nullptr || display_context_ == nullptr) {
    return;
  }

  std::vector<rviz_default_plugins::displays::GridDisplay *> found;
  collectGridDisplays(display_context_->getRootDisplayGroup(), found);

  if (found.empty()) {
    if (!logged_no_grid_) {
      log_(
        "Floor plane: no Grid display (rviz_default_plugins/Grid) found in the "
        "display tree — nothing to mirror.");
      logged_no_grid_ = true;
    }
  } else {
    logged_no_grid_ = false;
  }

  // Drop bookkeeping for any Grid display that has been deleted. This
  // deliberately does NOT touch the Ogre objects we made for it.
  // rviz_common::Display's teardown destroys the GRID'S OWN scene node
  // (grid_->getSceneNode(), via rviz_rendering::Grid's destructor, called
  // from GridDisplay's `= default` destructor) but there is no documented
  // guarantee it cascades to OTHER children of the Display's own scene node —
  // Ogre::SceneManager::destroySceneNode() does not, by convention, cascade
  // to children (this codebase's own Grid does its own explicit child
  // cleanup rather than relying on that). So by the time we notice a
  // GridDisplay* is gone, our sibling node's parent may already be freed;
  // touching it — even just to destroy it — risks a use-after-free that
  // cannot be verified from headers alone. Leaking one small box + material
  // for a Grid display the operator deletes at runtime is the far safer failure
  // mode, so `release()` below skips Entry's (and Shape's) destructor on
  // purpose.
  for (auto it = entries_.begin(); it != entries_.end();) {
    const bool still_present =
      std::find(found.begin(), found.end(), it->first) != found.end();
    if (!still_present) {
      it->second.release();
      it = entries_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto * grid : found) {
    syncOne(grid);
  }
}

void GridFloorPlaneManager::syncOne(rviz_default_plugins::displays::GridDisplay * grid)
{
  using rviz_common::properties::EnumProperty;
  using rviz_common::properties::TfFrameProperty;
  using rviz_common::properties::VectorProperty;

  Ogre::SceneNode * display_node = grid->getSceneNode();
  if (display_node == nullptr) {
    return;
  }

  auto map_it = entries_.find(grid);
  if (map_it == entries_.end()) {
    map_it = entries_.emplace(grid, std::make_unique<Entry>()).first;
  }
  Entry & entry = *map_it->second;

  if (!entry.shape) {
    entry.shape = std::make_unique<rviz_rendering::Shape>(
      rviz_rendering::Shape::Cube, scene_manager_, display_node);

    Ogre::MaterialPtr material = entry.shape->getMaterial();
    if (material && material->getNumTechniques() > 0) {
      Ogre::Technique * technique = material->getTechnique(0);
      if (technique != nullptr && technique->getNumPasses() > 0) {
        Ogre::Pass * pass = technique->getPass(0);
        // Lighting must stay ON here. rviz_rendering::Shape::setColor() only
        // ever writes ambient/diffuse (setAmbient(c * 0.5) and setDiffuse(c)
        // on technique 0; see shape.cpp in rviz_rendering) and Ogre's own
        // OgrePass.h documents ambient/diffuse/specular/self-illumination as
        // all having "no effect if dynamic lighting is disabled". With
        // lighting off, this app's fixed-function pipeline (only
        // RenderSystem_GL is loaded, see render_system.cpp in rviz_rendering,
        // and no Ogre::RTShader::ShaderGenerator is ever created, so there
        // is no RTSS shader path here) falls back to the mesh's vertex
        // colour, which is absent -> white: the plane would render white
        // despite setColor(kGreyLevel, kGreyLevel, kGreyLevel, 1.0f).
        //
        // So: keep lighting on, zero ambient/diffuse/specular so no light
        // - or its absence - can shade the plane, and put kGreyLevel in
        // self-illumination/emissive, which FFP simply adds on top:
        //   colour = emissive + ambient * sceneAmbient + sum_lights(...)
        // Zeroing ambient/diffuse/specular kills every term but emissive, so
        // the plane is exactly kGreyLevel everywhere, regardless of the
        // plugin's own scene lights. No setColor() call follows, since it
        // would only write colours these lines override.
        pass->setLightingEnabled(true);
        pass->setAmbient(0.0f, 0.0f, 0.0f);
        pass->setDiffuse(0.0f, 0.0f, 0.0f, 1.0f);
        pass->setSpecular(0.0f, 0.0f, 0.0f, 1.0f);
        pass->setSelfIllumination(kGreyLevel, kGreyLevel, kGreyLevel);
        pass->setDepthCheckEnabled(true);
        pass->setDepthWriteEnabled(true);
        pass->setDepthBias(kDepthBiasConstant, kDepthBiasSlopeScale);
      }
    }

    // Excluded from Ogre ray/box scene queries (e.g. the "Focus Camera" and
    // "Publish Point" tools' picking rays) so it is inert to anything that
    // walks the scene looking for something to hit. RViz's own click-select
    // tool never sees it in the first place: that tool only resolves a click
    // to a Display through an explicit SelectionHandler registration, which
    // we never make for this object.
    if (entry.shape->getEntity() != nullptr) {
      entry.shape->getEntity()->setQueryFlags(0);
      // Drawn BEFORE the Grid display and every other display: RENDER_QUEUE_4 comes ahead of
      // RENDER_QUEUE_MAIN, the group an entity is in unless it asks for another one. The plane
      // is opaque and writes depth, so what is drawn later is tested against it, and a Grid
      // that blends (its Alpha below 1: no depth write, source-alpha blending) is blended
      // onto the grey instead of being painted over by it.
      entry.shape->getEntity()->setRenderQueueGroup(Ogre::RENDER_QUEUE_4);
    }
  }

  const bool enabled = grid->isEnabled();
  entry.shape->getRootNode()->setVisible(enabled);
  if (!enabled) {
    return;
  }

  const QVariant cell_count_value = grid->subProp("Plane Cell Count")->getValue();
  const QVariant cell_size_value = grid->subProp("Cell Size")->getValue();
  const std::uint32_t cell_count =
    cell_count_value.isValid() ? cell_count_value.toUInt() : 10u;
  const double cell_size = cell_size_value.isValid() ? cell_size_value.toDouble() : 1.0;

  auto * plane_property = dynamic_cast<EnumProperty *>(grid->subProp("Plane"));
  const std::string plane_name = planeOptionName(plane_property);
  const GridPlaneKind plane_kind = parse_grid_plane_kind(plane_name);

  Ogre::Vector3 offset = Ogre::Vector3::ZERO;
  if (auto * offset_property = dynamic_cast<VectorProperty *>(grid->subProp("Offset"))) {
    offset = offset_property->getVector();
  }

  std::string frame_name = "<unknown>";
  if (auto * frame_property = dynamic_cast<TfFrameProperty *>(grid->subProp("Reference Frame"))) {
    frame_name = frame_property->getFrame().toStdString();
  }

  if (!is_valid_grid_extent(cell_count, cell_size)) {
    entry.shape->getRootNode()->setVisible(false);
    return;
  }

  // The box's CENTER, not its near face, is what setPosition places, so the
  // pure geometry is asked for a push of (the 1 mm drop) + (half the
  // box's own thickness): that puts the near face exactly 1 mm below the
  // grid and lets the box extend the rest of its thickness further away.
  const Vec3d grid_offset{offset.x, offset.y, offset.z};
  const GridFloorPlaneGeometry geometry = compute_grid_floor_plane_geometry(
    plane_kind, cell_count, cell_size, grid_offset, floor_plane_push_metres(kThicknessMetres));

  entry.shape->setOrientation(planeOrientation(plane_kind));
  entry.shape->setPosition(
    Ogre::Vector3(
      static_cast<float>(geometry.local_position.x),
      static_cast<float>(geometry.local_position.y),
      static_cast<float>(geometry.local_position.z)));
  entry.shape->setScale(
    Ogre::Vector3(
      static_cast<float>(geometry.side_metres),
      static_cast<float>(kThicknessMetres),
      static_cast<float>(geometry.side_metres)));

  std::ostringstream signature;
  signature << "frame=" << frame_name << " plane=" << plane_name
            << " cells=" << cell_count << " cell_size=" << cell_size
            << " offset=(" << offset.x << "," << offset.y << "," << offset.z << ")";
  const std::string signature_str = signature.str();
  if (signature_str != entry.last_signature) {
    entry.last_signature = signature_str;
    std::ostringstream line;
    line << "Floor plane: frame='" << frame_name << "' plane=" << plane_name
         << " " << cell_count << "x" << cell_count << " cells @ " << cell_size
         << " m -> " << geometry.side_metres << "x" << geometry.side_metres
         << " m plane, offset=(" << offset.x << ", " << offset.y << ", " << offset.z
         << ") m, 1mm drop, depth_bias(constant=" << kDepthBiasConstant
         << ", slopeScale=" << kDepthBiasSlopeScale << ").";
    log_(line.str());
  }
}

}  // namespace qar_rviz_streaming
