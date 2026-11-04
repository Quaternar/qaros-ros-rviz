// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The panel sender on the session owner's thread: every call the GUI thread makes returns at
// once, the QarOS calls run on the owner thread in the order they were asked for, and the
// outcomes come back through drain_results(). The owner runs on the fake QarOS library, whose
// calls are slowed with the call hook to the 2 seconds that the installed QAROS needs.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <thread>
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

seam::PanelDesc stream_desc(const std::string & key)
{
  seam::PanelDesc desc;
  desc.key = key;
  desc.display_name = key;
  desc.common_name = qar_ar_core::make_panel_common_name(
    key, qar_ar_core::make_name_scope("test-program", "", "lab-pc"));
  desc.buffer_width = 64;
  desc.buffer_height = 48;
  desc.placement.size = seam::PanelSize{0.4f, 0.3f, 1.0f};
  return desc;
}

// ---------------------------------------------------------------------------------------------
// T3: streamed panels
// ---------------------------------------------------------------------------------------------

TEST(PanelSender, EveryConsumerCallReturnsWithinMilliseconds)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  slow_down(h);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  const auto test_thread = std::this_thread::get_id();

  std::shared_ptr<seam::IPanelSender> adapter;
  EXPECT_LT(milliseconds_of([&] { adapter = factory.create_panel_sender(stream_desc("dock")); }),
    50.0);
  ASSERT_NE(adapter, nullptr) << factory.last_error();
  EXPECT_TRUE(adapter->pending());

  bool ready = true;
  bool begun = false;
  EXPECT_LT(milliseconds_of([&] { begun = adapter->try_begin_frame(ready); }), 50.0);
  EXPECT_TRUE(begun);
  EXPECT_FALSE(ready);
  EXPECT_EQ(adapter->not_ready_cause(), seam::PanelNotReadyCause::Creating);

  seam::PanelPlacement placed;
  placed.size = seam::PanelSize{0.8f, 0.6f, 1.0f};
  bool accepted = false;
  EXPECT_LT(milliseconds_of([&] { accepted = adapter->set_placement(placed); }), 50.0);
  EXPECT_TRUE(accepted);
  EXPECT_LT(milliseconds_of([&] {
        accepted = adapter->set_state(seam::PanelState::Hidden);
      }), 50.0);
  EXPECT_TRUE(accepted);

  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create) &&
        has_result(r, PanelOpResult::Op::Placement) && has_result(r, PanelOpResult::Op::State);
      })) << "the outcomes arrive within 30 s";
  EXPECT_FALSE(adapter->pending());
  EXPECT_FALSE(adapter->stream_id_text().empty());
  const auto state_result = std::find_if(results.begin(), results.end(),
      [](const PanelOpResult & r) { return r.op == PanelOpResult::Op::State; });
  ASSERT_NE(state_result, results.end());
  EXPECT_EQ(state_result->state, seam::PanelState::Hidden);

  seam::PanelCloseReport report;
  EXPECT_LT(milliseconds_of([&] { report = adapter->close(false); }), 50.0);
  EXPECT_EQ(report.panel, seam::PanelCloseOutcome::Queued);
  EXPECT_FALSE(report.sender_destroyed) << "the close is only queued";
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));

  const auto create = indices_of(h, "gui_panels_get_or_create");
  const auto state = indices_of(h, "gui_panels_set_state");
  const auto close = indices_of(h, "gui_panels_close_panel");
  const auto destroy = indices_of(h, "video_sender_handle_destroy");
  ASSERT_EQ(create.size(), 1u);
  ASSERT_FALSE(state.empty());
  ASSERT_EQ(close.size(), 1u);
  ASSERT_EQ(destroy.size(), 1u);
  EXPECT_LT(create[0], state[0]);
  EXPECT_LT(state.back(), close[0]);
  EXPECT_LT(close[0], destroy[0]);

  const auto calls = h.fake().calls();
  for (const int index : {create[0], state[0], close[0], destroy[0]}) {
    EXPECT_EQ(calls[static_cast<std::size_t>(index)].thread, h.owner_thread())
      << calls[static_cast<std::size_t>(index)].name;
    EXPECT_NE(calls[static_cast<std::size_t>(index)].thread, test_thread);
  }
  const auto panels = h.fake().panels();
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_EQ(panels[0].state, QAR_GUI_PANEL_STATE_CLOSED);
  EXPECT_FLOAT_EQ(panels[0].size.width_meters, 0.8f) << "the placement reached the panel";
}

TEST(PanelSender, ASessionThatIsNotRunningRefusesThePanel)
{
  Harness h;
  h.start();
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  EXPECT_EQ(factory.create_panel_sender(stream_desc("dock")), nullptr);
  EXPECT_FALSE(factory.last_error().empty());
  EXPECT_EQ(h.fake().count("gui_panels_get_or_create"), 0);
}

TEST(PanelSender, AFailedCreateComesBackAsAResultAndThePanelStaysUnusable)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_call_failure("gui_panels_get_or_create", QAR_STATUS_LOGIC_ERROR);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);

  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create, false);
      }));
  EXPECT_FALSE(adapter->pending());
  bool ready = true;
  EXPECT_FALSE(adapter->try_begin_frame(ready));
  EXPECT_FALSE(ready);
  EXPECT_NE(adapter->last_error().find("gui_panels_get_or_create"), std::string::npos);
  EXPECT_FALSE(adapter->set_state(seam::PanelState::Hidden));
  // The sender made before the panel is released with the adapter.
  adapter.reset();
  EXPECT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST(PanelSender, AFrameIsShownOnceThePanelIsReadyAndNeverOnTheOwnerThread)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));

  std::vector<std::uint8_t> pixels(64u * 48u * 4u, 0x7F);
  seam::PanelPixels frame;
  frame.data = pixels.data();
  frame.width = 64;
  frame.height = 48;
  frame.stride_bytes = 64u * 4u;
  bool ready = false;
  ASSERT_TRUE(adapter->try_begin_frame(ready));
  ASSERT_TRUE(ready);
  ASSERT_TRUE(adapter->submit_frame(frame)) << adapter->last_error();
  EXPECT_EQ(h.fake().video_frames_shown(), 1);

  const auto calls = h.fake().calls();
  for (const auto & call : calls) {
    if (call.name == "video_sender_try_begin_frame" || call.name == "video_sender_frame_cpu" ||
      call.name == "video_sender_show_frame")
    {
      EXPECT_EQ(call.thread, std::this_thread::get_id()) << call.name << " stays on the GUI thread";
    }
  }
}

TEST(PanelSender, ThePanelInputIsSubscribedOnTheOwnerThreadAndEndedBeforeTheClose)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  auto input = std::make_shared<qar_capi_panels::QarCApiPanelInputSource>(h.owner().session());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner(), input);
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  EXPECT_TRUE(input->has_panel("dock"));

  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
  EXPECT_FALSE(input->has_panel("dock"));

  const auto subscribed = h.fake().first_index("gui_panels_subscribe_all_gesture_events");
  ASSERT_GE(subscribed, 0);
  EXPECT_EQ(h.fake().calls()[static_cast<std::size_t>(subscribed)].thread, h.owner_thread());
  EXPECT_LT(h.fake().last_index("gui_panels_subscribe_panel_updates"),
    h.fake().last_index("gui_panels_close_panel"));
  EXPECT_LT(h.fake().last_index("cancel_token_cancel"),
    h.fake().last_index("gui_panels_close_panel")) << "input first, then the panel";
  adapter.reset();
}

// ---------------------------------------------------------------------------------------------
// T9: a resize swaps the stream of the same panel
// ---------------------------------------------------------------------------------------------

TEST(PanelSender, ResizeSwapDetachRunsBeforeTheNewCreate)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto old_adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(old_adapter, nullptr);
  std::vector<PanelOpResult> old_results;
  ASSERT_TRUE(drain_until(*old_adapter, old_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));

  // The swap: the old stream lets go, a new one is made for the same panel, back to back.
  old_adapter->detach();
  auto new_adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(new_adapter, nullptr);
  std::vector<PanelOpResult> new_results;
  ASSERT_TRUE(drain_until(*new_adapter, new_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));

  const auto destroy = indices_of(h, "video_sender_handle_destroy");
  const auto create = indices_of(h, "gui_panels_get_or_create");
  ASSERT_EQ(destroy.size(), 1u) << "the old sender is gone, the new one lives";
  ASSERT_EQ(create.size(), 2u);
  EXPECT_LT(destroy[0], create[1]) << "the detach runs before the new panel is found";
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0) << "the panel is kept";
  ASSERT_EQ(h.fake().panels().size(), 1u);
  const auto created = std::find_if(new_results.begin(), new_results.end(),
      [](const PanelOpResult & r) { return r.op == PanelOpResult::Op::Create; });
  ASSERT_NE(created, new_results.end());
  EXPECT_TRUE(created->reattached) << "the second adapter found the panel of the first";
  EXPECT_EQ(h.fake().live_video_senders(), 1);
  EXPECT_EQ(h.fake().panels()[0].state, QAR_GUI_PANEL_STATE_VISIBLE);
}

// ---------------------------------------------------------------------------------------------
// The end of a session: hide, keep, release
// ---------------------------------------------------------------------------------------------

TEST(PanelSender, HideThenDetachRightAwayHidesThePanelAndKeepsIt)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_call_hook([](const std::string & name) {
      if (name == "gui_panels_get_or_create") {
        std::this_thread::sleep_for(200ms);
      }
    });
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  // The create is under way (it makes the panel): the hide and the detach queue behind it, and
  // both run on the panel it made.
  ASSERT_TRUE(wait_until([&h] { return h.fake().count("gui_panels_get_or_create") >= 1; }));
  EXPECT_TRUE(adapter->set_state(seam::PanelState::Hidden));
  adapter->detach();
  EXPECT_FALSE(adapter->set_state(seam::PanelState::Visible))
    << "nothing is queued after a detach";
  ASSERT_TRUE(wait_until([&h] {
        return h.fake().count("video_sender_handle_destroy") == 1 &&
        h.fake().live_video_senders() == 0;
      }));
  const auto panels = h.fake().panels();
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_EQ(panels[0].state, QAR_GUI_PANEL_STATE_HIDDEN) << "the hide ran before the detach";
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0) << "the panel is kept";
  EXPECT_LT(h.fake().first_index("gui_panels_set_state"),
    h.fake().first_index("video_sender_handle_destroy"));
}

TEST(PanelSender, TheEndOfASessionHidesAndDetachesBeforeTheSessionIsReleased)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  auto input = std::make_shared<qar_capi_panels::QarCApiPanelInputSource>(h.owner().session());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner(), input);
  auto first = factory.create_panel_sender(stream_desc("dock"));
  auto second = factory.create_panel_sender(stream_desc("tools"));
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*first, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  ASSERT_TRUE(drain_until(*second, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create) && r.size() >= 2;
      }));

  // What the panel does at the end: hide, detach, then the owner lets go of the session.
  EXPECT_TRUE(first->set_state(seam::PanelState::Hidden));
  first->detach();
  EXPECT_TRUE(second->set_state(seam::PanelState::Hidden));
  second->detach();
  h.owner().disconnect();
  ASSERT_TRUE(h.wait_state(qar_qar_adapter::SessionState::kIdle, 15s));

  for (const auto & panel : h.fake().panels()) {
    EXPECT_EQ(panel.state, QAR_GUI_PANEL_STATE_HIDDEN) << panel.common_name;
  }
  EXPECT_EQ(h.fake().panels().size(), 2u);
  EXPECT_EQ(h.fake().live_video_senders(), 0);
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0);
  const int released = h.fake().first_index("session_handle_destroy");
  ASSERT_GE(released, 0);
  EXPECT_LT(h.fake().last_index("video_sender_handle_destroy"), released);
  EXPECT_LT(h.fake().last_index("gui_panels_set_state"), released);
  EXPECT_LT(h.fake().last_index("cancel_token_cancel"), released + 1);
}

TEST(PanelSender, AFailedStateChangeComesBackAsAResult)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  h.fake().set_call_failure("gui_panels_set_state", QAR_STATUS_LOGIC_ERROR);
  EXPECT_TRUE(adapter->set_state(seam::PanelState::Hidden)) << "accepted";
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::State, false);
      }));
  const auto failed = std::find_if(results.begin(), results.end(),
      [](const PanelOpResult & r) { return r.op == PanelOpResult::Op::State && !r.ok; });
  ASSERT_NE(failed, results.end());
  EXPECT_EQ(failed->state, seam::PanelState::Hidden);
  EXPECT_NE(failed->error.find("gui_panels_set_state"), std::string::npos);
}

TEST(PanelSender, OfSeveralPlacementsWhileTheThreadIsBusyOnlyTheLastIsApplied)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  std::atomic<bool> release{false};
  std::atomic<bool> held{false};
  ASSERT_TRUE(h.owner().post_session_task("hold", [&](QarSession *) {
        held.store(true);
        while (!release.load()) {
          std::this_thread::sleep_for(1ms);
        }
      }));
  ASSERT_TRUE(wait_until([&held] { return held.load(); }));
  for (int i = 1; i <= 5; ++i) {
    seam::PanelPlacement placement;
    placement.size = seam::PanelSize{0.1f * static_cast<float>(i), 0.3f, 1.0f};
    EXPECT_TRUE(adapter->set_placement(placement));
  }
  release.store(true);
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Placement);
      }));
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(h.fake().count("gui_panels_update_pose"), 1) << "five placements make one task";
  EXPECT_FLOAT_EQ(h.fake().panels()[0].size.width_meters, 0.5f);
}

// ---------------------------------------------------------------------------------------------
// A reattach that fails midway leaves the panel of the earlier session alone
// ---------------------------------------------------------------------------------------------

/// A first adapter makes the panel "dock" and lets go of it (hidden, kept), the way the end of a
/// session leaves it.
void leave_stream_panel_behind(Harness & h, qar_capi_panels::QarCApiPanelSenderFactory & factory)
{
  auto first = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(first, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*first, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  EXPECT_TRUE(first->set_state(seam::PanelState::Hidden));
  first->detach();
  first.reset();
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
  ASSERT_EQ(h.fake().panels().size(), 1u);
  ASSERT_EQ(h.fake().count("gui_panels_close_panel"), 0);
}

TEST(PanelSender, AReattachThatFailsInTheMiddleNeverClosesThePanelOfTheEarlierSession)
{
  for (const char * failing : {"gui_panels_set_stream", "gui_panels_update_pose"}) {
    SCOPED_TRACE(failing);
    Harness h;
    h.start();
    ASSERT_TRUE(h.connect_and_run());
    qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
    ASSERT_NO_FATAL_FAILURE(leave_stream_panel_behind(h, factory));

    h.fake().set_call_failure(failing, QAR_STATUS_LOGIC_ERROR);
    auto second = factory.create_panel_sender(stream_desc("dock"));
    ASSERT_NE(second, nullptr);
    std::vector<PanelOpResult> results;
    ASSERT_TRUE(drain_until(*second, results, [](const std::vector<PanelOpResult> & r) {
          return has_result(r, PanelOpResult::Op::Create, false);
        })) << "the reattach step fails";
    EXPECT_NE(second->last_error().find(failing), std::string::npos);

    // What the consumer does with a failed adapter: it drops it.
    second.reset();
    ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
    h.owner().disconnect();
    ASSERT_TRUE(h.wait_state(qar_qar_adapter::SessionState::kIdle, 15s));
    EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0)
      << "the panel of the earlier session is left alone";
    ASSERT_EQ(h.fake().panels().size(), 1u);
    EXPECT_NE(h.fake().panels()[0].state, QAR_GUI_PANEL_STATE_CLOSED);
  }
}

// ---------------------------------------------------------------------------------------------
// The frames do not wait for the attach probe
// ---------------------------------------------------------------------------------------------

/// The measured profile of the installed QAROS: the two queries of the attach probe take 2
/// seconds, the calls that make and change a panel are fast.
void slow_panel_queries(Harness & h)
{
  h.fake().set_call_hook([](const std::string & name) {
      if (name == "query_gui_panels_count" || name == "query_gui_panels") {
        std::this_thread::sleep_for(2000ms);
      }
    });
}

/// One frame through the adapter, the way the GUI thread does it. True when it was shown.
bool show_one_frame(
  seam::IPanelSender & adapter, const std::vector<std::uint8_t> & data)
{
  bool ready = false;
  if (!adapter.try_begin_frame(ready) || !ready) {
    return false;
  }
  seam::PanelPixels frame;
  frame.data = data.data();
  frame.width = 64;
  frame.height = 48;
  frame.stride_bytes = 64u * 4u;
  return adapter.submit_frame(frame);
}

TEST(PanelSender, FramesFlowWhileTheAttachProbeRuns)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  slow_panel_queries(h);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  ASSERT_TRUE(wait_until([&h] { return h.fake().count("gui_panels_get_or_create") >= 1; }, 5s));
  const auto panel_made = Clock::now();

  // The panel exists; its attach probe is still in the first slow query.
  std::vector<std::uint8_t> pixels(64u * 48u * 4u, 0x7F);
  ASSERT_TRUE(wait_until([&] { return !adapter->pending() && show_one_frame(*adapter, pixels); },
      1500ms)) << "a frame is shown while the probe runs";
  const double after_ms =
    std::chrono::duration<double, std::milli>(Clock::now() - panel_made).count();
  EXPECT_LT(after_ms, 300.0);
  EXPECT_EQ(h.fake().video_frames_shown(), 1);

  std::vector<PanelOpResult> results;
  adapter->drain_results(results);
  EXPECT_FALSE(has_result(results, PanelOpResult::Op::Create))
    << "the probe has not finished: no result yet";

  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  EXPECT_TRUE(show_one_frame(*adapter, pixels)) << "frames go on after the probe";
  for (const auto & call : h.fake().calls()) {
    if (call.name == "video_sender_try_begin_frame" || call.name == "video_sender_frame_cpu" ||
      call.name == "video_sender_show_frame")
    {
      EXPECT_EQ(call.thread, std::this_thread::get_id()) << call.name << " stays on the GUI thread";
    }
  }
}

TEST(PanelSender, FramesFlowBeforeTheReattachSwapsTheStreamAndGoOnAfterIt)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  ASSERT_NO_FATAL_FAILURE(leave_stream_panel_behind(h, factory));
  const int frames_before = h.fake().video_frames_shown();
  slow_panel_queries(h);

  auto second = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(second, nullptr);
  ASSERT_TRUE(wait_until([&h] { return h.fake().count("gui_panels_get_or_create") >= 2; }, 5s));
  std::vector<std::uint8_t> pixels(64u * 48u * 4u, 0x7F);
  ASSERT_TRUE(wait_until([&] { return !second->pending() && show_one_frame(*second, pixels); },
      1500ms)) << "a frame is shown before the probe has read the earlier panel";
  EXPECT_EQ(h.fake().count("gui_panels_set_stream"), 0) << "the stream is not swapped yet";

  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*second, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  const auto created = std::find_if(results.begin(), results.end(),
      [](const PanelOpResult & r) { return r.op == PanelOpResult::Op::Create; });
  ASSERT_NE(created, results.end());
  EXPECT_TRUE(created->reattached);
  const int swapped = h.fake().last_index("gui_panels_set_stream");
  const int shown = h.fake().last_index("gui_panels_set_state");
  ASSERT_GE(swapped, 0);
  EXPECT_GT(shown, swapped) << "the stream is swapped, then the panel is shown";
  const auto calls = h.fake().calls();
  EXPECT_EQ(calls[static_cast<std::size_t>(swapped)].thread, h.owner_thread());
  EXPECT_EQ(calls[static_cast<std::size_t>(shown)].thread, h.owner_thread());
  ASSERT_EQ(h.fake().panels().size(), 1u);
  EXPECT_EQ(h.fake().panels()[0].state, QAR_GUI_PANEL_STATE_VISIBLE);
  EXPECT_TRUE(h.fake().panels()[0].has_stream);
  EXPECT_FALSE(second->pending());
  EXPECT_TRUE(show_one_frame(*second, pixels)) << "the same sender goes on after the swap";
  EXPECT_EQ(h.fake().video_frames_shown(), frames_before + 2);
}

TEST(PanelSender, AReattachThatFailsAfterTheFramesOpenedStopsTheFrames)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  ASSERT_NO_FATAL_FAILURE(leave_stream_panel_behind(h, factory));
  h.fake().set_call_failure("gui_panels_set_stream", QAR_STATUS_LOGIC_ERROR);
  slow_panel_queries(h);

  auto second = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(second, nullptr);
  ASSERT_TRUE(wait_until([&h] { return h.fake().count("gui_panels_get_or_create") >= 2; }, 5s));
  std::vector<std::uint8_t> pixels(64u * 48u * 4u, 0x7F);
  ASSERT_TRUE(wait_until([&] { return !second->pending() && show_one_frame(*second, pixels); },
      1500ms)) << "frames flow while the probe runs";

  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*second, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create, false);
      })) << "the swap fails";
  EXPECT_FALSE(second->pending());
  bool ready = true;
  EXPECT_FALSE(second->try_begin_frame(ready)) << "a failed panel takes no more frames";
  EXPECT_FALSE(ready);
  EXPECT_NE(second->last_error().find("gui_panels_set_stream"), std::string::npos);

  second.reset();
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
  h.owner().disconnect();
  ASSERT_TRUE(h.wait_state(qar_qar_adapter::SessionState::kIdle, 15s));
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0)
    << "the panel of the earlier session is left alone";
}

TEST(PanelSender, ANameTakenByAClosedPanelKeepsTheFramesOpenAcrossTheRetry)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto first = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(first, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*first, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  first->close(false);
  first.reset();
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
  slow_panel_queries(h);

  auto second = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(second, nullptr);
  ASSERT_TRUE(wait_until([&h] { return h.fake().count("gui_panels_get_or_create") >= 2; }, 5s));
  std::vector<std::uint8_t> pixels(64u * 48u * 4u, 0x7F);
  ASSERT_TRUE(wait_until([&] { return !second->pending() && show_one_frame(*second, pixels); },
      1500ms));

  std::vector<PanelOpResult> second_results;
  ASSERT_TRUE(drain_until(*second, second_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  EXPECT_EQ(h.fake().count("gui_panels_get_or_create"), 3) << "the name with a suffix is made";
  EXPECT_EQ(h.fake().panels().size(), 2u);
  EXPECT_TRUE(show_one_frame(*second, pixels)) << "the same sender serves the new panel";
}

TEST(PanelSender, TheProbeAsksOnceAndFindsOurPanelInALongList)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  // 70 panels of other makers come first in the list: ours is beyond the first 64 handles.
  ASSERT_TRUE(h.owner().post_session_task("fill", [](QarSession * session) {
        for (int i = 0; i < 70; ++i) {
          const std::string name = "filler-" + std::to_string(i);
          QarGuiPanelInit init = qar_gui_panel_init_default();
          init.common_name = name.c_str();
          QarGuiPanelId id{};
          qar_gui_panels_get_or_create(session, &init, &id);
        }
      }));
  ASSERT_TRUE(wait_until([&h] { return h.fake().panels().size() == 70u; }));
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto first = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(first, nullptr);
  std::vector<PanelOpResult> first_results;
  ASSERT_TRUE(drain_until(*first, first_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  EXPECT_TRUE(first->set_state(seam::PanelState::Hidden));
  first->detach();
  first.reset();
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
  ASSERT_EQ(h.fake().panels().size(), 71u);
  auto second = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(second, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*second, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  const auto created = std::find_if(results.begin(), results.end(),
      [](const PanelOpResult & r) { return r.op == PanelOpResult::Op::Create; });
  ASSERT_NE(created, results.end());
  EXPECT_TRUE(created->reattached) << "the panel of the earlier session was found in the list";
  EXPECT_EQ(h.fake().count("query_gui_panels_count"), 0) << "no count is asked for";
  EXPECT_GE(h.fake().count("query_gui_panels"), 2) << "the first handle buffer was too short";
  EXPECT_EQ(h.fake().panels()[70].state, QAR_GUI_PANEL_STATE_VISIBLE);
}

// ---------------------------------------------------------------------------------------------
// A panel that was never made costs nothing at the end of the session
// ---------------------------------------------------------------------------------------------

TEST(PanelSender, ObsoleteCreatesDoNotKeepTheHideAndDetachOfARealPanelFromRunningInTime)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto real = factory.create_panel_sender(stream_desc("real"));
  ASSERT_NE(real, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*real, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  slow_down(h);

  // Four panels are asked for and dropped before their turn comes, behind a busy thread.
  OwnerHold hold;
  ASSERT_TRUE(hold_owner(h, hold));
  std::vector<std::shared_ptr<seam::IPanelSender>> obsolete;
  for (int i = 0; i < 4; ++i) {
    obsolete.push_back(factory.create_panel_sender(stream_desc("gone" + std::to_string(i))));
    ASSERT_NE(obsolete.back(), nullptr);
  }
  for (auto & adapter : obsolete) {
    EXPECT_TRUE(adapter->set_state(seam::PanelState::Hidden));
    adapter->detach();
  }
  // The end of the session: hide and detach the real panel, then let go of the session.
  EXPECT_TRUE(real->set_state(seam::PanelState::Hidden));
  real->detach();
  h.owner().disconnect();
  const auto start = Clock::now();
  hold.release.store(true);
  ASSERT_TRUE(h.wait_state(qar_qar_adapter::SessionState::kIdle, 20s));
  const double took_s = std::chrono::duration<double>(Clock::now() - start).count();

  EXPECT_LT(took_s, 8.0) << "the hide and the detach ran within the drain budget";
  EXPECT_EQ(h.fake().count("gui_panels_get_or_create"), 1) << "no panel was made for a dropped one";
  EXPECT_EQ(h.fake().count("video_sender_create"), 1);
  ASSERT_EQ(h.fake().panels().size(), 1u);
  EXPECT_EQ(h.fake().panels()[0].state, QAR_GUI_PANEL_STATE_HIDDEN) << "the real panel was hidden";
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0);
  EXPECT_EQ(h.fake().live_video_senders(), 0);
}

// ---------------------------------------------------------------------------------------------
// A panel that QarOS reports in the opposite state right after the adapter set it
// ---------------------------------------------------------------------------------------------

/// The panel kinds the guard is tested on: the adapter type and how to make one.
struct SenderKit
{
  using Adapter = qar_capi_panels::QarCApiPanelSenderAdapter;
  using Factory = qar_capi_panels::QarCApiPanelSenderFactory;
  static Factory factory(Harness & h) { return Factory(&h.owner()); }
  static bool set_state(Adapter & adapter, QarGuiPanelState state)
  {
    return adapter.set_state(qar_capi_panels::to_seam(state));
  }
  static std::shared_ptr<Adapter> make(Factory & factory, const std::string & name)
  {
    return std::static_pointer_cast<Adapter>(factory.create_panel_sender(stream_desc(name)));
  }
};

TEST(PanelStateGuard, AStreamPanelThatQarOsReportsHiddenAfterTheReattachIsShownAgain)
{
  expect_one_ask_for_a_contrary_report<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelIsAskedThreeTimesAtMostAndThenWarnedAbout)
{
  expect_at_most_three_asks_then_a_warning<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelIsNotAskedAgainForTheEchoOfTheStateBeforeAnAsk)
{
  expect_a_report_right_after_an_ask_not_to_be_asked_again<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelThatQarOsHidesAgainInsideTheHoldAndFallsSilentIsAskedAgain)
{
  expect_a_silent_runtime_to_be_asked_again_when_the_hold_ends<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelThatQarOsShowedAgainIsNotAskedWhenTheHoldEnds)
{
  expect_no_ask_after_the_hold_when_a_report_confirmed_the_state<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelAskedByTicksStaysWithinTheBudgetOfThreeAsks)
{
  expect_the_ticks_to_keep_the_budget<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelIsNotAskedByATickSixSecondsAfterTheChange)
{
  expect_a_tick_to_leave_the_operators_state_alone<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelCountsFromOneAgainAfterAStateItSetItself)
{
  expect_an_own_state_to_start_the_count_anew<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelReportedHiddenSixSecondsAfterTheChangeIsLeftAlone)
{
  expect_a_late_report_to_be_left_alone<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelNeverContestsClosedOrMinimized)
{
  expect_closed_and_minimized_to_be_left_alone<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelThatTheAdapterHidesIsNotShownAgain)
{
  expect_an_own_hide_not_to_be_shown_again<SenderKit>();
}

TEST(PanelStateGuard, AStreamPanelAsksForNothingWhileItsOwnStateTaskIsQueued)
{
  expect_nothing_to_be_asked_while_an_own_state_task_is_queued<SenderKit>();
}

TEST(PanelStateGuard, TheWatchOfAStreamPanelEndsWithItsClose)
{
  expect_the_watch_to_end_with_the_panel<SenderKit>(false);
}

TEST(PanelStateGuard, TheWatchOfAStreamPanelEndsWithItsDetach)
{
  expect_the_watch_to_end_with_the_panel<SenderKit>(true);
}

// ---------------------------------------------------------------------------------------------
// The values of the seam reach the QarOS calls, and the answers come back as seam values
// ---------------------------------------------------------------------------------------------

TEST(PanelSeamBoundary, ThePoseAndTheSizeOfTheDescriptionReachTheQarOsPanel)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  seam::PanelDesc desc = stream_desc("dock");
  desc.placement.pose.orientation = seam::Quaternion{0.0f, 0.5f, 0.0f, 0.8660254f};
  desc.placement.pose.position = seam::Vector3{1.0f, 1.5f, -2.0f};
  desc.placement.size = seam::PanelSize{0.8f, 0.6f, 1.5f};
  auto adapter = factory.create_panel_sender(desc);
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  const auto panels = h.fake().panels();
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_FLOAT_EQ(panels[0].pose.orientation.y, 0.5f);
  EXPECT_FLOAT_EQ(panels[0].pose.orientation.w, 0.8660254f);
  EXPECT_FLOAT_EQ(panels[0].pose.position.x, 1.0f);
  EXPECT_FLOAT_EQ(panels[0].pose.position.y, 1.5f);
  EXPECT_FLOAT_EQ(panels[0].pose.position.z, -2.0f);
  EXPECT_FLOAT_EQ(panels[0].size.width_meters, 0.8f);
  EXPECT_FLOAT_EQ(panels[0].size.height_meters, 0.6f);
  EXPECT_FLOAT_EQ(panels[0].size.content_scale, 1.5f);
}

TEST(PanelSeamBoundary, ThePoseAndTheSizeOfAPlacementReachTheQarOsPanel)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  seam::PanelPlacement placement;
  placement.pose.orientation = seam::Quaternion{0.5f, 0.5f, 0.5f, 0.5f};
  placement.pose.position = seam::Vector3{-1.0f, 2.0f, 3.0f};
  placement.size = seam::PanelSize{1.2f, 0.9f, 1.0f};
  ASSERT_TRUE(adapter->set_placement(placement));
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Placement);
      }));
  const auto panels = h.fake().panels();
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_FLOAT_EQ(panels[0].pose.orientation.x, 0.5f);
  EXPECT_FLOAT_EQ(panels[0].pose.position.x, -1.0f);
  EXPECT_FLOAT_EQ(panels[0].pose.position.z, 3.0f);
  EXPECT_FLOAT_EQ(panels[0].size.width_meters, 1.2f);
  EXPECT_FLOAT_EQ(panels[0].size.height_meters, 0.9f);
}

TEST(PanelSeamBoundary, ASeamStateReachesTheQarOsPanelAndComesBackAsTheSameSeamState)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  EXPECT_TRUE(adapter->set_state(seam::PanelState::Minimized));
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::State);
      }));
  const auto state_result = std::find_if(results.begin(), results.end(),
      [](const PanelOpResult & r) { return r.op == PanelOpResult::Op::State; });
  ASSERT_NE(state_result, results.end());
  EXPECT_EQ(state_result->state, seam::PanelState::Minimized);
  ASSERT_EQ(h.fake().panels().size(), 1u);
  EXPECT_EQ(h.fake().panels()[0].state, QAR_GUI_PANEL_STATE_MINIMIZED);
}

}  // namespace
