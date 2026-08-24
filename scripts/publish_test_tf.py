#!/usr/bin/env python3
# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
"""Broadcast a small static TF tree for the M4 test scenes (scene2: Grid + TF).

Publishes map -> base_link -> lidar (and a tilted camera_link) as static
transforms so the RViz TF display renders visible axes. Run alongside rviz2
(see scripts/capture_test_scenes.ps1). Requires the ROS 2 Humble distro
environment (scripts/env_setup.ps1) to be sourced.
"""
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import TransformStamped
from tf2_ros import StaticTransformBroadcaster


def _tf(parent, child, x, y, z, qx=0.0, qy=0.0, qz=0.0, qw=1.0, stamp=None):
    t = TransformStamped()
    if stamp is not None:
        t.header.stamp = stamp
    t.header.frame_id = parent
    t.child_frame_id = child
    t.transform.translation.x = float(x)
    t.transform.translation.y = float(y)
    t.transform.translation.z = float(z)
    t.transform.rotation.x = float(qx)
    t.transform.rotation.y = float(qy)
    t.transform.rotation.z = float(qz)
    t.transform.rotation.w = float(qw)
    return t


class TestTfPublisher(Node):
    def __init__(self):
        super().__init__("qar_test_tf_publisher")
        self.br = StaticTransformBroadcaster(self)
        stamp = self.get_clock().now().to_msg()
        self.br.sendTransform([
            _tf("map", "base_link", 0.0, 0.0, 0.5, stamp=stamp),
            _tf("base_link", "lidar", 0.0, 0.0, 1.0, stamp=stamp),
            # ~45 deg yaw camera
            _tf("base_link", "camera_link", 1.0, 0.0, 0.5,
                qz=0.3826834, qw=0.9238795, stamp=stamp),
        ])
        self.get_logger().info("Broadcast static TF: map->base_link->{lidar,camera_link}")


def main():
    rclpy.init()
    node = TestTfPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
