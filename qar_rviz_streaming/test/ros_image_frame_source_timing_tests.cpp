// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// RosImageFrameSource's per-frame timing (rx_<topic>.csv): the part of the instrument that
// stamps the executor callback and the decode worker, exercised against a REAL ROS 2 publisher
// in another context (so the message crosses participants exactly as it does in RViz).
//
// Same harness as video_source_executor_tests.cpp: one rclcpp "host" context on a DDS domain
// derived from the process id, so a concurrent RViz or a second test run never sees our topic.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

#include "qar_ar_core/timing_log.hpp"
#include "qar_rviz_streaming/image_topic_transport.hpp"
#include "qar_rviz_streaming/ros_image_frame_source.hpp"
#include "qar_rviz_streaming/video_source_executor.hpp"

using qar_ar_core::kTimingBlank;
using qar_ar_core::TimingLog;
using qar_rviz_streaming::ImageTransport;
using qar_rviz_streaming::RosImageFrameSource;
using qar_rviz_streaming::VideoSourceExecutor;

namespace
{

namespace fs = std::filesystem;

int process_id()
{
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

class TempDir
{
public:
  TempDir()
  {
    std::random_device rd;
    std::ostringstream name;
    name << "qar_rx_timing_test_" << std::hex << rd() << rd();
    path_ = fs::temp_directory_path() / name.str();
    fs::create_directories(path_);
  }
  ~TempDir()
  {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }
  TempDir(const TempDir &) = delete;
  TempDir & operator=(const TempDir &) = delete;
  const fs::path & path() const { return path_; }

private:
  fs::path path_;
};

using Row = std::map<std::string, std::string>;

std::vector<Row> read_rows(const fs::path & file)
{
  std::ifstream in(file, std::ios::binary);
  std::string line;
  std::vector<std::string> header;
  std::vector<Row> rows;
  bool first = true;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::vector<std::string> cells;
    std::string cell;
    std::istringstream cs(line);
    while (std::getline(cs, cell, ',')) {
      cells.push_back(cell);
    }
    if (!line.empty() && line.back() == ',') {
      cells.emplace_back();
    }
    if (first) {
      header = cells;
      first = false;
      continue;
    }
    Row row;
    for (std::size_t c = 0; c < header.size(); ++c) {
      row[header[c]] = (c < cells.size()) ? cells[c] : std::string();
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

std::int64_t as_int(const Row & row, const std::string & column)
{
  const auto it = row.find(column);
  EXPECT_NE(it, row.end()) << "no column " << column;
  return (it == row.end() || it->second.empty()) ? kTimingBlank : std::stoll(it->second);
}

class ScopedTimingLog
{
public:
  explicit ScopedTimingLog(TimingLog & log)
  : previous_(TimingLog::override_instance_for_testing(&log))
  {
  }
  ~ScopedTimingLog() { TimingLog::override_instance_for_testing(previous_); }
  ScopedTimingLog(const ScopedTimingLog &) = delete;
  ScopedTimingLog & operator=(const ScopedTimingLog &) = delete;

private:
  TimingLog * previous_;
};

sensor_msgs::msg::Image make_image(const std::string & encoding, std::uint8_t value)
{
  sensor_msgs::msg::Image msg;
  msg.header.frame_id = "camera";
  msg.height = 36;
  msg.width = 64;
  msg.encoding = encoding;
  msg.is_bigendian = 0;
  msg.step = msg.width * 3;
  msg.data.assign(static_cast<std::size_t>(msg.step) * msg.height, value);
  const std::int64_t now_us = qar_ar_core::timing_epoch_us();
  msg.header.stamp.sec = static_cast<std::int32_t>(now_us / 1000000);
  msg.header.stamp.nanosec = static_cast<std::uint32_t>((now_us % 1000000) * 1000);
  return msg;
}

/// Publish until @p done() or 30 s pass (the first frames can be lost to discovery).
template<typename Done>
void publish_until(
  const rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr & publisher,
  const std::string & encoding, Done done)
{
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  std::uint8_t value = 0;
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    publisher->publish(make_image(encoding, value++));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

}  // namespace

TEST(RosImageFrameSourceTiming, EveryReceivedMessageGetsOneRowWithTheStagesInOrder)
{
  TempDir dir;
  TimingLog log(dir.path().string(), std::chrono::milliseconds(20));
  ScopedTimingLog scope(log);

  auto host_node = std::make_shared<rclcpp::Node>("qar_rx_timing_test_publisher");
  const std::string topic = "/qar_rx_timing_test_" + std::to_string(process_id());
  auto publisher = host_node->create_publisher<sensor_msgs::msg::Image>(
    topic, rclcpp::QoS(10).reliable());

  VideoSourceExecutor executor;
  ASSERT_NE(executor.node(), nullptr) << executor.error();
  RosImageFrameSource source(executor.node(), topic, ImageTransport::Raw, host_node);

  const std::int64_t before = qar_ar_core::timing_epoch_us();
  publish_until(publisher, "bgr8", [&] { return source.decoded() >= 12; });
  ASSERT_GE(source.decoded(), 12u) << "no frame crossed from the host context in 30 s";
  const std::int64_t after = qar_ar_core::timing_epoch_us();

  // Quiesce: every received message has been decoded or dropped (overwritten by a newer one).
  const auto settle = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (source.decoded() + source.dropped() < source.received() &&
    std::chrono::steady_clock::now() < settle)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  const std::uint64_t received = source.received();
  const std::uint64_t decoded = source.decoded();
  const std::uint64_t dropped = source.dropped();
  log.flush_all();

  const fs::path file = dir.path() / ("rx_" + TimingLog::sanitize_name(topic) + ".csv");
  ASSERT_TRUE(fs::exists(file)) << file;
  const std::vector<Row> rows = read_rows(file);
  EXPECT_EQ(rows.size(), received) << "one row per message the callback saw";

  std::uint64_t decoded_rows = 0;
  std::uint64_t superseded_rows = 0;
  std::int64_t last_decoded_seq = 0;
  std::int64_t last_mailbox = 0;
  std::int64_t last_decode_seq = 0;
  std::map<std::int64_t, int> seen;
  for (const Row & row : rows) {
    seen[as_int(row, "seq")]++;
    const std::string outcome = row.at("outcome");
    const std::int64_t stamp = as_int(row, "header_stamp_us");
    const std::int64_t arrival = as_int(row, "arrival_us");
    EXPECT_GE(stamp, before - 5000) << "header.stamp is the publisher's system-clock time";
    EXPECT_LE(stamp, after + 5000);
    EXPECT_GE(arrival, stamp) << "a message cannot arrive before it was stamped";
    if (outcome == "decoded") {
      ++decoded_rows;
      const std::int64_t decode_start = as_int(row, "decode_start_us");
      const std::int64_t decode_done = as_int(row, "decode_done_us");
      const std::int64_t mailbox = as_int(row, "mailbox_us");
      EXPECT_GE(decode_start, arrival);
      EXPECT_GE(decode_done, decode_start);
      EXPECT_GE(mailbox, decode_done) << "the frame is visible only after the decode";
      EXPECT_EQ(as_int(row, "decode_seq"), last_decode_seq + 1)
        << "decode_seq is the mailbox's own count: one more per decoded frame, in order";
      last_decode_seq = as_int(row, "decode_seq");
      if (as_int(row, "seq") > last_decoded_seq) {
        last_decoded_seq = as_int(row, "seq");
        last_mailbox = mailbox;
      }
    } else if (outcome == "superseded") {
      ++superseded_rows;
      EXPECT_EQ(as_int(row, "decode_done_us"), kTimingBlank);
      EXPECT_EQ(as_int(row, "mailbox_us"), kTimingBlank);
    } else {
      ADD_FAILURE() << "unexpected outcome '" << outcome << "'";
    }
  }
  EXPECT_EQ(decoded_rows, decoded);
  EXPECT_EQ(superseded_rows, dropped);
  for (std::uint64_t n = 1; n <= received; ++n) {
    EXPECT_EQ(seen[static_cast<std::int64_t>(n)], 1) << "seq " << n << " must appear exactly once";
  }

  // The mailbox carries the trace of the frame a panel will capture next.
  const qar_qt_streaming::VideoFrameTrace trace = source.frames()->latest_trace();
  ASSERT_TRUE(trace.valid);
  EXPECT_EQ(static_cast<std::int64_t>(trace.seq), last_decoded_seq);
  EXPECT_EQ(trace.mailbox_us, last_mailbox);
  EXPECT_EQ(source.frames()->sequence(), decoded);
}

TEST(RosImageFrameSourceTiming, ADecodeFailureIsARowWithNoMailboxTime)
{
  TempDir dir;
  TimingLog log(dir.path().string(), std::chrono::milliseconds(20));
  ScopedTimingLog scope(log);

  auto host_node = std::make_shared<rclcpp::Node>("qar_rx_timing_test_bad_publisher");
  const std::string topic = "/qar_rx_timing_bad_" + std::to_string(process_id());
  auto publisher = host_node->create_publisher<sensor_msgs::msg::Image>(
    topic, rclcpp::QoS(10).reliable());

  VideoSourceExecutor executor;
  ASSERT_NE(executor.node(), nullptr) << executor.error();
  RosImageFrameSource source(executor.node(), topic, ImageTransport::Raw, host_node);

  publish_until(publisher, "no-such-encoding", [&] { return source.received() >= 3; });
  ASSERT_GE(source.received(), 3u);
  const auto settle = std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (source.last_error().empty() && std::chrono::steady_clock::now() < settle) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  log.flush_all();

  const fs::path file = dir.path() / ("rx_" + TimingLog::sanitize_name(topic) + ".csv");
  ASSERT_TRUE(fs::exists(file));
  const std::vector<Row> rows = read_rows(file);
  ASSERT_FALSE(rows.empty());
  std::size_t errors = 0;
  for (const Row & row : rows) {
    if (row.at("outcome") == "decode_error") {
      ++errors;
      EXPECT_NE(as_int(row, "decode_done_us"), kTimingBlank) << "the failed decode still ends";
      EXPECT_EQ(as_int(row, "mailbox_us"), kTimingBlank) << "nothing was published";
      EXPECT_EQ(as_int(row, "decode_seq"), kTimingBlank);
    }
  }
  EXPECT_GT(errors, 0u);
  EXPECT_EQ(source.decoded(), 0u);
}

TEST(RosImageFrameSourceTiming, WithTimingOffNothingIsWrittenAndFramesStillDecode)
{
  TempDir dir;
  TimingLog log("");   // disabled
  ScopedTimingLog scope(log);

  auto host_node = std::make_shared<rclcpp::Node>("qar_rx_timing_test_off_publisher");
  const std::string topic = "/qar_rx_timing_off_" + std::to_string(process_id());
  auto publisher = host_node->create_publisher<sensor_msgs::msg::Image>(
    topic, rclcpp::QoS(10).reliable());

  VideoSourceExecutor executor;
  ASSERT_NE(executor.node(), nullptr) << executor.error();
  RosImageFrameSource source(executor.node(), topic, ImageTransport::Raw, host_node);

  publish_until(publisher, "bgr8", [&] { return source.decoded() >= 3; });
  ASSERT_GE(source.decoded(), 3u);
  EXPECT_FALSE(source.frames()->latest_trace().valid) << "no trace is recorded when timing is off";
  std::size_t files = 0;
  for (const auto & entry : fs::directory_iterator(dir.path())) {
    (void)entry;
    ++files;
  }
  EXPECT_EQ(files, 0u);
}

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
