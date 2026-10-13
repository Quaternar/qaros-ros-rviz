# Log reference

This page explains the text that a run of the plugin leaves behind: where each stream of text goes, how to tell its writers apart, what each line says and what the fields of the heartbeat line mean. A line that is quoted here has the form that the code gives it, with `%1`, `%2` and so on for the values of the run, and a line that you read in a log has the same words with the values filled in. [Troubleshooting](TROUBLESHOOTING.md#reading-the-logs) says which lines to read first.

## Where the logs go

The panel writes every line that it logs to two places. It appends the line to its `Diagnostics` view, with the time of day in front, and the view keeps the last 2000 lines. It also writes the line to the standard error of RViz after the prefix `[qar-panel]`. Under the launcher the standard error is the file `rviz_live_<stamp>.err.log` and the standard output is `rviz_live_<stamp>.out.log`, both in the log folder, which is `qar_rviz_logs` in `TEMP` unless you pass `-LogDir`. The view does not survive RViz, so the file is the record.

| Destination | What is in it | What controls it |
|---|---|---|
| the `Diagnostics` view | the lines of the panel, with `HH:mm:ss.zzz` in front, the last 2000 | the heartbeat only: `QAR_PANEL_HEARTBEAT_S` |
| the standard error of RViz | the same lines after `[qar-panel]`, and the lines that the libraries of qar_ogre_adapter write straight to it | nothing for the panel; the adapters follow `QAR_ADAPTER_TRACE` for the per-call lines |
| the standard output of RViz | nothing of the panel; the console copy of the QarOS library | `QAR_PANEL_LOG_CONSOLE` |
| the QarOS log folder | the log of the library and of the processes that it starts, and the frame trace | `QAR_PANEL_LOG_DIR`, `QAR_PANEL_LOG_SEVERITY` |
| the folder `QAR_VIDEO_TIMING_DIR` | the timing files | the launcher parameter `-TimingCsv` |
| the output prefix of a scripted mode | the result files of the mode | the variables `QAR_PANEL_AUTO...` and the preview script |

The QarOS log folder is `quaternar` in `TEMP` unless `QAR_PANEL_LOG_DIR` names another, and every process that the library starts writes its own files there. [Where the QarOS library writes its logs](TROUBLESHOOTING.md#where-the-qaros-library-writes-its-logs) explains the folder and [Which severity applies](TROUBLESHOOTING.md#which-severity-applies) the severity. The names of the files carry the role of the process, and the maintainers saw these in sessions with a Visualizer: `streaming_viz_<id>_stdout.log` for a Visualizer, `mixer_<id>_stdout.log` for a mixer that the runtime starts for itself, and `qar_launcher_stdout.log` for the runtime launcher. The runtime reports its own internal peers too, which is why the admission policy exists.

The result files of the capture buttons and of the scripted modes are text files of `key=value` lines, and the first line of each says what the file is. The file `_capture.txt` starts with `"# Qar RViz2 offscreen capture"`, `_stereo.txt` with `"# Qar RViz2 single-peer stereo capture"`, `_autostereo.txt` with `"# Qar RViz2 single-peer stereo AUTOSTEREO summary"`, `_automulti.txt` with `"# Qar RViz2 multi-peer batched render scheduler proof"`, and the file that `QAR_PANEL_AUTODUMP` names with `"# Qar RViz2 streaming panel - auto diagnostics dump"`.

The command lists the files of the QarOS log folder that hold the standard output of a process, by name and with the ending of the name cut off:

```powershell
Get-ChildItem -LiteralPath (Join-Path $env:TEMP 'quaternar') -Filter '*_stdout.log' | ForEach-Object { $_.Name -replace '_stdout\.log$', '' }
```
```text
mixer_1b797fb9-0178-4449-8eb3-e192bad32a3c
mixer_323b42fc-0f3c-4459-9ab0-9a2aad9c203b
qar_launcher
streaming_viz_1b9facce-e064-4c2e-b1d2-5c0201963373
streaming_viz_bf2bb600-7dce-4ded-81f1-9061a69591c4
```

## Channels and prefixes

Every line starts with the prefix of its writer. The first rows come through the panel, which forwards the diagnostics of the libraries to its own log. The last row is written straight to the standard error by the adapters of qar_ogre_adapter 0.1.0, whose document TROUBLESHOOTING describes them in the section "Reading the logs".

| Prefix | Written by | Lines |
|---|---|---|
| `[qar-panel]` | the panel | everything below that has a keyword in front |
| `"[lib:"` | the manager of qar_ogre_streaming, through the sink of the panel | the creation, activation and destruction of the 3D streams, and the lighting |
| `"[qt_lib:"` | the manager of qar_qt_streaming, through the sink of the panel | the lifecycle of the AR panels of Qt windows, with the key of the panel |
| `"[ros_ui:"` | qar_ros_ui, through the sink of the panel | the diagnostics of the graph discovery and of the bridges |
| `[qar-web]` | the panel | the viewport that a web page reports |
| `[qar-adapter]`, `[qar-panel-adapter]`, `[qar-url-panel-adapter]`, `[qar-panel-input]`, `[qar-spatial]`, `[qar-timing]` | the adapters and the timing log of qar_ogre_adapter | the calls into QarOS, the creation and close of panels, the gestures, and a timing file that could not be created |

The lines of the panel itself start with a keyword, and the sections of this page follow the keywords:

| Keyword | Section |
|---|---|
| `HB` | [The heartbeat line](#the-heartbeat-line) |
| `LIVE:`, `"QarOS:"`, `"Connected to QarOS:"`, `GEO:`, `NOTE:` | [Connect, invite and session lines](#connect-invite-and-session-lines) |
| `"AR PANEL:"` | [AR panel lines](#ar-panel-lines) and [Hidden Displays panel lines](#hidden-displays-panel-lines) |
| `TOPICS:`, `GRAPH:`, `"ROS UI:"`, `"AR TOOL:"`, `"AR CONTROL:"` | [AR tool and web lines](#ar-tool-and-web-lines) |
| `"AR VIDEO:"` | [Video lines](#video-lines) |
| `SPATIAL:` | [Spatial lines](#spatial-lines) |
| `"Floor plane:"`, `SCHED:`, `TIMING:`, `"Render tick:"`, `"Prime next frame:"`, `"Scene lighting:"` | [Floor plane, scheduling and timing lines](#floor-plane-scheduling-and-timing-lines) |
| `ERROR:` | an error that the panel also shows in `"Last error:"` |

## The heartbeat line

The panel writes one line every two seconds, and `QAR_PANEL_HEARTBEAT_S` sets the period; `0` switches it off. It is the one line that tells you whether the GUI thread of RViz was blocked, so read it first. The line has this form, and the parts after the first are appended in this order, so that a program that reads the earlier fields keeps working:

```text
HB dt=%1s ticks=%2 tick_fps=%3 max_gap=%4ms
streams=%1 batch_avg=%2ms budget_skips=%3
| %1 fps=%2 sub=%3 skip=%4 rend=%5ms
why=%1[nr=%2 err=%3 tgt=%4 bud=%5]
tick=%1 prime=%2
rr=%1/%2
|| panels=%1 live=%2 closed=%3 notice=%4
arclosed=%1
CLOSEFAIL=%1
CAPPED=%1
| %1%2 sub=%3
UNSERVED %1s nr=%2 stream=%3
|| vdisp %1
```

Each line of the block is one part of the heartbeat. The fields, in the order of the line:

| Field | What it says |
|---|---|
| `dt` | the seconds since the previous heartbeat |
| `ticks`, `tick_fps` | the number of times that the render timer fired in that window and the number per second of wall time; a value of about 32 for a period of 16 ms or less is the symptom of a timer that Windows serves from its coarse tick |
| `max_gap` | the longest gap between two ticks since the previous heartbeat, in milliseconds; a gap of hundreds of milliseconds means that the GUI thread of RViz was blocked |
| `streams`, `batch_avg`, `budget_skips` | the number of 3D streams, the mean time of a batch render, and the frames that the render budget shed |
| one field for each 3D stream | after a bar: the first eight hexadecimal digits of the peer identifier, the achieved frame rate `fps`, the frames submitted `sub` and skipped `skip`, and the mean render time `rend` |
| `why` | only while frames were skipped: the last reason, one of `none`, `sender_not_ready`, `sender_error`, `target_init_failed` and `budget_shed`, with the totals for not ready `nr`, sender error `err`, target initialisation `tgt` and budget `bud`; the detail that the adapter gives follows in parentheses |
| `tick`, `prime` | the render tick in milliseconds and the prime switch, 1 or 0 |
| `rr` | the rows of the frame trace whose pose changed between its reads, out of all rows; only while the frame trace is on |
| `panels`, `live`, `closed`, `notice` | after two bars: the AR panels, those that show a window, those that were closed and destroyed, and those that show a notice; the field stays once a panel has been closed |
| `arclosed`, `"CLOSEFAIL"`, `"CAPPED"` | the panels that the operator closed in AR, the failures of the close call, and the creations that the tombstone cap refused; each appears only when it is not 0 |
| one field for each AR panel | after a bar: its key, the marker `[NOTICE]`, `[ARCLOSED]` or both, and the frames submitted `sub` |
| `"UNSERVED"`, `nr` | a panel that holds a sender and never submitted a frame: the seconds, the streak of not-ready ticks and the stream identifier; a panel that has frames and waits shows `nr` alone |
| `"AR gestures: rx="` and `picker` | the counters of the pointer, which [Spatial lines](#spatial-lines) explains |
| `vdisp` | after two bars, one field for each RViz Image or Camera display, in the form `<display>{<topic> pubs=<n> <verdict>}`: the verdict is `RX` when frames reach the display, `WAITING` when a publisher is visible and nothing arrived, `NOPUB` when RViz sees no publisher, `ERR` when its Topic row starts with `Error`, and `UNKNOWN` when there is no row to read; `{NOTOPIC}` stands for a display without a topic and `OFF` for a disabled one |

## Status lines

The labels of the panel say the same things as the lines, and some of them are logged. The label of the connection has the states `Qar runtime: STOPPED`, `Qar runtime: CONNECTING...`, `Qar runtime: RUNNING (live)` and, in the preview, `Qar runtime: RUNNING (offline preview)`. A failed action sets `"Last error:"`, which turns red, and logs the same text after `ERROR:`; a later success clears it. The readout of the AR panels has this form:

```text
AR panels: none (live=0 closed=%1)
input: in %1, missed %2, dropped %3
panels: live=%1 closed=%2 (destroyed) notice=%3
%1: %2x%3 px, %4 x %5 m, content_scale %6 sent (supersample x%7) — sent %8, unchanged %9
; input rx %1, delivered %2, dropped %3
; waiting for a receiver (%1 ticks not ready, %2 total)
; rebuilds %1
[CLOSED IN AR — capture stopped, sender still held]
[WIDGET HIDDEN — the AR panel will look blank]
```

The first line is what the readout says when nothing streams and some panels were closed, the second is the counter of the whole input path, the third the lifecycle, and the fourth one panel with its optional parts: the receiver wait, the rebuilds and the marks. A mark names a state that needs attention: `NOTICE`, `"CLOSED IN AR"` or `"WIDGET HIDDEN"`. The readouts of the tools and of the pointer have these forms; each line has this form:

```text
AR tools: %1
— served on %1:%2
viewport: no page has reported one yet
Spatial: %1
- waiting for an AR pointer
AR gestures: NOT SUBSCRIBED (%1)
AR gestures: not subscribed yet (connect to QarOS first)
```

The button `"Refresh diagnostics"` also writes one line for the 3D streams and one for each stream, which have this form; each line has this form:

```text
Diagnostics: streaming=%1 streams=%2 render_ticks=%3 batches=%4 batch_last=%5ms batch_avg=%6ms served=%7 skipped=%8 budget_skips=%9
peer %1 [%2x%3 @%4fps] submitted=%5 skipped=%6 fps=%7 L=%8ms R=%9ms total=%10ms avg=%11ms
```

## Connect, invite and session lines

A connection writes these lines of the panel in this order; the first is written when you press the button, and the lines after it appear when the session is built. Each line has this form:

```text
Connecting to QarOS runtime (building session; RViz stays responsive)...
LIVE: near/far = %1 / %2 m, depth_scale = %3
LIVE: QarOS library logging: severity=%1 (from %2), console duplicate to stdout=%3%4
LIVE: app volume '%1' %2 m cube at y=%3 m, app_scale=%4
LIVE: auto-invite %1, admission=%2, max_peers=%3
Connected to QarOS: session open, streaming active. No peer invited — pick a device kind and click 'Invite peer' to open a Visualizer.
Connected to QarOS: streaming active; waiting for the invited peer(s).
```

The line that names the logging reports the severity that the plugin decided on and where it came from, which is the variable `QAR_PANEL_LOG_SEVERITY`, `QAR_LOG_SEVERITY` or the default, and the part `%4` is a note when `QAR_LOG_SEVERITY` in the environment disagreed and was overwritten. The two last lines are alternatives: the first says that nothing was invited. A failure to connect ends the attempt with one of these lines and sets `"Last error:"`; the state label then keeps `Qar runtime: CONNECTING...`, because the label is not refreshed at that moment; each line has this form:

```text
QarOS connect threw: %1
QarOS connect failed: %1
QarOS: connect already in progress — RViz stays live; please wait.
QarOS: streaming paused (session kept open).
QarOS: streaming resumed.
```

The first two lines are failures. The other three belong to the buttons. `"QarOS: connect already in progress"` answers a second press on `"Connect to QarOS"` while the session is being built. `"QarOS: streaming paused (session kept open)."` is what `"Disconnect (pause)"` writes on a connected panel, and the panel does not count as connected afterwards. `"QarOS: streaming resumed."` is written only by a press on `"Connect to QarOS"` while the panel still counts as connected, that is, a press with no pause before it: it only switches the streaming on again and runs no step of the connection.

A press on `"Connect to QarOS"` after a pause takes the way of a first connection on the session that is still open. It builds no session, so the lines `"Connecting to QarOS runtime"` and `LIVE:` of the list above are not written again. It hands the session to the manager and switches the streaming on, which the manager reports with the line `"streaming activated"` after its prefix `"[lib:"`. It subscribes to the gestures of the app volume again, and it writes one of the two lines `"Connected to QarOS:"` above. After a pause, therefore, a line `"Connected to QarOS:"` after the line `"QarOS: streaming paused (session kept open)."` shows that the streaming runs again, and the line `"QarOS: streaming resumed."` does not appear.

The options that change the connect write one line each when they are not at their defaults, and a value that cannot be used writes an error; each line has this form:

```text
LIVE: inviting devices at connect: %1
LIVE: peer admission = NEW — peers present in the first few seconds are the runtime's own; anything joining later is treated as a real consumer and gets a stream.
LIVE: GL submit mode = supply-own (expected BLACK against the real SDK)
LIVE: app volume DISABLED — streams get app_volume_id=NULL (mixer-off path only)
LIVE: WARNING: %1
bad QAR_PANEL_DEPTH_SCALE '%1' (use a number or 'norm')
unknown QAR_PANEL_GL_SUBMIT '%1' (use render-into|supply-own)
unknown QAR_PANEL_PEER_ADMISSION '%1' (use invited|new|named|any)
```

The admission lines for `any` and `named` follow the same pattern with `"ANY"` and `"NAMED"`. An invitation and the streams that follow it write these lines; each line has this form:

```text
LIVE: invited %1%2 as peer %3 (%4 invited in total). Its stream opens when the device connects.
NOTE: each Visualizer invite carries a fresh GUID by default (QAR_PANEL_VISUALIZER_GUID=auto), so every invite opens its own Visualizer.
Invite peer: a HoloLens needs its device hostname — type it in the field next to the device selector.
LIVE: sender creation in flight (async) — RViz stays responsive; the stream opens as soon as the peer completes negotiation.
LIVE: opened stream for peer %1
LIVE: closed stream for peer %1
```

The first line gives the first ten hexadecimal digits of the peer identifier that the runtime assigned to the invitation. The line about the sender is written once, because the creation of a sender is asynchronous and reports "not yet" on most early ticks. The adapter prints the bootstrap and the peers on the same stream, and the lines below are the ones that decide most cases. They are always printed; `QAR_ADAPTER_TRACE` adds a pair of lines around every call; each line has this form:

```text
[qar-adapter] library logging: severity=%s console=%s folder=%s%s
[qar-adapter] zenoh logging: off (QAR_ZENOH_LOG not set)
[qar-adapter] *** %s
[qar-adapter] *** SEH FAULT in %s: code=0x%08lX addr=%p module=%s
[qar-adapter] app volume '%s' created id=%s size=%.2fx%.2fx%.2f m centre_y=%.2f m app_scale=%.3f
[qar-adapter] *** invite '%s' failed: %s
[qar-adapter] peer baseline frozen: %zu pre-existing peer(s); any peer appearing from now on is treated as a consumer and admitted
[qar-adapter] *** peer %s refused: already streaming to %zu peers (max_peers=%u)
[qar-adapter] create_sender: peer %s still creating after %.1f s — the receiver has not completed transfer negotiation (the render tick keeps running).
[qar-adapter] create_sender: async creation FAILED for peer %s
[qar-adapter] created GL context for QAR HGLRC=%p (shares Ogre's %p) [sender #%zu]
```

A line that starts with `***` is a failure; the text after it names the call and the status that QarOS returned. The geolocation writes these lines when it starts and when it anchors the scene; each line has this form:

```text
GEO: listening for NavSatFix on '%1' (QAR_PANEL_GPS_TOPIC; QAR_PANEL_GEO=off disables).
GEO: app volume anchored at lat=%1 lon=%2 alt=%3 m (ECEF WGS84, ENU axes).
GEO: anchor NOT set — %1
GEO: no ROS node available; geolocation disabled for this session.
```

## AR panel lines

The lines of the panel announce a stream and its close, and the library of Qt and the adapter write what each step did. A stream of a Qt window starts with these lines; each line has this form:

```text
AR PANEL: close mode '%1'%2 — a streamed window that closes %3.
AR PANEL: %1 streamable panel(s) found.
AR PANEL: streaming '%1' as a QarOS GUI panel. The QarOS panel itself is created on the next tick, off this click.
AR PANEL: '%1' is in the RViz dock '%2'; closing that dock closes its AR panel.
[qar-panel-adapter] %s panel '%s' created: id=%s stream=%s buffer=%ux%u px
[qar-panel-adapter] %s panel '%s' subscribed for AR click+hover: id='%s' content=%ux%u px
[qar-panel-adapter] %s panel '%s' streams but takes no AR input: %s
```

The close mode is logged once, when the first panel is streamed, and says whether it came from `QAR_PANEL_CLOSE_MODE` or is the default. The last line is the failure of the input subscription: the panel still streams, but a tap does nothing. A close in the default mode writes the line of the panel, the line of the manager of Qt with the outcome of each step, and the three steps of the adapter with a time of their own, `hh:mm:ss.mmm`; each line has this form:

```text
AR PANEL: close requested for '%1' (%2): its AR panel is closed and its video sender destroyed — the [qt_lib] 'closed' line has each step. Starting it again makes a new AR panel.
[qar-panel-adapter] %s panel '%s' %s
closing: id=%s stream=%s (operator already closed it in AR: %s)
step 1/3: AR input + panel-update subscriptions cancelled
step 2/3: qar_gui_panels_close_panel ran, ok
step 2/3: qar_gui_panels_close_panel SKIPPED -- the operator already closed this panel in AR
step 3/3: destroying the video sender
[qar-panel-adapter] %s panel '%s' retired: id=%s stream=%s took=%lldms -- GUI panel %s, video sender %s
```

The adapter prints `"close FAILED (see step 2/3 above)"` in the retired line when the close call failed, and then the video sender is destroyed anyway. The manager of Qt writes the close with the key of the panel after the text, in the form `[qt_lib:%1:%2] %3` followed by `(panel '%1')`. Its sentence names the stream, the frames that it served, the reason and the teardown, and ends with a sentence that says that the stream entry is gone; the fragments are these; each line has this form:

```text
added panel stream
frame(s) (reason:
 Teardown:
subscriptions cancelled
had no subscription
, close_panel
, video sender
. The stream entry is gone (no closed-window notice); opening the window again is a fresh add on a new sender.
having served it NO frames after
```

A close that follows a stream that never served a frame is a warning with the words `having served it NO frames`, which reports that QarOS was told about the stream and never got a frame. In notice mode the sentences of the manager say that the stream stays up and shows the panel-closed notice, and that the video sender is deliberately not destroyed. The panel adds these lines for the operator's own panels, for a close in AR and for a panel that is started under a title that an earlier panel used; each line has this form:

```text
AR PANEL: %1 is already streaming.
AR PANEL: '%1' was not streaming.
AR PANEL: re-using the retired AR panel for '%1' — same QarOS panel, same video stream, no new sender.
AR PANEL: '%1' (%2): its AR panel now shows the window-closed notice and stays up until you close it in AR (notice mode keeps the video sender until the session ends).
AR PANEL: '%1' gets the QarOS panel name '%2', because an earlier QarOS panel of this process already used '%3'.
dialog opened from '
' is streaming as a panel of its own
```

The last two lines are one line of the manager of Qt around the key of the panel that opened a dialog: the manager writes it when a dialog gets a panel of its own.

The line with `gets the QarOS panel name` is written when the plugin starts a QarOS panel under a title that an earlier QarOS panel of the same RViz process already used: `%1` is the key of the panel, `%2` is the title that the QarOS panel gets, which is the title that was asked for with a number after it, and `%3` is the title that was asked for. A start under a title that no earlier panel used writes no such line, and neither does a start that only brings back a retired panel. The line `AR PANEL: streaming the hidden Displays panel as` names the title `"Displays (AR)"` that the plugin asks for, also at a restart, so the number of a restart is in the line with `gets the QarOS panel name` only.

## AR tool and web lines

Topics, services and actions that you add from the graph write these lines, and a tool that you expose writes the next group when its page is served and when its panel opens; each line has this form:

```text
TOPICS: indexed %1 message type(s) across the installed RViz displays.
TOPICS: added display %1 for %2.
TOPICS: %1 (%2) — added to the watch list only.
TOPICS: stopped watching %1 (its display, if any, stays).
GRAPH: no ROS node, or nothing visible on the graph.
GRAPH: refresh failed — %1
ROS UI: graph discovery + topic/service/action bridges live on RViz's node.
AR TOOL: panel server listening on %1:%2 (a wrong or missing token is a 404).
AR TOOL: exposed %1 as '%2' (%3) at %4. The QarOS panel is created on the next tick, off this click.
AR TOOL: %1 — %2.
AR TOOL: opened %1 in AR at %2 — %3.
AR TOOL: %1 is served but not in AR yet — %2
AR TOOL: %1 will be re-opened on the next tick.
AR TOOL: retired %1.
AR TOOL: copied %1
```

The line that says exposed names the template that laid the tool out, and the line after it, `AR TOOL: %1 — %2.`, carries the size note: the size of the panel in metres and the number of columns, which the plugin recomputes when a page reports its viewport and the size moved by more than 5 mm. A tool that is served and not in AR yet waits, and the line is written once. The page reports its viewport and the panel refuses a control that it does not understand with these lines; each line has this form:

```text
[qar-web] %1
-> panel %1x%2 m, theme x%3, %4
AR CONTROL: refused '%1' on %2 — %3
AR CONTROL: publish to %1 %2
AR TOOL: %1 has no live subscription — %2
AR TOOL: could not expose %1 — %2
```

The line that starts with `[qar-web]` reports the viewport that the page was laid out into: its CSS size, the pixel ratio, the size of its content and of its root, and the reason for the report.

## Video lines

A video panel writes lines when it opens and closes, and every two seconds while it streams, one pair of lines for each video panel and for each region of a dock; each line has this form:

```text
AR VIDEO: video topics are received in their own ROS context (a DDS participant separate from RViz's), so RViz's own displays of the same topic cannot starve them.
AR VIDEO: could not create the private ROS context for video topics (%1) — '%2' will show no frames.
AR VIDEO: '%1' reusing its existing ROS subscription — no second subscribe/decode for the same topic.
AR VIDEO: %1 video panel for '%2' (%3).
AR VIDEO: '%1' closed; stopping its ROS subscription.
AR PANEL: '%1' is an RViz %2 display — its render window cannot be captured, so '%3' is streamed into the AR panel directly.%4
AR PANEL: '%1' has no Topic set — its render window will show blank in AR until one is.
```

The recurring lines trace the path from the ROS message to the submitted frame; each line has this form:

```text
AR VIDEO: '%1' msgs=%2 decoded=%3 dropped=%4 mailbox_pub=%5 avg_decode_ms=%6
AR VIDEO: '%1' tick: captures=%2 unchanged=%3 not_ready=%4 submitted=%5 avg_capture_ms=%6 %7
AR VIDEO: '%1' region '%2' msgs=%3 decoded=%4 dropped=%5 mailbox_pub=%6 avg_decode_ms=%7
AR VIDEO: '%1' region '%2' dock tick: captures=%3 unchanged=%4 not_ready=%5 submitted=%6 avg_capture_ms=%7 %8
submit_gap_max_ms=%1 submit_gap_avg_ms=%2 fg=%3
```

`msgs` counts the messages that the callback saw, `decoded` those that the worker decoded, and `dropped` those that a newer message replaced before the worker reached them, which is normal under load. `mailbox_pub` is the sequence number of the slot that holds the newest frame. In the second line `captures` counts the capture ticks of the panel, and the counters `unchanged`, `not_ready` and `submitted` say what became of them: nothing changed, QarOS offered no destination, or the frame was sent. The last three fields are the longest and the mean gap between submits and `fg`, which is 1 when the RViz window is in front of the screen, 0 when another window covers it, such as the Visualizer, and `?` when that cannot be told.

## Spatial lines

The subscription to the gestures and the picks write these lines; each line has this form:

```text
SPATIAL: subscribed to the session's AR pointer gestures (click + hover).
SPATIAL: no app volume in this session — AR pointer gestures are not subscribed.
SPATIAL: could not subscribe to AR pointer gestures — %1
SPATIAL: scene placement %1
SPATIAL: scene placement changed — %1
SPATIAL: mode = %1
SPATIAL: target = %1 - %2
SPATIAL: published %1 to %2
SPATIAL: pick canceled
Close polygon: need at least three vertices in polygon area mode.
```

The line `SPATIAL: scene placement %1` reports where QarOS displays the scene, and the next lines say that the placement is reported and not applied: the gesture points are in the content space of the app, so a pick uses the change of basis and nothing else. The first 20 gestures and then one in 50 are traced with the point that QarOS gave, the point after the conversion, and the pick that the armed target resolved; each line has this form:

```text
SPATIAL: sample #%1 %2/%3 commits=%4 room=(%5, %6, %7) -> ros=(%8, %9, %10)%11
SPATIAL: ... scene displayed at (not applied to the pick) %1
SPATIAL: sample #%1%2 pick=%3 via %4 on '%5'%6
, NO RAY: the action point sat %1 m off the plane and was projected onto it
, QarOS's own scene intersection, published verbatim
```

The words after `via` name the method: `"QarOS action point"` when the answer is the intersection that the runtime computed, `ray/plane` or `ray/depth` when the straight aiming ray was intersected with the target, and `projected` when there was no ray. The ray stands between the number of the sample and `pick=`: it is written as `ray=` with its origin and its direction in the frame of the pick, and as `ray=none` when the sample has no hand ray. The adapter writes a longer trace of each gesture, with the identifier, kind and phase of the gesture, the points and the ray, and the verdict of the converter; each line has this form:

```text
[qar-spatial] subscribed via qar_app_volumes_subscribe_all_gesture_events (one reader; CLICK+HOVER dispatched on kind, manipulation kinds counted as other)
[qar-spatial] #%llu kind=%s phase=%s has_point=%d action_room=(%.3f, %.3f, %.3f) has_start=%d start_room=(%.3f, %.3f, %.3f) start_to_action=%.3f%s%s%s -> %s
ray_src=%s origin=(%.3f, %.3f, %.3f) dir=(%.3f, %.3f, %.3f) aim_off=%.3f(reconstruction-vs-action_point: the fallback's error)
NO-HIT(action==start: the runtime intersected nothing; an Action Point pick publishes nothing)
[qar-panel-input] #%llu panel='%s' %s/%s id=%llu hand=%s taps=%u x=%d y=%d -> u=%.4f v=%.4f
```

`ray_src` is `SDK(start->action)` when the ray comes from the runtime and `FALLBACK(...)` when the adapter reconstructed it from the hands. `start_to_action=0.000` and the mark `NO-HIT` say that the click hit nothing: the action point is the start point. The lines of `[qar-panel-input]` are the same for the clicks on AR panels.

## Hidden Displays panel lines

The hidden copy of the Displays panel writes lines when it is created, started and brought back, and when something goes wrong; it never stops RViz; each line has this form:

```text
AR PANEL: created a hidden RViz '%1' panel (%2): %3x%4 px, shown off-screen only - it never appears on the desktop - with item dragging off on this copy. It views RViz's live display tree and will stream as '%5' ("%6"). RViz's panel factory verified by its own RTTI as '%7'.
AR PANEL: streaming the hidden Displays panel as '%1' ("%2", %3x%4 px) at room (-1.2, 1.5, -1.0) m. The QarOS panel itself is created on the next tick, off this call. 'Stop streaming' and 'Stream panel to AR' act on the stream only: the widget stays shown off-screen.
AR PANEL: the hidden Displays panel is not started on connect (%1=%2). '%3' in the panel list still starts it by hand.
AR PANEL: could not create the hidden Displays panel (class id '%1'%2%3): %4 -- continuing without it.
AR PANEL: the hidden Displays panel was not started: %1
AR PANEL: the hidden Displays panel had been hidden by something (a hide() or close() on it); it is shown off-screen again. Said once: a hidden widget would have streamed a blank rectangle.
AR PANEL: '%1' was closed in AR, which stops its capture but leaves its stream entry; clearing that entry before starting it again.
```

The text after `"could not create"` is the reason that the creation reports, and it names the first check that failed: the RViz window has no panel factory, the layout of the window is not the one that the plugin was built against, the factory failed the type check, the factory made another class, or an exception was thrown. The plugin continues without the copy.

## Floor plane, scheduling and timing lines

At load the panel says what it did about the power throttling and the timer resolution of Windows. The floor plane, the lighting and the timers write one line when they start or change; each line has this form:

```text
SCHED: not Windows - no scheduling opt-out needed.
process power throttling OFF (full speed, timer resolution always honoured even when occluded)
SetProcessInformation not available (older Windows) - power-throttling opt-out skipped
; timeBeginPeriod(1) OK.
; timeBeginPeriod(1) FAILED.
Floor plane: no Grid display (rviz_default_plugins/Grid) found in the display tree — nothing to mirror.
Floor plane: disabled via QAR_PANEL_FLOOR_PLANE=off.
Scene lighting: mode=%1, RViz headlight=%2
Render tick: %1 ms
Prime next frame: %1
TIMING: per-frame stage timing is ON -> %1 (video_*.csv, rx_*.csv, render3d*.csv, gui_gaps.csv; epoch microseconds; flushed ~1/s by a background thread, never on this one)
```

The `SCHED:` line is the first part followed by the second. A line that starts with `"Floor plane: frame='"` is written for each Grid display when it is created and whenever its frame, plane, cell count, cell size or offset changes, and not on every tick; it gives the size of the plane and the depth bias.

## QarOS library logs

The QarOS library, the runtime launcher, the mixers and the Visualizer write their own files into the QarOS log folder. What they write depends on `QAR_LOG_SEVERITY`, which the library reads in preference to its init structure, and the plugin sets it from its own decision, so that the severity in the line that begins `LIVE: QarOS library logging:` is the severity that applies. At `trace` the library writes hundreds of thousands of lines in a short run, [How much a trace log writes](TROUBLESHOOTING.md#how-much-a-trace-log-writes) explains why a quiet run is the default of the launcher. The log of the zenoh transport is the only place that names the reason for a session that does not come up; the adapter switches it on when `QAR_ZENOH_LOG` is set and the severity allows it, and it always writes one line that says whether it did; each line has this form:

```text
[qar-adapter] zenoh logging: OFF - QAR_ZENOH_LOG='%s' is ignored because the library log severity is %s; use severity debug or trace to get it
[qar-adapter] zenoh logging enabled (filter=%s)
```

## Timing CSV files

With `-TimingCsv` the launcher sets `QAR_VIDEO_TIMING_DIR` to a new folder, and the plugin writes the files below into it. Every time is a count of microseconds since the epoch of the system clock, which is the clock that the video publisher uses, and an empty cell means no value. The files are created on first use by a thread that writes in the background, flushes about once a second, and never runs on the GUI thread.

| File | One row for | Columns |
|---|---|---|
| `video_<key>.csv` | each capture tick of an AR video panel | `seq`, `header_stamp_us`, `arrival_us`, `decode_done_us`, `mailbox_us`, `decode_seq`, `tick_us`, `capture_us`, `ready_us`, `not_ready_start_us`, `not_ready_end_us`, `not_ready_count`, `submit_us`, `outcome` |
| `rx_<topic>.csv` | each message that the ROS callback received | `seq`, `header_stamp_us`, `arrival_us`, `decode_start_us`, `decode_done_us`, `mailbox_us`, `decode_seq`, `outcome` |
| `render3d.csv` and a numbered file for each further stream | the runs of the 3D stream of each peer | `kind`, `start_us`, `end_us`, `count`, `frame`, `detail`, `mid_us` |
| `gui_gaps.csv` | each gap of more than 25 ms between two render ticks | `start_us`, `end_us`, `gap_us` |

The `outcome` of a row of `video_<key>.csv` is one of `submitted`, `not_ready`, `unchanged`, `capture_failed`, `submit_failed` and `adapter_error`, and that of a row of `rx_<topic>.csv` is `decoded`, `superseded` or `decode_error`. In `render3d.csv` the `kind` is `not_ready` for a run of polls that found no destination, `ready` for a frame that was ready, `submit` for a frame that was rendered and shown, and `slow_poll` for a poll of `try_begin_frame` that took more than 2 ms. The script `scripts\analyze_video_timing.py` reads the folder and, with `--publisher`, the file `publisher.csv` of the video publisher, which it writes when you start `scripts\publish_video_file.py` with the ROS parameter `timing_csv`. It prints the distribution of each stage, finds the episodes in which a panel went longer than `--gap-ms` without a submit, 100 ms by default, names the first stage that went quiet, and gives one verdict for each episode: `SOURCE` when nothing reached the plugin, `OUR-PIPELINE` when frames arrived and the plugin did not process them, and `QAROS-BACKPRESSURE` when everything was done and QarOS offered no destination. A strict period of the onsets, such as 15 seconds, points at a task of the machine and not at a frame. A panel that has no new frame to show submits its last picture again after one second, so a video that has stopped makes a submit gap of about a second, over and over. The analyzer does not read such a gap as a stall. A gap is idle when every capture tick in it found the picture unchanged, no message reached the topic of the panel during it and the submit that ends it carries no newer frame; idle gaps are ignored, so a pause of the source of more than a second is idle time and no episode. The summary of each panel gives the seconds in which a frame was waiting, the submit rate over those seconds, the percentiles of the submit gaps in them, and the number and the total duration of the idle gaps that were ignored. A gap in which a new frame arrived and was not submitted and a gap without a capture tick while a message arrived are episodes, and so is a gap in which a tick found QarOS not ready or a step failing, even when no frame was waiting. When the video starts again after idle time, the wait of its first frame is counted from the moment that frame arrived. The analyzer has a self-test, `python scripts\analyze_video_timing.py --self-test`, which [Testing](TESTING.md#the-analyzer-of-the-timing-files) describes.

## Frame trace CSV

With `QAR_ADAPTER_FRAME_TRACE` set, the adapter writes one row for each frame that a 3D stream submits into a file in the QarOS log folder, named `qar_frame_trace_<yyyyMMdd_HHmmss>_<tag>.csv`; the tag comes from the peer. The first line of the file is the header; each line has this form:

```text
frame,peer,tick_ms,prime,wait_start_us,wait_src,polls,ready_us,consume_us,render_done_us,show_done_us,show_ok,pos_x,pos_y,pos_z,quat_x,quat_y,quat_z,quat_w,consume_local,a_pos_delta_mm,a_angle_delta_deg,c_pos_delta_mm,c_angle_delta_deg,changed
```

The columns from `wait_start_us` to `show_ok` follow one frame. `wait_start_us` is when the wait for the frame began, and `wait_src` says which call started it, `prime` for the call that follows the previous show and `poll` for a call of the next tick. `polls` counts the calls of `try_begin_frame` in the wait, and `ready_us`, `consume_us`, `render_done_us` and `show_done_us` are the times at which the frame was seen ready, taken, rendered and shown, with `show_ok` as the result of the show. The columns `pos_x` to `quat_w` are the pose of the left eye that the frame was rendered from. The runtime can move the pose between the moments at which the plugin reads it: `a_pos_delta_mm` and `a_angle_delta_deg` compare the pose that was read when the frame was first seen ready with the pose of the render, which only the primed path has, and `c_pos_delta_mm` and `c_angle_delta_deg` compare the pose that is read again just before the show. `changed` is 1 when either read differs from the pose of the render, and the heartbeat counts the rows in which it is 1 as `rr=`.
