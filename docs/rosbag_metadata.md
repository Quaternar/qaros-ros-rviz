# Regenerating a rosbag2 `metadata.yaml`

A rosbag2 bag is a directory holding one or more `.db3` files **plus** a
`metadata.yaml` index. Without the index, every rosbag2 command fails with:

```
Could not find metadata in bag directory <dir>
```

Bags that arrive as a bare `.db3` (copied out of a recording, unpacked from an
archive) are missing it. The normal fix is:

```powershell
. .\scripts\env_setup.ps1
$ros2 = 'D:\development\ros2_humble\ros2-windows\Scripts\ros2-script.py'
python $ros2 bag reindex '<bag dir>' -s sqlite3
```

Call `ros2-script.py` through `python`, not `ros2.exe` — the distro's `.exe`
wrappers embed a bare `#!python.exe` shebang and no `python.exe` sits beside
them, so they fail with `failed to create process`.

## When `reindex` gives up

Observed 2026-08-22 on `D:\development\rosbags\Trayectory2` (3.4 GB, 71 556
messages): `reindex` prints `Beginning reindexing bag in directory: …` and then
exits without writing anything and without an error. The same command succeeded
on a 1.8 GB bag in the same session, so size is the only obvious difference —
both bags carry an identical schema (`schema` table version 3, `humble`).

Nothing is actually lost when this happens. The index is a summary of data the
`.db3` already holds: its `topics` table has every topic name, type and
serialization format, and its `messages` table has the per-topic counts and the
timestamp range. So the index can be rebuilt directly from the bag:

```powershell
python scripts\rosbag_write_metadata.py '<bag dir>\<file>.db3'
```

Verify the result the same way rosbag2 will read it:

```powershell
python $ros2 bag info '<bag dir>'
```

## QoS is part of the index — and losing it makes bags render nothing

`offered_qos_profiles` lives in `metadata.yaml`, so a bag that lost its index
also lost the QoS its topics were published with. That matters more than it
sounds: where a publisher's QoS is incompatible with a subscriber's, DDS
delivers **nothing**, and RViz reports no error — the display simply stays
empty.

Two rules when regenerating:

* **`/tf_static` must be `TRANSIENT_LOCAL`.** It is published at the start of
  the bag. RViz subscribes TRANSIENT_LOCAL; if the bag offers VOLATILE the two
  are incompatible, no static transform ever arrives, and every display whose
  frame depends on one stays blank.
* **`/tf_static` depth must cover every latched message, not 1.** A
  TRANSIENT_LOCAL publisher replays only its last `depth` samples to a late
  joiner. These bags emit their static transforms as **six separate messages**,
  so `depth: 1` — which is what `ros2 bag reindex` writes — delivers one of six.
  The surviving frames look fine while the rest are silently orphaned, and the
  point clouds stamped in them are dropped with
  `Message Filter dropping message ... queue is full`. Use a depth comfortably
  above the number of latched messages (this repo uses 100).
* **Prefer `RELIABLE` when the original is unknown.** A RELIABLE publisher can
  serve both RELIABLE and BEST_EFFORT subscribers; a BEST_EFFORT publisher
  cannot serve a RELIABLE subscriber at all.

`scripts/rosbag_write_metadata.py` applies both. For bags whose index is intact
but whose recorded QoS is incompatible with RViz anyway — e.g. Ouster topics
recorded BEST_EFFORT — override at playback instead:

```powershell
python $ros2 bag play '<bag dir>' --qos-profile-overrides-path scripts\rosbag_qos_override.yaml
```

The player names the problem on stderr, and it is the only place it is
reported:

```
New subscription discovered on topic '/ouster/points', requesting incompatible QoS.
No messages will be sent to it. Last incompatible policy: RELIABILITY_QOS_POLICY
```

## Harmless warnings

`ros2 bag play` and `bag info` may print

```
Path is not a file: <bag dir>\<file>.db3
```

while still opening the database and replaying normally. It comes from
rosbag2's bag-size probe, not from the read path; `Bag size:` in `bag info` is
correspondingly wrong. Ignore it — the playback itself is unaffected.
