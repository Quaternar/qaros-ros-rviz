// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The Windows scheduling opt-out: what it reports, and that applying/releasing
// it is safe to repeat. The OS call itself is exercised for real (it only turns
// off this test process's own throttling), but no test depends on it being
// accepted: an older Windows legitimately refuses or lacks it.

#include <gtest/gtest.h>

#include "qar_rviz_streaming/windows_scheduling.hpp"

#include <string>

using qar_rviz_streaming::ForegroundState;
using qar_rviz_streaming::SchedulingOptOutResult;
using qar_rviz_streaming::WindowsSchedulingGuard;
using qar_rviz_streaming::foreground_state;

TEST(WindowsScheduling, ApplyIsIdempotentAndReleaseIsSafeToRepeat)
{
  WindowsSchedulingGuard guard;
  EXPECT_FALSE(guard.applied());
  guard.release();  // before apply: nothing to undo

  const SchedulingOptOutResult first = guard.apply();
  EXPECT_TRUE(guard.applied());
  const SchedulingOptOutResult second = guard.apply();  // does not touch the OS again
  EXPECT_EQ(first.windows, second.windows);
  EXPECT_EQ(first.power_throttling_disabled, second.power_throttling_disabled);
  EXPECT_EQ(first.timer_period_raised, second.timer_period_raised);

  guard.release();
  guard.release();  // a second release must not call timeEndPeriod again
  EXPECT_FALSE(guard.result().timer_period_raised);
}

TEST(WindowsScheduling, TheSummaryIsOneSchedLineThatMatchesWhatHappened)
{
  WindowsSchedulingGuard guard;
  const SchedulingOptOutResult result = guard.apply();
  const std::string line = result.summary();
  EXPECT_EQ(line.rfind("SCHED:", 0), 0u) << line;
  EXPECT_EQ(line.find('\n'), std::string::npos);

#ifdef _WIN32
  EXPECT_TRUE(result.windows);
  EXPECT_TRUE(result.timer_period_raised) << "timeBeginPeriod(1) is accepted on every Windows";
  if (result.power_throttling_disabled) {
    EXPECT_NE(line.find("throttling OFF"), std::string::npos) << line;
  } else if (!result.process_information_api_found) {
    EXPECT_NE(line.find("not available"), std::string::npos) << line;
  } else {
    EXPECT_NE(line.find("FAILED, error"), std::string::npos) << line;
  }
  EXPECT_NE(line.find("timeBeginPeriod(1) OK"), std::string::npos) << line;
#else
  EXPECT_FALSE(result.windows);
  EXPECT_NE(line.find("not Windows"), std::string::npos) << line;
#endif
}

TEST(WindowsScheduling, NoWindowHandleMeansTheForegroundStateIsUnknown)
{
  EXPECT_EQ(foreground_state(0), ForegroundState::kUnknown);
}
