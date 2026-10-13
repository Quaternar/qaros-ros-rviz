# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
#
# ===========================================================================
# examples/ - the programs that use the libraries of this repository
# ===========================================================================
# web_panel_example serves one AR panel from a hand-built UI model over HTTP and WebSocket and opens it as
# a URL panel, so it needs qar_web_panels and the render and url components of the adapter.
# ros_topic_panel_example adds qar_ros_ui and an rclcpp node and exposes a topic in the same way. Neither
# uses Qt Widgets or RViz. The render component brings Ogre from the ROS 2 distribution, because the
# session bootstrap lives in it. Each example is added only when everything it links is built (the
# check_layering_examples CTest runs scripts/check_layering.py on examples/**).

if(TARGET qar_web_panels AND TARGET qar_ogre_adapter::render AND TARGET qar_ogre_adapter::url)
  add_subdirectory(web_panel_example)
endif()

if(TARGET qar_web_panels AND TARGET qar_ros_ui AND TARGET qar_ogre_adapter::render AND TARGET qar_ogre_adapter::url)
  add_subdirectory(ros_topic_panel_example)
endif()
