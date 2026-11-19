// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__GPU_FRAME_GATE_HPP_
#define QAR_RVIZ_STREAMING__GPU_FRAME_GATE_HPP_

/// @file gpu_frame_gate.hpp
/// @brief The decision that lets or stops a frame of RViz: standard library only, so it is tested
/// without Ogre.
///
/// RViz renders its main window through Ogre's renderOneFrame, which asks every frame listener
/// first and does not render when one answers false. The plugin's frame listener asks this gate.
/// While the GPU progress watch says that the GPU has not finished the frames queued so far, the
/// gate stops the frame, so the GUI thread issues no GL command that could wait for the GPU.
/// When a frame ends, the gate tells the watch that one more frame was queued.
///
/// The hooks are set and cleared on the GUI thread, which is also the only thread that asks the
/// gate; the counters are atomics so that a log line may read them from anywhere.

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace qar_rviz_streaming
{

/// @brief What the gate asks of the GPU progress watch. Each member may be empty (no watch: the
/// gate lets every frame start).
struct GpuFrameGateHooks
{
  /// True while the GPU is not progressing (a read of atomics, no GL call).
  std::function<bool()> closed;

  /// A frame of RViz was queued and ended on the GUI thread.
  std::function<void()> frame_issued;

  /// A frame of RViz was stopped because the gate is closed.
  std::function<void()> frame_skipped;
};

/// @brief The gate of the frames of RViz. Used from the GUI thread only, except the counters.
class GpuFrameGate
{
public:
  /// Replaces the hooks (a session starts or ends).
  void set_hooks(GpuFrameGateHooks hooks)
  {
    hooks_ = std::move(hooks);
  }

  /// Removes the hooks; every frame may start again.
  void clear_hooks()
  {
    hooks_ = GpuFrameGateHooks{};
  }

  /// True when RViz may render the frame that is about to start. False counts the frame as
  /// skipped and tells the watch.
  bool frame_may_start()
  {
    asked_.fetch_add(1, std::memory_order_relaxed);
    if (hooks_.closed && hooks_.closed()) {
      skipped_.fetch_add(1, std::memory_order_relaxed);
      if (hooks_.frame_skipped) {
        hooks_.frame_skipped();
      }
      return false;
    }
    return true;
  }

  /// A frame that was allowed has ended: the watch queues a fence behind it.
  void frame_ended()
  {
    ended_.fetch_add(1, std::memory_order_relaxed);
    if (hooks_.frame_issued) {
      hooks_.frame_issued();
    }
  }

  /// The frames that asked.
  std::uint64_t asked() const {return asked_.load(std::memory_order_relaxed);}

  /// The frames that were stopped.
  std::uint64_t skipped() const {return skipped_.load(std::memory_order_relaxed);}

  /// The frames that ended after they were allowed.
  std::uint64_t ended() const {return ended_.load(std::memory_order_relaxed);}

private:
  GpuFrameGateHooks hooks_;
  std::atomic<std::uint64_t> asked_{0};
  std::atomic<std::uint64_t> skipped_{0};
  std::atomic<std::uint64_t> ended_{0};
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__GPU_FRAME_GATE_HPP_
