// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ros_image_frame_source.hpp"

#include <chrono>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "qar_ar_core/timing_log.hpp"
#include "qar_qt_streaming/video_frame.hpp"
#include "qar_ros_ui/qos_selection.hpp"

namespace qar_rviz_streaming
{

namespace
{

/// What the executor callback knows about a message by the time it is stored; the worker needs
/// it later, and the message itself (a const Image) has nowhere to carry it. Only filled while
/// per-frame timing is on (QAR_VIDEO_TIMING_DIR).
struct PendingMeta
{
  std::uint64_t seq = 0;
  std::int64_t header_stamp_us = 0;
  std::int64_t arrival_us = 0;
};

/// `header.stamp` as microseconds since the Unix epoch - the unit of every timing CSV (the
/// publisher stamps it from the same system clock, so arrival - stamp is a latency).
template<typename TimeMsg>
std::int64_t stamp_to_epoch_us(const TimeMsg & stamp)
{
  return static_cast<std::int64_t>(stamp.sec) * 1000000 +
         static_cast<std::int64_t>(stamp.nanosec) / 1000;
}

/// rx_<topic>.csv: one row per message the ROS callback received, whatever became of it.
const char * const kRxColumns[] = {
  "seq", "header_stamp_us", "arrival_us", "decode_start_us", "decode_done_us", "mailbox_us",
  "decode_seq", "outcome"};
const char * const kRxOutcomes[] = {"decoded", "superseded", "decode_error"};
constexpr std::int64_t kRxDecoded = 0;
constexpr std::int64_t kRxSuperseded = 1;
constexpr std::int64_t kRxDecodeError = 2;
constexpr int kRxOutcomeColumn = 7;

}  // namespace

/// Callback-shared state — see ros_image_frame_source.hpp's "Lifetime"
/// section. Heap-allocated and owned by `RosImageFrameSource::state_`
/// (shared_ptr), with a copy of that shared_ptr captured by value in each
/// subscription lambda, so an in-flight callback keeps it alive independent
/// of `RosImageFrameSource`'s own destruction progress.
struct CallbackState
{
  std::mutex mutex;
  std::condition_variable cv;
  // Only one of the two pending fields is ever populated, matching the
  // owning source's transport_; a newer message replaces whatever was there
  // (dropped counts that) rather than queuing.
  sensor_msgs::msg::Image::ConstSharedPtr pending_raw;
  sensor_msgs::msg::CompressedImage::ConstSharedPtr pending_compressed;
  bool stop = false;

  std::atomic<std::uint64_t> received{0};
  std::atomic<std::uint64_t> dropped{0};

  /// Per-frame timing (QAR_VIDEO_TIMING_DIR): the rx_<topic>.csv sink, null when timing is
  /// off, and what the callback recorded about whichever message is pending (guarded by
  /// `mutex`, like the message itself). Set once in the constructor, before the
  /// subscription exists, so the callbacks may read it without a lock.
  std::shared_ptr<qar_ar_core::TimingSink> timing;
  PendingMeta pending_meta;
};

namespace
{

/// The message pending in @p state is about to be overwritten by a newer one: it will never be
/// decoded. Called with state.mutex held; one queued row (no formatting, no I/O).
void note_superseded(CallbackState & state)
{
  const PendingMeta & m = state.pending_meta;
  state.timing->append({
      static_cast<std::int64_t>(m.seq), m.header_stamp_us, m.arrival_us,
      qar_ar_core::kTimingBlank, qar_ar_core::kTimingBlank,
      qar_ar_core::kTimingBlank, qar_ar_core::kTimingBlank, kRxSuperseded});
}

/// @brief QoS this subscription should use RIGHT NOW for @p topic, judged from
/// the publishers @p node's graph cache currently knows (callers pass a warm,
/// host-context node here — see the constructor's `graph_node`).
///
/// Not unconditionally `rclcpp::SensorDataQoS()` (best-effort): that matches a
/// best-effort camera driver, but a best-effort REQUEST also matches a RELIABLE
/// publisher (DDS RxO compatibility), and that match delivers to THIS reader
/// best-effort-style: any lost fragment of a large sample (these are raw
/// 1920x1080 bgr8 frames, ~6.2 MB each) is simply dropped, uncorrected, unlike a
/// reliable reader's. Against scripts/publish_video_file.py (whose default QoS
/// is rclcpp's plain RELIABLE) a reliable subscription receives 2-4x the frames
/// a best-effort one does in the same window.
/// `qar_ros_ui::choose_subscription_profile`'s Adaptive preset is the existing,
/// already-unit-tested rule for exactly this: RELIABLE when every current
/// publisher on the topic offers it, BEST_EFFORT the moment any one doesn't (a
/// reliable request cannot match a best-effort offer at all, so this is not
/// optional) — so a genuine best-effort sensor driver is still heard, and a
/// reliable driver is heard reliably instead of losing most of its (large)
/// frames for no reason.
///
/// Depth 2, not SensorDataQoS's 5: this class already keeps only the newest
/// pending message itself (on_raw/on_compressed overwrite, counted in
/// `dropped()`), so a deeper DDS-level history would only be idle memory for
/// ~6 MB frames — 2 gives a little slack for jitter without hoarding buffers.
rclcpp::QoS choose_qos(const rclcpp::Node::SharedPtr & node, const std::string & topic)
{
  std::vector<rmw_qos_profile_t> publisher_profiles;
  if (node) {
    for (const auto & info : node->get_publishers_info_by_topic(topic)) {
      publisher_profiles.push_back(info.qos_profile().get_rmw_qos_profile());
    }
  }
  const rmw_qos_profile_t chosen = qar_ros_ui::choose_subscription_profile(
    publisher_profiles, qar_ros_ui::QosPreset::Adaptive, /*depth=*/2);
  return rclcpp::QoS(rclcpp::QoSInitialization::from_rmw(chosen), chosen);
}

}  // namespace

RosImageFrameSource::RosImageFrameSource(
  rclcpp::Node::SharedPtr node, std::string topic, ImageTransport transport,
  rclcpp::Node::SharedPtr graph_node, QSize max_size)
: node_(std::move(node)),
  topic_(std::move(topic)),
  transport_(transport),
  max_size_(max_size),
  frames_(std::make_shared<qar_qt_streaming::LatestVideoFrame>()),
  state_(std::make_shared<CallbackState>())
{
  frames_->set_status("Waiting for video on " + topic_ + "\xE2\x80\xA6");  // U+2026 …

  // Per-frame timing: set before the worker starts and before any callback can run.
  {
    qar_ar_core::TimingLog & timing_log = qar_ar_core::TimingLog::instance();
    if (timing_log.enabled()) {
      state_->timing = timing_log.open(
        "rx_" + qar_ar_core::TimingLog::sanitize_name(topic_),
        std::vector<std::string>(std::begin(kRxColumns), std::end(kRxColumns)),
        std::vector<std::string>(std::begin(kRxOutcomes), std::end(kRxOutcomes)),
        kRxOutcomeColumn);
    }
  }

  // Start the worker before subscribing: a message that arrives between the
  // two would otherwise sit unread with nothing waiting on the condition
  // variable yet — harmless (it is just picked up next notify), but starting
  // the consumer first is the simpler invariant to reason about.
  worker_ = std::thread(&RosImageFrameSource::worker_main, this);

  if (!node_) {
    frames_->set_status("No ROS node available for " + topic_);
    return;
  }

  const rclcpp::QoS qos = choose_qos(graph_node ? graph_node : node_, topic_);
  // A local copy of the shared_ptr, captured BY VALUE below: the lambda must
  // never capture `this` (see the header's "Lifetime" section) so an
  // in-flight callback stays race-free no matter how far ~RosImageFrameSource
  // has otherwise progressed.
  const std::shared_ptr<CallbackState> state = state_;
  if (transport_ == ImageTransport::Compressed) {
    compressed_sub_ = node_->create_subscription<sensor_msgs::msg::CompressedImage>(
      topic_, qos,
      [state](sensor_msgs::msg::CompressedImage::ConstSharedPtr msg) {
        on_compressed(state, std::move(msg));
      });
  } else {
    raw_sub_ = node_->create_subscription<sensor_msgs::msg::Image>(
      topic_, qos,
      [state](sensor_msgs::msg::Image::ConstSharedPtr msg) {
        on_raw(state, std::move(msg));
      });
  }
}

RosImageFrameSource::~RosImageFrameSource()
{
  // GUI thread. Reset the subscriptions first — this stops FUTURE dispatches.
  // The callbacks run on VideoSourceExecutor's own thread, so that does NOT
  // guarantee an already-in-flight callback has finished; state_ being a
  // separate, shared_ptr-kept-alive heap object is what makes that race
  // harmless (see the header's "Lifetime" section) — nothing below touches
  // `this`.
  raw_sub_.reset();
  compressed_sub_.reset();

  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->stop = true;
  }
  state_->cv.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::uint64_t RosImageFrameSource::received() const
{
  return state_->received.load(std::memory_order_relaxed);
}

std::uint64_t RosImageFrameSource::dropped() const
{
  return state_->dropped.load(std::memory_order_relaxed);
}

std::string RosImageFrameSource::last_error() const
{
  std::lock_guard<std::mutex> lock(error_mutex_);
  return last_error_;
}

double RosImageFrameSource::avg_decode_ms() const
{
  const std::uint64_t n = decoded_.load(std::memory_order_relaxed);
  if (n == 0) {
    return 0.0;
  }
  const std::uint64_t total_us = decode_us_total_.load(std::memory_order_relaxed);
  return static_cast<double>(total_us) / static_cast<double>(n) / 1000.0;
}

void RosImageFrameSource::set_last_error(const std::string & error)
{
  std::lock_guard<std::mutex> lock(error_mutex_);
  last_error_ = error;
}

void RosImageFrameSource::on_raw(
  const std::shared_ptr<CallbackState> & state, sensor_msgs::msg::Image::ConstSharedPtr msg)
{
  // VideoSourceExecutor's dedicated thread. STORE ONLY: this thread also
  // dispatches every other message on that executor (and a separate worker
  // thread does the actual decode), so anything slower than that would delay
  // this — and every other subscription sharing the executor's — next
  // dispatch.
  const std::int64_t arrival_us = state->timing ? qar_ar_core::timing_epoch_us() : 0;
  const std::uint64_t seq = state->received.fetch_add(1, std::memory_order_relaxed) + 1;
  PendingMeta meta;
  if (state->timing) {
    meta = PendingMeta{seq, stamp_to_epoch_us(msg->header.stamp), arrival_us};
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->pending_raw) {
      state->dropped.fetch_add(1, std::memory_order_relaxed);
      if (state->timing) {
        note_superseded(*state);
      }
    }
    state->pending_raw = std::move(msg);
    state->pending_meta = meta;
  }
  state->cv.notify_one();
}

void RosImageFrameSource::on_compressed(
  const std::shared_ptr<CallbackState> & state,
  sensor_msgs::msg::CompressedImage::ConstSharedPtr msg)
{
  const std::int64_t arrival_us = state->timing ? qar_ar_core::timing_epoch_us() : 0;
  const std::uint64_t seq = state->received.fetch_add(1, std::memory_order_relaxed) + 1;
  PendingMeta meta;
  if (state->timing) {
    meta = PendingMeta{seq, stamp_to_epoch_us(msg->header.stamp), arrival_us};
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->pending_compressed) {
      state->dropped.fetch_add(1, std::memory_order_relaxed);
      if (state->timing) {
        note_superseded(*state);
      }
    }
    state->pending_compressed = std::move(msg);
    state->pending_meta = meta;
  }
  state->cv.notify_one();
}

void RosImageFrameSource::worker_main()
{
  for (;;) {
    sensor_msgs::msg::Image::ConstSharedPtr raw;
    sensor_msgs::msg::CompressedImage::ConstSharedPtr compressed;
    PendingMeta meta;
    {
      std::unique_lock<std::mutex> lock(state_->mutex);
      state_->cv.wait(
        lock,
        [this] { return state_->stop || state_->pending_raw || state_->pending_compressed; });
      if (state_->stop && !state_->pending_raw && !state_->pending_compressed) {
        return;
      }
      raw.swap(state_->pending_raw);
      compressed.swap(state_->pending_compressed);
      meta = state_->pending_meta;
    }

    QImage image;
    std::string error;
    bool ok = false;
    const bool timed = static_cast<bool>(state_->timing);
    const std::int64_t decode_start_us = timed ? qar_ar_core::timing_epoch_us() : 0;
    const auto decode_start = std::chrono::steady_clock::now();
    if (raw) {
      qar_qt_streaming::RawImageView view;
      view.width = raw->width;
      view.height = raw->height;
      view.step = raw->step;
      view.encoding = raw->encoding;
      view.is_bigendian = raw->is_bigendian != 0;
      view.data = raw->data.data();
      view.size = raw->data.size();
      ok = qar_qt_streaming::decode_raw_image(view, max_size_, image, &error);
    } else if (compressed) {
      ok = qar_qt_streaming::decode_compressed_image(
        compressed->format, compressed->data.data(), compressed->data.size(), max_size_, image,
        &error);
    } else {
      continue;  // spurious wake with nothing pending
    }
    const auto decode_us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - decode_start).count();
    const std::int64_t decode_done_us = timed ? qar_ar_core::timing_epoch_us() : 0;

    if (ok) {
      if (timed) {
        qar_qt_streaming::VideoFrameTrace trace;
        trace.valid = true;
        trace.seq = meta.seq;
        trace.header_stamp_us = meta.header_stamp_us;
        trace.arrival_us = meta.arrival_us;
        trace.decode_start_us = decode_start_us;
        trace.decode_done_us = decode_done_us;
        frames_->publish(std::move(image), trace);   // stamps mailbox_us under its lock
        const qar_qt_streaming::VideoFrameTrace stored = frames_->latest_trace();
        state_->timing->append({
            static_cast<std::int64_t>(meta.seq), meta.header_stamp_us, meta.arrival_us,
            decode_start_us, decode_done_us, stored.mailbox_us,
            static_cast<std::int64_t>(frames_->sequence()), kRxDecoded});
      } else {
        frames_->publish(std::move(image));
      }
      decoded_.fetch_add(1, std::memory_order_relaxed);
      decode_us_total_.fetch_add(static_cast<std::uint64_t>(decode_us), std::memory_order_relaxed);
    } else {
      set_last_error(error);
      frames_->set_status("Decode error on " + topic_ + ": " + error);
      if (timed) {
        state_->timing->append({
            static_cast<std::int64_t>(meta.seq), meta.header_stamp_us, meta.arrival_us,
            decode_start_us, decode_done_us, qar_ar_core::kTimingBlank,
            qar_ar_core::kTimingBlank, kRxDecodeError});
      }
    }
  }
}

}  // namespace qar_rviz_streaming
