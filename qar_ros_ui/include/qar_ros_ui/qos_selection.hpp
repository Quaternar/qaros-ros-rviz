// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__QOS_SELECTION_HPP_
#define QAR_ROS_UI__QOS_SELECTION_HPP_

/// @file qos_selection.hpp
/// @brief Choosing a QoS profile that actually matches what is on the graph.
///
/// DDS matching is one-directional: a RELIABLE subscription never hears a
/// BEST_EFFORT publisher, a VOLATILE subscription never receives what a
/// TRANSIENT_LOCAL publisher latched before it arrived. Sensor drivers publish
/// BEST_EFFORT, maps and static transforms TRANSIENT_LOCAL, so a fixed default
/// is wrong for half the topics a UI wants to show. `Adaptive` reads the
/// endpoints that exist and picks the profile that hears all of them; the
/// bridges re-run it (`reconcile()`) so a publisher that appears later with a
/// different profile is not missed. Pure functions over `rmw_qos_profile_t`,
/// so the rules are unit-testable without a graph.

#include <cstddef>
#include <string>
#include <vector>

#include "rmw/types.h"

namespace qar_ros_ui
{

enum class QosPreset
{
  Adaptive = 0,    ///< Derived from the endpoints present (see file comment).
  SystemDefault,   ///< rmw_qos_profile_system_default.
  Reliable,        ///< RELIABLE / VOLATILE / KEEP_LAST(depth): rclcpp's default.
  SensorData,      ///< BEST_EFFORT / VOLATILE / KEEP_LAST(depth).
  TransientLocal   ///< RELIABLE / TRANSIENT_LOCAL / KEEP_LAST(depth).
};
const char * to_string(QosPreset preset);

/// @brief Subscription profile. Adaptive: BEST_EFFORT if any publisher is,
/// else RELIABLE; TRANSIENT_LOCAL only if every publisher offers it (a
/// TRANSIENT_LOCAL request cannot match a VOLATILE offer); with no publisher
/// at all, BEST_EFFORT/VOLATILE — the profile that matches whatever comes.
rmw_qos_profile_t choose_subscription_profile(
  const std::vector<rmw_qos_profile_t> & publishers,
  QosPreset preset,
  std::size_t depth = 10);

/// @brief Publisher profile. Adaptive: RELIABLE (matches every subscription),
/// TRANSIENT_LOCAL if any existing subscription or publisher on the topic uses
/// it (so a UI publishing `/initialpose`-style latched data behaves like its
/// peers), else VOLATILE.
rmw_qos_profile_t choose_publisher_profile(
  const std::vector<rmw_qos_profile_t> & subscriptions,
  const std::vector<rmw_qos_profile_t> & other_publishers,
  QosPreset preset,
  std::size_t depth = 10);

/// @brief Equal in the policies that decide matching and delivery
/// (reliability, durability, history, depth).
bool same_matching_policies(const rmw_qos_profile_t & a, const rmw_qos_profile_t & b);

/// @brief "reliable/volatile/keep_last(10)" for logs and diagnostics.
std::string describe_profile(const rmw_qos_profile_t & profile);

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__QOS_SELECTION_HPP_
