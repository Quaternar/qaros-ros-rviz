# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Unit tests for qar_capi_panels: the panel sender and its input source on the session owner,
# which runs on a fake of the QarOS function tables (fake_qar_api.cpp), and the conversions of
# seam_c.hpp. No test loads the QarOS library; no Qt, Ogre, ROS or RViz.
#
# GoogleTest comes from the top-level qar_require_googletest(); this directory just uses
# its targets.

add_executable(qar_capi_panels_tests
  # The QarOS function tables the sender runs on.
  fake_qar_api.cpp
  # The panel sender on the owner's thread, on the same fake, and the guard of its state.
  panel_sender_tests.cpp
  seam_c_tests.cpp
  # The panel path when QAROS stops answering: bounded calls, the release lane, the frame-call
  # breaker and the panel found again after a rejoin.
  panel_outage_tests.cpp
  # The frame calls on the frame worker: its thread, a call that blocks, the last one of the teardown
  # and the worker to destroy the video sender.
  panel_worker_tests.cpp
  # The input source on a fixed sequence of gesture events: the samples it produces.
  panel_input_source_golden_tests.cpp
  # A kept panel that is shown again is subscribed for AR input again.
  panel_revival_tests.cpp
  # The end of a session: a hide queued before a hanging sender destroy is done.
  panel_exit_order_tests.cpp
  # The helpers those tests share.
  panel_test_kit.hpp
  session_owner_harness.hpp
)

target_link_libraries(qar_capi_panels_tests PRIVATE
  qar_capi_panels
  qar_ogre_adapter::session
  GTest::gtest_main
)
target_compile_features(qar_capi_panels_tests PRIVATE cxx_std_17)
target_include_directories(qar_capi_panels_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

qar_apply_warning_flags(qar_capi_panels_tests)

include(GoogleTest)
gtest_discover_tests(qar_capi_panels_tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 60)
