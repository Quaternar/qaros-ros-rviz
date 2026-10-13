// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/video_source_executor.hpp"

#include <chrono>
#include <exception>

namespace qar_rviz_streaming
{

namespace
{
/// Every VideoSourceExecutor needs a node name unique within the process —
/// one instance per QarStreamingPanel in practice, but nothing here assumes
/// only one ever exists.
std::atomic<std::uint64_t> next_id{0};

/// A fresh context on the HOST's DDS domain (so the new participant can see
/// the publishers RViz sees), with logging initialisation off: RViz already
/// configured the process-wide logging, and a context that did not would
/// otherwise re-run (and on shutdown tear down) rcl_logging underneath it.
/// Throws whatever rclcpp throws if the middleware cannot start.
rclcpp::Context::SharedPtr make_private_context()
{
  rclcpp::InitOptions options;
  options.auto_initialize_logging(false);
  const rclcpp::Context::SharedPtr host = rclcpp::contexts::get_global_default_context();
  if (host && host->is_valid()) {
    options.set_domain_id(host->get_domain_id());
  }
  auto context = std::make_shared<rclcpp::Context>();
  context->init(0, nullptr, options);
  return context;
}

/// Only what a subscribe-only node needs. Every extra service/publisher a node
/// creates is another endpoint the NEW participant has to announce and have
/// discovered before the video subscription can match (parameter services,
/// /parameter_events, /rosout), for features nothing uses on this node.
rclcpp::NodeOptions make_node_options(const rclcpp::Context::SharedPtr & context)
{
  rclcpp::NodeOptions options;
  options.context(context);
  options.use_global_arguments(false);
  options.enable_rosout(false);
  options.start_parameter_services(false);
  options.start_parameter_event_publisher(false);
  return options;
}
}  // namespace

VideoSourceExecutor::VideoSourceExecutor()
{
  try {
    context_ = make_private_context();
    node_ = std::make_shared<rclcpp::Node>(
      "qar_video_sources_" + std::to_string(next_id.fetch_add(1)), make_node_options(context_));
    rclcpp::ExecutorOptions executor_options;
    executor_options.context = context_;
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(executor_options);
    executor_->add_node(node_);
    thread_ = std::thread([this] { spin(); });
  } catch (const std::exception & e) {
    error_ = e.what();
    if (error_.empty()) {
      error_ = "unknown error";
    }
    release();
  } catch (...) {
    error_ = "unknown error";
    release();
  }
}

VideoSourceExecutor::~VideoSourceExecutor()
{
  release();
}

void VideoSourceExecutor::spin()
{
  try {
    executor_->spin();
  } catch (...) {
    // rclcpp can throw out of spin() if the context is shut down under an
    // in-flight take (e.g. the signal handler shutting every context down at
    // once). An exception escaping a std::thread would terminate the whole
    // RViz process, and there is nothing useful to do with it here: the
    // sources on this node simply stop receiving.
  }
  spin_exited_.store(true);
}

void VideoSourceExecutor::release() noexcept
{
  if (thread_.joinable()) {
    // cancel() repeatedly until the thread has actually left spin(), not once:
    // rclcpp's spin() starts by setting its own `spinning` flag, which erases a
    // cancel() that raced in before the thread got that far — a single cancel()
    // followed by join() could then block forever (an immediately destroyed
    // instance hits exactly that window).
    while (!spin_exited_.load()) {
      try {
        executor_->cancel();
      } catch (...) {
        // cancel() throws when it cannot trigger its wake-up guard condition,
        // which happens once the context has already been shut down (the signal
        // handler does that to every context at once). A shut-down context also
        // ends spin() by itself, so just keep waiting for the thread to leave.
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    thread_.join();
  }
  executor_.reset();
  node_.reset();
  if (context_) {
    try {
      context_->shutdown("qar_rviz_streaming VideoSourceExecutor released");
    } catch (...) {
      // Already shut down by the signal handler, or the middleware is gone:
      // either way there is nothing left to release.
    }
    context_.reset();
  }
}

}  // namespace qar_rviz_streaming
