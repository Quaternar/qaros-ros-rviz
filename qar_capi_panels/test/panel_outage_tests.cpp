// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The panel path when QAROS stops answering: no call of the GUI thread waits for the Hub, the
// handles of a lost session are released on the thread that owns it (the release lane) or are
// left alone, a sender whose frame calls block is stopped, and a panel that a lost session left
// behind is found again by the next one. The owner runs on the fake QarOS library, whose Hub is
// blocked or lost from the test. Every test runs twice: with the frame calls on the frame worker
// and with the frame calls on the GUI thread (QAR_FRAME_CALLS=worker and gui).

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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

using qar_capi_panels::QarCApiPanelInputSource;
using qar_capi_panels::QarCApiPanelSenderAdapter;
using qar_capi_panels::QarCApiPanelSenderFactory;
using qar_qar_adapter::IQarSessionOwner;
using qar_qar_adapter::IReleaseLane;
using qar_qar_adapter::SessionState;

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

/// A lane that keeps what it is given until the test runs it, on a thread of its own.
class RecordingLane : public IReleaseLane
{
public:
  bool post_release(const char * name, std::function<void()> release) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (refuse_) {
      return false;
    }
    names_.push_back(name);
    items_.push_back(std::move(release));
    return true;
  }

  void set_refuse(bool refuse)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    refuse_ = refuse;
  }

  std::vector<std::string> names() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return names_;
  }

  /// Runs every item that is waiting on one new thread and waits for it.
  void run_items()
  {
    std::vector<std::function<void()>> items;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      items.swap(items_);
    }
    std::thread runner([&items, this] {
        thread_id_ = std::this_thread::get_id();
        for (auto & item : items) {
          item();
        }
      });
    runner.join();
  }

  std::thread::id thread_id() const { return thread_id_; }

private:
  mutable std::mutex mutex_;
  bool refuse_ = false;
  std::vector<std::string> names_;
  std::vector<std::function<void()>> items_;
  std::thread::id thread_id_;
};

/// An owner of a session that was lost: it has a session pointer and a lane and accepts no task
/// once told so. Tasks it accepts run at once on the calling thread.
class LostOwner : public IQarSessionOwner
{
public:
  LostOwner(QarSession * session, std::shared_ptr<IReleaseLane> lane)
  : session_(session), lane_(std::move(lane)) {}

  bool ready() const override { return true; }
  QarSession * session() const override { return session_; }
  bool post_session_task(
    const char * name, SessionTask task, std::function<void()> dropped = {}) override
  {
    if (lost_.load()) {
      return false;
    }
    return IQarSessionOwner::post_session_task(name, std::move(task), std::move(dropped));
  }
  std::shared_ptr<IReleaseLane> release_lane() const override { return lane_; }
  void lose() { lost_.store(true); }

private:
  QarSession * session_;
  std::shared_ptr<IReleaseLane> lane_;
  std::atomic<bool> lost_{false};
};

/// Waits until the adapter reports its panel made.
bool made(QarCApiPanelSenderAdapter & adapter)
{
  std::vector<PanelOpResult> results;
  return drain_until(adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      });
}

/// Both ways of making the frame calls, see FrameCallOptions.
class PanelOutage : public ::testing::TestWithParam<bool>
{
protected:
  bool on_worker() const {return GetParam();}

  qar_capi_panels::FrameCallOptions options() const
  {
    qar_capi_panels::FrameCallOptions options;
    options.on_worker = GetParam();
    return options;
  }
};

INSTANTIATE_TEST_SUITE_P(
  FrameCalls, PanelOutage, ::testing::Bool(),
  [](const ::testing::TestParamInfo<bool> & info) {return info.param ? "Worker" : "Gui";});

/// Asks for a cycle the way the ticks of the consumer do, until one is reserved or @p limit
/// passes.
bool begin_until_ready(
  QarCApiPanelSenderAdapter & adapter, std::chrono::milliseconds limit = 3000ms)
{
  const auto end = Clock::now() + limit;
  while (Clock::now() < end) {
    bool ready = false;
    if (!adapter.try_begin_frame(ready)) {
      return false;
    }
    if (ready) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

/// Keeps asking for a cycle and submitting what is reserved until the frame calls of the panel
/// are paused or @p limit passes.
bool drive_until_stopped(
  QarCApiPanelSenderAdapter & adapter, const seam::PanelPixels & pixels,
  std::chrono::milliseconds limit = 5000ms)
{
  const auto end = Clock::now() + limit;
  while (Clock::now() < end) {
    bool ready = false;
    adapter.try_begin_frame(ready);
    if (ready) {
      adapter.submit_frame(pixels);
    }
    if (adapter.frame_calls_stopped()) {
      return true;
    }
    std::this_thread::sleep_for(1ms);
  }
  return false;
}

/// The pixels of one frame of the test panels.
struct TestFrame
{
  TestFrame()
  : buffer(64 * 48 * 4, 7)
  {
    pixels.data = buffer.data();
    pixels.width = 64;
    pixels.height = 48;
    pixels.stride_bytes = 64 * 4;
  }
  std::vector<std::uint8_t> buffer;
  seam::PanelPixels pixels;
};

/// Waits until the frame workers of the process are as many as @p expected.
bool workers_running_are(int expected)
{
  return wait_until([expected] { return qar_qar_adapter::FrameWorker::running() == expected; });
}

}  // namespace

TEST_P(PanelOutage, CloseInALostSessionReleasesTheVideoSenderOnTheLane)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  auto input = std::make_shared<QarCApiPanelInputSource>(
    h.owner().session(), h.owner().release_lane());
  QarCApiPanelSenderFactory factory(&h.owner(), input);
  factory.set_frame_call_options(options());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr) << factory.last_error();
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  ASSERT_EQ(h.fake().live_video_senders(), 1);
  const int subscriptions = h.fake().panel_subscriptions();
  ASSERT_EQ(subscriptions, 2) << "the state watch of the adapter and the input source's";
  ASSERT_TRUE(input->has_panel("dock"));

  // The Hub is given up: the session cannot take a task any more.
  h.owner().declare_lost("the test gave the session up");
  ASSERT_TRUE(h.wait_state(SessionState::kLost));
  ASSERT_FALSE(h.owner().post_session_task("late", [](QarSession *) {}));

  seam::PanelCloseReport report;
  EXPECT_LT(milliseconds_of([&] { report = adapter->close(false); }), 20.0);
  EXPECT_EQ(report.panel, seam::PanelCloseOutcome::NoPanel) << "no task could be queued";
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }))
    << "the sender is destroyed on the lane";
  EXPECT_EQ(
    h.fake().thread_of_last("video_sender_handle_destroy"), h.owner_thread())
    << "on the thread that owns the session";
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0) << "the panel stays for the next session";
  ASSERT_EQ(h.fake().panels().size(), 1u);
  ASSERT_TRUE(wait_until([&h] { return h.fake().panel_subscriptions() == 0; }))
    << "the state watch and the input tokens are cancelled on the lane as well";
  EXPECT_EQ(h.fake().thread_of_last("cancel_token_cancel"), h.owner_thread());
  EXPECT_EQ(h.fake().thread_of_last("cancel_token_handle_destroy"), h.owner_thread());

  // A second close finds nothing to do.
  adapter->close(false);
  EXPECT_EQ(h.fake().count("video_sender_handle_destroy"), 1);
  adapter.reset();
  h.owner().release_lost_session();
}

TEST_P(PanelOutage, AClosePostedBeforeTheLossIsReleasedOnTheLaneWhenItIsDropped)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  QarCApiPanelSenderFactory factory(&h.owner());
  factory.set_frame_call_options(options());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));

  // The owner is busy; the loss is asked for first, so the owner has not yet taken it when the
  // close is queued, and drops the close when it does.
  OwnerHold hold;
  ASSERT_TRUE(hold_owner(h, hold));
  h.owner().declare_lost("the test gave the session up");
  adapter->close(false);
  hold.release.store(true);
  ASSERT_TRUE(h.wait_state(SessionState::kLost));
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }))
    << "the task that was dropped handed the sender to the lane";
  EXPECT_EQ(h.fake().count("video_sender_handle_destroy"), 1);
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0) << "the teardown task did not run";
  EXPECT_EQ(h.fake().thread_of_last("video_sender_handle_destroy"), h.owner_thread());
  ASSERT_TRUE(wait_until([&h] { return h.fake().panel_subscriptions() == 0; }))
    << "the state watch of the dropped close is cancelled on the lane";
  EXPECT_EQ(h.fake().thread_of_last("cancel_token_cancel"), h.owner_thread());
  adapter.reset();
  h.owner().release_lost_session();
}

TEST_P(PanelOutage, DrainResultsNeverWaitsWhileTheOwnerIsBlocked)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  QarCApiPanelSenderFactory factory(&h.owner());
  factory.set_frame_call_options(options());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));

  // The Hub stops answering: the next call of the owner thread waits for it.
  h.fake().set_hub_gone(qar_qar_adapter::test::HubGone::kBlockAll);
  seam::PanelPlacement placed;
  placed.size = seam::PanelSize{0.8f, 0.6f, 1.0f};
  ASSERT_TRUE(adapter->set_placement(placed));
  ASSERT_TRUE(wait_until([&h] { return h.fake().hub_blocked_calls() > 0; }))
    << "the owner thread is inside a call that waits";

  double worst_ms = 0.0;
  const auto timed = [&worst_ms](const std::function<void()> & call) {
      worst_ms = std::max(worst_ms, milliseconds_of(call));
    };
  bool ready = false;
  for (int i = 0; i < 20; ++i) {
    timed([&] { adapter->drain_results(results); });
    timed([&] { adapter->set_placement(placed); });
    timed([&] { adapter->set_state(seam::PanelState::Hidden); });
    timed([&] { (void)adapter->pending(); });
    timed([&] { adapter->try_begin_frame(ready); });
  }
  EXPECT_LT(worst_ms, 20.0) << "no call of the GUI thread waited for the Hub";
  seam::PanelCloseReport report;
  timed([&] { report = adapter->close(false); });
  EXPECT_LT(worst_ms, 20.0) << "the close only queues its work";
  EXPECT_EQ(report.panel, seam::PanelCloseOutcome::Queued);

  h.fake().set_hub_gone(qar_qar_adapter::test::HubGone::kNo);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }))
    << "the queued close runs when the Hub answers again";
  adapter.reset();
}

TEST_P(PanelOutage, TwoSlowFrameCallsStopTheFrameCallsOfThatPanel)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, options());
  ASSERT_FALSE(adapter->refused());
  ASSERT_EQ(adapter->frame_calls_on_worker(), on_worker());
  ASSERT_TRUE(made(*adapter));
  ASSERT_TRUE(wait_until([&adapter] { return adapter->ready(); }));

  // A fast call counts for nothing.
  TestFrame frame;
  ASSERT_TRUE(begin_until_ready(*adapter));
  EXPECT_FALSE(adapter->frame_calls_stopped());
  EXPECT_TRUE(adapter->submit_frame(frame.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().video_frames_shown() == 1; }));

  // One slow call is not enough; the second one stops the calls and says so once.
  h.fake().set_frame_delay_ms(320, "video_sender_try_begin_frame");
  bool ready = false;
  testing::internal::CaptureStderr();
  if (on_worker()) {
    ASSERT_TRUE(drive_until_stopped(*adapter, frame.pixels));
  } else {
    ASSERT_TRUE(adapter->try_begin_frame(ready));
    EXPECT_FALSE(adapter->frame_calls_stopped()) << "a single slow call";
    ASSERT_TRUE(adapter->try_begin_frame(ready));
  }
  const std::string log = testing::internal::GetCapturedStderr();
  EXPECT_TRUE(adapter->frame_calls_stopped());
  ready = true;
  ASSERT_TRUE(adapter->try_begin_frame(ready));
  EXPECT_FALSE(ready) << "the cycle of the call that tripped it is not used";
  EXPECT_NE(
    log.find("panel 'dock' frame calls stopped: try_begin_frame took "), std::string::npos) << log;
  EXPECT_NE(log.find(" ms twice"), std::string::npos) << log;

  // From then on no frame call reaches QarOS and the panel reports no destination.
  h.fake().set_frame_delay_ms(0, "video_sender_try_begin_frame");
  std::this_thread::sleep_for(30ms);
  const int begins = h.fake().count("video_sender_try_begin_frame");
  const int cpus = h.fake().count("video_sender_frame_cpu");
  const int shows = h.fake().count("video_sender_show_frame");
  for (int i = 0; i < 5; ++i) {
    ready = true;
    EXPECT_TRUE(adapter->try_begin_frame(ready));
    EXPECT_FALSE(ready);
    EXPECT_FALSE(adapter->submit_frame(frame.pixels)) << "no cycle was reserved";
  }
  std::this_thread::sleep_for(30ms);
  EXPECT_EQ(h.fake().count("video_sender_try_begin_frame"), begins);
  EXPECT_EQ(h.fake().count("video_sender_frame_cpu"), cpus);
  EXPECT_EQ(h.fake().count("video_sender_show_frame"), shows);

  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST_P(PanelOutage, SlowCallsOfTheOtherFramePathsCountToo)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, options());
  ASSERT_TRUE(made(*adapter));
  ASSERT_TRUE(wait_until([&adapter] { return adapter->ready(); }));
  TestFrame frame;

  // A show that takes long, twice: the second one stops the calls.
  h.fake().set_frame_delay_ms(300, "video_sender_show_frame");
  testing::internal::CaptureStderr();
  const bool stopped = drive_until_stopped(*adapter, frame.pixels);
  const std::string log = testing::internal::GetCapturedStderr();
  EXPECT_TRUE(stopped);
  EXPECT_TRUE(adapter->frame_calls_stopped());
  EXPECT_NE(log.find("frame calls stopped: show_frame took "), std::string::npos) << log;
  h.fake().set_frame_delay_ms(0);
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

// Breaker times shortened by a factor of about 40: a call of 100 ms or more is slow, the delay of
// a slow call is 130 ms.
QarCApiPanelSenderAdapter::FrameBreakerTimings quick_breaker()
{
  QarCApiPanelSenderAdapter::FrameBreakerTimings timings;
  timings.slow_call = std::chrono::milliseconds(100);
  timings.window = std::chrono::milliseconds(1000);
  timings.pause_first = std::chrono::milliseconds(150);
  timings.pause_cap = std::chrono::milliseconds(600);
  timings.calm_reset = std::chrono::milliseconds(1500);
  return timings;
}

constexpr const char * kBeginFrame = "video_sender_try_begin_frame";

double milliseconds_since(std::chrono::steady_clock::time_point start)
{
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration<double, std::milli>(elapsed).count();
}

// A panel sender with the quick breaker whose two slow try_begin_frame calls have paused the
// frame calls.
struct PausedPanel
{
  explicit PausedPanel(const qar_capi_panels::FrameCallOptions & options)
  {
    h.start();
    ok = h.connect_and_run();
    if (!ok) {
      return;
    }
    h.fake().set_video_sender_ready(true);
    adapter = std::make_shared<QarCApiPanelSenderAdapter>(
      &h.owner(), stream_desc("dock"), nullptr, options);
    ok = !adapter->refused() && made(*adapter) &&
      wait_until([this] { return adapter->ready(); });
    if (!ok) {
      return;
    }
    adapter->set_frame_breaker_timings(quick_breaker());
    trip();
  }

  ~PausedPanel()
  {
    h.fake().set_frame_delay_ms(0);
    if (adapter) {
      adapter->close(false);
      wait_until([this] { return h.fake().live_video_senders() == 0; });
    }
  }

  // Two slow try_begin_frame calls; the frame calls are paused when it returns.
  void trip()
  {
    h.fake().set_frame_delay_ms(130, kBeginFrame);
    ok = drive_until_stopped(*adapter, frame.pixels) && ok;
    h.fake().set_frame_delay_ms(0, kBeginFrame);
  }

  // Asks for a frame every 2 ms until a try_begin_frame reaches the fake (the probe). Returns the
  // milliseconds from @p since to the moment that call was entered, or -1 when none did in 3 s.
  double probe_after(std::chrono::steady_clock::time_point since)
  {
    const int base = h.fake().count(kBeginFrame);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < end) {
      const double entered_ms = milliseconds_since(since);
      bool ready = false;
      adapter->try_begin_frame(ready);
      if (h.fake().count(kBeginFrame) > base) {
        return entered_ms;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return -1.0;
  }

  qar_qar_adapter::test::Harness h;
  std::shared_ptr<QarCApiPanelSenderAdapter> adapter;
  TestFrame frame;
  bool ok = false;
};

TEST_P(PanelOutage, AFastProbeFrameAfterThePauseResumesThePanel)
{
  PausedPanel panel(options());
  ASSERT_TRUE(panel.ok);
  auto & adapter = panel.adapter;
  ASSERT_TRUE(adapter->frame_calls_stopped());

  // During the pause no frame call is made.
  bool ready = true;
  std::this_thread::sleep_for(30ms);
  const int begins = panel.h.fake().count(kBeginFrame);
  EXPECT_TRUE(adapter->try_begin_frame(ready));
  EXPECT_FALSE(ready);
  std::this_thread::sleep_for(30ms);
  EXPECT_EQ(panel.h.fake().count(kBeginFrame), begins);

  // After it the next frame is the probe; it is fast, so the panel resumes and one line says how
  // long the frame calls were paused.
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  testing::internal::CaptureStderr();
  const bool reserved = begin_until_ready(*adapter, 2000ms);
  const std::string log = testing::internal::GetCapturedStderr();
  EXPECT_TRUE(reserved);
  EXPECT_FALSE(adapter->frame_calls_stopped());
  EXPECT_NE(log.find("panel 'dock' frame calls resumed after 0."), std::string::npos) << log;
  EXPECT_EQ(panel.h.fake().count(kBeginFrame), begins + 1);
}

TEST_P(PanelOutage, ASlowProbeFrameDoublesThePauseUpToTheCap)
{
  PausedPanel panel(options());
  ASSERT_TRUE(panel.ok);
  panel.h.fake().set_frame_delay_ms(130, kBeginFrame);

  // The pause from a trip or a slow probe is 150, 300, 600, 600 ms (the cap): the next probe is
  // not made earlier. The wait is measured from the end of the call that started the pause (from
  // its start when the call is made on the worker, which adds the 130 ms of the slow call).
  const double pauses_ms[] = {150.0, 300.0, 600.0, 600.0};
  auto since = std::chrono::steady_clock::now();
  for (const double pause : pauses_ms) {
    const double waited = panel.probe_after(since);
    ASSERT_GE(waited, 0.0) << "no probe within 3 s after a pause of " << pause << " ms";
    EXPECT_GE(waited, pause - 20.0) << "the pause is not cut short";
    EXPECT_LE(waited, pause + 400.0) << "the pause is not longer than the cap allows";
    EXPECT_TRUE(panel.adapter->frame_calls_stopped()) << "a slow probe keeps the pause";
    since = std::chrono::steady_clock::now();
  }
}

TEST_P(PanelOutage, ThePauseIsTheFirstOneAgainAfterAHealthyWindow)
{
  PausedPanel panel(options());
  ASSERT_TRUE(panel.ok);
  // One slow probe (the pause becomes 300 ms), then a fast one that resumes.
  panel.h.fake().set_frame_delay_ms(130, kBeginFrame);
  ASSERT_GE(panel.probe_after(std::chrono::steady_clock::now()), 0.0);
  std::this_thread::sleep_for(200ms);
  panel.h.fake().set_frame_delay_ms(0, kBeginFrame);
  ASSERT_GE(panel.probe_after(std::chrono::steady_clock::now()), 0.0);
  ASSERT_TRUE(wait_until([&panel] { return !panel.adapter->frame_calls_stopped(); }));

  // No slow call for longer than the healthy window: the next pause is 150 ms, not 300.
  std::this_thread::sleep_for(std::chrono::milliseconds(1600));
  panel.ok = true;
  panel.trip();
  ASSERT_TRUE(panel.ok);
  ASSERT_TRUE(panel.adapter->frame_calls_stopped());
  const double waited = panel.probe_after(std::chrono::steady_clock::now());
  EXPECT_GE(waited, 130.0);
  EXPECT_LE(waited, 260.0) << "a pause of 150 ms, not 300 ms";
}

TEST_P(PanelOutage, APanelFoundAgainAfterARejoinIsShownWithItsStream)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  {
    QarCApiPanelSenderFactory factory(&h.owner());
    factory.set_frame_call_options(options());
    auto first = factory.create_panel_sender(stream_desc("dock"));
    ASSERT_NE(first, nullptr);
    std::vector<PanelOpResult> results;
    ASSERT_TRUE(drain_until(*first, results, [](const std::vector<PanelOpResult> & r) {
          return has_result(r, PanelOpResult::Op::Create);
        }));
    ASSERT_TRUE(first->set_state(seam::PanelState::Hidden));
    ASSERT_TRUE(wait_until([&h] {
          const auto panels = h.fake().panels();
          return !panels.empty() && panels[0].state == QAR_GUI_PANEL_STATE_HIDDEN;
        }));

    // The session is lost; its panel is released and the owner joins again by itself.
    h.owner().declare_lost("the test gave the session up");
    ASSERT_TRUE(h.wait_state(SessionState::kLost));
    first.reset();
    h.owner().release_lost_session();
  }
  ASSERT_TRUE(h.wait_for([&h] {
        return h.owner().state() == SessionState::kRunning && h.owner().ready();
      }, 60s)) << "the owner joined the Hub again";

  auto second = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, options());
  ASSERT_FALSE(second->refused());
  ASSERT_TRUE(made(*second));
  EXPECT_TRUE(second->reattached()) << "the panel the lost session left is found by its name";
  ASSERT_TRUE(wait_until([&h] {
        const auto panels = h.fake().panels();
        return panels.size() == 1u && panels[0].state == QAR_GUI_PANEL_STATE_VISIBLE;
      })) << "and shown again";
  const auto panels = h.fake().panels();
  ASSERT_EQ(panels.size(), 1u);
  EXPECT_TRUE(panels[0].has_stream);
  EXPECT_FALSE(second->stream_id_text().empty());
  EXPECT_EQ(h.fake().live_video_senders(), 1) << "one sender, the one of the new session";
  second->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST_P(PanelOutage, InputRetireAsLastOwnerCancelsOnTheLane)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  QarCApiPanelSenderFactory factory(&h.owner());
  factory.set_frame_call_options(options());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  ASSERT_TRUE(wait_until([&adapter] { return !adapter->pending(); }));
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  const QarGuiPanelId id = static_cast<QarCApiPanelSenderAdapter *>(adapter.get())->panel_id();

  auto lane = std::make_shared<RecordingLane>();
  auto input = std::make_shared<QarCApiPanelInputSource>(h.owner().session(), lane);
  const int tokens_before = h.fake().live_tokens();
  ASSERT_TRUE(input->subscribe_panel("dock", id, 64, 48));
  ASSERT_EQ(h.fake().live_tokens(), tokens_before + 2) << "a token for each subscription";
  const int cancels = h.fake().count("cancel_token_cancel");

  // The last owner lets go on the GUI thread: nothing is cancelled there.
  EXPECT_LT(milliseconds_of([&] { input.reset(); }), 20.0);
  EXPECT_EQ(h.fake().count("cancel_token_cancel"), cancels) << "no cancel on the caller";
  EXPECT_EQ(h.fake().live_tokens(), tokens_before + 2);
  ASSERT_EQ(lane->names().size(), 1u);
  EXPECT_EQ(lane->names()[0], "panel-input-release");

  // The lane runs it: both tokens are cancelled and destroyed there.
  lane->run_items();
  EXPECT_EQ(h.fake().count("cancel_token_cancel"), cancels + 2);
  EXPECT_EQ(h.fake().live_tokens(), tokens_before);
  EXPECT_EQ(h.fake().thread_of_last("cancel_token_cancel"), lane->thread_id());
  EXPECT_NE(lane->thread_id(), std::this_thread::get_id());

  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST_P(PanelOutage, InputWithoutALaneCancelsOnTheCaller)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  QarCApiPanelSenderFactory factory(&h.owner());
  factory.set_frame_call_options(options());
  auto adapter = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(adapter, nullptr);
  ASSERT_TRUE(wait_until([&adapter] { return !adapter->pending(); }));
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  const QarGuiPanelId id = static_cast<QarCApiPanelSenderAdapter *>(adapter.get())->panel_id();
  auto input = std::make_shared<QarCApiPanelInputSource>(h.owner().session());
  const int tokens_before = h.fake().live_tokens();
  ASSERT_TRUE(input->subscribe_panel("dock", id, 64, 48));
  input.reset();
  EXPECT_EQ(h.fake().live_tokens(), tokens_before) << "released where it was let go";
  EXPECT_EQ(h.fake().thread_of_last("cancel_token_cancel"), std::this_thread::get_id());
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST_P(PanelOutage, ARefusedLaneLeavesTheHandlesAloneAndSaysSo)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  QarCApiPanelSenderFactory factory(&h.owner());
  factory.set_frame_call_options(options());
  auto first = factory.create_panel_sender(stream_desc("dock"));
  ASSERT_NE(first, nullptr);
  ASSERT_TRUE(wait_until([&first] { return !first->pending(); }));
  std::vector<PanelOpResult> results;
  ASSERT_TRUE(drain_until(*first, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  const QarGuiPanelId id = static_cast<QarCApiPanelSenderAdapter *>(first.get())->panel_id();

  // Input tokens: the lane has no thread left.
  auto lane = std::make_shared<RecordingLane>();
  lane->set_refuse(true);
  auto input = std::make_shared<QarCApiPanelInputSource>(h.owner().session(), lane);
  ASSERT_TRUE(input->subscribe_panel("dock", id, 64, 48));
  const int tokens = h.fake().live_tokens();
  const int cancels = h.fake().count("cancel_token_cancel");
  testing::internal::CaptureStderr();
  input.reset();
  const std::string input_log = testing::internal::GetCapturedStderr();
  EXPECT_NE(input_log.find("panel input tokens left unreleased"), std::string::npos)
    << input_log;
  EXPECT_EQ(h.fake().live_tokens(), tokens) << "leaked, never destroyed";
  EXPECT_EQ(h.fake().count("cancel_token_cancel"), cancels) << "no call on them";

  // The video sender of a panel of a lost session: the same.
  auto refusing = std::make_shared<RecordingLane>();
  LostOwner owner(h.owner().session(), refusing);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &owner, stream_desc("side"), nullptr, options());
  ASSERT_FALSE(adapter->refused());
  ASSERT_TRUE(made(*adapter));
  ASSERT_EQ(h.fake().live_video_senders(), 2);
  owner.lose();
  refusing->set_refuse(true);
  const int workers = qar_qar_adapter::FrameWorker::running();
  testing::internal::CaptureStderr();
  adapter->close(false);
  if (on_worker()) {
    ASSERT_TRUE(workers_running_are(workers - 1)) << "the worker of the closed panel has left";
  }
  const std::string sender_log = testing::internal::GetCapturedStderr();
  EXPECT_NE(
    sender_log.find("panel 'side' video sender left unreleased"), std::string::npos)
    << sender_log;
  EXPECT_EQ(h.fake().live_video_senders(), 2) << "the sender was not destroyed";
  EXPECT_EQ(h.fake().count("video_sender_handle_destroy"), 0);

  first->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 1; }));
}

TEST_P(PanelOutage, ALaneThatAcceptsRunsTheSenderRelease)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  auto lane = std::make_shared<RecordingLane>();
  LostOwner owner(h.owner().session(), lane);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &owner, stream_desc("side"), nullptr, options());
  ASSERT_TRUE(made(*adapter));
  ASSERT_EQ(h.fake().live_video_senders(), 1);
  owner.lose();
  adapter->close(false);
  ASSERT_TRUE(wait_until([&lane] { return lane->names().size() == 2u; }))
    << "the worker posts the release of the video sender when it has left";
  EXPECT_EQ(lane->names()[0], "panel-watch-release:side");
  EXPECT_EQ(lane->names()[1], "panel-video-sender-release:side");
  EXPECT_EQ(h.fake().live_video_senders(), 1) << "nothing is destroyed on the GUI thread";
  lane->run_items();
  EXPECT_EQ(h.fake().live_video_senders(), 0);
  EXPECT_EQ(h.fake().thread_of_last("video_sender_handle_destroy"), lane->thread_id());
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0);
}

TEST_P(PanelOutage, TheStateWatchOfAClosedPanelIsCancelledOnTheLaneNotOnTheCaller)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  auto lane = std::make_shared<RecordingLane>();
  LostOwner owner(h.owner().session(), lane);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &owner, stream_desc("side"), nullptr, options());
  ASSERT_TRUE(made(*adapter));
  ASSERT_EQ(h.fake().panel_subscriptions(), 1) << "the state watch of the adapter";
  const int tokens = h.fake().live_tokens();
  const int cancels = h.fake().count("cancel_token_cancel");
  const int destroys = h.fake().count("cancel_token_handle_destroy");
  owner.lose();

  // The fake blocks every session call: the close on the GUI thread must still return at once.
  h.fake().set_hub_gone(qar_qar_adapter::test::HubGone::kBlockAll);
  EXPECT_LT(milliseconds_of([&] { adapter->close(false); }), 20.0);
  EXPECT_EQ(h.fake().count("cancel_token_cancel"), cancels) << "no cancel on the caller";
  EXPECT_EQ(h.fake().count("cancel_token_handle_destroy"), destroys);
  EXPECT_EQ(h.fake().live_tokens(), tokens);
  ASSERT_FALSE(lane->names().empty());
  EXPECT_EQ(lane->names()[0], "panel-watch-release:side");

  // The lane runs it; cancel tokens are not Hub calls. The Hub is released first because the
  // item of the video sender behind it destroys a sender, and that waits for a blocked Hub.
  h.fake().set_hub_gone(qar_qar_adapter::test::HubGone::kNo);
  ASSERT_TRUE(wait_until([&lane] { return lane->names().size() == 2u; }));
  lane->run_items();
  EXPECT_EQ(h.fake().panel_subscriptions(), 0);
  EXPECT_EQ(h.fake().thread_of_last("cancel_token_cancel"), lane->thread_id());
  EXPECT_EQ(h.fake().thread_of_last("cancel_token_handle_destroy"), lane->thread_id());
  EXPECT_LT(h.fake().live_tokens(), tokens);
}
