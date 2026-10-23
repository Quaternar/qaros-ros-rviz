# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# qar_rviz_streaming — RViz2 panel plugin (ament/colcon) for Qar Streaming.
# ===========================================================================
# This ament package packages the reusable, plain-CMake qar_ogre_streaming
# library (which stays free of rviz/Qt/ament) and exposes an rviz_common::Panel
# through pluginlib. See docs/ARCHITECTURE.md and docs/INSTALL.md.
#
# Dependency direction:
#   qar_rviz_streaming (this) -> qar_ogre_streaming -> Qar C API / Ogre
# This package MAY include rviz/Qt/ament; qar_ogre_streaming may NOT.

cmake_minimum_required(VERSION 3.8)
project(qar_rviz_streaming VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

# Match the ROS 2 Humble Windows binary distro (Release/RelWithDebInfo). A Debug
# plugin would mix CRT/Qt runtimes with the Release rviz2.exe and crash on load.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# --- Dependencies ----------------------------------------------------------
find_package(ament_cmake REQUIRED)
find_package(rviz_common REQUIRED)
find_package(rviz_rendering REQUIRED)
find_package(rviz_ogre_vendor REQUIRED)
# Floor plane: mirrors rviz_default_plugins/Grid, whose Display class
# (GridDisplay) is the only public handle onto the Grid's own Reference
# Frame/Plane/Offset properties — see src/grid_floor_plane.cpp.
find_package(rviz_default_plugins REQUIRED)
find_package(pluginlib REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
# Geolocation: we subscribe to sensor_msgs/NavSatFix on RViz's own node and use the
# fix to georeference the QarOS app volume (QarGeoAnchorFrame, ECEF WGS84).
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
# "Add topic": the RViz Display plugin manifests are discovered through the
# ament resource index (pluginlib's own ClassLoader header needs tinyxml2, whose
# headers this distro does not install).
find_package(ament_index_cpp REQUIRED)

# ---------------------------------------------------------------------------
# The libraries of the two upstream repositories: their installed CMake packages
# (scripts/build_plugin.ps1 passes -DQAR_PREFIX=<install prefix>) or, for development, their
# source checkouts (-DQAR_OGRE_ADAPTER_SOURCE_DIR, -DQAR_QT_STREAMING_SOURCE_DIR; see
# cmake/QarBuildSupport.cmake). The web libraries qar_ui_model, qar_ros_ui and qar_web_panels
# are part of this repository and are built from its tree. QAR_REPO_ROOT also locates
# third_party/stb below.
# ---------------------------------------------------------------------------
get_filename_component(QAR_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include("${QAR_REPO_ROOT}/cmake/QarBuildSupport.cmake")

qar_find_upstream(qar_ogre_adapter VERSION 0.1.0
  COMPONENTS qar_ar_core qar_ogre_streaming render panels url gestures)
qar_find_upstream(qar_qt_streaming VERSION 0.1.0)
add_subdirectory("${QAR_REPO_ROOT}/qar_ui_model" "${CMAKE_BINARY_DIR}/qar_ui_model")
add_subdirectory("${QAR_REPO_ROOT}/qar_ros_ui" "${CMAKE_BINARY_DIR}/qar_ros_ui")
find_package(Qt5 REQUIRED COMPONENTS Network WebSockets)
add_subdirectory("${QAR_REPO_ROOT}/qar_web_panels" "${CMAKE_BINARY_DIR}/qar_web_panels")

# Enable Qt MOC only AFTER the subdirectory (so the reusable library, which has
# no Q_OBJECT types, is not scanned) and BEFORE our target is defined.
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# --- The RViz panel plugin (shared module loaded by pluginlib) -------------
add_library(qar_rviz_streaming SHARED
  src/qar_streaming_panel.cpp
  src/rviz_display_context_adapter.cpp
  src/rviz_render_thread_executor.cpp
  src/rviz_peer_panel_model.cpp
  # Hidden Displays panel (QAR_PANEL_HIDDEN_DISPLAYS): makes a SECOND instance of RViz's
  # own Displays panel through the VisualizationFrame's panel factory, so it can be
  # streamed into AR without ever appearing on the desktop. Needs rviz_common and so only
  # builds here (see the QT_NO_KEYWORDS note below).
  src/hidden_rviz_panel.cpp
  src/qar_offscreen_capture.cpp
  # ROS topic browser (the two pure helpers are also unit-tested from the
  # repo-root CMake project — see qar_rviz_streaming/test).
  src/ros_topic_tree.cpp
  src/display_class_catalog.cpp
  src/add_topic_dialog.cpp
  # The ROS graph -> generated AR panel path. These are pure (standard
  # library + qar_ui_model, or QtCore) and are compiled into the plain-CMake
  # test project too — see qar_rviz_streaming/test.
  src/ros_graph_tree.cpp
  src/ar_ros_entity.cpp
  src/ar_panel_generator.cpp
  src/ar_control_router.cpp
  src/ar_exposure_registry.cpp
  src/ar_semantic_templates.cpp
  # Variant (a): "Open video in AR" on an Image/CompressedImage web tool opens a
  # new AR panel streaming that topic's video. image_topic_transport.cpp is pure
  # and also compiled into qar_rviz_streaming_tests (see qar_rviz_streaming/test);
  # ros_image_frame_source.cpp needs rclcpp + sensor_msgs + qar_qt_streaming, so it
  # only builds here.
  src/image_topic_transport.cpp
  src/ros_image_frame_source.cpp
  # New node + SingleThreadedExecutor, spun on its own thread, that
  # RosImageFrameSource subscribes on instead of RViz's own spin_some-on-the-
  # GUI-thread node (see video_source_executor.hpp). Needs rclcpp, so — like
  # ros_image_frame_source.cpp above — it only builds here.
  src/video_source_executor.cpp
  # Keeps Windows from taking RViz's timer resolution / CPU speed away while a
  # full-screen window (the QarOS Visualizer) covers it. Pure Win32 (no-op
  # elsewhere), also compiled into qar_rviz_streaming_tests.
  src/windows_scheduling.cpp
src/spatial_publish.cpp
src/spatial_interaction.cpp
  # Floor plane: the pure placement math plus the Ogre/
  # rviz_default_plugins-touching sync class that uses it (see both files'
  # headers). grid_floor_plane_geometry.cpp is ALSO compiled into
  # qar_rviz_streaming_tests (qar_rviz_streaming/test/CMakeLists.txt); keep
  # that no-Ogre/no-RViz boundary if you touch it.
  src/grid_floor_plane_geometry.cpp
  src/grid_floor_plane.cpp
  # The settings the panel keeps in the RViz configuration and the pure units of the AR control
  # panel (standard library, qar_ui_model and qar_ar_core only). They are also compiled into
  # qar_rviz_streaming_tests (qar_rviz_streaming/test).
  src/panel_settings.cpp
  src/ar_control_panel_router.cpp
  src/ar_control_panel_model.cpp
  src/ar_control_panel_state.cpp
  src/ar_control_panel_placement.cpp
  # The rule that keeps an AR panel above the ground (standard library and qar_ar_core only).
  src/panel_ground_rule.cpp
  # The tabbed layout of the panel: scroll pages, the elided status lines and the constant size
  # hints (Qt Widgets only, no RViz). Also compiled into qar_rviz_streaming_widget_tests.
  src/stable_panel_layout.cpp
  # Listed so AUTOMOC processes the Q_OBJECT headers.
  include/qar_rviz_streaming/qar_streaming_panel.hpp
  include/qar_rviz_streaming/add_topic_dialog.hpp
)

target_include_directories(qar_rviz_streaming PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# Vendored stb_image_write.h (single-header PNG writer) for the debug capture
# path. Private: it is an implementation detail of the plugin's capture TU.
target_include_directories(qar_rviz_streaming PRIVATE
  "${QAR_REPO_ROOT}/third_party/stb"
)

# The reusable library uses `emit` as an ordinary method name
# (OgreQarStreamingManager::emit). Qt defines `emit` as a macro, so this plugin
# TU must disable Qt's lowercase keyword macros and use the Q_* forms instead
# (Q_OBJECT/Q_SLOTS/Q_SIGNALS/Q_EMIT), which our code and the rviz_common public
# headers already use.
target_compile_definitions(qar_rviz_streaming PRIVATE QT_NO_KEYWORDS)

# ...except in ONE translation unit. hidden_rviz_panel.cpp needs
# rviz_common/visualization_frame.hpp, which includes rviz_rendering/render_window.hpp,
# and that installed header declares `public slots:` with the plain Qt keyword - which
# cannot compile under QT_NO_KEYWORDS (error C2146 at render_window.hpp). So this one
# source opts back out. The -U lands after the target's -D on the command line (the Ninja
# rule is `cl $DEFINES $INCLUDES $FLAGS`, and per-source options go in FLAGS) and cl takes
# the last word, with a harmless D9025 "overriding" note. The `emit` clash the define
# exists for cannot arise there: that file includes nothing from the reusable libraries.
set_source_files_properties(src/hidden_rviz_panel.cpp PROPERTIES
  COMPILE_OPTIONS "-UQT_NO_KEYWORDS")

target_link_libraries(qar_rviz_streaming
  qar_ogre_adapter::qar_ar_core              # Qt-free seams, pick model, frames, timing
  qar_ogre_adapter::qar_ogre_streaming
  qar_qt_streaming::qar_qt_streaming        # Qt widget -> QarOS GUI panel
  qar_rviz_plugin::qar_ui_model                # schema + widget tree + protocol
  qar_rviz_plugin::qar_ros_ui                    # graph, introspection, bridges
  qar_rviz_plugin::qar_web_panels            # HTTP/WebSocket panel delivery
  qar_ogre_adapter::render                  # QarOS session + 3D stereo streams
  qar_ogre_adapter::panels                  # Qt widget panels out, panel input in
  qar_ogre_adapter::url                     # browser-backed (web tool) panels
  qar_ogre_adapter::gestures                # app-volume (spatial) gestures
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
)

# windows_scheduling.cpp: timeBeginPeriod/timeEndPeriod live in winmm. (Plain
# signature, like every other target_link_libraries on this target — mixing in a
# keyword form would be an error.)
if(WIN32)
  target_link_libraries(qar_rviz_streaming winmm)
endif()

# rviz_common / rviz_rendering / pluginlib include dirs + link libs via ament.
ament_target_dependencies(qar_rviz_streaming
  rviz_common
  rviz_rendering
  rviz_default_plugins
  pluginlib
  ament_index_cpp
  rclcpp
  sensor_msgs
)

# Register the plugin description with the rviz_common pluginlib base package so
# RViz2 discovers the panel via the ament resource index.
pluginlib_export_plugin_description_file(rviz_common plugin_description.xml)

# --- Install ---------------------------------------------------------------
install(TARGETS qar_rviz_streaming
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY include/
  DESTINATION include
)
install(FILES plugin_description.xml
  DESTINATION share/${PROJECT_NAME}
)
install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)

ament_export_include_directories(include)
ament_export_dependencies(rviz_common rviz_rendering rviz_ogre_vendor rviz_default_plugins pluginlib)

ament_package()
