# Copyright 2026 Quaternar
# SPDX-License-Identifier: MIT
"""Regenerate a rosbag2 metadata.yaml from a sqlite3 .db3 whose index is missing.

`ros2 bag reindex` does this itself, but it can die silently on a large bag, so
this rebuilds the index from the same source of truth: the db's own
topics/messages tables. Reindex copies the QoS profile that the recorder stored
in the database; this script chooses one instead: RELIABLE and VOLATILE with
depth 10 for every topic, TRANSIENT_LOCAL with depth 100 for the latched ones.
"""
import argparse
import os
import pathlib
import sqlite3
import sys

parser = argparse.ArgumentParser(
    description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument(
    'db', metavar='DB3_FILE',
    help='the .db3 file; metadata.yaml is written into the folder that holds it')
db = parser.parse_args().db
if not os.path.isfile(db):
    sys.exit('%s: error: not a file: %s' % (parser.prog, db))
bag_dir = os.path.dirname(db)
rel = os.path.basename(db)

def qos(depth, durability):
    """One offered_qos_profiles entry. reliability is always 1 (RELIABLE).

    A RELIABLE publisher can serve both RELIABLE and BEST_EFFORT subscribers,
    while a BEST_EFFORT one cannot serve a RELIABLE subscriber at all - so
    claiming RELIABLE is the permissive choice when the recorded QoS is lost.
    """
    return (
        '- history: 1\\n  depth: %d\\n  reliability: 1\\n  durability: %d\\n'
        '  deadline:\\n    sec: 9223372036\\n    nsec: 854775807\\n'
        '  lifespan:\\n    sec: 9223372036\\n    nsec: 854775807\\n'
        '  liveliness: 1\\n  liveliness_lease_duration:\\n'
        '    sec: 9223372036\\n    nsec: 854775807\\n'
        '  avoid_ros_namespace_conventions: false' % (depth, durability)
    )


# /tf_static and /robot_description are latched: they are published at the
# start of the bag, and every consumer expects to receive them whenever it
# subscribes. That requires TRANSIENT_LOCAL durability (2 = VOLATILE would be
# wrong). Declaring one VOLATILE is not merely lossy: RViz subscribes to
# /tf_static (the transform buffer) and to /robot_description (the RobotModel
# display) with TRANSIENT_LOCAL, and the two are INCOMPATIBLE, so no static
# transform and no robot description ever arrives (the player warns about the
# DURABILITY) and nothing referencing those frames can be drawn. Everything
# else is fine as VOLATILE.
TRANSIENT_LOCAL, VOLATILE = 1, 2
LATCHED = {'/tf_static', '/robot_description'}
# ...and the depth must cover EVERY latched message, because a TRANSIENT_LOCAL
# publisher replays only its last `depth` samples to a late joiner. These bags
# send their static transforms as six separate messages, so depth 1 would
# deliver one of six and orphan the frames carried by the other five. The same
# depth serves /robot_description, which a publisher may repeat.
LATCHED_DEPTH = 100

# The URI is built from the absolute path, so that a relative path and a path with a space,
# a '#' or a '%' open the same file as the path that the caller typed.
uri = pathlib.Path(os.path.abspath(db)).as_uri() + '?mode=ro'
try:
    con = sqlite3.connect(uri, uri=True)
    cur = con.cursor()

    cur.execute('SELECT id, name, type, serialization_format FROM topics')
    topics = cur.fetchall()
    cur.execute('SELECT COUNT(*), MIN(timestamp), MAX(timestamp) FROM messages')
    total, lo, hi = cur.fetchone()

    counts = {}
    for tid, cnt in cur.execute(
            'SELECT topic_id, COUNT(*) FROM messages GROUP BY topic_id'):
        counts[tid] = cnt
except sqlite3.DatabaseError as exc:
    sys.exit('%s: error: cannot read %s as a rosbag2 sqlite3 database: %s'
             % (parser.prog, db, exc))
if not total:
    sys.exit('%s: error: %s holds no messages' % (parser.prog, db))

lines = [
    'rosbag2_bagfile_information:',
    '  version: 5',
    '  storage_identifier: sqlite3',
    '  duration:',
    '    nanoseconds: %d' % (hi - lo),
    '  starting_time:',
    '    nanoseconds_since_epoch: %d' % lo,
    '  message_count: %d' % total,
    '  topics_with_message_count:',
]
for tid, name, ttype, fmt in topics:
    latched = name in LATCHED
    lines += [
        '    - topic_metadata:',
        '        name: %s' % name,
        '        type: %s' % ttype,
        '        serialization_format: %s' % fmt,
        '        offered_qos_profiles: "%s"' % qos(
            LATCHED_DEPTH if latched else 10,
            TRANSIENT_LOCAL if latched else VOLATILE),
        '      message_count: %d' % counts.get(tid, 0),
    ]
lines += [
    '  compression_format: ""',
    '  compression_mode: ""',
    '  relative_file_paths:',
    '    - %s' % rel,
    '  files:',
    '    - path: %s' % rel,
    '      starting_time:',
    '        nanoseconds_since_epoch: %d' % lo,
    '      duration:',
    '        nanoseconds: %d' % (hi - lo),
    '      message_count: %d' % total,
    '',
]

out = os.path.join(bag_dir, 'metadata.yaml')
with open(out, 'w', encoding='utf-8', newline='\n') as f:
    f.write('\n'.join(lines))
print('wrote %s  (%d topics, %d messages)' % (out, len(topics), total))
