// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_ROS_ENTITY_HPP_
#define QAR_RVIZ_STREAMING__AR_ROS_ENTITY_HPP_

/// @file ar_ros_entity.hpp
/// @brief What an AR tool points at: one topic, service or action on the graph.
///
/// Pure C++17 — no Qt, no rclcpp, no rviz — so everything downstream of it
/// (panel generation, control routing, the exposure registry) is unit-tested in
/// the plain-CMake build. `qar_ros_ui::GraphDiscovery` fills these in from the
/// live graph; the Qt dialog only ever moves them around.

#include <cstdint>
#include <string>
#include <string_view>

namespace qar_rviz_streaming
{

/// @brief The three things a ROS graph offers a UI. Deliberately NOT a copy of
/// `qar_ros_ui`'s snapshot types: this side of the plugin must stay ROS-free so
/// its rules are testable without a graph.
enum class RosEntityKind : std::uint8_t
{
  Topic = 0,
  Service,
  Action
};

/// @brief Wire/id spelling: "topic", "service", "action".
const char * to_string(RosEntityKind kind);
bool ros_entity_kind_from_string(std::string_view text, RosEntityKind & out_kind);

/// @brief One entity, identified the way the bridges want it.
struct RosEntityRef
{
  RosEntityKind kind = RosEntityKind::Topic;
  std::string name;       ///< Fully-qualified graph name ("/turtle1/cmd_vel").
  std::string type_name;  ///< "geometry_msgs/msg/Twist", "pkg/srv/X", "pkg/action/Y".

  bool empty() const { return name.empty(); }
  bool operator==(const RosEntityRef & other) const
  {
    return kind == other.kind && name == other.name && type_name == other.type_name;
  }
  bool operator!=(const RosEntityRef & other) const { return !(*this == other); }
};

/// @brief A stable panel/widget id for @p name.
///
/// Widget ids become DOM ids and wire identifiers, so `qar_ui_model` restricts
/// them to `[A-Za-z_][A-Za-z0-9_.-]*` — which a ROS name is not (it starts with
/// `/`). Every character outside the set becomes `_` and the kind is prefixed,
/// so `/turtle1/cmd_vel` is `topic._turtle1_cmd_vel` and a topic and a service
/// of the same name cannot collide. Truncated to fit the 128-byte id limit.
std::string make_panel_id(RosEntityKind kind, std::string_view name);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_ROS_ENTITY_HPP_
