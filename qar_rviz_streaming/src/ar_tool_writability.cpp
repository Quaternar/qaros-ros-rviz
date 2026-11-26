// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_tool_writability.hpp"

#include <algorithm>
#include <array>

namespace qar_rviz_streaming
{
namespace
{

constexpr const char * kReasonNumber = "a number needs a keyboard";
constexpr const char * kReasonText = "text needs a keyboard";
constexpr const char * kReasonPose = "coordinates need a keyboard; use Picking to place a pose";
constexpr const char * kReasonCovariance = "a covariance needs a keyboard";
constexpr const char * kReasonStatus = "a status topic: read-only";
constexpr const char * kReasonEmpty = "its page has a placeholder number field";
constexpr const char * kReasonUnknown = "the type is unknown";

constexpr std::array<const char *, 12> kNumberTypes = {
  "std_msgs/msg/Byte", "std_msgs/msg/Char", "std_msgs/msg/Int8", "std_msgs/msg/UInt8",
  "std_msgs/msg/Int16", "std_msgs/msg/UInt16", "std_msgs/msg/Int32", "std_msgs/msg/UInt32",
  "std_msgs/msg/Int64", "std_msgs/msg/UInt64", "std_msgs/msg/Float32", "std_msgs/msg/Float64"};

constexpr std::array<const char *, 11> kPoseTypes = {
  "geometry_msgs/msg/Pose", "geometry_msgs/msg/PoseStamped",
  "geometry_msgs/msg/PoseWithCovariance", "geometry_msgs/msg/PoseWithCovarianceStamped",
  "geometry_msgs/msg/Point", "geometry_msgs/msg/Point32", "geometry_msgs/msg/Quaternion",
  "geometry_msgs/msg/QuaternionStamped", "geometry_msgs/msg/PointStamped",
  "geometry_msgs/msg/Transform", "geometry_msgs/msg/TransformStamped"};

constexpr std::array<const char *, 2> kCovarianceTwistTypes = {
  "geometry_msgs/msg/TwistWithCovariance", "geometry_msgs/msg/TwistWithCovarianceStamped"};

template<typename Array>
bool listed(const Array & names, const std::string & type_name)
{
  return std::any_of(
    names.begin(), names.end(),
    [&type_name](const char * name) { return type_name == name; });
}

}  // namespace

const std::vector<KeyboardFreeTopicType> &
keyboard_free_topic_types()
{
  static const std::vector<KeyboardFreeTopicType> kTable = {
    {"std_msgs/msg/Bool", "one switch"},
    {"geometry_msgs/msg/Twist", "sliders and a stop button"},
    {"geometry_msgs/msg/TwistStamped", "sliders and a stop button"},
  };
  return kTable;
}

std::string
read_only_reason(const std::string & type_name)
{
  if (type_name.empty()) {
    return kReasonUnknown;
  }
  if (listed(kNumberTypes, type_name)) {
    return kReasonNumber;
  }
  if (type_name == "std_msgs/msg/String") {
    return kReasonText;
  }
  if (listed(kPoseTypes, type_name)) {
    return kReasonPose;
  }
  if (listed(kCovarianceTwistTypes, type_name)) {
    return kReasonCovariance;
  }
  if (type_name == "std_msgs/msg/Empty") {
    return kReasonEmpty;
  }
  return kReasonStatus;
}

ArToolWriteDefault
default_write_for(RosEntityKind kind, const std::string & type_name)
{
  ArToolWriteDefault result;
  switch (kind) {
    case RosEntityKind::Service:
      result.writable = true;
      result.reason = "its request form";
      return result;
    case RosEntityKind::Action:
      result.writable = true;
      result.reason = "its goal form";
      return result;
    case RosEntityKind::Topic:
      break;
  }
  for (const KeyboardFreeTopicType & row : keyboard_free_topic_types()) {
    if (type_name == row.type_name) {
      result.writable = true;
      result.reason = row.reason;
      return result;
    }
  }
  result.writable = false;
  result.reason = read_only_reason(type_name);
  return result;
}

}  // namespace qar_rviz_streaming
