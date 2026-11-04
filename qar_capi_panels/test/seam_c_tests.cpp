// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The conversions between the QarOS C types and the values of the panel seam (seam_c.hpp): each
// field arrives where it was sent, an enumerator keeps its number, and a sample keeps every field.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "qar_capi_panels/seam_c.hpp"

namespace
{

namespace seam = qar_qt_streaming::seam;
using qar_capi_panels::append_to_seam;
using qar_capi_panels::to_c;
using qar_capi_panels::to_seam;

TEST(SeamC, AVectorKeepsItsThreeFieldsInBothDirections)
{
  const QarVector3 c{1.5f, -2.5f, 3.5f};
  const seam::Vector3 s = to_seam(c);
  EXPECT_FLOAT_EQ(s.x, 1.5f);
  EXPECT_FLOAT_EQ(s.y, -2.5f);
  EXPECT_FLOAT_EQ(s.z, 3.5f);
  const QarVector3 back = to_c(s);
  EXPECT_FLOAT_EQ(back.x, 1.5f);
  EXPECT_FLOAT_EQ(back.y, -2.5f);
  EXPECT_FLOAT_EQ(back.z, 3.5f);
}

TEST(SeamC, APoseKeepsOrientationAndPositionApart)
{
  QarPose c{};
  c.orientation = QarQuaternion{0.1f, 0.2f, 0.3f, 0.4f};
  c.position = QarVector3{5.0f, 6.0f, 7.0f};
  const seam::Pose s = to_seam(c);
  EXPECT_FLOAT_EQ(s.orientation.x, 0.1f);
  EXPECT_FLOAT_EQ(s.orientation.y, 0.2f);
  EXPECT_FLOAT_EQ(s.orientation.z, 0.3f);
  EXPECT_FLOAT_EQ(s.orientation.w, 0.4f);
  EXPECT_FLOAT_EQ(s.position.x, 5.0f);
  EXPECT_FLOAT_EQ(s.position.y, 6.0f);
  EXPECT_FLOAT_EQ(s.position.z, 7.0f);
  const QarPose back = to_c(s);
  EXPECT_EQ(std::memcmp(&back, &c, sizeof(c)), 0);
}

TEST(SeamC, TheDefaultsOfTheSeamAreTheIdentityPoseAndTheFullContentScale)
{
  const QarPose pose = to_c(seam::Pose{});
  EXPECT_FLOAT_EQ(pose.orientation.w, 1.0f);
  EXPECT_FLOAT_EQ(pose.orientation.x, 0.0f);
  EXPECT_FLOAT_EQ(pose.position.y, 0.0f);
  EXPECT_FLOAT_EQ(to_c(seam::PanelSize{}).content_scale, 1.0f);
}

TEST(SeamC, APanelSizeKeepsWidthHeightAndContentScale)
{
  const QarGuiPanelSize c{0.4f, 0.3f, 2.0f};
  const seam::PanelSize s = to_seam(c);
  EXPECT_FLOAT_EQ(s.width_meters, 0.4f);
  EXPECT_FLOAT_EQ(s.height_meters, 0.3f);
  EXPECT_FLOAT_EQ(s.content_scale, 2.0f);
  const QarGuiPanelSize back = to_c(s);
  EXPECT_EQ(std::memcmp(&back, &c, sizeof(c)), 0);
}

TEST(SeamC, ATimePointAndAPeerIdKeepEveryByte)
{
  const QarTimePoint time{1700000000123456789LL};
  EXPECT_EQ(to_seam(time).unix_ns, 1700000000123456789LL);
  EXPECT_EQ(to_c(to_seam(time)).unix_ns, time.unix_ns);

  QarPeerId peer{};
  for (std::size_t i = 0; i < sizeof(peer.data); ++i) {
    peer.data[i] = static_cast<std::uint8_t>(0xA0u + i);
  }
  const seam::PeerId s = to_seam(peer);
  for (std::size_t i = 0; i < sizeof(peer.data); ++i) {
    EXPECT_EQ(s.data[i], peer.data[i]) << i;
  }
  const QarPeerId back = to_c(s);
  EXPECT_EQ(std::memcmp(back.data, peer.data, sizeof(peer.data)), 0);
}

TEST(SeamC, AllFourPanelStatesKeepTheirNumbers)
{
  for (const QarGuiPanelState c : {QAR_GUI_PANEL_STATE_VISIBLE, QAR_GUI_PANEL_STATE_MINIMIZED,
      QAR_GUI_PANEL_STATE_HIDDEN, QAR_GUI_PANEL_STATE_CLOSED})
  {
    EXPECT_EQ(static_cast<int>(to_seam(c)), static_cast<int>(c));
    EXPECT_EQ(to_c(to_seam(c)), c);
  }
  EXPECT_EQ(to_c(seam::PanelState::Visible), QAR_GUI_PANEL_STATE_VISIBLE);
  EXPECT_EQ(to_c(seam::PanelState::Minimized), QAR_GUI_PANEL_STATE_MINIMIZED);
  EXPECT_EQ(to_c(seam::PanelState::Hidden), QAR_GUI_PANEL_STATE_HIDDEN);
  EXPECT_EQ(to_c(seam::PanelState::Closed), QAR_GUI_PANEL_STATE_CLOSED);
}

TEST(SeamC, BothPixelFormatsOfAPanelKeepTheirNumbers)
{
  EXPECT_EQ(to_c(seam::PixelFormat::R8G8B8A8), QAR_PIXEL_FORMAT_R8G8B8A8);
  EXPECT_EQ(to_c(seam::PixelFormat::B8G8R8A8), QAR_PIXEL_FORMAT_B8G8R8A8);
  EXPECT_EQ(to_seam(QAR_PIXEL_FORMAT_B8G8R8A8), seam::PixelFormat::B8G8R8A8);
}

TEST(SeamC, TheThreePointerEnumerationsKeepTheirNames)
{
  using qar_ar_core::PointerGesture;
  using qar_ar_core::PointerHand;
  using qar_ar_core::PointerPhase;
  EXPECT_EQ(to_seam(PointerGesture::Click), seam::PointerGesture::Click);
  EXPECT_EQ(to_seam(PointerGesture::Hover), seam::PointerGesture::Hover);
  EXPECT_EQ(to_seam(PointerGesture::LongPress), seam::PointerGesture::LongPress);
  EXPECT_EQ(to_seam(PointerHand::Unknown), seam::PointerHand::Unknown);
  EXPECT_EQ(to_seam(PointerHand::Left), seam::PointerHand::Left);
  EXPECT_EQ(to_seam(PointerHand::Right), seam::PointerHand::Right);
  EXPECT_EQ(to_seam(PointerPhase::Started), seam::PointerPhase::Started);
  EXPECT_EQ(to_seam(PointerPhase::Updated), seam::PointerPhase::Updated);
  EXPECT_EQ(to_seam(PointerPhase::Ended), seam::PointerPhase::Ended);
  EXPECT_EQ(to_seam(PointerPhase::Instant), seam::PointerPhase::Instant);
  EXPECT_EQ(to_seam(PointerPhase::Canceled), seam::PointerPhase::Canceled);
}

qar_ar_core::PanelPointerSample full_sample()
{
  qar_ar_core::PanelPointerSample sample;
  sample.pointer_id = 77;
  sample.gesture = qar_ar_core::PointerGesture::LongPress;
  sample.phase = qar_ar_core::PointerPhase::Canceled;
  sample.timestamp_s = 12.25;
  sample.hand = qar_ar_core::PointerHand::Right;
  sample.tap_count = 2;
  sample.has_room_point = true;
  sample.room_point = QarVector3{1.0f, 2.0f, 3.0f};
  sample.room_point_is_scene_hit = true;
  sample.has_room_start_point = true;
  sample.room_start_point = QarVector3{4.0f, 5.0f, 6.0f};
  sample.has_room_ray = true;
  sample.ray_origin = QarVector3{7.0f, 8.0f, 9.0f};
  sample.ray_direction = QarVector3{0.0f, 0.0f, -1.0f};
  sample.has_panel_uv = true;
  sample.panel_key = "dock";
  sample.u = 0.25f;
  sample.v = 0.75f;
  return sample;
}

TEST(SeamC, APointerSampleKeepsEveryField)
{
  const seam::PanelPointerSample s = to_seam(full_sample());
  EXPECT_EQ(s.pointer_id, 77u);
  EXPECT_EQ(s.gesture, seam::PointerGesture::LongPress);
  EXPECT_EQ(s.phase, seam::PointerPhase::Canceled);
  EXPECT_DOUBLE_EQ(s.timestamp_s, 12.25);
  EXPECT_EQ(s.hand, seam::PointerHand::Right);
  EXPECT_EQ(s.tap_count, 2);
  EXPECT_TRUE(s.has_room_point);
  EXPECT_FLOAT_EQ(s.room_point.x, 1.0f);
  EXPECT_FLOAT_EQ(s.room_point.y, 2.0f);
  EXPECT_FLOAT_EQ(s.room_point.z, 3.0f);
  EXPECT_TRUE(s.room_point_is_scene_hit);
  EXPECT_TRUE(s.has_room_start_point);
  EXPECT_FLOAT_EQ(s.room_start_point.x, 4.0f);
  EXPECT_FLOAT_EQ(s.room_start_point.z, 6.0f);
  EXPECT_TRUE(s.has_room_ray);
  EXPECT_FLOAT_EQ(s.ray_origin.y, 8.0f);
  EXPECT_FLOAT_EQ(s.ray_direction.z, -1.0f);
  EXPECT_TRUE(s.has_panel_uv);
  EXPECT_EQ(s.panel_key, "dock");
  EXPECT_FLOAT_EQ(s.u, 0.25f);
  EXPECT_FLOAT_EQ(s.v, 0.75f);
}

TEST(SeamC, ASampleWithNothingSetStaysAtTheDefaultsOfTheSeam)
{
  const seam::PanelPointerSample s = to_seam(qar_ar_core::PanelPointerSample{});
  const seam::PanelPointerSample defaults;
  EXPECT_EQ(s.pointer_id, defaults.pointer_id);
  EXPECT_EQ(s.gesture, defaults.gesture);
  EXPECT_EQ(s.phase, defaults.phase);
  EXPECT_EQ(s.hand, defaults.hand);
  EXPECT_FALSE(s.has_room_point);
  EXPECT_FALSE(s.has_panel_uv);
  EXPECT_TRUE(s.panel_key.empty());
}

TEST(SeamC, SamplesAreAppendedInOrderBehindWhatTheListAlreadyHolds)
{
  std::vector<seam::PanelPointerSample> out(1);
  out[0].pointer_id = 5;
  std::vector<qar_ar_core::PanelPointerSample> in(3);
  for (std::uint32_t i = 0; i < in.size(); ++i) {
    in[i].pointer_id = 100 + i;
  }
  append_to_seam(in, out);
  ASSERT_EQ(out.size(), 4u);
  EXPECT_EQ(out[0].pointer_id, 5u);
  EXPECT_EQ(out[1].pointer_id, 100u);
  EXPECT_EQ(out[2].pointer_id, 101u);
  EXPECT_EQ(out[3].pointer_id, 102u);
}

}  // namespace
