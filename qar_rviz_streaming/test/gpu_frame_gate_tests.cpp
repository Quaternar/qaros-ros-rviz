// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The gate of the frames of RViz (gpu_frame_gate.hpp): a frame starts while the GPU progress
// watch says the GPU progresses, is stopped while it says the GPU does not, and a frame that
// ended is reported to the watch. The frame listener of the panel calls exactly these two
// members; the listener itself is RViz-side and is not tested here.

#include <gtest/gtest.h>

#include "qar_rviz_streaming/gpu_frame_gate.hpp"

#include <atomic>
#include <cstdint>

using qar_rviz_streaming::GpuFrameGate;
using qar_rviz_streaming::GpuFrameGateHooks;

namespace
{

/// A stand-in for the watch: a flag for "closed" and counters for what the gate reported.
struct FakeWatch
{
  std::atomic<bool> is_closed{false};
  int issued = 0;
  int skipped = 0;
  int closed_asks = 0;

  GpuFrameGateHooks hooks()
  {
    GpuFrameGateHooks h;
    h.closed = [this] {++closed_asks; return is_closed.load();};
    h.frame_issued = [this] {++issued;};
    h.frame_skipped = [this] {++skipped;};
    return h;
  }
};

}  // namespace

TEST(GpuFrameGate, WithoutHooksEveryFrameStarts)
{
  GpuFrameGate gate;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(gate.frame_may_start());
    gate.frame_ended();
  }
  EXPECT_EQ(gate.asked(), 100u);
  EXPECT_EQ(gate.skipped(), 0u);
  EXPECT_EQ(gate.ended(), 100u);
}

TEST(GpuFrameGate, AFrameIsStoppedWhileTheGateIsClosedAndStartsAgainWhenItOpens)
{
  FakeWatch watch;
  GpuFrameGate gate;
  gate.set_hooks(watch.hooks());

  EXPECT_TRUE(gate.frame_may_start());
  gate.frame_ended();
  EXPECT_EQ(watch.issued, 1);

  watch.is_closed = true;
  for (int i = 0; i < 5; ++i) {
    EXPECT_FALSE(gate.frame_may_start()) << "frame " << i;
  }
  EXPECT_EQ(gate.skipped(), 5u);
  EXPECT_EQ(watch.skipped, 5);
  EXPECT_EQ(watch.issued, 1) << "a stopped frame queues nothing, so it reports no issued frame";

  watch.is_closed = false;
  EXPECT_TRUE(gate.frame_may_start());
  gate.frame_ended();
  EXPECT_EQ(watch.issued, 2);
  EXPECT_EQ(gate.asked(), 7u);
  EXPECT_EQ(gate.ended(), 2u);
}

TEST(GpuFrameGate, EveryFrameAsksTheWatchOnceAndNothingElse)
{
  FakeWatch watch;
  GpuFrameGate gate;
  gate.set_hooks(watch.hooks());
  for (int i = 0; i < 10; ++i) {
    gate.frame_may_start();
  }
  EXPECT_EQ(watch.closed_asks, 10);
  EXPECT_EQ(watch.issued, 0);
}

TEST(GpuFrameGate, ClearedHooksLetEveryFrameStartAgain)
{
  FakeWatch watch;
  watch.is_closed = true;
  GpuFrameGate gate;
  gate.set_hooks(watch.hooks());
  EXPECT_FALSE(gate.frame_may_start());
  gate.clear_hooks();
  EXPECT_TRUE(gate.frame_may_start());
  gate.frame_ended();
  EXPECT_EQ(watch.issued, 0) << "a cleared hook is not called";
}

TEST(GpuFrameGate, ASkipHookThatIsMissingIsFine)
{
  FakeWatch watch;
  watch.is_closed = true;
  GpuFrameGateHooks h = watch.hooks();
  h.frame_skipped = nullptr;
  h.frame_issued = nullptr;
  GpuFrameGate gate;
  gate.set_hooks(h);
  EXPECT_FALSE(gate.frame_may_start());
  gate.frame_ended();
  EXPECT_EQ(gate.skipped(), 1u);
}
