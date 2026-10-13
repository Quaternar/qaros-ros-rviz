#!/usr/bin/env python3
# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
"""Publish a small MarkerArray for the test scenes (scene3: Grid + Marker).

Publishes brightly-coloured primitive markers in the fixed frame ("map") on
/qar_test_markers so the RViz Marker display renders them without needing TF.
Run alongside rviz2 with qar_rviz_streaming/config/test_scenes/scene3_grid_marker.rviz.
Requires the ROS 2 Humble distro environment (scripts/env_setup.ps1) to be sourced.
"""
import math
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
from geometry_msgs.msg import Point
from std_msgs.msg import ColorRGBA
from visualization_msgs.msg import Marker, MarkerArray


def _color(r, g, b, a=1.0):
    c = ColorRGBA()
    c.r, c.g, c.b, c.a = float(r), float(g), float(b), float(a)
    return c


class TestMarkerPublisher(Node):
    def __init__(self):
        super().__init__("qar_test_marker_publisher")
        self.pub = self.create_publisher(MarkerArray, "/qar_test_markers", 10)
        self.frame = self.declare_parameter("frame_id", "map").value
        self.timer = self.create_timer(0.2, self.publish)
        self.get_logger().info("Publishing test markers on /qar_test_markers")

    def _base(self, mid, mtype):
        m = Marker()
        m.header.frame_id = self.frame
        m.header.stamp = self.get_clock().now().to_msg()
        m.ns = "qar_test"
        m.id = mid
        m.type = mtype
        m.action = Marker.ADD
        m.pose.orientation.w = 1.0
        return m

    def publish(self):
        arr = MarkerArray()

        cube = self._base(0, Marker.CUBE)
        cube.pose.position.x, cube.pose.position.y, cube.pose.position.z = 1.5, 1.5, 0.5
        cube.scale.x = cube.scale.y = cube.scale.z = 1.0
        cube.color = _color(1.0, 0.1, 0.1)  # red
        arr.markers.append(cube)

        sphere = self._base(1, Marker.SPHERE)
        sphere.pose.position.x, sphere.pose.position.y, sphere.pose.position.z = -1.5, 1.5, 0.75
        sphere.scale.x = sphere.scale.y = sphere.scale.z = 1.5
        sphere.color = _color(0.1, 1.0, 0.1)  # green
        arr.markers.append(sphere)

        cyl = self._base(2, Marker.CYLINDER)
        cyl.pose.position.x, cyl.pose.position.y, cyl.pose.position.z = -1.5, -1.5, 1.0
        cyl.scale.x = cyl.scale.y = 0.8
        cyl.scale.z = 2.0
        cyl.color = _color(0.2, 0.4, 1.0)  # blue
        arr.markers.append(cyl)

        line = self._base(3, Marker.LINE_STRIP)
        line.scale.x = 0.08
        line.color = _color(1.0, 1.0, 0.1)  # yellow
        for i in range(0, 37):
            ang = math.radians(i * 10)
            p = Point()
            p.x, p.y, p.z = 2.5 * math.cos(ang), 2.5 * math.sin(ang), 2.0
            line.points.append(p)
        arr.markers.append(line)

        self.pub.publish(arr)


def main():
    rclpy.init()
    node = TestMarkerPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
