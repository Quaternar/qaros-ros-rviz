// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// A kept panel of the resize ring that is shown again takes AR input again: the panel input source
// keeps ONE subscription per panel key, so the state task that shows a panel subscribes it when the
// key is subscribed on another panel. The gesture events are the scripted ones of the golden tests.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "qar_ar_core/common_name.hpp"
#include "qar_capi_panels/qar_capi_panel_input_source.hpp"
#include "qar_capi_panels/qar_capi_panel_sender_adapter.hpp"
#include "qar_capi_panels/seam_c.hpp"

namespace seam = qar_qt_streaming::seam;

namespace
{

using seam::PanelOpResult;  // read by panel_test_kit.hpp

}  // namespace

#include "panel_test_kit.hpp"

namespace
{

/// What the input source registered with the C API for one panel.
struct Registration
{
  QarGuiPanelId id{};
  qar_gui_panel_gesture_event_callback_t callback = nullptr;
  void * user_state = nullptr;
};

std::mutex & registrations_mutex()
{
  static std::mutex mutex;
  return mutex;
}

std::vector<Registration> & registrations()
{
  static std::vector<Registration> list;
  return list;
}

decltype(g_qar_gui_panels_api.gui_panels_subscribe_all_gesture_events) g_forward = nullptr;

QarResult capture_subscribe(
  QarSession * session, const QarGuiPanelId * id,
  qar_gui_panel_gesture_event_callback_t callback, void * user_state, QarCancelToken * token)
{
  {
    std::lock_guard<std::mutex> lock(registrations_mutex());
    registrations().push_back(Registration{*id, callback, user_state});
  }
  return g_forward(session, id, callback, user_state, token);
}

struct ScriptedGesture
{
  QarPeerId peer{};
  int x = 0;
  int y = 0;
};

QarResult ok_result()
{
  QarResult result{};
  result.code = QAR_STATUS_SUCCESS;
  return result;
}

const ScriptedGesture & of(QarGuiPanelGesture * gesture)
{
  return *reinterpret_cast<const ScriptedGesture *>(gesture);
}

QarResult get_lifecycle(QarGuiPanelGesture * gesture, QarPeerId * peer, std::uint64_t * id)
{
  *peer = of(gesture).peer;
  *id = 7;
  return ok_result();
}

QarResult get_classification(
  QarGuiPanelGesture *, QarGuiPanelGestureKind * kind, QarGesturePhase * phase)
{
  *kind = QAR_GUI_PANEL_GESTURE_KIND_TAP;
  *phase = QAR_GESTURE_PHASE_INSTANT;
  return ok_result();
}

QarResult get_positions(
  QarGuiPanelGesture * gesture, QarGuiPanelPoint * position, QarGuiPanelPoint * start)
{
  *position = QarGuiPanelPoint{of(gesture).x, of(gesture).y};
  *start = *position;
  return ok_result();
}

QarResult get_handedness(QarGuiPanelGesture *, QarHandedness * hand)
{
  *hand = QAR_HANDEDNESS_RIGHT;
  return ok_result();
}

QarResult get_tap_count(QarGuiPanelGesture *, std::uint8_t * tap_count)
{
  *tap_count = 1;
  return ok_result();
}

/// Puts the capture and the scripted accessors into the C table for one test.
struct TableOverride
{
  TableOverride()
  {
    registrations().clear();
    saved_ = g_qar_gui_panels_api;
    g_forward = g_qar_gui_panels_api.gui_panels_subscribe_all_gesture_events;
    g_qar_gui_panels_api.gui_panels_subscribe_all_gesture_events = &capture_subscribe;
    g_qar_gui_panels_api.gui_panel_gesture_get_lifecycle = &get_lifecycle;
    g_qar_gui_panels_api.gui_panel_gesture_get_classification = &get_classification;
    g_qar_gui_panels_api.gui_panel_gesture_get_positions = &get_positions;
    g_qar_gui_panels_api.gui_panel_gesture_get_handedness = &get_handedness;
    g_qar_gui_panels_api.gui_panel_gesture_get_tap_count = &get_tap_count;
  }
  ~TableOverride()
  {
    g_qar_gui_panels_api = saved_;
    g_forward = nullptr;
  }
  TableOverride(const TableOverride &) = delete;
  TableOverride & operator=(const TableOverride &) = delete;

private:
  decltype(g_qar_gui_panels_api) saved_{};
};

seam::PanelDesc ring_desc(const std::string & key, const std::string & suffix)
{
  seam::PanelDesc desc;
  desc.key = key;
  desc.display_name = key;
  desc.common_name = qar_ar_core::make_panel_common_name(
    key, qar_ar_core::make_name_scope("test-program", "", "lab-pc")) + suffix;
  desc.buffer_width = 64;
  desc.buffer_height = 48;
  desc.placement.size = seam::PanelSize{0.4f, 0.3f, 1.0f};
  return desc;
}

/// Delivers one tap at the pixel (32, 24) through registration @p which, as the runtime does.
void tap_through(std::size_t which)
{
  Registration reg;
  {
    std::lock_guard<std::mutex> lock(registrations_mutex());
    ASSERT_LT(which, registrations().size());
    reg = registrations()[which];
  }
  ScriptedGesture gesture;
  gesture.x = 32;
  gesture.y = 24;
  reg.callback(reinterpret_cast<QarGuiPanelGesture *>(&gesture), reg.user_state);
}

bool same_id(const QarGuiPanelId & a, const QarGuiPanelId & b)
{
  return std::memcmp(a.data, b.data, sizeof(a.data)) == 0;
}

TEST(PanelRevival, ARevivedPanelIsSubscribedAgainAndItsTapsReachTheQueue)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  TableOverride table;
  auto input = std::make_shared<qar_capi_panels::QarCApiPanelInputSource>(h.owner().session());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner(), input);

  // Size A, then size B: the panel of A is kept hidden, the key is subscribed on the panel of B.
  auto a = factory.create_panel_sender(ring_desc("dock", "@a"));
  ASSERT_NE(a, nullptr);
  std::vector<PanelOpResult> a_results;
  ASSERT_TRUE(drain_until(*a, a_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  ASSERT_TRUE(a->set_state(seam::PanelState::Hidden));
  auto b = factory.create_panel_sender(ring_desc("dock", "@b"));
  ASSERT_NE(b, nullptr);
  std::vector<PanelOpResult> b_results;
  ASSERT_TRUE(drain_until(*b, b_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  ASSERT_EQ(registrations().size(), 2u);
  const QarGuiPanelId id_a = registrations()[0].id;
  const QarGuiPanelId id_b = registrations()[1].id;
  ASSERT_FALSE(same_id(id_a, id_b));
  ASSERT_TRUE(input->subscribed_id("dock").has_value());
  EXPECT_TRUE(same_id(*input->subscribed_id("dock"), id_b));

  // Back to size A: the kept panel is shown again and the panel of B is hidden.
  ASSERT_TRUE(b->set_state(seam::PanelState::Hidden));
  ASSERT_TRUE(a->set_state(seam::PanelState::Visible));
  ASSERT_TRUE(wait_until([&input, &id_a] {
        const auto now = input->subscribed_id("dock");
        return now.has_value() && same_id(*now, id_a);
      }));
  ASSERT_EQ(registrations().size(), 3u) << "one more subscription, no more";
  EXPECT_TRUE(same_id(registrations()[2].id, id_a)) << "the last subscription names the panel of A";
  EXPECT_EQ(h.fake().panel_gesture_subscriptions(), 1) << "the key keeps exactly one";

  // A tap on the panel of A reaches the queue; one through the retired subscription of B does not.
  tap_through(2);
  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0].panel_key, "dock");
  tap_through(1);
  samples.clear();
  input->drain(samples);
  EXPECT_TRUE(samples.empty());

  // Showing the same panel again changes nothing.
  ASSERT_TRUE(a->set_state(seam::PanelState::Visible));
  ASSERT_TRUE(owner_caught_up(h));
  EXPECT_EQ(registrations().size(), 3u);

  a->close(false);
  b->close(false);
  input->unsubscribe_all();
}

TEST(PanelRevival, APanelThatWasNeverHiddenIsSubscribedAgainByEnsureInput)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  TableOverride table;
  auto input = std::make_shared<qar_capi_panels::QarCApiPanelInputSource>(h.owner().session());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner(), input);

  // Size A, then size B whose panel took the key; the hide of A was refused, so A stays visible
  // and no state task will ever run on it.
  auto a = factory.create_panel_sender(ring_desc("dock", "@a"));
  ASSERT_NE(a, nullptr);
  std::vector<PanelOpResult> a_results;
  ASSERT_TRUE(drain_until(*a, a_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  auto b = factory.create_panel_sender(ring_desc("dock", "@b"));
  ASSERT_NE(b, nullptr);
  std::vector<PanelOpResult> b_results;
  ASSERT_TRUE(drain_until(*b, b_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  ASSERT_EQ(registrations().size(), 2u);
  const QarGuiPanelId id_a = registrations()[0].id;
  const QarGuiPanelId id_b = registrations()[1].id;
  ASSERT_TRUE(same_id(*input->subscribed_id("dock"), id_b));
  const int states_before = h.fake().count("gui_panels_set_state");

  a->ensure_input();
  ASSERT_TRUE(wait_until([&input, &id_a] {
        const auto now = input->subscribed_id("dock");
        return now.has_value() && same_id(*now, id_a);
      }));
  EXPECT_EQ(registrations().size(), 3u);
  EXPECT_TRUE(same_id(registrations()[2].id, id_a));
  EXPECT_EQ(h.fake().panel_gesture_subscriptions(), 1) << "the key keeps exactly one";
  EXPECT_EQ(h.fake().count("gui_panels_set_state"), states_before) << "no state is asked for";

  tap_through(2);
  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[0].panel_key, "dock");

  // A second call finds the key on this panel and changes nothing.
  a->ensure_input();
  ASSERT_TRUE(owner_caught_up(h));
  EXPECT_EQ(registrations().size(), 3u);
  EXPECT_EQ(h.fake().panel_gesture_subscriptions(), 1);

  a->close(false);
  b->close(false);
  input->unsubscribe_all();
}

TEST(PanelRevival, TheTeardownOfAPanelThatDoesNotHoldTheKeyLeavesTheSubscriptionAlone)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  TableOverride table;
  auto input = std::make_shared<qar_capi_panels::QarCApiPanelInputSource>(h.owner().session());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner(), input);

  auto a = factory.create_panel_sender(ring_desc("dock", "@a"));
  ASSERT_NE(a, nullptr);
  std::vector<PanelOpResult> a_results;
  ASSERT_TRUE(drain_until(*a, a_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  auto b = factory.create_panel_sender(ring_desc("dock", "@b"));
  ASSERT_NE(b, nullptr);
  std::vector<PanelOpResult> b_results;
  ASSERT_TRUE(drain_until(*b, b_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  ASSERT_EQ(registrations().size(), 2u);
  const QarGuiPanelId id_a = registrations()[0].id;
  const QarGuiPanelId id_b = registrations()[1].id;
  ASSERT_TRUE(same_id(*input->subscribed_id("dock"), id_b));

  // The key is subscribed on B: the detach of A, queued on the owner, ends nothing of B's.
  a->detach();
  ASSERT_TRUE(owner_caught_up(h));
  ASSERT_TRUE(input->subscribed_id("dock").has_value()) << "the subscription of B is still live";
  EXPECT_TRUE(same_id(*input->subscribed_id("dock"), id_b));
  EXPECT_EQ(h.fake().panel_gesture_subscriptions(), 1);
  tap_through(1);
  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  EXPECT_EQ(samples.size(), 1u) << "a tap on B still reaches the queue";

  // The teardown of the panel that holds the key ends it.
  b->detach();
  ASSERT_TRUE(owner_caught_up(h));
  EXPECT_FALSE(input->subscribed_id("dock").has_value());
  EXPECT_EQ(h.fake().panel_gesture_subscriptions(), 0);
  (void)id_a;
}

}  // namespace
