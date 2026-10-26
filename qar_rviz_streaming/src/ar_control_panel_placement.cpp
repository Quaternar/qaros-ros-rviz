// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_control_panel_placement.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace qar_rviz_streaming
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

/// A gaze closer to vertical than this has no horizontal direction worth placing a panel
/// along: sin(5 degrees).
constexpr double kMinHorizontalGaze = 0.08715574274765817;

struct Vec3
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

bool finite(const QarVector3 & v)
{
  return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finite(const QarQuaternion & q)
{
  return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

/// Rotates @p v by the unit quaternion (qx, qy, qz, qw).
Vec3 rotate(double qx, double qy, double qz, double qw, const Vec3 & v)
{
  // t = 2 * (q.xyz x v); v' = v + w * t + q.xyz x t
  const double tx = 2.0 * (qy * v.z - qz * v.y);
  const double ty = 2.0 * (qz * v.x - qx * v.z);
  const double tz = 2.0 * (qx * v.y - qy * v.x);
  Vec3 out;
  out.x = v.x + qw * tx + (qy * tz - qz * ty);
  out.y = v.y + qw * ty + (qz * tx - qx * tz);
  out.z = v.z + qw * tz + (qx * ty - qy * tx);
  return out;
}

bool contains(const std::vector<std::string> & keys, const std::string & key)
{
  return std::find(keys.begin(), keys.end(), key) != keys.end();
}

}  // namespace

QarPose fallback_control_panel_pose(float top_y)
{
  QarPose pose{};
  pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  pose.position = QarVector3{0.7f, top_y, -1.2f};
  return pose;
}

bool head_from_eye_poses(
  const QarPose & left_eye, const QarPose & right_eye,
  const qar_ar_core::ScenePlacement & placement,
  QarVector3 & out_head_room, QarVector3 & out_gaze_room)
{
  if (!finite(left_eye.position) || !finite(right_eye.position) ||
    !finite(left_eye.orientation))
  {
    return false;
  }
  const QarQuaternion & q = left_eye.orientation;
  const double length = std::sqrt(
    static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
    static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w);
  if (length < 1e-6) {
    return false;
  }
  const Vec3 forward = rotate(
    q.x / length, q.y / length, q.z / length, q.w / length, Vec3{0.0, 0.0, -1.0});

  const QarVector3 head_scene{
    0.5f * (left_eye.position.x + right_eye.position.x),
    0.5f * (left_eye.position.y + right_eye.position.y),
    0.5f * (left_eye.position.z + right_eye.position.z)};
  const QarVector3 gaze_scene{
    static_cast<float>(forward.x), static_cast<float>(forward.y), static_cast<float>(forward.z)};

  out_head_room = qar_ar_core::scene_point_to_room(placement, head_scene);
  out_gaze_room = qar_ar_core::scene_direction_to_room(placement, gaze_scene);
  return true;
}

bool place_in_front_of(
  const QarVector3 & head_room, const QarVector3 & gaze_room,
  const ControlPanelPlacementParams & params, QarPose & out_pose)
{
  if (!finite(head_room) || !finite(gaze_room) || !std::isfinite(params.distance_m) ||
    !std::isfinite(params.right_m) || !std::isfinite(params.top_below_eyes_m))
  {
    return false;
  }
  const double gx = gaze_room.x;
  const double gy = gaze_room.y;
  const double gz = gaze_room.z;
  const double length = std::sqrt(gx * gx + gy * gy + gz * gz);
  const double flat = std::sqrt(gx * gx + gz * gz);
  if (length < 1e-6 || flat / length < kMinHorizontalGaze) {
    return false;
  }

  // The gaze flattened onto the horizontal plane, and the direction to its right (forward x up
  // in a right-handed frame with +Y up).
  const double forward_x = gx / flat;
  const double forward_z = gz / flat;
  const double right_x = -forward_z;
  const double right_z = forward_x;

  const double px = head_room.x + params.distance_m * forward_x + params.right_m * right_x;
  const double py = head_room.y - params.top_below_eyes_m;
  const double pz = head_room.z + params.distance_m * forward_z + params.right_m * right_z;

  // The horizontal unit vector from the panel to the head is where the front normal (-Z) has
  // to point. Turning about +Y by theta takes (0, 0, -1) to (-sin theta, 0, -cos theta).
  double nx = head_room.x - px;
  double nz = head_room.z - pz;
  const double n_length = std::sqrt(nx * nx + nz * nz);
  if (n_length < 1e-6) {
    nx = -forward_x;
    nz = -forward_z;
  } else {
    nx /= n_length;
    nz /= n_length;
  }
  double yaw = std::atan2(-nx, -nz);
  if (params.flip_facing) {
    yaw += kPi;
    if (yaw > kPi) {
      yaw -= 2.0 * kPi;
    }
  }

  out_pose.orientation = QarQuaternion{
    0.0f, static_cast<float>(std::sin(0.5 * yaw)), 0.0f, static_cast<float>(std::cos(0.5 * yaw))};
  out_pose.position = QarVector3{
    static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
  return true;
}

const char * to_string(ControlPanelMode mode)
{
  switch (mode) {
    case ControlPanelMode::Undecided:
      return "undecided";
    case ControlPanelMode::PerPeer:
      return "per_peer";
    case ControlPanelMode::Shared:
      return "shared";
  }
  return "undecided";
}

std::string control_panel_instance_key(const QarPeerId & peer)
{
  static const char * const kHex = "0123456789abcdef";
  std::string key = "peer:";
  for (const unsigned char byte : peer.data) {
    key.push_back(kHex[(byte >> 4) & 0x0F]);
    key.push_back(kHex[byte & 0x0F]);
  }
  return key;
}

ControlPanelPlacementPolicy::ControlPanelPlacementPolicy(double pose_wait_s, bool per_peer)
: pose_wait_s_(pose_wait_s), per_peer_(per_peer)
{
}

void ControlPanelPlacementPolicy::session_started(double now_s)
{
  if (started_) {
    return;
  }
  started_ = true;
  started_at_s_ = now_s;
  mode_ = per_peer_ ? ControlPanelMode::PerPeer : ControlPanelMode::Shared;
}

std::vector<ControlPanelInstanceRequest> ControlPanelPlacementPolicy::tick(
  double now_s, const std::vector<ControlPanelPeer> & peers_with_streams)
{
  std::vector<ControlPanelInstanceRequest> requests;
  if (!started_) {
    return requests;
  }

  std::vector<std::string> keys;
  keys.reserve(peers_with_streams.size());
  for (const ControlPanelPeer & peer : peers_with_streams) {
    keys.push_back(control_panel_instance_key(peer.peer));
  }

  if (per_peer_) {
    // An instance whose peer lost its stream is hidden, and shown again with the stream. This
    // runs before the new instances are added: a new instance starts out shown.
    for (const std::string & created : created_) {
      const bool was_present = contains(present_, created);
      const bool is_present = contains(keys, created);
      if (was_present && !is_present) {
        changes_.push_back(ControlPanelVisibilityChange{created, false});
      } else if (!was_present && is_present) {
        changes_.push_back(ControlPanelVisibilityChange{created, true});
      }
    }
    for (std::size_t i = 0; i < peers_with_streams.size(); ++i) {
      const ControlPanelPeer & peer = peers_with_streams[i];
      if (!peer.has_pose || contains(created_, keys[i])) {
        continue;
      }
      ControlPanelInstanceRequest request;
      request.instance_key = keys[i];
      request.everybody = false;
      request.peer = peer.peer;
      request.pose = peer.panel_pose;
      requests.push_back(request);
      created_.push_back(keys[i]);
    }
  } else if (!contains(created_, kSharedControlPanelKey)) {
    for (const ControlPanelPeer & peer : peers_with_streams) {
      if (!peer.has_pose) {
        continue;
      }
      ControlPanelInstanceRequest request;
      request.instance_key = kSharedControlPanelKey;
      request.everybody = true;
      request.peer = peer.peer;
      request.pose = peer.panel_pose;
      requests.push_back(request);
      break;
    }
    if (requests.empty() && now_s - started_at_s_ >= pose_wait_s_) {
      ControlPanelInstanceRequest request;
      request.instance_key = kSharedControlPanelKey;
      request.everybody = true;
      request.pose = fallback_control_panel_pose(fallback_top_y_);
      requests.push_back(request);
    }
    if (!requests.empty()) {
      created_.push_back(kSharedControlPanelKey);
    }
  }

  present_ = std::move(keys);
  return requests;
}

std::vector<ControlPanelVisibilityChange> ControlPanelPlacementPolicy::take_visibility_changes()
{
  std::vector<ControlPanelVisibilityChange> taken;
  taken.swap(changes_);
  return taken;
}

}  // namespace qar_rviz_streaming
