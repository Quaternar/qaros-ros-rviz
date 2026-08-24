# Geolocation: georeferencing the streamed RViz scene

RViz streams a scene whose coordinates are local — metres from some `map` or
`base_link` origin. QarOS can place that scene at a real position on Earth, so that
an AR client standing in the right field sees the robot's data where the robot
actually is.

This page covers the **panel side**: where the position comes from in ROS, how it
reaches QarOS, and how to configure it. The geodetic maths and the QarOS API it
drives live in the `qar_ogre_adapter` repository (`docs/geolocation.md` there).

## Where geolocation comes from in ROS

Two sources, both standard:

1. **`sensor_msgs/NavSatFix`** — latitude, longitude, altitude in WGS84. This is
   what any GPS/GNSS driver publishes. **This is the path that is wired.**
2. **A TF `earth` → `map` transform.** [REP-105][rep105] defines `earth` as the
   ECEF origin, so such a transform *is* a QarOS geo-anchor frame already, with no
   geodetic conversion needed. In practice most stacks never publish `earth`; it
   appears when something like `robot_localization`'s `navsat_transform_node` is
   georeferencing the map.

The TF route is **not** read from RViz's own `FrameManager`, even though it would
need no conversion. `FrameManager::getTransform()` returns `Ogre::Vector3` —
32-bit floats — relative to whatever the user currently has selected as the fixed
frame. Float32 has ~7 significant decimal digits, so across a 6.4 × 10⁶ m ECEF
radius it quantises to roughly half a metre, and the value would be relative to an
arbitrary frame. `QarCApiSenderFactory::set_geo_anchor_ecef()` is the entry point
for a proper double-precision `tf2` lookup if that route is ever wanted.

## How the panel uses it

```
NavSatFix callback (RViz executor thread)  ->  store lat/lon/alt only
render tick (Qt thread)                    ->  applyPendingGeoFix()
                                                 -> set_geo_anchor_wgs84()
                                                      -> qar_app_volumes_change_app_world_anchor()
```

The subscription callback never calls into QarOS. It stores the fix under a mutex
and the render tick applies it, so every QarOS call stays on the Qt thread — the
SDK is [designed for single-threaded use per runtime][conv], with callbacks as the
only cross-thread surface.

Re-anchoring is throttled to 2 m of movement. A 10 Hz receiver would otherwise
drive one SDK call per message for what is usually sub-metre jitter.

The QoS is `rclcpp::SensorDataQoS()`, i.e. BEST_EFFORT. **GPS publishers are
conventionally BEST_EFFORT, and a RELIABLE subscriber receives nothing at all from
such a publisher** — silently, with no error anywhere. That is the same
incompatibility that silently emptied the rosbag point clouds; see
[`rosbag_metadata.md`](rosbag_metadata.md).

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `QAR_PANEL_GPS_TOPIC` | `/gps/fix` | `sensor_msgs/NavSatFix` topic to subscribe to. |
| `QAR_PANEL_GEO` | on | Set to `off` to disable the whole path. |
| `QAR_PANEL_APP_VOLUME` | on | `off` removes the app volume — and with it any possibility of an anchor, because the anchor is a property of the volume. |

The subscription is armed unconditionally at panel init and simply never fires when
the topic is absent, so there is nothing to switch on for a GPS-equipped robot.

Success looks like this in the panel log (and in `rviz_live_*.err.log`):

```
GEO: listening for NavSatFix on '/gps/fix' (QAR_PANEL_GPS_TOPIC; QAR_PANEL_GEO=off disables).
GEO: app volume anchored at lat=50.0755000 lon=14.4378000 alt=200.00 m (ECEF WGS84, ENU axes).
```

A failure is logged once, not per fix, and streaming continues un-georeferenced.

## Two things that are easy to get wrong

- **`NavSatFix` altitude is height above the WGS84 ellipsoid, not above mean sea
  level.** The two differ by tens of metres in most of the world.
- **A receiver with no lock reports `NaN`** for latitude, longitude and altitude.
  NaN passes a naive numeric range check, so it is rejected explicitly rather than
  being allowed to produce a NaN anchor.

## Testing

There is no automated test of this path at the plugin level — it needs a live QarOS
session, a GPS publisher and a running RViz. See [`testing.md`](testing.md) for what
that means and why. The geodetic conversion underneath it *is* unit tested, in the
`qar_ogre_adapter` repository.

To exercise it manually, publish a fix on the configured topic while streaming:

```powershell
python -c @"
import rclpy
from rclpy.node import Node
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import NavSatFix
rclpy.init()
n = Node('fake_gps')
p = n.create_publisher(NavSatFix, '/gps/fix', qos_profile_sensor_data)
m = NavSatFix(); m.latitude = 50.0755; m.longitude = 14.4378; m.altitude = 200.0
n.create_timer(1.0, lambda: p.publish(m))
rclpy.spin(n)
"@
```

Then look for the `GEO: app volume anchored at ...` line in the panel log.

## Status

**Built and unit tested; never exercised against live GPS data.** No rosbag
supplied so far contains a GPS topic. The first real fix is the actual test.

[rep105]: https://github.com/ros-infrastructure/rep/blob/master/rep-0105.rst
[conv]: https://docs.quaternar.com/developer-guide/api-conventions
