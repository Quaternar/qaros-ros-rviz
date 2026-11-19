// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The Qt-thread stall watchdog: the core runs against a fake clock, a fake sink and a fake dump
// writer, so no test waits for real time except the thread tests, which use limits of tens of
// milliseconds. The Windows pieces are exercised for real on files of the test (the sink on a
// temporary file, the dump writer on this test process).

#include <gtest/gtest.h>

#include "qar_rviz_streaming/qt_stall_watchdog.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#endif

// --- Allocation counting ----------------------------------------------------------------------
//
// Every allocation made by a thread that has switched counting on is counted. The replacement
// forwards to malloc, so the rest of the test executable is not affected.
namespace
{
thread_local bool g_counting = false;
thread_local int g_allocations = 0;
}  // namespace

void * operator new(std::size_t size)
{
  if (g_counting) {
    ++g_allocations;
  }
  void * const p = std::malloc(size != 0 ? size : 1);
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  return p;
}

void * operator new[](std::size_t size)
{
  if (g_counting) {
    ++g_allocations;
  }
  void * const p = std::malloc(size != 0 ? size : 1);
  if (p == nullptr) {
    throw std::bad_alloc();
  }
  return p;
}

void operator delete(void * p) noexcept {std::free(p);}
void operator delete[](void * p) noexcept {std::free(p);}
void operator delete(void * p, std::size_t) noexcept {std::free(p);}
void operator delete[](void * p, std::size_t) noexcept {std::free(p);}

namespace
{

using qar_rviz_streaming::QtStallWatchdog;
using qar_rviz_streaming::StallClock;
using qar_rviz_streaming::StallDumpWriter;
using qar_rviz_streaming::StallReport;
using qar_rviz_streaming::StallSink;
using qar_rviz_streaming::StallWatchdogConfig;

constexpr std::int64_t kSecond = 1'000'000'000;
constexpr std::int64_t kMs = 1'000'000;

/// A clock the test moves by hand. Local time of day = 10:00:00.000 at steady 0.
class FakeClock : public StallClock
{
public:
  std::int64_t steady_ns() const override {return now_ns.load();}
  std::int64_t local_ms_of_day(std::int64_t at_ns) const override
  {
    return 10LL * 3600 * 1000 + at_ns / kMs;
  }
  void advance(std::int64_t ns) {now_ns += ns;}
  std::atomic<std::int64_t> now_ns{1 * kSecond};
};

/// Keeps the lines in a fixed buffer: writing a line allocates nothing.
class FixedSink : public StallSink
{
public:
  void write(const char * text, std::size_t length) override
  {
    if (used_ + length <= sizeof(buffer_)) {
      std::memcpy(buffer_ + used_, text, length);
      used_ += length;
    }
    ++writes_;
  }
  std::string text() const {return std::string(buffer_, used_);}
  int writes() const {return writes_;}

private:
  char buffer_[4096];
  std::size_t used_ = 0;
  int writes_ = 0;
};

/// The same, safe to read while a watchdog thread writes.
class LockedSink : public StallSink
{
public:
  void write(const char * text, std::size_t length) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    text_.append(text, length);
  }
  std::string text() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return text_;
  }

private:
  mutable std::mutex mutex_;
  std::string text_;
};

class FakeDump : public StallDumpWriter
{
public:
  bool write_dump(char * detail, std::size_t capacity) override
  {
    ++calls;
    const char * const text = succeed ? "rviz_stall_test.dmp" : "the volume is nearly full";
    std::size_t n = 0;
    while (text[n] != '\0' && n + 1 < capacity) {
      detail[n] = text[n];
      ++n;
    }
    detail[n] = '\0';
    return succeed;
  }
  int calls = 0;
  bool succeed = true;
};

std::size_t count_of(const std::string & text, const std::string & needle)
{
  std::size_t count = 0;
  for (std::size_t at = text.find(needle); at != std::string::npos;
    at = text.find(needle, at + 1))
  {
    ++count;
  }
  return count;
}

/// Polls every 250 ms of fake time for @p seconds.
void poll_for(QtStallWatchdog & dog, FakeClock & clock, double seconds)
{
  const int steps = static_cast<int>(seconds * 4.0);
  for (int i = 0; i < steps; ++i) {
    clock.advance(250 * kMs);
    dog.poll();
  }
}

}  // namespace

TEST(QtStallWatchdog, ClockText)
{
  char text[16];
  qar_rviz_streaming::format_stall_clock(14LL * 3600000 + 13 * 60000 + 40 * 1000 + 105, text);
  EXPECT_STREQ(text, "14:13:40.105");
  qar_rviz_streaming::format_stall_clock(-1, text);
  EXPECT_STREQ(text, "23:59:59.999");
  qar_rviz_streaming::format_stall_clock(24LL * 3600000, text);
  EXPECT_STREQ(text, "00:00:00.000");
}

TEST(QtStallWatchdog, NoLineBeforeTheLimit)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  dog.tick();
  clock.advance(2900 * kMs);
  dog.poll();
  EXPECT_EQ(sink.writes(), 0);
  EXPECT_EQ(dog.stall_lines(), 0U);
  clock.advance(100 * kMs);  // exactly 3.0 s
  dog.poll();
  EXPECT_EQ(sink.writes(), 1);
  EXPECT_EQ(dog.stall_lines(), 1U);
}

TEST(QtStallWatchdog, NothingBeforeTheFirstTick)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  poll_for(dog, clock, 20.0);
  EXPECT_EQ(sink.writes(), 0);
}

TEST(QtStallWatchdog, OneStallLineNamesTheStepAndWhenItBegan)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  dog.tick();
  clock.advance(100 * kMs);
  dog.set_step("3D render batch");  // at 1.1 s -> 10:00:01.100
  poll_for(dog, clock, 10.0);
  EXPECT_EQ(sink.writes(), 1);
  EXPECT_EQ(
    sink.text(),
    "[qar-stall] 10:00:04.100 Qt thread has not ticked for 3.1 s; last step: 3D render batch "
    "(since 10:00:01.100)\n");
}

TEST(QtStallWatchdog, OneResumeLineGivesTheExactGapAndTheStep)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  dog.tick();                                   // 1.000 s
  dog.set_step("panel manager tick");
  poll_for(dog, clock, 19.0);                   // 20.000 s: one stall line
  clock.advance(604 * kMs);                     // 20.604 s
  dog.tick();                                   // the first tick after the gap: 19.604 s
  clock.advance(180 * kMs);                     // the watchdog looks 180 ms later
  dog.tick();
  dog.poll();
  EXPECT_EQ(dog.stall_lines(), 1U);
  EXPECT_EQ(dog.resume_lines(), 1U);
  const std::string text = sink.text();
  EXPECT_EQ(count_of(text, "[qar-stall]"), 2U) << text;
  EXPECT_NE(
    text.find(
      "Qt thread ticks again after 19.6 s (last step: panel manager tick)\n"), std::string::npos)
    << text;
  poll_for(dog, clock, 1.0);
  EXPECT_EQ(dog.resume_lines(), 1U) << "no second resume line";

  StallReport report;
  ASSERT_TRUE(dog.take_report(&report));
  EXPECT_NEAR(report.gap_s, 19.604, 0.001);
  EXPECT_STREQ(report.step, "panel manager tick");
  EXPECT_EQ(report.began_ms_of_day, 10LL * 3600 * 1000 + 1000);
  EXPECT_FALSE(dog.take_report(&report)) << "one report per stall";
}

TEST(QtStallWatchdog, TheLastStepSeenDuringTheStallIsKept)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  dog.tick();
  dog.set_step("heartbeat");
  poll_for(dog, clock, 4.0);
  dog.set_step("RViz frame: swap");  // the thread moved on inside the stall
  poll_for(dog, clock, 2.0);
  dog.tick();
  dog.poll();
  EXPECT_NE(sink.text().find("(last step: RViz frame: swap)"), std::string::npos) << sink.text();
}

TEST(QtStallWatchdog, AStallThatEndsStartsAgainWithTheNextOne)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  for (int round = 0; round < 3; ++round) {
    dog.tick();
    poll_for(dog, clock, 4.0);
    clock.advance(10 * kMs);
    dog.tick();
    dog.poll();
  }
  EXPECT_EQ(dog.stall_lines(), 3U);
  EXPECT_EQ(dog.resume_lines(), 3U);
}

TEST(QtStallWatchdog, PausedWritesNothingAndTheResumeIsNotAGap)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  dog.tick();
  dog.set_paused(true);
  poll_for(dog, clock, 30.0);
  EXPECT_EQ(sink.writes(), 0);
  dog.set_paused(false);
  poll_for(dog, clock, 2.0);  // two seconds after the resume: still within the limit
  EXPECT_EQ(sink.writes(), 0);
  dog.tick();
  dog.poll();
  EXPECT_EQ(sink.writes(), 0) << sink.text();
  poll_for(dog, clock, 4.0);
  EXPECT_EQ(dog.stall_lines(), 1U) << "a real stall after the resume is still seen";
}

TEST(QtStallWatchdog, APauseInsideAStallEndsItWithoutALine)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  dog.tick();
  poll_for(dog, clock, 4.0);
  ASSERT_EQ(dog.stall_lines(), 1U);
  dog.set_paused(true);
  dog.poll();
  dog.set_paused(false);
  dog.tick();
  dog.poll();
  EXPECT_EQ(dog.resume_lines(), 0U);
  EXPECT_EQ(sink.writes(), 1);
}

TEST(QtStallWatchdog, ASlowTickTimerRaisesTheLimit)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  dog.set_tick_period_ms(2000.0);  // four periods: 8 s
  dog.tick();
  poll_for(dog, clock, 7.5);
  EXPECT_EQ(sink.writes(), 0);
  poll_for(dog, clock, 1.0);
  EXPECT_EQ(dog.stall_lines(), 1U);
  dog.set_tick_period_ms(0.1);  // back to the 3 s of the configuration
  dog.tick();
  poll_for(dog, clock, 2.5);
  EXPECT_EQ(dog.stall_lines(), 1U);
}

TEST(QtStallWatchdog, TheDumpIsWrittenOnceAtTheDumpLimit)
{
  FakeClock clock;
  FixedSink sink;
  FakeDump dump;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink, &dump);
  dog.tick();
  poll_for(dog, clock, 4.75);
  EXPECT_EQ(dump.calls, 0);
  poll_for(dog, clock, 0.25);  // 5.0 s of silence
  EXPECT_EQ(dump.calls, 1);
  EXPECT_NE(
    sink.text().find("stack dump written: rviz_stall_test.dmp\n"), std::string::npos)
    << sink.text();
  poll_for(dog, clock, 20.0);
  EXPECT_EQ(dump.calls, 1);
  // A second stall of the same run writes no second dump.
  dog.tick();
  dog.poll();
  dog.tick();
  poll_for(dog, clock, 10.0);
  EXPECT_EQ(dump.calls, 1);
  EXPECT_EQ(dog.dumps_written(), 1U);
}

TEST(QtStallWatchdog, AFailedDumpIsSaidAndNotRepeated)
{
  FakeClock clock;
  FixedSink sink;
  FakeDump dump;
  dump.succeed = false;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink, &dump);
  dog.tick();
  poll_for(dog, clock, 12.0);
  EXPECT_EQ(dump.calls, 1);
  EXPECT_NE(sink.text().find("stack dump not written: the volume is nearly full\n"),
    std::string::npos) << sink.text();
  EXPECT_EQ(dog.dumps_written(), 0U);
}

TEST(QtStallWatchdog, NoDumpWithoutAWriter)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink, nullptr);
  dog.tick();
  poll_for(dog, clock, 12.0);
  EXPECT_EQ(count_of(sink.text(), "[qar-stall]"), 1U);
}

TEST(QtStallWatchdog, ALongStepNameIsCutAndTheLineStillEnds)
{
  FakeClock clock;
  FixedSink sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  const std::string name(2000, 'x');
  dog.tick();
  dog.set_step(name.c_str());
  poll_for(dog, clock, 4.0);
  const std::string text = sink.text();
  ASSERT_FALSE(text.empty());
  EXPECT_EQ(text.back(), '\n');
  EXPECT_LE(text.size(), 401U);
  dog.set_step(nullptr);  // a null step is the empty text
  EXPECT_STREQ(dog.step(), "");
}

TEST(QtStallWatchdog, NeitherSideAllocates)
{
  FakeClock clock;
  FixedSink sink;
  FakeDump dump;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink, &dump);
  g_allocations = 0;
  g_counting = true;
  // The Qt thread's calls.
  for (int i = 0; i < 100; ++i) {
    dog.tick();
    dog.set_step("3D render batch");
    dog.set_step(qar_rviz_streaming::kStallStepOutsideTick);
  }
  dog.set_tick_period_ms(0.1);
  dog.set_paused(false);
  // The watchdog thread's stall path: the stall line, the dump line, the resume line.
  poll_for(dog, clock, 8.0);
  clock.advance(10 * kMs);
  dog.tick();
  dog.poll();
  StallReport report;
  const bool reported = dog.take_report(&report);
  g_counting = false;
  EXPECT_TRUE(reported);
  EXPECT_EQ(g_allocations, 0);
  EXPECT_EQ(dog.stall_lines(), 1U);
  EXPECT_EQ(dog.resume_lines(), 1U);
  EXPECT_EQ(dump.calls, 1);
}

TEST(QtStallWatchdog, TheThreadWritesTheLinesAndJoinsAtOnce)
{
  struct SteadyClock : StallClock
  {
    std::int64_t steady_ns() const override
    {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    std::int64_t local_ms_of_day(std::int64_t at_ns) const override {return at_ns / kMs;}
  } clock;
  LockedSink sink;
  StallWatchdogConfig config;
  config.stall_after_ns = 150 * kMs;
  config.poll_every_ns = 20 * kMs;
  QtStallWatchdog dog(config, &clock, &sink);
  dog.start();
  dog.start();  // idempotent
  dog.tick();
  dog.set_step("drain results");
  const auto wait_for = [&](const std::string & needle) {
      for (int i = 0; i < 100 && sink.text().find(needle) == std::string::npos; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      return sink.text().find(needle) != std::string::npos;
    };
  EXPECT_TRUE(wait_for("has not ticked for")) << sink.text();
  EXPECT_NE(sink.text().find("last step: drain results"), std::string::npos);
  dog.tick();
  EXPECT_TRUE(wait_for("ticks again after")) << sink.text();
  StallReport report;
  EXPECT_TRUE(dog.take_report(&report));
  EXPECT_GE(report.gap_s, 0.15);

  const auto begin = std::chrono::steady_clock::now();
  dog.stop();
  const auto joined = std::chrono::steady_clock::now() - begin;
  EXPECT_LT(joined, std::chrono::seconds(1));
  dog.stop();  // idempotent
}

TEST(QtStallWatchdog, AThreadThatWasNeverStartedStopsAndDiesQuietly)
{
  FakeClock clock;
  FixedSink sink;
  {
    QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
    dog.stop();
  }
  {
    // A slow poll interval does not delay the stop: the wait is woken.
    StallWatchdogConfig config;
    config.poll_every_ns = 30 * kSecond;
    QtStallWatchdog dog(config, &clock, &sink);
    dog.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto begin = std::chrono::steady_clock::now();
    dog.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - begin, std::chrono::seconds(1));
  }
  EXPECT_EQ(sink.writes(), 0);
}

TEST(QtStallWatchdog, TheReportIsPublishedBeforeTheResumeLine)
{
  FakeClock clock;
  // Looks for the report in the instant the resume line is written.
  struct ReportProbeSink : StallSink
  {
    void write(const char * text, std::size_t length) override
    {
      if (std::string(text, length).find("ticks again after") != std::string::npos) {
        StallReport report;
        report_ready = dog != nullptr && dog->take_report(&report);
        saw_line = true;
      }
    }
    QtStallWatchdog * dog = nullptr;
    bool saw_line = false;
    bool report_ready = false;
  } sink;
  QtStallWatchdog dog(StallWatchdogConfig{}, &clock, &sink);
  sink.dog = &dog;
  dog.tick();
  poll_for(dog, clock, 4.0);
  clock.advance(10 * kMs);
  dog.tick();
  dog.poll();
  EXPECT_TRUE(sink.saw_line);
  EXPECT_TRUE(sink.report_ready);
}

TEST(QtStallWatchdog, AStopWaitsThreeSecondsForAHangingDumpAndLeavesTheThread)
{
  struct SteadyClock : StallClock
  {
    std::int64_t steady_ns() const override
    {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    std::int64_t local_ms_of_day(std::int64_t at_ns) const override {return at_ns / kMs;}
  } clock;
  // A dump that does not return until the test lets it go.
  struct HangingDump : StallDumpWriter
  {
    bool write_dump(char * detail, std::size_t capacity) override
    {
      entered = true;
      while (!release) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      returned = true;
      const char * const text = "released";
      std::size_t n = 0;
      while (text[n] != '\0' && n + 1 < capacity) {
        detail[n] = text[n];
        ++n;
      }
      detail[n] = '\0';
      return true;
    }
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> returned{false};
  } dump;
  LockedSink sink;
  StallWatchdogConfig config;
  config.stall_after_ns = 100 * kMs;
  config.dump_after_ns = 100 * kMs;
  config.poll_every_ns = 20 * kMs;
  {
    QtStallWatchdog dog(config, &clock, &sink, &dump);
    dog.start();
    dog.tick();
    for (int i = 0; i < 200 && !dump.entered; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    ASSERT_TRUE(dump.entered) << sink.text();
    const auto begin = std::chrono::steady_clock::now();
    dog.stop();
    const auto waited = std::chrono::steady_clock::now() - begin;
    EXPECT_GE(waited, std::chrono::milliseconds(2900));
    EXPECT_LT(waited, std::chrono::seconds(6));
    EXPECT_EQ(count_of(sink.text(), "did not end within 3 s (a stack dump is still being written)"),
      1U) << sink.text();
    dog.stop();  // nothing is left to wait for
  }
  // The watchdog is gone. The dump returns now: the thread ends without writing a line or
  // touching the destroyed object.
  const std::string before = sink.text();
  dump.release = true;
  for (int i = 0; i < 100 && !dump.returned; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  EXPECT_TRUE(dump.returned);
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  EXPECT_EQ(sink.text(), before);
  EXPECT_EQ(count_of(sink.text(), "stack dump written"), 0U);
}

TEST(QtStallWatchdog, TheSystemWatchdogStartsAndStopsWithoutOutput)
{
  std::unique_ptr<QtStallWatchdog> dog = qar_rviz_streaming::make_system_stall_watchdog(false);
  ASSERT_NE(dog, nullptr);
  dog->start();
  dog->tick();
  dog->set_step("heartbeat");
  dog->stop();
  EXPECT_EQ(dog->stall_lines(), 0U);
}

#ifdef _WIN32

TEST(QtStallWatchdogWindows, TheSinkWritesTheBytesOfALineToAFile)
{
  const std::filesystem::path path =
    std::filesystem::temp_directory_path() / "qar_stall_sink_test.txt";
  HANDLE const file = CreateFileW(
    path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
    nullptr);
  ASSERT_NE(file, INVALID_HANDLE_VALUE);
  {
    qar_rviz_streaming::FileHandleStallSink sink(file);
    sink.write("[qar-stall] one\n", 16);
    sink.write("[qar-stall] two\n", 16);
  }
  CloseHandle(file);
  std::ifstream in(path, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  std::filesystem::remove(path);
  EXPECT_EQ(text, "[qar-stall] one\n[qar-stall] two\n");
}

TEST(QtStallWatchdogWindows, ASinkWithoutAHandleIgnoresTheLine)
{
  qar_rviz_streaming::FileHandleStallSink none(nullptr);
  none.write("x\n", 2);
  qar_rviz_streaming::FileHandleStallSink invalid(INVALID_HANDLE_VALUE);
  invalid.write("x\n", 2);
  SUCCEED();
}

TEST(QtStallWatchdogWindows, TheSystemClockGivesTheLocalTimeOfDay)
{
  qar_rviz_streaming::SystemStallClock clock;
  const std::int64_t now = clock.steady_ns();
  const std::int64_t earlier = clock.local_ms_of_day(now - 5 * kSecond);
  const std::int64_t later = clock.local_ms_of_day(now);
  SYSTEMTIME t;
  GetLocalTime(&t);
  const std::int64_t reference = ((t.wHour * 60LL + t.wMinute) * 60 + t.wSecond) * 1000;
  // Midnight apart, the difference is five seconds, and "now" is within a second of the API.
  std::int64_t diff = later - earlier;
  if (diff < 0) {
    diff += 24LL * 3600 * 1000;
  }
  EXPECT_NEAR(static_cast<double>(diff), 5000.0, 50.0);
  std::int64_t off = later - reference;
  if (off > 12LL * 3600 * 1000) {
    off -= 24LL * 3600 * 1000;
  } else if (off < -12LL * 3600 * 1000) {
    off += 24LL * 3600 * 1000;
  }
  EXPECT_LT(std::llabs(off), 1500);
}

TEST(QtStallWatchdogWindows, TheDumpWriterWritesAFileOfThisTestProcessOrRefuses)
{
  const std::filesystem::path dir =
    std::filesystem::temp_directory_path() / "qar_stall_dump_test";
  std::filesystem::remove_all(dir);
  char detail[300];
  {
    qar_rviz_streaming::MiniDumpStallWriter refusing(dir.wstring(), ~0ULL);
    EXPECT_FALSE(refusing.write_dump(detail, sizeof(detail)));
    EXPECT_STREQ(detail, "the volume is nearly full");
  }
  {
    qar_rviz_streaming::MiniDumpStallWriter writer(dir.wstring(), 0);
    ASSERT_TRUE(writer.write_dump(detail, sizeof(detail))) << detail;
    const std::filesystem::path file = std::filesystem::path(std::string(detail));
    EXPECT_NE(file.filename().string().find("rviz_stall_"), std::string::npos);
    EXPECT_EQ(file.extension().string(), ".dmp");
    EXPECT_TRUE(std::filesystem::exists(file));
    EXPECT_GT(std::filesystem::file_size(file), 0U);
  }
  std::filesystem::remove_all(dir);
}

#endif  // _WIN32
