// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The samples of the panel input source for a fixed sequence of QarOS gesture events. The events
// are delivered through the callbacks that the source registers with the C API, read through
// scripted accessors of the gesture handle; every sample the source hands out is written as one
// text row and compared with the table below. The table pins the mapping of a gesture to a pointer
// sample (kind, phase, hand, pointer id, panel coordinates, lifecycle cancels) so that a change of
// the code behind it shows up as a changed row.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fake_qar_api.hpp"
#include "qar_capi_panels/qar_capi_panel_input_source.hpp"
#include "qar_capi_panels/seam_c.hpp"

namespace seam = qar_qt_streaming::seam;

namespace
{

using qar_capi_panels::QarCApiPanelInputSource;
using qar_qar_adapter::test::FakeQarApi;

// ---------------------------------------------------------------------------------------------
// The gesture handle: a record that the scripted accessors read
// ---------------------------------------------------------------------------------------------

struct Scripted
{
  bool lifecycle_ok = true;
  bool classification_ok = true;
  bool positions_ok = true;
  bool handedness_ok = true;
  bool tap_count_ok = true;
  QarPeerId peer{};
  std::uint64_t gesture_id = 0;
  int kind = 0;
  int phase = 0;
  int x = 0;
  int y = 0;
  int start_x = 0;
  int start_y = 0;
  int hand = 0;
  std::uint8_t tap_count = 0;
};

QarResult scripted_result(bool ok)
{
  QarResult result{};
  result.code = ok ? QAR_STATUS_SUCCESS : QAR_STATUS_ARGUMENT_NOT_SUPPORTED;
  result.error_handle = ok ? 0 : 1;
  return result;
}

const Scripted & of(QarGuiPanelGesture * gesture)
{
  return *reinterpret_cast<const Scripted *>(gesture);
}

QarResult get_lifecycle(QarGuiPanelGesture * gesture, QarPeerId * peer, std::uint64_t * id)
{
  const Scripted & g = of(gesture);
  if (g.lifecycle_ok) {
    *peer = g.peer;
    *id = g.gesture_id;
  }
  return scripted_result(g.lifecycle_ok);
}

QarResult get_classification(
  QarGuiPanelGesture * gesture, QarGuiPanelGestureKind * kind, QarGesturePhase * phase)
{
  const Scripted & g = of(gesture);
  if (g.classification_ok) {
    *kind = static_cast<QarGuiPanelGestureKind>(g.kind);
    *phase = static_cast<QarGesturePhase>(g.phase);
  }
  return scripted_result(g.classification_ok);
}

QarResult get_positions(
  QarGuiPanelGesture * gesture, QarGuiPanelPoint * position, QarGuiPanelPoint * start)
{
  const Scripted & g = of(gesture);
  if (g.positions_ok) {
    *position = QarGuiPanelPoint{g.x, g.y};
    *start = QarGuiPanelPoint{g.start_x, g.start_y};
  }
  return scripted_result(g.positions_ok);
}

QarResult get_handedness(QarGuiPanelGesture * gesture, QarHandedness * hand)
{
  const Scripted & g = of(gesture);
  if (g.handedness_ok) {
    *hand = static_cast<QarHandedness>(g.hand);
  }
  return scripted_result(g.handedness_ok);
}

QarResult get_tap_count(QarGuiPanelGesture * gesture, std::uint8_t * tap_count)
{
  const Scripted & g = of(gesture);
  if (g.tap_count_ok) {
    *tap_count = g.tap_count;
  }
  return scripted_result(g.tap_count_ok);
}

/// What the source registered with the C API for one panel.
struct Registration
{
  qar_gui_panel_gesture_event_callback_t callback = nullptr;
  void * user_state = nullptr;
};

std::vector<Registration> & registrations()
{
  static std::vector<Registration> list;
  return list;
}

QarResult capture_subscribe(
  QarSession *, const QarGuiPanelId *, qar_gui_panel_gesture_event_callback_t callback,
  void * user_state, QarCancelToken *)
{
  registrations().push_back(Registration{callback, user_state});
  return scripted_result(true);
}

// ---------------------------------------------------------------------------------------------
// Rows
// ---------------------------------------------------------------------------------------------

std::string row_of(const seam::PanelPointerSample & s)
{
  // Every sample of this source carries panel coordinates and none carries a room point or ray.
  EXPECT_TRUE(s.has_panel_uv);
  EXPECT_FALSE(s.has_room_point);
  EXPECT_FALSE(s.room_point_is_scene_hit);
  EXPECT_FALSE(s.has_room_start_point);
  EXPECT_FALSE(s.has_room_ray);
  char text[320];
  std::snprintf(
    text, sizeof(text), "id=%u %s/%s %s t%u %s (%.9g,%.9g)", static_cast<unsigned>(s.pointer_id),
    seam::to_string(s.gesture), seam::to_string(s.phase), seam::to_string(s.hand),
    static_cast<unsigned>(s.tap_count), s.panel_key.c_str(), static_cast<double>(s.u),
    static_cast<double>(s.v));
  return text;
}

std::string stats_row(const qar_capi_panels::PanelInputStats & s)
{
  char text[320];
  std::snprintf(
    text, sizeof(text),
    "received=%llu invalid=%llu clamped=%llu timeouts=%llu hover_timeouts=%llu unknown=%llu "
    "multi_tap=%llu drag=%llu long_press=%llu",
    static_cast<unsigned long long>(s.events_received),
    static_cast<unsigned long long>(s.events_invalid),
    static_cast<unsigned long long>(s.events_clamped),
    static_cast<unsigned long long>(s.lifecycle_timeouts),
    static_cast<unsigned long long>(s.hover_timeouts),
    static_cast<unsigned long long>(s.events_unknown_kind),
    static_cast<unsigned long long>(s.events_multi_tap),
    static_cast<unsigned long long>(s.events_drag),
    static_cast<unsigned long long>(s.events_long_press));
  return text;
}

QarPeerId peer_filled(std::uint8_t value)
{
  QarPeerId peer{};
  std::memset(peer.data, value, sizeof(peer.data));
  return peer;
}

QarPeerId peer_counting()
{
  QarPeerId peer{};
  for (std::size_t i = 0; i < sizeof(peer.data); ++i) {
    peer.data[i] = static_cast<std::uint8_t>(i);
  }
  return peer;
}

constexpr int kHover = QAR_GUI_PANEL_GESTURE_KIND_HOVER;
constexpr int kTap = QAR_GUI_PANEL_GESTURE_KIND_TAP;
constexpr int kLongPress = QAR_GUI_PANEL_GESTURE_KIND_LONG_PRESS;
constexpr int kDrag = QAR_GUI_PANEL_GESTURE_KIND_DRAG;
constexpr int kStarted = QAR_GESTURE_PHASE_STARTED;
constexpr int kUpdated = QAR_GESTURE_PHASE_UPDATED;
constexpr int kEnded = QAR_GESTURE_PHASE_ENDED;
constexpr int kInstant = QAR_GESTURE_PHASE_INSTANT;
constexpr int kCanceled = QAR_GESTURE_PHASE_CANCELED;
constexpr int kLeft = QAR_HANDEDNESS_LEFT;
constexpr int kRight = QAR_HANDEDNESS_RIGHT;
constexpr int kNoHand = QAR_HANDEDNESS_UNSPECIFIED;

Scripted gesture(
  const QarPeerId & peer, std::uint64_t id, int kind, int phase, int x, int y, int hand,
  std::uint8_t taps = 0)
{
  Scripted g;
  g.peer = peer;
  g.gesture_id = id;
  g.kind = kind;
  g.phase = phase;
  g.x = x;
  g.y = y;
  g.start_x = x;
  g.start_y = y;
  g.hand = hand;
  g.tap_count = taps;
  return g;
}

// ---------------------------------------------------------------------------------------------
// The recorded table
// ---------------------------------------------------------------------------------------------

// clang-format off
const char * const kGoldenRows[] = {
  "hover: id=2197036572 Hover/Started Left t0 dock (0.00249999994,0.00499999989)",
  "hover: id=2197036572 Hover/Updated Left t0 dock (0.502499998,0.504999995)",
  "hover: id=2197036572 Hover/Updated Left t0 dock (0.997500002,0.995000005)",
  "hover: id=2197036572 Hover/Updated Left t0 dock (0.997500002,0.00499999989)",
  "hover: id=2197036572 Hover/Ended Left t0 dock (0.0524999984,0.104999997)",
  "tap: id=2247369429 Click/Instant Right t1 dock (0.0524999984,0.104999997)",
  "tap: id=2247369429 Click/Instant Right t2 dock (0.057500001,0.115000002)",
  "tap: id=2247369429 Click/Instant Right t3 dock (0.0625,0.125)",
  "tap: id=2247369429 LongPress/Instant Right t0 dock (0.252499998,0.504999995)",
  "drag: id=2197036572 Click/Started Left t0 dock (0.102499999,0.204999998)",
  "drag: id=2197036572 Click/Updated Left t0 dock (0.152500004,0.254999995)",
  "drag: id=2197036572 Click/Updated Left t0 dock (0.202500001,0.305000007)",
  "drag: id=2197036572 Click/Ended Left t0 dock (0.202500001,0.305000007)",
  "hands: id=2197036572 Click/Started Left t0 dock (0.0274999999,0.0549999997)",
  "hands: id=2247369429 Click/Started Right t0 dock (0.752499998,0.805000007)",
  "hands: id=2197036572 Click/Updated Left t0 dock (0.0324999988,0.0649999976)",
  "hands: id=2247369429 Click/Updated Right t0 dock (0.757499993,0.814999998)",
  "hands: id=2197036572 Click/Ended Left t0 dock (0.0324999988,0.0649999976)",
  "hands: id=2247369429 Click/Canceled Right t0 dock (0.757499993,0.814999998)",
  "peers: id=2146047775 Hover/Started Unknown t0 dock (0.30250001,0.605000019)",
  "peers: id=2129270156 Click/Instant Left t0 dock (0.307500005,0.61500001)",
  "peers: id=4117914351 Hover/Started Unknown t0 dock (0.352499992,0.704999983)",
  "peers: id=4117914351 Click/Instant Unknown t1 dock (0.357499987,0.714999974)",
  "kinds: id=2197036572 Hover/Started Left t0 dock (0.00749999983,0.0149999997)",
  "kinds: id=2197036572 Click/Started Left t0 dock (0.0125000002,0.0250000004)",
  "kinds: id=2197036572 Click/Ended Left t0 dock (0.0175000001,0.0350000001)",
  "invalid: (nothing)",
  "orphan: id=2197036572 Click/Ended Left t0 dock (0.0425000004,0.0850000009)",
  "side: id=2197036572 Hover/Started Left t0 side (0.9921875,0.989583313)",
  "side: id=2197036572 Hover/Updated Left t0 side (0.9921875,0.989583313)",
  "side: id=2247369429 Click/Started Right t0 side (0.1640625,0.21875)",
  "unsubscribe: id=2197036572 Hover/Canceled Left t0 side (0.9921875,0.989583313)",
  "unsubscribe: id=2247369429 Click/Canceled Right t0 side (0.1640625,0.21875)",
  "stale: (nothing)",
  "resubscribe: id=4117914351 Hover/Canceled Unknown t0 dock (0.352499992,0.704999983)",
  "resubscribe: id=2146047775 Hover/Canceled Unknown t0 dock (0.30250001,0.605000019)",
  "resubscribe: id=2197036572 Hover/Canceled Left t0 dock (0.00749999983,0.0149999997)",
  "old dock: (nothing)",
  "new dock: id=2197036572 Click/Instant Left t1 dock (0.0274999999,0.0549999997)",
  "sweep: id=2247369429 Hover/Started Right t0 dock (0.502499998,0.504999995)",
  "sweep: id=2247369429 Click/Started Right t0 dock (0.507499993,0.514999986)",
  "sweep: id=2247369429 Hover/Canceled Right t0 dock (0.502499998,0.504999995)",
  "sweep: id=2247369429 Click/Canceled Right t0 dock (0.507499993,0.514999986)",
  "sweep off: id=2247369429 Hover/Started Right t0 dock (0.512499988,0.524999976)",
  "unsubscribe all: id=2247369429 Hover/Canceled Right t0 dock (0.512499988,0.524999976)",
  "stats: received=39 invalid=3 clamped=2 timeouts=2 hover_timeouts=1 unknown=2 multi_tap=2 "
  "drag=15 long_press=1"
};
// clang-format on

class PanelInputGolden : public ::testing::Test
{
protected:
  void SetUp() override
  {
    registrations().clear();
    fake_ = std::make_unique<FakeQarApi>();
    g_qar_gui_panels_api.gui_panels_subscribe_all_gesture_events = &capture_subscribe;
    g_qar_gui_panels_api.gui_panel_gesture_get_lifecycle = &get_lifecycle;
    g_qar_gui_panels_api.gui_panel_gesture_get_classification = &get_classification;
    g_qar_gui_panels_api.gui_panel_gesture_get_positions = &get_positions;
    g_qar_gui_panels_api.gui_panel_gesture_get_handedness = &get_handedness;
    g_qar_gui_panels_api.gui_panel_gesture_get_tap_count = &get_tap_count;
    source_ = std::make_unique<QarCApiPanelInputSource>(reinterpret_cast<QarSession *>(&session_));
  }

  void TearDown() override
  {
    source_.reset();
    fake_.reset();
    registrations().clear();
  }

  /// Delivers one gesture to the registration with the index @p which, as the runtime does.
  void deliver(std::size_t which, const Scripted & g)
  {
    ASSERT_LT(which, registrations().size());
    Scripted copy = g;
    registrations()[which].callback(
      reinterpret_cast<QarGuiPanelGesture *>(&copy), registrations()[which].user_state);
  }

  /// Drains the source and appends one row per sample, tagged with @p tag.
  void snap(const std::string & tag)
  {
    std::vector<seam::PanelPointerSample> samples;
    source_->drain(samples);
    for (const seam::PanelPointerSample & s : samples) {
      rows_.push_back(tag + ": " + row_of(s));
      timestamps_.push_back(s.timestamp_s);
    }
    if (samples.empty()) {
      rows_.push_back(tag + ": (nothing)");
    }
  }

  QarGuiPanelId panel_id_{};
  int session_ = 0;
  std::unique_ptr<FakeQarApi> fake_;
  std::unique_ptr<QarCApiPanelInputSource> source_;
  std::vector<std::string> rows_;
  std::vector<double> timestamps_;
};

TEST_F(PanelInputGolden, TheSamplesOfAFixedGestureSequenceAreTheRecordedOnes)
{
  const QarPeerId a = peer_filled(0xAA);
  const QarPeerId b = peer_counting();
  const QarPeerId zero{};

  // Three seconds of liveness so that only the sweep section below times anything out.
  source_->set_lifecycle_timeout(3.0);
  ASSERT_TRUE(source_->subscribe_panel("dock", panel_id_, 200, 100));  // registration 0
  ASSERT_TRUE(source_->subscribe_panel("side", panel_id_, 64, 48));    // registration 1
  ASSERT_EQ(registrations().size(), 2u);

  // A hover over the whole panel: the corner pixel, the middle, the last pixel, a pixel past the
  // edge on both axes, and the exit.
  deliver(0, gesture(a, 1, kHover, kStarted, 0, 0, kLeft));
  deliver(0, gesture(a, 1, kHover, kUpdated, 100, 50, kLeft));
  deliver(0, gesture(a, 1, kHover, kUpdated, 199, 99, kLeft));
  deliver(0, gesture(a, 1, kHover, kUpdated, 250, -5, kLeft));
  deliver(0, gesture(a, 1, kHover, kEnded, 10, 10, kLeft));
  snap("hover");

  // Taps (the recogniser's count rides along), and a long press.
  deliver(0, gesture(a, 2, kTap, kInstant, 10, 10, kRight, 1));
  deliver(0, gesture(a, 3, kTap, kInstant, 11, 11, kRight, 2));
  deliver(0, gesture(a, 4, kTap, kInstant, 12, 12, kRight, 3));
  deliver(0, gesture(a, 5, kLongPress, kInstant, 50, 50, kRight));
  snap("tap");

  // One drag: press, two moves, release.
  deliver(0, gesture(a, 7, kDrag, kStarted, 20, 20, kLeft));
  deliver(0, gesture(a, 7, kDrag, kUpdated, 30, 25, kLeft));
  deliver(0, gesture(a, 7, kDrag, kUpdated, 40, 30, kLeft));
  deliver(0, gesture(a, 7, kDrag, kEnded, 40, 30, kLeft));
  snap("drag");

  // Two hands of one peer at the same time, one released and one cancelled.
  deliver(0, gesture(a, 8, kDrag, kStarted, 5, 5, kLeft));
  deliver(0, gesture(a, 9, kDrag, kStarted, 150, 80, kRight));
  deliver(0, gesture(a, 8, kDrag, kUpdated, 6, 6, kLeft));
  deliver(0, gesture(a, 9, kDrag, kUpdated, 151, 81, kRight));
  deliver(0, gesture(a, 8, kDrag, kEnded, 6, 6, kLeft));
  deliver(0, gesture(a, 9, kDrag, kCanceled, 151, 81, kRight));
  snap("hands");

  // Two peers with the same gesture id; a hand that cannot be read, one that is unspecified and
  // one outside the enumeration; a tap count that cannot be read.
  Scripted no_hand = gesture(b, 1, kHover, kStarted, 60, 60, kLeft);
  no_hand.handedness_ok = false;
  deliver(0, no_hand);
  Scripted no_taps = gesture(b, 2, kTap, kInstant, 61, 61, kLeft, 9);
  no_taps.tap_count_ok = false;
  deliver(0, no_taps);
  deliver(0, gesture(zero, 1, kHover, kStarted, 70, 70, kNoHand));
  deliver(0, gesture(zero, 2, kTap, kInstant, 71, 71, 9, 1));
  snap("peers");

  // The same id for two kinds of one peer: ending the drag must leave the hover open.
  deliver(0, gesture(a, 20, kHover, kStarted, 1, 1, kLeft));
  deliver(0, gesture(a, 20, kDrag, kStarted, 2, 2, kLeft));
  deliver(0, gesture(a, 20, kDrag, kEnded, 3, 3, kLeft));
  snap("kinds");

  // Events that cannot be turned into a sample: an accessor that refuses, in turn, and a kind and
  // a phase that the build does not know. An end for a lifecycle that was never seen still maps.
  Scripted bad_lifecycle = gesture(a, 30, kHover, kStarted, 1, 1, kLeft);
  bad_lifecycle.lifecycle_ok = false;
  deliver(0, bad_lifecycle);
  Scripted bad_class = gesture(a, 31, kHover, kStarted, 1, 1, kLeft);
  bad_class.classification_ok = false;
  deliver(0, bad_class);
  Scripted bad_position = gesture(a, 32, kHover, kStarted, 1, 1, kLeft);
  bad_position.positions_ok = false;
  deliver(0, bad_position);
  deliver(0, gesture(a, 33, 9, kStarted, 1, 1, kLeft));
  deliver(0, gesture(a, 34, kDrag, 9, 1, 1, kLeft));
  snap("invalid");
  deliver(0, gesture(a, 99, kDrag, kEnded, 8, 8, kLeft));
  snap("orphan");

  // The second panel has its own content size; a drag and a hover are left open on it.
  deliver(1, gesture(a, 40, kHover, kStarted, 63, 47, kLeft));
  deliver(1, gesture(a, 40, kHover, kUpdated, 64, 48, kLeft));
  deliver(1, gesture(a, 41, kDrag, kStarted, 10, 10, kRight));
  snap("side");

  // Removing that panel lets go of what is held on it.
  source_->unsubscribe_panel("side");
  snap("unsubscribe");

  // A callback of the retired subscription finds nothing to deliver to.
  deliver(1, gesture(a, 42, kHover, kStarted, 1, 1, kLeft));
  snap("stale");

  // Subscribing a panel again retires the old subscription and lets go of what the panel holds
  // (the two hovers of the peers b and zero, and the hover 20 of the peer a).
  ASSERT_TRUE(source_->subscribe_panel("dock", panel_id_, 200, 100));  // registration 2
  ASSERT_EQ(registrations().size(), 3u);
  snap("resubscribe");
  deliver(0, gesture(a, 50, kTap, kInstant, 5, 5, kLeft, 1));
  snap("old dock");
  deliver(2, gesture(a, 50, kTap, kInstant, 5, 5, kLeft, 1));
  snap("new dock");

  // The liveness sweep: a hover and a drag that fall silent are cancelled, in the order of their
  // keys; with the sweep switched off nothing is cancelled.
  deliver(2, gesture(a, 60, kHover, kStarted, 100, 50, kRight));
  deliver(2, gesture(a, 61, kDrag, kStarted, 101, 51, kRight));
  source_->set_lifecycle_timeout(0.05);
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  snap("sweep");
  deliver(2, gesture(a, 62, kHover, kStarted, 102, 52, kRight));
  source_->set_lifecycle_timeout(0.0);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  snap("sweep off");

  // Retiring everything lets go of the hover that is still open.
  source_->unsubscribe_all();
  snap("unsubscribe all");

  rows_.push_back("stats: " + stats_row(source_->stats()));

  // The timestamps of the samples are on one clock and never run backwards.
  ASSERT_FALSE(timestamps_.empty());
  for (std::size_t i = 0; i < timestamps_.size(); ++i) {
    EXPECT_GT(timestamps_[i], 0.0) << "sample " << i;
    if (i > 0) {
      EXPECT_GE(timestamps_[i], timestamps_[i - 1]) << "sample " << i;
    }
  }

  const std::size_t expected = sizeof(kGoldenRows) / sizeof(kGoldenRows[0]);
  ASSERT_EQ(rows_.size(), expected);
  for (std::size_t i = 0; i < expected; ++i) {
    EXPECT_EQ(rows_[i], kGoldenRows[i]) << "row " << i;
  }
}

TEST(PanelInputGoldenIds, ThePointerIdOfAPeerAndAHandIsTheRecordedOne)
{
  using qar_ar_core::PointerHand;
  EXPECT_EQ(qar_capi_panels::panel_pointer_id_for(QarPeerId{}, PointerHand::Unknown), 4117914351u);
  EXPECT_EQ(qar_capi_panels::panel_pointer_id_for(peer_filled(0xAA), PointerHand::Left),
    2197036572u);
  EXPECT_EQ(qar_capi_panels::panel_pointer_id_for(peer_filled(0xAA), PointerHand::Right),
    2247369429u);
  EXPECT_EQ(qar_capi_panels::panel_pointer_id_for(peer_counting(), PointerHand::Unknown),
    2146047775u);
}

}  // namespace
