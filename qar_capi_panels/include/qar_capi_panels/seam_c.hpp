// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_CAPI_PANELS__SEAM_C_HPP_
#define QAR_CAPI_PANELS__SEAM_C_HPP_

/// @file seam_c.hpp
/// @brief The conversions between the QarOS C types and the plain values of qar_qt_streaming's
/// panel seam.
///
/// Code that works on the C types (the session owner, the QarOS calls, the spatial model) hands
/// a value to qar_qt_streaming through `to_seam`, and takes one back through `to_c`. Every
/// conversion copies the fields; a `static_assert` on the size, the offsets and the enumerator
/// values of both sides makes a drift of either side a compile error.

#include "qar_ar_core/pointer_input.hpp"
#include "qar_ar_core/qar_streaming_c.hpp"
#include "qar_qt_streaming/seam/pointer_input.hpp"
#include "qar_qt_streaming/seam/values.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace qar_capi_panels
{

namespace seam = qar_qt_streaming::seam;

static_assert(
  std::is_standard_layout<seam::Vector3>::value && sizeof(seam::Vector3) == sizeof(QarVector3) &&
  offsetof(seam::Vector3, x) == offsetof(QarVector3, x) &&
  offsetof(seam::Vector3, y) == offsetof(QarVector3, y) &&
  offsetof(seam::Vector3, z) == offsetof(QarVector3, z),
  "seam::Vector3 and QarVector3 differ in layout");
static_assert(
  std::is_standard_layout<seam::Quaternion>::value &&
  sizeof(seam::Quaternion) == sizeof(QarQuaternion) &&
  offsetof(seam::Quaternion, x) == offsetof(QarQuaternion, x) &&
  offsetof(seam::Quaternion, y) == offsetof(QarQuaternion, y) &&
  offsetof(seam::Quaternion, z) == offsetof(QarQuaternion, z) &&
  offsetof(seam::Quaternion, w) == offsetof(QarQuaternion, w),
  "seam::Quaternion and QarQuaternion differ in layout");
static_assert(
  std::is_standard_layout<seam::Pose>::value && sizeof(seam::Pose) == sizeof(QarPose) &&
  offsetof(seam::Pose, orientation) == offsetof(QarPose, orientation) &&
  offsetof(seam::Pose, position) == offsetof(QarPose, position),
  "seam::Pose and QarPose differ in layout");
static_assert(
  std::is_standard_layout<seam::PanelSize>::value &&
  sizeof(seam::PanelSize) == sizeof(QarGuiPanelSize) &&
  offsetof(seam::PanelSize, width_meters) == offsetof(QarGuiPanelSize, width_meters) &&
  offsetof(seam::PanelSize, height_meters) == offsetof(QarGuiPanelSize, height_meters) &&
  offsetof(seam::PanelSize, content_scale) == offsetof(QarGuiPanelSize, content_scale),
  "seam::PanelSize and QarGuiPanelSize differ in layout");
static_assert(
  std::is_standard_layout<seam::TimePoint>::value &&
  sizeof(seam::TimePoint) == sizeof(QarTimePoint) &&
  offsetof(seam::TimePoint, unix_ns) == offsetof(QarTimePoint, unix_ns),
  "seam::TimePoint and QarTimePoint differ in layout");
static_assert(
  std::is_standard_layout<seam::PeerId>::value && sizeof(seam::PeerId) == sizeof(QarPeerId) &&
  seam::kPeerIdLength == QAR_MAX_ID_LENGTH &&
  offsetof(seam::PeerId, data) == offsetof(QarPeerId, data),
  "seam::PeerId and QarPeerId differ in layout");
static_assert(
  static_cast<int>(seam::PanelState::Visible) == static_cast<int>(QAR_GUI_PANEL_STATE_VISIBLE) &&
  static_cast<int>(seam::PanelState::Minimized) ==
  static_cast<int>(QAR_GUI_PANEL_STATE_MINIMIZED) &&
  static_cast<int>(seam::PanelState::Hidden) == static_cast<int>(QAR_GUI_PANEL_STATE_HIDDEN) &&
  static_cast<int>(seam::PanelState::Closed) == static_cast<int>(QAR_GUI_PANEL_STATE_CLOSED),
  "seam::PanelState and QarGuiPanelState differ in value");
static_assert(
  static_cast<int>(seam::PixelFormat::R8G8B8A8) == static_cast<int>(QAR_PIXEL_FORMAT_R8G8B8A8) &&
  static_cast<int>(seam::PixelFormat::B8G8R8A8) == static_cast<int>(QAR_PIXEL_FORMAT_B8G8R8A8),
  "seam::PixelFormat and QarPixelFormat differ in value");
static_assert(
  static_cast<int>(seam::PointerGesture::Click) ==
  static_cast<int>(qar_ar_core::PointerGesture::Click) &&
  static_cast<int>(seam::PointerGesture::Hover) ==
  static_cast<int>(qar_ar_core::PointerGesture::Hover) &&
  static_cast<int>(seam::PointerGesture::LongPress) ==
  static_cast<int>(qar_ar_core::PointerGesture::LongPress),
  "seam::PointerGesture and qar_ar_core::PointerGesture differ in value");
static_assert(
  static_cast<int>(seam::PointerHand::Unknown) ==
  static_cast<int>(qar_ar_core::PointerHand::Unknown) &&
  static_cast<int>(seam::PointerHand::Left) == static_cast<int>(qar_ar_core::PointerHand::Left) &&
  static_cast<int>(seam::PointerHand::Right) == static_cast<int>(qar_ar_core::PointerHand::Right),
  "seam::PointerHand and qar_ar_core::PointerHand differ in value");
static_assert(
  static_cast<int>(seam::PointerPhase::Started) ==
  static_cast<int>(qar_ar_core::PointerPhase::Started) &&
  static_cast<int>(seam::PointerPhase::Updated) ==
  static_cast<int>(qar_ar_core::PointerPhase::Updated) &&
  static_cast<int>(seam::PointerPhase::Ended) ==
  static_cast<int>(qar_ar_core::PointerPhase::Ended) &&
  static_cast<int>(seam::PointerPhase::Instant) ==
  static_cast<int>(qar_ar_core::PointerPhase::Instant) &&
  static_cast<int>(seam::PointerPhase::Canceled) ==
  static_cast<int>(qar_ar_core::PointerPhase::Canceled),
  "seam::PointerPhase and qar_ar_core::PointerPhase differ in value");

// ---------------------------------------------------------------------------
// Vectors, poses, sizes, times, ids
// ---------------------------------------------------------------------------

inline seam::Vector3
to_seam(const QarVector3 & value)
{
  return seam::Vector3{value.x, value.y, value.z};
}

inline QarVector3
to_c(const seam::Vector3 & value)
{
  return QarVector3{value.x, value.y, value.z};
}

inline seam::Quaternion
to_seam(const QarQuaternion & value)
{
  return seam::Quaternion{value.x, value.y, value.z, value.w};
}

inline QarQuaternion
to_c(const seam::Quaternion & value)
{
  return QarQuaternion{value.x, value.y, value.z, value.w};
}

inline seam::Pose
to_seam(const QarPose & value)
{
  return seam::Pose{to_seam(value.orientation), to_seam(value.position)};
}

inline QarPose
to_c(const seam::Pose & value)
{
  return QarPose{to_c(value.orientation), to_c(value.position)};
}

inline seam::PanelSize
to_seam(const QarGuiPanelSize & value)
{
  return seam::PanelSize{value.width_meters, value.height_meters, value.content_scale};
}

inline QarGuiPanelSize
to_c(const seam::PanelSize & value)
{
  return QarGuiPanelSize{value.width_meters, value.height_meters, value.content_scale};
}

inline seam::TimePoint
to_seam(const QarTimePoint & value)
{
  return seam::TimePoint{value.unix_ns};
}

inline QarTimePoint
to_c(const seam::TimePoint & value)
{
  return QarTimePoint{value.unix_ns};
}

inline seam::PeerId
to_seam(const QarPeerId & value)
{
  seam::PeerId out;
  std::memcpy(out.data, value.data, sizeof(out.data));
  return out;
}

inline QarPeerId
to_c(const seam::PeerId & value)
{
  QarPeerId out{};
  std::memcpy(out.data, value.data, sizeof(out.data));
  return out;
}

// ---------------------------------------------------------------------------
// Enumerations
// ---------------------------------------------------------------------------

inline seam::PanelState
to_seam(QarGuiPanelState state)
{
  return static_cast<seam::PanelState>(static_cast<int>(state));
}

inline QarGuiPanelState
to_c(seam::PanelState state)
{
  return static_cast<QarGuiPanelState>(static_cast<int>(state));
}

inline seam::PixelFormat
to_seam(QarPixelFormat format)
{
  return static_cast<seam::PixelFormat>(static_cast<int>(format));
}

inline QarPixelFormat
to_c(seam::PixelFormat format)
{
  return static_cast<QarPixelFormat>(static_cast<int>(format));
}

inline seam::PointerGesture
to_seam(qar_ar_core::PointerGesture gesture)
{
  return static_cast<seam::PointerGesture>(static_cast<std::uint8_t>(gesture));
}

inline seam::PointerHand
to_seam(qar_ar_core::PointerHand hand)
{
  return static_cast<seam::PointerHand>(static_cast<std::uint8_t>(hand));
}

inline seam::PointerPhase
to_seam(qar_ar_core::PointerPhase phase)
{
  return static_cast<seam::PointerPhase>(static_cast<std::uint8_t>(phase));
}

// ---------------------------------------------------------------------------
// A pointer sample
// ---------------------------------------------------------------------------

/// @brief The sample of qar_ar_core, field by field, in the seam's vocabulary.
inline seam::PanelPointerSample
to_seam(const qar_ar_core::PanelPointerSample & sample)
{
  seam::PanelPointerSample out;
  out.pointer_id = sample.pointer_id;
  out.gesture = to_seam(sample.gesture);
  out.phase = to_seam(sample.phase);
  out.timestamp_s = sample.timestamp_s;
  out.hand = to_seam(sample.hand);
  out.tap_count = sample.tap_count;
  out.has_room_point = sample.has_room_point;
  out.room_point = to_seam(sample.room_point);
  out.room_point_is_scene_hit = sample.room_point_is_scene_hit;
  out.has_room_start_point = sample.has_room_start_point;
  out.room_start_point = to_seam(sample.room_start_point);
  out.has_room_ray = sample.has_room_ray;
  out.ray_origin = to_seam(sample.ray_origin);
  out.ray_direction = to_seam(sample.ray_direction);
  out.has_panel_uv = sample.has_panel_uv;
  out.panel_key = sample.panel_key;
  out.u = sample.u;
  out.v = sample.v;
  return out;
}

/// @brief Appends the converted samples to @p out, oldest first.
inline void
append_to_seam(
  const std::vector<qar_ar_core::PanelPointerSample> & samples,
  std::vector<seam::PanelPointerSample> & out)
{
  out.reserve(out.size() + samples.size());
  for (const qar_ar_core::PanelPointerSample & sample : samples) {
    out.push_back(to_seam(sample));
  }
}

}  // namespace qar_capi_panels

#endif  // QAR_CAPI_PANELS__SEAM_C_HPP_
