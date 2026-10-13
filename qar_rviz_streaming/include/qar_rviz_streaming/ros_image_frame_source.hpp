// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__ROS_IMAGE_FRAME_SOURCE_HPP_
#define QAR_RVIZ_STREAMING__ROS_IMAGE_FRAME_SOURCE_HPP_

/// @file ros_image_frame_source.hpp
/// @brief A ROS Image/CompressedImage topic, decoded onto a `LatestVideoFrame`.
///
/// The self-contained piece variant (a) — "Open video in AR" on a web AR tool's
/// Image/CompressedImage page opens a new streamed AR panel of this — and
/// variant (b) (a Qt-dock substitute for RViz's uncapturable native Image/Camera
/// display, `qt_widget_capture.hpp`'s region-source path) both reuse: subscribe
/// on a dedicated node, decode off that thread, publish into a
/// `qar_qt_streaming::LatestVideoFrame`. Nothing here is variant-specific.
///
/// ## Threading — the one rule that matters
///
/// The subscription runs on `VideoSourceExecutor`'s dedicated node and thread
/// (video_source_executor.hpp) — its own `SingleThreadedExecutor::spin()`,
/// blocked waiting for work, independent of RViz's Qt GUI thread, and in its
/// own rclcpp context (= its own DDS participant) so a second, slow reader of
/// the same topic inside RViz (its Image display) cannot starve delivery to
/// this one. Without that the video stutters: RViz spins ITS OWN node with
/// `spin_some` only when the GUI thread is otherwise idle, so a subscription
/// placed there could go many message-periods without being serviced under load,
/// and by the time it was, a shallow KeepLast history had already overwritten the
/// message with a newer one — most of the stream silently lost. Subscribing on a
/// node that is always blocked in `spin()` on a thread of its own removes that
/// dependency entirely.
///
/// The callback does the least possible amount of work — store the
/// newest message, notify — and a dedicated worker thread does the rest, so a
/// slow decode never delays the executor thread's next dispatch:
///
/// ```
/// rclcpp callback (VideoSourceExecutor's own thread)
///   -> stores the newest ConstSharedPtr, drops whatever was pending, notifies
/// worker thread (started in the ctor, joined in the dtor)
///   -> wakes, takes the newest message (older ones were already dropped —
///      that IS the frame-skipping policy), decodes with video_frame.hpp,
///      publishes into LatestVideoFrame (or calls set_status on a decode error)
/// GUI thread, later, on its own tick
///   -> reads frames()->latest() (VideoPanelWidget::paintEvent) — never blocks
///      on the worker; the mailbox is the only thing shared.
/// ```
///
/// ## Lifetime
///
/// The destructor runs on the GUI thread while callbacks run on
/// VideoSourceExecutor's independent thread, so — unlike a subscription on
/// RViz's own spin_some-on-the-GUI-thread node — `raw_sub_.reset()` /
/// `compressed_sub_.reset()` do NOT guarantee no callback is still executing
/// when the destructor proceeds: `reset()` only stops FUTURE dispatches, and
/// rclcpp's executor keeps the Subscription (and the callback it holds) alive
/// for the duration of a dispatch already in progress even after our own
/// shared_ptr to it is gone. So the callbacks never touch `this` at all — the
/// mutex/condition-variable/pending-message/stop-flag/received/dropped state
/// they share with the worker lives in a heap-allocated `CallbackState`
/// (ros_image_frame_source.cpp), owned by `state_` and captured BY VALUE
/// (its own shared_ptr copy) in each subscription lambda, so an in-flight
/// callback keeps that state alive and race-free no matter how far the
/// destructor has otherwise progressed. Destruction order: reset the
/// subscriptions, then signal stop and join the worker — the worker
/// is a member function on `this` because, unlike the callbacks, it is always
/// fully joined before the rest of `this` is torn down.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <QSize>

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/compressed_image.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "qar_qt_streaming/latest_video_frame.hpp"

#include "qar_rviz_streaming/image_topic_transport.hpp"

namespace qar_rviz_streaming
{

/// Callback-shared state (mutex, pending messages, stop flag, received/dropped
/// counters) — heap-allocated and reference-counted separately from
/// `RosImageFrameSource` itself; see this file's "Lifetime" section above.
/// Defined in ros_image_frame_source.cpp.
struct CallbackState;

/// @brief Subscribes to one Image/CompressedImage topic and decodes it onto a
/// `LatestVideoFrame` a `VideoPanelWidget` (or any other reader) can display.
class RosImageFrameSource
{
public:
  /// Subscribes immediately, with an Adaptive QoS chosen from the topic's
  /// CURRENT publishers (`qar_ros_ui::choose_subscription_profile`, shallow
  /// depth 2): RELIABLE when every publisher on the topic offers it, else
  /// BEST_EFFORT — never a hardcoded best-effort that would silently drop most
  /// of a large (e.g. raw 1080p, ~6.2 MB) frame from a reliable publisher. See
  /// the file-local `choose_qos` in ros_image_frame_source.cpp for the
  /// reasoning behind this.
  ///
  /// @param node The dedicated node subscriptions run on — see this file's
  ///        "Threading" section above; typically `QarStreamingPanel::
  ///        video_executor_`'s node (video_source_executor.hpp), NOT RViz's
  ///        own node.
  /// @param topic Fully-qualified topic name.
  /// @param transport Which message type to subscribe as — see
  ///        `choose_image_transport`.
  /// @param graph_node Optional node whose graph cache is read to learn the
  ///        topic's CURRENT publishers (and so which QoS to request). Pass a
  ///        long-lived node of the host context (RViz's own node) whenever
  ///        @p node lives in a freshly created context: that participant has
  ///        not discovered anything yet, so it would see no publishers, fall
  ///        back to BEST_EFFORT and lose most of a reliable ~6 MB stream.
  ///        Null (the default) reads the graph from @p node itself.
  /// @param max_size Upper bound the decoders scale down to fit (never up).
  ///        An invalid/empty size means no limit.
  RosImageFrameSource(
    rclcpp::Node::SharedPtr node, std::string topic, ImageTransport transport,
    rclcpp::Node::SharedPtr graph_node = nullptr, QSize max_size = QSize(1280, 1280));

  /// GUI thread: resets the subscription first, then stops and joins the
  /// worker. See this file's "Lifetime" section above for why an in-flight
  /// callback racing this is still safe.
  ~RosImageFrameSource();

  RosImageFrameSource(const RosImageFrameSource &) = delete;
  RosImageFrameSource & operator=(const RosImageFrameSource &) = delete;

  /// @brief The mailbox the worker publishes decoded frames into. Never null.
  std::shared_ptr<qar_qt_streaming::LatestVideoFrame> frames() const { return frames_; }

  const std::string & topic() const { return topic_; }
  ImageTransport transport() const { return transport_; }

  /// @name Counters — for the host's log/status line
  /// Thread-safe; cheap enough to poll from a render tick.
  /// @{

  /// Messages the ROS callback has seen, whether or not the worker got to them.
  std::uint64_t received() const;
  /// Messages the worker successfully decoded and published.
  std::uint64_t decoded() const { return decoded_.load(std::memory_order_relaxed); }
  /// Messages overwritten by a newer one before the worker reached them (frame
  /// skipping under load — expected, not an error).
  std::uint64_t dropped() const;
  /// The most recent decode failure's message, or empty if there has been none.
  std::string last_error() const;
  /// Mean wall-clock time the worker spent inside decode_raw_image /
  /// decode_compressed_image, across every successful decode so far. 0 before
  /// the first one. For the host's periodic "AR VIDEO:" diagnostics line.
  double avg_decode_ms() const;

  /// @}

private:
  /// Subscription callbacks: static, and given the shared state explicitly
  /// (never `this`) — see this file's "Lifetime" section above.
  static void on_raw(
    const std::shared_ptr<CallbackState> & state, sensor_msgs::msg::Image::ConstSharedPtr msg);
  static void on_compressed(
    const std::shared_ptr<CallbackState> & state,
    sensor_msgs::msg::CompressedImage::ConstSharedPtr msg);
  /// Joined in the destructor before any other member is touched further, so
  /// (unlike the callbacks above) this may freely use `this`.
  void worker_main();
  void set_last_error(const std::string & error);

  rclcpp::Node::SharedPtr node_;
  std::string topic_;
  ImageTransport transport_;
  QSize max_size_;
  std::shared_ptr<qar_qt_streaming::LatestVideoFrame> frames_;

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr raw_sub_;
  rclcpp::Subscription<sensor_msgs::msg::CompressedImage>::SharedPtr compressed_sub_;

  /// The single-slot handoff to the worker, plus received/dropped — every
  /// field the subscription callbacks touch. Heap-allocated so an in-flight
  /// callback (which captured its own shared_ptr copy) stays valid regardless
  /// of `this`'s destruction progress; see "Lifetime" above. Never null.
  std::shared_ptr<CallbackState> state_;

  std::thread worker_;

  std::atomic<std::uint64_t> decoded_{0};
  /// Sum of every successful decode's duration, in microseconds; paired with
  /// decoded_ to form the mean in avg_decode_ms(). A sum + count rather than an
  /// EMA: cheap, exact, and this is a diagnostics readout, not a control input.
  std::atomic<std::uint64_t> decode_us_total_{0};
  mutable std::mutex error_mutex_;
  std::string last_error_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__ROS_IMAGE_FRAME_SOURCE_HPP_
