#!/usr/bin/env python3
# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
"""Publish a simple URDF + link TF for the test scenes (scene4: Grid + RobotModel).

Publishes /robot_description (std_msgs/String, transient-local so RViz gets it as a
late subscriber) and the matching static TF (map -> base_link -> pole -> head), so
the RViz RobotModel display renders without robot_state_publisher / joint_states.
Run alongside rviz2 with qar_rviz_streaming/config/test_scenes/scene4_grid_robot.rviz.
Requires the ROS 2 Humble distro environment (scripts/env_setup.ps1) to be sourced.
"""
import sys

try:
    import rclpy
except ModuleNotFoundError as exc:
    if exc.name != "rclpy":
        raise
    sys.exit(
        "This Python has no ROS 2 packages (a plain virtual environment does not contain rclpy).\n"
        "Run this script in a shell that was prepared with scripts\\env_setup.ps1.")
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSDurabilityPolicy, QoSHistoryPolicy
from std_msgs.msg import String
from geometry_msgs.msg import TransformStamped
from tf2_ros import StaticTransformBroadcaster

URDF = """<?xml version="1.0"?>
<robot name="qar_test_robot">
  <material name="grey"><color rgba="0.6 0.6 0.6 1.0"/></material>
  <material name="orange"><color rgba="1.0 0.5 0.1 1.0"/></material>
  <material name="blue"><color rgba="0.2 0.4 1.0 1.0"/></material>

  <link name="base_link">
    <visual>
      <origin xyz="0 0 0.1" rpy="0 0 0"/>
      <geometry><box size="1.0 1.0 0.2"/></geometry>
      <material name="grey"/>
    </visual>
  </link>

  <link name="pole">
    <visual>
      <origin xyz="0 0 0.5" rpy="0 0 0"/>
      <geometry><cylinder radius="0.1" length="1.0"/></geometry>
      <material name="orange"/>
    </visual>
  </link>

  <link name="head">
    <visual>
      <origin xyz="0 0 0" rpy="0 0 0"/>
      <geometry><sphere radius="0.3"/></geometry>
      <material name="blue"/>
    </visual>
  </link>

  <joint name="base_to_pole" type="fixed">
    <parent link="base_link"/><child link="pole"/>
    <origin xyz="0 0 0.2" rpy="0 0 0"/>
  </joint>
  <joint name="pole_to_head" type="fixed">
    <parent link="pole"/><child link="head"/>
    <origin xyz="0 0 1.0" rpy="0 0 0"/>
  </joint>
</robot>
"""


def _tf(parent, child, x, y, z, stamp):
    t = TransformStamped()
    t.header.stamp = stamp
    t.header.frame_id = parent
    t.child_frame_id = child
    t.transform.translation.x = float(x)
    t.transform.translation.y = float(y)
    t.transform.translation.z = float(z)
    t.transform.rotation.w = 1.0
    return t


class TestRobotPublisher(Node):
    def __init__(self):
        super().__init__("qar_test_robot_publisher")
        qos = QoSProfile(depth=1)
        qos.durability = QoSDurabilityPolicy.TRANSIENT_LOCAL
        qos.history = QoSHistoryPolicy.KEEP_LAST
        self.desc_pub = self.create_publisher(String, "/robot_description", qos)
        self.desc_pub.publish(String(data=URDF))

        self.br = StaticTransformBroadcaster(self)
        stamp = self.get_clock().now().to_msg()
        self.br.sendTransform([
            _tf("map", "base_link", 0.0, 0.0, 0.0, stamp),
            _tf("base_link", "pole", 0.0, 0.0, 0.2, stamp),
            _tf("pole", "head", 0.0, 0.0, 1.0, stamp),
        ])
        # Re-publish the description periodically in case RViz subscribes late.
        self.timer = self.create_timer(1.0, lambda: self.desc_pub.publish(String(data=URDF)))
        self.get_logger().info("Published /robot_description + static TF for qar_test_robot")


def main():
    rclpy.init()
    node = TestRobotPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
