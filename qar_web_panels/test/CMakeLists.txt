# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# Unit tests for qar_web_panels.
#
# GoogleTest comes from the top-level qar_require_googletest(); this directory just uses
# its targets.
#
# These tests open REAL sockets on loopback: a raw TCP client for the HTTP
# side and QWebSocket for the wire protocol, all on the one event loop that
# test_main.cpp's QCoreApplication provides. No window, no platform plugin.
# What cannot run here is the page's JavaScript (there is no browser in this
# build); the served bytes are checked for well-formedness and agreement with
# the server instead.

find_package(Qt5 REQUIRED COMPONENTS Core Network WebSockets Test)

add_executable(qar_web_panels_tests
  test_main.cpp
  http_message_tests.cpp
  http_server_tests.cpp
  web_panel_server_tests.cpp
  url_panel_sizing_tests.cpp
)

target_link_libraries(qar_web_panels_tests PRIVATE
  qar_rviz_plugin::qar_web_panels
  Qt5::Test
  GTest::gtest
)

target_compile_features(qar_web_panels_tests PRIVATE cxx_std_17)

qar_apply_warning_flags(qar_web_panels_tests)

# Stage the Qt runtime next to the test executable: gtest_discover_tests runs
# the exe to enumerate tests, outside any per-test ENVIRONMENT, so the DLLs
# must simply be there (same reasoning as qar_qt_streaming/test).
foreach(_qar_qt_lib Qt5::Core Qt5::Network Qt5::WebSockets Qt5::Test)
  if(TARGET ${_qar_qt_lib})
    add_custom_command(TARGET qar_web_panels_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:${_qar_qt_lib}>" "$<TARGET_FILE_DIR:qar_web_panels_tests>"
      VERBATIM
    )
  endif()
endforeach()

include(GoogleTest)
gtest_discover_tests(qar_web_panels_tests DISCOVERY_TIMEOUT 60)
