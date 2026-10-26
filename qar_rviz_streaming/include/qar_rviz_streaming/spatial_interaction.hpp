// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__SPATIAL_INTERACTION_HPP_
#define QAR_RVIZ_STREAMING__SPATIAL_INTERACTION_HPP_

/// @file spatial_interaction.hpp
/// @brief The armed spatial mode: what the operator turned on, what the AR user
/// picked, and what would be published. Pure — no Qt, no rclcpp.
///
/// The RViz panel above this is a shell: it sets a mode, shows
/// @ref SpatialInteractionStatus::summary, and offers Confirm / Cancel. Every
/// decision — which session a sample drives, when a pick counts as made, what
/// message it becomes, and whether it may go out without a human saying so —
/// is here, where it is unit-tested.
///
/// ## Confirmation is on by default, deliberately
///
/// A stray hand gesture must not drive a robot. A committed pick therefore
/// becomes *pending*: the operator sees it in the panel and presses Confirm.
/// @ref SpatialInteractionController::set_auto_confirm turns the extra step off
/// for demos and for `/clicked_point`, where the cost of a wrong pick is a
/// marker in RViz rather than a robot moving.

#include <cstdint>
#include <string>
#include <vector>

#include "qar_ar_core/panel_input_source.hpp"
#include "qar_ar_core/spatial_pick.hpp"

#include "qar_rviz_streaming/spatial_publish.hpp"

namespace qar_rviz_streaming
{

/// @brief One entry of the panel's "Target" dropdown, declared HERE rather than
/// in the Qt code so the list, its order, its default and its help text are one
/// unit-tested table instead of three hand-kept copies.
///
/// ## Which one leads — @ref kActionPointTargetId
///
/// QarOS resolves a pick itself: it casts a ray — **slightly curved, not a
/// straight line** — against the app volume's scene and reports the intersection
/// as the gesture's `action_point`. It is the authoritative answer and the one
/// the AR user saw themselves select, so it leads the list and is
/// @ref default_spatial_target_id. The minimum drop returned a near-field point
/// instead of the scene intersection, so this default needs the drop after the
/// minimum drop or a newer one.
///
/// The SDK header of that drop also names the failure mode: the action point is
/// "the intersection the gesture acts on, **or the start point when nothing was
/// intersected**". A point on the hand is therefore the no-hit case: it equals
/// the start point exactly, and it is flagged per gesture
/// (`PanelPointerSample::room_point_is_scene_hit`, `start_to_action=0.000` in
/// the `[qar-spatial]` trace). That is what makes leading with it safe: a click
/// on empty space is refused, never published on the operator's hand.
///
/// The other two entries stay, and stay labelled: only a plane target can
/// promise the answer is on the floor, which a nav goal needs. Both are
/// **straight-ray approximations** of a ray QarOS casts curved, which is what
/// @ref SpatialTargetOption::straight_ray_approximation flags and what
/// `PointingRay::aim_to_reported_m` ("aim_off" in the trace) measures.
struct SpatialTargetOption
{
  /// Stable value stored in the combo's item data. Never renumbered: the switch
  /// dispatches on the id, not on position, so the order of the list can change.
  int id = 0;
  /// What the operator reads in the dropdown.
  const char * label = "";
  /// One line of help, shown in the tooltip beneath the list.
  const char * help = "";
  /// True when this entry answers with OUR straight-ray reconstruction rather
  /// than with the runtime's own intersection.
  bool straight_ray_approximation = false;
};

/// The runtime's own scene intersection, published verbatim. THE DEFAULT.
constexpr int kActionPointTargetId = 1;
/// The aiming ray, intersected with z = 0.
constexpr int kGroundPlaneTargetId = 0;
/// The aiming ray, advanced a fixed 2 m.
constexpr int kFixedDepthTargetId = 2;

/// @brief The dropdown's entries, in the order the panel shows them. Index 0 is
/// the default selection, so a freshly built combo is already on it.
const std::vector<SpatialTargetOption> & spatial_target_options();

/// @brief The id a panel starts on: @ref kActionPointTargetId.
int default_spatial_target_id();

/// @brief The pick target an option id arms. An unknown id falls back to the
/// default rather than to a silently different geometry.
qar_ar_core::PickTarget spatial_target_from_id(int id);

/// @brief What the operator armed from the QAR panel.
enum class SpatialMode : std::uint8_t
{
  Off = 0,      ///< Spatial samples are ignored (the default).
  Point,        ///< Pick a point -> `/clicked_point`.
  NavGoal,      ///< Pick a pose -> `/goal_pose`.
  InitialPose,  ///< Pick a pose -> `/initialpose`.
  Area          ///< Sweep a region -> a polygon topic.
};
const char * to_string(SpatialMode mode);

/// @brief The publish target a mode uses unless the operator overrides it.
SpatialPublishTarget publish_target_for(SpatialMode mode);

/// @brief Everything the panel needs to render, in one snapshot.
struct SpatialInteractionStatus
{
  SpatialMode mode = SpatialMode::Off;
  qar_ar_core::PickState state = qar_ar_core::PickState::Idle;
  bool has_pending = false;   ///< A pick is waiting for Confirm.
  bool auto_confirm = false;  ///< ... unless it publishes itself.
  /// One line for the operator: "Nav goal @ x=1.20 y=3.40 z=0.00, yaw 90 deg
  /// (ground plane)". Empty when there is nothing to say.
  std::string summary;
  /// The last refusal, verbatim from the pick or the message mapping.
  std::string error;
};

/// @brief How the controller has treated the samples handed to it.
///
/// The spatial path's counterpart to PanelInputStats, and it exists for the same
/// reason: when no pick is produced, the first thing to establish is
/// WHERE the stream stopped. @ref samples_seen separates "the adapter delivered
/// nothing" from "the controller threw everything away", and the four ignore
/// counters say which of the four gates closed.
struct SpatialInputStats
{
  /// Samples handed to @ref SpatialInteractionController::handle_pointer.
  std::uint64_t samples_seen = 0;
  /// Dropped because no mode is armed — the panel's Mode combo is "Off".
  std::uint64_t ignored_mode_off = 0;
  /// Dropped because the sample carried neither a room point nor a room ray
  /// (a panel (u, v) sample, or a phase with nothing to place).
  std::uint64_t ignored_not_spatial = 0;
  /// Dropped because a committed pick is already waiting for Confirm.
  std::uint64_t ignored_pending = 0;
  /// Hover samples that reached a session. A hover PREVIEWS and never commits
  /// (qar_ar_core::gesture_commits_pick), so this counts separately from
  /// the refusals below: a large number here beside `picks_committed == 0` is
  /// the picker working correctly while a hand waves at it, not a fault.
  std::uint64_t hover_previews = 0;
  /// Reached the session and changed nothing: an out-of-order phase, or
  /// geometry that resolves to no point (ray parallel to the plane, ...).
  std::uint64_t ignored_by_session = 0;
  /// The part of @ref ignored_by_session that was an Action Point CLICK aimed
  /// at EMPTY SPACE: the runtime intersected nothing, handed its start point
  /// (the hand) back as the action point, and the pick was refused rather than
  /// published on the operator's hand (see
  /// `qar_ar_core::SpatialPoint::refused_no_intersection`). Counted on its
  /// own so the heartbeat tells "clicked on nothing" apart from every other
  /// refusal. Only gestures that would commit count: a hover over empty space
  /// is routine and previews only.
  std::uint64_t refused_no_intersection = 0;
  /// Samples that moved the session on (a preview, an arm, or a commit).
  std::uint64_t samples_accepted = 0;
  /// Picks that became a publishable message.
  std::uint64_t picks_committed = 0;
  /// The most recent ignore, in words. Empty until something is ignored.
  std::string last_drop_reason;
};

/// @brief Drives the pick sessions for whichever mode is armed.
class SpatialInteractionController
{
public:
  SpatialInteractionController();

  SpatialMode mode() const { return mode_; }
  /// Switching mode abandons anything in flight: a half-drawn area must not
  /// survive into a nav goal.
  void set_mode(SpatialMode mode);

  /// The room -> ROS frame change. MUST be the same one the camera uses, or the
  /// pick lands where the operator was not pointing (see spatial_frames.hpp).
  void set_transform(const qar_ar_core::RoomToRosTransform & transform);
  const qar_ar_core::RoomToRosTransform & transform() const { return transform_; }

  void set_target(const qar_ar_core::PickTarget & target);
  const qar_ar_core::PickTarget & target() const { return target_; }

  /// Area mode only. Changing it abandons a region in progress.
  void set_area_shape(qar_ar_core::AreaShape shape);
  qar_ar_core::AreaShape area_shape() const;

  /// The frame every published pick is stamped with.
  void set_frame_id(std::string frame_id);
  const std::string & frame_id() const { return frame_id_; }

  void set_auto_confirm(bool auto_confirm) { auto_confirm_ = auto_confirm; }
  bool auto_confirm() const { return auto_confirm_; }

  void set_stamp(const RosStamp & stamp) { stamp_ = stamp; }

  /// Override the destination for one mode — an operator publishing goals to a
  /// non-standard topic, or a custom message type.
  void set_publish_override(SpatialMode mode, const SpatialPublishConfig & config);
  void clear_publish_override(SpatialMode mode);
  /// The config a mode would publish with right now (override, else default,
  /// with the controller's frame id and stamp applied).
  SpatialPublishConfig publish_config(SpatialMode mode) const;

  /// @brief Feed one raw sample from the panel input source.
  /// @return true when the panel should refresh (state or preview changed).
  bool handle_sample(const qar_ar_core::PanelPointerSample & sample);

  /// @brief Feed a sample already brought into the ROS frame (tests, and any
  /// host that has done its own transform).
  bool handle_pointer(const qar_ar_core::SpatialPointer & pointer);

  /// @brief What has happened to every sample fed in so far. Never reset by
  /// arming, cancelling or publishing: these are session-lifetime totals, and a
  /// counter that resets when the operator touches the panel answers nothing.
  const SpatialInputStats & input_stats() const { return input_stats_; }

  /// Polygon mode only: close the region the taps have collected.
  bool close_polygon();

  SpatialInteractionStatus status() const;

  /// A pick is committed and waiting.
  bool has_pending() const { return pending_.ok; }
  /// The pending pick may go out without a human pressing Confirm.
  bool pending_ready() const { return pending_.ok && auto_confirm_; }
  const SpatialPublishRequest & pending() const { return pending_; }

  /// @brief Take the pending message; the session returns to idle.
  /// A request with `ok == false` means there was nothing to confirm.
  SpatialPublishRequest confirm();

  /// Abandon the pick, pending or in flight.
  void cancel();

  /// Direct access for a host that draws the preview into the streamed scene.
  const qar_ar_core::PointPickSession & point_session() const { return point_; }
  const qar_ar_core::PosePickSession & pose_session() const { return pose_; }
  const qar_ar_core::AreaPickSession & area_session() const { return area_; }

private:
  void reset_sessions();
  void capture_pending();

  SpatialMode mode_ = SpatialMode::Off;
  qar_ar_core::RoomToRosTransform transform_{};
  /// NOT @ref default_spatial_target_id — deliberately. That id is the PANEL's
  /// default and the panel arms it explicitly in its constructor. This is what
  /// a host that never calls @ref set_target gets, and for that case the plane
  /// target is the conservative one: it resolves to z = 0 by construction and
  /// so cannot publish a marker on the operator's hand, whatever a gesture
  /// carries.
  qar_ar_core::PickTarget target_ = qar_ar_core::PickTarget::ground();
  std::string frame_id_ = "map";
  bool auto_confirm_ = false;
  RosStamp stamp_{};

  qar_ar_core::PointPickSession point_;
  qar_ar_core::PosePickSession pose_;
  qar_ar_core::AreaPickSession area_;

  bool has_override_[5] = {false, false, false, false, false};
  SpatialPublishConfig override_[5]{};

  SpatialPublishRequest pending_{};
  std::string error_;
  SpatialInputStats input_stats_{};
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__SPATIAL_INTERACTION_HPP_
