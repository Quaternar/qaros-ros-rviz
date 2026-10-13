#!/usr/bin/env python3
# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
"""Publish a video file (MP4 or anything OpenCV can decode) as a sensor_msgs/Image stream.

Decodes frames with OpenCV and publishes them as raw BGR8 sensor_msgs/Image messages on
/qar_test_video/image_raw (default) so RViz's Image or Camera display can render it without
a real camera. The video file is a required positional argument.

Requires the ROS 2 Humble distribution environment (scripts/env_setup.ps1) to be sourced, and
opencv-python installed in the Python virtual environment (see requirements.txt).

Usage:
    python scripts/publish_video_file.py clip.mp4
    python scripts/publish_video_file.py clip.mp4 --ros-args -p loop:=false -p rate:=10.0

Then in RViz: Add -> By display type -> Image (or Camera), set Image Topic to
/qar_test_video/image_raw. Nothing in RViz has to be changed for the DDS tuning below.

Positional argument:
    video_path - path to the video file (required).

Command-line options (they are read BEFORE rclpy.init, because Fast-DDS reads its environment
when the first participant is created - that is why they are not ROS parameters):
    --dds-profile FILE|none   Fast-DDS XML profile applied to this process through
                              FASTRTPS_DEFAULT_PROFILES_FILE. Default: scripts/fastdds_large_data.xml
                              (see "Why DDS tuning" below). A FASTRTPS_DEFAULT_PROFILES_FILE that is
                              already set in the environment always wins; "none" leaves Fast-DDS at
                              its defaults.
    --publication-mode MODE   RMW_FASTRTPS_PUBLICATION_MODE for this process: ASYNCHRONOUS (default),
                              SYNCHRONOUS, AUTO, or "keep" (do not touch it). A value already set in
                              the environment always wins.

ROS parameters (override with --ros-args -p name:=value):
    topic    (string) - Image topic to publish on. Default: /qar_test_video/image_raw.
    frame_id (string) - header.frame_id on published messages. Default: camera.
    loop     (bool)   - restart from the first frame at end of stream. Default: true.
    rate     (double) - publish rate in Hz. Default: 0.0, meaning "use the video's own fps".
    qos_depth (int)   - KeepLast history depth of the (always RELIABLE) publisher. Default: 10.
    timing_csv (string) - when set, write one CSV row per published frame to this file (see
                          "Timing CSV" below). Default: empty = off.
    width, height (int) - scale every frame to this size before publishing (cv2.INTER_AREA). 0 =
                          native; if only one is given the other follows the aspect ratio.
                          Default: 0 / 0. 1280x720 cuts the payload by 56 %.
    compressed (bool) - also publish every frame as a JPEG sensor_msgs/CompressedImage on
                        <topic>/compressed (the image_transport naming). Default: false.
                        RViz's Image display cannot show it: this distro ships only the "raw"
                        image_transport plugin (no compressed_image_transport), so RViz keeps
                        using <topic>; the plugin's AR video panel reads CompressedImage.
    raw (bool)        - publish the raw Image on <topic>. Default: true. raw:=false together with
                        compressed:=true publishes ONLY the JPEG stream.
    jpeg_quality (int) - JPEG quality of the compressed stream, 1-100. Default: 80.

Why DDS tuning (measured offline: private ROS_DOMAIN_ID, no GUI, 1080p bgr8 = 6.2 MB at 30 fps,
this publisher + a reader like RViz's Image display (reliable, KeepLast 5, 15 ms of work per
frame) + one like the AR panel's (reliable, KeepLast 2); medians of 3 runs of 15 s; the AR-like
reader is shown, the RViz-like one is within 1 ms):
                                      stamp->arrival     arrival spacing   best-effort    publish()
                                      p50 / p95 / max    p95 / max         reader gets    p50
    Fast-DDS defaults                 12.7 / 32 / 40 ms  51 / 60 ms        92-95 %        5.1 ms
    this script's defaults            11.3 / 14.8 / 18   38 / 43           99.8-100 %     2.2 ms
    Fast-DDS 2.6 sends a sample between processes of one machine through a shared-memory segment of
    512 KB per sending participant. A raw 1080p frame is ~96 fragments of 64 KB, so 5-8 % of the
    frames lost fragments; the reliable protocol repaired them 10-25 ms late, at BOTH readers (the
    same frames were late for both), which is the "judder" and why RViz's Image display and the AR
    panel stuttered together. fastdds_large_data.xml enlarges the segment to 64 MB (and keeps UDPv4
    for discovery / remote peers). The sending side is what matters, so this script applies the
    profile to itself - RViz needs nothing; the live launcher exports it to rviz2 as well so both
    ends agree. The Humble RMW's default publication mode is SYNCHRONOUS (the whole DDS send runs
    inside pub.publish()); ASYNCHRONOUS hands it to a Fast-DDS thread: same delivery numbers, but
    publish() drops from 5.1 to 2.2 ms (8.7 to 3.3 ms with the machine loaded) and a slow or
    remote subscriber cannot block the timer callback. Tried and rejected: a dedicated pacing
    thread (arrival spacing only ~1.5 ms tighter on top of the profile, +30 % publisher CPU, and
    stalls when another subscriber freezes), 1280x720 alone (the drops stay), bigger UDP socket
    buffers (no difference). Not changed: the loop restart (rewind to frame 0) still costs one
    ~60 ms frame gap every 30 s of the sample clip.

Timing CSV (the publisher half of the stutter proof; scripts/analyze_video_timing.py merges it
with the plugin's own CSVs):
    python scripts/publish_video_file.py clip.mp4 --ros-args -p timing_csv:=publisher.csv
Columns, all integers, microseconds since the Unix epoch on the system clock (the clock the
plugin's CSVs use, so values from the two processes can be subtracted):
    seq            1-based count of frames this node published
    stamp_us       the message's header.stamp - the join key with the plugin's header_stamp_us
    scheduled_us   when the timer was DUE to fire for this frame (rcl timers are drift-free; the
                   value is derived from Timer.time_until_next_call(), falling back to the
                   nominal grid start + n * period if that is unavailable)
    read_start_us  the callback began (read_start_us - scheduled_us is this process's lateness)
    publish_us     immediately before pub.publish(msg) (frame decoded and converted)
    publish_end_us immediately after it returned (the DDS hand-over / serialisation cost; with the
                   default ASYNCHRONOUS publication mode that is only the hand-over to Fast-DDS's
                   send thread, not the transport - --publication-mode SYNCHRONOUS puts the whole
                   send back inside it, at the price of ~3 ms more per frame)
Rows are queued by the timer callback and written, about once a second, by a background thread,
so the measurement does not add disk I/O to the very callback whose lateness it records.
"""
import argparse
import array
import ctypes
import os
import queue
import sys
import threading
import time
from pathlib import Path

import cv2

try:
    import rclpy
except ModuleNotFoundError as exc:
    if exc.name != "rclpy":
        raise
    sys.exit(
        "This Python has no ROS 2 packages (a plain virtual environment does not contain rclpy).\n"
        "Run this script in a shell that was prepared with scripts\\env_setup.ps1.")
from rclpy.node import Node
from rclpy.utilities import remove_ros_args
from sensor_msgs.msg import CompressedImage, Image

DEFAULT_DDS_PROFILE = Path(__file__).resolve().parent / "fastdds_large_data.xml"
PUBLICATION_MODES = ("ASYNCHRONOUS", "SYNCHRONOUS", "AUTO")


class _TimingWriter:
    """Writes the publisher's timing rows from a background thread.

    The timer callback only does ``add()``: a ``queue.put`` of a tuple of ints (microseconds).
    The thread wakes about once a second, writes whatever is queued and flushes, so a slow disk
    cannot delay a publish - and a publisher stall that WAS caused by something else is not
    confused with this file's own I/O.
    """

    HEADER = "seq,stamp_us,scheduled_us,read_start_us,publish_us,publish_end_us\n"

    def __init__(self, path, flush_period_s=1.0):
        self.path = str(path)
        parent = os.path.dirname(os.path.abspath(self.path))
        os.makedirs(parent, exist_ok=True)
        self._file = open(self.path, "w", buffering=1 << 16, newline="\n")
        self._file.write(self.HEADER)
        self._file.flush()
        self._queue = queue.SimpleQueue()
        self._flush_period_s = flush_period_s
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="timing-csv-writer", daemon=True)
        self._thread.start()

    def add(self, row):
        self._queue.put(row)

    def _drain(self):
        lines = []
        while True:
            try:
                row = self._queue.get_nowait()
            except queue.Empty:
                break
            lines.append(",".join("" if v is None else str(int(v)) for v in row))
        if lines:
            self._file.write("\n".join(lines) + "\n")
        self._file.flush()

    def _run(self):
        while not self._stop.wait(self._flush_period_s):
            self._drain()

    def close(self):
        self._stop.set()
        self._thread.join(timeout=5.0)
        self._drain()
        self._file.close()


def _build_arg_parser():
    parser = argparse.ArgumentParser(
        description="Publish a video file as a sensor_msgs/Image stream over ROS 2."
    )
    parser.add_argument("video_path", help="Video file to stream (required)")
    parser.add_argument(
        "--dds-profile", default=str(DEFAULT_DDS_PROFILE), metavar="FILE|none",
        help="Fast-DDS XML profile for this process (default: %(default)s); an already-set "
             "FASTRTPS_DEFAULT_PROFILES_FILE wins; 'none' = Fast-DDS defaults",
    )
    parser.add_argument(
        "--publication-mode", default="ASYNCHRONOUS", metavar="MODE",
        help="RMW_FASTRTPS_PUBLICATION_MODE for this process: ASYNCHRONOUS (default), SYNCHRONOUS, "
             "AUTO or 'keep' (leave it alone); an already-set value wins",
    )
    return parser


def _configure_dds_environment(profile, publication_mode):
    """Apply the DDS tuning to os.environ. MUST run before rclpy.init(): Fast-DDS reads
    FASTRTPS_DEFAULT_PROFILES_FILE and the RMW reads RMW_FASTRTPS_PUBLICATION_MODE when the first
    participant / publisher is created. Returns the log lines describing what was decided."""
    notes = []
    existing = os.environ.get("FASTRTPS_DEFAULT_PROFILES_FILE")
    if existing:
        notes.append(f"DDS: FASTRTPS_DEFAULT_PROFILES_FILE from the environment: {existing}")
    elif str(profile).lower() == "none":
        notes.append("DDS: no Fast-DDS profile (--dds-profile none): default 512 KB shared-memory segment")
    elif Path(profile).is_file():
        os.environ["FASTRTPS_DEFAULT_PROFILES_FILE"] = str(Path(profile).resolve())
        notes.append(f"DDS: Fast-DDS profile {Path(profile).resolve()} (64 MB shared-memory segment)")
    else:
        notes.append(f"DDS: WARNING profile not found ({profile}); running with Fast-DDS defaults")
    existing = os.environ.get("RMW_FASTRTPS_PUBLICATION_MODE")
    mode = str(publication_mode).upper()
    if existing:
        notes.append(f"DDS: RMW_FASTRTPS_PUBLICATION_MODE from the environment: {existing}")
    elif str(publication_mode).lower() == "keep":
        notes.append("DDS: publication mode left at the RMW default (SYNCHRONOUS)")
    elif mode in PUBLICATION_MODES:
        os.environ["RMW_FASTRTPS_PUBLICATION_MODE"] = mode
        notes.append(f"DDS: publication mode {mode}")
    else:
        notes.append(f"DDS: WARNING unknown --publication-mode {publication_mode!r}; leaving the default")
    return notes


class VideoFilePublisher(Node):
    def __init__(self, video_path):
        super().__init__("qar_video_file_publisher")
        topic = self.declare_parameter("topic", "/qar_test_video/image_raw").value
        self.frame_id = self.declare_parameter("frame_id", "camera").value
        self.loop = self.declare_parameter("loop", True).value
        rate_override = self.declare_parameter("rate", 0.0).value
        # Publisher history depth (KeepLast). The reliability stays RELIABLE regardless:
        # RViz's Image display subscribes reliable, and a reliable reader cannot match a
        # best-effort writer.
        qos_depth = max(1, int(self.declare_parameter("qos_depth", 10).value))
        timing_csv = str(self.declare_parameter("timing_csv", "").value or "")
        self.out_width = max(0, int(self.declare_parameter("width", 0).value))
        self.out_height = max(0, int(self.declare_parameter("height", 0).value))
        self.publish_raw = bool(self.declare_parameter("raw", True).value)
        self.publish_compressed = bool(self.declare_parameter("compressed", False).value)
        self.jpeg_quality = min(100, max(1, int(self.declare_parameter("jpeg_quality", 80).value)))
        if not (self.publish_raw or self.publish_compressed):
            raise RuntimeError("raw:=false needs compressed:=true - nothing would be published")

        self.cap = cv2.VideoCapture(video_path)
        if not self.cap.isOpened():
            raise RuntimeError(f"Could not open video file: {video_path}")

        fps = rate_override if rate_override > 0.0 else self.cap.get(cv2.CAP_PROP_FPS)
        if not fps or fps <= 0.0:
            fps = 30.0

        self.pub = self.create_publisher(Image, topic, qos_depth) if self.publish_raw else None
        # image_transport naming: <base topic>/compressed. Same QoS as the raw stream.
        self.compressed_pub = (
            self.create_publisher(CompressedImage, topic + "/compressed", qos_depth)
            if self.publish_compressed else None)
        # Per-frame timing CSV (timing_csv:=<path>): see the module docstring.
        self._timing = None
        self._seq = 0
        self._period_us = int(round(1e6 / fps))
        self._nominal_start_us = self._now_us()
        self._timer_api_ok = True
        if timing_csv:
            self._timing = _TimingWriter(timing_csv)
            self.get_logger().info(f"Timing CSV -> {self._timing.path}")
        self.timer = self.create_timer(1.0 / fps, self._publish_next_frame)
        streams = ", ".join(
            ([f"{topic} (raw bgr8)"] if self.publish_raw else [])
            + ([f"{topic}/compressed (jpeg q{self.jpeg_quality})"] if self.publish_compressed else []))
        size = f", scaled to {self.out_width}x{self.out_height}" if (self.out_width or self.out_height) else ""
        self.get_logger().info(
            f"Streaming '{video_path}' on {streams} at {fps:.2f} fps{size} (loop={self.loop}, "
            f"reliable KeepLast({qos_depth}))"
        )

        # The line above states the CONFIGURED rate only -- it is not a measurement, and
        # has been read as one before. Log what actually happened every ~5s instead: how
        # many frames this node's own timer callback actually published, independent of
        # whether any subscriber received them.
        self._rate_log_period_s = 5.0
        self._frames_since_rate_log = 0
        self._last_rate_log_time = self.get_clock().now()
        # Which call inside the timer callback the time actually goes to -- decoding the
        # next frame (cap.read(), CPU-bound OpenCV work) or handing a ~6 MB raw bgr8
        # message to rclpy's publish() (Python message construction + DDS serialize/send).
        # Both accumulate here and are reported as per-frame averages alongside the rate.
        self._read_ms_since_log = 0.0
        self._publish_ms_since_log = 0.0

    def _now_us(self):
        """Microseconds since the Unix epoch, from the same clock that stamps header.stamp."""
        return self.get_clock().now().nanoseconds // 1000

    def _scheduled_us(self, now_us):
        """When the rcl timer was due to fire for the callback now running.

        rcl advances the timer to its next period before the callback runs, so the due time of
        THIS call is "now + time until the next one - period". Falls back to the nominal grid
        (start + n * period) if the rclpy timer API is not available.
        """
        if self._timer_api_ok:
            try:
                return now_us + self.timer.time_until_next_call() // 1000 - self._period_us
            except Exception:  # noqa: BLE001 - older rclpy / canceled timer: use the grid
                self._timer_api_ok = False
        return self._nominal_start_us + self._seq * self._period_us

    def _maybe_log_achieved_rate(self):
        self._frames_since_rate_log += 1
        now = self.get_clock().now()
        elapsed = (now - self._last_rate_log_time).nanoseconds / 1e9
        if elapsed >= self._rate_log_period_s:
            achieved_fps = self._frames_since_rate_log / elapsed
            avg_read_ms = self._read_ms_since_log / self._frames_since_rate_log
            avg_publish_ms = self._publish_ms_since_log / self._frames_since_rate_log
            self.get_logger().info(
                f"Actual achieved publish rate: {achieved_fps:.2f} fps "
                f"({self._frames_since_rate_log} frames in {elapsed:.2f}s) -- "
                f"avg read+convert={avg_read_ms:.1f} ms, avg pub.publish()={avg_publish_ms:.1f} ms"
            )
            self._frames_since_rate_log = 0
            self._last_rate_log_time = now
            self._read_ms_since_log = 0.0
            self._publish_ms_since_log = 0.0

    def _scaled(self, frame):
        """Apply width:=/height:= (0 = native; one of them alone keeps the aspect ratio)."""
        if not (self.out_width or self.out_height):
            return frame
        src_h, src_w = frame.shape[:2]
        width = self.out_width or max(2, int(round(src_w * self.out_height / src_h / 2.0)) * 2)
        height = self.out_height or max(2, int(round(src_h * self.out_width / src_w / 2.0)) * 2)
        if (width, height) == (src_w, src_h):
            return frame
        return cv2.resize(frame, (width, height), interpolation=cv2.INTER_AREA)

    @staticmethod
    def _uint8_array(buffer):
        """A uint8[] message field value from a C-contiguous numpy buffer, copied ONCE.

        rclpy validates every element of a uint8[] assigned as bytes in Python
        (~400 ms for a 1080p frame); an array.array('B') skips that check. frombytes() takes the
        numpy buffer directly, where array.array('B', frame.tobytes()) copied the 6 MB twice.
        """
        data = array.array("B")
        data.frombytes(buffer.data if buffer.flags.c_contiguous else buffer.tobytes())
        return data

    def _publish_next_frame(self):
        read_start = time.perf_counter()
        timing = self._timing is not None
        read_start_us = self._now_us() if timing else 0
        scheduled_us = self._scheduled_us(read_start_us) if timing else 0
        ok, frame = self.cap.read()
        if not ok:
            if not self.loop:
                self.get_logger().info("Reached end of video, stopping (loop=false). Ctrl+C to exit.")
                self.timer.cancel()
                return
            self.cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
            ok, frame = self.cap.read()
            if not ok:
                self.get_logger().error("Failed to rewind video for looping.")
                self.timer.cancel()
                return

        stamp = self.get_clock().now().to_msg()
        frame = self._scaled(frame)
        raw_msg = compressed_msg = None
        if self.publish_raw:
            raw_msg = Image()
            raw_msg.header.stamp = stamp
            raw_msg.header.frame_id = self.frame_id
            raw_msg.height, raw_msg.width = frame.shape[:2]
            raw_msg.encoding = "bgr8"
            raw_msg.is_bigendian = 0
            raw_msg.step = raw_msg.width * frame.shape[2]
            raw_msg.data = self._uint8_array(frame)
        if self.publish_compressed:
            encoded_ok, jpeg = cv2.imencode(".jpg", frame, [cv2.IMWRITE_JPEG_QUALITY, self.jpeg_quality])
            if encoded_ok:
                compressed_msg = CompressedImage()
                compressed_msg.header.stamp = stamp
                compressed_msg.header.frame_id = self.frame_id
                compressed_msg.format = "jpeg"
                compressed_msg.data = self._uint8_array(jpeg)
        read_end = time.perf_counter()
        publish_us = self._now_us() if timing else 0
        if raw_msg is not None:
            self.pub.publish(raw_msg)
        if compressed_msg is not None:
            self.compressed_pub.publish(compressed_msg)
        publish_end = time.perf_counter()
        if timing:
            self._seq += 1
            stamp_us = stamp.sec * 1000000 + stamp.nanosec // 1000
            self._timing.add(
                (self._seq, stamp_us, scheduled_us, read_start_us, publish_us, self._now_us()))
        self._read_ms_since_log += (read_end - read_start) * 1000.0
        self._publish_ms_since_log += (publish_end - read_end) * 1000.0
        self._maybe_log_achieved_rate()

    def destroy_node(self):
        self.cap.release()
        if self._timing is not None:
            self._timing.close()
        super().destroy_node()


def _opt_out_of_power_throttling():
    """Windows-only: ask the OS to always run this process at full speed and to
    always honour its timer-resolution requests, even when it is in the
    background or fully occluded (Windows 11 otherwise stops honouring a
    process's timeBeginPeriod and may EcoQoS-throttle it, which snaps the 30 fps
    timer back to the ~15.6 ms scheduler tick).

    PROCESS_POWER_THROTTLING_STATE {Version=1, ControlMask=EXECUTION_SPEED(0x1) |
    IGNORE_TIMER_RESOLUTION(0x4), StateMask=0}, information class
    ProcessPowerThrottling = 4. StateMask = 0 means "throttling OFF" for every bit
    in ControlMask. Returns one status line; never raises (an older Windows that
    lacks SetProcessInformation simply skips it).
    """
    if sys.platform != "win32":
        return None
    try:
        class ProcessPowerThrottlingState(ctypes.Structure):
            _fields_ = [
                ("Version", ctypes.c_ulong),
                ("ControlMask", ctypes.c_ulong),
                ("StateMask", ctypes.c_ulong),
            ]

        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        set_process_information = getattr(kernel32, "SetProcessInformation", None)
        if set_process_information is None:
            return "SCHED: SetProcessInformation unavailable (older Windows); skipped"
        kernel32.GetCurrentProcess.restype = ctypes.c_void_p
        set_process_information.argtypes = [
            ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_ulong]
        set_process_information.restype = ctypes.c_int
        state = ProcessPowerThrottlingState(1, 0x1 | 0x4, 0)
        ok = set_process_information(
            kernel32.GetCurrentProcess(), 4, ctypes.byref(state), ctypes.sizeof(state))
        if ok:
            return ("SCHED: process power-throttling opt-out applied "
                    "(full speed, timer resolution always honoured)")
        return "SCHED: SetProcessInformation(ProcessPowerThrottling) failed, error %d" % (
            ctypes.get_last_error())
    except Exception as exc:  # noqa: BLE001 - must never stop the publisher
        return "SCHED: power-throttling opt-out skipped (%s)" % (exc,)


def main():
    # Our own options first, and the DDS environment BEFORE rclpy.init (see the module docstring).
    args = _build_arg_parser().parse_args(remove_ros_args(args=sys.argv)[1:])
    dds_notes = _configure_dds_environment(args.dds_profile, args.publication_mode)
    rclpy.init(args=sys.argv)
    node = VideoFilePublisher(args.video_path)
    for note in dds_notes:
        node.get_logger().info(note)
    # Windows' default ~15.6 ms scheduler tick would make this node's 33.3 ms
    # (30 fps) rclpy timer snap to multiples of it, so raise the process-wide
    # timer resolution to 1 ms for the duration of the spin loop.
    if sys.platform == "win32":
        ctypes.windll.winmm.timeBeginPeriod(1)
        # ...and keep Windows 11 from taking that resolution (or the CPU speed)
        # away again while this process is in the background / occluded.
        sched_line = _opt_out_of_power_throttling()
        if sched_line:
            node.get_logger().info(sched_line)
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if sys.platform == "win32":
            ctypes.windll.winmm.timeEndPeriod(1)
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
