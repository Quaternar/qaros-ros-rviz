// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Where the AR control panel goes and for whom it is created: eye poses -> head and gaze in room
// space -> a yaw-only panel pose, and the policy that decides when an instance is created.
// Pure C++17: no session, no ROS graph, no Qt.

#include "qar_rviz_streaming/ar_control_panel_placement.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::ControlPanelInstanceRequest;
using qar_rviz_streaming::ControlPanelMode;
using qar_rviz_streaming::ControlPanelPeer;
using qar_rviz_streaming::ControlPanelPlacementParams;
using qar_rviz_streaming::ControlPanelPlacementPolicy;
using qar_rviz_streaming::ControlPanelVisibilityChange;
using qar_rviz_streaming::control_panel_instance_key;
using qar_rviz_streaming::head_from_eye_poses;
using qar_rviz_streaming::place_in_front_of;

constexpr double kPi = 3.14159265358979323846;

struct V3
{
  double x;
  double y;
  double z;
};

/// Rotates @p v by the unit quaternion @p q, written as a rotation matrix so that it is not the
/// formula of the code under test.
V3 rotate(const QarQuaternion & q, V3 v)
{
  const double x = q.x;
  const double y = q.y;
  const double z = q.z;
  const double w = q.w;
  return V3{
    (1 - 2 * (y * y + z * z)) * v.x + 2 * (x * y - z * w) * v.y + 2 * (x * z + y * w) * v.z,
    2 * (x * y + z * w) * v.x + (1 - 2 * (x * x + z * z)) * v.y + 2 * (y * z - x * w) * v.z,
    2 * (x * z - y * w) * v.x + 2 * (y * z + x * w) * v.y + (1 - 2 * (x * x + y * y)) * v.z};
}

double norm(const QarQuaternion & q)
{
  return std::sqrt(
    static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
    static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w);
}

/// A unit quaternion for a turn of @p yaw_rad about +Y.
QarQuaternion yaw_quaternion(double yaw_rad)
{
  return QarQuaternion{
    0.0f, static_cast<float>(std::sin(0.5 * yaw_rad)), 0.0f,
    static_cast<float>(std::cos(0.5 * yaw_rad))};
}

/// The horizontal unit vector from @p from to @p to.
V3 horizontal_direction(const QarVector3 & from, const QarVector3 & to)
{
  const double dx = static_cast<double>(to.x) - from.x;
  const double dz = static_cast<double>(to.z) - from.z;
  const double length = std::sqrt(dx * dx + dz * dz);
  return V3{dx / length, 0.0, dz / length};
}

QarPose identity_pose_at(float x, float y, float z)
{
  QarPose pose{};
  pose.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f};
  pose.position = QarVector3{x, y, z};
  return pose;
}

QarPeerId peer_id(unsigned char tag)
{
  QarPeerId id{};
  id.data[0] = tag;
  id.data[15] = tag;
  return id;
}

ControlPanelPeer peer(unsigned char tag, bool has_pose, float x = 0.0f)
{
  ControlPanelPeer entry;
  entry.peer = peer_id(tag);
  entry.has_pose = has_pose;
  entry.panel_pose = identity_pose_at(x, 1.5f, -1.0f);
  return entry;
}

const QarVector3 kHead{1.0f, 1.6f, -2.0f};

}  // namespace

// --- The pose ---------------------------------------------------------------------------------

TEST(ControlPanelPlacement, FacesTheHeadYawOnly)
{
  const ControlPanelPlacementParams params;
  EXPECT_FLOAT_EQ(params.distance_m, 1.0f);
  EXPECT_FLOAT_EQ(params.right_m, 0.35f);
  EXPECT_FLOAT_EQ(params.top_below_eyes_m, 0.10f);
  EXPECT_FALSE(params.flip_facing);

  // Looking along -Z: the panel is 1.0 m ahead, 0.35 m to the right (+X) and 0.10 m lower.
  QarPose pose{};
  ASSERT_TRUE(place_in_front_of(kHead, QarVector3{0.0f, 0.0f, -1.0f}, params, pose));
  EXPECT_NEAR(pose.position.x, 1.35f, 1e-6f);
  EXPECT_NEAR(pose.position.y, 1.5f, 1e-6f);
  EXPECT_NEAR(pose.position.z, -3.0f, 1e-6f);
  // Yaw only: the quaternion has a Y part and a W part and nothing else, and it is a unit.
  EXPECT_EQ(pose.orientation.x, 0.0f);
  EXPECT_EQ(pose.orientation.z, 0.0f);
  EXPECT_NEAR(norm(pose.orientation), 1.0, 1e-6);
  // The panel faces the head, which is not straight ahead of the panel: it is 0.35 m to its
  // left. The turn is atan2(-n.x, -n.z) with n the horizontal direction from panel to head.
  const double len = std::sqrt(0.35 * 0.35 + 1.0);
  const double yaw = std::atan2(0.35 / len, -1.0 / len);
  EXPECT_NEAR(yaw, 2.8049, 1e-3) << "about 160.7 degrees";
  EXPECT_NEAR(pose.orientation.y, std::sin(0.5 * yaw), 1e-6);
  EXPECT_NEAR(pose.orientation.w, std::cos(0.5 * yaw), 1e-6);

  // The pitch of the gaze does not matter: only its horizontal part is used.
  QarPose pitched{};
  ASSERT_TRUE(place_in_front_of(kHead, QarVector3{0.0f, -0.6f, -0.8f}, params, pitched));
  EXPECT_NEAR(pitched.position.x, pose.position.x, 1e-6f);
  EXPECT_NEAR(pitched.position.y, pose.position.y, 1e-6f);
  EXPECT_NEAR(pitched.position.z, pose.position.z, 1e-6f);
  EXPECT_NEAR(pitched.orientation.y, pose.orientation.y, 1e-6f);
  // ... and neither does the length of the gaze vector.
  QarPose scaled{};
  ASSERT_TRUE(place_in_front_of(kHead, QarVector3{0.0f, 0.0f, -37.0f}, params, scaled));
  EXPECT_NEAR(scaled.position.z, pose.position.z, 1e-5f);

  // Looking along +X: ahead is +X, to the right is +Z.
  QarPose east{};
  ASSERT_TRUE(place_in_front_of(kHead, QarVector3{1.0f, 0.0f, 0.0f}, params, east));
  EXPECT_NEAR(east.position.x, 2.0f, 1e-6f);
  EXPECT_NEAR(east.position.z, -1.65f, 1e-6f);

  // Whatever way the head looks, the panel stands upright (a turn about +Y and nothing else)
  // and is as far ahead and aside as asked.
  for (int step = 0; step < 16; ++step) {
    const double angle = step * kPi / 8.0;
    const QarVector3 gaze{
      static_cast<float>(std::sin(angle)), 0.2f, static_cast<float>(-std::cos(angle))};
    QarPose turned{};
    ASSERT_TRUE(place_in_front_of(kHead, gaze, params, turned)) << step;
    EXPECT_EQ(turned.orientation.x, 0.0f) << step;
    EXPECT_EQ(turned.orientation.z, 0.0f) << step;
    EXPECT_NEAR(norm(turned.orientation), 1.0, 1e-6) << step;
    const double dx = static_cast<double>(turned.position.x) - kHead.x;
    const double dz = static_cast<double>(turned.position.z) - kHead.z;
    EXPECT_NEAR(std::sqrt(dx * dx + dz * dz), std::sqrt(1.0 + 0.35 * 0.35), 1e-5) << step;
    EXPECT_NEAR(turned.position.y, 1.5f, 1e-6f) << step;
    const V3 up = rotate(turned.orientation, V3{0.0, 1.0, 0.0});
    EXPECT_NEAR(up.y, 1.0, 1e-6) << step;
  }

  // The offsets are the parameters'.
  ControlPanelPlacementParams custom;
  custom.distance_m = 2.0f;
  custom.right_m = 0.0f;
  custom.top_below_eyes_m = 0.5f;
  QarPose near_pose{};
  ASSERT_TRUE(place_in_front_of(kHead, QarVector3{0.0f, 0.0f, -1.0f}, custom, near_pose));
  EXPECT_NEAR(near_pose.position.x, 1.0f, 1e-6f);
  EXPECT_NEAR(near_pose.position.y, 1.1f, 1e-6f);
  EXPECT_NEAR(near_pose.position.z, -4.0f, 1e-6f);
  // Straight ahead of the panel: the head is on the +Z side, so the turn is 180 degrees.
  EXPECT_NEAR(std::fabs(near_pose.orientation.y), 1.0f, 1e-6f);
  EXPECT_NEAR(near_pose.orientation.w, 0.0f, 1e-6f);
}

TEST(ControlPanelPlacement, FrontNormalPointsAtTheViewer)
{
  // The front normal of a QarOS panel is its local -Z axis. It must point at the viewer: along
  // the horizontal direction from the panel to the head.
  const ControlPanelPlacementParams params;
  const QarVector3 heads[] = {{0.0f, 1.6f, 0.0f}, {1.0f, 1.6f, -2.0f}, {-3.0f, 1.2f, 4.0f}};
  for (const QarVector3 & head : heads) {
    for (int step = 0; step < 12; ++step) {
      const double angle = step * kPi / 6.0 + 0.1;
      const QarVector3 gaze{
        static_cast<float>(std::sin(angle)), -0.1f, static_cast<float>(-std::cos(angle))};
      QarPose pose{};
      ASSERT_TRUE(place_in_front_of(head, gaze, params, pose));
      const V3 normal = rotate(pose.orientation, V3{0.0, 0.0, -1.0});
      const V3 to_head = horizontal_direction(pose.position, head);
      EXPECT_NEAR(normal.x, to_head.x, 1e-5) << step;
      EXPECT_NEAR(normal.y, 0.0, 1e-5) << step;
      EXPECT_NEAR(normal.z, to_head.z, 1e-5) << step;
    }
  }

  // The example that fixes the convention: a viewer at (0, 1.6, 0) looking along -Z gets the
  // panel at (0.35, 1.5, -1.0). Its front normal points back at the viewer, to (-0.33, 0, 0.94)
  // ...
  QarPose pose{};
  ASSERT_TRUE(
    place_in_front_of(QarVector3{0.0f, 1.6f, 0.0f}, QarVector3{0.0f, 0.0f, -1.0f}, params, pose));
  EXPECT_NEAR(pose.position.x, 0.35f, 1e-6f);
  EXPECT_NEAR(pose.position.z, -1.0f, 1e-6f);
  const V3 normal = rotate(pose.orientation, V3{0.0, 0.0, -1.0});
  EXPECT_NEAR(normal.x, -0.35 / std::sqrt(1.1225), 1e-5);
  EXPECT_NEAR(normal.z, 1.0 / std::sqrt(1.1225), 1e-5);
  // ... whereas the identity orientation, which the fixed panels of the plugin use, has its front
  // normal along -Z: away from this viewer, and toward a viewer that stands at z < -1.
  const V3 identity_normal = rotate(QarQuaternion{0.0f, 0.0f, 0.0f, 1.0f}, V3{0.0, 0.0, -1.0});
  EXPECT_DOUBLE_EQ(identity_normal.z, -1.0);
  EXPECT_LT(identity_normal.x * normal.x + identity_normal.z * normal.z, 0.0);
}

TEST(ControlPanelPlacement, VerticalGazeIsRefused)
{
  const ControlPanelPlacementParams params;
  QarPose pose = identity_pose_at(7.0f, 7.0f, 7.0f);
  EXPECT_FALSE(place_in_front_of(kHead, QarVector3{0.0f, 1.0f, 0.0f}, params, pose));
  EXPECT_FALSE(place_in_front_of(kHead, QarVector3{0.0f, -1.0f, 0.0f}, params, pose));
  EXPECT_FALSE(place_in_front_of(kHead, QarVector3{0.0f, 0.0f, 0.0f}, params, pose));
  EXPECT_FLOAT_EQ(pose.position.x, 7.0f) << "a refused placement leaves the pose alone";
  EXPECT_FLOAT_EQ(pose.orientation.w, 1.0f);

  // Within about 5 degrees of vertical there is no horizontal direction worth using; a little
  // further out there is.
  const auto tilted_from_vertical = [](double degrees) {
      const double a = degrees * kPi / 180.0;
      return QarVector3{0.0f, static_cast<float>(std::cos(a)), static_cast<float>(-std::sin(a))};
    };
  EXPECT_FALSE(place_in_front_of(kHead, tilted_from_vertical(1.0), params, pose));
  EXPECT_FALSE(place_in_front_of(kHead, tilted_from_vertical(4.0), params, pose));
  EXPECT_TRUE(place_in_front_of(kHead, tilted_from_vertical(6.0), params, pose));
  EXPECT_TRUE(place_in_front_of(kHead, tilted_from_vertical(30.0), params, pose));
  // Looking up or down by the same amount is the same.
  QarVector3 down = tilted_from_vertical(4.0);
  down.y = -down.y;
  EXPECT_FALSE(place_in_front_of(kHead, down, params, pose));
  down = tilted_from_vertical(6.0);
  down.y = -down.y;
  EXPECT_TRUE(place_in_front_of(kHead, down, params, pose));

  // Numbers that are not numbers.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(place_in_front_of(kHead, QarVector3{nan, 0.0f, -1.0f}, params, pose));
  EXPECT_FALSE(place_in_front_of(kHead, QarVector3{0.0f, inf, -1.0f}, params, pose));
  const QarVector3 ahead{0.0f, 0.0f, -1.0f};
  EXPECT_FALSE(place_in_front_of(QarVector3{nan, 0.0f, 0.0f}, ahead, params, pose));
  EXPECT_FALSE(place_in_front_of(QarVector3{0.0f, -inf, 0.0f}, ahead, params, pose));
  ControlPanelPlacementParams bad;
  bad.distance_m = nan;
  EXPECT_FALSE(place_in_front_of(kHead, QarVector3{0.0f, 0.0f, -1.0f}, bad, pose));
}

TEST(ControlPanelPlacement, HeadFromEyesUsesTheScenePlacement)
{
  QarPose left = identity_pose_at(-0.03f, 1.6f, 0.0f);
  QarPose right = identity_pose_at(0.03f, 1.6f, 0.0f);

  // No placement: the head is the midpoint of the eyes, the gaze the left eye's forward axis.
  QarVector3 head{};
  QarVector3 gaze{};
  ASSERT_TRUE(head_from_eye_poses(left, right, qar_ar_core::ScenePlacement{}, head, gaze));
  EXPECT_NEAR(head.x, 0.0f, 1e-6f);
  EXPECT_NEAR(head.y, 1.6f, 1e-6f);
  EXPECT_NEAR(head.z, 0.0f, 1e-6f);
  EXPECT_NEAR(gaze.x, 0.0f, 1e-6f);
  EXPECT_NEAR(gaze.y, 0.0f, 1e-6f);
  EXPECT_NEAR(gaze.z, -1.0f, 1e-6f);

  // The gaze is the LEFT eye's: a left eye turned 90 degrees to the left (about +Y) looks at -X.
  left.orientation = yaw_quaternion(0.5 * kPi);
  ASSERT_TRUE(head_from_eye_poses(left, right, qar_ar_core::ScenePlacement{}, head, gaze));
  EXPECT_NEAR(gaze.x, -1.0f, 1e-6f);
  EXPECT_NEAR(gaze.z, 0.0f, 1e-6f);
  // An orientation that is not normalised is normalised.
  const float quarter_turn = 0.25f * static_cast<float>(kPi);
  left.orientation =
    QarQuaternion{0.0f, 2.0f * std::sin(quarter_turn), 0.0f, 2.0f * std::cos(quarter_turn)};
  ASSERT_TRUE(head_from_eye_poses(left, right, qar_ar_core::ScenePlacement{}, head, gaze));
  EXPECT_NEAR(gaze.x, -1.0f, 1e-5f);

  // A scene that QarOS displays scaled, rotated and moved: the head follows all three, the
  // gaze only the rotation.
  left = identity_pose_at(0.9f, 1.6f, -0.4f);
  right = identity_pose_at(1.1f, 1.6f, -0.4f);
  qar_ar_core::ScenePlacement placement;
  placement.known = true;
  placement.app_scale = 0.5f;
  placement.volume_pose.orientation = yaw_quaternion(0.5 * kPi);
  placement.volume_pose.position = QarVector3{1.0f, 0.0f, 2.0f};
  ASSERT_TRUE(head_from_eye_poses(left, right, placement, head, gaze));
  // Scene (1.0, 1.6, -0.4) -> scaled (0.5, 0.8, -0.2) -> turned by 90 degrees about +Y
  // (x, y, z) -> (z, y, -x) = (-0.2, 0.8, -0.5) -> moved to (0.8, 0.8, 1.5).
  EXPECT_NEAR(head.x, 0.8f, 1e-5f);
  EXPECT_NEAR(head.y, 0.8f, 1e-5f);
  EXPECT_NEAR(head.z, 1.5f, 1e-5f);
  // Scene forward (0, 0, -1) -> turned (-1, 0, 0), still a unit vector.
  EXPECT_NEAR(gaze.x, -1.0f, 1e-5f);
  EXPECT_NEAR(gaze.y, 0.0f, 1e-5f);
  EXPECT_NEAR(gaze.z, 0.0f, 1e-5f);
  // The same as the frame helpers give for the same input.
  const QarVector3 midpoint{1.0f, 1.6f, -0.4f};
  const QarVector3 expected_head = qar_ar_core::scene_point_to_room(placement, midpoint);
  const QarVector3 expected_gaze =
    qar_ar_core::scene_direction_to_room(placement, QarVector3{0.0f, 0.0f, -1.0f});
  EXPECT_NEAR(head.x, expected_head.x, 1e-6f);
  EXPECT_NEAR(head.z, expected_head.z, 1e-6f);
  EXPECT_NEAR(gaze.x, expected_gaze.x, 1e-6f);

  // The panel then stands in front of the head in room space.
  QarPose pose{};
  ASSERT_TRUE(place_in_front_of(head, gaze, ControlPanelPlacementParams{}, pose));
  EXPECT_NEAR(pose.position.x, 0.8f - 1.0f, 1e-5f) << "one metre ahead along -X";
  EXPECT_NEAR(pose.position.z, 1.5f - 0.35f, 1e-5f) << "0.35 m to the right of -X is -Z";

  // Poses that cannot be used.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  QarPose broken = identity_pose_at(nan, 1.6f, 0.0f);
  EXPECT_FALSE(head_from_eye_poses(broken, right, qar_ar_core::ScenePlacement{}, head, gaze));
  EXPECT_FALSE(head_from_eye_poses(left, broken, qar_ar_core::ScenePlacement{}, head, gaze));
  broken = identity_pose_at(0.0f, 1.6f, 0.0f);
  broken.orientation = QarQuaternion{0.0f, 0.0f, 0.0f, 0.0f};
  EXPECT_FALSE(head_from_eye_poses(broken, right, qar_ar_core::ScenePlacement{}, head, gaze));
  broken.orientation = QarQuaternion{nan, 0.0f, 0.0f, 1.0f};
  EXPECT_FALSE(head_from_eye_poses(broken, right, qar_ar_core::ScenePlacement{}, head, gaze));
}

TEST(ControlPanelPlacement, FallbackPose)
{
  const QarPose pose = qar_rviz_streaming::fallback_control_panel_pose();
  EXPECT_FLOAT_EQ(pose.position.x, 0.7f);
  EXPECT_FLOAT_EQ(pose.position.y, 1.5f);
  EXPECT_FLOAT_EQ(pose.position.z, -1.2f);
  EXPECT_FLOAT_EQ(pose.orientation.x, 0.0f);
  EXPECT_FLOAT_EQ(pose.orientation.y, 0.0f);
  EXPECT_FLOAT_EQ(pose.orientation.z, 0.0f);
  EXPECT_FLOAT_EQ(pose.orientation.w, 1.0f);
}

TEST(ControlPanelPlacement, FlipTurnsTheYawBy180)
{
  ControlPanelPlacementParams flipped;
  flipped.flip_facing = true;
  const ControlPanelPlacementParams plain;
  for (int step = 0; step < 16; ++step) {
    const double angle = step * kPi / 8.0 + 0.05;
    const QarVector3 gaze{
      static_cast<float>(std::sin(angle)), -0.2f, static_cast<float>(-std::cos(angle))};
    QarPose a{};
    QarPose b{};
    ASSERT_TRUE(place_in_front_of(kHead, gaze, plain, a)) << step;
    ASSERT_TRUE(place_in_front_of(kHead, gaze, flipped, b)) << step;
    // The panel stays where it is ...
    EXPECT_FLOAT_EQ(a.position.x, b.position.x) << step;
    EXPECT_FLOAT_EQ(a.position.y, b.position.y) << step;
    EXPECT_FLOAT_EQ(a.position.z, b.position.z) << step;
    // ... and turns its front away from the head.
    const V3 front_a = rotate(a.orientation, V3{0.0, 0.0, -1.0});
    const V3 front_b = rotate(b.orientation, V3{0.0, 0.0, -1.0});
    EXPECT_NEAR(front_b.x, -front_a.x, 1e-5) << step;
    EXPECT_NEAR(front_b.y, 0.0, 1e-5) << step;
    EXPECT_NEAR(front_b.z, -front_a.z, 1e-5) << step;
    // Still a yaw and still a unit.
    EXPECT_EQ(b.orientation.x, 0.0f) << step;
    EXPECT_EQ(b.orientation.z, 0.0f) << step;
    EXPECT_NEAR(norm(b.orientation), 1.0, 1e-6) << step;
    // The yaw is 180 degrees further (or 180 degrees less, which is the same turn).
    const double yaw_a = 2.0 * std::atan2(a.orientation.y, a.orientation.w);
    const double yaw_b = 2.0 * std::atan2(b.orientation.y, b.orientation.w);
    double difference = std::fmod(std::fabs(yaw_b - yaw_a), 2.0 * kPi);
    EXPECT_NEAR(difference, kPi, 1e-4) << step;
  }
}

// --- The policy -------------------------------------------------------------------------------

TEST(ControlPanelPlacementPolicy, InstanceKeyIsPeerAndHex)
{
  QarPeerId id{};
  for (int i = 0; i < 16; ++i) {
    id.data[i] = static_cast<uint8_t>(i * 17);
  }
  EXPECT_EQ(control_panel_instance_key(id), "peer:00112233445566778899aabbccddeeff");
  EXPECT_EQ(control_panel_instance_key(QarPeerId{}), "peer:00000000000000000000000000000000");
  EXPECT_NE(control_panel_instance_key(peer_id(1)), control_panel_instance_key(peer_id(2)));
  EXPECT_STREQ(qar_rviz_streaming::kSharedControlPanelKey, "everybody");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelMode::Undecided), "undecided");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelMode::PerPeer), "per_peer");
  EXPECT_STREQ(qar_rviz_streaming::to_string(ControlPanelMode::Shared), "shared");
}

TEST(ControlPanelPlacementPolicy, PerPeerInstanceAtFirstPose)
{
  ControlPanelPlacementPolicy policy;  // 20 s, per peer
  EXPECT_EQ(policy.mode(), ControlPanelMode::Undecided);

  // Before the session started nothing is created, whatever the peers have.
  EXPECT_TRUE(policy.tick(5.0, {peer(1, true, 0.25f)}).empty());

  policy.session_started(100.0);
  EXPECT_EQ(policy.mode(), ControlPanelMode::PerPeer);
  const std::vector<ControlPanelInstanceRequest> requests =
    policy.tick(101.0, {peer(1, true, 0.25f)});
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].instance_key, control_panel_instance_key(peer_id(1)));
  EXPECT_EQ(requests[0].instance_key.compare(0, 5, "peer:"), 0);
  EXPECT_EQ(requests[0].instance_key.size(), 5u + 32u);
  EXPECT_FALSE(requests[0].everybody) << "visible to that peer only";
  EXPECT_EQ(control_panel_instance_key(requests[0].peer), requests[0].instance_key);
  EXPECT_FLOAT_EQ(requests[0].pose.position.x, 0.25f) << "the peer's own panel pose";
  EXPECT_FLOAT_EQ(requests[0].pose.position.y, 1.5f);
  EXPECT_FLOAT_EQ(requests[0].pose.orientation.w, 1.0f);
  EXPECT_TRUE(policy.take_visibility_changes().empty());
}

TEST(ControlPanelPlacementPolicy, NoInstanceWithoutPoseInPerPeerMode)
{
  ControlPanelPlacementPolicy policy;
  policy.session_started(0.0);
  // No peer at all: no instance, however long the session runs. There is no timeout and no
  // shared instance in this mode.
  EXPECT_TRUE(policy.tick(1.0, {}).empty());
  EXPECT_TRUE(policy.tick(20.0, {}).empty());
  EXPECT_TRUE(policy.tick(21.0, {}).empty());
  EXPECT_TRUE(policy.tick(10000.0, {}).empty());
  // A peer that has a stream but no pose yet: still none.
  EXPECT_TRUE(policy.tick(10001.0, {peer(1, false)}).empty());
  EXPECT_TRUE(policy.tick(20000.0, {peer(1, false)}).empty());
  // ... until its pose arrives, however late.
  const std::vector<ControlPanelInstanceRequest> late = policy.tick(30000.0, {peer(1, true)});
  ASSERT_EQ(late.size(), 1u);
  EXPECT_FALSE(late[0].everybody);
  EXPECT_TRUE(policy.take_visibility_changes().empty());
}

TEST(ControlPanelPlacementPolicy, LaterPeersGetTheirOwn)
{
  ControlPanelPlacementPolicy policy;
  policy.session_started(0.0);
  const auto first = policy.tick(1.0, {peer(1, true, 0.1f)});
  ASSERT_EQ(first.size(), 1u);
  EXPECT_FLOAT_EQ(first[0].pose.position.x, 0.1f);

  // A second peer without a pose: nothing yet.
  EXPECT_TRUE(policy.tick(2.0, {peer(1, true, 0.1f), peer(2, false)}).empty());
  // Its pose arrives: it gets an instance of its own, at its own pose.
  const auto second = policy.tick(3.0, {peer(1, true, 0.1f), peer(2, true, 0.2f)});
  ASSERT_EQ(second.size(), 1u);
  EXPECT_EQ(second[0].instance_key, control_panel_instance_key(peer_id(2)));
  EXPECT_NE(second[0].instance_key, first[0].instance_key);
  EXPECT_FLOAT_EQ(second[0].pose.position.x, 0.2f);
  EXPECT_FALSE(second[0].everybody);

  // Two peers whose poses arrive in the same tick get one instance each, in list order.
  const auto both = policy.tick(
    4.0, {peer(1, true, 0.1f), peer(2, true, 0.2f), peer(3, true, 0.3f), peer(4, true, 0.4f)});
  ASSERT_EQ(both.size(), 2u);
  EXPECT_EQ(both[0].instance_key, control_panel_instance_key(peer_id(3)));
  EXPECT_EQ(both[1].instance_key, control_panel_instance_key(peer_id(4)));
  EXPECT_TRUE(policy.take_visibility_changes().empty());

  // A panel is placed once: a changed pose of a peer that has its instance moves nothing.
  EXPECT_TRUE(policy.tick(5.0, {peer(1, true, 9.0f)}).empty());
}

TEST(ControlPanelPlacementPolicy, SharedUsesFirstPose)
{
  ControlPanelPlacementPolicy policy(20.0, false);
  policy.session_started(0.0);
  EXPECT_EQ(policy.mode(), ControlPanelMode::Shared);

  // Peers without a pose do not create it.
  EXPECT_TRUE(policy.tick(1.0, {peer(1, false)}).empty());
  EXPECT_TRUE(policy.tick(2.0, {}).empty());

  // The first peer of the list that has a pose decides.
  const auto requests =
    policy.tick(3.0, {peer(1, false), peer(2, true, 0.2f), peer(3, true, 0.3f)});
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].instance_key, "everybody");
  EXPECT_TRUE(requests[0].everybody) << "visible to every peer";
  EXPECT_FLOAT_EQ(requests[0].pose.position.x, 0.2f);
  EXPECT_EQ(control_panel_instance_key(requests[0].peer), control_panel_instance_key(peer_id(2)))
    << "placed in front of that peer";
  EXPECT_EQ(policy.mode(), ControlPanelMode::Shared);
}

TEST(ControlPanelPlacementPolicy, SharedFallsBackAfterTheWait)
{
  ControlPanelPlacementPolicy policy(20.0, false);
  policy.session_started(5.0);
  EXPECT_TRUE(policy.tick(5.0, {}).empty());
  EXPECT_TRUE(policy.tick(24.9, {peer(1, false)}).empty());
  const auto requests = policy.tick(25.0, {peer(1, false)});
  ASSERT_EQ(requests.size(), 1u);
  EXPECT_EQ(requests[0].instance_key, "everybody");
  EXPECT_TRUE(requests[0].everybody);
  const QarPose fallback = qar_rviz_streaming::fallback_control_panel_pose();
  EXPECT_FLOAT_EQ(requests[0].pose.position.x, fallback.position.x);
  EXPECT_FLOAT_EQ(requests[0].pose.position.y, fallback.position.y);
  EXPECT_FLOAT_EQ(requests[0].pose.position.z, fallback.position.z);
  EXPECT_FLOAT_EQ(requests[0].pose.orientation.w, 1.0f);

  // No peer at all is no reason to wait longer.
  ControlPanelPlacementPolicy empty_room(2.0, false);
  empty_room.session_started(0.0);
  EXPECT_TRUE(empty_room.tick(1.9, {}).empty());
  EXPECT_EQ(empty_room.tick(2.0, {}).size(), 1u);

  // A pose that arrives in the very tick of the deadline wins over the fallback.
  ControlPanelPlacementPolicy on_time(2.0, false);
  on_time.session_started(0.0);
  const auto at_deadline = on_time.tick(2.0, {peer(7, true, 0.7f)});
  ASSERT_EQ(at_deadline.size(), 1u);
  EXPECT_FLOAT_EQ(at_deadline[0].pose.position.x, 0.7f);

  // The wait is counted from the first session_started; a second call is ignored.
  ControlPanelPlacementPolicy twice(10.0, false);
  twice.session_started(0.0);
  twice.session_started(8.0);
  EXPECT_EQ(twice.tick(10.0, {}).size(), 1u);
}

TEST(ControlPanelPlacementPolicy, SharedIgnoresLaterPeers)
{
  // The fallback was used: later peers, with poses, get nothing more.
  {
    ControlPanelPlacementPolicy policy(1.0, false);
    policy.session_started(0.0);
    ASSERT_EQ(policy.tick(1.0, {}).size(), 1u);
    EXPECT_TRUE(policy.tick(2.0, {peer(1, true, 0.1f)}).empty());
    EXPECT_TRUE(policy.tick(3.0, {peer(1, true, 0.1f), peer(2, true, 0.2f)}).empty());
    EXPECT_TRUE(policy.take_visibility_changes().empty());
  }
  // A peer's pose was used: later peers get nothing more either.
  {
    ControlPanelPlacementPolicy policy(20.0, false);
    policy.session_started(0.0);
    ASSERT_EQ(policy.tick(1.0, {peer(1, true, 0.1f)}).size(), 1u);
    EXPECT_TRUE(policy.tick(2.0, {peer(1, true, 0.1f), peer(2, true, 0.2f)}).empty());
    EXPECT_TRUE(policy.tick(100.0, {peer(3, true, 0.3f)}).empty());
    // The shared panel stays when its peer leaves: nothing is hidden or shown.
    EXPECT_TRUE(policy.tick(101.0, {}).empty());
    EXPECT_TRUE(policy.tick(102.0, {peer(1, true, 0.1f)}).empty());
    EXPECT_TRUE(policy.take_visibility_changes().empty());
  }
}

TEST(ControlPanelPlacementPolicy, EachInstanceIsCreatedOnce)
{
  // Per peer: a peer whose instance exists gets no second one, whatever happens to its stream.
  ControlPanelPlacementPolicy policy;
  policy.session_started(0.0);
  ASSERT_EQ(policy.tick(1.0, {peer(1, true), peer(2, true)}).size(), 2u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(policy.tick(2.0 + i, {peer(1, true), peer(2, true)}).empty());
  }
  EXPECT_TRUE(policy.tick(10.0, {peer(2, true)}).empty()) << "peer 1 left";
  EXPECT_TRUE(policy.tick(11.0, {peer(1, true), peer(2, true)}).empty()) << "peer 1 returned";
  EXPECT_TRUE(policy.tick(12.0, {peer(1, false), peer(2, true)}).empty()) << "a new stream";
  // The same peer twice in one list is one peer.
  ControlPanelPlacementPolicy twice;
  twice.session_started(0.0);
  EXPECT_EQ(twice.tick(1.0, {peer(5, true), peer(5, true)}).size(), 1u);

  // Shared: one instance for the whole session.
  ControlPanelPlacementPolicy shared(3.0, false);
  shared.session_started(0.0);
  ASSERT_EQ(shared.tick(1.0, {peer(1, true)}).size(), 1u);
  for (double t = 2.0; t < 60.0; t += 7.0) {
    EXPECT_TRUE(shared.tick(t, {peer(1, true)}).empty());
    EXPECT_TRUE(shared.tick(t + 1.0, {}).empty());
  }
}

TEST(ControlPanelPlacementPolicy, LeaveHidesReturnShows)
{
  ControlPanelPlacementPolicy policy;
  policy.session_started(0.0);
  ASSERT_EQ(policy.tick(1.0, {peer(1, true), peer(2, false)}).size(), 1u);
  EXPECT_TRUE(policy.take_visibility_changes().empty()) << "a new instance starts out shown";
  const std::string key = control_panel_instance_key(peer_id(1));

  // The stream of peer 1 closes: its instance is hidden. Peer 2 never had an instance, so its
  // stream going away is no news.
  EXPECT_TRUE(policy.tick(2.0, {}).empty());
  std::vector<ControlPanelVisibilityChange> changes = policy.take_visibility_changes();
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].instance_key, key);
  EXPECT_FALSE(changes[0].visible);
  EXPECT_TRUE(policy.take_visibility_changes().empty()) << "taken once";

  // Nothing new while it stays away.
  EXPECT_TRUE(policy.tick(3.0, {}).empty());
  EXPECT_TRUE(policy.take_visibility_changes().empty());

  // It returns: the same instance is shown again, and none is created.
  EXPECT_TRUE(policy.tick(4.0, {peer(1, false)}).empty());
  changes = policy.take_visibility_changes();
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].instance_key, key);
  EXPECT_TRUE(changes[0].visible);

  // Leaving and returning between two takes gives both changes, in order.
  EXPECT_TRUE(policy.tick(5.0, {}).empty());
  EXPECT_TRUE(policy.tick(6.0, {peer(1, true)}).empty());
  changes = policy.take_visibility_changes();
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_FALSE(changes[0].visible);
  EXPECT_TRUE(changes[1].visible);

  // Two instances change independently.
  ASSERT_EQ(policy.tick(7.0, {peer(1, true), peer(2, true)}).size(), 1u);
  EXPECT_TRUE(policy.tick(8.0, {peer(2, true)}).empty());
  changes = policy.take_visibility_changes();
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].instance_key, key);
  EXPECT_FALSE(changes[0].visible);
}

// --- The height of the fallback pose follows the choice of the panel ------------------------

TEST(ControlPanelFallback, TheTopEdgeIsAParameterAndTheDefaultIsTheFixedHeight)
{
  EXPECT_EQ(qar_rviz_streaming::kFallbackControlPanelTopM, 1.5f);
  const QarPose standard = qar_rviz_streaming::fallback_control_panel_pose();
  EXPECT_EQ(standard.position.x, 0.7f);
  EXPECT_EQ(standard.position.y, 1.5f);
  EXPECT_EQ(standard.position.z, -1.2f);
  const QarPose lifted = qar_rviz_streaming::fallback_control_panel_pose(2.7f);
  EXPECT_EQ(lifted.position.x, 0.7f);
  EXPECT_EQ(lifted.position.y, 2.7f);
  EXPECT_EQ(lifted.position.z, -1.2f);
  EXPECT_EQ(lifted.orientation.w, 1.0f);
}

TEST(ControlPanelFallback, ASharedInstanceWithoutAPoseIsCreatedAtTheHeightTheCallerSaid)
{
  using qar_rviz_streaming::ControlPanelPlacementPolicy;
  // Shared mode, no peer with a pose, the wait is over: the instance hangs at the fallback pose.
  const auto fallback_y = [](bool set, float top_y) {
      ControlPanelPlacementPolicy policy(5.0, false);
      if (set) {
        policy.set_fallback_top_y(top_y);
      }
      policy.session_started(0.0);
      EXPECT_TRUE(policy.tick(1.0, {}).empty()) << "still waiting for a pose";
      const auto requests = policy.tick(6.0, {});
      EXPECT_EQ(requests.size(), 1u);
      return requests.empty() ? -1.0f : requests.front().pose.position.y;
    };
  EXPECT_EQ(fallback_y(false, 0.0f), 1.5f) << "nothing said: the fixed height of the room";
  EXPECT_NEAR(fallback_y(true, 2.9f), 2.9f, 1e-6f)
    << "the ground plus that height, as the panel says";
  ControlPanelPlacementPolicy policy;
  EXPECT_EQ(policy.fallback_top_y(), 1.5f);
  policy.set_fallback_top_y(3.1f);
  EXPECT_EQ(policy.fallback_top_y(), 3.1f);
}
