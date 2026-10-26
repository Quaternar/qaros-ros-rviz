#!/usr/bin/env python3
# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
r"""Publish one topic for each of ten semantic AR page templates of the plugin, plus a Trigger service.

For trying the AR pages that the plugin generates from the ROS graph, without a robot: one node,
`qar_template_publishers`, publishes plausible, slowly changing values for ten ROS message types to which the
plugin gives a dedicated page template, and serves a Trigger service for an eleventh. Each topic can then be
exposed as an AR tool (the panel's "Add from ROS graph..." dialog, with "Expose as a QAR AR tool" ticked) and
its page looked at. Topic, type and template:

    /qar_demo/battery       sensor_msgs/msg/BatteryState       battery
    /qar_demo/diagnostics   diagnostic_msgs/msg/DiagnosticArray diagnostics
    /qar_demo/odom          nav_msgs/msg/Odometry              odometry
    /qar_demo/path          nav_msgs/msg/Path                  path
    /qar_demo/joint_states  sensor_msgs/msg/JointState         joint-state
    /qar_demo/map           nav_msgs/msg/OccupancyGrid         occupancy-grid
    /qar_demo/scan          sensor_msgs/msg/LaserScan          laser-scan
    /qar_demo/imu           sensor_msgs/msg/Imu                imu
    /qar_demo/temperature   std_msgs/msg/Float64               scalar-status
    /qar_demo/pose          geometry_msgs/msg/PoseStamped      pose
    /qar_demo/trigger       std_srvs/srv/Trigger (service)     robot-service

The service answers success=True with the message "trigger <n>" and prints "trigger call <n>" for every call.
Every topic is published at --rate Hz (default 1.0). The program prints one line per topic at the start and ends
by itself after --seconds (default 1500): no process has to be stopped. The clean-up of the live
launcher's -Confirm stops only publish_test_markers.py, publish_test_tf.py, publish_test_robot.py
and publish_test_pointcloud.py, so it leaves this script running.

Usage:
    python scripts\publish_template_topics.py [--seconds 1500] [--rate 1.0]

Needs the ROS 2 Humble distribution environment (scripts\env_setup.ps1) and a ROS_DOMAIN_ID that matches the
RViz session. Exit status: 0 = ran to its end, 1 = ROS 2 packages missing, 2 = usage error.
"""
import argparse
import math
import sys
import time

DEMO = '/qar_demo'
TOPICS = (
    ('battery', 'sensor_msgs/msg/BatteryState'),
    ('diagnostics', 'diagnostic_msgs/msg/DiagnosticArray'),
    ('odom', 'nav_msgs/msg/Odometry'),
    ('path', 'nav_msgs/msg/Path'),
    ('joint_states', 'sensor_msgs/msg/JointState'),
    ('map', 'nav_msgs/msg/OccupancyGrid'),
    ('scan', 'sensor_msgs/msg/LaserScan'),
    ('imu', 'sensor_msgs/msg/Imu'),
    ('temperature', 'std_msgs/msg/Float64'),
    ('pose', 'geometry_msgs/msg/PoseStamped'),
)
SERVICE = ('trigger', 'std_srvs/srv/Trigger')


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        prog='publish_template_topics.py',
        description='Publish one topic for each of ten semantic AR page templates of the plugin and a Trigger '
                    'service; ends by itself after --seconds.')
    parser.add_argument('--seconds', type=float, default=1500.0, help='run time in seconds (default 1500)')
    parser.add_argument('--rate', type=float, default=1.0, help='publication rate of every topic in Hz (default 1.0)')
    args = parser.parse_args(argv)
    if args.seconds <= 0 or args.rate <= 0:
        parser.error('--seconds and --rate must be positive')
    return args


def build_messages(t, stamp):
    """The ten messages for time t (seconds since the start), stamped with `stamp` (a builtin_interfaces Time)."""
    from diagnostic_msgs.msg import DiagnosticArray, DiagnosticStatus, KeyValue
    from geometry_msgs.msg import Pose, PoseStamped
    from nav_msgs.msg import OccupancyGrid, Odometry, Path
    from sensor_msgs.msg import BatteryState, Imu, JointState, LaserScan
    from std_msgs.msg import Float64

    messages = {}

    battery = BatteryState()
    battery.header.stamp = stamp
    battery.header.frame_id = 'base_link'
    battery.voltage = 24.0
    battery.current = -1.5
    battery.percentage = 0.7 + 0.2 * math.sin(t / 60.0)           # 0.5 .. 0.9, as a fraction
    battery.charge = 14.0
    battery.capacity = 20.0
    battery.design_capacity = 20.0
    battery.power_supply_status = BatteryState.POWER_SUPPLY_STATUS_DISCHARGING
    battery.power_supply_health = BatteryState.POWER_SUPPLY_HEALTH_GOOD
    battery.power_supply_technology = BatteryState.POWER_SUPPLY_TECHNOLOGY_LION
    battery.present = True
    battery.cell_voltage = [4.0]                                 # one cell
    battery.location = 'qar_demo'
    battery.serial_number = 'qar-demo-0001'
    messages['battery'] = battery

    diagnostics = DiagnosticArray()
    diagnostics.header.stamp = stamp
    ok = DiagnosticStatus()
    ok.level = DiagnosticStatus.OK
    ok.name = 'qar_demo: motors'
    ok.message = 'all motors nominal'
    ok.hardware_id = 'qar-demo-motors'
    ok.values = [KeyValue(key='temperature', value='%.1f' % (41.0 + 2.0 * math.sin(t / 20.0))),
                 KeyValue(key='load', value='0.35')]
    warn = DiagnosticStatus()
    warn.level = DiagnosticStatus.WARN
    warn.name = 'qar_demo: battery'
    warn.message = 'charge below 75 percent'
    warn.hardware_id = 'qar-demo-battery'
    warn.values = [KeyValue(key='voltage', value='24.0'), KeyValue(key='cells', value='1')]
    diagnostics.status = [ok, warn]
    messages['diagnostics'] = diagnostics

    odom = Odometry()
    odom.header.stamp = stamp
    odom.header.frame_id = 'odom'
    odom.child_frame_id = 'base_link'
    odom.pose.pose.position.x = 0.05 * t % 5.0                   # slow forward motion, wraps every 100 s
    odom.pose.pose.orientation.w = 1.0
    odom.twist.twist.linear.x = 0.05
    messages['odom'] = odom

    path = Path()
    path.header.stamp = stamp
    path.header.frame_id = 'map'
    for i in range(20):
        pose = PoseStamped()
        pose.header = path.header
        pose.pose.position.x = 0.25 * i
        pose.pose.position.y = 0.5 * math.sin(0.4 * i)
        pose.pose.orientation.w = 1.0
        path.poses.append(pose)
    messages['path'] = path

    joints = JointState()
    joints.header.stamp = stamp
    joints.name = ['joint1', 'joint2', 'joint3']
    joints.position = [0.5 * math.sin(t / 5.0), 0.3 * math.cos(t / 7.0), 0.2 * math.sin(t / 3.0)]
    joints.velocity = [0.1, 0.05, 0.02]
    joints.effort = [1.0, 0.5, 0.2]
    messages['joint_states'] = joints

    grid = OccupancyGrid()
    grid.header.stamp = stamp
    grid.header.frame_id = 'map'
    grid.info.resolution = 0.1
    grid.info.width = 20
    grid.info.height = 20
    grid.info.origin = Pose()
    grid.info.origin.position.x = -1.0
    grid.info.origin.position.y = -1.0
    grid.info.origin.orientation.w = 1.0
    cells = []
    for row in range(20):
        for column in range(20):
            if row in (0, 19) or column in (0, 19):
                cells.append(100)                                # occupied border
            elif (row, column) == (10, 10):
                cells.append(-1)                                 # one unknown cell
            else:
                cells.append(0)
    grid.data = cells
    messages['map'] = grid

    scan = LaserScan()
    scan.header.stamp = stamp
    scan.header.frame_id = 'laser'
    scan.angle_min = -math.pi / 2.0
    scan.angle_max = math.pi / 2.0
    scan.angle_increment = math.pi / 179.0                       # 180 beams over half a turn
    scan.range_min = 0.1
    scan.range_max = 10.0
    scan.ranges = [3.0 + 0.5 * math.sin(0.1 * i + t / 4.0) for i in range(180)]
    messages['scan'] = scan

    imu = Imu()
    imu.header.stamp = stamp
    imu.header.frame_id = 'imu_link'
    imu.orientation.w = 1.0
    imu.angular_velocity.z = 0.02 * math.sin(t / 10.0)
    imu.linear_acceleration.z = 9.81
    messages['imu'] = imu

    temperature = Float64()
    temperature.data = 22.0 + 2.0 * math.sin(t / 30.0)
    messages['temperature'] = temperature

    pose = PoseStamped()
    pose.header.stamp = stamp
    pose.header.frame_id = 'map'
    pose.pose.position.x = 1.0 + 0.5 * math.sin(t / 10.0)
    pose.pose.position.y = 0.5 * math.cos(t / 10.0)
    pose.pose.orientation.w = 1.0
    messages['pose'] = pose
    return messages


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding='utf-8', errors='replace')
        except (AttributeError, ValueError):
            pass
    args = parse_args(argv)

    try:
        import rclpy
    except ModuleNotFoundError as exc:
        if exc.name != 'rclpy':
            raise
        sys.exit(
            'This Python has no ROS 2 packages (a plain virtual environment does not contain rclpy).\n'
            'Run this script in a shell that was prepared with scripts\\env_setup.ps1.')
    from diagnostic_msgs.msg import DiagnosticArray
    from geometry_msgs.msg import PoseStamped
    from nav_msgs.msg import OccupancyGrid, Odometry, Path
    from rclpy.node import Node
    from sensor_msgs.msg import BatteryState, Imu, JointState, LaserScan
    from std_msgs.msg import Float64
    from std_srvs.srv import Trigger

    classes = {'battery': BatteryState, 'diagnostics': DiagnosticArray, 'odom': Odometry, 'path': Path,
               'joint_states': JointState, 'map': OccupancyGrid, 'scan': LaserScan, 'imu': Imu,
               'temperature': Float64, 'pose': PoseStamped}

    class TemplatePublishers(Node):
        def __init__(self):
            super().__init__('qar_template_publishers')
            self.start = time.monotonic()
            self.calls = 0
            self.publishers_by_name = {name: self.create_publisher(classes[name], '%s/%s' % (DEMO, name), 10)
                                       for name, _ in TOPICS}
            self.service = self.create_service(Trigger, '%s/%s' % (DEMO, SERVICE[0]), self.on_trigger)
            self.timer = self.create_timer(1.0 / args.rate, self.publish_all)

        def publish_all(self):
            messages = build_messages(time.monotonic() - self.start, self.get_clock().now().to_msg())
            for name, message in messages.items():
                self.publishers_by_name[name].publish(message)

        def on_trigger(self, request, response):
            self.calls += 1
            print('trigger call %d' % self.calls, flush=True)
            response.success = True
            response.message = 'trigger %d' % self.calls
            return response

    rclpy.init()
    node = TemplatePublishers()
    for name, type_name in TOPICS:
        print('publishing %s/%s %s at %s Hz' % (DEMO, name, type_name, args.rate), flush=True)
    print('serving %s/%s %s' % (DEMO, SERVICE[0], SERVICE[1]), flush=True)
    print('ending by itself after %s s' % args.seconds, flush=True)
    deadline = time.monotonic() + args.seconds
    try:
        while rclpy.ok() and time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()
    print('done: %d trigger call(s) answered' % node.calls, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
