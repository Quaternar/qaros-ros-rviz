"""Regenerate a rosbag2 metadata.yaml from a sqlite3 .db3 whose index is missing.

`ros2 bag reindex` does this itself, but it dies silently on the larger of the two
bags, so this reproduces its output from the same source of truth: the db's own
topics/messages tables.
"""
import os
import sqlite3
import sys

db = sys.argv[1]
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


# /tf_static is latched: it is published at the start of the bag, and every
# consumer expects to receive it whenever it subscribes. That requires
# TRANSIENT_LOCAL durability (2 = VOLATILE would be wrong). Declaring it VOLATILE
# is not merely lossy - RViz subscribes TRANSIENT_LOCAL, the two are INCOMPATIBLE,
# so no static transform ever arrives and nothing referencing those frames can be
# drawn. Everything else is fine as VOLATILE.
TRANSIENT_LOCAL, VOLATILE = 1, 2
LATCHED = {'/tf_static'}
# ...and the depth must cover EVERY latched message, because a TRANSIENT_LOCAL
# publisher replays only its last `depth` samples to a late joiner. These bags
# send their static transforms as six separate messages, so depth 1 would
# deliver one of six and orphan the frames carried by the other five.
LATCHED_DEPTH = 100

uri = 'file:///' + db.replace(os.sep, '/') + '?mode=ro'
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
