// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The panel sender with its frame calls on the frame worker: every QarOS frame call is made on
// the worker's thread, the GUI thread never waits for a call that blocks, a panel that is closed
// while a call blocks hands its video sender to the last one of the teardown and the worker, and
// the pixels reach the destination of the cycle that the worker reserved for them. A send cycle
// is reserved only for a frame that is staged and none outlives the job. The owner runs on the
// fake QarOS library, whose frame calls are slowed, blocked or failed from the test.

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "qar_ar_core/common_name.hpp"
#include "qar_capi_panels/qar_capi_panel_sender_adapter.hpp"
#include "qar_capi_panels/seam_c.hpp"
#include "qar_qar_adapter/frame_worker.hpp"

namespace seam = qar_qt_streaming::seam;

namespace
{

using seam::PanelOpResult;  // read by panel_test_kit.hpp

}  // namespace

#include "panel_test_kit.hpp"

namespace
{

using qar_capi_panels::FrameCallOptions;
using qar_capi_panels::QarCApiPanelSenderAdapter;
using qar_qar_adapter::FrameWorker;
using qar_qar_adapter::IQarSessionOwner;
using qar_qar_adapter::IReleaseLane;

// The other test files of this executable drive the frame calls from the test thread, which is
// what the frame calls on the GUI thread allow. Without a switch of the environment they get
// those, and the tests below ask for the worker with their own options. A run that sets
// QAR_TEST_FRAME_CALLS_AS_SET keeps the QAR_FRAME_CALLS of its environment for those files.
struct DefaultToGuiFrameCalls
{
  DefaultToGuiFrameCalls()
  {
    if (std::getenv("QAR_TEST_FRAME_CALLS_AS_SET") != nullptr) {
      return;
    }
#ifdef _WIN32
    _putenv_s("QAR_FRAME_CALLS", "gui");
#else
    setenv("QAR_FRAME_CALLS", "gui", 1);
#endif
  }
};
const DefaultToGuiFrameCalls g_default_to_gui_frame_calls;

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

FrameCallOptions worker_options()
{
  FrameCallOptions options;
  options.on_worker = true;
  options.worker_timings.wait_notice = 400ms;
  return options;
}

constexpr const char * kBegin = "video_sender_try_begin_frame";
constexpr const char * kCpu = "video_sender_frame_cpu";
constexpr const char * kShow = "video_sender_show_frame";
constexpr const char * kDestroy = "video_sender_handle_destroy";

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

bool made(QarCApiPanelSenderAdapter & adapter)
{
  std::vector<PanelOpResult> results;
  return drain_until(adapter, results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      });
}

// Asks for a cycle the way the ticks of the consumer do, until one is reserved.
bool begin_until_ready(QarCApiPanelSenderAdapter & adapter, std::chrono::milliseconds limit = 3s)
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

// Sits between the sender and the fake QarOS library for the three frame calls of the video
// sender: fails the next calls with a status, and hands every frame access a destination buffer
// of its own, which stays readable after its cycle, so that a test can tell which cycle a frame
// was written into. The calls still reach the fake (it counts them and blocks them). One per
// test, made after the harness and gone before it.
class VideoApiTap
{
public:
  VideoApiTap()
  {
    real_begin_ = g_qar_video_sender_api.video_sender_try_begin_frame;
    real_cpu_ = g_qar_video_sender_api.video_sender_frame_cpu;
    real_show_ = g_qar_video_sender_api.video_sender_show_frame;
    g_qar_video_sender_api.video_sender_try_begin_frame = &VideoApiTap::begin;
    g_qar_video_sender_api.video_sender_frame_cpu = &VideoApiTap::cpu;
    g_qar_video_sender_api.video_sender_show_frame = &VideoApiTap::show;
    begin_code_ = 0;
    show_code_ = 0;
    failed_shows_ = 0;
    destinations_.clear();
  }
  ~VideoApiTap()
  {
    g_qar_video_sender_api.video_sender_try_begin_frame = real_begin_;
    g_qar_video_sender_api.video_sender_frame_cpu = real_cpu_;
    g_qar_video_sender_api.video_sender_show_frame = real_show_;
  }
  VideoApiTap(const VideoApiTap &) = delete;
  VideoApiTap & operator=(const VideoApiTap &) = delete;

  /// Every later qar_video_sender_try_begin_frame fails with @p code (0: it works again).
  void fail_begin(int code) { begin_code_ = code; }
  /// Every later qar_video_sender_show_frame fails with @p code (0: it works again).
  void fail_show(int code) { show_code_ = code; }
  int failed_shows() const { return failed_shows_.load(); }
  int destination_count() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(destinations_.size());
  }
  std::vector<std::uint8_t> destination(int index) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return destinations_.at(static_cast<std::size_t>(index));
  }

private:
  static QarResult failure(int code)
  {
    QarResult result{};
    result.code = static_cast<QarStatusCode>(code);
    result.error_handle = 1;
    return result;
  }
  static QarResult begin(QarVideoSender * sender, bool * out_ready)
  {
    const QarResult real = real_begin_(sender, out_ready);
    const int code = begin_code_.load();
    return code != 0 ? failure(code) : real;
  }
  static QarResult cpu(QarVideoSender * sender, QarVideoFrameCpu * out)
  {
    const QarResult real = real_cpu_(sender, out);
    if (!qar_result_is_error(real) && out->textures_count > 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      destinations_.emplace_back(out->textures[0].texture_data_size, std::uint8_t(0));
      out->textures[0].texture_data = destinations_.back().data();
    }
    return real;
  }
  static QarResult show(QarVideoSender * sender)
  {
    const int code = show_code_.load();
    if (code != 0) {
      failed_shows_.fetch_add(1);
      return failure(code);
    }
    return real_show_(sender);
  }

  static inline decltype(g_qar_video_sender_api.video_sender_try_begin_frame) real_begin_ = nullptr;
  static inline decltype(g_qar_video_sender_api.video_sender_frame_cpu) real_cpu_ = nullptr;
  static inline decltype(g_qar_video_sender_api.video_sender_show_frame) real_show_ = nullptr;
  static inline std::atomic<int> begin_code_{0};
  static inline std::atomic<int> show_code_{0};
  static inline std::atomic<int> failed_shows_{0};
  static inline std::mutex mutex_;
  static inline std::deque<std::vector<std::uint8_t>> destinations_;
};

// One tick of the consumer that has a frame to send: asks for a cycle and, when it is offered,
// submits. True when the frame was handed over.
bool offer_frame(QarCApiPanelSenderAdapter & adapter, const seam::PanelPixels & pixels)
{
  bool ready = false;
  if (!adapter.try_begin_frame(ready) || !ready) {
    return false;
  }
  return adapter.submit_frame(pixels);
}

// The ticks of a consumer whose content does not change: the adapter's drain only.
void idle_ticks(QarCApiPanelSenderAdapter & adapter, int n)
{
  for (int i = 0; i < n; ++i) {
    std::vector<PanelOpResult> out;
    adapter.drain_results(out);
    std::this_thread::sleep_for(10ms);
  }
}

// Asks the way the ticks of a consumer with a pending frame do, until @p done holds.
bool tick_until(
  QarCApiPanelSenderAdapter & adapter, const std::function<bool()> & done,
  std::chrono::milliseconds limit = 3s)
{
  const auto end = Clock::now() + limit;
  while (Clock::now() < end) {
    bool ready = false;
    adapter.try_begin_frame(ready);
    std::vector<PanelOpResult> out;
    adapter.drain_results(out);
    if (done()) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return done();
}

bool all_bytes(const std::vector<std::uint8_t> & bytes, std::uint8_t value)
{
  return !bytes.empty() && std::all_of(bytes.begin(), bytes.end(), [value](std::uint8_t b) {
        return b == value;
      });
}

// Runs @p fn on a thread of its own and tells whether it returned within @p limit. When it did
// not, @p unblock runs, so that a test whose subject waits fails instead of hanging.
bool returns_within_or_unblock(
  const std::function<void()> & fn, std::chrono::milliseconds limit,
  const std::function<void()> & unblock)
{
  auto done = std::make_shared<std::promise<void>>();
  std::future<void> result = done->get_future();
  std::thread runner([fn, done] {
      fn();
      done->set_value();
    });
  const bool in_time = result.wait_for(limit) == std::future_status::ready;
  if (!in_time) {
    unblock();
  }
  runner.join();
  return in_time;
}

// An owner of the session of the harness whose lane the test supplies. Tasks it accepts run at
// once on the calling thread.
class FixedOwner : public IQarSessionOwner
{
public:
  FixedOwner(QarSession * session, std::shared_ptr<IReleaseLane> lane)
  : session_(session), lane_(std::move(lane)) {}
  bool ready() const override {return true;}
  QarSession * session() const override {return session_;}
  std::shared_ptr<IReleaseLane> release_lane() const override {return lane_;}

private:
  QarSession * session_;
  std::shared_ptr<IReleaseLane> lane_;
};

// A lane that runs nothing and takes no frame worker.
class NoWorkerLane : public IReleaseLane
{
public:
  bool post_release(const char *, std::function<void()> release) override
  {
    release();
    return true;
  }
  bool enter_worker(const std::shared_ptr<WorkerSlot> &) override {return false;}
};

}  // namespace

TEST(PanelWorker, EveryFrameCallRunsOnTheWorkersThread)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  testing::internal::CaptureStderr();
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_FALSE(adapter->refused());
  ASSERT_TRUE(adapter->frame_calls_on_worker());
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  ASSERT_TRUE(begin_until_ready(*adapter));
  ASSERT_TRUE(adapter->submit_frame(frame.pixels)) << adapter->last_error();
  ASSERT_TRUE(wait_until([&h] { return h.fake().video_frames_shown() == 1; }));
  const std::string log = testing::internal::GetCapturedStderr();

  const std::thread::id caller = std::this_thread::get_id();
  const std::thread::id begin = h.fake().thread_of_last(kBegin);
  EXPECT_NE(begin, std::thread::id());
  EXPECT_NE(begin, caller);
  EXPECT_NE(begin, h.owner_thread());
  EXPECT_EQ(h.fake().thread_of_last(kCpu), begin);
  EXPECT_EQ(h.fake().thread_of_last(kShow), begin);
  EXPECT_NE(log.find("[qar-panel-adapter] "), std::string::npos);
  EXPECT_NE(log.find(" panel 'dock' frame worker started tid="), std::string::npos) << log;
  EXPECT_NE(log.find(" panel 'dock' first show_frame tid="), std::string::npos) << log;

  // The thread number of both lines is the worker's, so that the scorecard can tell the thread of
  // the frame calls from the thread that created the panel.
  const auto number_after = [&log](const std::string & text) {
      const std::size_t at = log.find(text);
      return at == std::string::npos ?
             0ull : std::strtoull(log.c_str() + at + text.size(), nullptr, 10);
    };
  const unsigned long long started = number_after("frame worker started tid=");
  const unsigned long long shown = number_after("first show_frame tid=");
  EXPECT_NE(started, 0ull);
  EXPECT_EQ(started, shown);

  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST(PanelWorker, TheEnvironmentSetsTheDefaultAndTheOptionsWinOverIt)
{
  // The default of this executable is the GUI thread; the options win over it.
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  EXPECT_FALSE(factory.frame_call_options().on_worker) << "QAR_FRAME_CALLS=gui";
  testing::internal::CaptureStderr();
  auto gui = factory.create_panel_sender(stream_desc("a"));
  factory.set_frame_call_options(worker_options());
  auto worker = factory.create_panel_sender(stream_desc("b"));
  const std::string log = testing::internal::GetCapturedStderr();
  ASSERT_NE(gui, nullptr);
  ASSERT_NE(worker, nullptr);
  EXPECT_FALSE(static_cast<QarCApiPanelSenderAdapter *>(gui.get())->frame_calls_on_worker());
  EXPECT_TRUE(static_cast<QarCApiPanelSenderAdapter *>(worker.get())->frame_calls_on_worker());
  EXPECT_NE(log.find("frame calls run on the GUI thread (QAR_FRAME_CALLS=gui)"), std::string::npos)
    << log << "\nthe line is printed once per factory, for the first panel";
  gui.reset();
  worker.reset();
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST(PanelWorker, ThePixelsReachTheDestinationOfTheCycleThatTheWorkerReservesForThem)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  VideoApiTap tap;
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  // The rows of the captured frame are 16 bytes longer than the pixels, as a Qt image's can be;
  // the padding holds a value that must not reach the destination.
  const std::size_t stride = 64 * 4 + 16;
  std::vector<std::uint8_t> padded(stride * 48, 0xEE);
  for (std::size_t y = 0; y < 48; ++y) {
    const auto row = padded.begin() + static_cast<std::ptrdiff_t>(y * stride);
    std::fill(row, row + 64 * 4, std::uint8_t(0x5A));
  }
  seam::PanelPixels pixels;
  pixels.data = padded.data();
  pixels.width = 64;
  pixels.height = 48;
  pixels.stride_bytes = static_cast<std::uint32_t>(stride);

  // Asking for a cycle makes no QarOS call: nothing is reserved before there is a frame to send.
  ASSERT_TRUE(begin_until_ready(*adapter));
  for (int i = 0; i < 20; ++i) {
    bool ready = false;
    ASSERT_TRUE(adapter->try_begin_frame(ready));
    EXPECT_TRUE(ready);
  }
  EXPECT_EQ(h.fake().count(kBegin), 0) << "no cycle is reserved by the question";
  EXPECT_EQ(h.fake().count(kCpu), 0);
  ASSERT_TRUE(adapter->submit_frame(pixels)) << adapter->last_error();
  ASSERT_TRUE(wait_until([&h] { return h.fake().video_frames_shown() == 1; }));
  EXPECT_EQ(h.fake().count(kBegin), 1) << "the cycle is reserved by the job that sends the frame";
  EXPECT_EQ(h.fake().count(kCpu), 1) << "the destination is asked for once per cycle";
  EXPECT_EQ(h.fake().count(kShow), 1);
  ASSERT_EQ(tap.destination_count(), 1);
  EXPECT_TRUE(all_bytes(tap.destination(0), 0x5A))
    << "every row of the destination holds the pixels and none holds the padding";
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST(PanelWorker, TheGuiThreadNeverWaitsForABlockedFrameCall)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  ASSERT_TRUE(begin_until_ready(*adapter));
  EXPECT_FALSE(adapter->frame_calls_stopped());

  // The first frame call of the job that sends this frame blocks.
  h.fake().set_frame_block(true);
  testing::internal::CaptureStderr();
  std::vector<double> took_ms;
  bool submitted = false;
  const bool in_time = returns_within_or_unblock([&] {
        const auto first = Clock::now();
        submitted = adapter->submit_frame(frame.pixels);
        took_ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - first).count());
        std::vector<PanelOpResult> results;
        for (int i = 0; i < 200; ++i) {
          const auto start = Clock::now();
          bool ready = false;
          adapter->try_begin_frame(ready);
          adapter->drain_results(results);
          took_ms.push_back(
            std::chrono::duration<double, std::milli>(Clock::now() - start).count());
          if (i == 10) {
            std::this_thread::sleep_for(1ms);
          }
        }
      }, 3s, [&h] { h.fake().set_frame_block(false); });
  ASSERT_TRUE(in_time) << "a call of the GUI thread waited for the blocked frame call";
  EXPECT_TRUE(submitted);
  ASSERT_EQ(took_ms.size(), 201u);
  std::vector<double> sorted = took_ms;
  std::sort(sorted.begin(), sorted.end());
  EXPECT_LT(sorted[190], 5.0) << "the 95th percentile of the 201 calls";
  EXPECT_LT(sorted.back(), 100.0) << "the slowest call";
  ASSERT_TRUE(wait_until([&h] { return h.fake().frame_calls_blocked() == 1; }));

  // After the notice time the wait is named and the panel counts as stopped.
  EXPECT_FALSE(wait_until([&adapter] { return adapter->frame_calls_stopped(); }, 100ms))
    << "not before the notice time";
  ASSERT_TRUE(wait_until([&adapter] { return adapter->frame_calls_stopped(); }, 3s));
  const int shown_before = h.fake().video_frames_shown();
  h.fake().set_frame_block(false);
  ASSERT_TRUE(wait_until([&] { return h.fake().video_frames_shown() > shown_before; }))
    << "the frame is shown when the call returns";
  ASSERT_TRUE(wait_until([&adapter] { return !adapter->frame_calls_stopped(); }));
  ASSERT_TRUE(begin_until_ready(*adapter)) << "and the panel goes on";
  const std::string log = testing::internal::GetCapturedStderr();
  EXPECT_NE(
    log.find("panel 'dock' frame worker waiting in qar_video_sender_try_begin_frame for "),
    std::string::npos) << log;
  EXPECT_NE(log.find("panel 'dock' frame worker free again after "), std::string::npos) << log;

  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST(PanelWorker, ClosingWhileAFrameCallBlocksReturnsAtOnceAndDestroysTheSenderOnceAfterwards)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  const int baseline = FrameWorker::running();
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  ASSERT_TRUE(begin_until_ready(*adapter));
  h.fake().set_frame_block(true);
  ASSERT_TRUE(adapter->submit_frame(frame.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().frame_calls_blocked() == 1; }));
  std::this_thread::sleep_for(500ms);

  seam::PanelCloseReport report;
  testing::internal::CaptureStderr();
  EXPECT_LT(milliseconds_of([&] { report = adapter->close(false); }), 20.0);
  EXPECT_EQ(report.panel, seam::PanelCloseOutcome::Queued);
  EXPECT_TRUE(owner_caught_up(h)) << "the teardown task ran on the owner thread";
  EXPECT_EQ(h.fake().count(kDestroy), 0) << "a sender with a worker inside a call is not destroyed";
  EXPECT_EQ(h.fake().live_video_senders(), 1);
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 1) << "the panel itself is closed";

  h.fake().set_frame_block(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }))
    << "the worker's release destroys the sender when the call has returned";
  const std::string log = testing::internal::GetCapturedStderr();
  EXPECT_EQ(h.fake().count(kDestroy), 1) << "exactly once";
  EXPECT_EQ(h.fake().thread_of_last(kDestroy), h.owner_thread()) << "on the owner's thread";
  EXPECT_NE(
    log.find("step 3/3: the video sender is destroyed when the frame worker has left"),
    std::string::npos) << log;
  EXPECT_NE(log.find("video sender released by the frame worker"), std::string::npos) << log;
  EXPECT_NE(
    log.find(
      "panel 'dock' frame worker left running inside qar_video_sender_try_begin_frame after "),
    std::string::npos) << log;
  ASSERT_TRUE(wait_until([baseline] { return FrameWorker::running() <= baseline; }));
  FrameWorker::reap();
}

TEST(PanelWorker, ADestroyedPanelWhileACallBlocksLeavesItsSenderAloneUntilTheCallReturns)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  ASSERT_TRUE(begin_until_ready(*adapter));
  h.fake().set_frame_block(true);
  ASSERT_TRUE(adapter->submit_frame(frame.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().frame_calls_blocked() == 1; }));
  std::this_thread::sleep_for(300ms);
  EXPECT_LT(milliseconds_of([&] { adapter.reset(); }), 20.0);
  EXPECT_TRUE(owner_caught_up(h));
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(h.fake().count(kDestroy), 0) << "nothing destroys a sender a call is inside";
  EXPECT_EQ(h.fake().live_video_senders(), 1);
  h.fake().set_frame_block(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
  EXPECT_EQ(h.fake().count(kDestroy), 1);
}

// Of the teardown task and the worker's exit the last one destroys the video sender, in either
// order, exactly once, and always on the owner's thread. Order 1: the worker is inside a call when
// the panel is closed, so the teardown comes first. Order 2: the owner is busy when the panel is
// closed, so the worker has left before the teardown runs. The delays between the steps are
// random.
TEST(PanelWorker, TheLastOfTheTeardownAndTheWorkerDestroysTheVideoSenderExactlyOnce)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  std::mt19937 random(20261105);
  std::uniform_int_distribution<int> micros(0, 3000);
  const auto pause = [&] {
      std::this_thread::sleep_for(std::chrono::microseconds(micros(random)));
    };
  TestFrame frame;
  int destroyed = 0;
  for (int round = 0; round < 100; ++round) {
    for (int order = 1; order <= 2; ++order) {
      const std::string key = "r" + std::to_string(round) + "o" + std::to_string(order);
      auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
        &h.owner(), stream_desc(key), nullptr, worker_options());
      ASSERT_TRUE(made(*adapter)) << key;
      ASSERT_TRUE(begin_until_ready(*adapter)) << key;
      const int workers = FrameWorker::running();
      if (order == 1) {
        h.fake().set_frame_block(true);
        ASSERT_TRUE(adapter->submit_frame(frame.pixels)) << key;
        ASSERT_TRUE(wait_until([&h] { return h.fake().frame_calls_blocked() == 1; })) << key;
        adapter->close(false);
        ASSERT_TRUE(owner_caught_up(h)) << key;
        pause();
        ASSERT_EQ(h.fake().count(kDestroy), destroyed) << key << ": destroyed while a call blocked";
        h.fake().set_frame_block(false);
      } else {
        OwnerHold hold;
        ASSERT_TRUE(hold_owner(h, hold)) << key;
        adapter->close(false);
        ASSERT_TRUE(wait_until([workers] { return FrameWorker::running() < workers; }))
          << key << ": the worker leaves while the owner is busy";
        pause();
        ASSERT_EQ(h.fake().count(kDestroy), destroyed) << key << ": destroyed by the worker";
        hold.release.store(true);
        ASSERT_TRUE(owner_caught_up(h)) << key;
      }
      ASSERT_TRUE(wait_until([&h, destroyed] { return h.fake().count(kDestroy) > destroyed; }))
        << key;
      ++destroyed;
      ASSERT_TRUE(owner_caught_up(h)) << key;
      ASSERT_EQ(h.fake().count(kDestroy), destroyed) << key << ": destroyed twice";
      ASSERT_EQ(h.fake().live_video_senders(), 0) << key;
      ASSERT_EQ(h.fake().thread_of_last(kDestroy), h.owner_thread()) << key;
      adapter.reset();
      pause();
    }
  }
  EXPECT_EQ(destroyed, 200);
  FrameWorker::reap();
}

TEST(PanelWorker, ALaneThatRefusesTheWorkerKeepsTheFrameCallsOnTheGuiThread)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  FixedOwner owner(h.owner().session(), std::make_shared<NoWorkerLane>());
  testing::internal::CaptureStderr();
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &owner, stream_desc("dock"), nullptr, worker_options());
  const std::string log = testing::internal::GetCapturedStderr();
  ASSERT_FALSE(adapter->refused());
  EXPECT_FALSE(adapter->frame_calls_on_worker());
  EXPECT_NE(
    log.find("panel 'dock' frame calls run on the GUI thread (no frame worker could start)"),
    std::string::npos) << log;
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  bool ready = false;
  ASSERT_TRUE(adapter->try_begin_frame(ready));
  ASSERT_TRUE(ready) << "the call is answered at once, on this thread";
  ASSERT_TRUE(adapter->submit_frame(frame.pixels));
  EXPECT_EQ(h.fake().video_frames_shown(), 1);
  EXPECT_EQ(h.fake().thread_of_last(kShow), std::this_thread::get_id());
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST(PanelWorker, KicksAreCoalescedAndTheWorkerDoesNotSpin)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(false);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  // One frame is staged, and the sender has no destination for it.
  ASSERT_TRUE(offer_frame(*adapter, frame.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().count(kBegin) >= 1; }));
  std::this_thread::sleep_for(50ms);
  const int before = h.fake().count(kBegin);
  const auto start = Clock::now();
  for (int i = 0; i < 20000; ++i) {
    bool ready = false;
    adapter->try_begin_frame(ready);
  }
  const double elapsed_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  std::this_thread::sleep_for(100ms);
  const int polls = h.fake().count(kBegin) - before;
  EXPECT_GE(polls, 1) << "the asks reach the worker";
  EXPECT_LE(polls, static_cast<int>(elapsed_ms / 0.5) + 5)
    << "no more than one poll per half millisecond of asking (" << elapsed_ms << " ms)";
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(h.fake().count(kBegin), before + polls) << "an idle worker polls nothing";
  EXPECT_EQ(h.fake().count(kCpu), 0) << "no cycle was reserved";
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

// A panel with nothing to send reserves no cycle (the rule of the consumer: a reserved cycle must
// be followed by a show_frame). The ticks of a panel whose content does not change only drain.
TEST(PanelWorker, AnIdlePanelReservesNoCycleBeforeItHasSomethingToSend)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(adapter->frame_calls_on_worker());
  ASSERT_TRUE(made(*adapter));
  idle_ticks(*adapter, 30);
  EXPECT_EQ(h.fake().count(kBegin), 0) << "the worker reserved a cycle nobody asked for";
  EXPECT_EQ(h.fake().count(kCpu), 0);
  EXPECT_EQ(h.fake().video_frames_shown(), 0);
  EXPECT_EQ(adapter->frame_worker_jobs(), 0ull);
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

TEST(PanelWorker, AShownFrameLeavesNoReservedCycleBehindWhileTheContentIsUnchanged)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  ASSERT_TRUE(begin_until_ready(*adapter));
  ASSERT_TRUE(adapter->submit_frame(frame.pixels)) << adapter->last_error();
  ASSERT_TRUE(wait_until([&h] { return h.fake().video_frames_shown() == 1; }));
  idle_ticks(*adapter, 30);
  EXPECT_EQ(h.fake().count(kBegin), 1) << "a cycle was reserved after the show";
  EXPECT_EQ(h.fake().count(kCpu), 1) << "a cycle was reserved after the show and stays stranded";
  EXPECT_EQ(h.fake().count(kShow), 1);
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

// A frame that finds no destination (nothing attached, a stream that reconnects, a stream the
// receiver paused) reserves no cycle, stays staged, is reported with its cause, and is shown from
// the staging buffer as soon as a destination is offered.
TEST(PanelWorker, AStagedFrameWaitsForADestinationAndIsShownWhenOneAppears)
{
  struct Case
  {
    int code;
    seam::PanelNotReadyCause cause;
    const char * name;
  };
  const Case cases[] = {
    {QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS, seam::PanelNotReadyCause::NoActiveTransfers,
      "604"},
    {QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING, seam::PanelNotReadyCause::Reconnecting,
      "603"},
    {QAR_STATUS_MEDIA_STREAMING_STREAM_IS_PAUSED, seam::PanelNotReadyCause::Paused, "602"},
  };
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  VideoApiTap tap;
  for (const Case & c : cases) {
    SCOPED_TRACE(c.name);
    h.fake().set_video_sender_ready(true);
    tap.fail_begin(c.code);
    const int shown_before = h.fake().video_frames_shown();
    const int cpu_before = h.fake().count(kCpu);
    const int destinations_before = tap.destination_count();
    auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
      &h.owner(), stream_desc(std::string("dock") + c.name), nullptr, worker_options());
    ASSERT_TRUE(made(*adapter));
    TestFrame frame;
    std::fill(frame.buffer.begin(), frame.buffer.end(), std::uint8_t(0x33));
    ASSERT_TRUE(begin_until_ready(*adapter));
    ASSERT_TRUE(adapter->submit_frame(frame.pixels)) << adapter->last_error();
    // The frame is staged; the question is answered with the cause the worker found.
    ASSERT_TRUE(tick_until(*adapter, [&] {
          bool ready = true;
          adapter->try_begin_frame(ready);
          return !ready && adapter->not_ready_cause() == c.cause;
        })) << "the cause of the staged frame is reported";
    EXPECT_EQ(h.fake().video_frames_shown(), shown_before);
    EXPECT_EQ(h.fake().count(kCpu), cpu_before) << "no cycle is reserved without a destination";
    // A destination appears: the staged frame is shown without a second submit.
    tap.fail_begin(0);
    ASSERT_TRUE(tick_until(*adapter, [&] {
          return h.fake().video_frames_shown() == shown_before + 1;
        })) << "the staged frame was not shown";
    EXPECT_EQ(h.fake().count(kCpu), cpu_before + 1);
    ASSERT_EQ(tap.destination_count(), destinations_before + 1);
    EXPECT_TRUE(all_bytes(tap.destination(destinations_before), 0x33));
    bool ready = false;
    ASSERT_TRUE(adapter->try_begin_frame(ready));
    EXPECT_TRUE(ready) << "the staging slot is free again";
    adapter->close(false);
    ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
  }
}

// A frame staged while the worker is inside its call replaces the one the worker holds when that
// one finds no destination: the newest frame is the one that is shown.
TEST(PanelWorker, ANewerFrameReplacesTheOneThatFoundNoDestination)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  VideoApiTap tap;
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame first;
  TestFrame second;
  std::fill(first.buffer.begin(), first.buffer.end(), std::uint8_t(0x11));
  std::fill(second.buffer.begin(), second.buffer.end(), std::uint8_t(0x22));
  h.fake().set_frame_block(true);
  ASSERT_TRUE(offer_frame(*adapter, first.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().frame_calls_blocked() == 1; }));
  ASSERT_TRUE(offer_frame(*adapter, second.pixels))
    << "a staging slot is free while the worker holds one";
  bool ready = true;
  ASSERT_TRUE(adapter->try_begin_frame(ready));
  EXPECT_FALSE(ready) << "and only one";
  tap.fail_begin(QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS);
  h.fake().set_frame_block(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().count(kBegin) >= 1; }));
  std::this_thread::sleep_for(50ms);
  tap.fail_begin(0);
  ASSERT_TRUE(tick_until(*adapter, [&] { return h.fake().video_frames_shown() >= 1; }));
  std::this_thread::sleep_for(100ms);
  EXPECT_EQ(h.fake().video_frames_shown(), 1) << "the older frame is not shown after the newer";
  ASSERT_EQ(tap.destination_count(), 1);
  EXPECT_TRUE(all_bytes(tap.destination(0), 0x22)) << "the newest frame is shown";
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

// Each frame is written into the destination of the cycle the worker reserved for it: when the
// receiver leaves and comes back, the destination of the earlier cycle is not written again.
TEST(PanelWorker, AReceiverThatLeavesAndComesBackNeverGetsAWriteIntoAnOldDestination)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  VideoApiTap tap;
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame a;
  TestFrame b;
  std::fill(a.buffer.begin(), a.buffer.end(), std::uint8_t(0xA1));
  std::fill(b.buffer.begin(), b.buffer.end(), std::uint8_t(0xB2));
  ASSERT_TRUE(offer_frame(*adapter, a.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().video_frames_shown() == 1; }));
  ASSERT_EQ(tap.destination_count(), 1);
  ASSERT_TRUE(all_bytes(tap.destination(0), 0xA1));

  // The receiver leaves; the next frame finds no destination and waits.
  tap.fail_begin(QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING);
  ASSERT_TRUE(offer_frame(*adapter, b.pixels));
  ASSERT_TRUE(tick_until(*adapter, [&] {
        bool ready = true;
        adapter->try_begin_frame(ready);
        return !ready && adapter->not_ready_cause() == seam::PanelNotReadyCause::Reconnecting;
      }));
  idle_ticks(*adapter, 10);
  EXPECT_EQ(tap.destination_count(), 1) << "no new destination without a receiver";
  EXPECT_TRUE(all_bytes(tap.destination(0), 0xA1))
    << "the destination of the earlier cycle was written again";
  EXPECT_EQ(h.fake().video_frames_shown(), 1);

  // The receiver comes back: the staged frame goes to the destination of the new cycle.
  tap.fail_begin(0);
  ASSERT_TRUE(tick_until(*adapter, [&] { return h.fake().video_frames_shown() == 2; }));
  ASSERT_EQ(tap.destination_count(), 2);
  EXPECT_TRUE(all_bytes(tap.destination(1), 0xB2));
  EXPECT_TRUE(all_bytes(tap.destination(0), 0xA1));
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

// A show that fails is reported through the next question as an error, as before, and the panel
// goes on with the next frame.
TEST(PanelWorker, AFailedShowIsReportedAsAnErrorAndThePanelGoesOn)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  VideoApiTap tap;
  h.fake().set_video_sender_ready(true);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  TestFrame frame;
  tap.fail_show(QAR_STATUS_LOGIC_ERROR);
  ASSERT_TRUE(offer_frame(*adapter, frame.pixels));
  ASSERT_TRUE(wait_until([&tap] { return tap.failed_shows() == 1; }));
  bool ready = true;
  bool answered = true;
  ASSERT_TRUE(wait_until([&] {
        answered = adapter->try_begin_frame(ready);
        return !answered;
      }));
  EXPECT_NE(adapter->last_error().find("qar_video_sender_show_frame"), std::string::npos)
    << adapter->last_error();
  EXPECT_EQ(h.fake().video_frames_shown(), 0);
  tap.fail_show(0);
  ASSERT_TRUE(offer_frame(*adapter, frame.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().video_frames_shown() == 1; }));
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}

// CPU: a panel with no receiver starts no job of its worker while nothing is staged, and one
// that holds a frame asks for a destination at a bounded rate.
TEST(PanelWorker, APanelWithNoReceiverMakesNoWorkerPollsWhileNothingIsStaged)
{
  Harness h;
  h.start();
  ASSERT_TRUE(h.connect_and_run());
  VideoApiTap tap;
  h.fake().set_video_sender_ready(true);
  tap.fail_begin(QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS);
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    &h.owner(), stream_desc("dock"), nullptr, worker_options());
  ASSERT_TRUE(made(*adapter));
  std::this_thread::sleep_for(50ms);
  const unsigned long long jobs_before = adapter->frame_worker_jobs();
  idle_ticks(*adapter, 30);  // 300 ms
  EXPECT_EQ(adapter->frame_worker_jobs(), jobs_before) << "an idle panel started worker jobs";
  EXPECT_EQ(h.fake().count(kBegin), 0);

  // One frame is staged and finds no receiver; the ticks of the consumer retry it at a bounded
  // rate.
  TestFrame frame;
  ASSERT_TRUE(offer_frame(*adapter, frame.pixels));
  ASSERT_TRUE(wait_until([&h] { return h.fake().count(kBegin) >= 1; }));
  const int begins = h.fake().count(kBegin);
  idle_ticks(*adapter, 30);  // 300 ms and a bit
  const int retries = h.fake().count(kBegin) - begins;
  EXPECT_GE(retries, 1) << "the staged frame is tried again";
  EXPECT_LE(retries, 20) << "no more than about one try per 20 ms";
  EXPECT_EQ(h.fake().count(kCpu), 0);
  adapter->close(false);
  ASSERT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 0; }));
}
