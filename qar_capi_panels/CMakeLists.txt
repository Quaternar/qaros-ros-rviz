# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# qar_capi_panels - the QarOS C API behind the panel interfaces of qar_qt_streaming.
#
# Plain CMake. It implements the sender, the sender factory and the input source of
# qar_qt_streaming's seam (qar_qt_streaming::seam: plain C++ values and interfaces) on the session
# owner and the C API of qar_ogre_adapter, and converts the values of the seam to and from the
# QarOS C types (seam_c.hpp). It needs neither Qt nor ROS nor Ogre nor RViz, and no import library
# of the QarOS SDK: the C API is reached through the loader of qar_ogre_adapter::capi.
#
# The boundary is enforced by the layering guard: scripts/check_layering.py, CTest test
# check_layering_qar_capi_panels.

add_library(qar_capi_panels STATIC
  src/qar_capi_panel_sender_adapter.cpp
  src/qar_capi_panel_input_source.cpp
)
add_library(qar_rviz_plugin::qar_capi_panels ALIAS qar_capi_panels)

target_compile_features(qar_capi_panels PUBLIC cxx_std_17)

target_include_directories(qar_capi_panels PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# capi and qar_ar_core are PUBLIC: the public headers use the session owner interface and the
# QarOS C types. The seam is PUBLIC: the classes derive from its interfaces.
target_link_libraries(qar_capi_panels PUBLIC
  qar_ogre_adapter::capi
  qar_ogre_adapter::qar_ar_core
  qar_qt_streaming::seam
)

# A static library linked into a shared consumer (the RViz plugin) must be position-independent.
set_target_properties(qar_capi_panels PROPERTIES POSITION_INDEPENDENT_CODE ON)

qar_apply_warning_flags(qar_capi_panels)
