// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__RVIZ_DISPLAY_CONTEXT_ADAPTER_HPP_
#define QAR_RVIZ_STREAMING__RVIZ_DISPLAY_CONTEXT_ADAPTER_HPP_

#include <cstddef>
#include <cstdint>
#include <string>

namespace Ogre
{
class SceneManager;
class Camera;
}  // namespace Ogre

namespace rviz_common
{
class DisplayContext;
}  // namespace rviz_common

namespace qar_rviz_streaming
{

/// @brief Adapts an rviz_common::DisplayContext into the generic Ogre access +
/// render synchronization that the reusable qar_ogre_streaming library needs.
///
/// This is the single RViz-specific seam: it wraps @c getDisplayContext() and exposes only what the library
/// consumes — the shared @c Ogre::SceneManager and the render lock/queue calls —
/// so the library itself never includes an rviz header. It also validates that
/// the context and scene manager are non-null and reports scene identity
/// (name, camera count, node count, frame count) to the panel diagnostics.
///
/// The wrapped pointers are owned by RViz; this adapter is non-owning.
class RVizDisplayContextAdapter
{
public:
  /// @param context The panel's DisplayContext (may be null; isValid() reports it).
  explicit RVizDisplayContextAdapter(rviz_common::DisplayContext * context);

  /// @brief True iff both the context and its Ogre::SceneManager are available.
  bool isValid() const;

  rviz_common::DisplayContext * context() const { return context_; }

  /// @brief The shared RViz Ogre scene (nullptr if unavailable). This is the
  /// exact pointer handed to OgreQarStreamingConfig::scene_manager.
  Ogre::SceneManager * getSceneManager() const;

  /// @brief The Ogre camera of RViz's current (main) view controller, or nullptr
  /// if unavailable. Used by the offscreen path to clone the live view each tick
  /// (via getViewManager()->getCurrent()->getCamera()).
  Ogre::Camera * getViewCamera() const;

  // --- Render synchronization -----------------------
  void lockRender();
  void unlockRender();
  void queueRender();

  // --- Diagnostics ---------------------------------------------------------

  /// @brief Ogre scene manager name, or a placeholder if unavailable.
  std::string sceneManagerName() const;

  /// @brief Number of Ogre cameras in the scene (>= 1 once RViz's main view exists).
  std::size_t cameraCount() const;

  /// @brief Number of direct children of the scene's root node.
  std::size_t sceneNodeChildCount() const;

  /// @brief RViz's monotonically increasing rendered-frame counter.
  std::uint64_t frameCount() const;

  /// @brief Human-readable multi-line status block for the panel/diagnostics dump.
  std::string statusSummary() const;

private:
  rviz_common::DisplayContext * context_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__RVIZ_DISPLAY_CONTEXT_ADAPTER_HPP_
