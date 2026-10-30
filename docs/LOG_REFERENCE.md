# Log reference

This page explains the text that a run of the plugin leaves behind: where each stream of text goes, how to tell its writers apart, what each line says and what the fields of the heartbeat line mean. A line that is quoted here has the form that the code gives it, with `%1`, `%2` and so on for the values of the run, and a line that you read in a log has the same words with the values filled in. [Troubleshooting](TROUBLESHOOTING.md#reading-the-logs) says which lines to read first.

## Where the logs go

The panel writes every line that it logs to two places. It appends the line to its `Diagnostics` view, with the time of day in front, and the view keeps the last 2000 lines. It also writes the line to the standard error of RViz after the prefix `[qar-panel]`. Under the launcher the standard error is the file `rviz_live_<stamp>.err.log` and the standard output is `rviz_live_<stamp>.out.log`, both in the log folder, which is `qar_rviz_logs` in `TEMP` unless you pass `-LogDir`. The view does not survive RViz, so the file is the record.

| Destination | What is in it | What controls it |
|---|---|---|
| the `Diagnostics` view | the lines of the panel, with `HH:mm:ss.zzz` in front, the last 2000 | the heartbeat only: `QAR_PANEL_HEARTBEAT_S` |
| the standard error of RViz | the same lines after `[qar-panel]`, and the lines that the libraries of qar_ogre_adapter write straight to it | nothing for the panel; the adapters follow `QAR_ADAPTER_TRACE` for the per-call lines |
| the standard output of RViz | nothing of the panel; the console copy of the QarOS library | `QAR_PANEL_LOG_CONSOLE` |
| the QarOS log folder | `qar_streaming.log`, the log of the library inside RViz, the crash reports of the process, and the frame trace | `QAR_PANEL_LOG_DIR`, `QAR_PANEL_LOG_SEVERITY` |
| the folder logs of the QarOS log folder | the logs of the processes that QAROS starts: one folder for each session of the runtime launcher below `qar-launcher-default`, and `qaros-hub-service` for the Hub service | QAROS |
| the folder `QAR_VIDEO_TIMING_DIR` | the timing files | the launcher parameter `-TimingCsv` |
| the output prefix of a scripted mode | the result files of the mode | the variables `QAR_PANEL_AUTO...` and the preview script |
| the log folder of the launcher | the results of the start checks, `rviz_live_<stamp>.checks.txt`, and, after RViz has ended, the copy of the log of the QarOS library, `rviz_live_<stamp>.qar_streaming.log`, the copies of the log files of the newest launcher session of the run, `rviz_live_<stamp>.launcher_<name>`, and the copies of the crash reports of the run, `rviz_live_<stamp>.<report>` | the launcher parameter `-LogDir` |

The launcher makes the copies of the QarOS processes when `rviz2.exe` has ended, with the crash reports of the QarOS log folder, of its folder `crashes` and of the Hub service, and it copies only the files that were written at or after the start of the run. It prints one line for each copy, with the new name and the size. A QarOS log folder that does not exist, or that holds no file of the run, gives a note and no error, and a copy that fails gives a warning. The files with the ending `.dmp` of the crash reports are not copied.

The QarOS log folder is `quaternar` in `TEMP` unless `QAR_PANEL_LOG_DIR` names another. The library inside RViz writes its log there and starts the file afresh in every session, and the processes that QAROS starts write below the folder logs of the QarOS log folder: the runtime launcher into one folder for each session below `qar-launcher-default`, and the Hub service into `qaros-hub-service`. [Where the QarOS library writes its logs](TROUBLESHOOTING.md#where-the-qaros-library-writes-its-logs) explains the folder and [Which severity applies](TROUBLESHOOTING.md#which-severity-applies) the severity. The runtime reports its own internal peers too; the session owner keeps them out of the list of targets and uses them only to tell that the Hub is gone.

The result files of the capture buttons and of the scripted modes are text files of `key=value` lines, and the first line of each says what the file is. The file `_capture.txt` starts with `"# Qar RViz2 offscreen capture"`, `_stereo.txt` with `"# Qar RViz2 single-peer stereo capture"`, `_autostereo.txt` with `"# Qar RViz2 single-peer stereo AUTOSTEREO summary"`, `_automulti.txt` with `"# Qar RViz2 multi-peer batched render scheduler proof"`, and the file that `QAR_PANEL_AUTODUMP` names with `"# Qar RViz2 streaming panel - auto diagnostics dump"`.

The command lists the folders of the logs of the processes that QAROS starts, by name:

```powershell
Get-ChildItem -LiteralPath (Join-Path $env:TEMP 'quaternar\logs') -Directory | ForEach-Object { $_.Name }
```
```text
qar-launcher-default
qaros-hub-service
```

## Channels and prefixes

Every line starts with the prefix of its writer. The first rows come through the panel, which forwards the diagnostics of the libraries to its own log. The last row is written straight to the standard error by the adapters of qar_ogre_adapter 0.1.0, whose document TROUBLESHOOTING describes them in the section "Reading the logs".

| Prefix | Written by | Lines |
|---|---|---|
| `[qar-panel]` | the panel | everything below that has a keyword in front |
| `"[lib:"` | the manager of qar_ogre_streaming, through the sink of the panel | the creation, activation and destruction of the 3D streams, the wait for a sender, and the lighting |
| `"[qt_lib:"` | the manager of qar_qt_streaming, through the sink of the panel | the lifecycle of the AR panels of Qt windows, with the key of the panel |
| `"[ros_ui:"` | qar_ros_ui, through the sink of the panel | the diagnostics of the graph discovery and of the bridges |
| `[qar-web]` | the panel | the viewport that a web page reports |
| `[qar-adapter]`, `[qar-panel-adapter]`, `[qar-url-panel-adapter]`, `[qar-panel-input]`, `[qar-spatial]`, `[qar-timing]` | the adapters and the timing log of qar_ogre_adapter | the calls into QarOS, the creation and close of panels, the gestures, and a timing file that could not be created |

The lines of the panel itself start with a keyword, and the sections of this page follow the keywords:

| Keyword | Section |
|---|---|
| `HB` | [The heartbeat line](#the-heartbeat-line) |
| `LIVE:`, `"QarOS:"`, `"Connected to QarOS:"`, `GEO:`, `NOTE:` | [Connect, invite and session lines](#connect-invite-and-session-lines) |
| `"PANEL SIZE:"`, `"SETTINGS:"` | [Size and settings lines](#size-and-settings-lines) |
| `"AR PANEL:"`, `"AR VISIBILITY:"` | [AR panel lines](#ar-panel-lines) and [Hidden Displays panel lines](#hidden-displays-panel-lines) |
| `TOPICS:`, `GRAPH:`, `"ROS UI:"`, `"AR TOOL:"`, `"AR CONTROL:"` | [AR tool and web lines](#ar-tool-and-web-lines) |
| `"AR CONTROL PANEL:"` | [The AR control panel lines](#the-ar-control-panel-lines) |
| `"TEST MARKERS:"` | [Test marker lines](#test-marker-lines) |
| `"AR VIDEO:"` | [Video lines](#video-lines) |
| `SPATIAL:` | [Spatial lines](#spatial-lines) |
| `"Floor plane:"`, `SCHED:`, `TIMING:`, `"Render tick:"`, `"Prime next frame:"`, `"Scene lighting:"`, `"RENDER:"` | [Floor plane, scheduling and timing lines](#floor-plane-scheduling-and-timing-lines) |
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
hidden=%1
parked=%1
arclosed=%1
CLOSEFAIL=%1
VISFAIL=%1
CAPPED=%1
| %1%2 sub=%3
UNSERVED %1s nr=%2 stream=%3
nr=%1
| tasks q=%1 run=%2 slow=%3
| gui_calls slow=%1 worst=%2
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
| `panels`, `live`, `closed`, `notice` | after two bars: the AR panels, those that stream, those that were closed and destroyed, and those that show a notice; the field stays once a panel has been closed |
| `hidden`, `parked` | right after `notice`, each only while it is not 0: the panels that are hidden in AR with nothing closed, and the panels whose window was closed and that are kept hidden; a parked panel counts in `live` and, once QarOS has accepted the hide, in `hidden` |
| `arclosed`, `"CLOSEFAIL"`, `"VISFAIL"`, `"CAPPED"` | each appears only when it is not 0: the panels that the operator closed in AR, the failures of the close call, the hide and show requests that QarOS refused or that could not be carried out, and the creations that the tombstone cap refused |
| one field for each AR panel | after a bar: its key, the marks `[NOTICE]`, `[ARCLOSED]`, `[HIDDEN]` and `[PARKED]`, which stand together in one pair of brackets and are separated by commas, and the frames submitted `sub` |
| `"UNSERVED"`, `nr` | a panel that holds a sender, has not submitted a frame and is neither closed in AR, hidden nor parked: the seconds, the streak of not-ready ticks and the stream identifier; the streak is three numbers separated by slashes, for the ticks that had no destination, for the status 604 and for the status 603, for example 6/0/0, where 604 says that there is no transfer to reserve a destination on and 603 that the transfer is reconnecting; a panel that has frames and waits shows `nr` alone, as one number |
| `"lum="`, `"a="` | the last part of the field of each AR panel: the mean luminance of the frame that the panel sent last over all rows, over the top rows, which are the first 15 percent of the rows, and over the other rows, as three whole numbers separated by slashes, and then the mean alpha; the sample is every eighth pixel and is taken at most once a second; `"lum=-"` stands for a panel that has no measurement, and the alpha is left out until it has one |
| `"AR gestures: rx="` and `picker` | the counters of the pointer, which [Spatial lines](#spatial-lines) explains |
| `tasks` | in a QarOS session, after the counters of the pointer and a bar: the calls that wait for the thread of the session owner `q`, the call that runs now with its name and its running time `run`, and the slowest call so far with its name and time `slow`; `-` stands for none |
| `"gui_calls"` | in a QarOS session, after a bar: the number of calls on the GUI thread that took longer than the limit `slow`, and the slowest of them with its name and time `worst`; `-` stands for none |
| `vdisp` | after two bars, one field for each RViz Image or Camera display, in the form `<display>{<topic> pubs=<n> <verdict>}`: the verdict is `RX` when frames reach the display, `WAITING` when a publisher is visible and nothing arrived, `NOPUB` when RViz sees no publisher, `ERR` when its Topic row starts with `Error`, and `UNKNOWN` when there is no row to read; `{NOTOPIC}` stands for a display without a topic and `OFF` for a disabled one |

The brightness field is where the log tells two faults apart: a panel that shows black in AR while `"lum="` is high has frames with content that the receiver does not show, and a panel whose frames are black has a problem of capture.

## Status lines

The labels of the panel say the same things as the lines, and some of them are logged. In a QarOS session the first line of the strip is the State line of the group `"QAROS Hub"`: `"QarOS: not connected"`, `"QarOS: loading the QAROS runtime ..."`, `"QarOS: waiting for approval in the QAROS Hub - click Allow in the QAROS window"`, `"QarOS: rejoining"`, `"QarOS: pairing"`, `"QarOS: joined"`, `"QarOS: connected"`, `"QarOS: Hub not reachable - waiting for it to come back"`, `"QarOS: disconnecting ..."`, and `"QarOS: "` with the sentence of the failure when a join failed. [Connecting and pausing](OPERATOR_GUIDE.md#connecting-and-pausing) explains each state; the log does not repeat these texts, it has the lines of the owner instead. In the offline preview the label of the connection has the states `Qar runtime: STOPPED` and `Qar runtime: RUNNING (offline preview)`. A failed action sets `"Last error:"`, which turns red, and logs the same text after `ERROR:`; a later success clears it. Both labels are one line each in the strip at the top of the panel: a text that is longer than the strip is shortened to its width, and the whole text is the tool tip. The readout of the AR panels has this form:

```text
AR panels: none
AR panels: none (live=0 closed=%1)
AR panels:
input: in %1, missed %2, dropped %3
panels: live=%1
 (hidden %1)
 (parked %1)
 closed=%1 (destroyed) notice=%2
 arclosed=%1
 (%1 already closed in AR, close_panel skipped)
 — %1 retired as notices this session, %2 slot(s) re-used
  [%1 close_panel FAILED — see the log]
  [CAP REACHED — %1 panel(s) refused]
%1: %2x%3 px, sent %4
... and %1 more panel(s); all counters are in the log
```

The first two lines are what the readout says when nothing streams, the second one when some panels were closed. Otherwise the readout holds the heading, the counter of the whole input path, the lifecycle and one line for each panel. The readout is a box of sixteen lines that never scrolls and never changes its size, so it holds only these short lines; the long counters of a panel, its size in metres, the supersample and the input that it received, are in the heartbeat and in the lines of the library, and when the lines do not fit, the last panel lines are dropped and the last line of the box says how many. The input line gives the samples read, those that hit no panel and those that the state machine refused, with the last reason for a refusal in parentheses when there is one. The lifecycle line gives the live panels, then `(hidden n)` and `(parked n)` while there are some, then the panels closed and destroyed and the notices, and the parts that appear only when they apply: `arclosed`, the closes that were skipped because the operator had closed the panel in AR, the notices retired and the slots re-used, the failures of the close call, and the creations that the cap refused. A panel line names the key, the size of the buffer in pixels and the frames sent, and then the flags that apply, separated by commas: `"notice"`, `"closed in AR"`, `"parked"` or `"hidden"`, `"widget hidden"`, `"waiting for a receiver"`, `"rebuilds %1"` and `"error: %1"`. A window that is not shown is `"widget hidden"` and the AR panel looks blank, a panel that was built more than once or waits for a new size is `"rebuilds %1"`, and the error is the last error of the panel, shortened to 48 characters. The readouts of the tools and of the pointer have these forms; each line has this form:

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

### Size and settings lines

The panel reports its own size and the use of its settings. The lines with `"PANEL SIZE:"` say what size the panel and its dock have and why. Sizes are written as `<width>x<height>` in pixels. The first line is written once, when the panel is shown for the first time, and gives the size of the panel, its size hint and its minimum, the height of the status strip and the height of the tab bar. The line about a tab says whether the size of the panel changed when the tab was selected, which is the symptom of a size hint that is not constant, and the line about a resize is written at most once a second, so that a burst of changes gives one line when it starts and one where it ends. Each line has this form:

```text
PANEL SIZE: panel %1 (hint %2, min %3), strip %4 px, tab bar %5 px
PANEL SIZE: panel resized %1 -> %2
PANEL SIZE: tab '%1' selected; panel %2 unchanged
PANEL SIZE: tab '%1' selected; panel %2 CHANGED from %3
```

The first line is followed by one line for each tab, which gives the size that the content of the page shows, the minimum it needs, the viewport of the page and whether the page fits or scrolls; a page that needs more height than its viewport has says `SCROLLS` and the pixels that are missing, and a page that needs more width says by how much it is wider than the viewport. The parts of the line of a page are these; each line has this form:

```text
page '
': content
 (min
), viewport
SCROLLS by
fits
 px wider than the viewport
```

A configuration that an older panel saved has a settings version below 2 or none, and its dock gets the default size of the panel once, half a second after the configuration is loaded, when RViz has restored its window. The line `default dock size applied` gives the size that the dock has afterwards and the size that was asked for, and the lines `not applied` give the reason: an AR panel is already streaming and a resize would rebuild it, the panel is not in a dock, or the dock has no main window. When the dock needs more height than its window has, the window of RViz is grown, and the line says so; when it cannot grow, the line names the reason, which is the bottom of the screen, a maximized window, a full-screen window or a streamed dock in another column. Each line has this form:

```text
PANEL SIZE: default dock size applied: %1 (asked %2; the config had settings version %3)
PANEL SIZE: default dock size not applied: the panel is not in a dock
PANEL SIZE: default dock size not applied: an AR panel is already streaming and a resize would rebuild it
PANEL SIZE: default dock size not applied: the dock has no main window
PANEL SIZE: RViz window grown from %1 to %2 px so that '%3' gets %4 px.
PANEL SIZE: RViz window cannot grow (%1); '%2' stays %3 px.
PANEL SIZE: This QAR panel fitted at start: %1
PANEL SIZE: This QAR panel not fitted at start: %1
PANEL SIZE: This QAR panel fitted at start: goes on as the fit of its stream (dock %1 -> %2 px so far)
```

After the first show the panel is fitted to its tallest page, so that no page needs a scroll bar. The fit looks at the dock every 100 ms and at most six times, and makes the dock higher where a scroll area is missing room. The line `This QAR panel fitted at start` gives in `%1` the dock before and after, `dock <before> -> <after> px`, or `dock <height> px already high enough` when nothing had to change, and the result of the fit in parentheses. The line `not fitted at start` names the reason: the panel is not in a dock, its dock is not visible, an AR panel is already streaming and a resize would rebuild it, or the fit for its stream is already looking at the dock. A fit that is running when the operator streams the panel goes on as the fit of the stream, and the line says so.

The fit of a dock that is streamed, and the fit of the hidden Displays copy, write each round with the prefix `"AR PANEL:"` and the key of the panel, and the end of the fit; each line has this form:

```text
AR PANEL: '%1' %2
fit round
 px, missing
 px (
 area(s) measured,
 unmeasured; largest
fitted to
 px in
 round(s)
, confirmed: no scroll bar
: a scroll bar of
 px is left (
: not confirmed (
```

A round line gives the round, the height of the dock in pixels, the pixels that the largest scroll area is missing, the number of areas that were measured and that could not be measured, and the largest area. The end of the fit gives the height and the number of rounds, and either `confirmed: no scroll bar`, or the scroll bar that is left, in pixels, and the reason.

The lines with `"SETTINGS:"` report the fourteen settings of the panel. The panel writes them when it loads or saves the RViz configuration, when you edit a setting, and at the connect; a line is written once, however often the situation comes back. Each line has this form:

```text
SETTINGS: loaded %1 values from the RViz config (version %2)
SETTINGS: ignored unknown keys:
SETTINGS: saved %1 values (version %2)
SETTINGS: %1 = %2 (the operator's own value, written by File > Save Config)
SETTINGS: %1 '%2' is %3 - not kept for the RViz config
%1 = %2 comes from the environment (%3=%4); its control shows that value and is disabled
```

The line `loaded` appears when the configuration holds values of the panel, and the line `ignored unknown keys` lists the keys that the panel does not know and ignores. The line `saved` names the number of values and the version of the settings. A line with `the operator's own value` follows an edit that changed a value, and a line with `not kept` follows a text that the setting does not accept. A setting that a variable decides is written once, with the variable and its text, and its control shows that value and is disabled. A value that is not acceptable is named with the reason: a saved value is replaced by the default, and a variable is ignored in favour of the saved value or the default.

## Connect, invite and session lines

A connection writes lines of three kinds. The panel writes its own lines, which start with a keyword such as `LIVE:`, `"QarOS:"` or `"Connected to QarOS:"`. The session owner, the object that runs on a thread of its own, loads the installed QAROS library and joins the Hub, writes lines that start with `"QAROS: "` in capital letters. The adapters write lines that start with `[qar-adapter]`. The owner's lines go to the standard error of RViz unchanged and are queued for the `Diagnostics` view, which shows them from the render tick, so they carry the time of the tick and not the time of the event. The first lines of a connection are these, in this order; the first is written when you press the button, and the lines after it appear as the owner reports the progress of the join. Each line has this form:

```text
LIVE: QarOS library logging: severity=%1 (from %2), console duplicate to stdout=%3%4
QAROS: session owner made (program %1, Hub mode setting '%2')
Connecting to QarOS (the QAROS runtime is loaded and the Hub joined on a thread of its own; RViz stays responsive)...
QarOS: the Hub session is open; making the render factory.
LIVE: near/far = %1 / %2 m, depth_scale = %3
LIVE: max_peers=%1 (the session owner serves at most that many targets)
LIVE: app volume '%1' %2 m cube (%3) at y=%4 m, app_scale=%5, fixed panel poses: %6
LIVE: AR panel frame top %1, bottom %2, left %3, right %4 m, fit margin %5 m (QarOS draws a stream panel's video at the full panel width under its title bar when it fits there and otherwise adds a scroll track at the right: a panel is made as tall as the video needs plus the margin, a dialog as narrow; source: %6%7)
QarOS: the app volume of the session is being made on the owner's thread.
Connected to QarOS: session open, streaming active; a stream opens for each target that is online.
AR VISIBILITY: hide (panels are hidden and parked in AR, never closed while the session runs; experimental)
AR VISIBILITY: close (panels are closed and made again)
```

The first two lines are written once, when the owner is made at the first press of `"Connect to QarOS"`; a later press finds the owner and writes `QAROS: the Hub settings changed; the session owner applies them at the next connect.` when a Hub setting differs from the one it holds. The line that names the logging reports the severity that the plugin decided on and where it came from, which is the variable `QAR_PANEL_LOG_SEVERITY`, `QAR_LOG_SEVERITY` or the default, and the part `%4` is a note when `QAR_LOG_SEVERITY` in the environment disagreed and was overwritten. The line about the app volume gives the edge of the cube in metres, where the edge came from, the height of the centre of the cube, the scale and the way in which the panels with a fixed place are hung, `room` or `ground`. The edge comes from `QAR_PANEL_VOLUME_SIZE`, from the default edge times the scale, which the line writes as `"m x app_scale"` and the scale, or it is the default size; with the app volume off, the line `LIVE: fixed panel poses: %1` takes its place. The line about the frame gives the four insets and the fit margin, in metres, with which every AR panel is sized, and where they came from: `QAR_PANEL_AR_FRAME`, `QAR_PANEL_AR_HEADROOM (the top inset only)` or `default`. The part `, QAR_PANEL_AR_HEADROOM ignored` says that both variables were set, in which case the frame variable decides. The line `LIVE: max_peers=%1` gives the largest number of targets that get a stream, 8 unless `QAR_PANEL_MAX_PEERS` says otherwise. The line `Connected to QarOS:` is written when the render factory and the app volume are ready and the streaming has started, and the last line says how a panel is switched off from now on, `hide` or `close`.

The lines of the owner are what the Hub did. They start with `"QAROS: "`, and the owner writes one line for each of these events; each line has this form:

```text
QAROS: 
runtime 
 from 
state 
status 
progress 
hub mode 
joined as peer 
) in 
target 
 -> served
not served (
 -> stream stopped
invite 
 -> error: 
settings kept for the next connect (state 
identity file damaged: pair again (
identity file not written: 
```

The line `runtime` names the version, the flavour and the C API of the library that was loaded and the folder it came from. The line `state` names the state of the join, one of `idle`, `loading`, `waiting_for_approval`, `rejoining`, `needs_code`, `pairing`, `running`, `reconnecting`, `stopping` and `failed`, and a state that has a cause is followed by a line `status` with the symbol, the sentence and, in parentheses, the detail. The line `progress` carries a step that the library reported while the join was running, and the line `hub mode` says whether the owner took the Hub of this PC, `local`, or the Hub of another PC, `remote`, with the reason in parentheses. The line `joined as peer` ends the join: it gives the first eight hexadecimal digits of the identifier of the program, the way of the join, `local`, `rejoin` or `pairing`, and the seconds that the join took. The line `target` is written when a peer of the Hub appears, changes its presence or goes: it gives the first eight digits of the peer identifier, the kind of the device, its name and its presence, and whether the peer is `served`, which is to say that a stream is made for it, or `not served`, with the reason in parentheses, for example the cap of targets. The line `invite` gives the kind and the host of an invited device and either the first digits of the new peer or the error that the Hub answered. The line `settings kept for the next connect` says that a change of the Hub settings arrived while a join was running, so that it applies at the next connect, and `"identity file damaged"` says that the stored pairing could not be read, so that the next connect needs a code.

A QAROS call of the owner's thread that takes half a second or longer and a task that takes one second or more are logged with their name and their duration, the first twenty of each name and then every fiftieth, and a task that the owner drops at its end is logged with the reason; each line has this form:

```text
QAROS: call 
 took 
 ms
task '
' took 
' dropped (
teardown: 
 in 
```

The lines `teardown:` are written when the session ends, one for each step in the order in which the owner takes the session apart: the onboarding attempt, the invites, the subscription to the peers, the session, the runtime and the library, each with the milliseconds that it took. A call on the GUI thread of RViz that takes longer than the limit, 20 ms unless `QAR_GUI_CALL_WARN_MS` says otherwise, is logged by the guard of the plugin; its line has this form:

```text
[qar-gui-call] %s WARNING %s took %.0f ms on the GUI thread (limit %.0f ms)
```

A join that fails ends with a state `failed` or `needs_code`, and the panel then sets `"Last error:"` to the sentence of the owner, after the prefix `"QarOS: "`, and logs it after `ERROR:`; the State line shows the same sentence and `"Connect to QarOS"` is on again. A failure after the join, when the render factory or the app volume could not be made, ends the attempt with one of these lines, gives the session back to the owner and sets `"Last error:"`; the lines of the buttons are on the same list. Each line has this form:

```text
QarOS connect failed: %1
QarOS: connect already in progress - RViz stays live; please wait.
QarOS: already connected.
QarOS: streaming paused (session kept open).
QarOS: streaming resumed.
QarOS: the connect was stopped while the session was being set up.
```

The first line is the failure. `"QarOS: connect already in progress"` answers a press on `"Connect to QarOS"` while a join is under way, and `"QarOS: already connected."` a press while the panel is connected and streaming. `"QarOS: streaming paused (session kept open)."` is what `"Disconnect (pause)"` writes on a connected panel, and the panel does not count as connected afterwards. `"QarOS: the connect was stopped while the session was being set up."` is what `"Disconnect (pause)"` writes while the app volume is still being made: the render factory lets go and the session goes back to the owner. A press on `"Connect to QarOS"` after a pause writes `"QarOS: streaming resumed."` and then does what the end of a first connection does on the session that is still open: it builds no session, so the lines of the list above up to the app volume are not written again, but it switches the streaming on, which the manager reports with the line `"streaming activated"` after its prefix `"[lib:"`, subscribes to the gestures of the app volume again, and writes the lines `"Connected to QarOS:"` and `"AR VISIBILITY:"` again.

The owner reports the end of a pairing, of a forget and of a cancel to the panel, which writes these lines; each line has this form:

```text
QarOS: pairing code sent to the Hub at %1:%2.
QarOS: forgetting the pairing with the Hub (the session is released first).
QarOS: the pairing with the Hub was forgotten; the next connect needs a code.
QarOS: cancelling the join.
```

The pairing code itself is never logged. When the session ends, on `"Forget pairing"`, because the owner left the running state on its own, or when RViz closes the panel, the panel releases what it built on the session, and the lines say what was done; each line has this form:

```text
QarOS: releasing everything built on the session (%1).
QarOS: %1 session task(s) queued; the owner runs them before it releases the session (%2 ms on this thread). AR panels are hidden in the Hub; start them again with 'Stream panel to AR' after the next connect.
```

The first line gives the reason: `the pairing is forgotten` or `the QAROS session ended`. The second line gives the number of calls that wait for the thread of the owner, which hides each QarOS panel and lets go of its sender before it releases the session, and the milliseconds that the release took on the GUI thread; no QarOS panel is closed.

The options that change the connect write one line each when they are not at their defaults, and a value that cannot be used writes an error; each line has this form:

```text
LIVE: GL submit mode = supply-own (expected BLACK against the real SDK)
LIVE: app volume DISABLED - streams get app_volume_id=NULL (mixer-off path only)
LIVE: WARNING: %1
bad QAR_PANEL_DEPTH_SCALE '%1' (use a number or 'norm')
unknown QAR_PANEL_GL_SUBMIT '%1' (use render-into|supply-own)
```

A value of `QAR_PANEL_AR_FRAME`, `QAR_PANEL_AR_HEADROOM` or `QAR_PANEL_FIXED_POSES` that cannot be used is named in a line after the prefix `LIVE: WARNING:`, with the text that was refused and what is used instead, as is a value of `QAR_PANEL_APPROVAL_TIMEOUT_S` that is not a number of seconds. The lines end in the following parts; each line has this form:

```text
QAR_PANEL_AR_FRAME='
' is not four numbers of metres from 0 to 
QAR_PANEL_AR_HEADROOM='
' is not a number of metres from 0 to 
' is not room or ground; using room.
QAR_PANEL_APPROVAL_TIMEOUT_S='
' is not a number of seconds (0 to 600) - the Hub's default of 120 s applies
```

Targets need no invitation: every target of the Hub that is online gets a stream as soon as the Hub reports it. A device is invited by the group `"Invite a device"` or by `QAR_PANEL_INVITE_PEERS`, in the mode This PC only, and the invitation and the streams that follow it write these lines; each line has this form:

```text
LIVE: inviting devices at connect: %1
LIVE: QAR_PANEL_INVITE_PEERS was handled when this session first connected; it is not handled again on a resume.
LIVE: QAR_PANEL_INVITE_PEERS is ignored: device invites are served only to an app that joined the Hub on its own PC.
LIVE: QAR_PANEL_INVITE_PEERS entry '%1' skipped: the kind is hololens or headset.
LIVE: QAR_PANEL_INVITE_PEERS entry '%1' skipped: it needs a host (kind:host).
LIVE: invite %1 '%2' refused: %3
LIVE: inviting %1 '%2'; the Hub's answer follows.
LIVE: invited %1 as peer %2. Its stream opens when the device connects.
Invite peer: type the host name or address of the device in the field next to the device selector.
Invite %1 failed: %2
LIVE: sender creation in flight (async) — RViz stays responsive; the stream opens as soon as the peer completes negotiation.
LIVE: opened stream for peer %1
LIVE: closed stream for peer %1
```

The line `"LIVE: inviting devices at connect"` lists the entries of `QAR_PANEL_INVITE_PEERS`, which the panel handles once for a session, and the entries that fail are skipped one by one. The line `"LIVE: inviting %1"` is written when the button `"Invite peer"` sends an invitation, and the line `"LIVE: invited"` when the Hub has answered; it gives the first ten hexadecimal digits of the peer identifier that the Hub assigned to the device. A refusal or an error of the Hub is the text of `"Last error:"`, after `"Invite %1 failed:"`, and is logged after `ERROR:`. The line about the sender is written once, because the creation of a sender is asynchronous and reports "not yet" on most early ticks. The ten digits of the lines `opened stream` and `closed stream` are those of the target that the stream belongs to.

The manager of the 3D streams, which writes with the prefix `"[lib:"`, asks the sender factory for a sender every time a stream is created, and the factory answers "not yet" until the receiver has completed the negotiation. A caller that asks again for every tick would fill the log, so the manager writes the first answer of a wait as one line, an error, and counts the answers that follow. The wait ends with one line that gives the number of answers and the milliseconds: an information when the sender came, a warning when the stream was destroyed or the factory was exchanged first. A wait that is open when the manager is destroyed ends without a line. Each line has this form, with the peer after it in the form `(peer %1)`:

```text
[lib:%1:%2] %3
sender factory returned null for peer
(repeats are counted; one line follows when the wait ends)
sender factory returned the sender for peer after 
sender wait for peer ended without a sender (
null result in 
null results in 
stream destroyed
sender factory replaced
```

The adapter prints the bootstrap and the peers on the same stream, and the lines below are the ones that decide most cases. The lines about the logging come from the session owner when it initialises the library, and the other lines from the adapter of the render streams. They are always printed; `QAR_ADAPTER_TRACE` adds a pair of lines around each main call of the bootstrap; each line has this form:

```text
[qar-adapter] library logging: severity=%s console=%s folder=%s%s
[qar-adapter] zenoh logging: off (QAR_ZENOH_LOG not set)
[qar-adapter] *** %s
[qar-adapter] *** SEH FAULT in %s: code=0x%08lX addr=%p module=%s
[qar-adapter] app volume '%s' id=%s placement read back: volume pose=(%.2f, %.2f, %.2f) app_scale=%.3f (asked: centre_y=%.2f m, size=%.2fx%.2fx%.2f m, app_scale=%.3f)
[qar-adapter] app volume '%s' id=%s: the placement could not be read back
[qar-adapter] *** peer %s refused: already streaming to %zu peers (max_peers=%u)
[qar-adapter] create_sender: peer %s still creating after %.1f s — the receiver has not completed transfer negotiation (the render tick keeps running).
[qar-adapter] create_sender: async creation FAILED for peer %s
[qar-adapter] created GL context for QAR HGLRC=%p (shares Ogre's %p) [sender #%zu]
```

The line about the app volume says whether the placement that the Hub holds for the volume was read back after the volume was made: a volume that existed already keeps the placement the operator gave it, and the line shows it next to what the plugin asked for. A line that starts with `***` is a failure; the text after it names the call and the status that QarOS returned.

The adapter also traces the start of each 3D stream. Its lines have the time of day in front and name the peer by the first eight digits of its identifier and the thread that did the step, and they come in this order: the request for the sender, the sender handed over by the runtime with the seconds that the creation took, the first frame that the sender shows with the numbers of its four textures, and the first time the graphics thread of the runtime makes the context of the sender current, with the handle of that context and of its device context. The textures are the colour of the left and of the right eye, then their depth, as `c<left>/<right> d<left>/<right>`, and the size of the first one. A viewer that stops asking for frames for two seconds or more and then asks again leaves a stall. The first frame after it writes one line with the length of the stall, the mean colour of that frame, which is read back from the GPU once, and the position of the head in the space of the app; a colour of `"(unavailable)"` says that it could not be read back. Each line has this form:

```text
[qar-adapter] %s %s
peer 
 create_async tid=
 created (callback tid=%llu) after %.2f s
 first show_frame tid=%llu tex=c%u/%u d%u/%u %ux%u
 SDK bound ctx 
 hdc=
LIVE: first 3D frame after a %.1f s stall: mean colour %s, head app (%.2f, %.2f, %.2f)
```

The geolocation writes these lines when it starts and when it anchors the scene; each line has this form:

```text
GEO: listening for NavSatFix on '%1' (QAR_PANEL_GPS_TOPIC; QAR_PANEL_GEO=off disables).
GEO: app volume anchored at lat=%1 lon=%2 alt=%3 m (ECEF WGS84, ENU axes).
GEO: anchor NOT set — %1
GEO: no ROS node available; geolocation disabled for this session.
GEO: no callback group on RViz's node (%1); geolocation disabled for this session.
```

The last line is written instead of the first one when the node cannot make the callback group of the panel, which the subscription joins; `%1` is the reason that the node gives.

## AR panel lines

The lines of the panel announce a stream and its close, and the library of Qt and the adapter write what each step did. A stream of a Qt window starts with these lines; each line has this form:

```text
AR PANEL: close mode '%1' (%2) - a streamed window that closes %3.
AR PANEL: %1 streamable panel(s) found.
AR PANEL: streaming '%1' as a QarOS GUI panel. The QarOS panel itself is created on the next tick, off this click.
AR PANEL: '%1' is in the RViz dock '%2'; closing that dock %3.
AR PANEL: '%1' raised by %2 m so that its lower edge lies %3 m above the RViz ground (room y %4).
AR PANEL: '%1' start cancelled (%2) while its dock was being fitted.
[qar-panel-adapter] %s panel '%s' %s: id=%s stream=%s buffer=%ux%u px
[qar-panel-adapter] %s panel '%s' subscribed for AR click+hover: id='%s' content=%ux%u px
[qar-panel-adapter] %s panel '%s' streams but takes no AR input: %s
```

The close mode line is written once, when the first panel is streamed. `%1` is `destroy`, `notice` or `park`. `%2` says where the mode comes from, which is `QAR_PANEL_CLOSE_MODE`, `"from the panel settings"` or `"the default"`, and for `park` the words `"AR Visibility is hide:"` stand in front of it, because the mode is decided by that setting. `%3` is what a window that closes does: `has its AR panel closed and its video sender destroyed`, `leaves its AR panel up as a closed-window notice and keeps its video sender until the session ends`, or `is hidden and parked in AR: nothing is closed while the session runs, its QarOS panel and video sender are kept until it ends, and opening the window again shows the panel again`. When the choice changes while the panel runs, the line `close mode is now` gives the new mode, and says that panels that are parked or show a notice stay as they are. The line about the dock says what closing that dock does with its AR panel in the mode in force: it closes the panel, turns it into a notice, or hides and parks it. The line `"raised"` is written when the ground rule lifted a panel, and gives the raise in metres, the margin of 0.10 m above the RViz ground, and the height of the ground in room space; the name in front is the key of the panel, `dialog` for a dialog, or `control:` and the key of the instance for the AR control panel. The line `"start cancelled"` is written when a start that waits for its dock to be fitted ends before the stream is made, and names the reason: the session is not live, the dock is not visible, or the cause of a stop. The last line of the block above is the failure of the input subscription: the panel still streams, but a tap does nothing. The adapter looks for a panel of that name before it makes one. A panel of an earlier session that it finds is `reattached`: the stream of the new sender is put on it, and its size is changed when it differs, and it is shown again; the line of the attach says `created` or `reattached`, the name, the identifier, the state that QarOS reported for the panel before the adapter touched it, and whether the stream and the size were exchanged. When the name is taken by a panel of a web page or by a closed panel, the adapter tries the name with a number after it and says so, and when no free name is left it refuses the panel with the reason, so that the panel fails with the same text. Each line has this form:

```text
[qar-panel-adapter] 
 panel '
reattached
 name=
 id=
 state_was=
 stream_swapped=
 size_changed=
[qar-panel-adapter] %s panel '%s' name=%s is taken by %s panel, trying %s%s
[qar-panel-adapter] %s panel '%s' refused: %s
```

A close in the mode `destroy` writes the line of the panel, the line of the manager of Qt with the outcome of each step, and the three steps of the adapter with a time of their own, `hh:mm:ss.mmm`; each line has this form:

```text
AR PANEL: close requested for '%1' (%2): its AR panel is closed and its video sender destroyed — the [qt_lib] 'closed' line has each step. Starting it again makes a new AR panel.
[qar-panel-adapter] %s panel '%s' %s
closing: id=%s stream=%s (operator already closed it in AR: %s)
step 1/3: AR input + panel-update subscriptions cancelled
step 2/3: qar_gui_panels_close_panel ran, ok
step 2/3: qar_gui_panels_close_panel SKIPPED -- the operator already closed this panel in AR
detaching: id=%s stream=%s (the GUI panel is kept)
step 2/3: GUI panel kept for the next session
step 3/3: destroying the video sender
[qar-panel-adapter] %s panel '%s' retired: id=%s stream=%s took=%lldms -- GUI panel %s, video sender %s
```

The adapter prints `"close FAILED (see step 2/3 above)"` in the retired line when the close call failed, and then the video sender is destroyed anyway. When the session ends, the panel is not closed: the line `"detaching"` stands in the place of `"closing"`, step 2/3 says that the panel is kept for the next session, and the retired line says `kept` for the panel, because every QarOS panel was hidden before and the next session finds it again by its name. A panel is closed only when its sender cannot detach. The manager of Qt writes the close with the key of the panel after the text, in the form `[qt_lib:%1:%2] %3` followed by `(panel '%1')`. Its sentence names the stream, the frames that it served, the reason and the teardown, and ends with a sentence that says that the stream entry is gone; the fragments are these; each line has this form:

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
AR PANEL: re-using the retired AR panel for '%1' - same QarOS panel, same video stream, no new sender.
AR PANEL: '%1' (%2): its AR panel now shows the window-closed notice and stays up until you close it in AR (notice mode keeps the video sender until the session ends).
AR PANEL: '%1' gets the QarOS panel name '%2', because an earlier QarOS panel of this process already used '%3'.
dialog opened from '
' is streaming as a panel of its own
```

The last two lines are one line of the manager of Qt around the key of the panel that opened a dialog: the manager writes it when a dialog gets a panel of its own.

With hiding on, which is the default, a window that closes does not close its AR panel: the panel is hidden and parked, and nothing is closed until the session ends. The lines say which case it was. A close that is asked for from inside a click on the panel itself is carried out when the click has returned. A close of a panel that is parked already changes nothing. A panel that had no QarOS panel yet has nothing to park. Streaming the window again shows the parked panel again on the same QarOS panel and the same video sender, and the line says that no new QarOS panel is made; a panel that was hidden in AR is shown again in the same way. Each line has this form:

```text
AR PANEL: '%1' was hidden in AR; shown again
AR PANEL: '%1' was parked; QarOS is asked to show it again (no new QarOS panel).
AR PANEL: '%1' parked (%2): hidden in AR; its QarOS panel and video sender are kept until the session ends.
AR PANEL: park requested for '%1' (%2): it is hidden in AR and parked when the click being delivered has returned; its QarOS panel and video sender are kept until the session ends.
AR PANEL: '%1' (%2): it is parked already; hidden in AR, its QarOS panel and video sender are kept until the session ends.
AR PANEL: '%1' (%2): it had no QarOS panel yet, so there is nothing to park; starting it again makes one.
```

The manager of Qt writes the same events with the key of the panel. A parked panel and a notice count together against a cap of 16 for the session, and the manager warns when it is reached: no further panel stream is created then, and a window that already has a retired panel re-uses it. The fragments of the lines are these; each line has this form:

```text
parked '
' (reason:
): hidden in AR; its sender and QarOS panel are kept until the session ends
revived parked '
': QarOS is asked to show it again (no new QarOS panel)
could not hide '
' in AR (
no reason given
), so it shows the closed-window notice instead of being parked; its sender and QarOS panel are kept until the session ends
could not show the revived panel '
the parked panel for key
 has no window to show; add the window again
```

The first two lines are the park and the revival. The third is written when QarOS refuses the hide of a parked panel: the panel then shows the closed-window notice, and its sender and QarOS panel are kept all the same. The fourth is written once when QarOS refuses the show of a revived panel, which stays hidden, and the last when a parked panel has lost its window and has to be given one again.


The line with `gets the QarOS panel name` is written when the plugin starts a QarOS panel under a title that an earlier QarOS panel of the same RViz process already used: `%1` is the key of the panel, `%2` is the title that the QarOS panel gets, which is the title that was asked for with a number after it, and `%3` is the title that was asked for. A start under a title that no earlier panel used writes no such line, and neither does a start that only brings back a retired panel. The line `AR PANEL: streaming the hidden Displays panel as` names the title `"Displays (AR)"` that the plugin asks for, also at a restart, so the number of a restart is in the line with `gets the QarOS panel name` only.

### AR visibility lines

A panel is hidden and shown with a call to QarOS that the render tick makes, so a request and its outcome are two events, and the line of the outcome is written by the tick that carried it out. The key in the lines is the key of the panel, `"tool:"` and the name for an AR tool, or the key of an instance of the AR control panel. Each line has this form:

```text
AR VISIBILITY: '%1' hidden in AR; its QarOS panel and video sender are kept
AR VISIBILITY: '%1' shown again in AR (no new QarOS panel)
AR VISIBILITY: could not ask for '%1' to be %2: %3
AR VISIBILITY: '%1' was asked to be %2 and the manager has not carried it out in 30 s; the request is dropped
AR VISIBILITY: set_state(HIDDEN) failed for '%1': %2 - left visible (hiding is the way of the switch: nothing is closed)
AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - left as it is (hiding is the way of the switch: no panel is closed or made again)
AR VISIBILITY: set_state(HIDDEN) failed for '%1': %2 - closed it instead (close mode %3)
AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - left as it is (close mode %3 keeps the QarOS panel)
AR VISIBILITY: set_state(VISIBLE) failed for '%1': %2 - %3
```

The first two lines are the outcomes that QarOS accepted. The third is written when the manager refuses to record the request, and the fourth when it has not carried the request out in 30 seconds, which drops it. The others are written when QarOS refused a hide or a show; `%2` is the reason that QarOS gave, or `QarOS refused the request`. With hiding on, a refused hide leaves the panel visible, and a refused show leaves the panel as it is; nothing is closed and nothing is made again, because closing a QarOS panel while a session runs has crashed the desktop Visualizer. With closing as the way of the switch, a panel that cannot be hidden is closed, and a panel that cannot be shown is closed and started again, except in the close mode `notice`, where a close keeps the QarOS panel, so that the panel is left as it is. The last line gives the outcome in `%3`: `"re-created it"`, `"closed it, but starting it again failed"`, `"not re-created: its close is still under way"` or `"not re-created: it could not be closed"`.

### Lines of the panel adapter

The adapter of the panels writes straight to the standard error, with the time of day in front and the key of the panel. It writes a line after each change of the state of a panel that QarOS accepted: `VISIBLE`, `MINIMIZED`, `HIDDEN` or `CLOSED`, of which the plugin uses the first and the third. It also writes a line each time the runtime republishes a panel with another state, another stream as its content, or another size to six decimals: the record of the runtime, numbered from 1 for each panel, with the state, the identifier of the stream that the content of the panel is, the identifier of the stream that the sender was built with, `match` or `MISMATCH`, and the size in metres. A `MISMATCH` says that the runtime shows a stream other than the one the adapter built the sender with, and it is also written when the runtime named no stream. At most 256 records wait for the render thread, and a record that finds the queue full is counted as dropped. Three lines are written once for a stream: the first frame that the sender shows, with the number of the thread, the turn of its transfer to reconnecting after frames were shown, with the stream and the number of frames, and the pause of the stream by the receiver. The adapter of the URL panels of the AR tools writes the change of state with the identifier of the panel and, when the call failed, `FAILED` and the error. The parts of the lines are these; each line has this form:

```text
[qar-panel-adapter] %s panel '%s' state -> %s
panel '
' runtime record #
: state=
 content_stream=
 (ours
:
MISMATCH
) size=
' stream
: transfer RECONNECTING after
: paused by the receiver
' first show_frame tid=
[qar-url-panel-adapter]
' state ->
 (id
 FAILED (id
```

A panel that QarOS reports in the opposite state shortly after an adapter set the state it wants is asked again, by both adapters, and the adapter says so. A panel of an earlier session that was left hidden can be reported as hidden again a few milliseconds after the adapter showed it. After a change that the adapter made itself (the panel was made or found again, or it set a state), a report that shows the panel hidden while visible is wanted, or the reverse, is contested for 5 seconds: at most three times within 10 seconds, and an ask is held for 1 second, because a contrary report inside the hold may be the echo of the state that was there before. A report that confirms the wanted state and every state that the adapter sets itself start the count anew, and when the hold has ended with no newer report, the adapter asks again. Only the pair hidden and visible is contested, so a state that the operator sets later in AR is never fought. Each line has this form, with `qar-panel-adapter` or `qar-url-panel-adapter` in the first brackets:

```text
[%s] %s panel '%s' QarOS reported %s while %s is wanted - asked again (%u/%u)
[%s] %s panel '%s' WARNING: QarOS keeps reporting %s while %s is wanted - gave up after %u asks
[%s] %s panel '%s' does not watch its state: %s
[%s] %s panel '%s' *** qar_gui_panels_set_state failed: code=%lld
```

The warning is written only for a report that arrives after three asks that nothing confirmed. The line `does not watch its state` says that the subscription to the updates of the panel could not be made, in which case the panel works without the guard.

### Lines of the library of Qt

The library of Qt writes the lines about each panel that it streams, and the panel forwards them in the form `[qt_lib:%1:%2] %3`, with the key of the panel after the text. When a panel gets a sender, at its creation and after a resize that settled, the library writes one line with the whole geometry of the panel: the logical size, the pixel ratio of the display, the supersample, the effective ratio, the `content_scale` that is sent, the size of the content and of the buffer in pixels, and the size in metres. The size in metres ends with the video that QarOS draws, the four insets of the frame it draws it inside, and the fit margin, which is the height that the panel has beyond its video and the insets; the line of the panel of a dialog, which fits its width to the frame, also says how the width was fitted. The parts of the line are these; each line has this form:

```text
: logical
 px, dpr
, render_scale
, effective_ratio
, content_scale sent
, content
 px, buffer
 (video
 m inside a frame of top
, bottom
, left
, right
 m, fit margin
 (a dialog: width
 m for the frame
, limited to half of the width
, not applied because the frame and its fit margin are not below the video height
, not made wider than the widget asks
```

A panel that a size change replaces is not closed in the mode `park`: the library hides its old QarOS panel and keeps it until the session ends, and makes a new one. The line is an information; it is a warning when QarOS refused the hide, in which case the old panel stays visible next to the new one, and when the change takes the last of the 32 slots that the session may keep, the line says that the next size changes close their old QarOS panel, and that in the mode `park` such a close may crash the desktop Visualizer. At the limit the change closes the old panel and the line is a warning of that. When the session ends, each panel is hidden where QarOS can and left in place for the next session, and nothing is closed except a panel whose sender cannot detach. One line for each key gives the number of QarOS panels that were left hidden, with the number of those that were closed because their sender cannot detach, and a warning names the panels that could not be hidden; they stay visible in AR until QarOS removes them. The lines have the words `"ended the session for"` and `"left hidden in place, not closed"`.

A dialog is fitted on the desktop before it gets its panel, so that the QarOS panel is made once, at the final size. The line of the end of the fit gives the size, the number of rounds and a note: `"no scroll bar"`, the scroll bar that is left with its reason, `"clamped to the screen"` or `"not confirmed"`. A dialog that goes away while it is fitted gets no panel, and a dialog that the cap refuses is a warning.

A scroll bar in an AR panel is a defect that the operator cannot work around, because the pointer there is a ray and a wheel does not exist, so the library counts the scroll areas of a streamed window: when the first frame is sent, when the layout changes and 1.5 s after the first frame. The line of a census gives the key, why it was taken, the number of areas and how many of them are hidden, and then one description for each area. A bar that is still there 1.5 s after the first frame is said once for each area, as a note when the area scrolls by design and as a warning when it was not meant to scroll. The first frame of a panel and the first frame after a run of unserved ticks also write the mean luminance of the frame. Each line has this form:

```text
' scroll census (
):
 area(s),
 hidden:
 v=
 page=
 viewport=
 vbar=
 hbar=
 overflow=
 by-design
' 1.5 s after its first frame an area that scrolls by design shows a vertical scroll bar:
' still shows a vertical scroll bar 1.5 s after its first frame:
' overflows by
 % of its
 px viewport)
' frame luminance
 (first frame)
 (first frame after
 unserved ticks)
```

A panel that holds a sender and has never submitted a frame is reported once, as a warning, with the seconds, the streak of not-ready ticks and the identifier of the stream; the heartbeat repeats it as `"UNSERVED"` until a frame lands.

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
AR TOOL: %1 will be shown again on the next tick.
AR TOOL: %1 will be hidden on the next tick.
AR TOOL: %1 reuses its parked QarOS panel (no new QarOS panel).
AR TOOL: retired %1.
AR TOOL: retired %1; it had no QarOS panel yet, so nothing was closed or parked.
AR TOOL: retired %1; its QarOS panel is parked (hidden in AR) until the session ends.
AR TOOL: retired %1; its QarOS panel could not be hidden (%2), so it stays visible and is kept until the session ends.
AR TOOL: copied %1
```

The line that says exposed names the template that laid the tool out, and the line after it, `AR TOOL: %1 — %2.`, carries the size note: the size of the panel in metres and the number of columns, which the plugin recomputes when a page reports its viewport and writes when the size moved by more than 5 mm; the size comes from a fixed design viewport of 500 by 500 CSS pixels and not from the viewport that the page reports, so the note reads `"css px (design viewport)"`. A tool that is served and not in AR yet waits, and the line is written once. With hiding on, which is the default, `"Re-open in AR"` and the switches of the AR control panel show and hide the QarOS panel of a tool, and `"Remove tool"` hides it and keeps it until the session ends. The lines `"will be shown again"` and `"will be hidden"` say what the next tick will do, and the lines of the [AR visibility](#ar-visibility-lines) say what it did. A removed tool says what became of its panel: it had none, it is parked and hidden in AR, or QarOS refused the hide, in which case the panel stays visible and is kept; a tool that is exposed again takes over the parked panel and the line says that no new QarOS panel is made. Without hiding, `"Re-open in AR"` closes the panel and makes a new one on the next tick, and `"Remove tool"` closes it. The page reports its viewport and the panel refuses a control that it does not understand with these lines; each line has this form:

```text
[qar-web] %1
-> panel %1x%2 m, theme x%3, %4
AR CONTROL: refused '%1' on %2 — %3
AR CONTROL: publish to %1 %2
AR TOOL: %1 has no live subscription — %2
AR TOOL: could not expose %1 — %2
%1: a width of %2 m is below the %3 m floor - size held at %4
%1 resized %2 times in %3 s - size held at %4
```

The line that starts with `[qar-web]` reports the viewport that the page was laid out into: its CSS size, the pixel ratio, the size of its content and of its root, and the reason for the report. A resize guard keeps a size that changes without end from reaching QarOS: after three resizes within ten seconds the panel keeps its size and the first refusal is written once, and a width below the floor is refused in the same way. The lines name the tool as `"AR TOOL: '"` and an instance of the AR control panel as `"AR CONTROL PANEL: instance '"` in front of the text.

When RViz ends, the panel releases the entities that it made on the node of RViz, which are those of the AR tools, the publisher of the picks, the bridges, the graph discovery and the geolocation subscription, and then its callback group. If it had made a group, it writes one of these lines; each line has this form:

```text
ROS UI: the panel's ROS entities and their callback group on RViz's node are released.
ROS UI: the panel let its ROS entities go; the callback group on RViz's node is still held and goes with its last holder.
```

The first line says that the group is gone. The second says that a holder outside the panel still has the group, and that it goes when that holder lets go.

### The AR control panel lines

The AR control panel is a page that the plugin serves and shows in AR in front of each user. Its lines begin with `"AR CONTROL PANEL:"`. The first one is written when the page is made, at the connect or in the offline preview, and gives the address, whether there is one panel in front of each peer or one shared panel, and whether the facing flip is on; when the page is not made, a line says why, which is the setting `"Show the AR control panel when connecting"` being off or the reason that the panel server gave. A value of `QAR_PANEL_AR_CONTROL_PER_PEER` or `QAR_PANEL_AR_CONTROL_FLIP` that is neither on nor off is named in a line of its own, and the default is used. Each line has this form:

```text
AR CONTROL PANEL: page served at %1 - %2, facing flip %3
AR CONTROL PANEL: not created - the setting "Show the AR control panel when connecting" is off
AR CONTROL PANEL: not created -
%1='%2' is not a switch (on or off) - using %3
AR CONTROL PANEL: page viewers %1
AR CONTROL PANEL: refused '%1' - %2
AR CONTROL PANEL: '%1' - %2
AR CONTROL PANEL: instance '%1' for %2 at (%3) yaw %4 (%5)
AR CONTROL PANEL: could not create instance '%1' - %2; trying again every 5 s
AR CONTROL PANEL: the app volume has not reported its placement; the head pose is taken as it is (identity placement)
AR CONTROL PANEL: Show AR control panel pressed - QarOS is asked to show the panels again on the next tick
AR CONTROL PANEL: Show AR control panel pressed again within 10 s - hiding is the way of the switch, so no panel is made again; QarOS is asked to show the panels once more on the next tick
AR CONTROL PANEL: Show AR control panel pressed again within 10 s - the panels QarOS would not show are made again on the next tick
AR CONTROL PANEL: re-creating instance '%1'
AR CONTROL PANEL: could not show instance '%1' - %2
AR CONTROL PANEL: QarOS accepted set_state(VISIBLE) for %1 instance(s)
AR CONTROL PANEL: could not resize instance '%1' - %2
AR CONTROL PANEL: panel size %1 x %2 m - %3.
AR CONTROL PANEL: copied %1
AR CONTROL PANEL: the page's state could not be published - %1
AR VISIBILITY: control panel instance '%1' shown again (its peer is back)
AR VISIBILITY: control panel instance '%1' hidden (its peer's stream closed)
AR VISIBILITY: set_state(%1) failed for control panel instance '%2': %3
SPATIAL: mode = %1%2
```

The line `"page viewers"` gives the number of browsers that show the page. When the page reports a viewport with another CSS size, the line gives the same description as the `[qar-web]` line. A command from the page is answered by a line with the control and a short text. A refused command gives the reason, which the router names when the control is unknown, has no action, declares another action than the one it carries, or sends a value of the wrong kind, and which the panel names when there is no panel or tool in the slot, when QarOS is not connected, or when no pick waits for its confirmation. An accepted command gives `"picking off"`, `"Point armed"` or `"Nav goal armed"` for the pick mode, `"published to"` and the topic for a confirmed pick, `"nothing to cancel"` or `"pick dropped"` for a cancel, and, for a panel or a tool, `"already on"` or `"already off"` when nothing is left to do, or `"showing"`, `"starting"`, `"hiding"` or `"stopping"` with the title. A pick mode that the page sets writes the line `SPATIAL: mode = %1` with the origin ` (from the AR control panel)` after it.

The panels themselves are made and changed by the render tick. The line `instance` gives the key of the instance, who it is for, which is a peer or `everybody`, the position in room space, the yaw in degrees and how the pose was found: from the head and the gaze of the viewer, with the flip, and in front of the first peer for a shared panel; or `no head pose: fixed spot`; a panel that the ground rule lifted adds the raise and the height of the ground. When the app volume has not reported its placement, one line says that the head pose is taken as it is. A creation that fails is written once for each reason and tried again every 5 seconds. The size of the panels comes from the page for the fixed design viewport and not from the viewport that the browser reports: a line gives the size in metres and the note of the sizing when the size moved, a resize that QarOS refuses names the instance, and the resize guard holds the size of an instance in the same way as that of a tool. A panel of a peer whose stream closed is hidden, and shown again when the peer is back.

The button `"Show AR control panel"` writes a line for each press. The first press asks QarOS to show the panels again on the next tick. A second press within 10 seconds, after a panel could not be shown, asks once more when hiding is the way of the switch, and makes the panels that QarOS would not show again when it is not, with a line for each instance that is re-created. The lines `could not show instance` and `QarOS accepted set_state(VISIBLE)` give the outcome of the request, and the button `"Copy page URL"` writes the address that it copied.

### Test marker lines

The button `"Start test markers"` starts a publisher of markers with the python and the script that `QAR_PANEL_TEST_MARKERS_PYTHON` and `QAR_PANEL_TEST_MARKERS_SCRIPT` name, and writes a line for each event. What the script prints goes to the log after `"TEST MARKERS:"`, at most one line a second; the lines that are held back are counted, and the count is added to the next line that is shown and said again when the process ends. A start that is not possible names what is missing: a variable that `"is not set"` or a file that `"does not exist"`. The line of the end says whether the button stopped the process, which exit code it had and whether it crashed, and a process that is still running when the panel closes is killed. Each line has this form:

```text
TEST MARKERS: not started -
TEST MARKERS: started (%1 %2)
TEST MARKERS: could not start (
TEST MARKERS:
 (%1 more line(s) since the last one shown)
TEST MARKERS: %1 output line(s) were not shown (at most one line a second is logged)
TEST MARKERS: stopped
TEST MARKERS: exited with code %1%2
 (the process crashed)
TEST MARKERS: stopped (the panel is closing)
```

## Video lines

A video panel writes lines when it opens and closes, and every two seconds while it streams, one pair of lines for each video panel and for each region of a dock; each line has this form:

```text
AR VIDEO: video topics are received in their own ROS context (a DDS participant separate from RViz's), so RViz's own displays of the same topic cannot starve them.
AR VIDEO: could not create the private ROS context for video topics (%1) — '%2' will show no frames.
AR VIDEO: '%1' reusing its existing ROS subscription — no second subscribe/decode for the same topic.
AR VIDEO: %1 video panel for '%2' (%3).
AR VIDEO: '%1' is waiting for its first frame; its AR panel is made when it arrives.
AR VIDEO: '%1' got its first frame (%2x%3 px); its AR panel is made now.
AR VIDEO: '%1' could not make its AR panel: %2
AR VIDEO: '%1' closed; stopping its ROS subscription.
AR PANEL: '%1' %2; stopping its AR video region's ROS subscription.
AR PANEL: '%1' is an RViz %2 display — its render window cannot be captured, so '%3' is streamed into the AR panel directly.%4
AR PANEL: '%1' has no Topic set — its render window will show blank in AR until one is.
```

A video panel makes its AR panel when its first frame has arrived, so that the QarOS panel is made once, at the final size of the picture. The line `"is waiting for its first frame"` is written once when no frame has come after five seconds, the line `"got its first frame"` gives the size of the frame, and `"could not make its AR panel"` gives the reason and sets `"Last error:"`. The line `stopping its AR video region` is written for each video region of a dock whose panel is parked or closed, with the word `parked` or `closed` after the name of the display; a parked video panel itself keeps its ROS subscription and its widget, so that showing it again needs no new QarOS panel.

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
SPATIAL: mode = %1%2
SPATIAL: target = %1 - %2
SPATIAL: published %1 to %2
SPATIAL: pick canceled
Close polygon: need at least three vertices in polygon area mode.
```

The line `SPATIAL: mode = %1%2` ends with ` (from the AR control panel)` when the page of the AR control panel set the mode. The line `SPATIAL: scene placement %1` reports where QarOS displays the scene, and the next lines say that the placement is reported and not applied: the gesture points are in the content space of the app, so a pick uses the change of basis and nothing else. The first 20 gestures and then one in 50 are traced with the point that QarOS gave, the point after the conversion, and the pick that the armed target resolved; each line has this form:

```text
SPATIAL: sample #%1 %2/%3 commits=%4 room=(%5, %6, %7) -> ros=(%8, %9, %10)%11
SPATIAL: ... scene displayed at (not applied to the pick) %1
SPATIAL: sample #%1%2 pick=%3 via %4 on '%5'%6
, NO RAY: the action point sat %1 m off the plane and was projected onto it
, QarOS's own scene intersection, published verbatim
```

The words after `via` name the method: `"QarOS action point"` when the answer is the intersection that the runtime computed (the detail of the line calls it a scene intersection, as QuaternAR states it), `ray/plane` or `ray/depth` when the straight aiming ray was intersected with the target, and `projected` when there was no ray. The ray stands between the number of the sample and `pick=`: it is written as `ray=` with its origin and its direction in the frame of the pick, and as `ray=none` when the sample has no hand ray. The adapter writes a longer trace of each gesture, with the identifier, kind and phase of the gesture, the points and the ray, and the verdict of the converter; each line has this form:

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
AR PANEL: the hidden Displays panel is not started on connect (%1). '%2' in the panel list still starts it by hand.
AR PANEL: the hidden Displays copy starts now; it waited for the first peer's 3D stream so that it is not made before anything can receive it.
AR PANEL: created a hidden RViz '%1' panel (%2): %3x%4 px (%8), shown off-screen only - it never appears on the desktop - with item dragging off%9 on this copy. It views RViz's live display tree and will stream as '%5' ("%6"). RViz's panel factory verified by its own RTTI as '%7'.
AR PANEL: the hidden Displays copy is being fitted to its display tree; it streams when the fit is over.
AR PANEL: '%1' start cancelled (%2) while the copy was being fitted.
AR PANEL: streaming the hidden Displays panel as '%1' ("%2", %3x%4 px) at room (%5) m. The QarOS panel itself is created on the next tick, off this call. 'Stop streaming' and 'Stream panel to AR' act on the stream only: the widget stays shown off-screen.
AR PANEL: could not create the hidden Displays panel (class id '%1'%2%3): %4 -- continuing without it.
AR PANEL: the hidden Displays panel was not started: %1
AR PANEL: the hidden Displays panel had been hidden by something (a hide() or close() on it); it is shown off-screen again. Said once: a hidden widget would have streamed a blank rectangle.
AR PANEL: '%1' was closed in AR; QarOS is asked to show it again. Hiding is the way of the switch, so the panel is neither closed nor made again.
AR PANEL: '%1' was closed in AR, which stops its capture but leaves its stream entry; clearing that entry before starting it again.
```

The text after `"could not create"` is the reason that the creation reports, and it names the first check that failed: the RViz window has no panel factory, the layout of the window is not the one that the plugin was built against, the factory failed the type check, the factory made another class, or an exception was thrown. The plugin continues without the copy. The copy is made when the first peer's 3D stream is open, so that it does not show black until something can receive it, and the line `"starts now"` says so; the line `"not started on connect"` names the setting or the variable that keeps it from starting by itself, and the entry in the panel list still starts it by hand. The creation line says in `%8` how the size was chosen: `size from QAR_PANEL_HIDDEN_DISPLAYS_SIZE` when the variable gave a size, `fitting to its display tree before it streams` otherwise, or `not fitted: it has no tree view`; `%9` is ` and its help area hidden` when the help area was hidden. A copy that is fitted is made as high as its display tree needs, in rounds that the lines with `"fit round"` report as for [the fit of a dock](#size-and-settings-lines), and it streams when the fit is over; a start that is asked meanwhile is told so, and a start that is cancelled during the fit gives the reason, which is that the session is not live, or the stop of the session. The streaming line gives the key, the title `"Displays (AR)"`, the size in pixels and the three coordinates of the position in room space, in metres. While the copy streams, a change of its display tree earns a new census of its scroll areas, with the reason `display tree changed`. With hiding on, a copy that the operator closed in AR is shown again by the line `"was closed in AR; QarOS is asked to show it again"`, and nothing is closed or made again.

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
RENDER: no render system yet
RENDER: 
, device '
Render tick: %1 ms (%2)
Prime next frame: %1
TIMING: per-frame stage timing is ON -> %1 (video_*.csv, rx_*.csv, render3d*.csv, gui_gaps.csv; epoch microseconds; flushed ~1/s by a background thread, never on this one)
```

The `SCHED:` line is the first part followed by the second. A line that starts with `"Floor plane: frame='"` is written for each Grid display when it is created and whenever its frame, plane, cell count, cell size or offset changes, and not on every tick; it gives the size of the plane and the depth bias.

The line `"RENDER:"` names the render system that RViz renders the streamed scene with and the device behind it, read from Ogre once, with the other startup lines; `"RENDER: no render system yet"` says that there is none, as in a preview, and a device without a name is shown as `<unknown>`. Only printable ASCII goes into the line. The line `"Render tick"` gives the period in milliseconds and where it comes from, which is `QAR_PANEL_TICK_MS` or `"default"`.

## QarOS library logs

The QarOS library and the processes that QAROS starts write their own files, the library into the QarOS log folder and the processes below its folder logs. What they write depends on `QAR_LOG_SEVERITY`, which the library reads in preference to its init structure, and the plugin sets it from its own decision, so that the severity in the line that begins `LIVE: QarOS library logging:` is the severity that applies. At `trace` the library writes hundreds of thousands of lines in a short run, [How much a trace log writes](TROUBLESHOOTING.md#how-much-a-trace-log-writes) explains why a quiet run is the default of the launcher. The log of the zenoh transport is the only place that names the reason for a session that does not come up; the adapter switches it on when `QAR_ZENOH_LOG` is set and the severity allows it, and it always writes one line that says whether it did; each line has this form:

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
