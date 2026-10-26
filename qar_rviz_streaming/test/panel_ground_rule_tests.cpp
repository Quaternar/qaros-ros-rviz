// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The rule that keeps an AR panel above the ground of the RViz scene: where the ground is under
// a live scene placement, and how a panel pose is raised above it. Pure C++17: no session, no
// ROS graph, no Qt.

#include "qar_rviz_streaming/panel_ground_rule.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <regex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using qar_ar_core::ScenePlacement;
using qar_rviz_streaming::ground_raise_m;
using qar_rviz_streaming::keep_above_ground;
using qar_rviz_streaming::kGroundMinNormalY;
using qar_rviz_streaming::kPanelGroundMarginM;
using qar_rviz_streaming::scene_ground_room_y;

constexpr double kPi = 3.14159265358979323846;
constexpr float kFallback = 7.25f;  // a height that no placement of these tests produces

/// A unit quaternion for a turn of @p degrees about the unit axis (ax, ay, az).
QarQuaternion turn(double degrees, double ax, double ay, double az)
{
  const double half = 0.5 * degrees * kPi / 180.0;
  const double s = std::sin(half);
  return QarQuaternion{
    static_cast<float>(ax * s), static_cast<float>(ay * s), static_cast<float>(az * s),
    static_cast<float>(std::cos(half))};
}

/// A turn about the vertical axis (+Y): the scene's ground stays level.
QarQuaternion yaw(double degrees) {return turn(degrees, 0.0, 1.0, 0.0);}
/// A turn about +X: the scene's up axis leans towards +Z, and the ground falls towards +Z.
QarQuaternion pitch(double degrees) {return turn(degrees, 1.0, 0.0, 0.0);}
/// A turn about +Z: the scene's up axis leans towards -X, and the ground rises towards +X.
QarQuaternion roll(double degrees) {return turn(degrees, 0.0, 0.0, 1.0);}

/// The placement of a fresh session: known, the app volume centred at room (0, 1, 0), identity
/// app pose, unit scale.
ScenePlacement fresh_placement()
{
  ScenePlacement placement;
  placement.known = true;
  placement.volume_pose.position = QarVector3{0.0f, 1.0f, 0.0f};
  return placement;
}

QarPose pose_at(float x, float y, float z, QarQuaternion orientation = {0.0f, 0.0f, 0.0f, 1.0f})
{
  QarPose pose{};
  pose.orientation = orientation;
  pose.position = QarVector3{x, y, z};
  return pose;
}

/// The lower edge of an upright panel whose top-edge midpoint is @p pose, in double precision.
double lower_edge(const QarPose & pose, float height_m)
{
  return static_cast<double>(pose.position.y) - static_cast<double>(height_m);
}

}  // namespace

// --- Where the ground is ----------------------------------------------------------------------

TEST(PanelGroundRule, VolumeCentreIsTheGroundWithAnIdentityAppPose)
{
  // The app origin sits at the centre of the app volume, 1 m above the room origin: the RViz
  // ground is at room height 1.0, everywhere.
  const ScenePlacement placement = fresh_placement();
  for (const float x : {-3.0f, -1.2f, 0.0f, 0.35f, 4.0f}) {
    for (const float z : {-2.5f, -1.0f, 0.0f, 1.0f, 6.0f}) {
      EXPECT_NEAR(scene_ground_room_y(placement, x, z, kFallback), 1.0f, 1e-6f) << x << " " << z;
    }
  }
  // The identity placement that is known puts the ground at the room origin.
  ScenePlacement identity;
  identity.known = true;
  EXPECT_NEAR(scene_ground_room_y(identity, 2.0f, -3.0f, kFallback), 0.0f, 1e-6f);
}

TEST(PanelGroundRule, MovedAppPoseMovesTheGround)
{
  // Grab-to-move writes the app pose; the scene is raised by 0.83 m or lowered by 0.24 m.
  ScenePlacement placement = fresh_placement();
  placement.app_pose.position = QarVector3{0.0f, 0.83f, 0.0f};
  EXPECT_NEAR(scene_ground_room_y(placement, 0.0f, 0.0f, kFallback), 1.83f, 1e-6f);
  EXPECT_NEAR(scene_ground_room_y(placement, -1.2f, -1.0f, kFallback), 1.83f, 1e-6f);

  placement.app_pose.position = QarVector3{0.0f, -0.24f, 0.0f};
  EXPECT_NEAR(scene_ground_room_y(placement, 0.0f, 0.0f, kFallback), 0.76f, 1e-6f);

  // A move along the ground changes nothing about its height.
  placement.app_pose.position = QarVector3{1.5f, -0.24f, -2.0f};
  EXPECT_NEAR(scene_ground_room_y(placement, 0.0f, 0.0f, kFallback), 0.76f, 1e-6f);
  EXPECT_NEAR(scene_ground_room_y(placement, 9.0f, 9.0f, kFallback), 0.76f, 1e-6f);

  // A moved volume moves it too.
  placement.volume_pose.position = QarVector3{0.0f, 1.6f, 0.0f};
  EXPECT_NEAR(scene_ground_room_y(placement, 0.0f, 0.0f, kFallback), 1.36f, 1e-6f);
}

TEST(PanelGroundRule, ScaleDoesNotChangeALevelGround)
{
  // The app scale shrinks the scene, not the offset of its origin (the app pose is in volume
  // metres), so a smaller scene keeps its ground where the app pose puts it.
  ScenePlacement placement = fresh_placement();
  placement.app_pose.position = QarVector3{0.0f, 0.83f, 0.0f};
  for (const float scale : {1.0f, 0.5f, 0.08f, 3.0f}) {
    placement.app_scale = scale;
    EXPECT_NEAR(scene_ground_room_y(placement, 0.4f, -0.7f, kFallback), 1.83f, 1e-6f) << scale;
  }
  // A scale that is no scale at all counts as 1, as it does for the scene points.
  for (const float scale : {0.0f, -2.0f, std::numeric_limits<float>::quiet_NaN()}) {
    placement.app_scale = scale;
    EXPECT_NEAR(scene_ground_room_y(placement, 0.4f, -0.7f, kFallback), 1.83f, 1e-6f);
  }
}

TEST(PanelGroundRule, YawOnlyRotationKeepsTheHeight)
{
  // A turn about the vertical axis, of the app pose or of the volume, with the scene moved
  // sideways as well: the plane stays level, at the height of the app origin.
  ScenePlacement placement = fresh_placement();
  placement.app_pose.position = QarVector3{0.5f, 0.83f, -0.3f};
  placement.app_pose.orientation = yaw(30.0);
  placement.volume_pose.orientation = yaw(90.0);
  placement.app_scale = 0.5f;
  for (const float x : {-2.0f, 0.0f, 1.3f}) {
    for (const float z : {-1.0f, 0.0f, 2.2f}) {
      EXPECT_NEAR(scene_ground_room_y(placement, x, z, kFallback), 1.83f, 1e-5f) << x << " " << z;
    }
  }
}

TEST(PanelGroundRule, ASmallTiltMakesTheHeightVaryWithXAndZ)
{
  // Tilted 5 degrees about the room's X axis: the scene's up axis leans towards +Z, so the
  // ground falls by tan(5 degrees) for every metre towards +Z, and does not change along X.
  const double slope = std::tan(5.0 * kPi / 180.0);
  ScenePlacement tilted = fresh_placement();
  tilted.app_pose.orientation = pitch(5.0);
  EXPECT_NEAR(scene_ground_room_y(tilted, 0.0f, 0.0f, kFallback), 1.0f, 1e-5f);
  EXPECT_NEAR(scene_ground_room_y(tilted, 0.0f, 1.0f, kFallback), 1.0 - slope, 1e-5);
  EXPECT_NEAR(scene_ground_room_y(tilted, 0.0f, -2.0f, kFallback), 1.0 + 2.0 * slope, 1e-5);
  EXPECT_NEAR(scene_ground_room_y(tilted, 5.0f, 0.0f, kFallback), 1.0f, 1e-5f);
  EXPECT_NE(
    scene_ground_room_y(tilted, 0.0f, 1.0f, kFallback),
    scene_ground_room_y(tilted, 0.0f, -1.0f, kFallback));

  // Tilted 5 degrees about the Z axis: the ground rises towards +X, and does not change along Z.
  ScenePlacement leaning = fresh_placement();
  leaning.app_pose.orientation = roll(5.0);
  EXPECT_NEAR(scene_ground_room_y(leaning, 1.0f, 0.0f, kFallback), 1.0 + slope, 1e-5);
  EXPECT_NEAR(scene_ground_room_y(leaning, -3.0f, 0.0f, kFallback), 1.0 - 3.0 * slope, 1e-5);
  EXPECT_NEAR(scene_ground_room_y(leaning, 0.0f, 4.0f, kFallback), 1.0f, 1e-5f);

  // The plane passes through the app origin wherever that is: moved by (0.5, 0.2, -1.0) and
  // tilted about X, the ground is at the origin's height exactly above the origin.
  ScenePlacement moved = fresh_placement();
  moved.app_pose.orientation = pitch(5.0);
  moved.app_pose.position = QarVector3{0.5f, 0.2f, -1.0f};
  EXPECT_NEAR(scene_ground_room_y(moved, 0.5f, -1.0f, kFallback), 1.2f, 1e-5f);
  EXPECT_NEAR(scene_ground_room_y(moved, 0.5f, 0.0f, kFallback), 1.2 - slope, 1e-5);

  // The volume's own tilt does the same as the app pose's.
  ScenePlacement volume_tilt = fresh_placement();
  volume_tilt.volume_pose.orientation = pitch(5.0);
  EXPECT_NEAR(scene_ground_room_y(volume_tilt, 0.0f, 1.0f, kFallback), 1.0 - slope, 1e-5);
}

TEST(PanelGroundRule, ANearVerticalPlaneGivesTheFallback)
{
  // The scene lying on its side: the ground is a wall and has no height worth keeping a panel
  // above.
  ScenePlacement placement = fresh_placement();
  placement.app_pose.orientation = pitch(90.0);
  EXPECT_EQ(scene_ground_room_y(placement, 0.0f, 0.0f, kFallback), kFallback);
  EXPECT_EQ(scene_ground_room_y(placement, 1.0f, 2.0f, kFallback), kFallback);
  placement.app_pose.orientation = roll(-90.0);
  EXPECT_EQ(scene_ground_room_y(placement, 0.0f, 0.0f, kFallback), kFallback);
  placement.app_pose.orientation = pitch(80.0);
  EXPECT_EQ(scene_ground_room_y(placement, 0.0f, 0.0f, kFallback), kFallback);

  // The limit is a tilt of 60 degrees (a normal that is half vertical).
  EXPECT_FLOAT_EQ(kGroundMinNormalY, 0.5f);
  placement.app_pose.orientation = pitch(61.0);
  EXPECT_EQ(scene_ground_room_y(placement, 0.0f, 1.0f, kFallback), kFallback);
  placement.app_pose.orientation = pitch(59.0);
  EXPECT_NEAR(
    scene_ground_room_y(placement, 0.0f, 1.0f, kFallback),
    1.0 - std::tan(59.0 * kPi / 180.0), 1e-4);
  // The sign of the tilt does not matter.
  placement.app_pose.orientation = pitch(-61.0);
  EXPECT_EQ(scene_ground_room_y(placement, 0.0f, 1.0f, kFallback), kFallback);

  // A scene that is turned over is level again: the plane is read as it is.
  placement.app_pose.orientation = roll(180.0);
  EXPECT_NEAR(scene_ground_room_y(placement, 0.7f, -0.7f, kFallback), 1.0f, 1e-5f);
}

TEST(PanelGroundRule, AnUnknownPlacementGivesTheFallback)
{
  // A placement that was never read back is a guess, whatever it holds.
  ScenePlacement unknown;
  EXPECT_FALSE(unknown.known);
  EXPECT_EQ(scene_ground_room_y(unknown, 0.0f, 0.0f, kFallback), kFallback);
  unknown.volume_pose.position = QarVector3{0.0f, 1.0f, 0.0f};
  EXPECT_EQ(scene_ground_room_y(unknown, 0.0f, 0.0f, kFallback), kFallback);
  EXPECT_EQ(scene_ground_room_y(unknown, 0.0f, 0.0f, 1.0f), 1.0f) << "the fallback is the caller's";
  EXPECT_EQ(scene_ground_room_y(unknown, 3.0f, -1.0f, -2.5f), -2.5f);

  // The same placement, known, is believed.
  unknown.known = true;
  EXPECT_NEAR(scene_ground_room_y(unknown, 0.0f, 0.0f, kFallback), 1.0f, 1e-6f);
}

TEST(PanelGroundRule, NumbersThatAreNotFiniteGiveTheFallback)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const ScenePlacement placement = fresh_placement();
  EXPECT_EQ(scene_ground_room_y(placement, nan, 0.0f, kFallback), kFallback);
  EXPECT_EQ(scene_ground_room_y(placement, 0.0f, nan, kFallback), kFallback);
  EXPECT_EQ(scene_ground_room_y(placement, inf, 0.0f, kFallback), kFallback);
  EXPECT_EQ(scene_ground_room_y(placement, 0.0f, -inf, kFallback), kFallback);

  ScenePlacement broken = fresh_placement();
  broken.app_pose.position = QarVector3{0.0f, nan, 0.0f};
  EXPECT_EQ(scene_ground_room_y(broken, 0.0f, 0.0f, kFallback), kFallback);
  broken = fresh_placement();
  broken.volume_pose.position = QarVector3{inf, 1.0f, 0.0f};
  EXPECT_EQ(scene_ground_room_y(broken, 0.0f, 0.0f, kFallback), kFallback);
  // A tilted ground does not turn a huge distance into an answer.
  ScenePlacement tilted = fresh_placement();
  tilted.app_pose.orientation = pitch(50.0);
  EXPECT_EQ(scene_ground_room_y(tilted, 0.0f, 3.0e38f, kFallback), kFallback);
}

// --- Keeping a panel above it -----------------------------------------------------------------

TEST(PanelGroundRule, RaisesAPanelByExactlyTheMissingAmount)
{
  // The Displays copy of the diagnosis: 0.576 x 0.864 m, top edge at 1.5 over a ground at 1.0.
  // Its lower edge (0.636) is 0.364 below the ground; it has to reach 0.10 above it.
  const QarPose top = pose_at(-1.2f, 1.5f, -1.0f, yaw(160.7));
  const QarPose raised = keep_above_ground(top, 0.864f, 1.0f);
  EXPECT_NEAR(raised.position.y, 1.964f, 1e-6f);
  EXPECT_NEAR(lower_edge(raised, 0.864f), 1.0 + 0.10, 1e-6) << "0.10 m above the ground";
  EXPECT_NEAR(ground_raise_m(top, 0.864f, 1.0f), 0.464f, 1e-6f);
  EXPECT_NEAR(raised.position.y, top.position.y + ground_raise_m(top, 0.864f, 1.0f), 1e-6f);

  // A shorter panel is raised by less, a lower ground by less again, and a ground low enough
  // leaves it where it is (its lower edge, 0.636, is above 0.5 + 0.10).
  EXPECT_NEAR(keep_above_ground(top, 0.5f, 1.0f).position.y, 1.6f, 1e-6f);
  EXPECT_NEAR(keep_above_ground(top, 0.864f, 0.7f).position.y, 1.664f, 1e-6f);
  EXPECT_EQ(keep_above_ground(top, 0.864f, 0.5f).position.y, 1.5f);
}

TEST(PanelGroundRule, TheRowsOfTheDiagnosisTable)
{
  // Every fixed panel of the diagnosis, over a ground at 1.0: top-centre y 1.5 (the control
  // panel at -0.10), the panel's height, the top edge that the rule asks for, and the raise.
  struct Row
  {
    const char * panel;
    float top_y;
    float height;
    float want_top_y;
    float want_raise;
  };
  const Row rows[] = {
    {"Displays copy", 1.5f, 0.864f, 1.964f, 0.464f},
    {"add-display dialog", 1.5f, 0.792f, 1.892f, 0.392f},
    {"add from the ROS graph", 1.5f, 0.624f, 1.724f, 0.224f},
    {"self panel and docks", 1.5f, 0.395f, 1.5f, 0.0f},           // its lower edge: 1.105
    {"tool battery, first size", 1.5f, 1.30f, 2.4f, 0.9f},
    {"tool battery, second size", 1.5f, 0.60f, 1.7f, 0.2f},
    {"control panel, first pose", -0.10f, 0.55f, 1.65f, 1.75f},
    {"shared-mode fallback", 1.5f, 0.55f, 1.65f, 0.15f},
  };
  for (const Row & row : rows) {
    const QarPose top = pose_at(0.5f, row.top_y, -1.0f);
    const QarPose raised = keep_above_ground(top, row.height, 1.0f);
    EXPECT_NEAR(raised.position.y, row.want_top_y, 1e-5f) << row.panel;
    EXPECT_NEAR(ground_raise_m(top, row.height, 1.0f), row.want_raise, 1e-5f) << row.panel;
    EXPECT_GE(lower_edge(raised, row.height), 1.0 + static_cast<double>(kPanelGroundMarginM) - 1e-9)
      << row.panel;
  }
}

TEST(PanelGroundRule, AfterGrabToMoveTheGroundDecidesTheRaise)
{
  // The same Displays copy under a scene that was raised or lowered by grab-to-move.
  const QarPose top = pose_at(-1.2f, 1.5f, -1.0f);
  ScenePlacement placement = fresh_placement();
  placement.app_pose.position = QarVector3{0.0f, 0.83f, 0.0f};
  const float raised_ground =
    scene_ground_room_y(placement, top.position.x, top.position.z, kFallback);
  EXPECT_NEAR(keep_above_ground(top, 0.864f, raised_ground).position.y, 2.794f, 1e-5f);

  placement.app_pose.position = QarVector3{0.0f, -0.24f, 0.0f};
  const float lowered_ground =
    scene_ground_room_y(placement, top.position.x, top.position.z, kFallback);
  EXPECT_NEAR(keep_above_ground(top, 0.864f, lowered_ground).position.y, 1.724f, 1e-5f);

  // A scene lowered far enough leaves the panel where it is: the rule never lowers a pose.
  placement.app_pose.position = QarVector3{0.0f, -1.0f, 0.0f};
  const float deep_ground =
    scene_ground_room_y(placement, top.position.x, top.position.z, kFallback);
  EXPECT_EQ(keep_above_ground(top, 0.864f, deep_ground).position.y, 1.5f);
  EXPECT_EQ(ground_raise_m(top, 0.864f, deep_ground), 0.0f);
}

TEST(PanelGroundRule, LeavesAPanelThatIsHighEnoughUnchanged)
{
  const QarPose orientation_marker = pose_at(0.3f, 0.0f, -2.0f, yaw(45.0));
  // Just above the line and far above it: the pose comes back as it is.
  const float height = 0.4f;
  for (const float top_y : {1.502f, 1.6f, 10.0f}) {
    QarPose top = orientation_marker;
    top.position.y = top_y;
    const QarPose same = keep_above_ground(top, height, 1.0f);
    EXPECT_EQ(same.position.y, top.position.y) << top_y;
    EXPECT_EQ(same.position.x, top.position.x);
    EXPECT_EQ(same.position.z, top.position.z);
    EXPECT_EQ(same.orientation.x, top.orientation.x);
    EXPECT_EQ(same.orientation.y, top.orientation.y);
    EXPECT_EQ(same.orientation.z, top.orientation.z);
    EXPECT_EQ(same.orientation.w, top.orientation.w);
    EXPECT_EQ(ground_raise_m(top, height, 1.0f), 0.0f) << top_y;
  }
  // The self panel of the diagnosis stands 0.105 m above the ground: high enough.
  const QarPose self = pose_at(0.0f, 1.5f, -1.0f);
  EXPECT_EQ(keep_above_ground(self, 0.395f, 1.0f).position.y, 1.5f);

  // A lower edge exactly on the line is on the line: numbers a float holds exactly, so that the
  // test says nothing about rounding. Ground 1.0, margin 0.25, height 0.5: the top may be 1.75.
  const QarPose on_line = pose_at(0.0f, 1.75f, 0.0f);
  EXPECT_EQ(keep_above_ground(on_line, 0.5f, 1.0f, 0.25f).position.y, 1.75f);
  EXPECT_EQ(ground_raise_m(on_line, 0.5f, 1.0f, 0.25f), 0.0f);
  const QarPose just_under = pose_at(0.0f, 1.74f, 0.0f);
  EXPECT_NEAR(keep_above_ground(just_under, 0.5f, 1.0f, 0.25f).position.y, 1.75f, 1e-6f);
  EXPECT_NEAR(ground_raise_m(just_under, 0.5f, 1.0f, 0.25f), 0.01f, 1e-6f);
}

TEST(PanelGroundRule, NeverLowersAPose)
{
  // A ground far below, a negative margin and a huge top edge: nothing comes down.
  for (const float top_y : {-5.0f, 0.0f, 1.5f, 40.0f}) {
    const QarPose top = pose_at(0.0f, top_y, 0.0f);
    for (const float ground : {-100.0f, 0.0f, 1.0f, 50.0f}) {
      for (const float margin : {-1.0f, 0.0f, 0.10f, 2.0f}) {
        const QarPose result = keep_above_ground(top, 0.5f, ground, margin);
        EXPECT_GE(result.position.y, top.position.y) << top_y << " " << ground << " " << margin;
        EXPECT_GE(ground_raise_m(top, 0.5f, ground, margin), 0.0f);
      }
    }
  }
}

TEST(PanelGroundRule, KeepsTheOrientationAndTheHorizontalPosition)
{
  // The control panel turns about the vertical axis to face the viewer: the rule leaves the turn
  // alone, and moves nothing sideways.
  const QarPose top = pose_at(0.35f, -0.10f, 0.0f, yaw(160.7));
  const QarPose raised = keep_above_ground(top, 0.55f, 1.0f);
  EXPECT_GT(raised.position.y, top.position.y);
  EXPECT_EQ(raised.position.x, top.position.x);
  EXPECT_EQ(raised.position.z, top.position.z);
  EXPECT_EQ(raised.orientation.x, top.orientation.x);
  EXPECT_EQ(raised.orientation.y, top.orientation.y);
  EXPECT_EQ(raised.orientation.z, top.orientation.z);
  EXPECT_EQ(raised.orientation.w, top.orientation.w);
}

TEST(PanelGroundRule, TheMarginIsAParameter)
{
  EXPECT_FLOAT_EQ(kPanelGroundMarginM, 0.10f);
  const QarPose top = pose_at(0.0f, 1.0f, 0.0f);
  EXPECT_NEAR(keep_above_ground(top, 0.5f, 1.0f).position.y, 1.6f, 1e-6f) << "the default margin";
  EXPECT_NEAR(keep_above_ground(top, 0.5f, 1.0f, 0.0f).position.y, 1.5f, 1e-6f);
  EXPECT_NEAR(keep_above_ground(top, 0.5f, 1.0f, 0.25f).position.y, 1.75f, 1e-6f);
  EXPECT_NEAR(ground_raise_m(top, 0.5f, 1.0f, 0.25f), 0.75f, 1e-6f);
  // A negative margin lets the panel dip under the ground by that much, but not further.
  EXPECT_NEAR(keep_above_ground(top, 0.5f, 1.0f, -0.2f).position.y, 1.3f, 1e-6f) << "raised";
  EXPECT_EQ(keep_above_ground(top, 0.5f, 0.0f, -0.2f).position.y, 1.0f) << "high enough";
}

TEST(PanelGroundRule, TheLowerEdgeIsNeverUnderTheLine)
{
  // Rounding to float must not leave the lower edge under the line by a hair: a sweep over
  // awkward numbers, the check in double precision on the float values that are stored.
  std::uint32_t state = 12345u;
  const auto next = [&state](float low, float high) {
      state = state * 1664525u + 1013904223u;
      return low + (high - low) * (static_cast<float>(state >> 8) / 16777216.0f);
    };
  int raised_count = 0;
  for (int i = 0; i < 20000; ++i) {
    const QarPose top = pose_at(0.0f, next(-3.0f, 6.0f), 0.0f);
    const float height = next(0.0f, 2.0f);
    const float ground = next(-2.0f, 4.0f);
    const float margin = next(0.0f, 0.5f);
    const QarPose result = keep_above_ground(top, height, ground, margin);
    const double line = static_cast<double>(ground) + static_cast<double>(margin);
    ASSERT_GE(result.position.y, top.position.y) << i;
    if (lower_edge(top, height) >= line) {
      ASSERT_EQ(result.position.y, top.position.y) << "high enough, so untouched: " << i;
    } else {
      ++raised_count;
      ASSERT_GE(lower_edge(result, height), line) << i;
      // ... and not by more than a few units of the last place above the line.
      ASSERT_LE(lower_edge(result, height), line + 1e-5) << i;
    }
  }
  EXPECT_GT(raised_count, 1000) << "the sweep did raise panels";
}

TEST(PanelGroundRule, OddNumbersAreHandledWithoutSurprises)
{
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const QarPose top = pose_at(1.0f, 1.5f, 2.0f, yaw(10.0));

  // A negative height counts as zero: the top edge is the lowest point.
  EXPECT_NEAR(keep_above_ground(top, -1.0f, 2.0f).position.y, 2.1f, 1e-6f);
  EXPECT_NEAR(keep_above_ground(top, 0.0f, 2.0f).position.y, 2.1f, 1e-6f);

  // A number that is not finite leaves the pose as it is, and raises by nothing.
  for (const float bad : {nan, inf, -inf}) {
    EXPECT_EQ(keep_above_ground(top, bad, 1.0f).position.y, 1.5f);
    EXPECT_EQ(keep_above_ground(top, 0.5f, bad).position.y, 1.5f);
    EXPECT_EQ(keep_above_ground(top, 0.5f, 1.0f, bad).position.y, 1.5f);
    EXPECT_EQ(ground_raise_m(top, bad, 1.0f), 0.0f);
    EXPECT_EQ(ground_raise_m(top, 0.5f, bad), 0.0f);
    EXPECT_EQ(ground_raise_m(top, 0.5f, 1.0f, bad), 0.0f);
  }
  const QarPose broken = pose_at(0.0f, nan, 0.0f);
  EXPECT_TRUE(std::isnan(keep_above_ground(broken, 0.5f, 1.0f).position.y));
  EXPECT_EQ(ground_raise_m(broken, 0.5f, 1.0f), 0.0f);
}

// --- Panels with a fixed top edge ------------------------------------------------------------

namespace
{

using qar_rviz_streaming::FixedPoseMode;
using qar_rviz_streaming::fixed_panel_top_y;
using qar_rviz_streaming::parse_fixed_pose_mode;

/// The top edge of a panel hung from the ground at the room point (x, z), under @p placement.
float ground_top(const ScenePlacement & placement, float x, float z, float fallback = kFallback)
{
  return fixed_panel_top_y(FixedPoseMode::Ground, placement, x, z, fallback);
}

/// The same hung from the room.
float room_top(const ScenePlacement & placement, float x, float z)
{
  return fixed_panel_top_y(FixedPoseMode::Room, placement, x, z, kFallback);
}

}  // namespace

TEST(FixedPoses, TheModeIsRoomUnlessTheTextSaysGround)
{
  // Not set, blank and the default are the room.
  for (const char * text : {static_cast<const char *>(nullptr), "", "   ", "\t\n", "room", "ROOM",
      " Room "})
  {
    const auto choice = parse_fixed_pose_mode(text);
    EXPECT_EQ(choice.mode, FixedPoseMode::Room) << (text != nullptr ? text : "(null)");
    EXPECT_TRUE(choice.warning.empty()) << (text != nullptr ? text : "(null)");
  }
  // Ground, in any case and with blanks around it.
  for (const char * text : {"ground", "GROUND", " Ground\t", "gRoUnD"}) {
    const auto choice = parse_fixed_pose_mode(text);
    EXPECT_EQ(choice.mode, FixedPoseMode::Ground) << text;
    EXPECT_TRUE(choice.warning.empty()) << text;
  }
}

TEST(FixedPoses, AnythingElseIsTheRoomWithAWarningThatNamesTheText)
{
  for (const char * text : {"floor", "grounded", "1", "room,ground", "off"}) {
    const auto choice = parse_fixed_pose_mode(text);
    EXPECT_EQ(choice.mode, FixedPoseMode::Room) << text;
    EXPECT_EQ(
      choice.warning,
      std::string("QAR_PANEL_FIXED_POSES='") + text + "' is not room or ground; using room.");
  }
  // The variable is named as the caller says, and only printable ASCII goes into the sentence.
  const auto named = parse_fixed_pose_mode("x", "SOME_VARIABLE");
  EXPECT_EQ(named.warning, "SOME_VARIABLE='x' is not room or ground; using room.");
  const auto odd = parse_fixed_pose_mode("gr\x01ound\xc3\xa9");
  EXPECT_EQ(odd.mode, FixedPoseMode::Room);
  EXPECT_EQ(odd.warning, "QAR_PANEL_FIXED_POSES='gr?ound??' is not room or ground; using room.");
  EXPECT_STREQ(to_string(FixedPoseMode::Room), "room");
  EXPECT_STREQ(to_string(FixedPoseMode::Ground), "ground");
}

TEST(FixedPoses, TheRoomKeepsTheTopEdgeAtAFixedHeightWhateverTheSceneDoes)
{
  EXPECT_EQ(qar_rviz_streaming::kFixedPanelTopM, 1.5f);

  ScenePlacement placement = fresh_placement();
  EXPECT_EQ(room_top(placement, 0.0f, -1.0f), 1.5f);
  // The scene moved up by 0.83 m and down by 0.24 m: the top edge stays where it was.
  placement.app_pose.position = QarVector3{0.0f, 0.83f, 0.0f};
  EXPECT_EQ(room_top(placement, 0.0f, -1.0f), 1.5f);
  placement.app_pose.position = QarVector3{0.0f, -0.24f, 0.0f};
  EXPECT_EQ(room_top(placement, -1.2f, -1.0f), 1.5f);
  // An unknown placement changes nothing either.
  EXPECT_EQ(room_top(ScenePlacement(), 0.0f, 0.0f), 1.5f);
}

TEST(FixedPoses, TheGroundHangsTheTopEdgeThatHeightAboveTheRvizGround)
{
  // A fresh scene: the ground is at 1.0 m, the top edge 1.5 m above it.
  ScenePlacement placement = fresh_placement();
  EXPECT_NEAR(ground_top(placement, 0.0f, -1.0f), 2.5f, 1e-6f);
  // The scene moved up by 0.83 m: the panels went with it. Moved down by 0.24 m: so did they.
  placement.app_pose.position = QarVector3{0.0f, 0.83f, 0.0f};
  EXPECT_NEAR(ground_top(placement, -1.2f, -1.0f), 3.33f, 1e-6f);
  placement.app_pose.position = QarVector3{0.0f, -0.24f, 0.0f};
  EXPECT_NEAR(ground_top(placement, 0.65f, -1.0f), 2.26f, 1e-6f);
  // A move along the ground changes nothing about the height.
  placement.app_pose.position = QarVector3{1.5f, -0.24f, -2.0f};
  EXPECT_NEAR(ground_top(placement, 0.0f, -1.0f), 2.26f, 1e-6f);

  // The ground is read where the panel hangs: a tilted scene gives another height at another x.
  ScenePlacement tilted = fresh_placement();
  tilted.app_pose.orientation = roll(5.0);
  const float near_origin = ground_top(tilted, 0.0f, -1.0f);
  const float far_right = ground_top(tilted, 1.3f, -1.0f);
  EXPECT_GT(std::fabs(far_right - near_origin), 0.05f);
  EXPECT_NEAR(near_origin, scene_ground_room_y(tilted, 0.0f, -1.0f, kFallback) + 1.5f, 1e-6f);
}

TEST(FixedPoses, TheGroundFallsBackWhereTheRuleHasNoGround)
{
  // The placement is not known yet: the fallback is the ground (the centre of the app volume).
  EXPECT_NEAR(ground_top(ScenePlacement(), 0.0f, 0.0f, 1.0f), 2.5f, 1e-6f);
  EXPECT_NEAR(ground_top(ScenePlacement(), 0.0f, 0.0f, 1.2f), 2.7f, 1e-6f);
  // A scene on its side has no floor to hang from.
  ScenePlacement on_its_side = fresh_placement();
  on_its_side.app_pose.orientation = roll(80.0);
  EXPECT_NEAR(ground_top(on_its_side, 0.0f, -1.0f, 1.0f), 2.5f, 1e-6f);
}

TEST(FixedPoses, TheGroundRuleStillRaisesAPanelThatReachesBelowTheGround)
{
  // Hung from the ground, a 1.2 m panel reaches down to 0.3 m above it: nothing to raise. Hung from
  // the room with the scene lifted by 1.1 m, the same panel is raised by the rule as before.
  ScenePlacement placement = fresh_placement();
  placement.app_pose.position = QarVector3{0.0f, 1.1f, 0.0f};
  const float ground = scene_ground_room_y(placement, 0.0f, -1.0f, kFallback);
  const float height = 1.2f;
  const QarPose hung = pose_at(0.0f, ground_top(placement, 0.0f, -1.0f), -1.0f);
  EXPECT_EQ(ground_raise_m(hung, height, ground), 0.0f);
  const QarPose roomed = pose_at(0.0f, room_top(placement, 0.0f, -1.0f), -1.0f);
  EXPECT_GT(ground_raise_m(roomed, height, ground), 0.0f);
}

TEST(FixedPoses, NeighbouringSlotsAreFurtherApartThanAPanelIsWide)
{
  using qar_rviz_streaming::kPanelSlotStepM;
  using qar_rviz_streaming::panel_slot_x;
  EXPECT_EQ(kPanelSlotStepM, 0.65f);
  EXPECT_EQ(panel_slot_x(0), 0.0f);
  EXPECT_NEAR(panel_slot_x(1), 0.65f, 1e-6f);
  EXPECT_NEAR(panel_slot_x(2), 1.30f, 1e-6f);
  EXPECT_NEAR(panel_slot_x(7), 4.55f, 1e-5f);
  // The panels the operator streams are 0.57 to 0.60 m wide (475 to 500 px at 0.0012 m a pixel): a
  // gap of at least 5 cm is left between neighbours in the same plane.
  constexpr float kWidestPanelM = 0.60f;
  EXPECT_GE(kPanelSlotStepM - kWidestPanelM, 0.05f - 1e-6f);
  // The step is exactly what the slot is multiplied by, for every slot.
  for (std::size_t slot = 0; slot < 20; ++slot) {
    EXPECT_FLOAT_EQ(panel_slot_x(slot), kPanelSlotStepM * static_cast<float>(slot));
  }
  // The Displays copy hangs left of slot 0 at x = -1.2 m with a width of 0.58 m: it does not touch
  // the first panel (half its width, 0.29 m, on each side of x = 0).
  EXPECT_GT(0.0f - 0.29f, -1.2f + 0.29f);
}

// ---------------------------------------------------------------------------------------------
// The brightness field of the heartbeat: the contract with the log scorecard
// ---------------------------------------------------------------------------------------------

namespace
{

using qar_rviz_streaming::brightness_field;

qar_ar_core::PanelStreamDiagnostics measured_panel(
  double all, double top, double rest, double alpha)
{
  qar_ar_core::PanelStreamDiagnostics panel;
  panel.luminance_all = all;
  panel.luminance_top = top;
  panel.luminance_rest = rest;
  panel.alpha_mean = alpha;
  return panel;
}

/// The pattern with which the log scorecard reads the field of a panel part.
const std::regex & scorecard_pattern()
{
  static const std::regex pattern(R"((?:^| )lum=(?:(\d+)/(\d+)/(\d+)(?: a=(\d+))?|-)(?= |$))");
  return pattern;
}

}  // namespace

TEST(HeartbeatBrightness, AMeasuredFrameIsLumAllTopRestAndAlphaInWholeNumbers)
{
  EXPECT_EQ(brightness_field(measured_panel(255.0, 255.0, 255.0, 255.0)), " lum=255/255/255 a=255");
  EXPECT_EQ(brightness_field(measured_panel(223.0, 201.0, 227.0, 255.0)), " lum=223/201/227 a=255");
  EXPECT_EQ(brightness_field(measured_panel(0.0, 0.0, 0.0, 0.0)), " lum=0/0/0 a=0");
}

TEST(HeartbeatBrightness, ValuesAreRoundedToWholeNumbersAndHalvesGoUp)
{
  EXPECT_EQ(brightness_field(measured_panel(239.4, 200.5, 12.49, 254.6)), " lum=239/201/12 a=255");
  EXPECT_EQ(brightness_field(measured_panel(0.4, 0.5, 0.6, 0.49)), " lum=0/1/1 a=0");
}

TEST(HeartbeatBrightness, NothingMeasuredIsLumDashWhicheverOfTheThreeIsMissing)
{
  EXPECT_EQ(brightness_field(qar_ar_core::PanelStreamDiagnostics()), " lum=-");
  EXPECT_EQ(brightness_field(measured_panel(-1.0, 10.0, 10.0, 255.0)), " lum=-");
  EXPECT_EQ(brightness_field(measured_panel(10.0, -1.0, 10.0, 255.0)), " lum=-");
  EXPECT_EQ(brightness_field(measured_panel(10.0, 10.0, -1.0, 255.0)), " lum=-");
  EXPECT_EQ(brightness_field(measured_panel(std::nan(""), 10.0, 10.0, 255.0)), " lum=-");
}

TEST(HeartbeatBrightness, TheAlphaPartIsLeftOutWhileTheAlphaIsNotMeasured)
{
  EXPECT_EQ(brightness_field(measured_panel(10.0, 20.0, 30.0, -1.0)), " lum=10/20/30");
}

TEST(HeartbeatBrightness, EveryFormMatchesThePatternOfTheScorecardWholly)
{
  const std::vector<std::string> fields = {
    brightness_field(measured_panel(255.0, 255.0, 255.0, 255.0)),
    brightness_field(measured_panel(10.0, 20.0, 30.0, -1.0)),
    brightness_field(qar_ar_core::PanelStreamDiagnostics()),
  };
  for (const std::string & field : fields) {
    std::smatch match;
    ASSERT_TRUE(std::regex_search(field, match, scorecard_pattern())) << field;
    EXPECT_EQ(static_cast<std::size_t>(match.length(0)), field.size()) << field;
  }

  std::smatch full;
  ASSERT_TRUE(std::regex_search(fields[0], full, scorecard_pattern()));
  EXPECT_EQ(full[1].str(), "255");
  EXPECT_EQ(full[2].str(), "255");
  EXPECT_EQ(full[3].str(), "255");
  EXPECT_EQ(full[4].str(), "255");
  std::smatch without_alpha;
  ASSERT_TRUE(std::regex_search(fields[1], without_alpha, scorecard_pattern()));
  EXPECT_EQ(without_alpha[3].str(), "30");
  EXPECT_FALSE(without_alpha[4].matched);
  std::smatch dash;
  ASSERT_TRUE(std::regex_search(fields[2], dash, scorecard_pattern()));
  EXPECT_FALSE(dash[1].matched);
}

TEST(HeartbeatBrightness, TheFieldEndsThePartOfAPanelAfterItsOtherTails)
{
  // As the heartbeat writes a panel part: the tails first, the brightness last, a blank before it.
  const std::string part = " | dock:Views[HIDDEN,PARKED] sub=2 nr=6" +
    brightness_field(measured_panel(239.0, 239.0, 239.0, 255.0));
  std::smatch match;
  ASSERT_TRUE(std::regex_search(part, match, scorecard_pattern())) << part;
  EXPECT_EQ(match[1].str(), "239");
  EXPECT_EQ(part.substr(part.size() - 22), " lum=239/239/239 a=255");
}

// --- The frame of an AR panel -----------------------------------------------------------------

namespace
{

using qar_rviz_streaming::AppVolumeSize;
using qar_rviz_streaming::AppVolumeSizeSource;
using qar_rviz_streaming::ArFrameChoice;
using qar_rviz_streaming::ArFrameInsets;
using qar_rviz_streaming::ArFrameSource;
using qar_rviz_streaming::choose_app_volume_size;
using qar_rviz_streaming::kMaxArFitMarginM;
using qar_rviz_streaming::kMaxArFrameInsetM;
using qar_rviz_streaming::parse_ar_frame;

/// The default of the library as the panel passes it in: top, bottom, left, right, fit margin.
const ArFrameInsets kLibraryDefault{0.066, 0.003, 0.003, 0.003, 0.003};

void expect_insets(
  const ArFrameInsets & got, double top, double bottom, double left, double right, double margin,
  const std::string & what)
{
  EXPECT_DOUBLE_EQ(got.top_m, top) << what;
  EXPECT_DOUBLE_EQ(got.bottom_m, bottom) << what;
  EXPECT_DOUBLE_EQ(got.left_m, left) << what;
  EXPECT_DOUBLE_EQ(got.right_m, right) << what;
  EXPECT_DOUBLE_EQ(got.fit_margin_m, margin) << what;
}

const char * const kNotAFrame =
  "' is not four numbers of metres from 0 to 0.5 (top,bottom,left,right) and optionally a fit "
  "margin from 0 to 0.05; ";
const char * const kUsingTheDefault =
  "using top 0.066, bottom 0.003, left 0.003, right 0.003, fit margin 0.003 m.";

}  // namespace

TEST(ArFrame, NeitherSetAndBlankAreTheDefaultWithoutAWarning)
{
  const char * const texts[] = {nullptr, "", "   ", "\t\r\n"};
  for (const char * frame : texts) {
    for (const char * headroom : texts) {
      const std::string what = std::string(frame != nullptr ? frame : "(null)") + " / " +
        (headroom != nullptr ? headroom : "(null)");
      const ArFrameChoice choice = parse_ar_frame(frame, headroom, kLibraryDefault);
      expect_insets(choice.insets, 0.066, 0.003, 0.003, 0.003, 0.003, what);
      EXPECT_EQ(choice.source, ArFrameSource::Default) << what;
      EXPECT_FALSE(choice.headroom_ignored) << what;
      EXPECT_TRUE(choice.warning.empty()) << what;
    }
  }
  // The default is the caller's: the unit does not know the library.
  const ArFrameChoice other =
    parse_ar_frame(nullptr, nullptr, ArFrameInsets{0.1, 0.2, 0.3, 0.4, 0.02});
  expect_insets(other.insets, 0.1, 0.2, 0.3, 0.4, 0.02, "the caller's default");
  EXPECT_STREQ(to_string(ArFrameSource::Default), "default");
}

TEST(ArFrame, FourNumbersAreTheFrameInTheOrderTopBottomLeftRightAndTheMarginStaysTheDefault)
{
  struct Case
  {
    const char * text;
    double top, bottom, left, right;
  };
  const Case cases[] = {
    {"0.063,0,0,0.03", 0.063, 0.0, 0.0, 0.03},
    {"0.01,0.02,0.04,0.08", 0.01, 0.02, 0.04, 0.08},
    {" 0.1 , 0.2 ,0.3, 0.4 ", 0.1, 0.2, 0.3, 0.4},
    {"\t0.1,\r0.2,\n0.3,0.4\t", 0.1, 0.2, 0.3, 0.4},
    {"0,0,0,0", 0.0, 0.0, 0.0, 0.0},
    {"-0,0.0,0,0", 0.0, 0.0, 0.0, 0.0},
    {".5,0.5,0.50,5e-1", 0.5, 0.5, 0.5, 0.5},
    {"+0.1,1e-1,0.10,.1", 0.1, 0.1, 0.1, 0.1},
  };
  for (const Case & c : cases) {
    const ArFrameChoice choice = parse_ar_frame(c.text, nullptr, kLibraryDefault);
    // Four numbers leave the fit margin at the caller's default.
    expect_insets(choice.insets, c.top, c.bottom, c.left, c.right, 0.003, c.text);
    EXPECT_EQ(choice.source, ArFrameSource::Frame) << c.text;
    EXPECT_FALSE(choice.headroom_ignored) << c.text;
    EXPECT_TRUE(choice.warning.empty()) << c.text;
    EXPECT_FALSE(std::signbit(choice.insets.top_m)) << c.text;
  }
  EXPECT_DOUBLE_EQ(kMaxArFrameInsetM, 0.5);
  EXPECT_STREQ(to_string(ArFrameSource::Frame), "QAR_PANEL_AR_FRAME");
  // The margin that four numbers leave is the caller's, whatever it is.
  const ArFrameChoice kept =
    parse_ar_frame("0.1,0.2,0.3,0.4", nullptr, ArFrameInsets{0.0, 0.0, 0.0, 0.0, 0.02});
  expect_insets(kept.insets, 0.1, 0.2, 0.3, 0.4, 0.02, "the caller's margin");
}

TEST(ArFrame, FiveNumbersAreTheFrameAndTheFitMargin)
{
  struct Case
  {
    const char * text;
    double top, bottom, left, right, margin;
  };
  const Case cases[] = {
    {"0.066,0.003,0.003,0.003,0.003", 0.066, 0.003, 0.003, 0.003, 0.003},
    {"0.01,0.02,0.04,0.08,0.005", 0.01, 0.02, 0.04, 0.08, 0.005},
    {" 0.1 , 0.2 ,0.3, 0.4 , 0.05 ", 0.1, 0.2, 0.3, 0.4, 0.05},
    {"0,0,0,0,0", 0.0, 0.0, 0.0, 0.0, 0.0},
    {"0.5,0.5,0.5,0.5,.05", 0.5, 0.5, 0.5, 0.5, 0.05},
    {"0.1,0.1,0.1,0.1,1e-2", 0.1, 0.1, 0.1, 0.1, 0.01},
  };
  for (const Case & c : cases) {
    const ArFrameChoice choice = parse_ar_frame(c.text, nullptr, kLibraryDefault);
    expect_insets(choice.insets, c.top, c.bottom, c.left, c.right, c.margin, c.text);
    EXPECT_EQ(choice.source, ArFrameSource::Frame) << c.text;
    EXPECT_FALSE(choice.headroom_ignored) << c.text;
    EXPECT_TRUE(choice.warning.empty()) << c.text;
  }
  EXPECT_DOUBLE_EQ(kMaxArFitMarginM, 0.05);
}

TEST(ArFrame, AFitMarginThatIsNotAcceptableRefusesTheWholeFrame)
{
  for (const char * text : {"0.1,0.2,0.3,0.4,0.051", "0.1,0.2,0.3,0.4,0.5",
      "0.1,0.2,0.3,0.4,-0.001", "0.1,0.2,0.3,0.4,abc", "0.1,0.2,0.3,0.4,", "0.1,0.2,0.3,0.4,nan",
      "0.1,0.2,0.3,0.4,0.01,0", "0.1,0.2,0.3,0.4 0.01"})
  {
    const ArFrameChoice choice = parse_ar_frame(text, nullptr, kLibraryDefault);
    expect_insets(choice.insets, 0.066, 0.003, 0.003, 0.003, 0.003, text);
    EXPECT_EQ(choice.source, ArFrameSource::Default) << text;
    EXPECT_EQ(
      choice.warning, std::string("QAR_PANEL_AR_FRAME='") + text + kNotAFrame + kUsingTheDefault)
      << text;
  }
}

TEST(ArFrame, AnythingElseIsTheDefaultWithAWarningThatNamesTheText)
{
  for (const char * text : {"0.1", "0.1,0.2", "0.1,0.2,0.3", "0.1,0.2,0.3,0.4,0.5", "0.1,,0.3,0.4",
      "0.1,0.2,0.3,", ",0.1,0.2,0.3", "a,b,c,d", "0.51,0,0,0", "0,0,0,0.51", "-0.1,0,0,0",
      "0,-0.1,0,0", "nan,0,0,0", "0,inf,0,0", "0.1;0.2;0.3;0.4", "0,15,0,0,0", "0.1 m,0,0,0",
      "0.1.2,0,0,0", "1e999,0,0,0", "0x10,0,0,0", "--1,0,0,0", "5cm,0,0,0", "0,0,0,1"})
  {
    const ArFrameChoice choice = parse_ar_frame(text, nullptr, kLibraryDefault);
    expect_insets(choice.insets, 0.066, 0.003, 0.003, 0.003, 0.003, text);
    EXPECT_EQ(choice.source, ArFrameSource::Default) << text;
    EXPECT_EQ(
      choice.warning, std::string("QAR_PANEL_AR_FRAME='") + text + kNotAFrame + kUsingTheDefault)
      << text;
  }
  // Only printable ASCII goes into the sentence, and the blanks around the text do not.
  const ArFrameChoice odd = parse_ar_frame(
    "  0.\x01" "5\xc3\xa9,0,0,0 ", nullptr, kLibraryDefault);
  EXPECT_EQ(
    odd.warning, std::string("QAR_PANEL_AR_FRAME='0.?5??,0,0,0") + kNotAFrame + kUsingTheDefault);
}

TEST(ArFrame, TheHeadroomIsTheTopInsetWhenTheFrameIsNotSet)
{
  struct Case
  {
    const char * text;
    double top;
  };
  const Case cases[] = {
    {"0", 0.0}, {"0.08", 0.08}, {" 0.1 ", 0.1}, {".5", 0.5}, {"0.5", 0.5}, {"+0.4", 0.4},
    {"1e-1", 0.1},
  };
  for (const char * frame : {static_cast<const char *>(nullptr), "", "  "}) {
    for (const Case & c : cases) {
      const ArFrameChoice choice = parse_ar_frame(frame, c.text, kLibraryDefault);
      // The other three insets and the fit margin are the default's.
      expect_insets(choice.insets, c.top, 0.003, 0.003, 0.003, 0.003, c.text);
      EXPECT_EQ(choice.source, ArFrameSource::Headroom) << c.text;
      EXPECT_FALSE(choice.headroom_ignored) << c.text;
      EXPECT_TRUE(choice.warning.empty()) << c.text;
    }
  }
  EXPECT_STREQ(
    to_string(ArFrameSource::Headroom), "QAR_PANEL_AR_HEADROOM (the top inset only)");
  // The rest is the caller's, whatever it is.
  const ArFrameChoice kept =
    parse_ar_frame(nullptr, "0.2", ArFrameInsets{0.1, 0.2, 0.3, 0.4, 0.02});
  expect_insets(kept.insets, 0.2, 0.2, 0.3, 0.4, 0.02, "the caller's rest");
}

TEST(ArFrame, ARefusedHeadroomIsTheDefaultWithAWarningThatNamesTheText)
{
  for (const char * text : {"-0.1", "0.51", "1", "2", "100", "0,15", "abc", "0.1 m", "0.1.2",
      "1e999", "nan", "inf", "-inf", "0x10", "--1", "5cm", "0.1,0.2,0.3,0.4"})
  {
    const ArFrameChoice choice = parse_ar_frame(nullptr, text, kLibraryDefault);
    expect_insets(choice.insets, 0.066, 0.003, 0.003, 0.003, 0.003, text);
    EXPECT_EQ(choice.source, ArFrameSource::Default) << text;
    EXPECT_EQ(
      choice.warning,
      std::string("QAR_PANEL_AR_HEADROOM='") + text +
      "' is not a number of metres from 0 to 0.5; using the top inset 0.066 m.")
      << text;
  }
}

TEST(ArFrame, TheFrameWinsOverTheHeadroomEvenWhenItIsRefused)
{
  const ArFrameChoice both = parse_ar_frame("0.01,0.02,0.03,0.04", "0.2", kLibraryDefault);
  expect_insets(both.insets, 0.01, 0.02, 0.03, 0.04, 0.003, "both set");
  EXPECT_EQ(both.source, ArFrameSource::Frame);
  EXPECT_TRUE(both.headroom_ignored);
  EXPECT_TRUE(both.warning.empty());

  // A frame that is refused is the default: the headroom is not read in its place.
  const ArFrameChoice refused = parse_ar_frame("0.01,0.02", "0.2", kLibraryDefault);
  expect_insets(refused.insets, 0.066, 0.003, 0.003, 0.003, 0.003, "frame refused");
  EXPECT_EQ(refused.source, ArFrameSource::Default);
  EXPECT_TRUE(refused.headroom_ignored);
  EXPECT_FALSE(refused.warning.empty());

  // A blank headroom is not set, so nothing is ignored.
  const ArFrameChoice blank = parse_ar_frame("0.01,0.02,0.03,0.04", "  ", kLibraryDefault);
  EXPECT_FALSE(blank.headroom_ignored);
}

// --- The size of the app volume ---------------------------------------------------------------

TEST(AppVolumeSize, NeitherStatedIsTheDefaultEdge)
{
  const AppVolumeSize size = choose_app_volume_size(std::nullopt, std::nullopt, 10.0f);
  EXPECT_FLOAT_EQ(size.edge_m, 10.0f);
  EXPECT_EQ(size.source, AppVolumeSizeSource::Default);
}

TEST(AppVolumeSize, ASizeFollowsTheScaleWhenOnlyTheScaleIsStated)
{
  struct Case
  {
    float scale;
    float edge_m;
  };
  // The box keeps holding the same part of the scene: edge / scale stays the default's 10 m.
  const Case cases[] = {{0.25f, 2.5f}, {0.5f, 5.0f}, {1.0f, 10.0f}, {2.0f, 20.0f}, {0.1f, 1.0f}};
  for (const Case & c : cases) {
    const AppVolumeSize size = choose_app_volume_size(std::nullopt, c.scale, 10.0f);
    EXPECT_FLOAT_EQ(size.edge_m, c.edge_m) << c.scale;
    EXPECT_FLOAT_EQ(size.edge_m / c.scale, 10.0f) << c.scale;
    EXPECT_EQ(size.source, AppVolumeSizeSource::FromScale) << c.scale;
  }
  // The default is the caller's.
  EXPECT_FLOAT_EQ(choose_app_volume_size(std::nullopt, 0.5f, 6.0f).edge_m, 3.0f);
}

TEST(AppVolumeSize, AStatedSizeWinsOverTheScale)
{
  const AppVolumeSize with_scale = choose_app_volume_size(4.0f, 0.25f, 10.0f);
  EXPECT_FLOAT_EQ(with_scale.edge_m, 4.0f);
  EXPECT_EQ(with_scale.source, AppVolumeSizeSource::Stated);
  const AppVolumeSize alone = choose_app_volume_size(4.0f, std::nullopt, 10.0f);
  EXPECT_FLOAT_EQ(alone.edge_m, 4.0f);
  EXPECT_EQ(alone.source, AppVolumeSizeSource::Stated);
  // A size of 10 m that is stated is stated, not the default.
  EXPECT_EQ(choose_app_volume_size(10.0f, 0.25f, 10.0f).source, AppVolumeSizeSource::Stated);
  EXPECT_FLOAT_EQ(choose_app_volume_size(10.0f, 0.25f, 10.0f).edge_m, 10.0f);
}

TEST(AppVolumeSize, ANumberThatIsNotFiniteAndPositiveCountsAsNotStated)
{
  const float nonsense[] = {0.0f, -1.0f, -0.25f, std::nanf(""),
    std::numeric_limits<float>::infinity()};
  for (const float bad : nonsense) {
    // A bad size leaves the scale to decide, a bad scale leaves the default, both leave it too.
    const AppVolumeSize scaled = choose_app_volume_size(bad, 0.5f, 10.0f);
    EXPECT_FLOAT_EQ(scaled.edge_m, 5.0f) << bad;
    EXPECT_EQ(scaled.source, AppVolumeSizeSource::FromScale) << bad;
    const AppVolumeSize sized = choose_app_volume_size(4.0f, bad, 10.0f);
    EXPECT_FLOAT_EQ(sized.edge_m, 4.0f) << bad;
    EXPECT_EQ(sized.source, AppVolumeSizeSource::Stated) << bad;
    const AppVolumeSize neither = choose_app_volume_size(std::nullopt, bad, 10.0f);
    EXPECT_FLOAT_EQ(neither.edge_m, 10.0f) << bad;
    EXPECT_EQ(neither.source, AppVolumeSizeSource::Default) << bad;
    const AppVolumeSize both = choose_app_volume_size(bad, bad, 10.0f);
    EXPECT_FLOAT_EQ(both.edge_m, 10.0f) << bad;
    EXPECT_EQ(both.source, AppVolumeSizeSource::Default) << bad;
  }
}

// --- The render device line ------------------------------------------------------------------

TEST(RenderDeviceLine, NamesTheRenderSystemAndTheDeviceItRendersOn)
{
  EXPECT_EQ(
    qar_rviz_streaming::render_device_line(
      "OpenGL Rendering Subsystem", "NVIDIA GeForce RTX 3080/PCIe/SSE2"),
    "RENDER: OpenGL Rendering Subsystem, device 'NVIDIA GeForce RTX 3080/PCIe/SSE2'");
}

TEST(RenderDeviceLine, WithoutARenderSystemItSaysSoWhateverTheDevice)
{
  const std::string none = "RENDER: no render system yet";
  EXPECT_EQ(qar_rviz_streaming::render_device_line(nullptr, nullptr), none);
  EXPECT_EQ(qar_rviz_streaming::render_device_line("", ""), none);
  EXPECT_EQ(qar_rviz_streaming::render_device_line(nullptr, "NVIDIA"), none);
  EXPECT_EQ(qar_rviz_streaming::render_device_line("", "NVIDIA"), none);
}

TEST(RenderDeviceLine, ADeviceThatIsNotNamedIsUnknownAndOnlyPrintableAsciiIsShown)
{
  const std::string unknown = "RENDER: OpenGL, device '<unknown>'";
  EXPECT_EQ(qar_rviz_streaming::render_device_line("OpenGL", nullptr), unknown);
  EXPECT_EQ(qar_rviz_streaming::render_device_line("OpenGL", ""), unknown);
  EXPECT_EQ(
    qar_rviz_streaming::render_device_line("Open\x01GL", "Card\n\xc3\xa9 1"),
    "RENDER: Open?GL, device 'Card??? 1'");
}
