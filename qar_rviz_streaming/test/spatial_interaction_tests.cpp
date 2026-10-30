// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The armed spatial mode. The RViz panel is a shell over this, so the
// arming / confirm / cancel rules are tested here rather than by clicking.

#include "qar_rviz_streaming/spatial_interaction.hpp"

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

namespace
{

using qar_ar_core::AreaShape;
using qar_ar_core::PanelPointerSample;
using qar_ar_core::PickState;
using qar_ar_core::PickTarget;
using qar_ar_core::PointerGesture;
using qar_ar_core::PointerPhase;
using qar_ar_core::RoomToRosTransform;
using qar_ar_core::SpatialPointer;
using qar_rviz_streaming::SpatialInteractionController;
using qar_rviz_streaming::SpatialMode;
using qar_rviz_streaming::SpatialPublishConfig;
using qar_rviz_streaming::SpatialPublishTarget;
using qar_rviz_streaming::default_publish_config;
using qar_ui_model::UiValue;

SpatialPointer pointer_at(float x, float y, float z, PointerPhase phase)
{
  SpatialPointer p;
  p.valid = true;
  p.pointer_id = 1;
  p.phase = phase;
  p.gesture = PointerGesture::Click;
  p.has_point = true;
  p.point = QarVector3{x, y, z};
  // A runtime action point that HIT something: the ActionPoint target refuses a
  // non-hit, so a sample that is meant to commit a pick says so.
  p.point_is_scene_hit = true;
  return p;
}

const UiValue & at(const UiValue & value, const char * key)
{
  static const UiValue kNull;
  const UiValue * found = value.find(key);
  return found != nullptr ? *found : kNull;
}

}  // namespace

TEST(SpatialInteraction, DefaultsToOffAndIgnoresSamples)
{
  SpatialInteractionController controller;
  EXPECT_EQ(controller.mode(), SpatialMode::Off);
  EXPECT_FALSE(controller.handle_pointer(pointer_at(1, 1, 0, PointerPhase::Instant)));
  EXPECT_FALSE(controller.has_pending());
  EXPECT_EQ(controller.status().summary, "");
}

TEST(SpatialInteraction, DefaultsToConfirmationRequired)
{
  SpatialInteractionController controller;
  EXPECT_FALSE(controller.auto_confirm());
  EXPECT_EQ(controller.frame_id(), "map");
}

TEST(SpatialInteraction, PointModeCommitsToAPendingClickedPoint)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  EXPECT_TRUE(controller.handle_pointer(pointer_at(1.0f, 2.0f, 0.5f, PointerPhase::Instant)));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_FALSE(controller.pending_ready());  // a human still has to say yes
  EXPECT_EQ(controller.pending().topic, "/clicked_point");
  EXPECT_EQ(controller.pending().type_name, "geometry_msgs/msg/PointStamped");
  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_DOUBLE_EQ(at(point, "x").as_double(), 1.0);
  EXPECT_DOUBLE_EQ(at(point, "z").as_double(), 0.0);  // resolved onto the ground plane
}

TEST(SpatialInteraction, AutoConfirmMakesAPickReadyImmediately)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_auto_confirm(true);
  controller.handle_pointer(pointer_at(1.0f, 2.0f, 0.0f, PointerPhase::Instant));
  EXPECT_TRUE(controller.pending_ready());
}

TEST(SpatialInteraction, ConfirmHandsOverTheMessageAndClears)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.handle_pointer(pointer_at(1.0f, 2.0f, 0.0f, PointerPhase::Instant));
  const auto request = controller.confirm();
  ASSERT_TRUE(request.ok) << request.error;
  EXPECT_EQ(request.topic, "/clicked_point");
  EXPECT_FALSE(controller.has_pending());
  EXPECT_EQ(controller.status().state, PickState::Idle);
}

TEST(SpatialInteraction, ConfirmWithNothingPendingIsRefused)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  const auto request = controller.confirm();
  EXPECT_FALSE(request.ok);
  EXPECT_EQ(request.error, "there is no pick to confirm");
}

TEST(SpatialInteraction, CancelDropsAPendingPick)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.handle_pointer(pointer_at(1.0f, 2.0f, 0.0f, PointerPhase::Instant));
  ASSERT_TRUE(controller.has_pending());
  controller.cancel();
  EXPECT_FALSE(controller.has_pending());
  EXPECT_EQ(controller.status().state, PickState::Idle);
}

TEST(SpatialInteraction, APendingPickBlocksFurtherInput)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.handle_pointer(pointer_at(1.0f, 1.0f, 0.0f, PointerPhase::Instant));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_FALSE(controller.handle_pointer(pointer_at(9.0f, 9.0f, 0.0f, PointerPhase::Instant)));
  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_DOUBLE_EQ(at(point, "x").as_double(), 1.0);
}

TEST(SpatialInteraction, SwitchingModeAbandonsWhatWasInFlight)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::NavGoal);
  controller.handle_pointer(pointer_at(0.0f, 0.0f, 0.0f, PointerPhase::Started));
  controller.handle_pointer(pointer_at(1.0f, 0.0f, 0.0f, PointerPhase::Updated));
  controller.set_mode(SpatialMode::Point);
  EXPECT_FALSE(controller.has_pending());
  EXPECT_EQ(controller.status().state, PickState::Idle);
}

TEST(SpatialInteraction, NavGoalPressDragRelease)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::NavGoal);
  controller.handle_pointer(pointer_at(1.0f, 2.0f, 0.0f, PointerPhase::Started));
  controller.handle_pointer(pointer_at(1.0f, 3.0f, 0.0f, PointerPhase::Updated));
  controller.handle_pointer(pointer_at(1.0f, 3.0f, 0.0f, PointerPhase::Ended));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_EQ(controller.pending().topic, "/goal_pose");
  const UiValue & pose = at(controller.pending().message, "pose");
  EXPECT_DOUBLE_EQ(at(at(pose, "position"), "y").as_double(), 2.0);
  EXPECT_NEAR(at(at(pose, "orientation"), "z").as_double(), 0.70710678, 1e-6);
}

TEST(SpatialInteraction, InitialPoseModeCarriesCovariance)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::InitialPose);
  controller.handle_pointer(pointer_at(0.0f, 0.0f, 0.0f, PointerPhase::Instant));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_EQ(controller.pending().topic, "/initialpose");
  EXPECT_EQ(at(at(controller.pending().message, "pose"), "covariance").size(), 36u);
}

TEST(SpatialInteraction, FrameIdReachesTheMessageHeader)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::NavGoal);
  controller.set_frame_id("odom");
  controller.handle_pointer(pointer_at(0.0f, 0.0f, 0.0f, PointerPhase::Instant));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_EQ(at(at(controller.pending().message, "header"), "frame_id").as_string(), "odom");
}

TEST(SpatialInteraction, PublishOverrideRedirectsAMode)
{
  SpatialInteractionController controller;
  SpatialPublishConfig custom = default_publish_config(SpatialPublishTarget::Custom);
  custom.topic = "/my/goal";
  custom.type_name = "geometry_msgs/msg/Pose";
  controller.set_publish_override(SpatialMode::NavGoal, custom);
  controller.set_mode(SpatialMode::NavGoal);
  controller.handle_pointer(pointer_at(1.0f, 1.0f, 0.0f, PointerPhase::Instant));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_EQ(controller.pending().topic, "/my/goal");
  EXPECT_FALSE(controller.pending().message.contains("header"));  // an unstamped Pose

  controller.cancel();
  controller.clear_publish_override(SpatialMode::NavGoal);
  EXPECT_EQ(controller.publish_config(SpatialMode::NavGoal).topic, "/goal_pose");
}

TEST(SpatialInteraction, AnUnbuildableTargetSurfacesAnErrorAndDoesNotHang)
{
  SpatialInteractionController controller;
  SpatialPublishConfig broken = default_publish_config(SpatialPublishTarget::Custom);
  broken.topic = "/somewhere";
  broken.type_name = "std_msgs/msg/String";
  controller.set_publish_override(SpatialMode::Point, broken);
  controller.set_mode(SpatialMode::Point);
  controller.handle_pointer(pointer_at(1.0f, 1.0f, 0.0f, PointerPhase::Instant));
  EXPECT_FALSE(controller.has_pending());
  EXPECT_NE(controller.status().error.find("std_msgs/msg/String"), std::string::npos);
  EXPECT_EQ(controller.status().state, PickState::Idle);  // ready for another try
}

TEST(SpatialInteraction, AreaSweepBecomesAPolygon)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Area);
  controller.handle_pointer(pointer_at(0.0f, 0.0f, 0.0f, PointerPhase::Started));
  controller.handle_pointer(pointer_at(2.0f, 3.0f, 0.0f, PointerPhase::Updated));
  controller.handle_pointer(pointer_at(2.0f, 3.0f, 0.0f, PointerPhase::Ended));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_EQ(controller.pending().type_name, "geometry_msgs/msg/PolygonStamped");
  EXPECT_EQ(at(at(controller.pending().message, "polygon"), "points").size(), 4u);
}

TEST(SpatialInteraction, PolygonModeCollectsThenCloses)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Area);
  controller.set_area_shape(AreaShape::Polygon);
  controller.handle_pointer(pointer_at(0.0f, 0.0f, 0.0f, PointerPhase::Instant));
  controller.handle_pointer(pointer_at(4.0f, 0.0f, 0.0f, PointerPhase::Instant));
  EXPECT_FALSE(controller.close_polygon());  // two vertices is not a region
  controller.handle_pointer(pointer_at(4.0f, 3.0f, 0.0f, PointerPhase::Instant));
  ASSERT_TRUE(controller.close_polygon());
  ASSERT_TRUE(controller.has_pending());
  EXPECT_EQ(at(at(controller.pending().message, "polygon"), "points").size(), 3u);
}

TEST(SpatialInteraction, RawSampleCrossesTheFrameChange)
{
  SpatialInteractionController controller;
  RoomToRosTransform transform;
  transform.enabled = true;
  transform.yaw_rad = 0.0f;
  controller.set_transform(transform);
  controller.set_mode(SpatialMode::Point);
  controller.set_target(PickTarget::action_point());

  PanelPointerSample sample;
  sample.phase = PointerPhase::Instant;
  sample.gesture = PointerGesture::Click;
  sample.has_room_point = true;
  sample.room_point_is_scene_hit = true;  // a real intersection, as the runtime reports one
  sample.room_point = QarVector3{1.0f, 2.0f, 3.0f};  // room: +Y up
  ASSERT_TRUE(controller.handle_sample(sample));
  ASSERT_TRUE(controller.has_pending());
  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_DOUBLE_EQ(at(point, "x").as_double(), -3.0);
  EXPECT_DOUBLE_EQ(at(point, "y").as_double(), -1.0);
  EXPECT_DOUBLE_EQ(at(point, "z").as_double(), 2.0);
}

TEST(SpatialInteraction, PanelOnlySamplesAreIgnored)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  PanelPointerSample sample;
  sample.has_panel_uv = true;
  sample.panel_key = "displays";
  sample.phase = PointerPhase::Instant;
  EXPECT_FALSE(controller.handle_sample(sample));
  EXPECT_FALSE(controller.has_pending());
}

TEST(SpatialInteraction, StatusSummaryDescribesThePendingPick)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::NavGoal);
  controller.handle_pointer(pointer_at(1.25f, 2.5f, 0.0f, PointerPhase::Started));
  controller.handle_pointer(pointer_at(2.25f, 2.5f, 0.0f, PointerPhase::Ended));
  const auto status = controller.status();
  EXPECT_TRUE(status.has_pending);
  EXPECT_NE(status.summary.find("Nav goal"), std::string::npos);
  EXPECT_NE(status.summary.find("x=1.25"), std::string::npos);
  EXPECT_NE(status.summary.find("ground plane"), std::string::npos);
  EXPECT_NE(status.summary.find("waiting for confirmation"), std::string::npos);
}

TEST(SpatialInteraction, StatusShowsALivePreviewBeforeCommitting)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  SpatialPointer hover = pointer_at(3.0f, 4.0f, 1.0f, PointerPhase::Updated);
  hover.gesture = PointerGesture::Hover;
  EXPECT_TRUE(controller.handle_pointer(hover));
  const auto status = controller.status();
  EXPECT_FALSE(status.has_pending);
  EXPECT_NE(status.summary.find("x=3.00"), std::string::npos);
}

TEST(SpatialInteraction, TargetChoiceReachesEverySession)
{
  SpatialInteractionController controller;
  controller.set_target(PickTarget::ground(1.5f));
  controller.set_mode(SpatialMode::Point);
  controller.handle_pointer(pointer_at(1.0f, 1.0f, 9.0f, PointerPhase::Instant));
  ASSERT_TRUE(controller.has_pending());
  EXPECT_DOUBLE_EQ(at(at(controller.pending().message, "point"), "z").as_double(), 1.5);
}

// ---------------------------------------------------------------------------
// The drop counters.
//
// When no spatial pick ever fires, the panel has to say whether nothing arrived
// or everything was discarded. These are the counters that answer it, and each
// gate names itself.
// ---------------------------------------------------------------------------

TEST(SpatialInteraction, CountsAndExplainsEverySampleItIgnores)
{
  SpatialInteractionController controller;

  // Gate 1: nothing armed.
  EXPECT_FALSE(controller.handle_pointer(pointer_at(1, 1, 0, PointerPhase::Instant)));
  EXPECT_EQ(controller.input_stats().samples_seen, 1u);
  EXPECT_EQ(controller.input_stats().ignored_mode_off, 1u);
  EXPECT_NE(controller.input_stats().last_drop_reason.find("Off"), std::string::npos);

  // Gate 2: a sample that is not a spatial one at all (a panel (u, v)).
  controller.set_mode(SpatialMode::Point);
  SpatialPointer not_spatial;
  not_spatial.valid = false;
  EXPECT_FALSE(controller.handle_pointer(not_spatial));
  EXPECT_EQ(controller.input_stats().samples_seen, 2u);
  EXPECT_EQ(controller.input_stats().ignored_not_spatial, 1u);

  // An accepted sample: a tap commits a point pick.
  EXPECT_TRUE(controller.handle_pointer(pointer_at(1, 2, 0, PointerPhase::Instant)));
  EXPECT_EQ(controller.input_stats().samples_accepted, 1u);
  EXPECT_EQ(controller.input_stats().picks_committed, 1u);
  ASSERT_TRUE(controller.has_pending());

  // Gate 3: input is refused while a pick waits for Confirm.
  EXPECT_FALSE(controller.handle_pointer(pointer_at(3, 4, 0, PointerPhase::Instant)));
  EXPECT_EQ(controller.input_stats().ignored_pending, 1u);
  EXPECT_NE(controller.input_stats().last_drop_reason.find("Confirm"), std::string::npos);

  // Gate 4: the session itself refuses an out-of-order phase.
  controller.cancel();
  EXPECT_FALSE(controller.handle_pointer(pointer_at(5, 6, 0, PointerPhase::Ended)));
  EXPECT_EQ(controller.input_stats().ignored_by_session, 1u);
  EXPECT_FALSE(controller.input_stats().last_drop_reason.empty());

  // Totals stay session-lifetime: cancelling does not rewrite history.
  EXPECT_EQ(controller.input_stats().samples_seen, 5u);
}

TEST(SpatialInteraction, ReportsDegenerateGeometryAsTheReasonForADroppedSample)
{
  // A ray parallel to the ground plane resolves to nothing. The counter must
  // say so in the geometry's own words rather than blaming the phase, because
  // "the pick target is wrong" and "the gesture was out of order" need
  // completely different fixes.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(PickTarget::ground());

  SpatialPointer parallel;
  parallel.valid = true;
  parallel.pointer_id = 1;
  parallel.gesture = PointerGesture::Click;
  parallel.phase = PointerPhase::Instant;
  parallel.has_ray = true;
  parallel.ray_origin = QarVector3{0.0f, 0.0f, 1.0f};
  parallel.ray_direction = QarVector3{1.0f, 0.0f, 0.0f};

  EXPECT_FALSE(controller.handle_pointer(parallel));
  EXPECT_EQ(controller.input_stats().ignored_by_session, 1u);
  EXPECT_NE(controller.input_stats().last_drop_reason.find("parallel"), std::string::npos)
    << controller.input_stats().last_drop_reason;
}

// ---------------------------------------------------------------------------
// With "Publish without confirming" ticked, a hover must not publish.
// ---------------------------------------------------------------------------

namespace
{

SpatialPointer hover_at(float x, float y, float z, PointerPhase phase)
{
  SpatialPointer p = pointer_at(x, y, z, phase);
  p.gesture = PointerGesture::Hover;
  return p;
}

}  // namespace

TEST(SpatialInteraction, AutoConfirmDoesNotPublishHovers)
{
  // With auto-confirm on, mere hovers must not register as picked points. The
  // runtime delivers hover as PHASE INSTANT -- the same phase a tap uses -- so
  // this is the exact shape of the runtime's stream, replayed.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(PickTarget::action_point());
  controller.set_auto_confirm(true);

  for (int i = 0; i < 50; ++i) {
    controller.handle_pointer(
      hover_at(static_cast<float>(i) * 0.01f, 1.0f, 2.0f, PointerPhase::Instant));
  }
  EXPECT_EQ(controller.input_stats().picks_committed, 0u);
  EXPECT_EQ(controller.input_stats().hover_previews, 50u);
  EXPECT_FALSE(controller.has_pending());

  // One tap, one pick.
  EXPECT_TRUE(controller.handle_pointer(pointer_at(3.0f, 4.0f, 5.0f, PointerPhase::Instant)));
  EXPECT_EQ(controller.input_stats().picks_committed, 1u);
  ASSERT_TRUE(controller.has_pending());
  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_DOUBLE_EQ(at(point, "x").as_double(), 3.0);
}

TEST(SpatialInteraction, AHoverNeverCommitsANavGoal)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::NavGoal);
  controller.set_target(PickTarget::action_point());
  controller.set_auto_confirm(true);

  controller.handle_pointer(hover_at(1.0f, 1.0f, 0.0f, PointerPhase::Started));
  controller.handle_pointer(hover_at(2.0f, 1.0f, 0.0f, PointerPhase::Updated));
  controller.handle_pointer(hover_at(3.0f, 1.0f, 0.0f, PointerPhase::Ended));
  controller.handle_pointer(hover_at(4.0f, 1.0f, 0.0f, PointerPhase::Instant));
  EXPECT_EQ(controller.input_stats().picks_committed, 0u);
  EXPECT_FALSE(controller.has_pending());
}

TEST(SpatialInteraction, TheGroundTargetPublishesOnTheGroundEvenForASceneHit)
{
  // The target names WHERE the answer is: with "Ground plane (z = 0)" selected
  // a scene hit must not keep its height, or a nav point would be published
  // metres above the floor. With no aiming ray the honest way to keep that
  // promise is the orthogonal projection, and z must come out 0.
  //
  // "Publish the thing I am pointing at" is PickTarget::action_point(), covered
  // by the test below.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(PickTarget::ground());
  controller.set_auto_confirm(true);

  RoomToRosTransform t;
  t.enabled = true;
  t.yaw_rad = 0.0f;
  controller.set_transform(t);

  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_room_point = true;
  sample.room_point_is_scene_hit = true;
  sample.room_point = QarVector3{0.5f, 1.25f, -2.0f};

  ASSERT_TRUE(controller.handle_sample(sample));
  ASSERT_TRUE(controller.has_pending()) << controller.status().error;

  // ros = (-room.z, -room.x, room.y) = (2.0, -0.5, 1.25), then onto z = 0.
  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_NEAR(at(point, "x").as_double(), 2.0, 1e-4);
  EXPECT_NEAR(at(point, "y").as_double(), -0.5, 1e-4);
  EXPECT_NEAR(at(point, "z").as_double(), 0.0, 1e-4);
}

TEST(SpatialInteraction, AHandRayMakesTheGroundTargetATrueIntersection)
{
  // Given a ray, "Ground plane (z = 0)" is not a projection but the intersection
  // the operator aimed at -- which is somewhere else entirely, and further away
  // the flatter the aim.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(PickTarget::ground());
  controller.set_auto_confirm(true);

  RoomToRosTransform t;
  t.enabled = false;  // room coordinates used as ROS coordinates, so the
                      // arithmetic below is readable straight off the sample
  controller.set_transform(t);

  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  // The runtime's own action point, 2 m up -- not what the ground target publishes.
  sample.has_room_point = true;
  sample.room_point_is_scene_hit = true;
  sample.room_point = QarVector3{0.0f, 0.0f, 2.0f};
  // ... and the hand ray, aimed forward and down from eye height.
  sample.has_room_ray = true;
  sample.ray_origin = QarVector3{0.0f, 0.0f, 1.6f};
  sample.ray_direction = QarVector3{1.0f, 0.0f, -0.8f};

  ASSERT_TRUE(controller.handle_sample(sample));
  ASSERT_TRUE(controller.has_pending()) << controller.status().error;

  // z = 0 is reached after 1.6 / 0.8 = 2 units of direction, i.e. at x = 2.
  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_NEAR(at(point, "x").as_double(), 2.0, 1e-4);
  EXPECT_NEAR(at(point, "y").as_double(), 0.0, 1e-4);
  EXPECT_NEAR(at(point, "z").as_double(), 0.0, 1e-4);
}

TEST(SpatialInteraction, TheActionPointTargetStillPublishesTheSceneHitUntouched)
{
  // The mode that MEANS "pick the thing I am pointing at". The runtime already
  // intersected the aim with the scene, so its answer is taken at face value --
  // height and all.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(PickTarget::action_point());
  controller.set_auto_confirm(true);

  RoomToRosTransform t;
  t.enabled = true;
  t.yaw_rad = 0.0f;
  controller.set_transform(t);

  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_room_point = true;
  sample.room_point_is_scene_hit = true;
  sample.room_point = QarVector3{0.5f, 1.25f, -2.0f};

  ASSERT_TRUE(controller.handle_sample(sample));
  ASSERT_TRUE(controller.has_pending()) << controller.status().error;

  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_NEAR(at(point, "x").as_double(), 2.0, 1e-4);
  EXPECT_NEAR(at(point, "y").as_double(), -0.5, 1e-4);
  EXPECT_NEAR(at(point, "z").as_double(), 1.25, 1e-4);
}

TEST(SpatialInteraction, AnActionPointClickOnEmptySpacePublishesNothingAndSaysSo)
{
  // The whole chain from the gesture's own fields: the runtime intersected
  // nothing, so it handed its start point (the hand) back as the action point.
  // Under the DEFAULT target that must publish nothing -- not even with
  // auto-confirm on, which would otherwise publish the instant it resolved --
  // and the refusal must be countable on its own, not a silent drop.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::default_spatial_target_id()));
  controller.set_auto_confirm(true);

  const QarTimePoint timestamp{0};
  const QarVector3 hand{0.4f, 1.3f, -0.2f};
  PanelPointerSample no_hit;
  ASSERT_TRUE(
    qar_ar_core::pointer_sample_from_gesture_fields(
      QAR_GESTURE_CLICK, QAR_GESTURE_PHASE_INSTANT, true, hand, timestamp, 1, no_hit, true,
      hand));
  EXPECT_FALSE(controller.handle_sample(no_hit));
  EXPECT_FALSE(controller.has_pending());
  EXPECT_EQ(controller.input_stats().picks_committed, 0u);
  EXPECT_EQ(controller.input_stats().refused_no_intersection, 1u);
  EXPECT_NE(
    controller.input_stats().last_drop_reason.find("intersected nothing"), std::string::npos)
    << controller.input_stats().last_drop_reason;

  // A HOVER over empty space is routine and must not inflate the counter that
  // exists to flag a refused CLICK.
  PanelPointerSample hover = no_hit;
  hover.gesture = PointerGesture::Hover;
  controller.handle_sample(hover);
  EXPECT_EQ(controller.input_stats().refused_no_intersection, 1u);

  // And the same click on something the runtime DID hit publishes.
  const QarVector3 target{0.4f, 1.3f, -9.0f};
  PanelPointerSample hit;
  ASSERT_TRUE(
    qar_ar_core::pointer_sample_from_gesture_fields(
      QAR_GESTURE_CLICK, QAR_GESTURE_PHASE_INSTANT, true, target, timestamp, 1, hit, true,
      hand));
  EXPECT_TRUE(controller.handle_sample(hit));
  EXPECT_EQ(controller.input_stats().picks_committed, 1u);
  EXPECT_EQ(controller.input_stats().refused_no_intersection, 1u);
}

TEST(SpatialInteraction, HoverSamplesAreExplainedRatherThanBlamedOnThePhase)
{
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Area);
  controller.set_area_shape(AreaShape::Polygon);
  controller.set_target(PickTarget::action_point());
  controller.handle_pointer(hover_at(1.0f, 1.0f, 0.0f, PointerPhase::Instant));
  EXPECT_EQ(
    controller.input_stats().last_drop_reason,
    "a Hover previews only; it never commits a pick (tap to commit)");
}


// ---------------------------------------------------------------------------
// The panel's Target dropdown, declared here so it is tested rather than typed
// out in Qt code. `action_point` is QarOS's OWN intersection of the operator's
// aim -- along a slightly CURVED ray -- with the app volume's scene, so it is
// the authoritative answer and the default.
// ---------------------------------------------------------------------------

TEST(SpatialTargetOptions, TheActionPointIsFirstAndIsTheDefault)
{
  // The runtime's own scene intersection leads; the ground plane is an
  // approximation and does not.
  const auto & options = qar_rviz_streaming::spatial_target_options();
  ASSERT_FALSE(options.empty());
  // Index 0 is what a freshly built QComboBox selects, so "first" and "default"
  // have to be the same entry or the panel shows one thing and arms another.
  EXPECT_EQ(options.front().id, qar_rviz_streaming::default_spatial_target_id());
  EXPECT_EQ(options.front().id, qar_rviz_streaming::kActionPointTargetId);

  const PickTarget target =
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::default_spatial_target_id());
  EXPECT_EQ(target.kind, qar_ar_core::PickTargetKind::ActionPoint);
}

TEST(SpatialTargetOptions, TheOperatorsOwnTargetsAreStillAllOffered)
{
  // The default is not required to avoid targets that could resolve onto the
  // hand: the SDK header defines that case exactly -- the action point equals
  // the start point when nothing was intersected -- so it is detected per
  // gesture rather than avoided wholesale (PanelPointerSample::
  // room_point_is_scene_hit, pinned in panel_input_source_tests.cpp).
  //
  // What has to hold is that nothing is demoted out of the list: every target
  // the operator can arm is offered, so a session that distrusts the runtime can
  // switch to a plane in one click.
  const auto & options = qar_rviz_streaming::spatial_target_options();
  EXPECT_EQ(options.size(), 3u);
  for (const int id : {qar_rviz_streaming::kActionPointTargetId,
      qar_rviz_streaming::kGroundPlaneTargetId, qar_rviz_streaming::kFixedDepthTargetId})
  {
    const auto it = std::find_if(
      options.begin(), options.end(), [id](const qar_rviz_streaming::SpatialTargetOption & o) {
        return o.id == id;
      });
    EXPECT_NE(it, options.end()) << id;
  }
  EXPECT_EQ(
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::kGroundPlaneTargetId).kind,
    qar_ar_core::PickTargetKind::GroundPlane);
  EXPECT_EQ(
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::kFixedDepthTargetId).kind,
    qar_ar_core::PickTargetKind::Depth);
}

TEST(SpatialTargetOptions, TheActionPointSaysItIsTheRuntimesOwnAnswer)
{
  // Its label is what the operator reads in the dropdown and is pinned here so
  // a reorder cannot quietly rename it.
  const auto & options = qar_rviz_streaming::spatial_target_options();
  const auto it = std::find_if(
    options.begin(), options.end(), [](const qar_rviz_streaming::SpatialTargetOption & o) {
      return o.id == qar_rviz_streaming::kActionPointTargetId;
    });
  ASSERT_NE(it, options.end());
  EXPECT_STREQ(it->label, "Action Point (QarOS scene intersect)");
  EXPECT_FALSE(it->straight_ray_approximation);
  EXPECT_EQ(
    qar_rviz_streaming::spatial_target_from_id(it->id).kind,
    qar_ar_core::PickTargetKind::ActionPoint);
}

TEST(SpatialTargetOptions, ExactlyOneEntryMeansTheActionPoint)
{
  // "Depth reported by the gesture" and QarOS's "Action Point" are the SAME
  // operation, so there is one entry for it and not two that quietly mean the
  // same thing.
  int action_point_entries = 0;
  for (const auto & option : qar_rviz_streaming::spatial_target_options()) {
    if (qar_rviz_streaming::spatial_target_from_id(option.id).kind ==
      qar_ar_core::PickTargetKind::ActionPoint)
    {
      ++action_point_entries;
    }
  }
  EXPECT_EQ(action_point_entries, 1);
}

TEST(SpatialTargetOptions, TheStraightRayTargetsSurviveUnchangedAndSayTheyApproximate)
{
  // Offered, because only a plane target can promise the answer is on the floor --
  // but labelled, because QarOS aims along a curve and a straight reconstruction
  // cannot land on its answer.
  const PickTarget ground =
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::kGroundPlaneTargetId);
  EXPECT_EQ(ground.kind, qar_ar_core::PickTargetKind::GroundPlane);
  EXPECT_FLOAT_EQ(ground.plane_point.z, 0.0f);
  EXPECT_FLOAT_EQ(ground.plane_normal.z, 1.0f);

  const PickTarget depth =
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::kFixedDepthTargetId);
  EXPECT_EQ(depth.kind, qar_ar_core::PickTargetKind::Depth);
  EXPECT_FLOAT_EQ(depth.depth_m, 2.0f);

  for (const auto & option : qar_rviz_streaming::spatial_target_options()) {
    const bool is_action_point = option.id == qar_rviz_streaming::kActionPointTargetId;
    EXPECT_EQ(option.straight_ray_approximation, !is_action_point) << option.label;
    if (option.straight_ray_approximation) {
      EXPECT_NE(std::string(option.label).find("approximation"), std::string::npos)
        << option.label;
      EXPECT_NE(std::string(option.help).find("curve"), std::string::npos) << option.help;
    }
  }
}

TEST(SpatialTargetOptions, AnUnknownIdFallsBackToTheDefaultTarget)
{
  // An id the panel does not know must arm the SAME geometry the panel shows as
  // selected, not a third one nobody chose.
  EXPECT_EQ(
    qar_rviz_streaming::spatial_target_from_id(4242).kind,
    qar_rviz_streaming::spatial_target_from_id(
      qar_rviz_streaming::default_spatial_target_id()).kind);
  EXPECT_EQ(
    qar_rviz_streaming::spatial_target_from_id(4242).kind,
    qar_ar_core::PickTargetKind::ActionPoint);
}

TEST(SpatialInteraction, TheActionPointTargetPublishesTheRuntimePointVerbatim)
{
  // End to end through the controller: room action point in, ROS point out, and
  // nothing in between. No plane, no ray, no depth substitution -- the whole
  // difference between this target and the two approximations beside it.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::kActionPointTargetId));
  controller.set_auto_confirm(true);

  RoomToRosTransform t;
  t.enabled = true;
  t.yaw_rad = 0.0f;
  controller.set_transform(t);

  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_room_point = true;
  sample.room_point_is_scene_hit = true;   // it is QarOS's own intersection
  sample.room_point = QarVector3{0.5f, 1.25f, -2.0f};
  // A straight aiming ray is attached as well, pointing somewhere else entirely.
  // It must not be consulted: it is our reconstruction of a ray the runtime
  // casts CURVED, and the action point is already the answer.
  sample.has_room_ray = true;
  sample.ray_origin = QarVector3{0.0f, 0.0f, 0.0f};
  sample.ray_direction = QarVector3{0.0f, -1.0f, 0.0f};

  ASSERT_TRUE(controller.handle_sample(sample));
  ASSERT_TRUE(controller.has_pending()) << controller.status().error;

  // ros = (-room.z, -room.x, room.y) = (2.0, -0.5, 1.25). Height KEPT: this
  // target does not flatten, which is exactly what makes it different from
  // "Ground plane (z = 0)".
  const UiValue & point = at(controller.pending().message, "point");
  EXPECT_NEAR(at(point, "x").as_double(), 2.0, 1e-4);
  EXPECT_NEAR(at(point, "y").as_double(), -0.5, 1e-4);
  EXPECT_NEAR(at(point, "z").as_double(), 1.25, 1e-4);
}

TEST(SpatialInteraction, AGestureWithNoActionPointIsRefusedRatherThanPublishingTheOrigin)
{
  // The SDK zeroes an absent action point rather than leaving it stale, so
  // "publish whatever we were given" would publish (0, 0, 0) -- a plausible
  // point sitting on the map origin, and the robot would drive to it. The
  // refusal is the feature.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::kActionPointTargetId));
  controller.set_auto_confirm(true);

  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_room_point = false;  // has_action_point == false at the C API
  sample.room_point = QarVector3{0.0f, 0.0f, 0.0f};

  EXPECT_FALSE(controller.handle_sample(sample));
  EXPECT_FALSE(controller.has_pending());
  EXPECT_EQ(controller.input_stats().picks_committed, 0u);
  EXPECT_EQ(controller.input_stats().ignored_not_spatial, 1u);
}

TEST(SpatialInteraction, ARayOnlySampleUnderTheActionPointTargetPublishesNothing)
{
  // The hands gave us a ray this frame but the runtime reported no
  // intersection. Our straight ray is not a stand-in for QarOS's curved one, so
  // the pick is refused with the reason the operator can read.
  SpatialInteractionController controller;
  controller.set_mode(SpatialMode::Point);
  controller.set_target(
    qar_rviz_streaming::spatial_target_from_id(qar_rviz_streaming::kActionPointTargetId));
  controller.set_auto_confirm(true);

  PanelPointerSample sample;
  sample.pointer_id = 1;
  sample.gesture = PointerGesture::Click;
  sample.phase = PointerPhase::Instant;
  sample.has_room_point = false;
  sample.has_room_ray = true;
  sample.ray_origin = QarVector3{0.0f, 0.0f, 1.6f};
  sample.ray_direction = QarVector3{0.0f, 0.0f, -1.0f};

  EXPECT_FALSE(controller.handle_sample(sample));
  EXPECT_FALSE(controller.has_pending());
  EXPECT_EQ(controller.input_stats().picks_committed, 0u);
}
