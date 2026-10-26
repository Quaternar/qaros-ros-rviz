#!/usr/bin/env python3
# Copyright 2026 QuaternAR
# SPDX-License-Identifier: MIT
"""Attribute AR-video stutter to a pipeline stage from the plugin's per-frame timing CSVs.

Pure Python (standard library only, no ROS needed). Reads the folder the plugin writes with the
launcher's ``-TimingCsv`` switch (``QAR_VIDEO_TIMING_DIR``) and, optionally, the publisher's own
CSV (``publish_video_file.py ... --ros-args -p timing_csv:=<file>``), and prints:

  1. per-stage latency distributions (p50 / p95 / p99 / max) for publisher -> arrival,
     arrival -> decode, decode -> capture (the ticks that found a frame they had not seen),
     capture -> submit, the not_ready durations of every video panel and of the 3D stereo stream;
  2. stall episodes: every gap between two consecutive submits of a panel longer than
     ``--gap-ms`` (default 100 ms) that is not idle (see below), with the FIRST pipeline stage
     that went quiet (the stage whose silence began earliest), whether the 3D stream was starved
     at the same moment, and whether RViz's own GUI thread had stopped;
  3. periodicity: the dominant period of the episodes' onsets (a strict 15.001 s grid points at
     a periodic task of the machine, not at a frame-driven cause);
  4. one verdict per episode:
       SOURCE              nothing reached us: the publisher did not publish, or the frames
                           never arrived (transport) - the pipeline had nothing to process;
       OUR-PIPELINE        frames arrived but our decode, GUI-thread capture or submit did not
                           run;
       QAROS-BACKPRESSURE  frames arrived, were decoded and captured, but QarOS offered no
                           destination (try_begin_frame -> not ready) until the gap ended.

A panel that has no new frame to show submits its last picture again about once a second (the
keep-alive of qar_qt_streaming), so a video that stopped, or that has not started yet, still makes
a submit gap of about a second, over and over. Such a gap is IDLE, not a stall: no frame was
waiting in it. A gap is idle when every capture tick inside it has the outcome ``unchanged``, no
message reached the rx source of the panel inside it, and the submit that closes it carries no
frame newer than the one the opening submit carried. Idle gaps are counted and added up but never
become episodes, and the summary of a panel describes its ACTIVE part: the seconds in which a
frame was waiting, the submit rate over them and the percentiles of the submit gaps in them. These
stay episodes: a gap in which a new frame arrived and was not submitted, a gap without any capture
tick while a message arrived (the GUI thread stood still), and a gap in which a tick says
``not_ready``, ``capture_failed``, ``submit_failed`` or ``adapter_error``. When the video starts
again after an idle stretch, the wait of its first frame is counted from the arrival of that
frame: the silence before it belongs to the source.

Files read from the timing folder (all written by the plugin; epoch microseconds on the system
clock, the same clock the publisher uses). Formats are documented in
docs/LOG_REFERENCE.md, section "Timing CSV files":

  video_<key>.csv   one row per capture tick of an AR video panel:
                    seq,header_stamp_us,arrival_us,decode_done_us,mailbox_us,decode_seq,tick_us,
                    capture_us,ready_us,not_ready_start_us,not_ready_end_us,not_ready_count,
                    submit_us,outcome            (outcome: submitted|not_ready|unchanged|
                                                  capture_failed|submit_failed|adapter_error)
  rx_<topic>.csv    one row per message the ROS callback received:
                    seq,header_stamp_us,arrival_us,decode_start_us,decode_done_us,mailbox_us,
                    decode_seq,outcome           (outcome: decoded|superseded|decode_error)
  render3d*.csv     the 3D stereo stream: kind,start_us,end_us,count,frame,detail,mid_us
                    (kind: not_ready|ready|submit|slow_poll)
  gui_gaps.csv      every gap between Qt render ticks over 25 ms: start_us,end_us,gap_us
  publisher.csv     seq,stamp_us,scheduled_us,read_start_us,publish_us,publish_end_us

Usage:
  python scripts/analyze_video_timing.py build/timing/<yyyymmdd_HHMMSS>
  python scripts/analyze_video_timing.py <dir> --publisher <dir>/publisher.csv --gap-ms 100
  python scripts/analyze_video_timing.py --self-test
"""
import argparse
import bisect
import csv
import datetime
import glob
import io
import os
import random
import statistics
import sys
import tempfile

# Pipeline order: an episode's FIRST gapped stage is the earliest of these whose silence began
# first; ties inside TIE_US go to the stage that comes first here.
STAGES = ("publish", "arrival", "decode", "capture", "ready", "submit")
TIE_US = 5000

SOURCE = "SOURCE"
OUR = "OUR-PIPELINE"
QAROS = "QAROS-BACKPRESSURE"
UNKNOWN = "UNKNOWN"

# What a gap between two consecutive submits of a panel can be (see classify_gaps).
IDLE = "idle"        # no frame was waiting: the panel only repeated its last picture
ACTIVE = "active"    # a frame was waiting: it arrived, or a tick found a newer one
BLOCKED = "blocked"  # no frame was waiting, but a tick found QarOS not ready or a step failing
# With no frame waiting only these stages can be at fault; silence upstream is not a cause.
DOWNSTREAM = ("capture", "ready", "submit")


# ---------------------------------------------------------------------------------------------
# loading
# ---------------------------------------------------------------------------------------------
def _num(text):
    if text is None or text == "":
        return None
    try:
        return int(text)
    except ValueError:
        return text


def load_csv(path):
    """Rows as dicts: integers parsed, blank cells None, anything else kept as text."""
    rows = []
    with open(path, newline="", encoding="utf-8") as handle:
        for raw in csv.DictReader(handle):
            rows.append({key: _num(value) for key, value in raw.items()})
    return rows


class Panel:
    """One AR video panel: its tick rows, and the rx rows of the topic it shows (if known)."""

    def __init__(self, name, rows):
        self.name = name
        self.rows = rows
        self.rx = None
        self.rx_name = None
        self.publisher_matched = False

    def stamps(self):
        return {r["header_stamp_us"] for r in self.rows if r.get("header_stamp_us") is not None}


class Data:
    def __init__(self):
        self.panels = []          # [Panel]
        self.rx = {}              # name -> rows
        self.render3d = {}        # name -> rows
        self.gui_gaps = []        # [(start_us, end_us)]
        self.publisher = None     # rows or None


def load_dir(directory, publisher_path=None):
    data = Data()
    for path in sorted(glob.glob(os.path.join(directory, "video_*.csv"))):
        name = os.path.splitext(os.path.basename(path))[0][len("video_"):]
        data.panels.append(Panel(name, load_csv(path)))
    for path in sorted(glob.glob(os.path.join(directory, "rx_*.csv"))):
        data.rx[os.path.splitext(os.path.basename(path))[0][len("rx_"):]] = load_csv(path)
    for path in sorted(glob.glob(os.path.join(directory, "render3d*.csv"))):
        data.render3d[os.path.splitext(os.path.basename(path))[0]] = load_csv(path)
    gaps_path = os.path.join(directory, "gui_gaps.csv")
    if os.path.exists(gaps_path):
        data.gui_gaps = [(r["start_us"], r["end_us"]) for r in load_csv(gaps_path)
                         if r.get("start_us") is not None and r.get("end_us") is not None]
    if publisher_path is None:
        candidate = os.path.join(directory, "publisher.csv")
        publisher_path = candidate if os.path.exists(candidate) else None
    if publisher_path:
        data.publisher = load_csv(publisher_path)
    _link(data)
    return data


def _link(data):
    """Pair every panel with the rx file of the topic it shows (largest header-stamp overlap;
    the only rx file when there is just one) and note which have a publisher CSV."""
    pub_stamps = set()
    if data.publisher:
        pub_stamps = {r["stamp_us"] for r in data.publisher if r.get("stamp_us") is not None}
    for panel in data.panels:
        stamps = panel.stamps()
        best, best_overlap = None, 0
        for name, rows in data.rx.items():
            rx_stamps = {r["header_stamp_us"] for r in rows if r.get("header_stamp_us") is not None}
            overlap = len(stamps & rx_stamps)
            if overlap > best_overlap:
                best, best_overlap = name, overlap
        if best is None and len(data.rx) == 1:
            best = next(iter(data.rx))
        if best is not None:
            panel.rx_name = best
            panel.rx = data.rx[best]
        if pub_stamps:
            rx_stamps = ({r["header_stamp_us"] for r in panel.rx if r.get("header_stamp_us") is not None}
                         if panel.rx else stamps)
            panel.publisher_matched = bool(rx_stamps & pub_stamps)


# ---------------------------------------------------------------------------------------------
# statistics
# ---------------------------------------------------------------------------------------------
def percentile(sorted_values, p):
    """Linear-interpolation percentile (p in 0..100) of an ascending list."""
    if not sorted_values:
        return None
    if len(sorted_values) == 1:
        return sorted_values[0]
    k = (len(sorted_values) - 1) * p / 100.0
    low = int(k)
    high = min(low + 1, len(sorted_values) - 1)
    return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * (k - low)


def summarize(values_us):
    """(n, p50, p95, p99, max) in milliseconds, or None for an empty list."""
    if not values_us:
        return None
    ordered = sorted(values_us)
    return (len(ordered),) + tuple(percentile(ordered, p) / 1000.0 for p in (50, 95, 99)) + (
        ordered[-1] / 1000.0,)


def format_table(title, rows):
    """rows: [(label, summary-or-None)] -> text."""
    lines = [title,
             "  {:<34} {:>7} {:>10} {:>10} {:>10} {:>10}".format(
                 "stage", "n", "p50 ms", "p95 ms", "p99 ms", "max ms")]
    for label, summary in rows:
        if summary is None:
            lines.append("  {:<34} {:>7}".format(label, "-"))
        else:
            n, p50, p95, p99, mx = summary
            lines.append("  {:<34} {:>7} {:>10.2f} {:>10.2f} {:>10.2f} {:>10.2f}".format(
                label, n, p50, p95, p99, mx))
    return "\n".join(lines)


def clock(us):
    """Local time of day of an epoch-microsecond stamp."""
    return datetime.datetime.fromtimestamp(us / 1e6).strftime("%H:%M:%S.%f")[:-3]


# ---------------------------------------------------------------------------------------------
# per-panel stage events and latencies
# ---------------------------------------------------------------------------------------------
def _column(rows, name, where=None):
    return [r[name] for r in rows if r.get(name) is not None and (where is None or where(r))]


def stage_events(panel, data):
    """Sorted event times (us) of every pipeline stage for @panel; an absent stage is omitted."""
    events = {}
    if data.publisher and panel.publisher_matched:
        events["publish"] = sorted(_column(data.publisher, "publish_us"))
    if panel.rx:
        events["arrival"] = sorted(_column(panel.rx, "arrival_us"))
        events["decode"] = sorted(_column(panel.rx, "mailbox_us", lambda r: r.get("outcome") == "decoded"))
    events["capture"] = sorted(_column(panel.rows, "capture_us"))
    events["ready"] = sorted(_column(panel.rows, "ready_us"))
    events["submit"] = sorted(_column(panel.rows, "submit_us"))
    return {stage: times for stage, times in events.items() if times}


def not_ready_runs(rows):
    """Durations (us) of the not-ready runs recorded in video rows: one per distinct run start,
    taking the longest end seen for it (the closing row carries the final one)."""
    longest = {}
    for r in rows:
        start, end = r.get("not_ready_start_us"), r.get("not_ready_end_us")
        if start is not None and end is not None:
            longest[start] = max(longest.get(start, 0), end - start)
    return list(longest.values())


def latency_rows(panel, data):
    rows = []
    if panel.rx:
        decoded = [r for r in panel.rx if r.get("outcome") == "decoded"]
        if data.publisher and panel.publisher_matched:
            publish_at = {r["stamp_us"]: r["publish_us"] for r in data.publisher
                          if r.get("stamp_us") is not None and r.get("publish_us") is not None}
            values = [r["arrival_us"] - publish_at[r["header_stamp_us"]] for r in panel.rx
                      if r.get("arrival_us") is not None and r.get("header_stamp_us") in publish_at]
            rows.append(("publisher -> arrival", summarize(values)))
        else:
            values = _column_diff(panel.rx, "arrival_us", "header_stamp_us")
            rows.append(("header.stamp -> arrival (no pub CSV)", summarize(values)))
        rows.append(("arrival -> decode done", summarize(_column_diff(decoded, "decode_done_us", "arrival_us"))))
    # A tick that finds no new frame captures the old one again; its age is no latency.
    fresh = fresh_rows(tick_rows(panel))
    rows.append(("decode (mailbox) -> capture",
                 summarize(_column_diff(fresh, "capture_us", "mailbox_us"))))
    submitted = [r for r in panel.rows if r.get("outcome") == "submitted"]
    rows.append(("capture -> submit", summarize(_column_diff(submitted, "submit_us", "capture_us"))))
    rows.append(("not_ready run duration", summarize(not_ready_runs(panel.rows))))
    return rows


def publisher_rows(rows):
    """Where the publisher's own time goes, and how late its timer fires."""
    late = _column_diff(rows, "read_start_us", "scheduled_us")
    convert = _column_diff(rows, "publish_us", "read_start_us")
    send = _column_diff(rows, "publish_end_us", "publish_us")
    stamps = sorted(r["publish_us"] for r in rows if r.get("publish_us") is not None)
    intervals = [b - a for a, b in zip(stamps, stamps[1:])]
    return [("timer lateness (read - scheduled)", summarize([max(0, v) for v in late])),
            ("read + convert (publish - read)", summarize(convert)),
            ("pub.publish() duration", summarize(send)),
            ("interval between publishes", summarize(intervals))]


def _column_diff(rows, later, earlier):
    return [r[later] - r[earlier] for r in rows if r.get(later) is not None and r.get(earlier) is not None]


# ---------------------------------------------------------------------------------------------
# 3D stream and GUI-thread context
# ---------------------------------------------------------------------------------------------
def render3d_runs(data):
    """Every not_ready run of every 3D stream: (start_us, end_us, polls)."""
    runs = []
    for rows in data.render3d.values():
        for r in rows:
            if r.get("kind") == "not_ready" and r.get("start_us") is not None and r.get("end_us") is not None:
                runs.append((r["start_us"], r["end_us"], r.get("count") or 0))
    return sorted(runs)


def polled_runs(runs):
    """The runs during which the render thread was really POLLING.

    A 3D stream is polled thousands of times a second, so a long not_ready run with almost no
    polls in it means the polling thread was not running (a GUI stall), not that QarOS had no
    destination: it must not be read as QarOS back-pressure. A run counts when its poll rate is
    at least a quarter of the typical rate of the ordinary (short) runs; with no baseline, 100/s.
    """
    rates = [polls / ((end - start) / 1e6) for start, end, polls in runs
             if 0 < end - start < 50000 and polls > 0]
    floor = 0.25 * statistics.median(rates) if rates else 100.0
    return [(s, e, p) for s, e, p in runs if e > s and p / ((e - s) / 1e6) >= floor]


def overlap_us(a0, a1, b0, b1):
    return max(0, min(a1, b1) - max(a0, b0))


def coverage(intervals, w0, w1):
    """Fraction of [w0, w1] covered by the union of @intervals (assumed non-overlapping)."""
    if w1 <= w0:
        return 0.0
    return min(1.0, sum(overlap_us(s, e, w0, w1) for s, e in intervals) / float(w1 - w0))


# ---------------------------------------------------------------------------------------------
# submit gaps: idle or waiting
# ---------------------------------------------------------------------------------------------
def _row_time(row):
    """When the tick of a video row began: the first time stamp the row has."""
    for key in ("tick_us", "capture_us", "ready_us", "submit_us"):
        if row.get(key) is not None:
            return row[key]
    return None


def tick_rows(panel):
    """The rows of @panel in the order of their ticks."""
    return sorted((r for r in panel.rows if _row_time(r) is not None), key=_row_time)


def fresh_rows(rows):
    """The rows (in tick order) whose tick found a frame it had not seen before. The others
    capture the picture of the last frame once more, as an idle panel does when it polls."""
    fresh, seen = [], object()
    for r in rows:
        if r.get("decode_seq") != seen:
            fresh.append(r)
        seen = r.get("decode_seq")
    return fresh


class SubmitGap:
    """The time between two consecutive submits of one panel, and what it was."""

    def __init__(self, opened, closed):
        self.opened = opened        # submit_us of the submit that opens the gap
        self.closed = closed        # submit_us of the submit that closes it
        self.kind = ACTIVE
        self.start = opened         # where the wait of a frame begins (see classify_gaps)
        self.after_idle = False     # the video started again in this gap

    @property
    def wait_us(self):
        return self.closed - self.start


def classify_gaps(panel, data, gap_us):
    """The gaps between the consecutive submits of @panel, each IDLE, ACTIVE or BLOCKED.

    A frame is WAITING in a gap when a message reached the rx source of the panel after the tick
    that opened the gap began (and before the tick that closed it began), or when a tick of the
    gap, the closing one included, saw a frame newer than the one the opening submit carried. A
    gap without a waiting frame is IDLE when every tick inside it found the picture unchanged;
    when some tick said not_ready or a failure it is BLOCKED: the keep-alive of the last picture
    could not go out.

    The submit that opens a gap may itself only repeat a picture (the keep-alive of an idle
    panel). When a frame then arrives, the silence before it belongs to the source, so the wait of
    the gap counts from that arrival, and the gap is IDLE if the frame was submitted within
    @gap_us.
    """
    rows = tick_rows(panel)
    arrivals = sorted(_column(panel.rx, "arrival_us")) if panel.rx else None
    submitted = [i for i, r in enumerate(rows) if r.get("submit_us") is not None]
    gaps = []
    for n, (i, j) in enumerate(zip(submitted, submitted[1:])):
        opening, closing = rows[i], rows[j]
        gap = SubmitGap(opening["submit_us"], closing["submit_us"])
        inside = rows[i + 1:j]
        carried = opening.get("decode_seq")
        newer = [r for r in inside + [closing] if r.get("decode_seq") != carried]
        arrived = [r["arrival_us"] for r in newer if r.get("arrival_us") is not None]
        if arrivals is not None:
            k = bisect.bisect_right(arrivals, _row_time(opening))
            if k < len(arrivals) and arrivals[k] < _row_time(closing):
                arrived.append(arrivals[k])
        if newer or arrived:
            # Was the picture the opening submit sent a new frame, or an old one sent again?
            if n == 0:
                repeat = opening.get("arrival_us") is None
            else:
                repeat = carried == rows[submitted[n - 1]].get("decode_seq")
            if repeat:
                gap.after_idle = True
                if arrived and min(arrived) > gap.opened:
                    gap.start = min(arrived)
                if gap.wait_us < gap_us:
                    gap.kind = IDLE
        elif all(r.get("outcome") == "unchanged" for r in inside):
            gap.kind = IDLE
        else:
            gap.kind = BLOCKED
        gaps.append(gap)
    return gaps


def gap_totals(gaps):
    """{kind: (number of gaps, seconds)}. The seconds of an ACTIVE gap are its wait."""
    totals = {ACTIVE: [0, 0.0], IDLE: [0, 0.0], BLOCKED: [0, 0.0]}
    for g in gaps:
        totals[g.kind][0] += 1
        totals[g.kind][1] += (g.wait_us if g.kind == ACTIVE else g.closed - g.opened) / 1e6
    return {kind: tuple(value) for kind, value in totals.items()}


# ---------------------------------------------------------------------------------------------
# stall episodes
# ---------------------------------------------------------------------------------------------
def silent_intervals(times, min_gap_us):
    return [(a, b) for a, b in zip(times, times[1:]) if b - a >= min_gap_us]


def best_gap(gaps, w0, w1):
    """The (start, end) of the stage gap overlapping [w0, w1] most, if it covers at least half
    of the shorter of the two."""
    starts = [g[0] for g in gaps]
    i = max(0, bisect.bisect_left(starts, w0) - 2)
    best, best_ov = None, 0
    while i < len(gaps) and gaps[i][0] < w1:
        ov = overlap_us(gaps[i][0], gaps[i][1], w0, w1)
        if ov > best_ov:
            best, best_ov = gaps[i], ov
        i += 1
    if best is not None and best_ov >= 0.5 * min(w1 - w0, best[1] - best[0]):
        return best
    return None


class Episode:
    def __init__(self, panel, start, end):
        self.panel = panel
        self.start = start            # the last submit before the gap (see SubmitGap.start)
        self.end = end                # the first submit after it
        self.gapped = {}              # stage -> onset (us)
        self.first = None
        self.verdict = UNKNOWN
        self.detail = ""
        self.three_d = 0.0
        self.gui = 0.0
        self.not_ready_ticks = 0
        self.after_idle = False       # the wait counts from the first frame after an idle stretch
        self.blocked = False          # no frame was waiting: a keep-alive could not go out

    @property
    def duration_ms(self):
        return (self.end - self.start) / 1000.0


def find_episodes(panel, data, gap_us, gaps=None):
    events = stage_events(panel, data)
    stage_gaps = {stage: silent_intervals(times, gap_us) for stage, times in events.items()}
    # "The 3D stream was starved" = a LONG not_ready run (a healthy stream is not_ready for ~80% of
    # the time, waiting 16 ms for each viewer request) during which the render thread kept polling.
    poll_runs = [(s, e) for s, e, _ in polled_runs(render3d_runs(data)) if e - s >= gap_us]
    gui = [(s, e) for s, e in data.gui_gaps]
    not_ready = sorted(r["tick_us"] for r in panel.rows
                       if r.get("outcome") == "not_ready" and r.get("tick_us") is not None)
    if gaps is None:
        gaps = classify_gaps(panel, data, gap_us)
    episodes = []
    for gap in gaps:
        if gap.kind == IDLE or gap.wait_us < gap_us:
            continue
        a, b = gap.start, gap.closed
        ep = Episode(panel, a, b)
        ep.after_idle = gap.after_idle
        ep.blocked = gap.kind == BLOCKED
        for stage in (DOWNSTREAM if ep.blocked else STAGES):
            if stage in stage_gaps:
                hit = best_gap(stage_gaps[stage], a, b)
                if hit is not None:
                    ep.gapped[stage] = hit[0]
        if ep.gapped:
            earliest = min(ep.gapped.values())
            ep.first = next(s for s in STAGES if s in ep.gapped and ep.gapped[s] - earliest <= TIE_US)
        ep.three_d = coverage(poll_runs, a, b)
        ep.gui = coverage(gui, a, b)
        ep.not_ready_ticks = bisect.bisect_right(not_ready, b) - bisect.bisect_left(not_ready, a)
        _judge(ep, events)
        if ep.blocked:
            ep.detail += (" (no frame was waiting: the keep-alive of the last picture could "
                          "not go out)")
        if ep.after_idle:
            ep.detail += " (counted from the arrival of the first frame after an idle stretch)"
        episodes.append(ep)
    return episodes


def _judge(ep, events):
    have_up = "arrival" in events
    shared = " (3D stream starved too: %.0f%% of the gap)" % (100 * ep.three_d) if ep.three_d >= 0.5 else ""
    if ep.first is None:
        ep.verdict = UNKNOWN
        ep.detail = ("no stage went quiet during the gap - data missing?" if have_up else
                     "no rx_*.csv for this panel: cannot see upstream of the capture")
        return
    if ep.first in ("publish", "arrival"):
        ep.verdict = SOURCE
        if ep.first == "publish":
            ep.detail = "the publisher itself published nothing"
        elif "publish" in events:
            ep.detail = "the publisher published but nothing arrived (transport / delivery)"
        else:
            ep.detail = "no frame arrived (no publisher CSV to say whether it was published)"
    elif ep.first == "decode":
        ep.verdict = OUR
        ep.detail = "frames arrived but the decode worker produced nothing"
    elif ep.first == "capture":
        ep.verdict = OUR
        ep.detail = ("frames arrived and were decoded, but the GUI thread did not capture"
                     + (" - its render ticks stopped (gui_gaps %.0f%%)" % (100 * ep.gui) if ep.gui >= 0.5
                        else " (render ticks kept running: capture gate or capture failure)"))
    else:  # ready / submit: everything upstream kept flowing
        if ep.gui >= 0.5:
            ep.verdict = OUR
            ep.detail = "the GUI thread stopped (gui_gaps %.0f%% of the gap)" % (100 * ep.gui)
        elif ep.not_ready_ticks > 0:
            ep.verdict = QAROS
            ep.detail = ("frames were captured but try_begin_frame said not ready on %d tick(s)"
                         % ep.not_ready_ticks)
        else:
            ep.verdict = OUR
            ep.detail = "frames were captured and no not_ready was recorded: our submit did not run"
    if ep.verdict == QAROS:
        ep.detail += shared


# ---------------------------------------------------------------------------------------------
# periodicity
# ---------------------------------------------------------------------------------------------
def dominant_period(onsets_us, tolerance_us=50000):
    """(period_s, fits, total) for the spacing of @onsets_us, or None.

    Candidates are the gaps between onsets; a gap "fits" a candidate period P when it is a whole
    number of P within @tolerance_us (a missed episode doubles a gap, it does not break the
    grid). The winner is the P with the most fits, preferring the shorter on a tie; it is then
    refined as the median of gap / round(gap / P). A period counts only when it is at least ten
    times the tolerance (a shorter one fits every spacing by chance, because the tolerance
    covers it whole) and when it fits at least half of the spacings.
    """
    onsets = sorted(onsets_us)
    gaps = [b - a for a, b in zip(onsets, onsets[1:]) if b - a > 0]
    if len(gaps) < 2:
        return None
    best_p, best_fits = None, 0
    for candidate in sorted(set(gaps)):
        if candidate < 10 * tolerance_us:
            continue
        fits = sum(1 for g in gaps if round(g / candidate) >= 1
                   and abs(g - round(g / candidate) * candidate) <= tolerance_us)
        if fits > best_fits:
            best_p, best_fits = candidate, fits
    if best_p is None or best_fits < 2 or 2 * best_fits < len(gaps):
        return None
    matching = [g / round(g / best_p) for g in gaps
                if round(g / best_p) >= 1 and abs(g - round(g / best_p) * best_p) <= tolerance_us]
    return statistics.median(matching) / 1e6, best_fits, len(gaps)


def grid_deviation_ms(onsets_us, period_s):
    """Largest distance (ms) of any onset from the best-fit grid t0 + k * period."""
    onsets = sorted(onsets_us)
    period_us = period_s * 1e6
    t0 = statistics.median(o - round((o - onsets[0]) / period_us) * period_us for o in onsets)
    return max(abs(o - (t0 + round((o - t0) / period_us) * period_us)) for o in onsets) / 1000.0


# ---------------------------------------------------------------------------------------------
# report
# ---------------------------------------------------------------------------------------------
def summary_lines(submits, gaps, gap_ms):
    """The lines that sum up the submits of one panel: all of them, then the ACTIVE part (the
    seconds in which a frame was waiting, the rate over them, the percentiles of the submit gaps
    in them), then the idle gaps that were ignored."""
    gap_us = int(gap_ms * 1000)
    totals = gap_totals(gaps)
    head = "submitted %d frames in %.1f s" % (len(submits), (submits[-1] - submits[0]) / 1e6)
    active_n, active_s = totals[ACTIVE]
    if active_n and active_s > 0:
        waits = sorted(g.wait_us for g in gaps if g.kind == ACTIVE)
        head += ("; active %.1f s at %.1f fps; submit gap in the active part p50 %.1f / p95 %.1f / "
                 "p99 %.1f / max %.1f ms; gaps >%.0f ms: %d, >250 ms: %d, >500 ms: %d" % (
                     active_s, active_n / active_s,
                     *(percentile(waits, p) / 1000.0 for p in (50, 95, 99)), waits[-1] / 1000.0,
                     gap_ms, sum(w >= gap_us for w in waits), sum(w >= 250000 for w in waits),
                     sum(w >= 500000 for w in waits)))
    else:
        head += "; no active part: no frame was waiting in any submit gap"
    idle_n, idle_s = totals[IDLE]
    lines = ["  " + head, "  idle keep-alive gaps ignored: %d (%.1f s)" % (idle_n, idle_s)]
    blocked_n, blocked_s = totals[BLOCKED]
    if blocked_n:
        lines.append("  keep-alive gaps with no frame waiting but a tick not ready or failing: %d "
                     "(%.1f s), outside the active part; episodes when over %.0f ms" % (
                         blocked_n, blocked_s, gap_ms))
    return lines


def analyze(data, gap_ms=100.0, max_episodes=25, out=sys.stdout):
    """Print the proof tables. Returns {panel name: [Episode]} (the self-test asserts on it)."""
    gap_us = int(gap_ms * 1000)
    write = lambda text="": print(text, file=out)  # noqa: E731
    if not data.panels:
        write("No video_*.csv in the timing folder - nothing to analyse (was -TimingCsv used, "
              "and did an AR video panel stream?).")
        return {}

    write("=== AR video timing: %d panel(s), %d rx source(s), %d 3D stream(s), publisher CSV: %s, "
          "GUI gaps recorded: %d ===" % (len(data.panels), len(data.rx), len(data.render3d),
                                         "yes" if data.publisher else "no", len(data.gui_gaps)))
    write("(episode = submit gap over %.0f ms that is not an idle keep-alive gap; all times local; "
          "ms unless noted)" % gap_ms)

    all_episodes = {}
    idle_ignored = 0
    for panel in data.panels:
        write()
        write("--- panel %s (rx source: %s) ---" % (panel.name, panel.rx_name or "none found"))
        submits = stage_events(panel, data).get("submit", [])
        gaps = classify_gaps(panel, data, gap_us)
        if len(submits) >= 2:
            for line in summary_lines(submits, gaps, gap_ms):
                write(line)
            idle_ignored += gap_totals(gaps)[IDLE][0]
        else:
            write("  fewer than two submitted frames: no gaps to analyse.")
        write(format_table("  per-stage latency", latency_rows(panel, data)))
        outcomes = {}
        for r in panel.rows:
            outcomes[r.get("outcome")] = outcomes.get(r.get("outcome"), 0) + 1
        write("  capture ticks by outcome: " + ", ".join("%s=%d" % kv for kv in sorted(
            outcomes.items(), key=lambda kv: str(kv[0]))))
        if panel.rx:
            dropped = sum(1 for r in panel.rx if r.get("outcome") == "superseded")
            write("  messages received %d, decoded %d, superseded before decode %d" % (
                len(panel.rx), sum(1 for r in panel.rx if r.get("outcome") == "decoded"), dropped))
        all_episodes[panel.name] = find_episodes(panel, data, gap_us, gaps)

    if data.publisher:
        write()
        write(format_table("--- publisher (publish_video_file.py timing_csv)", publisher_rows(data.publisher)))
    runs = render3d_runs(data)
    write()
    write("--- 3D stereo stream(s) ---")
    if runs or data.render3d:
        durations = [(e - s) for s, e, _ in runs]
        write(format_table("  begin-frame wait", [("not_ready run duration", summarize(durations))]))
        subs = []
        slow = 0
        for rows in data.render3d.values():
            subs += [r["end_us"] - r["start_us"] for r in rows
                     if r.get("kind") == "submit" and r.get("start_us") is not None and r.get("end_us") is not None]
            slow += sum(1 for r in rows if r.get("kind") == "slow_poll")
        write(format_table("  submit", [("consume -> show_frame returned", summarize(subs))]))
        long_runs = [(s, e, p) for s, e, p in runs if e - s >= gap_us]
        polled = set(polled_runs(runs))      # judged against the typical poll rate of ALL runs
        starved = sum(1 for run in long_runs if run not in polled)
        write("  long not_ready runs (>= %.0f ms): %d (%d of them with almost no polls inside - the "
              "polling thread itself was not running); slow try_begin calls: %d" % (
                  gap_ms, len(long_runs), starved, slow))
    else:
        write("  no render3d*.csv recorded.")
    if data.gui_gaps:
        durations = [e - s for s, e in data.gui_gaps]
        write(format_table("--- Qt render-tick gaps (RViz GUI thread, > 25 ms)", [("gap", summarize(durations))]))
    else:
        write("--- Qt render-tick gaps: none over 25 ms recorded (or gui_gaps.csv missing).")

    write()
    write("=== stall episodes (submit gap > %.0f ms, idle keep-alive gaps excluded) ===" % gap_ms)
    total = 0
    verdicts = {}
    onsets_all = []
    for name, episodes in all_episodes.items():
        write()
        write("panel %s: %d episode(s)%s" % (name, len(episodes), "" if episodes else
                                              " - no stall episode"))
        for ep in episodes[:max_episodes]:
            stages = ", ".join("%s@%s" % (s, clock(ep.gapped[s])) for s in STAGES if s in ep.gapped) or "-"
            write("  %s +%6.0f ms  first quiet stage: %-8s  3D starved %3.0f%%  gui stalled %3.0f%%  -> %s" % (
                clock(ep.start), ep.duration_ms, ep.first or "?", 100 * ep.three_d, 100 * ep.gui, ep.verdict))
            write("      %s" % ep.detail)
            write("      quiet stages (onset): %s" % stages)
        if len(episodes) > max_episodes:
            write("  ... %d more not listed (--max-episodes)" % (len(episodes) - max_episodes))
        for ep in episodes:
            total += 1
            verdicts[ep.verdict] = verdicts.get(ep.verdict, 0) + 1
            onsets_all.append(ep.start)

    write()
    write("=== periodicity ===")
    for name, episodes in all_episodes.items():
        write(_period_line(name, [ep.start for ep in episodes]))
    if len(all_episodes) > 1:
        write(_period_line("all panels", onsets_all))

    write()
    write("=== verdict ===")
    if total == 0 and idle_ignored:
        write("No stall episode: %d submit gap(s) were idle keep-alive gaps, in which no frame was "
              "waiting, and no other submit gap was over %.0f ms." % (idle_ignored, gap_ms))
    elif total == 0:
        write("No stall episode.")
        write("No submit gap over %.0f ms: the video was submitted continuously in this recording." % gap_ms)
    else:
        write("%d episode(s): " % total + ", ".join("%d %s" % (n, v) for v, n in sorted(verdicts.items())))
        ours = verdicts.get(OUR, 0)
        if ours == 0:
            write("None of them started in our pipeline (decode, GUI capture or submit).")
        else:
            write("%d episode(s) started in OUR pipeline - see the details above." % ours)
    return all_episodes


def _period_line(label, onsets):
    if len(onsets) < 3:
        return "  %s: %d episode(s): too few to find a period" % (label, len(onsets))
    found = dominant_period(onsets)
    if not found:
        return "  %s: %d episodes, no dominant period (not periodic)" % (label, len(onsets))
    period, fits, total = found
    return ("  %s: %d episodes, dominant period %.3f s (%d of %d spacings are a whole multiple within "
            "+-50 ms; worst onset %.0f ms off the fitted grid)" % (
                label, len(onsets), period, fits, total, grid_deviation_ms(onsets, period)))


# ---------------------------------------------------------------------------------------------
# self-test: fabricated CSVs with a known cause per scenario
# ---------------------------------------------------------------------------------------------
T0 = 1_790_000_000_000_000          # an arbitrary epoch origin
FRAME_US = 33_333                   # 30 fps video
STALL_AT = (5_000_000, 20_000_000, 35_000_000, 50_000_000)   # a strict 15 s grid
RUN_US = 60_000_000


def _write_csv(path, header, rows):
    with open(path, "w", newline="", encoding="utf-8") as handle:
        handle.write(",".join(header) + "\n")
        for row in rows:
            handle.write(",".join("" if v is None else str(v) for v in row) + "\n")


def _in_any(t, windows):
    return any(a <= t < b for a, b in windows)


def synthesize(directory, scenario, lead_us=0, tail_us=0, edge=None):
    """Write a 60 s recording whose only anomaly is the one @scenario names.

    scenario: "source"   - the publisher stops for 700 ms at each stall time
              "pipeline" - the GUI thread stops for 600 ms (no render ticks, no 3D polls)
              "qaros"    - QarOS offers no destination for 800 ms (video AND 3D)
              "clean"    - nothing goes wrong

    The last three parameters make the panel behave like one that is open before and after the
    video: it polls while no frame comes and submits its last picture again every second.
      lead_us  the panel is open this long before the first frame arrives
      tail_us  the publisher stays silent this long after the 60 s of video
      edge     "late-last"   the GUI thread stops for 450 ms when the last frame arrives
               "late-first"  QarOS offers the video no destination for 400 ms when the first
                             frame arrives
               "refused"     QarOS offers the video no destination from 3 s to 6 s into the tail
    """
    rng = random.Random(7)
    stall = {"source": 700_000, "pipeline": 600_000, "qaros": 800_000, "clean": 0}[scenario]
    windows = [(T0 + lead_us + s, T0 + lead_us + s + stall) for s in STALL_AT] if stall else []
    idle_model = bool(lead_us or tail_us or edge)
    end_us = lead_us + RUN_US + tail_us

    publisher, rx, panel = [], [], []
    seq = 0
    t = lead_us
    pub_seq = 0
    frames = []
    while t < lead_us + RUN_US:
        due = T0 + t
        t += FRAME_US
        if scenario == "source" and _in_any(due, windows):
            continue                                   # the publisher does not publish
        pub_seq += 1
        read_start = due + rng.randint(50, 400)
        stamp = read_start + rng.randint(2000, 4000)
        publish = stamp + rng.randint(4000, 8000)
        publisher.append((pub_seq, stamp, due, read_start, publish, publish + rng.randint(8000, 12000)))
        arrival = publish + 8000 + rng.randint(-1000, 1000)
        decode_start = arrival + rng.randint(100, 500)
        decode_done = decode_start + 12000 + rng.randint(-2000, 2000)
        mailbox = decode_done + 50
        seq += 1
        frames.append((seq, stamp, arrival, decode_start, decode_done, mailbox))

    # when the GUI thread is stopped, and when QarOS offers the video no destination
    gui_stops = list(windows) if scenario == "pipeline" else []
    refused = list(windows) if scenario == "qaros" else []
    if edge == "late-last":
        gui_stops.append((frames[-1][5] - 3000, frames[-1][5] + 450_000))
    elif edge == "late-first":
        refused.append((frames[0][5] - 3000, frames[0][5] + 400_000))
    elif edge == "refused":
        refused.append((T0 + lead_us + RUN_US + 3_000_000, T0 + lead_us + RUN_US + 6_000_000))

    # the GUI render tick: every 8 ms, except while the GUI thread is stopped
    ticks = []
    tick = T0
    while tick < T0 + end_us + 1_000_000:
        tick += 8000
        if _in_any(tick, gui_stops):
            continue
        ticks.append(tick)
    gui_gaps = []
    for a, b in zip(ticks, ticks[1:]):
        if b - a > 25000:
            gui_gaps.append((a, b, b - a))

    # capture: a tick that finds a frame it has not captured yet takes the NEWEST one (frames
    # visible but older were overwritten in the mailbox - frame skipping, as in the plugin)
    fi = 0
    not_ready_start = None
    not_ready_count = 0
    last_frame = None       # the frame of the latest capture
    last_submit = None
    last_poll = None
    for tk in ticks:
        newest = None
        while fi < len(frames) and frames[fi][5] <= tk:
            newest = frames[fi]
            fi += 1
        if newest is None:
            # A panel whose source has been quiet for 0.1 s polls about every 32 ms and, when a
            # second has passed since its last submit, sends the last picture again.
            if not idle_model or (last_frame is not None and tk - last_frame[5] < 100_000):
                continue
            if last_poll is not None and tk - last_poll < 32_000:
                continue
            last_poll = tk
            capture = tk + 3000 + rng.randint(0, 500)
            if last_frame is None:
                cells = (None,) * 5 + (0,)
            else:
                f = last_frame
                cells = (f[0], f[1], f[2], f[4], f[5], f[0])
            if last_submit is not None and tk - last_submit < 1_000_000:
                panel.append(cells + (tk, capture, None, None, None, None, None, "unchanged"))
            elif _in_any(tk, refused):
                if not_ready_start is None:
                    not_ready_start = capture
                not_ready_count += 1
                panel.append(cells + (tk, capture, None, not_ready_start, capture,
                                      not_ready_count, None, "not_ready"))
            else:
                ready = capture + 200
                submit = ready + 2000 + rng.randint(0, 400)
                nr = (None, None, None)
                if not_ready_start is not None:
                    nr = (not_ready_start, ready, not_ready_count)
                not_ready_start, not_ready_count = None, 0
                last_submit = submit
                panel.append(cells + (tk, capture, ready) + nr + (submit, "submitted"))
            continue
        last_frame = newest
        (fseq, stamp, arrival, decode_start, decode_done, mailbox) = newest
        decode_seq = fseq
        capture = tk + 3000 + rng.randint(0, 500)
        if _in_any(tk, refused):
            outcome, ready, submit = "not_ready", None, None
            if not_ready_start is None:
                not_ready_start = capture
            not_ready_count += 1
            panel.append((fseq, stamp, arrival, decode_done, mailbox, decode_seq, tk, capture, None,
                          not_ready_start, capture, not_ready_count, None, outcome))
        else:
            ready = capture + 200
            submit = ready + 2000 + rng.randint(0, 400)
            nr = (not_ready_start, ready, not_ready_count) if not_ready_start is not None else (None, None, None)
            not_ready_start, not_ready_count = None, 0
            last_submit = submit
            panel.append((fseq, stamp, arrival, decode_done, mailbox, decode_seq, tk, capture, ready,
                          nr[0], nr[1], nr[2], submit, "submitted"))

    for (fseq, stamp, arrival, decode_start, decode_done, mailbox) in frames:
        rx.append((fseq, stamp, arrival, decode_start, decode_done, mailbox, fseq, "decoded"))

    # the 3D stream: ~60 Hz begin-frame waits (16 ms, polled ~4000 times a second)
    r3d = []
    now = T0
    frame_no = 0
    while now < T0 + end_us:
        start = now
        end = start + 16_000
        polls = 64
        for a, b in windows:
            if scenario == "qaros" and a <= start < b:
                end = b                           # QarOS offers nothing until the stall is over
                polls = max(1, (end - start) // 250)   # ...and the thread keeps polling
            elif scenario == "pipeline" and start <= a < end:
                end = b + 1000                    # the GUI thread stops inside this wait...
                polls = 100                       # ...so almost no polls happen in it
        r3d.append(("not_ready", start, end, polls, None, None, None))
        r3d.append(("ready", end, end, 1, None, 0, None))
        frame_no += 1
        r3d.append(("submit", end, end + 4000, 1, frame_no, 1, end + 2500))
        now = end + 4000

    os.makedirs(directory, exist_ok=True)
    _write_csv(os.path.join(directory, "publisher.csv"),
               ("seq", "stamp_us", "scheduled_us", "read_start_us", "publish_us", "publish_end_us"), publisher)
    _write_csv(os.path.join(directory, "rx_cam_image_raw.csv"),
               ("seq", "header_stamp_us", "arrival_us", "decode_start_us", "decode_done_us", "mailbox_us",
                "decode_seq", "outcome"), rx)
    _write_csv(os.path.join(directory, "video_cam_image_raw.csv"),
               ("seq", "header_stamp_us", "arrival_us", "decode_done_us", "mailbox_us", "decode_seq", "tick_us",
                "capture_us", "ready_us", "not_ready_start_us", "not_ready_end_us", "not_ready_count",
                "submit_us", "outcome"), panel)
    _write_csv(os.path.join(directory, "render3d.csv"),
               ("kind", "start_us", "end_us", "count", "frame", "detail", "mid_us"), r3d)
    _write_csv(os.path.join(directory, "gui_gaps.csv"), ("start_us", "end_us", "gap_us"), gui_gaps)


def _check(condition, message, failures):
    if not condition:
        failures.append(message)


def _fabricate(scenario, sink, drop_rx=False, **options):
    """Fabricate a recording and analyse it: (data, the episodes of its panel, its gaps)."""
    with tempfile.TemporaryDirectory() as tmp:
        synthesize(tmp, scenario, **options)
        if drop_rx:
            os.remove(os.path.join(tmp, "rx_cam_image_raw.csv"))
        data = load_dir(tmp)
    episodes = analyze(data, out=sink).get("cam_image_raw", [])
    return data, episodes, classify_gaps(data.panels[0], data, 100_000)


def _idle_cases(sink):
    """Recordings of a panel that is open before and after the video: it polls while no frame
    comes and sends its last picture again every second. Those submits are no stalls, and the
    stalls beside them are still found. Returns the list of failures."""
    failures = []

    def check(condition, message):
        _check(condition, message, failures)

    def rate(totals):
        n, seconds = totals[ACTIVE]
        return n / seconds if seconds else 0.0

    expected = {"source": SOURCE, "pipeline": OUR, "qaros": QAROS}
    first_stage = {"source": "publish", "pipeline": "capture", "qaros": "ready"}
    tail = 20_000_000

    # The video ends and the panel submits the last picture again for 20 s.
    report = io.StringIO()
    data, episodes, gaps = _fabricate("clean", report, tail_us=tail)
    totals = gap_totals(gaps)
    check(not episodes, "idle tail: expected no stall episode, got %d" % len(episodes))
    check("idle keep-alive gaps ignored: " in report.getvalue()
          and "=== verdict ===\nNo stall episode" in report.getvalue(),
          "idle tail: the report must name the idle gaps ignored and say that there is no episode")
    check(59.0 <= totals[ACTIVE][1] <= 61.0 and 29.5 <= rate(totals) <= 30.5,
          "idle tail: active part %.1f s at %.1f fps, expected 60 s at 30 fps" % (
              totals[ACTIVE][1], rate(totals)))
    check(19 <= totals[IDLE][0] <= 22 and 19.0 <= totals[IDLE][1] <= 22.0,
          "idle tail: %d idle gaps (%.1f s), expected 20 (20 s)" % totals[IDLE])
    check(totals[BLOCKED][0] == 0, "idle tail: %d blocked gaps" % totals[BLOCKED][0])

    # The stalls inside the video are still found, with the verdict of their cause. Without the
    # rx file the rows of the panel itself say that frames arrived.
    for scenario, drop_rx in (("source", False), ("pipeline", False), ("qaros", False),
                              ("qaros", True)):
        label = "%s with an idle tail%s" % (scenario, ", no rx file" if drop_rx else "")
        data, episodes, gaps = _fabricate(scenario, sink, drop_rx=drop_rx, tail_us=tail)
        check(len(episodes) == len(STALL_AT), "%s: expected %d episodes, got %d" % (
            label, len(STALL_AT), len(episodes)))
        for ep in episodes:
            check(ep.verdict == expected[scenario] and ep.first == first_stage[scenario],
                  "%s: episode at %s judged %s from %s, expected %s from %s (%s)" % (
                      label, clock(ep.start), ep.verdict, ep.first, expected[scenario],
                      first_stage[scenario], ep.detail))
            check(not ep.blocked and not ep.after_idle, "%s: a stall counted as idle" % label)
        period = dominant_period([ep.start for ep in episodes])
        check(period is not None and abs(period[0] - 15.0) < 0.05,
              "%s: dominant period %s, expected 15.0 s" % (label, period))
        idle_n = gap_totals(gaps)[IDLE][0]
        check(19 <= idle_n <= 22, "%s: %d idle gaps, expected 20" % (label, idle_n))

    # The end of the video: the last frame arrives, the GUI thread stops, and the submit is late.
    # That is a stall of the last interval, and the keep-alive submits after it are not.
    data, episodes, gaps = _fabricate("clean", sink, tail_us=tail, edge="late-last")
    last_arrival = max(r["arrival_us"] for r in data.rx["cam_image_raw"])
    check(len(episodes) == 1, "late last frame: expected 1 episode, got %d" % len(episodes))
    for ep in episodes:
        check(ep.verdict == OUR and ep.start < last_arrival and ep.end - last_arrival >= 440_000,
              "late last frame: episode %s +%.0f ms judged %s, expected the last interval (%s)" % (
                  clock(ep.start), ep.duration_ms, ep.verdict, ep.detail))
    idle_n = gap_totals(gaps)[IDLE][0]
    check(19 <= idle_n <= 22, "late last frame: %d idle gaps, expected 20" % idle_n)

    # The start of the video after 5.4 s of idle submits: the first frame is submitted at once,
    # so there is no episode (the 400 ms or so before it belong to the source).
    for drop_rx in (False, True):
        label = "start of the video%s" % (", no rx file" if drop_rx else "")
        data, episodes, gaps = _fabricate("clean", sink, drop_rx=drop_rx, lead_us=5_400_000)
        totals = gap_totals(gaps)
        check(not episodes, "%s: expected no stall episode, got %d" % (label, len(episodes)))
        check(totals[IDLE][0] >= 5 and any(g.after_idle for g in gaps),
              "%s: the idle gaps before the first frame were not recognised (%d)" % (
                  label, totals[IDLE][0]))
        check(59.0 <= totals[ACTIVE][1] <= 61.0 and 29.5 <= rate(totals) <= 30.5,
              "%s: active part %.1f s at %.1f fps, expected 60 s at 30 fps" % (
                  label, totals[ACTIVE][1], rate(totals)))

    # The first frame waits 400 ms for QarOS: a stall that counts from the arrival of the frame,
    # not from the keep-alive submit before it.
    for drop_rx in (False, True):
        label = "late first frame%s" % (", no rx file" if drop_rx else "")
        data, episodes, gaps = _fabricate("clean", sink, drop_rx=drop_rx, lead_us=5_400_000,
                                          edge="late-first")
        first_arrival = min(r["arrival_us"] for r in data.panels[0].rows
                            if r.get("arrival_us") is not None)
        check(len(episodes) == 1, "%s: expected 1 episode, got %d" % (label, len(episodes)))
        for ep in episodes:
            check(ep.verdict == QAROS and ep.after_idle and ep.start == first_arrival
                  and 380 <= ep.duration_ms <= 480,
                  "%s: episode %s +%.0f ms judged %s (%s), expected %s from the arrival %s" % (
                      label, clock(ep.start), ep.duration_ms, ep.verdict, ep.detail, QAROS,
                      clock(first_arrival)))

    # A keep-alive that QarOS refuses for 4 s while no frame is waiting is an episode of its own
    # kind, and it is not part of the active part.
    data, episodes, gaps = _fabricate("clean", sink, tail_us=tail, edge="refused")
    totals = gap_totals(gaps)
    check(len(episodes) == 1, "refused keep-alive: expected 1 episode, got %d" % len(episodes))
    for ep in episodes:
        check(ep.blocked and ep.verdict == QAROS and 3500 <= ep.duration_ms <= 4600,
              "refused keep-alive: episode %s +%.0f ms judged %s (%s)" % (
                  clock(ep.start), ep.duration_ms, ep.verdict, ep.detail))
    check(totals[BLOCKED][0] == 1 and 29.5 <= rate(totals) <= 30.5,
          "refused keep-alive: %d blocked gaps, active rate %.1f fps, expected 1 and 30" % (
              totals[BLOCKED][0], rate(totals)))
    return failures


def self_test(verbose=False):
    """Fabricate recordings with a known cause and assert the analyser names it. Returns the
    list of failures (empty = pass)."""
    failures = []
    expected = {"source": SOURCE, "pipeline": OUR, "qaros": QAROS}
    first_stage = {"source": "publish", "pipeline": "capture", "qaros": "ready"}
    for scenario in ("source", "pipeline", "qaros", "clean"):
        with tempfile.TemporaryDirectory() as tmp:
            synthesize(tmp, scenario)
            data = load_dir(tmp)
            sink = sys.stdout if verbose else open(os.devnull, "w")
            try:
                result = analyze(data, out=sink)
            finally:
                if sink is not sys.stdout:
                    sink.close()
            episodes = result.get("cam_image_raw", [])
            if scenario == "clean":
                _check(not episodes, "clean: expected no stall episodes, got %d" % len(episodes), failures)
                continue
            _check(len(episodes) == len(STALL_AT),
                   "%s: expected %d episodes, got %d" % (scenario, len(STALL_AT), len(episodes)), failures)
            for ep in episodes:
                _check(ep.verdict == expected[scenario], "%s: episode at %s judged %s, expected %s (%s)" % (
                    scenario, clock(ep.start), ep.verdict, expected[scenario], ep.detail), failures)
                _check(ep.first == first_stage[scenario], "%s: first quiet stage %s, expected %s" % (
                    scenario, ep.first, first_stage[scenario]), failures)
                _check(ep.duration_ms > 100, "%s: episode shorter than the gap threshold" % scenario, failures)
            if scenario == "qaros":
                _check(all(ep.three_d >= 0.9 for ep in episodes),
                       "qaros: the 3D stream must be seen starved during every episode", failures)
            if scenario == "pipeline":
                _check(all(ep.gui >= 0.5 for ep in episodes),
                       "pipeline: the GUI-thread stall must be seen in gui_gaps", failures)
                _check(all(ep.three_d < 0.5 for ep in episodes),
                       "pipeline: 3D not_ready runs with no polls inside must NOT count as back-pressure", failures)
            if scenario == "source":
                _check(all(ep.three_d < 0.5 for ep in episodes),
                       "source: the 3D stream was not starved", failures)
            period = dominant_period([ep.start for ep in episodes])
            _check(period is not None and abs(period[0] - 15.0) < 0.05,
                   "%s: dominant period %s, expected 15.0 s" % (scenario, period), failures)
            if scenario == "source":
                # latency tables: fabricated delays are publish->arrival ~8 ms, arrival->decode ~12 ms
                rows = dict(latency_rows(data.panels[0], data))
                pa = rows.get("publisher -> arrival")
                ad = rows.get("arrival -> decode done")
                cs = rows.get("capture -> submit")
                _check(pa is not None and 7.0 <= pa[1] <= 9.0, "source: publisher->arrival p50 %s, expected ~8 ms" % (pa,), failures)
                _check(ad is not None and 11.0 <= ad[1] <= 13.5, "source: arrival->decode p50 %s, expected ~12.3 ms" % (ad,), failures)
                _check(cs is not None and 2.0 <= cs[1] <= 3.0, "source: capture->submit p50 %s, expected ~2.4 ms" % (cs,), failures)
            if scenario == "qaros":
                runs = not_ready_runs(data.panels[0].rows)
                _check(bool(runs) and max(runs) >= 700000,
                       "qaros: the video not_ready run should last ~800 ms, got %s" % (
                           max(runs) / 1000.0 if runs else None), failures)
    # Onsets that are scattered have no dominant period (a period within the tolerance would fit
    # any spacing).
    scattered = [0, 140_000, 250_000, 1_900_000, 2_050_000, 7_300_000, 7_450_000]
    _check(dominant_period(scattered) is None,
           "periodicity: scattered onsets have no dominant period, got %s" % (
               dominant_period(scattered),), failures)
    sink = sys.stdout if verbose else open(os.devnull, "w")
    try:
        failures += _idle_cases(sink)
    finally:
        if sink is not sys.stdout:
            sink.close()
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Attribute AR-video stutter to a pipeline stage from the plugin's timing CSVs.")
    parser.add_argument("directory", nargs="?", help="timing folder written with -TimingCsv (QAR_VIDEO_TIMING_DIR)")
    parser.add_argument("--publisher", help="publisher CSV (default: <directory>/publisher.csv if present)")
    parser.add_argument("--gap-ms", type=float, default=100.0, help="submit gap that counts as a stall (default 100)")
    parser.add_argument("--max-episodes", type=int, default=25, help="episodes listed per panel (default 25)")
    parser.add_argument("--self-test", action="store_true",
                        help="fabricate recordings with a known cause and check the verdicts, then exit")
    parser.add_argument("--verbose", action="store_true", help="with --self-test: print the full reports")
    args = parser.parse_args(argv)

    if args.self_test:
        failures = self_test(verbose=args.verbose)
        if failures:
            print("SELF-TEST FAILED:")
            for failure in failures:
                print("  - " + failure)
            return 1
        print("SELF-TEST PASSED: 4 fabricated recordings (source stall, GUI-thread stall, QarOS "
              "back-pressure, clean) attributed correctly; latency tables and the 15 s period "
              "recovered; idle keep-alive submits at the end and at the start of the video "
              "ignored, the stalls beside them still found.")
        return 0
    if not args.directory:
        parser.error("give the timing folder, or --self-test")
    if not os.path.isdir(args.directory):
        parser.error("not a folder: %s" % args.directory)
    data = load_dir(args.directory, args.publisher)
    analyze(data, gap_ms=args.gap_ms, max_episodes=args.max_episodes)
    return 0


if __name__ == "__main__":
    sys.exit(main())
