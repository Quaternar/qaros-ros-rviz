// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__VIDEO_SOURCE_EXECUTOR_HPP_
#define QAR_RVIZ_STREAMING__VIDEO_SOURCE_EXECUTOR_HPP_

/// @file video_source_executor.hpp
/// @brief A private rclcpp context + node + SingleThreadedExecutor, spun on a
/// dedicated thread, that RosImageFrameSource subscribes video topics on instead
/// of RViz's own node.
///
/// ## Why a dedicated executor thread
///
/// RViz spins its own node's executor with `spin_some`, called only when the
/// Qt GUI thread is otherwise idle. Under load (redraws, other panels, AR
/// captures) that starves the executor for stretches long enough that, with a
/// shallow KeepLast subscription history, a video message arrives and is
/// overwritten by the next one before RViz's node ever takes either — the
/// cause of the AR video stutter this class avoids.
///
/// A `VideoSourceExecutor` sidesteps that entirely: its `SingleThreadedExecutor`
/// runs on a thread of its own, permanently blocked in `spin()`, so a message
/// is taken the moment DDS delivers it, independent of whatever the Qt GUI
/// thread happens to be doing.
///
/// ## Why its OWN rclcpp::Context (= its own DDS participant)
///
/// A node in RViz's default context shares RViz's DDS participant (one
/// participant per context in rmw_fastrtps_cpp): the same UDP/SHM receive
/// threads and the same per-participant reader plumbing as every RViz
/// subscription. A dedicated executor thread alone is not enough: RViz's
/// standard Image display on the SAME 6.2 MB raw topic (a second, slow reliable
/// reader, KeepLast(5), taken by `spin_some` on the GUI thread) collapses OUR
/// reader from a steady 30 msg/s to about 13 msg/s with 200-250 ms gaps, and the
/// rate recovers the moment that display is removed. A C++ emulation of both
/// readers reproduces it: 5-19 msg/s in a shared context, 28-30 msg/s with ours
/// in its own. The coupling is not writer-side (a shallower publisher history
/// changes nothing); it is the shared participant's receive path: a second
/// reader's takes (one 6.2 MB allocate+deserialize per message, under the
/// reader's lock) stall the receive thread that also feeds our reader, losing
/// fragments that the reliable protocol then has to repair. A separate
/// participant has its own sockets, receive threads and reader locks, so the
/// slow reader cannot touch our delivery. See docs/ARCHITECTURE.md,
/// "Video pipeline", for the full measurements.
///
/// The context is initialised with the host's domain id (so the participant is on
/// the same DDS domain as RViz) and with logging initialisation OFF, so creating
/// and shutting it down never touches the process-wide logging RViz set up.
/// Consequence for callers: this node's graph cache starts EMPTY (the new
/// participant has not discovered anything yet), so any QoS decision that reads
/// the current publishers must read them from a long-lived node of the host
/// context instead — see RosImageFrameSource's `graph_node`.
///
/// ## Lifetime
///
/// The destructor runs on the GUI thread. It cancels the executor, joins the
/// spin thread, releases the node and only then shuts the private context down
/// — so `node()` must never be called, and nothing spun by this executor may
/// still be running, once the destructor returns. Declaration order of the
/// members matters (see below): plain reverse-order member destruction releases
/// whatever the destructor body did not already reset in the right order.
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <rclcpp/rclcpp.hpp>

namespace qar_rviz_streaming
{

/// @brief Owns a private rclcpp context, a uniquely-named node on it and a
/// `SingleThreadedExecutor` spinning that node on a dedicated thread, so
/// subscriptions created on `node()` are serviced promptly and are insulated
/// from RViz's own (slow, GUI-thread-spun) participant.
class VideoSourceExecutor
{
public:
  /// Creates the private context and a uniquely-named node on it
  /// ("qar_video_sources_<N>"), and immediately starts spinning it on a
  /// dedicated thread; there is no separate start(). Never throws: on any
  /// failure everything already made is released, `node()` is null and
  /// `error()` says why.
  VideoSourceExecutor();

  /// GUI thread: cancels the executor and joins the spin thread, resets the
  /// node, then shuts the private context down — see this file's "Lifetime"
  /// section above.
  ~VideoSourceExecutor();

  VideoSourceExecutor(const VideoSourceExecutor &) = delete;
  VideoSourceExecutor & operator=(const VideoSourceExecutor &) = delete;

  /// @brief The node to construct subscriptions on; lives in `context()`, NOT
  /// in RViz's default context. Null only when the private context could not be
  /// created (see `error()`). Do not call once this object's destructor has
  /// started.
  rclcpp::Node::SharedPtr node() const { return node_; }

  /// @brief The private context (one DDS participant, separate from RViz's).
  /// Null exactly when `node()` is.
  rclcpp::Context::SharedPtr context() const { return context_; }

  /// @brief Why creation failed, or empty when it succeeded.
  const std::string & error() const { return error_; }

private:
  /// The spin thread's body: `spin()` until cancelled or the context shuts
  /// down; never lets an exception escape the thread (that would terminate
  /// RViz), and always raises `spin_exited_` last.
  void spin();

  /// Shared by the destructor and the failed-construction path: cancels and
  /// joins the spin thread (if any), then releases executor, node and context
  /// in that order. Safe on a partially built object; never throws.
  void release() noexcept;

  // Declaration order is load-bearing — see this file's "Lifetime" section.
  std::string error_;
  rclcpp::Context::SharedPtr context_;
  rclcpp::Node::SharedPtr node_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::atomic<bool> spin_exited_{false};
  std::thread thread_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__VIDEO_SOURCE_EXECUTOR_HPP_
