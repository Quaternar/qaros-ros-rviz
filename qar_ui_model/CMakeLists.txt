# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_ui_model — declarative AR UI model + self-contained HTML/JS renderer.
#
# Plain CMake (NOT ament). Pure C++17: no Qt, no ROS, no rviz, no Ogre, no
# vendored JSON library — nothing beyond the standard library. It is the
# foundation of the ROS-graph -> AR-UI generation: qar_ros_ui fills it from
# introspection, templates build it, and qar_web_panels serves it over
# HTTP/WebSocket; every one of them links this target. See docs/ARCHITECTURE.md
# and docs/WIRE_PROTOCOL.md.
#
# The boundary (rviz, Qt, ament, rclcpp all forbidden — the same rule as
# qar_ogre_streaming, minus Ogre, which it never needed) is enforced by the
# layering guard: scripts/check_layering.py, CTest test
# check_layering_qar_ui_model.

add_library(qar_ui_model STATIC
  src/ui_value.cpp
  src/unicode.cpp
  src/json.cpp
  src/html_escape.cpp
  src/type_description.cpp
  src/ui_model.cpp
  src/ui_protocol.cpp
  src/ar_theme.cpp
  src/page_metrics.cpp
  src/scroll_paging.cpp
  src/viewport_fit.cpp
  src/html_renderer.cpp
  src/html_runtime.cpp
)
add_library(qar_rviz_plugin::qar_ui_model ALIAS qar_ui_model)

target_compile_features(qar_ui_model PUBLIC cxx_std_17)

target_include_directories(qar_ui_model PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# Static lib linked into a shared consumer (the ament RViz plugin, or the panel
# server) on non-Windows must be position-independent.
set_target_properties(qar_ui_model PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(COMMAND qar_apply_warning_flags)
  qar_apply_warning_flags(qar_ui_model)
endif()
