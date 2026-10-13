# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# qar_rviz_plugin: the RViz2 plugin that streams RViz into QarOS, and the libraries of its web AR panels -
# qar_ui_model (the model and the HTML renderer), qar_web_panels (the HTTP and WebSocket server that QarOS
# opens as a URL panel) and qar_ros_ui (ROS 2 graph discovery and the topic, service and action bridges) -
# with the examples web_panel_example and ros_topic_panel_example. The plugin is the ament package
# qar_rviz_streaming, which scripts/build_plugin.ps1 builds with colcon against the installed packages of
# qar_ogre_adapter and qar_qt_streaming; it compiles the three libraries from this tree itself. This
# project is the plain-CMake side of the repository: it builds the three libraries, the examples and the
# parts of the plugin that need neither RViz nor Ogre, with their unit tests and the layering guard tests,
# against those installed packages. It installs nothing; colcon installs the plugin.
cmake_minimum_required(VERSION 3.22)
project(qar_rviz_plugin VERSION 0.1.0
  DESCRIPTION "Web AR panel libraries, their examples and the unit tests of the RViz2 plugin; the plugin is the ament package qar_rviz_streaming, built by scripts/build_plugin.ps1"
  LANGUAGES CXX)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/QarBuildSupport.cmake")

option(QAR_BUILD_TESTS "Build the unit tests and the layering guard tests" ON)
option(QAR_BUILD_EXAMPLES "Build the examples under examples/" ON)
option(QAR_BUILD_DOC_SNIPPETS "Build the compile-checked documentation snippets" ON)

qar_set_default_build_type()
qar_use_ros2_distro()
qar_use_ament_environment()
qar_use_qt5(COMPONENTS Core Gui Widgets Network WebSockets)

set(_qar_upstream_components qar_ar_core)
if(QAR_BUILD_EXAMPLES)
  list(APPEND _qar_upstream_components render url)
endif()
qar_find_upstream(qar_ogre_adapter VERSION 0.1.0 COMPONENTS ${_qar_upstream_components})
qar_find_upstream(qar_qt_streaming VERSION 0.1.0)

add_subdirectory(qar_ui_model)
add_subdirectory(qar_ros_ui)
add_subdirectory(qar_web_panels)
if(QAR_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
# The documentation snippets are built when the documentation folder is present.
if(QAR_BUILD_DOC_SNIPPETS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/snippets/CMakeLists.txt")
  add_subdirectory(docs/snippets)
endif()

if(QAR_BUILD_TESTS)
  enable_testing()
  qar_require_googletest()
  add_subdirectory(qar_ui_model/test)
  add_subdirectory(qar_ros_ui/test)
  add_subdirectory(qar_web_panels/test)
  add_subdirectory(qar_rviz_streaming/test)
  qar_add_layering_guard_tests(MODULES qar_ui_model qar_web_panels qar_ros_ui qar_rviz_streaming examples)
endif()
