// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

// One rclcpp context for the whole suite, on a domain derived from the process
// id so a concurrent RViz or a second test run on this host never sees our
// entities. Signal handlers stay off: gtest owns the process.

#include <gtest/gtest.h>

#include "rclcpp/rclcpp.hpp"

#include "test_support.hpp"

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);

  rclcpp::InitOptions options;
  options.auto_initialize_logging(false);
  options.set_domain_id(static_cast<size_t>(50 + (qar_ros_ui_test::process_id() % 50)));
  rclcpp::init(argc, argv, options, rclcpp::SignalHandlerOptions::None);

  const int result = RUN_ALL_TESTS();

  rclcpp::shutdown();
  return result;
}
