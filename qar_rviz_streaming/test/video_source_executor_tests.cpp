// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// VideoSourceExecutor: the private rclcpp context (= its own DDS participant) +
// node + executor thread RosImageFrameSource subscribes video topics on.
//
// What is pinned here is the property the design rests on: the node lives in a
// context of its OWN, so a second reader of the same 6.2 MB topic inside RViz's
// participant (RViz's Image display) cannot starve it (see video_source_executor.hpp
// for the evidence) — plus the lifecycle rules that go with it: teardown
// order, a context shutdown that spares the host's, and a destructor that cannot
// hang when it races the spin thread's start-up.
//
// One rclcpp "host" context for the whole suite, on a domain derived from the
// process id so a concurrent RViz or a second test run on this host never sees our
// entities (same convention as qar_ros_ui's test_main.cpp). The executor under
// test copies that domain id, so its private participant lands there too.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "rclcpp/rclcpp.hpp"

#ifdef QAR_VIDEO_SOURCE_LIVE_TESTS
#include "std_msgs/msg/string.hpp"
#endif

#include "qar_rviz_streaming/video_source_executor.hpp"

using qar_rviz_streaming::VideoSourceExecutor;

namespace
{

int process_id()
{
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

rclcpp::Context::SharedPtr host_context()
{
  return rclcpp::contexts::get_global_default_context();
}

}  // namespace

TEST(VideoSourceExecutor, CreatesItsNodeInAContextOfItsOwn)
{
  VideoSourceExecutor executor;

  ASSERT_TRUE(executor.error().empty()) << executor.error();
  ASSERT_NE(executor.node(), nullptr);
  ASSERT_NE(executor.context(), nullptr);
  EXPECT_TRUE(executor.context()->is_valid());
  // The node is on the private context...
  EXPECT_EQ(executor.node()->get_node_base_interface()->get_context(), executor.context());
  // ...which is NOT RViz's: that is the whole point (a separate DDS participant).
  EXPECT_NE(executor.context(), host_context());
  EXPECT_TRUE(host_context()->is_valid());
}

TEST(VideoSourceExecutor, JoinsTheHostsDdsDomain)
{
  VideoSourceExecutor executor;

  ASSERT_NE(executor.context(), nullptr) << executor.error();
  // A participant on another domain would never see the publishers RViz sees.
  EXPECT_EQ(executor.context()->get_domain_id(), host_context()->get_domain_id());
}

TEST(VideoSourceExecutor, EveryInstanceGetsItsOwnUniquelyNamedNodeAndContext)
{
  VideoSourceExecutor first;
  VideoSourceExecutor second;

  ASSERT_NE(first.node(), nullptr) << first.error();
  ASSERT_NE(second.node(), nullptr) << second.error();
  const std::string first_name = first.node()->get_name();
  const std::string second_name = second.node()->get_name();
  EXPECT_EQ(first_name.rfind("qar_video_sources_", 0), 0u) << first_name;
  EXPECT_EQ(second_name.rfind("qar_video_sources_", 0), 0u) << second_name;
  EXPECT_NE(first_name, second_name);
  EXPECT_NE(first.context(), second.context());
}

TEST(VideoSourceExecutor, DestructionShutsDownItsOwnContextAndLeavesTheHostsAlone)
{
  rclcpp::Context::SharedPtr context;
  {
    VideoSourceExecutor executor;
    context = executor.context();
    ASSERT_NE(context, nullptr) << executor.error();
    ASSERT_TRUE(context->is_valid());
  }

  EXPECT_FALSE(context->is_valid()) << "the private context must be shut down with its owner";
  EXPECT_TRUE(host_context()->is_valid()) << "RViz's own context must never be touched";
}

TEST(VideoSourceExecutor, SurvivesItsContextBeingShutDownFromOutside)
{
  // What rclcpp's signal handler does to every context on Ctrl+C: the spin
  // thread's executor loses its context under it, and the destructor's
  // cancel() cannot trigger its guard condition (it throws). Neither may
  // terminate RViz or leave the destructor waiting.
  {
    VideoSourceExecutor executor;
    ASSERT_NE(executor.context(), nullptr) << executor.error();
    executor.context()->shutdown("test: shut down from outside");
    EXPECT_FALSE(executor.context()->is_valid());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  {
    // ...and with no time for the spin thread to notice before the destructor runs.
    VideoSourceExecutor executor;
    ASSERT_NE(executor.context(), nullptr) << executor.error();
    executor.context()->shutdown("test: shut down from outside");
  }
  EXPECT_TRUE(host_context()->is_valid());
}

TEST(VideoSourceExecutor, ConstructingAndDestroyingStraightAwayNeverHangs)
{
  // The destructor's cancel() can land before the spin thread has entered
  // spin() — where rclcpp's own `spinning = true` would erase it — so the
  // destructor keeps cancelling until the thread is really out. A destructor
  // that cancelled once would block in join() for ever on some of these
  // iterations (ctest's TIMEOUT turns that into a failure).
  for (int i = 0; i < 25; ++i) {
    VideoSourceExecutor executor;
    ASSERT_TRUE(executor.error().empty()) << "iteration " << i << ": " << executor.error();
  }
}

#ifdef QAR_VIDEO_SOURCE_LIVE_TESTS

TEST(VideoSourceExecutor, ServicesASubscriptionFromAnotherContextOnItsOwnThread)
{
  // Declared BEFORE the executor, so they outlive its spin thread and any
  // callback still in flight when the executor is torn down.
  std::atomic<int> received{0};
  std::mutex mutex;
  std::thread::id callback_thread;

  VideoSourceExecutor executor;
  ASSERT_NE(executor.node(), nullptr) << executor.error();

  // The publisher is in the HOST context: this crosses participants exactly like
  // a publisher process does for RViz + the video source.
  auto host_node = std::make_shared<rclcpp::Node>("qar_video_source_executor_test_publisher");
  const std::string topic = "/qar_video_source_executor_test_" + std::to_string(process_id());
  auto publisher = host_node->create_publisher<std_msgs::msg::String>(
    topic, rclcpp::QoS(10).reliable());

  auto subscription = executor.node()->create_subscription<std_msgs::msg::String>(
    topic, rclcpp::QoS(10).reliable(),
    [&](std_msgs::msg::String::ConstSharedPtr) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        callback_thread = std::this_thread::get_id();
      }
      received.fetch_add(1);
    });

  // Nothing on THIS thread spins anything: delivery can only happen if the
  // executor's own thread is servicing the subscription. Discovery between the
  // two participants takes a moment, so keep publishing until one arrives.
  std_msgs::msg::String message;
  message.data = "frame";
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (received.load() == 0 && std::chrono::steady_clock::now() < deadline) {
    publisher->publish(message);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  EXPECT_GT(received.load(), 0)
    << "no message crossed from the host context to the private context in 30 s";
  std::lock_guard<std::mutex> lock(mutex);
  EXPECT_NE(callback_thread, std::this_thread::get_id())
    << "the callback must run on the executor's thread, not the test's";
  subscription.reset();
}

#endif  // QAR_VIDEO_SOURCE_LIVE_TESTS

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  rclcpp::InitOptions options;
  options.auto_initialize_logging(false);
  options.set_domain_id(static_cast<size_t>(50 + (process_id() % 50)));
  rclcpp::init(argc, argv, options, rclcpp::SignalHandlerOptions::None);

  const int result = RUN_ALL_TESTS();

  rclcpp::shutdown();
  return result;
}
