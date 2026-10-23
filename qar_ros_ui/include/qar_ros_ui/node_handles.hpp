// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__NODE_HANDLES_HPP_
#define QAR_ROS_UI__NODE_HANDLES_HPP_

/// @file node_handles.hpp
/// @brief The node interfaces the bridges need, so a host can hand over an
/// `rclcpp::Node`, a `LifecycleNode`, or RViz's node without this library
/// naming any of them.

#include <memory>

#include "rcl/node.h"
#include "rclcpp/callback_group.hpp"
#include "rclcpp/node_interfaces/node_base_interface.hpp"
#include "rclcpp/node_interfaces/node_graph_interface.hpp"
#include "rclcpp/node_interfaces/node_logging_interface.hpp"
#include "rclcpp/node_interfaces/node_services_interface.hpp"
#include "rclcpp/node_interfaces/node_timers_interface.hpp"
#include "rclcpp/node_interfaces/node_topics_interface.hpp"
#include "rclcpp/node_interfaces/node_waitables_interface.hpp"

namespace qar_ros_ui
{

struct NodeHandles
{
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr base;
  rclcpp::node_interfaces::NodeGraphInterface::SharedPtr graph;
  rclcpp::node_interfaces::NodeTopicsInterface::SharedPtr topics;
  rclcpp::node_interfaces::NodeServicesInterface::SharedPtr services;
  rclcpp::node_interfaces::NodeWaitablesInterface::SharedPtr waitables;
  rclcpp::node_interfaces::NodeTimersInterface::SharedPtr timers;
  rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr logging;
  /// @brief The callback group of the node that every subscription, publisher event, client,
  /// action client and timer of the bridges joins; null = the node's default group.
  ///
  /// A callback group keeps a weak reference to each entity in it, and the bookkeeping of such a
  /// reference belongs to the code that made the entity: the templates of rclcpp, instantiated
  /// in the module the bridges are built into. A host that can be unloaded while the node lives
  /// on (a plugin on its host's node) passes a group of its own, created on that node, and
  /// releases it after the bridges: the group and its references then go while that code is
  /// still loaded. The bridges, and the topic subscriptions and publishers they hand out, keep a
  /// copy of these handles, and with it the group, until they are destroyed. The group must
  /// belong to the node of @ref base.
  rclcpp::CallbackGroup::SharedPtr callback_group;

  template<typename NodeT>
  static NodeHandles from(NodeT & node)
  {
    NodeHandles handles;
    handles.base = node.get_node_base_interface();
    handles.graph = node.get_node_graph_interface();
    handles.topics = node.get_node_topics_interface();
    handles.services = node.get_node_services_interface();
    handles.waitables = node.get_node_waitables_interface();
    handles.timers = node.get_node_timers_interface();
    handles.logging = node.get_node_logging_interface();
    return handles;
  }

  template<typename NodeT>
  static NodeHandles from(const std::shared_ptr<NodeT> & node)
  {
    return node ? from(*node) : NodeHandles{};
  }

  bool valid() const
  {
    return base && graph && topics && services && waitables && timers && logging;
  }

  rcl_node_t * rcl_node() const { return base ? base->get_rcl_node_handle() : nullptr; }
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__NODE_HANDLES_HPP_
