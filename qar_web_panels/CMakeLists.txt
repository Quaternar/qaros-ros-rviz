# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
#
# qar_web_panels — HTTP + WebSocket delivery of generated AR panels.
#
# Plain CMake (NOT ament). Serves qar_ui_model pages to QarOS URL panels and
# speaks the server half of the qar-ui/1 wire protocol. Reusable by ANY Qt
# application, so it has zero dependency on rviz_common, rviz_rendering, ament
# or rclcpp — and none on Ogre or Qt Widgets either: Qt Core/Network/WebSockets
# is all it needs. See docs/ARCHITECTURE.md.
#
# The boundary (Qt allowed, everything else forbidden) is enforced by the layering
# guard: scripts/check_layering.py, CTest test check_layering_qar_web_panels.

find_package(Qt5 REQUIRED COMPONENTS Core Network WebSockets)

# Headers live under include/, not next to the sources, so AUTOMOC only sees
# the Q_OBJECT classes because the headers are listed as sources here.
set(CMAKE_AUTOMOC ON)

add_library(qar_web_panels STATIC
  src/http_message.cpp
  src/panel_token.cpp
  src/url_panel_sizing.cpp
  src/http_server.cpp
  src/panel_session.cpp
  src/web_panel_server.cpp
  include/qar_web_panels/http_server.hpp
  include/qar_web_panels/panel_session.hpp
  include/qar_web_panels/web_panel_server.hpp
)
add_library(qar_rviz_plugin::qar_web_panels ALIAS qar_web_panels)

target_compile_features(qar_web_panels PUBLIC cxx_std_17)

target_include_directories(qar_web_panels PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include>"
)

# qar_ui_model is PUBLIC: the session API takes UiModel / UiValue / PatchEntry.
# qar_ar_core is PUBLIC: url_panel_sizing.hpp returns the QarOS C type
# QarGuiPanelSize (through qar_ar_core's wrapper of the vendored header).
# Qt is PUBLIC because the public headers derive from QObject.
target_link_libraries(qar_web_panels PUBLIC
  qar_rviz_plugin::qar_ui_model
  qar_ogre_adapter::qar_ar_core
  Qt5::Core
  Qt5::Network
  Qt5::WebSockets
)

set_target_properties(qar_web_panels PROPERTIES POSITION_INDEPENDENT_CODE ON)

qar_apply_warning_flags(qar_web_panels)
