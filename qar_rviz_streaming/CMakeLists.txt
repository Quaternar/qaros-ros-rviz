# ===========================================================================
# qar_rviz_streaming — RViz2 panel plugin (ament/colcon) for QarOS streaming.
# ===========================================================================
# Exposes an rviz_common::Panel through pluginlib that streams the live RViz 3D
# scene to the QarOS Visualizer as a stereo pair. See docs/rviz_plugin_architecture.md.
#
# Dependency direction:
#   qar_rviz_streaming (this) -> qar_qar_adapter -> qar_ogre_streaming -> Ogre / QarOS C API
# This package MAY use rviz, Qt and ament. The two upstream packages may NOT, and a
# test in their own repository enforces that.
#
# The upstream packages live in a SEPARATE repository (qar_ogre_adapter) and are
# consumed here with find_package() from a colcon workspace — see README.md.

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
find_package(pluginlib REQUIRED)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
# Geolocation: we subscribe to sensor_msgs/NavSatFix on RViz's own node and use the
# fix to georeference the QarOS app volume (QarGeoAnchorFrame, ECEF WGS84).
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)

# The reusable streaming library and the QarOS C API adapter, from the separate
# qar_ogre_adapter repository. Build that repository into the same colcon workspace
# (or install it and put its prefix on CMAKE_PREFIX_PATH) before building this one.
find_package(qar_ogre_streaming REQUIRED)
find_package(qar_qar_adapter REQUIRED)

# Enable Qt MOC only after the dependencies are resolved and before our target is
# defined, so that no upstream target gets scanned for Q_OBJECT types.
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# --- The RViz panel plugin (shared module loaded by pluginlib) -------------
add_library(qar_rviz_streaming SHARED
  src/qar_streaming_panel.cpp
  src/rviz_display_context_adapter.cpp
  src/rviz_render_thread_executor.cpp
  src/rviz_peer_panel_model.cpp
  src/qar_offscreen_capture.cpp
  # Listed so AUTOMOC processes the Q_OBJECT panel header.
  include/qar_rviz_streaming/qar_streaming_panel.hpp
)

target_include_directories(qar_rviz_streaming PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# Vendored stb_image_write.h (single-header PNG writer) for the debug capture path.
# PRIVATE: it is an implementation detail of the capture translation unit.
target_include_directories(qar_rviz_streaming PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)

# The reusable library uses `emit` as an ordinary method name
# (OgreQarStreamingManager::emit). Qt defines `emit` as a macro, so this plugin
# must disable Qt's lowercase keyword macros and use the Q_* forms instead
# (Q_OBJECT/Q_SLOTS/Q_SIGNALS/Q_EMIT), which our code and the rviz_common public
# headers already use.
target_compile_definitions(qar_rviz_streaming PRIVATE QT_NO_KEYWORDS)

target_link_libraries(qar_rviz_streaming
  qar_ogre_streaming::qar_ogre_streaming
  qar_qar_adapter::qar_qar_adapter
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
)

# rviz_common / rviz_rendering / pluginlib include dirs + link libs via ament.
ament_target_dependencies(qar_rviz_streaming
  rviz_common
  rviz_rendering
  pluginlib
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
ament_export_dependencies(
  rviz_common rviz_rendering rviz_ogre_vendor pluginlib rclcpp sensor_msgs
  qar_ogre_streaming qar_qar_adapter
)

ament_package()
