// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/spatial_interaction.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace qar_rviz_streaming
{
namespace
{

using qar_ar_core::AreaShape;
using qar_ar_core::PickState;

constexpr std::size_t kModeCount = 5;

std::size_t index_of(SpatialMode mode)
{
  const auto raw = static_cast<std::size_t>(mode);
  return raw < kModeCount ? raw : 0u;
}

std::string fixed(double value, int decimals = 2)
{
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  return std::string(buffer);
}

std::string describe_position(const QarVector3 & p)
{
  return "x=" + fixed(p.x) + " y=" + fixed(p.y) + " z=" + fixed(p.z);
}

std::string describe_yaw(float yaw_rad)
{
  return fixed(static_cast<double>(yaw_rad) * 180.0 / 3.14159265358979323846, 1) + " deg";
}

std::string describe_area(const qar_ar_core::AreaPickResult & area)
{
  std::string text = std::string(qar_ar_core::to_string(area.shape));
  if (area.shape == AreaShape::Circle) {
    text += " r=" + fixed(area.radius_m) + " m";
  } else if (area.shape == AreaShape::Rectangle) {
    text += " " + fixed(area.width_m) + " x " + fixed(area.height_m) + " m";
  } else {
    text += " " + std::to_string(area.vertices.size()) + " vertices";
  }
  text += ", area " + fixed(area.area_m2) + " m2";
  return text;
}

}  // namespace

const std::vector<SpatialTargetOption> & spatial_target_options()
{
  // Order matters: index 0 is what a freshly built QComboBox selects, and it is
  // default_spatial_target_id().
  //
  // The Action Point leads: it is the only target the runtime itself computed,
  // so it is the only one that can be exactly what the AR user saw. The SDK
  // header of the drop after the minimum drop says the action point is "the
  // intersection the gesture acts on, or the start point when nothing was
  // intersected"; the minimum drop handed back a near-field point on every gesture.
  //
  // The failure of landing on the operator's own hand is guarded: a no-hit
  // gesture is the one where the action point EQUALS the start point, which is
  // detectable per gesture (PanelPointerSample::room_point_is_scene_hit) and
  // reads as `start_to_action=0.000` in the [qar-spatial] trace.
  static const std::vector<SpatialTargetOption> kOptions = {
    SpatialTargetOption{
      kActionPointTargetId,
      "Action Point (QarOS scene intersect)",
      "THE DEFAULT. The point QarOS itself computed: it casts its own ray -- "
      "slightly CURVED, not a straight line -- against the app volume's scene and "
      "reports where it hit. Published verbatim in the chosen frame, with no "
      "plane, no ray and no depth substitution, so it is the only target that can "
      "be exactly what the AR user saw. A gesture that carries no action point is "
      "refused, never published as the origin.",
      false},
    SpatialTargetOption{
      kGroundPlaneTargetId,
      "Ground plane (z = 0) - straight-ray approximation",
      "Intersects the aiming ray with z = 0, so the answer is always on the floor "
      "-- which is what a navigation goal means -- and can never land on the "
      "operator's own hand. It is an approximation: QarOS aims along a slightly "
      "CURVED ray and a straight line cannot reproduce that curve, so it differs "
      "from the Action Point by 'aim_off' in the gesture trace (centimetres in "
      "every session measured so far).",
      true},
    SpatialTargetOption{
      kFixedDepthTargetId,
      "Fixed depth, 2 m along the ray - local approximation",
      "LOCAL APPROXIMATION. Walks the aiming ray 2 m and stops. The honest answer "
      "when there is no geometry to hit, and the same caveat applies: QarOS's ray "
      "is curved, so this is a reconstruction rather than the runtime's own "
      "intersection.",
      true},
  };
  return kOptions;
}

int default_spatial_target_id()
{
  return kActionPointTargetId;
}

qar_ar_core::PickTarget spatial_target_from_id(int id)
{
  switch (id) {
    case kFixedDepthTargetId:
      return qar_ar_core::PickTarget::at_depth(2.0f);
    case kGroundPlaneTargetId:
      return qar_ar_core::PickTarget::ground();
    case kActionPointTargetId:
    default:
      // An unknown id lands on the DEFAULT target, not on a silently different
      // geometry -- so this case has to move whenever the default does.
      return qar_ar_core::PickTarget::action_point();
  }
}

const char * to_string(SpatialMode mode)
{
  switch (mode) {
    case SpatialMode::Off: return "off";
    case SpatialMode::Point: return "pick a point";
    case SpatialMode::NavGoal: return "set a navigation goal";
    case SpatialMode::InitialPose: return "set the initial pose";
    case SpatialMode::Area: return "select an area";
  }
  return "unknown";
}

SpatialPublishTarget publish_target_for(SpatialMode mode)
{
  switch (mode) {
    case SpatialMode::Point: return SpatialPublishTarget::ClickedPoint;
    case SpatialMode::NavGoal: return SpatialPublishTarget::GoalPose;
    case SpatialMode::InitialPose: return SpatialPublishTarget::InitialPose;
    case SpatialMode::Area: return SpatialPublishTarget::Area;
    case SpatialMode::Off: break;
  }
  // Off has no destination; a Custom config with no topic is refused rather
  // than publishing somewhere by accident.
  return SpatialPublishTarget::Custom;
}

SpatialInteractionController::SpatialInteractionController()
: point_(qar_ar_core::PickTarget::ground()),
  pose_(qar_ar_core::PickTarget::ground()),
  area_(qar_ar_core::PickTarget::ground())
{
}

void SpatialInteractionController::set_mode(SpatialMode mode)
{
  if (mode_ == mode) {
    return;
  }
  mode_ = mode;
  cancel();
}

void SpatialInteractionController::set_transform(
  const qar_ar_core::RoomToRosTransform & transform)
{
  transform_ = transform;
}

void SpatialInteractionController::set_target(const qar_ar_core::PickTarget & target)
{
  target_ = target;
  point_.set_target(target);
  pose_.set_target(target);
  area_.set_target(target);
  pending_ = SpatialPublishRequest{};
}

void SpatialInteractionController::set_area_shape(AreaShape shape)
{
  area_.set_shape(shape);
  pending_ = SpatialPublishRequest{};
}

AreaShape SpatialInteractionController::area_shape() const
{
  return area_.shape();
}

void SpatialInteractionController::set_frame_id(std::string frame_id)
{
  frame_id_ = std::move(frame_id);
}

void SpatialInteractionController::set_publish_override(
  SpatialMode mode, const SpatialPublishConfig & config)
{
  const std::size_t i = index_of(mode);
  has_override_[i] = true;
  override_[i] = config;
}

void SpatialInteractionController::clear_publish_override(SpatialMode mode)
{
  has_override_[index_of(mode)] = false;
}

SpatialPublishConfig SpatialInteractionController::publish_config(SpatialMode mode) const
{
  const std::size_t i = index_of(mode);
  SpatialPublishConfig config =
    has_override_[i] ? override_[i] : default_publish_config(publish_target_for(mode));
  // The frame is what the operator typed into the panel; the stamp is 0 (the
  // plugin never calls set_stamp), which tf resolves as the latest transform.
  config.frame_id = frame_id_;
  config.stamp = stamp_;
  return config;
}

bool SpatialInteractionController::handle_sample(
  const qar_ar_core::PanelPointerSample & sample)
{
  return handle_pointer(qar_ar_core::spatial_pointer_from_sample(sample, transform_));
}

bool SpatialInteractionController::handle_pointer(
  const qar_ar_core::SpatialPointer & pointer)
{
  // Counted BEFORE the gates, so "the adapter delivered N samples and the
  // controller saw N" is checkable. Every early return below names its gate.
  ++input_stats_.samples_seen;
  if (mode_ == SpatialMode::Off) {
    ++input_stats_.ignored_mode_off;
    input_stats_.last_drop_reason = "no pick mode armed (Mode is Off)";
    return false;
  }
  if (!pointer.valid) {
    ++input_stats_.ignored_not_spatial;
    input_stats_.last_drop_reason = "sample carried neither a room point nor a room ray";
    return false;
  }
  if (pending_.ok) {
    // A committed pick is waiting for Confirm or Cancel. Ignoring input until
    // then is what keeps "confirm before it moves" meaningful.
    ++input_stats_.ignored_pending;
    input_stats_.last_drop_reason = "a pick is already waiting for Confirm";
    return false;
  }
  bool changed = false;
  switch (mode_) {
    case SpatialMode::Point:
      changed = point_.update(pointer);
      if (point_.state() == PickState::Committed) {
        capture_pending();
      }
      break;
    case SpatialMode::NavGoal:
    case SpatialMode::InitialPose:
      changed = pose_.update(pointer);
      if (pose_.state() == PickState::Committed) {
        capture_pending();
      }
      break;
    case SpatialMode::Area:
      changed = area_.update(pointer);
      if (area_.state() == PickState::Committed) {
        capture_pending();
      }
      break;
    case SpatialMode::Off:
      break;
  }
  if (!qar_ar_core::gesture_commits_pick(pointer.gesture)) {
    ++input_stats_.hover_previews;
  }
  if (changed) {
    ++input_stats_.samples_accepted;
  } else {
    // The session refused it. The two causes look identical from here, so ask
    // the geometry: if the pointer does not resolve against the current target
    // the resolver says exactly why ("ray is parallel to the target plane",
    // "target plane is behind the pointer"); if it does resolve, the phase was
    // the problem. Only on the drop path, so a working stream pays nothing.
    ++input_stats_.ignored_by_session;
    const qar_ar_core::SpatialPoint probe =
      qar_ar_core::resolve_pick_point(target_, pointer);
    if (!probe.valid && !probe.error.empty()) {
      input_stats_.last_drop_reason = probe.error;
      // Only gestures that would COMMIT: a hover over empty space is every hand
      // wave between two targets, and counting those at ~100 Hz would bury the
      // one event this counter exists for -- a click on empty space.
      if (probe.refused_no_intersection &&
        qar_ar_core::gesture_commits_pick(pointer.gesture))
      {
        ++input_stats_.refused_no_intersection;
      }
    } else if (!qar_ar_core::gesture_commits_pick(pointer.gesture)) {
      // Not a fault: a hover previews and never commits, and a repeated hover
      // that has not moved does not even change the preview.
      input_stats_.last_drop_reason =
        std::string("a ") + qar_ar_core::to_string(pointer.gesture) +
        " previews only; it never commits a pick (tap to commit)";
    } else {
      input_stats_.last_drop_reason = std::string("phase ") +
        qar_ar_core::to_string(pointer.phase) + " did not advance the pick (out of order)";
    }
  }
  return changed;
}

bool SpatialInteractionController::close_polygon()
{
  if (mode_ != SpatialMode::Area || pending_.ok) {
    return false;
  }
  if (!area_.close_polygon()) {
    return false;
  }
  capture_pending();
  return true;
}

void SpatialInteractionController::capture_pending()
{
  const SpatialPublishConfig config = publish_config(mode_);
  SpatialPublishRequest request;
  switch (mode_) {
    case SpatialMode::Point:
      request = make_point_publish(config, point_.result());
      break;
    case SpatialMode::NavGoal:
    case SpatialMode::InitialPose: {
      qar_ar_core::NavGoalOptions options;
      options.frame_id = frame_id_;
      const auto goal = qar_ar_core::make_nav_goal(pose_.result(), options);
      request = make_nav_goal_publish(config, goal);
      break;
    }
    case SpatialMode::Area:
      request = make_area_publish(config, area_.result());
      break;
    case SpatialMode::Off:
      return;
  }
  if (request.ok) {
    pending_ = std::move(request);
    ++input_stats_.picks_committed;
    error_.clear();
  } else {
    // The pick cannot become a message (no topic configured, a type this cannot
    // build, a degenerate region). Say so and let the operator try again rather
    // than leaving a dead "pending" they cannot confirm.
    error_ = request.error;
    pending_ = SpatialPublishRequest{};
    reset_sessions();
  }
}

SpatialInteractionStatus SpatialInteractionController::status() const
{
  SpatialInteractionStatus status;
  status.mode = mode_;
  status.has_pending = pending_.ok;
  status.auto_confirm = auto_confirm_;
  status.error = error_;

  switch (mode_) {
    case SpatialMode::Off:
      return status;
    case SpatialMode::Point: {
      status.state = point_.state();
      const auto & point =
        point_.result().valid ? point_.result() : point_.preview();
      if (point.valid) {
        status.summary = "Point @ " + describe_position(point.position) +
          " (" + point.target_name + ")";
      }
      break;
    }
    case SpatialMode::NavGoal:
    case SpatialMode::InitialPose: {
      status.state = pose_.state();
      const auto & pose = pose_.result().valid ? pose_.result() : pose_.preview();
      if (pose.valid) {
        status.summary = std::string(
          mode_ == SpatialMode::NavGoal ? "Nav goal @ " : "Initial pose @ ") +
          describe_position(pose.position) + ", yaw " + describe_yaw(pose.yaw_rad) +
          (pose.heading_from_drag ? "" : " (default heading)") +
          " (" + pose.target_name + ")";
      }
      break;
    }
    case SpatialMode::Area: {
      status.state = area_.state();
      const auto & area = area_.result().valid ? area_.result() : area_.preview();
      if (area.valid) {
        status.summary = describe_area(area) + " (" + area.target_name + ")";
      } else if (area_.shape() == AreaShape::Polygon && !area_.polygon_vertices().empty()) {
        status.summary =
          "polygon: " + std::to_string(area_.polygon_vertices().size()) + " vertices so far";
      }
      break;
    }
  }
  if (status.has_pending) {
    status.summary += " - waiting for confirmation";
  }
  return status;
}

SpatialPublishRequest SpatialInteractionController::confirm()
{
  if (!pending_.ok) {
    SpatialPublishRequest empty;
    empty.error = "there is no pick to confirm";
    return empty;
  }
  SpatialPublishRequest out = std::move(pending_);
  pending_ = SpatialPublishRequest{};
  reset_sessions();
  return out;
}

void SpatialInteractionController::cancel()
{
  pending_ = SpatialPublishRequest{};
  error_.clear();
  reset_sessions();
}

void SpatialInteractionController::reset_sessions()
{
  point_.reset();
  pose_.reset();
  area_.reset();
}

}  // namespace qar_rviz_streaming
