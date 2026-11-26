// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The process exit guard against a clock that the test moves, a sink that keeps the lines and a
// terminator that only counts: no test ends its own process.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "qar_rviz_streaming/exit_guard.hpp"

namespace
{

using qar_rviz_streaming::ExitGuard;
using qar_rviz_streaming::ExitGuardConfig;
using qar_rviz_streaming::ExitTerminator;
using qar_rviz_streaming::StallClock;
using qar_rviz_streaming::StallSink;

constexpr std::int64_t kSecond = 1'000'000'000;
constexpr std::int64_t kMs = 1'000'000;

/// Steady 1 s is 10:00:00.000 of the local clock.
class FakeClock : public StallClock
{
public:
  std::int64_t steady_ns() const override {return now_ns.load();}
  std::int64_t local_ms_of_day(std::int64_t at_ns) const override
  {
    return 10LL * 3600 * 1000 + (at_ns - kSecond) / kMs;
  }
  void advance(std::int64_t ns) {now_ns += ns;}
  std::atomic<std::int64_t> now_ns{1 * kSecond};
};

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

class CountingTerminator : public ExitTerminator
{
public:
  void terminate(int code) override
  {
    last_code = code;
    calls.fetch_add(1);
  }
  std::atomic<int> calls{0};
  std::atomic<int> last_code{-1};
};

struct Fixture
{
  FakeClock clock;
  LockedSink sink;
  CountingTerminator terminator;
  ExitGuardConfig config;
};

template<typename Condition>
bool wait_for(Condition condition, std::chrono::milliseconds limit = std::chrono::seconds(5))
{
  const auto end = std::chrono::steady_clock::now() + limit;
  while (!condition()) {
    if (std::chrono::steady_clock::now() > end) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return true;
}

TEST(ExitGuard, ANeverArmedGuardNeverTerminates)
{
  Fixture f;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  f.clock.advance(60 * kSecond);
  EXPECT_FALSE(guard.poll());
  EXPECT_EQ(f.terminator.calls.load(), 0);
  EXPECT_TRUE(f.sink.text().empty());
  EXPECT_FALSE(guard.armed());
  EXPECT_FALSE(guard.fired());
}

TEST(ExitGuard, AnArmedGuardTerminatesOnceAfterTheDeadline)
{
  Fixture f;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.arm();
  EXPECT_TRUE(guard.armed());
  f.clock.advance(5 * kSecond - 1);
  EXPECT_FALSE(guard.poll()) << "one nanosecond before the deadline";
  EXPECT_EQ(f.terminator.calls.load(), 0);
  f.clock.advance(1);
  EXPECT_TRUE(guard.poll());
  EXPECT_EQ(f.terminator.calls.load(), 1);
  EXPECT_EQ(f.terminator.last_code.load(), 3);
  EXPECT_TRUE(guard.fired());
  EXPECT_FALSE(guard.poll()) << "the line and the termination happen once";
  f.clock.advance(10 * kSecond);
  EXPECT_FALSE(guard.poll());
  EXPECT_EQ(f.terminator.calls.load(), 1);
}

TEST(ExitGuard, ADisarmedGuardDoesNotTerminate)
{
  Fixture f;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.arm();
  f.clock.advance(2 * kSecond);
  guard.disarm();
  f.clock.advance(60 * kSecond);
  EXPECT_FALSE(guard.poll());
  EXPECT_EQ(f.terminator.calls.load(), 0);
  EXPECT_FALSE(guard.armed());
}

TEST(ExitGuard, ASecondArmKeepsTheEarlierDeadline)
{
  Fixture f;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.arm();
  f.clock.advance(4 * kSecond);
  guard.arm();
  f.clock.advance(1 * kSecond);
  EXPECT_TRUE(guard.poll()) << "5 s after the first arm, not 9 s";
  EXPECT_EQ(f.terminator.calls.load(), 1);
}

TEST(ExitGuard, TheLineNamesTheTimeTheLimitAndTheLastStep)
{
  Fixture f;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.set_step("panel teardown");
  guard.arm();
  f.clock.advance(5 * kSecond + 250 * kMs);
  guard.set_step("session owner exit");
  ASSERT_TRUE(guard.poll());
  EXPECT_EQ(
    f.sink.text(),
    "[qar-exit] 10:00:05.250 RViz did not end within 5 s after quitting; "
    "last step: session owner exit; the process ends now\n");
}

TEST(ExitGuard, AGuardWithNoStepNamesNone)
{
  Fixture f;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.arm();
  f.clock.advance(5 * kSecond);
  ASSERT_TRUE(guard.poll());
  EXPECT_NE(f.sink.text().find("last step: (none);"), std::string::npos) << f.sink.text();
}

TEST(ExitGuard, AFractionalLimitIsWrittenWithADecimal)
{
  Fixture f;
  f.config.deadline_ns = 2 * kSecond + 500 * kMs;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.arm();
  f.clock.advance(f.config.deadline_ns);
  ASSERT_TRUE(guard.poll());
  EXPECT_NE(f.sink.text().find("did not end within 2.5 s after"), std::string::npos)
    << f.sink.text();
}

TEST(ExitGuard, TheThreadTerminatesAnArmedGuardAfterTheDeadline)
{
  Fixture f;
  f.config.slice_ns = 5 * kMs;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.start();
  guard.start();   // a second start makes no second thread
  guard.arm();
  f.clock.advance(2 * kSecond);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  EXPECT_EQ(f.terminator.calls.load(), 0) << "before the deadline";
  f.clock.advance(3 * kSecond);
  ASSERT_TRUE(wait_for([&f] { return f.terminator.calls.load() > 0; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  EXPECT_EQ(f.terminator.calls.load(), 1);
  EXPECT_NE(f.sink.text().find("[qar-exit] "), std::string::npos);
  EXPECT_TRUE(guard.fired());
}

TEST(ExitGuard, TheThreadOfAGuardThatIsNeverArmedLeavesWithTheGuard)
{
  Fixture f;
  f.config.slice_ns = 5 * kMs;
  {
    ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
    guard.start();
    f.clock.advance(60 * kSecond);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
  }
  EXPECT_EQ(f.terminator.calls.load(), 0);
  EXPECT_TRUE(f.sink.text().empty());
}

TEST(ExitGuard, TheThreadDoesNothingWhenTheGuardIsDisarmedBeforeTheDeadline)
{
  Fixture f;
  f.config.slice_ns = 5 * kMs;
  ExitGuard guard(f.config, &f.clock, &f.sink, &f.terminator);
  guard.start();
  guard.arm();
  f.clock.advance(4 * kSecond);
  guard.disarm();
  f.clock.advance(10 * kSecond);
  std::this_thread::sleep_for(std::chrono::milliseconds(60));
  EXPECT_EQ(f.terminator.calls.load(), 0);
}

TEST(ExitGuard, TheProcessWideFunctionsDoNothingWhileTheGuardIsNotStarted)
{
  using namespace qar_rviz_streaming;
  // The process-wide guard is not started in this test process: no call may arm or end anything.
  EXPECT_FALSE(process_exit_guard_running());
  set_process_exit_step("a step");
  arm_process_exit_guard();
  EXPECT_FALSE(process_exit_guard_running());
}

}  // namespace
