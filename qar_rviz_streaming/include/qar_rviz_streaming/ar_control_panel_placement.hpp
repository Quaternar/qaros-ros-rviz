// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_PLACEMENT_HPP_
#define QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_PLACEMENT_HPP_

/// @file ar_control_panel_placement.hpp
/// @brief Where the AR control panel goes and for whom it is created. Pure: no GUI toolkit and no
/// ROS client library.
///
/// ## The pose
///
/// A render sender bound to an app volume reports the two eye poses of every frame in app
/// content space. @ref head_from_eye_poses turns them into the head position (the midpoint of
/// the eyes) and the gaze (the left eye's forward axis) in room space, through the live scene
/// placement. @ref place_in_front_of then puts the panel `distance_m` ahead of the head along
/// the gaze flattened onto the horizontal plane, `right_m` to the right of it, and its top edge
/// `top_below_eyes_m` below the eyes. The panel is turned about the vertical axis only, so that
/// it stands upright, and it faces the head.
///
/// ## The orientation convention
///
/// The pose of a QarOS panel is the midpoint of its top edge. In panel space +X runs along the
/// top edge to the viewer's left, +Y runs up and the front normal is -Z, pointing at the
/// viewer. A panel that faces a viewer therefore has its local -Z axis along the horizontal
/// direction from the panel to the viewer. A pose with the identity orientation shows its front
/// to a viewer that stands on its negative-Z side.
///
/// @ref ControlPanelPlacementParams::flip_facing turns the yaw by 180 degrees. It exists for
/// the case that a panel shows its back in a headset: the host reads it from a variable, so
/// the fix needs a restart and no rebuild.
///
/// ## The policy
///
/// @ref ControlPanelPlacementPolicy decides, from the peers that have a render stream, when an
/// instance of the panel is created and for whom. It makes no QarOS call itself.
///
///  * Per-peer mode (the default): every peer that has a render stream gets its own instance,
///    visible to that peer only, when its first eye poses are known. A session without a peer
///    has no instance.
///  * Shared mode: ONE instance, visible to every peer, is created at the first eye poses of
///    any peer and placed in front of that peer. When `pose_wait_s` pass after the session
///    started without a pose, it is created at @ref fallback_control_panel_pose instead. Later
///    peers get nothing more.
///
/// In both modes an instance key is created at most once per session and nothing is ever moved.
/// An instance of a peer whose stream closes is reported as hidden and, when the stream
/// returns, as shown again.

#include <cstdint>
#include <string>
#include <vector>

#include "qar_ar_core/qar_streaming_c.hpp"   // QarPose, QarVector3, QarPeerId
#include "qar_ar_core/spatial_frames.hpp"    // ScenePlacement, scene_*_to_room

namespace qar_rviz_streaming
{

struct ControlPanelPlacementParams
{
  float distance_m = 1.0f;         ///< ahead of the head, along the flattened gaze
  float right_m = 0.35f;           ///< to the right of the gaze
  float top_below_eyes_m = 0.10f;  ///< the top edge of the panel is this far below the eyes
  bool flip_facing = false;        ///< turn the yaw by 180 degrees
};

/// The default height of the top edge of the fallback pose, in metres.
constexpr float kFallbackControlPanelTopM = 1.5f;

/// The pose of the instance that is created without a pose: (0.7, @p top_y, -1.2), identity
/// orientation. With the default @p top_y the top edge is at a fixed height of the room.
QarPose fallback_control_panel_pose(float top_y = kFallbackControlPanelTopM);

/// @brief Head position and gaze in room space from the two eye poses of a frame.
///
/// The head is the midpoint of the eye positions, the gaze is the left eye's orientation
/// applied to (0, 0, -1); both are mapped from app content space to room space with
/// @p placement.
/// @return false for a pose with a non-finite number or a null orientation.
bool head_from_eye_poses(
  const QarPose & left_eye, const QarPose & right_eye,
  const qar_ar_core::ScenePlacement & placement,
  QarVector3 & out_head_room, QarVector3 & out_gaze_room);

/// @brief The pose of the panel for a viewer at @p head_room looking along @p gaze_room.
///
/// Yaw only; QarOS panel space (top-centre origin, -Z is the front normal and points at the
/// viewer).
/// @return false for a null or non-finite gaze and for a gaze within about 5 degrees of
/// vertical, which has no horizontal direction to place the panel along.
bool place_in_front_of(
  const QarVector3 & head_room, const QarVector3 & gaze_room,
  const ControlPanelPlacementParams & params, QarPose & out_pose);

/// One peer that has a render stream. @ref has_pose says that its eye poses are known and that
/// @ref place_in_front_of gave a pose for them, which is @ref panel_pose; it stays false while
/// the poses are unknown or the gaze is too steep to place a panel along.
struct ControlPanelPeer
{
  QarPeerId peer{};
  bool has_pose = false;
  QarPose panel_pose{};
};

/// Undecided until @ref ControlPanelPlacementPolicy::session_started, then the mode of the
/// constructor.
enum class ControlPanelMode : std::uint8_t { Undecided = 0, PerPeer, Shared };
const char * to_string(ControlPanelMode mode);  ///< "undecided", "per_peer", "shared"

/// The key of the one instance of the shared mode.
constexpr const char * kSharedControlPanelKey = "everybody";

struct ControlPanelInstanceRequest
{
  std::string instance_key;   ///< "everybody" or "peer:<32 hex>"
  bool everybody = false;     ///< UrlPanelDesc::visible_to_peers stays empty
  QarPeerId peer{};           ///< the peer the instance is for; in shared mode the peer in front
                              ///< of which it is placed (all zero when it is not placed by a pose)
  QarPose pose{};
};

struct ControlPanelVisibilityChange
{
  std::string instance_key;
  bool visible = true;
};

class ControlPanelPlacementPolicy
{
public:
  /// @param pose_wait_s Shared mode: how long after session_started the instance waits for a
  /// pose before it is created at the fallback pose. Not used in per-peer mode.
  /// @param per_peer true = one instance per peer, false = one shared instance.
  explicit ControlPanelPlacementPolicy(double pose_wait_s = 20.0, bool per_peer = true);

  /// The session is up. A second call is ignored. Before it, tick() creates nothing.
  void session_started(double now_s);

  /// Instances to create now; each key at most once per session.
  std::vector<ControlPanelInstanceRequest> tick(
    double now_s, const std::vector<ControlPanelPeer> & peers_with_streams);

  /// Per-peer instances whose peer left (hide) or came back (show) since the last call.
  std::vector<ControlPanelVisibilityChange> take_visibility_changes();

  ControlPanelMode mode() const { return mode_; }

  /// The height of the top edge of the pose an instance gets when it has no pose of a viewer:
  /// kFallbackControlPanelTopM until a caller says otherwise (for example the RViz ground plus
  /// that height). Read when the instance is created.
  void set_fallback_top_y(float top_y) { fallback_top_y_ = top_y; }
  float fallback_top_y() const { return fallback_top_y_; }

private:
  double pose_wait_s_;
  float fallback_top_y_ = kFallbackControlPanelTopM;
  bool per_peer_;
  bool started_ = false;
  double started_at_s_ = 0.0;
  ControlPanelMode mode_ = ControlPanelMode::Undecided;
  std::vector<std::string> created_;   ///< instance keys created so far
  std::vector<std::string> present_;   ///< keys of the peers that had a stream at the last tick
  std::vector<ControlPanelVisibilityChange> changes_;
};

/// "peer:" followed by the 32 lower-case hex digits of the peer id.
std::string control_panel_instance_key(const QarPeerId & peer);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_CONTROL_PANEL_PLACEMENT_HPP_
