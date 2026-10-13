# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# Unit tests for the RViz plugin's SDK-free, ROS-free helper logic.
#
# qar_rviz_streaming itself is an ament package that only builds under colcon
# (it needs rviz_common/rclcpp). Its genuinely pure pieces — the topic/graph
# namespace trees and the Display-class manifest parsing (QtCore alone), and the
# panel generator, control router, exposure registry and semantic templates
# (standard library + qar_ui_model alone) — are compiled directly into a test
# target in this plain-CMake build. That keeps every test in the repo behind one
# `ctest --preset` rather than splitting the suite across two build systems.
#
# That split is deliberate and load-bearing: the rules with the edge cases
# (schema -> UiModel, control event -> ROS action, what may be exposed) live in
# these pure units, and the Qt/RViz layer above them is a thin shell.

add_executable(qar_rviz_streaming_tests
  ros_topic_tree_tests.cpp
  display_class_catalog_tests.cpp
  ros_graph_tree_tests.cpp
  ar_panel_generator_tests.cpp
  ar_control_router_tests.cpp
  ar_exposure_registry_tests.cpp
  ar_semantic_templates_tests.cpp
  spatial_publish_tests.cpp
  spatial_interaction_tests.cpp
  grid_floor_plane_geometry_tests.cpp
  image_topic_transport_tests.cpp
  windows_scheduling_tests.cpp
  # The display name a new QarOS panel is created under (panel_display_names.hpp):
  # header-only, standard library only, so it needs no source file of its own.
  panel_display_names_tests.cpp
  # The run-time type check hidden_rviz_panel.cpp puts in front of RViz's panel factory
  # (rtti_guard.hpp, header-only, standard library). That file needs rviz_common and so
  # only builds under colcon; the check is tested here against stand-in classes whose
  # concrete types are defined in the SECOND file only, so the tests meet them as opaque
  # pointers the way the plugin meets RViz's PanelFactory.
  rtti_guard_tests.cpp
  rtti_guard_test_types.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ros_topic_tree.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/display_class_catalog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ros_graph_tree.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_ros_entity.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_panel_generator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_control_router.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_exposure_registry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_semantic_templates.cpp
  # Variant (a)'s pure transport-choice helper (image_topic_transport.hpp); the
  # rclcpp/Qt-needing RosImageFrameSource that uses it only builds under colcon
  # (qar_rviz_streaming/CMakeLists.txt).
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/image_topic_transport.cpp
  # Pure Win32 scheduling opt-out (no Qt, no ROS); a no-op off Windows.
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/windows_scheduling.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/spatial_publish.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/spatial_interaction.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/grid_floor_plane_geometry.cpp
)

target_include_directories(qar_rviz_streaming_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# qar_ar_core: the spatial pick model and frames (Qt-free, so this target keeps its
# stdlib + QtCore shape).
target_link_libraries(qar_rviz_streaming_tests PRIVATE
  qar_ogre_adapter::qar_ar_core
  qar_rviz_plugin::qar_ui_model
  Qt5::Core
  GTest::gtest_main
)

if(WIN32)
  target_link_libraries(qar_rviz_streaming_tests PRIVATE winmm)  # timeBeginPeriod
endif()

target_compile_features(qar_rviz_streaming_tests PRIVATE cxx_std_17)

qar_apply_warning_flags(qar_rviz_streaming_tests)

# QtCore only, so no platform plugin is needed and no QApplication is created —
# but Qt5Core.dll still has to be findable for gtest_discover_tests to run the exe.
if(TARGET Qt5::Core)
  add_custom_command(TARGET qar_rviz_streaming_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:Qt5::Core>" "$<TARGET_FILE_DIR:qar_rviz_streaming_tests>"
    VERBATIM
  )
endif()

include(GoogleTest)
gtest_discover_tests(qar_rviz_streaming_tests DISCOVERY_TIMEOUT 60)

# --- The semantic templates against the distro's own IDL --------------------
#
# The layouts above are checked against hand-written descriptions; this second
# executable re-checks them against the descriptions qar_ros_ui reads out of the
# installed interface packages' introspection typesupport. It is a SEPARATE
# target on purpose: qar_rviz_streaming_tests must stay stdlib-only so the
# generation rules keep building where ROS is not installed, and this one links
# rclcpp through qar_ros_ui. Skipped entirely when the distro was not found.
if(TARGET qar_rviz_plugin::qar_ros_ui)
  add_executable(qar_rviz_streaming_ros_tests
    ar_semantic_templates_ros_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_ros_entity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_panel_generator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ar_semantic_templates.cpp
  )

  target_include_directories(qar_rviz_streaming_ros_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
  )

  target_link_libraries(qar_rviz_streaming_ros_tests PRIVATE
    qar_rviz_plugin::qar_ros_ui
    qar_rviz_plugin::qar_ui_model
    GTest::gtest_main
  )

  target_compile_features(qar_rviz_streaming_ros_tests PRIVATE cxx_std_17)

  qar_apply_warning_flags(qar_rviz_streaming_ros_tests)

  # Same PATH dance as qar_ros_ui/test: the exe imports rclcpp.dll and the
  # introspection typesupport DLLs, so <distro>/bin has to be findable both when
  # ctest ENUMERATES the tests and when it runs them.
  set(_qar_sem_ros_bins "${QAR_ROS2_INSTALL_ROOT}/bin")
  file(GLOB _qar_sem_opt_bins LIST_DIRECTORIES true "${QAR_ROS2_INSTALL_ROOT}/opt/*/bin")
  foreach(_dir IN LISTS _qar_sem_opt_bins)
    if(IS_DIRECTORY "${_dir}")
      list(APPEND _qar_sem_ros_bins "${_dir}")
    endif()
  endforeach()
  if(WIN32)
    set(_qar_sem_path_sep ";")
  else()
    set(_qar_sem_path_sep ":")
  endif()
  string(REPLACE ";" "${_qar_sem_path_sep}" _qar_sem_path_prepend "${_qar_sem_ros_bins}")

  set(_qar_sem_env_script "${CMAKE_CURRENT_BINARY_DIR}/qar_rviz_streaming_ros_test_env.cmake")
  file(WRITE "${_qar_sem_env_script}"
    "# Generated by qar_rviz_streaming/test/CMakeLists.txt: ROS runtime DLLs.\n"
    "set(ENV{PATH} \"${_qar_sem_path_prepend}${_qar_sem_path_sep}\$ENV{PATH}\")\n"
  )
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_qar_sem_env_script}")

  file(TO_NATIVE_PATH "${QAR_ROS2_INSTALL_ROOT}/bin" _qar_sem_bin_native)

  gtest_discover_tests(qar_rviz_streaming_ros_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
    PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qar_sem_bin_native}"
      # The tests exercise the bridges, not the transport. Fast DDS's shared-memory transport is off for
      # these short-lived processes: participant teardown crashes intermittently and leaves stale segments.
      ENVIRONMENT "FASTDDS_BUILTIN_TRANSPORTS=UDPv4"
      TIMEOUT 120
      RESOURCE_LOCK qar_ros_dds
  )

  # --- VideoSourceExecutor (the video source's private rclcpp context) ---------
  #
  # Its own executable, with its own main(): the suite initialises rclcpp's host
  # context on a pid-derived DDS domain (like qar_ros_ui's tests) so the private
  # participant it creates never meets a concurrent RViz, and that main would
  # conflict with qar_rviz_streaming_ros_tests' GTest::gtest_main. Needs rclcpp
  # only, so it compiles video_source_executor.cpp directly rather than linking
  # the colcon-only plugin. The cross-context delivery test needs std_msgs and is
  # compiled out without it.
  find_package(std_msgs QUIET)

  add_executable(qar_rviz_streaming_video_source_tests
    video_source_executor_tests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/video_source_executor.cpp
  )

  target_include_directories(qar_rviz_streaming_video_source_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
  )

  target_link_libraries(qar_rviz_streaming_video_source_tests PRIVATE
    qar_rviz_plugin::qar_ros_ui
    GTest::gtest
  )

  if(std_msgs_FOUND)
    target_link_libraries(qar_rviz_streaming_video_source_tests PRIVATE
      std_msgs::std_msgs__rosidl_typesupport_cpp
    )
    target_compile_definitions(qar_rviz_streaming_video_source_tests PRIVATE
      QAR_VIDEO_SOURCE_LIVE_TESTS=1
    )
  else()
    message(WARNING
      "std_msgs not found; the VideoSourceExecutor cross-context delivery test is compiled out.")
  endif()

  target_compile_features(qar_rviz_streaming_video_source_tests PRIVATE cxx_std_17)

  qar_apply_warning_flags(qar_rviz_streaming_video_source_tests)

  gtest_discover_tests(qar_rviz_streaming_video_source_tests
    DISCOVERY_MODE PRE_TEST
    DISCOVERY_TIMEOUT 120
    PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qar_sem_bin_native}"
      # The tests exercise the bridges, not the transport. Fast DDS's shared-memory transport is off for
      # these short-lived processes: participant teardown crashes intermittently and leaves stale segments.
      ENVIRONMENT "FASTDDS_BUILTIN_TRANSPORTS=UDPv4"
      TIMEOUT 120
      RESOURCE_LOCK qar_ros_dds
  )

  # --- RosImageFrameSource's per-frame timing (rx_<topic>.csv) -----------------
  #
  # The one part of the stage-timing instrument that needs ROS: the executor callback and the
  # decode worker stamp each message. Its own executable (own main(), host context on a
  # pid-derived DDS domain, exactly like the video-source suite above), compiling
  # ros_image_frame_source.cpp directly rather than linking the colcon-only plugin, and driven
  # by a real sensor_msgs/Image publisher in another context. Skipped when sensor_msgs or the Qt
  # library is not available.
  find_package(sensor_msgs QUIET)
  if(sensor_msgs_FOUND AND TARGET qar_qt_streaming::qar_qt_streaming)
    add_executable(qar_rviz_streaming_image_source_tests
      ros_image_frame_source_timing_tests.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/../src/ros_image_frame_source.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/../src/video_source_executor.cpp
    )

    target_include_directories(qar_rviz_streaming_image_source_tests PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

    target_link_libraries(qar_rviz_streaming_image_source_tests PRIVATE
      qar_ogre_adapter::qar_ar_core
      qar_rviz_plugin::qar_ros_ui
      qar_qt_streaming::qar_qt_streaming
      sensor_msgs::sensor_msgs__rosidl_typesupport_cpp
      GTest::gtest
    )

    target_compile_features(qar_rviz_streaming_image_source_tests PRIVATE cxx_std_17)

    qar_apply_warning_flags(qar_rviz_streaming_image_source_tests)

    # QImage needs the Qt runtime next to the exe (gtest_discover_tests runs it to enumerate,
    # before any per-test ENVIRONMENT could add Qt to PATH).
    foreach(_qar_img_qt_lib Qt5::Core Qt5::Gui)
      if(TARGET ${_qar_img_qt_lib})
        add_custom_command(TARGET qar_rviz_streaming_image_source_tests POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${_qar_img_qt_lib}>"
            "$<TARGET_FILE_DIR:qar_rviz_streaming_image_source_tests>"
          VERBATIM)
      endif()
    endforeach()

    gtest_discover_tests(qar_rviz_streaming_image_source_tests
      DISCOVERY_MODE PRE_TEST
      DISCOVERY_TIMEOUT 120
      PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${_qar_sem_bin_native}"
        # The tests exercise the bridges, not the transport. Fast DDS's shared-memory transport is off for
        # these short-lived processes: participant teardown crashes intermittently and leaves stale segments.
        ENVIRONMENT "FASTDDS_BUILTIN_TRANSPORTS=UDPv4"
        TIMEOUT 180
        RESOURCE_LOCK qar_ros_dds
    )
  else()
    message(STATUS
      "sensor_msgs or qar_qt_streaming not found; the RosImageFrameSource timing test is skipped.")
  endif()
else()
  message(STATUS
    "qar_ros_ui not built; the semantic templates are not cross-checked against the distro's IDL.")
endif()
