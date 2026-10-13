# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_ros_ui — runtime ROS 2 graph discovery, type introspection and generic
# topic / service / action bridges that speak qar_ui_model values.
#
# Plain CMake (NOT ament). The ROS client libraries ARE the point of this
# library (rclcpp, rcl, rcl_action, rclcpp_action, rosidl_*); rviz_*, Qt and
# Ogre are forbidden so it stays usable from a web server, a CLI or a test.
# Enforced by the layering guard: scripts/check_layering.py, CTest test
# check_layering_qar_ros_ui. See docs/ARCHITECTURE.md ("ROS introspection
# design") and docs/DEVELOPING.md ("ROS 2 Humble introspection notes": the
# facts about this distro it is built on).
#
# The top-level CMakeLists locates the distro (QAR_ROS2_INSTALL_ROOT) and sets the
# two environment variables ament's configs need before it adds this directory, and
# the packages below are required — so a plain find_package is right here.

find_package(rclcpp REQUIRED)
find_package(rcl REQUIRED)
find_package(rcl_action REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(rcpputils REQUIRED)
find_package(rmw REQUIRED)
find_package(rosidl_runtime_c REQUIRED)
find_package(rosidl_runtime_cpp REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
find_package(action_msgs REQUIRED)

add_library(qar_ros_ui STATIC
  src/types.cpp
  src/type_support_registry.cpp
  src/introspection.cpp
  src/dynamic_message.cpp
  src/qos_selection.cpp
  src/graph_discovery.cpp
  src/dynamic_service_client.cpp
  src/dynamic_action_client.cpp
  src/topic_bridge.cpp
  src/service_bridge.cpp
  src/action_bridge.cpp
)
add_library(qar_rviz_plugin::qar_ros_ui ALIAS qar_ros_ui)

target_compile_features(qar_ros_ui PUBLIC cxx_std_17)

target_include_directories(qar_ros_ui PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# PUBLIC: the headers hand out rclcpp node interfaces, rmw QoS profiles and the
# introspection tables, so consumers need the same headers and import libs.
# action_msgs is the one interface package the action protocol fixes (cancel
# and status messages are its types, whatever the action).
target_link_libraries(qar_ros_ui PUBLIC
  qar_rviz_plugin::qar_ui_model
  rclcpp::rclcpp
  rclcpp_action::rclcpp_action
  rcl_action::rcl_action
  rcl::rcl
  rcpputils::rcpputils
  rmw::rmw
  rosidl_runtime_c::rosidl_runtime_c
  rosidl_runtime_cpp::rosidl_runtime_cpp
  rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
  action_msgs::action_msgs__rosidl_typesupport_cpp
)

# Static lib linked into a shared consumer (the ament RViz plugin, or the panel
# server) on non-Windows must be position-independent.
set_target_properties(qar_ros_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(COMMAND qar_apply_warning_flags)
  qar_apply_warning_flags(qar_ros_ui)
endif()
