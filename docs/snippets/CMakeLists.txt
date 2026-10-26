# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Compile-checked documentation snippets of qar_rviz_plugin. Every C++ and CMake block of the documentation
# is a region of a file in this folder (or of an example), so the code shown compiles and links.
# The target is built and never run; it registers no test.
file(GLOB _qar_snippet_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(qar_rviz_plugin_doc_snippets ${_qar_snippet_sources})
target_compile_features(qar_rviz_plugin_doc_snippets PRIVATE cxx_std_17)
# [qar-snippet:link]
target_link_libraries(qar_rviz_plugin_doc_snippets PRIVATE
  qar_rviz_plugin::qar_ui_model
  qar_rviz_plugin::qar_web_panels
  qar_rviz_plugin::qar_ros_ui
)
# [qar-snippet-end:link]
qar_apply_warning_flags(qar_rviz_plugin_doc_snippets)
