// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The speed of the render tick: the plan of each tier, the choice of the tier, the throttle of the
// zero-interval timer and the plan applied to a real QTimer. The cases that run an event loop
// measure wall time, so the target runs them one at a time (RUN_SERIAL).

#include "qar_rviz_streaming/render_tick_pacing.hpp"
#include "qar_rviz_streaming/render_tick_timer.hpp"

#include <gtest/gtest.h>

#include <chrono>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QTimer>

namespace
{

using qar_rviz_streaming::TickDemand;
using qar_rviz_streaming::TickThrottle;
using qar_rviz_streaming::TickTier;
using qar_rviz_streaming::TickTimerPlan;
using qar_rviz_streaming::tick_timer_plan;
using qar_rviz_streaming::tick_tier_for;

TickDemand live(bool connected, std::size_t streams_3d, std::size_t ar_panels)
{
  TickDemand d;
  d.live_mode = true;
  d.live_connected = connected;
  d.streams_3d = streams_3d;
  d.ar_panel_streams = ar_panels;
  return d;
}

// Runs the event loop of this thread for @p ms. The loop blocks in the event dispatcher while
// nothing is due, so an idle timer costs no CPU here (QCoreApplication::processEvents with a time
// limit does not wait for events and would spin).
void run_event_loop_for(int ms)
{
  QEventLoop loop;
  QTimer stop;
  stop.setSingleShot(true);
  stop.setTimerType(Qt::PreciseTimer);
  QObject::connect(&stop, &QTimer::timeout, &loop, &QEventLoop::quit);
  stop.start(ms);
  loop.exec();
}

}  // namespace

TEST(TickTimerPlan, StreamingIsTheTickOfTheConfiguredPeriod)
{
  TickTimerPlan p = tick_timer_plan(TickTier::kStreaming, 0.1);
  EXPECT_EQ(p.interval_ms, 0);
  EXPECT_TRUE(p.zero_interval);
  EXPECT_DOUBLE_EQ(p.throttle_us, 100.0);
  EXPECT_DOUBLE_EQ(p.period_ms, 0.1);

  p = tick_timer_plan(TickTier::kStreaming, 0.5);
  EXPECT_EQ(p.interval_ms, 0);
  EXPECT_TRUE(p.zero_interval);
  EXPECT_DOUBLE_EQ(p.throttle_us, 500.0);

  p = tick_timer_plan(TickTier::kStreaming, 1.0);
  EXPECT_EQ(p.interval_ms, 1);
  EXPECT_FALSE(p.zero_interval);

  p = tick_timer_plan(TickTier::kStreaming, 16.0);
  EXPECT_EQ(p.interval_ms, 16);
  EXPECT_FALSE(p.zero_interval);
  EXPECT_DOUBLE_EQ(p.period_ms, 16.0);
}

TEST(TickTimerPlan, IdleIsFiftyMsAndNeverFasterThanStreaming)
{
  TickTimerPlan p = tick_timer_plan(TickTier::kIdle, 0.1);
  EXPECT_EQ(p.interval_ms, 50);
  EXPECT_FALSE(p.zero_interval);
  EXPECT_DOUBLE_EQ(p.throttle_us, 0.0);
  EXPECT_DOUBLE_EQ(p.period_ms, 50.0);

  // A configured tick that is slower than the idle period is kept.
  p = tick_timer_plan(TickTier::kIdle, 100.0);
  EXPECT_EQ(p.interval_ms, 100);
  EXPECT_FALSE(p.zero_interval);

  // An idle period below 1 ms is never a zero-interval timer.
  p = tick_timer_plan(TickTier::kIdle, 0.1, 0.0);
  EXPECT_FALSE(p.zero_interval);
  EXPECT_GE(p.interval_ms, 1);
}

TEST(TickTier, Table)
{
  // Live mode: not connected, whatever exists.
  EXPECT_EQ(tick_tier_for(live(false, 0, 0)), TickTier::kIdle);
  EXPECT_EQ(tick_tier_for(live(false, 1, 1)), TickTier::kIdle);
  // Connected without a 3D stream and without an AR panel stream.
  EXPECT_EQ(tick_tier_for(live(true, 0, 0)), TickTier::kIdle);
  // One 3D stream, one AR panel stream.
  EXPECT_EQ(tick_tier_for(live(true, 1, 0)), TickTier::kStreaming);
  EXPECT_EQ(tick_tier_for(live(true, 0, 1)), TickTier::kStreaming);

  // Preview mode: the manager streams, or an AR panel stream exists.
  TickDemand preview;
  EXPECT_EQ(tick_tier_for(preview), TickTier::kIdle);
  preview.preview_streaming = true;
  EXPECT_EQ(tick_tier_for(preview), TickTier::kStreaming);
  preview.preview_streaming = false;
  preview.ar_panel_streams = 1;
  EXPECT_EQ(tick_tier_for(preview), TickTier::kStreaming);

  // A scripted run keeps the streaming tier, connected or not.
  TickDemand scripted = live(false, 0, 0);
  scripted.scripted_driver = true;
  EXPECT_EQ(tick_tier_for(scripted), TickTier::kStreaming);
}

TEST(TickThrottle, AdmitsOncePerInterval)
{
  using std::chrono::microseconds;
  const TickThrottle::Clock::time_point t0{};
  TickThrottle throttle;
  throttle.configure(tick_timer_plan(TickTier::kStreaming, 0.1));
  EXPECT_TRUE(throttle.active());
  EXPECT_TRUE(throttle.admit(t0));
  EXPECT_FALSE(throttle.admit(t0 + microseconds(50)));
  EXPECT_TRUE(throttle.admit(t0 + microseconds(100)));
  EXPECT_FALSE(throttle.admit(t0 + microseconds(150)));

  // A new plan forgets the last admitted tick.
  throttle.configure(tick_timer_plan(TickTier::kStreaming, 0.1));
  EXPECT_TRUE(throttle.admit(t0 + microseconds(160)));

  // An ordinary timer is paced by Qt and every tick is admitted.
  throttle.configure(tick_timer_plan(TickTier::kIdle, 0.1));
  EXPECT_FALSE(throttle.active());
  EXPECT_TRUE(throttle.admit(t0));
  EXPECT_TRUE(throttle.admit(t0));
}

TEST(GuiGapRecordMs, FollowsTheTimerPeriod)
{
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::gui_gap_record_ms(0.1), 25.0);
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::gui_gap_record_ms(1.0), 25.0);
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::gui_gap_record_ms(10.0), 25.0);
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::gui_gap_record_ms(50.0), 125.0);
}

TEST(RenderTickTimer, IdlePlanDoesNotSpin)
{
  QTimer timer;
  qar_rviz_streaming::apply_tick_plan(timer, tick_timer_plan(TickTier::kIdle, 0.1), true);
  int fires = 0;
  QObject::connect(&timer, &QTimer::timeout, [&fires] {++fires;});

  run_event_loop_for(600);
  // 50 ms over 600 ms is about 12 timeouts; a zero-interval timer gives tens of thousands.
  EXPECT_LE(fires, 30);
  EXPECT_GE(fires, 1);
}

TEST(RenderTickTimer, StreamingPlanIsUnchanged)
{
  QTimer timer;
  qar_rviz_streaming::apply_tick_plan(timer, tick_timer_plan(TickTier::kStreaming, 0.1), true);
  EXPECT_EQ(timer.interval(), 0);
  EXPECT_EQ(timer.timerType(), Qt::PreciseTimer);

  TickThrottle throttle;
  throttle.configure(tick_timer_plan(TickTier::kStreaming, 0.1));
  int bodies = 0;
  QObject::connect(&timer, &QTimer::timeout, [&] {
      if (throttle.admit(TickThrottle::Clock::now())) {
        ++bodies;
      }
    });

  QElapsedTimer wall;
  wall.start();
  run_event_loop_for(300);
  const double seconds = static_cast<double>(wall.nsecsElapsed()) / 1.0e9;
  // More bodies per second than any 1 ms timer can deliver, and no more than the 100 us throttle.
  EXPECT_GT(bodies, 1500.0 * seconds);
  EXPECT_LE(bodies, 10000.0 * seconds + 2.0);
}

TEST(RenderTickTimer, ASwitchLeavesAStoppedTimerStopped)
{
  QTimer timer;
  qar_rviz_streaming::apply_tick_plan(timer, tick_timer_plan(TickTier::kIdle, 0.1), true);
  EXPECT_TRUE(timer.isActive());
  EXPECT_EQ(timer.interval(), 50);

  qar_rviz_streaming::apply_tick_plan(timer, tick_timer_plan(TickTier::kStreaming, 0.1), false);
  EXPECT_TRUE(timer.isActive());
  EXPECT_EQ(timer.interval(), 0);

  timer.stop();
  qar_rviz_streaming::apply_tick_plan(timer, tick_timer_plan(TickTier::kIdle, 0.1), false);
  EXPECT_FALSE(timer.isActive());
}

TEST(RenderTickTimer, AnActiveZeroIntervalTimerSwitchedToIdleStopsSpinning)
{
  QTimer timer;
  qar_rviz_streaming::apply_tick_plan(timer, tick_timer_plan(TickTier::kStreaming, 0.1), true);
  ASSERT_TRUE(timer.isActive());
  ASSERT_EQ(timer.interval(), 0);
  int fires = 0;
  QObject::connect(&timer, &QTimer::timeout, [&fires] {++fires;});

  // The panel's switch path: the running timer gets the idle plan without a restart.
  qar_rviz_streaming::apply_tick_plan(timer, tick_timer_plan(TickTier::kIdle, 0.1), false);
  EXPECT_TRUE(timer.isActive());
  EXPECT_EQ(timer.interval(), 50);

  run_event_loop_for(600);
  // 50 ms over 600 ms is about 12 timeouts; a zero-interval timer gives tens of thousands.
  EXPECT_LE(fires, 30);
  EXPECT_GE(fires, 1);
}

int main(int argc, char ** argv)
{
  // QtCore only: the timers need an event loop of the thread, not a platform plugin.
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
