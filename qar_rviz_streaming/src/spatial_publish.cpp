// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/spatial_publish.hpp"

#include <cmath>
#include <vector>

namespace qar_rviz_streaming
{
namespace
{

using qar_ui_model::UiValue;

constexpr double kPi = 3.14159265358979323846;

UiValue make_header(const SpatialPublishConfig & config)
{
  UiValue stamp = UiValue::object();
  stamp.set("sec", UiValue(config.stamp.sec));
  stamp.set("nanosec", UiValue(static_cast<std::int64_t>(config.stamp.nanosec)));

  UiValue header = UiValue::object();
  header.set("stamp", stamp);
  header.set("frame_id", UiValue(config.frame_id));
  return header;
}

UiValue make_point(const QarVector3 & p)
{
  UiValue point = UiValue::object();
  point.set("x", UiValue(static_cast<double>(p.x)));
  point.set("y", UiValue(static_cast<double>(p.y)));
  point.set("z", UiValue(static_cast<double>(p.z)));
  return point;
}

UiValue make_quaternion(const QarQuaternion & q)
{
  UiValue orientation = UiValue::object();
  orientation.set("x", UiValue(static_cast<double>(q.x)));
  orientation.set("y", UiValue(static_cast<double>(q.y)));
  orientation.set("z", UiValue(static_cast<double>(q.z)));
  orientation.set("w", UiValue(static_cast<double>(q.w)));
  return orientation;
}

UiValue make_pose(const QarVector3 & position, const QarQuaternion & orientation)
{
  UiValue pose = UiValue::object();
  pose.set("position", make_point(position));
  pose.set("orientation", make_quaternion(orientation));
  return pose;
}

/// The 6x6 row-major covariance `PoseWithCovariance` carries, filled on the
/// diagonal only: x (index 0), y (7) and yaw (35). Everything else stays 0,
/// which is what RViz publishes and what AMCL expects.
UiValue make_covariance(double position_variance, double yaw_variance)
{
  UiValue covariance = UiValue::array();
  for (int i = 0; i < 36; ++i) {
    double value = 0.0;
    if (i == 0 || i == 7) {
      value = position_variance;
    } else if (i == 35) {
      value = yaw_variance;
    }
    covariance.push_back(UiValue(value));
  }
  return covariance;
}

SpatialPublishRequest fail(const SpatialPublishConfig & config, std::string error)
{
  SpatialPublishRequest request;
  request.topic = config.topic;
  request.type_name = config.type_name;
  request.error = std::move(error);
  return request;
}

/// Every publish shares these preconditions: a topic to publish on, a type to
/// build, and — for a stamped message — a frame the robot can resolve.
bool check_destination(const SpatialPublishConfig & config, std::string & out_error)
{
  if (config.topic.empty()) {
    out_error = "no topic configured for this spatial target";
    return false;
  }
  if (config.type_name.empty()) {
    out_error = "no message type configured for this spatial target";
    return false;
  }
  return true;
}

bool is_stamped(const std::string & type_name)
{
  return type_name.size() >= 7 && type_name.compare(type_name.size() - 7, 7, "Stamped") == 0;
}

bool needs_frame(const SpatialPublishConfig & config, std::string & out_error)
{
  if (is_stamped(config.type_name) && config.frame_id.empty()) {
    out_error = "a stamped message needs a frame id";
    return false;
  }
  return true;
}

SpatialPublishRequest succeed(const SpatialPublishConfig & config, UiValue message)
{
  SpatialPublishRequest request;
  request.ok = true;
  request.topic = config.topic;
  request.type_name = config.type_name;
  request.message = std::move(message);
  return request;
}

}  // namespace

const char * to_string(SpatialPublishTarget target)
{
  switch (target) {
    case SpatialPublishTarget::ClickedPoint: return "clicked point";
    case SpatialPublishTarget::GoalPose: return "navigation goal";
    case SpatialPublishTarget::InitialPose: return "initial pose";
    case SpatialPublishTarget::Area: return "selected area";
    case SpatialPublishTarget::Custom: return "custom";
  }
  return "unknown";
}

SpatialPublishConfig default_publish_config(SpatialPublishTarget target)
{
  SpatialPublishConfig config;
  config.target = target;
  switch (target) {
    case SpatialPublishTarget::ClickedPoint:
      config.topic = "/clicked_point";
      config.type_name = "geometry_msgs/msg/PointStamped";
      break;
    case SpatialPublishTarget::GoalPose:
      config.topic = "/goal_pose";
      config.type_name = "geometry_msgs/msg/PoseStamped";
      break;
    case SpatialPublishTarget::InitialPose:
      config.topic = "/initialpose";
      config.type_name = "geometry_msgs/msg/PoseWithCovarianceStamped";
      break;
    case SpatialPublishTarget::Area:
      config.topic = "/qar/selected_area";
      config.type_name = "geometry_msgs/msg/PolygonStamped";
      break;
    case SpatialPublishTarget::Custom:
      // Deliberately empty: a custom target is refused until the operator names
      // a topic and a type, rather than silently publishing somewhere.
      break;
  }
  return config;
}

SpatialPublishRequest make_point_publish(
  const SpatialPublishConfig & config, const qar_ar_core::SpatialPoint & point)
{
  std::string error;
  if (!check_destination(config, error) || !needs_frame(config, error)) {
    return fail(config, error);
  }
  if (!point.valid) {
    return fail(config, point.error.empty() ? "no point was picked" : point.error);
  }
  if (config.type_name == "geometry_msgs/msg/PointStamped") {
    UiValue message = UiValue::object();
    message.set("header", make_header(config));
    message.set("point", make_point(point.position));
    return succeed(config, std::move(message));
  }
  if (config.type_name == "geometry_msgs/msg/Point") {
    return succeed(config, make_point(point.position));
  }
  return fail(config, "a picked point cannot be published as " + config.type_name);
}

namespace
{

SpatialPublishRequest pose_message(
  const SpatialPublishConfig & config, const QarVector3 & position,
  const QarQuaternion & orientation)
{
  if (config.type_name == "geometry_msgs/msg/PoseStamped") {
    UiValue message = UiValue::object();
    message.set("header", make_header(config));
    message.set("pose", make_pose(position, orientation));
    return succeed(config, std::move(message));
  }
  if (config.type_name == "geometry_msgs/msg/PoseWithCovarianceStamped") {
    UiValue inner = UiValue::object();
    inner.set("pose", make_pose(position, orientation));
    inner.set("covariance", make_covariance(config.position_variance, config.yaw_variance));
    UiValue message = UiValue::object();
    message.set("header", make_header(config));
    message.set("pose", inner);
    return succeed(config, std::move(message));
  }
  if (config.type_name == "geometry_msgs/msg/Pose") {
    return succeed(config, make_pose(position, orientation));
  }
  return fail(config, "a picked pose cannot be published as " + config.type_name);
}

}  // namespace

SpatialPublishRequest make_pose_publish(
  const SpatialPublishConfig & config, const qar_ar_core::PosePickResult & pose)
{
  std::string error;
  if (!check_destination(config, error) || !needs_frame(config, error)) {
    return fail(config, error);
  }
  if (!pose.valid) {
    return fail(config, "no pose was picked");
  }
  return pose_message(config, pose.position, pose.orientation);
}

SpatialPublishRequest make_nav_goal_publish(
  const SpatialPublishConfig & config, const qar_ar_core::NavGoal & goal)
{
  std::string error;
  if (!check_destination(config, error)) {
    return fail(config, error);
  }
  if (!goal.valid) {
    return fail(config, goal.error.empty() ? "no navigation goal was picked" : goal.error);
  }
  // The goal carries the frame the operator chose when it was made; it wins over
  // the publisher's default, so a goal made in "odom" is not silently relabelled.
  SpatialPublishConfig resolved = config;
  if (!goal.frame_id.empty()) {
    resolved.frame_id = goal.frame_id;
  }
  if (!needs_frame(resolved, error)) {
    return fail(resolved, error);
  }
  return pose_message(resolved, goal.position, goal.orientation);
}

SpatialPublishRequest make_area_publish(
  const SpatialPublishConfig & config, const qar_ar_core::AreaPickResult & area)
{
  std::string error;
  if (!check_destination(config, error) || !needs_frame(config, error)) {
    return fail(config, error);
  }
  if (!area.valid) {
    return fail(config, area.error.empty() ? "no area was picked" : area.error);
  }

  std::vector<QarVector3> vertices = area.vertices;
  if (area.shape == qar_ar_core::AreaShape::Circle) {
    if (config.circle_segments < 3u) {
      return fail(config, "a circle needs at least three segments to become a polygon");
    }
    QarVector3 u{1.0f, 0.0f, 0.0f};
    QarVector3 v{0.0f, 1.0f, 0.0f};
    QarVector3 normal{0.0f, 0.0f, 1.0f};
    if (area.target == qar_ar_core::PickTargetKind::Plane ||
      area.target == qar_ar_core::PickTargetKind::GroundPlane)
    {
      // The rim is swept in the plane the circle was picked on, which for the
      // ground plane is the world XY plane.
      const QarVector3 radial{
        area.second.x - area.center.x, area.second.y - area.center.y,
        area.second.z - area.center.z};
      const float radial_length =
        std::sqrt(radial.x * radial.x + radial.y * radial.y + radial.z * radial.z);
      if (radial_length > 1e-6f) {
        u = QarVector3{
          radial.x / radial_length, radial.y / radial_length, radial.z / radial_length};
        v = QarVector3{
          normal.y * u.z - normal.z * u.y,
          normal.z * u.x - normal.x * u.z,
          normal.x * u.y - normal.y * u.x};
      }
    }
    vertices.clear();
    vertices.reserve(config.circle_segments);
    for (std::uint32_t i = 0; i < config.circle_segments; ++i) {
      const double angle =
        2.0 * kPi * static_cast<double>(i) / static_cast<double>(config.circle_segments);
      const float c = static_cast<float>(std::cos(angle)) * area.radius_m;
      const float s = static_cast<float>(std::sin(angle)) * area.radius_m;
      vertices.push_back(
        QarVector3{
          area.center.x + u.x * c + v.x * s,
          area.center.y + u.y * c + v.y * s,
          area.center.z + u.z * c + v.z * s});
    }
  }

  if (vertices.size() < 3u) {
    return fail(config, "a published region needs at least three points");
  }

  UiValue points = UiValue::array();
  for (const auto & vertex : vertices) {
    points.push_back(make_point(vertex));
  }
  UiValue polygon = UiValue::object();
  polygon.set("points", points);

  if (config.type_name == "geometry_msgs/msg/PolygonStamped") {
    UiValue message = UiValue::object();
    message.set("header", make_header(config));
    message.set("polygon", polygon);
    return succeed(config, std::move(message));
  }
  if (config.type_name == "geometry_msgs/msg/Polygon") {
    return succeed(config, std::move(polygon));
  }
  return fail(config, "a picked area cannot be published as " + config.type_name);
}

}  // namespace qar_rviz_streaming
