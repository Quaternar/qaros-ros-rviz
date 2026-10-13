// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__SPATIAL_PUBLISH_HPP_
#define QAR_RVIZ_STREAMING__SPATIAL_PUBLISH_HPP_

/// @file spatial_publish.hpp
/// @brief One spatial pick -> the ROS message it means. Pure, no rclcpp.
///
/// The counterpart of `ar_control_router.hpp` for picks: a pick made in the
/// headset (`qar_ar_core/spatial_pick.hpp`) has to leave as a real ROS message on
/// a real topic, and *which* message is a pure function of (the operator's target
/// choice, the declared ROS type, the pick). That function lives here, with no
/// Qt, no rclcpp and no session, so every convention a robot depends on — the
/// nav-goal quaternion, the `initialpose` covariance diagonal, the polygon
/// winding — is unit-tested rather than discovered on a robot.
///
/// The output is a `qar_ui_model::UiValue` shaped like the message's own IDL
/// fields, which is exactly what `qar_ros_ui::TopicPublisher::publish()` takes:
/// it converts, serializes and publishes generically, so no message header is
/// compiled in and a custom target needs no new code.
///
/// ## The conventional endpoints
///
/// | Target | Topic | Type |
/// |---|---|---|
/// | @ref SpatialPublishTarget::ClickedPoint | `/clicked_point` | `geometry_msgs/msg/PointStamped` |
/// | @ref SpatialPublishTarget::GoalPose     | `/goal_pose`     | `geometry_msgs/msg/PoseStamped` |
/// | @ref SpatialPublishTarget::InitialPose  | `/initialpose`   | `geometry_msgs/msg/PoseWithCovarianceStamped` |
/// | @ref SpatialPublishTarget::Area         | `/qar/selected_area` | `geometry_msgs/msg/PolygonStamped` |
/// | @ref SpatialPublishTarget::Custom       | operator-supplied | operator-supplied |
///
/// These are RViz's own defaults (Publish Point, 2D Goal Pose, 2D Pose Estimate),
/// so an operator's existing subscribers work unchanged — the AR headset simply
/// becomes another producer on the topics they already listen to.
///
/// The **message shape follows the declared type name**, not the target, so
/// @ref SpatialPublishTarget::Custom is not a special case in the code: point
/// it at `geometry_msgs/msg/PoseStamped` and it builds one.

#include <cstdint>
#include <string>

#include "qar_ar_core/spatial_pick.hpp"
#include "qar_ui_model/ui_value.hpp"

namespace qar_rviz_streaming
{

/// @brief Where a pick is published.
enum class SpatialPublishTarget : std::uint8_t
{
  ClickedPoint = 0,
  GoalPose,
  InitialPose,
  Area,
  Custom
};
const char * to_string(SpatialPublishTarget target);

/// @brief A ROS time, as the two fields `builtin_interfaces/msg/Time` carries.
///
/// Left at zero the message is stamped "time zero", which tf resolves as *the
/// latest available transform* — the right default for a pick made in a headset
/// whose clock is not the robot's. A host with a node hands its own `now()` in.
struct RosStamp
{
  std::int64_t sec = 0;
  std::uint32_t nanosec = 0;
};

struct SpatialPublishConfig
{
  SpatialPublishTarget target = SpatialPublishTarget::ClickedPoint;
  std::string topic;      ///< @ref default_publish_config fills the conventional one.
  std::string type_name;  ///< Chooses the message SHAPE; see the file comment.
  std::string frame_id = "map";
  RosStamp stamp;

  /// `initialpose` covariance diagonal. RViz's own 2D Pose Estimate tool
  /// defaults to a 0.5 m standard deviation in x and y and pi/12 in yaw; these
  /// are those numbers, squared, and AMCL reads them as the trust to place in
  /// the operator's guess.
  double position_variance = 0.25;
  double yaw_variance = 0.06853891945200942;

  /// A circle has no vertices, but `Polygon` only speaks vertices, so a circular
  /// region is sampled into this many segments. 32 keeps a 5 m radius within a
  /// centimetre of the true circle while staying small enough to publish at
  /// interaction rates.
  std::uint32_t circle_segments = 32;
};

/// @brief The conventional topic + type for a target (Custom leaves both empty).
SpatialPublishConfig default_publish_config(SpatialPublishTarget target);

/// @brief A ready-to-publish message, or the reason there isn't one.
struct SpatialPublishRequest
{
  bool ok = false;
  std::string error;
  std::string topic;
  std::string type_name;
  qar_ui_model::UiValue message;
};

/// @brief A picked point -> `PointStamped` (or bare `Point`).
SpatialPublishRequest make_point_publish(
  const SpatialPublishConfig & config, const qar_ar_core::SpatialPoint & point);

/// @brief A picked pose -> `PoseStamped`, `PoseWithCovarianceStamped` or `Pose`.
SpatialPublishRequest make_pose_publish(
  const SpatialPublishConfig & config, const qar_ar_core::PosePickResult & pose);

/// @brief A navigation goal -> the same pose shapes, already flattened and
/// grounded by `make_nav_goal()`.
SpatialPublishRequest make_nav_goal_publish(
  const SpatialPublishConfig & config, const qar_ar_core::NavGoal & goal);

/// @brief A picked region -> `PolygonStamped` (or bare `Polygon`). A rectangle
/// publishes its four corners, a polygon its vertices, and a circle
/// @ref SpatialPublishConfig::circle_segments points around its rim.
SpatialPublishRequest make_area_publish(
  const SpatialPublishConfig & config, const qar_ar_core::AreaPickResult & area);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__SPATIAL_PUBLISH_HPP_
