# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# examples/ros_topic_panel_example - one ROS 2 topic exposed as an AR panel
# ===========================================================================
# A plain Qt Core program with an rclcpp node. qar_ros_ui subscribes and publishes without a generated message
# header, qar_web_panels serves the page, and with --sdk-bin the render and url components of the adapter open the
# page as a URL panel in a QarOS Visualizer. It links only the public targets of this repository; there is no
# RViz and no Qt Widgets. check_layering.py scans examples/** to keep it that way. Nothing links the QarOS SDK: the
# DLL is loaded at run time from --sdk-bin. The render component pulls in Ogre only because the session
# bootstrap lives in it; this program draws no 3D scene and includes no Ogre header.

add_executable(qar_ros_topic_panel_example main.cpp)

target_compile_features(qar_ros_topic_panel_example PRIVATE cxx_std_17)

# [qar-snippet:link]
target_link_libraries(qar_ros_topic_panel_example PRIVATE
  qar_rviz_plugin::qar_ros_ui
  qar_rviz_plugin::qar_web_panels
  qar_rviz_plugin::qar_ui_model
  qar_ogre_adapter::render
  qar_ogre_adapter::url
)
# [qar-snippet-end:link]

if(MSVC)
  target_compile_options(qar_ros_topic_panel_example PRIVATE /W4)
else()
  target_compile_options(qar_ros_topic_panel_example PRIVATE -Wall -Wextra)
endif()
