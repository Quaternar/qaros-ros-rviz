// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI_TEST__TEST_SUPPORT_HPP_
#define QAR_ROS_UI_TEST__TEST_SUPPORT_HPP_

// Shared helpers: JSON literals for UiValues, path lookups, and an in-process
// node + executor with bounded spin helpers for the live tests.

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "rclcpp/rclcpp.hpp"

#include "qar_ui_model/json.hpp"
#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_value.hpp"

#include "qar_ros_ui/node_handles.hpp"

namespace qar_ros_ui_test
{

inline int process_id()
{
#ifdef _WIN32
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

/// Unique per process and per call, so parallel test runs never share a name.
inline std::string unique_suffix()
{
  static int counter = 0;
  return std::to_string(process_id()) + "_" + std::to_string(++counter);
}

inline std::string unique_name(const std::string & base)
{
  return "/qar_ros_ui_test_" + unique_suffix() + "/" + base;
}

inline qar_ui_model::UiValue json(const std::string & text)
{
  qar_ui_model::UiValue value;
  qar_ui_model::JsonParseError error;
  if (!qar_ui_model::json_parse(text, value, error)) {
    throw std::runtime_error("bad test JSON: " + error.to_string());
  }
  return value;
}

inline std::string dump(const qar_ui_model::UiValue & value)
{
  return qar_ui_model::json_write(value);
}

inline const qar_ui_model::UiValue * at(const qar_ui_model::UiValue & root, const std::string & path)
{
  qar_ui_model::FieldPath parsed;
  std::string error;
  if (!qar_ui_model::FieldPath::parse(path, parsed, error)) {
    return nullptr;
  }
  return qar_ui_model::get_path(root, parsed);
}

class TestNode
{
public:
  explicit TestNode(const std::string & name_base = "qar_ros_ui_test")
  {
    rclcpp::NodeOptions options;
    options.start_parameter_services(false);
    options.start_parameter_event_publisher(false);
    node_ = std::make_shared<rclcpp::Node>(name_base + "_" + unique_suffix(), options);
    executor_.add_node(node_);
  }

  ~TestNode()
  {
    executor_.remove_node(node_);
  }

  rclcpp::Node::SharedPtr node() const {return node_;}
  qar_ros_ui::NodeHandles handles() const {return qar_ros_ui::NodeHandles::from(node_);}

  void spin_some()
  {
    executor_.spin_some(std::chrono::milliseconds(10));
  }

  void spin_for(std::chrono::milliseconds duration)
  {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
      spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }

  /// Spins until @p predicate holds; false on timeout. Generous by default:
  /// DDS discovery on a loaded host can take seconds, a hang must not.
  bool spin_until(
    const std::function<bool()> & predicate,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(15000))
  {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        return false;
      }
      spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
  }

private:
  rclcpp::Node::SharedPtr node_;
  rclcpp::executors::SingleThreadedExecutor executor_;
};

}  // namespace qar_ros_ui_test

#endif  // QAR_ROS_UI_TEST__TEST_SUPPORT_HPP_
