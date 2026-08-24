#!/usr/bin/env python3
# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
"""Publish a synthetic PointCloud2 for the M4 test scenes (scene5: Grid + PointCloud2).

Publishes an XYZRGB point-cloud sine-wave surface in the fixed frame ("map") on
/qar_test_points so the RViz PointCloud2 display renders it without external data.
Run alongside rviz2 (see scripts/capture_test_scenes.ps1). Requires the ROS 2
Humble distro environment (scripts/env_setup.ps1) to be sourced.
"""
import math
import struct

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import PointCloud2, PointField
from std_msgs.msg import Header


def make_cloud(frame_id, stamp):
    fields = [
        PointField(name="x", offset=0, datatype=PointField.FLOAT32, count=1),
        PointField(name="y", offset=4, datatype=PointField.FLOAT32, count=1),
        PointField(name="z", offset=8, datatype=PointField.FLOAT32, count=1),
        PointField(name="rgb", offset=12, datatype=PointField.FLOAT32, count=1),
    ]
    n = 60
    data = bytearray()
    count = 0
    for i in range(n):
        for j in range(n):
            x = (i - n / 2) * 0.15
            y = (j - n / 2) * 0.15
            z = 1.0 + 0.6 * math.sin(0.6 * x) * math.cos(0.6 * y)
            r = int(128 + 127 * math.sin(0.6 * x))
            g = int(128 + 127 * math.cos(0.6 * y))
            b = int(255 - r)
            rgb = struct.unpack("f", struct.pack("I", (r << 16) | (g << 8) | b))[0]
            data += struct.pack("ffff", x, y, z, rgb)
            count += 1

    msg = PointCloud2()
    msg.header = Header()
    msg.header.frame_id = frame_id
    msg.header.stamp = stamp
    msg.height = 1
    msg.width = count
    msg.fields = fields
    msg.is_bigendian = False
    msg.point_step = 16
    msg.row_step = 16 * count
    msg.is_dense = True
    msg.data = bytes(data)
    return msg


class TestCloudPublisher(Node):
    def __init__(self):
        super().__init__("qar_test_pointcloud_publisher")
        self.pub = self.create_publisher(PointCloud2, "/qar_test_points", 10)
        self.frame = self.declare_parameter("frame_id", "map").value
        self.timer = self.create_timer(0.25, self.publish)
        self.get_logger().info("Publishing synthetic PointCloud2 on /qar_test_points")

    def publish(self):
        self.pub.publish(make_cloud(self.frame, self.get_clock().now().to_msg()))


def main():
    rclpy.init()
    node = TestCloudPublisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
