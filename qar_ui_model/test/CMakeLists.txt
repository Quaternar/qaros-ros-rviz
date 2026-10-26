# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Unit tests for qar_ui_model.
#
# GoogleTest comes from the top-level qar_require_googletest(); this directory just uses
# its targets.
#
# Everything here is pure C++ over strings and value trees: no Qt application,
# no Ogre context, no ROS graph, no browser. The one thing that cannot run here
# is the embedded JavaScript; its C++ mirror (paths, patches, the envelope) is
# what these tests pin, and docs/WIRE_PROTOCOL.md is the shared contract.

add_executable(qar_ui_model_tests
  ui_value_tests.cpp
  json_tests.cpp
  text_safety_tests.cpp
  type_description_tests.cpp
  ui_model_tests.cpp
  ui_model_bindings_tests.cpp
  ui_protocol_tests.cpp
  ar_theme_tests.cpp
  html_renderer_tests.cpp
  page_metrics_tests.cpp
  scroll_paging_tests.cpp
  viewport_fit_tests.cpp
)

target_link_libraries(qar_ui_model_tests PRIVATE
  qar_rviz_plugin::qar_ui_model
  GTest::gtest_main
)

target_compile_features(qar_ui_model_tests PRIVATE cxx_std_17)

qar_apply_warning_flags(qar_ui_model_tests)

include(GoogleTest)
gtest_discover_tests(qar_ui_model_tests DISCOVERY_TIMEOUT 30)
