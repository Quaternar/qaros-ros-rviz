// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__IMAGE_TOPIC_TRANSPORT_HPP_
#define QAR_RVIZ_STREAMING__IMAGE_TOPIC_TRANSPORT_HPP_

/// @file image_topic_transport.hpp
/// @brief Which wire message a ROS image topic actually carries.
///
/// `RosImageFrameSource` (ros_image_frame_source.hpp) must subscribe with the
/// right message type before it can receive a single byte —
/// `sensor_msgs/msg/Image` and `sensor_msgs/msg/CompressedImage` are unrelated
/// types on the wire, so guessing wrong means silence, not a decode error.
/// This is the pure decision half: stdlib-only, so it is unit-tested without
/// rclcpp and compiled straight into `qar_rviz_streaming_tests` alongside the
/// rest of the plugin's pure logic, and reusable as-is by the Qt-dock variant of
/// the same video panel (variant b).

#include <string>
#include <vector>

namespace qar_rviz_streaming
{

/// @brief Which `sensor_msgs` image message a topic is subscribed as.
enum class ImageTransport
{
  Raw,        ///< `sensor_msgs/msg/Image`
  Compressed  ///< `sensor_msgs/msg/CompressedImage`
};

/// @brief Decide @p topic's transport from what the graph currently advertises.
///
/// 1. An advertised `sensor_msgs/msg/CompressedImage` wins outright.
/// 2. Otherwise an advertised `sensor_msgs/msg/Image` wins.
/// 3. Otherwise — nothing is advertised yet (the publisher has not started, or
///    the caller could not ask) — fall back to the ROS convention: a topic
///    name ending in `/compressed` is Compressed, anything else is Raw.
///
/// @param advertised_types Every type currently advertised on @p topic, as
///        `rclcpp::Node::get_topic_names_and_types()` reports it for that
///        topic name (e.g. `{"sensor_msgs/msg/Image"}`). Order does not
///        matter, only presence; may be empty.
ImageTransport choose_image_transport(
  const std::string & topic, const std::vector<std::string> & advertised_types);

/// @brief The wire type name @p transport subscribes as: `"sensor_msgs/msg/Image"`
/// or `"sensor_msgs/msg/CompressedImage"`.
const char * to_string(ImageTransport transport);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__IMAGE_TOPIC_TRANSPORT_HPP_
