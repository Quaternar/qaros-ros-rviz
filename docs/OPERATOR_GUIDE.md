# Operator guide

## The Qar Streaming panel at a glance

The plugin adds one panel to RViz, the class `qar_rviz_streaming/QarStreamingPanel`, which the configuration `qar_streaming_dev.rviz` loads under the name `"Qar Streaming"`. The panel is a vertical stack of six groups: `"Qar runtime"`, `"Stream settings"`, `"Peers"`, `"AR GUI panels"`, `"Spatial AR interaction"` and `"Diagnostics"`. The panel can stream itself into AR, and a change of the size of a streamed window makes the library rebuild the QarOS panel of that window. The status widgets are therefore laid out so that no text can widen the dock, and the long readout of the AR panels scrolls inside a box of ten lines.

Three buttons change their labels with the mode. The launcher `scripts\run_live_rviz_stream_real.ps1` sets `QAR_PANEL_LIVE_STREAM`, and with it the panel is live-capable: the button that starts the session is `"Connect to QarOS"`, the button that pauses it is `"Disconnect (pause)"`, and the button that adds a peer is `"Invite peer"`. Without that variable the panel is in the [offline preview mode](#offline-preview-mode), and the same three buttons are `"Start Qar"`, `"Stop Qar"` and `"Add simulated peer"`. The table lists the controls in the order of the panel, with the live label first.

| Group | Control | What it does |
|---|---|---|
| `"Qar runtime"` | the state label | shows the state of the connection: `"Qar runtime: STOPPED"`, `"Qar runtime: CONNECTING..."`, `"Qar runtime: RUNNING (live)"` or, in the preview, `"Qar runtime: RUNNING (offline preview)"` |
| | `"Connect to QarOS"`, in the preview `"Start Qar"` | builds the session of QarOS and starts the streaming; after a pause it resumes the streaming |
| | `"Disconnect (pause)"`, in the preview `"Stop Qar"` | pauses the streaming and keeps the session open |
| `"Stream settings"` | `"Resolution:"` | the size of the pictures of the streams that are created from now on: 640 x 480, 1024 x 1024, 1280 x 720, 1600 x 900, 1920 x 1080 or 2048 x 2048, and 1280 x 720 at the start |
| | `"FPS:"` | the frame rate that a stream may reach: 15, 30, 60, 72 or 90 frames per second, and 60 at the start |
| | `"Render tick:"` | how often the timer of the panel fires: 16, 8, 4, 2, 1 or 0.1 ms, and 0.1 ms at the start |
| | `"Prime next frame"` | whether the wait for the next frame of QarOS starts right after the previous frame was shown; on at the start |
| `"Peers"` | the drop-down of device kinds and the field for the host name | in live mode only: `Visualizer`, `"HoloLens 2"`, `"Meta Quest"`, `"ZED camera"` and the host name of a HoloLens |
| | `"Invite peer"`, in the preview `"Add simulated peer"` | invites a device into the running session; in the preview it adds a simulated peer |
| | `"Remove peer"`, `"Start stream"`, `"Stop stream"` and the list | act on the selected entry of the list, which holds only simulated peers: the peers of QarOS are not listed |
| `"AR GUI panels"` | `"Add from ROS graph..."` and `"Remove topic"` | open the browser of the ROS graph; forget a watched topic |
| | the list of tools, `"Remove tool"`, `"Re-open in AR"`, `"Copy URL"`, the spin box `size x` and the label `"AR tools: none"` | manage the AR tools that were generated from the graph |
| | the drop-down, `"Refresh"`, `"Stream panel to AR"`, `"Stop streaming"` and the readout `"AR panels: none"` | stream a Qt window of RViz as a QarOS panel, stop it, and watch it |
| `"Spatial AR interaction"` | `Mode`, `Target`, `Frame`, `"Publish without confirming"`, `"Confirm pick"`, `"Cancel"`, `"Close polygon"` and the label `"Spatial: off"` | turn the pointer of the headset into ROS messages |
| `"Diagnostics"` | the label `"DisplayContext: (not initialized)"`, `"Refresh diagnostics"`, `"Capture offscreen frame"`, `"Capture stereo pair"`, the log view and the label `"Last error: <none>"` | show what the panel knows and what it did |

Two things need a session of QarOS. Without one, `"Stream panel to AR"` reports `QarOS session is not running — connect to QarOS first (live mode)` as the last error. An AR tool that you expose or re-open before you connect is served at once but stays `"[pending]"`, and the panel logs once that the tool is served but not in AR yet. [Diagnostics view and status lines](#diagnostics-view-and-status-lines) explains the readouts.

## Connecting and pausing

`"Connect to QarOS"` builds the session of QarOS on a worker thread, so that RViz stays responsive. The call that creates the session spawns the runtime launcher of QarOS and takes a few seconds. The render tick polls the worker, and the moment that the session is ready the tick adopts it; from then on only the GUI thread calls into the session. The panel does not start the runtime launcher itself: the library of QarOS starts and configures it, from the folder that the launcher script gives in `QAR_RUNTIME_BINARIES_DIR`. If you close RViz while a connection is in flight, RViz waits for the worker, which can take about three seconds, because unloading the library of the plugin under a running thread would be worse.

The connection happens when you press the button and never when the panel loads. A connection at load time would run the whole handshake of the transport on the GUI thread before the event loop of Qt is alive, and the asynchronous coordination of the runtime with its peers could not make progress. While the session builds, the state label says `"Qar runtime: CONNECTING..."`, and a second press of the button only writes that a connection is in progress. An error ends the attempt: the label `"Last error: <none>"` shows `"QarOS connect failed: "` or `"QarOS connect threw: "` followed by the reason, and the button tries again. The state label is not refreshed at that moment, so it can go on saying `"Qar runtime: CONNECTING..."` after a failed attempt; the line in the log that begins with `ERROR:` and names `"QarOS connect failed"` shows that the attempt has ended.

When the session is open, the panel adopts it: the streaming of the 3D scene starts, the plugin subscribes to the click and hover gestures of the app volume, the hidden Displays panel starts unless you switched it off, and the label becomes `"Qar runtime: RUNNING (live)"`. The connection invites nothing by default, so no Visualizer opens until you ask for one, and the panel says so in the log.

`"Disconnect (pause)"` stops the streaming of the 3D scene and keeps everything else: the session stays open, and the AR panels and the AR tools stay open and keep running. It is a pause and not a disconnect: the session of QarOS and its handles are destroyed when RViz closes, in the order that QarOS requires, the streams and panels before the session. Pressing `"Connect to QarOS"` again resumes the streaming on the open session: the panel runs the steps of the connection again without building a session, and writes the line `"Connected to QarOS:"` of the connection. The line `"QarOS: streaming resumed."` is written only by a press on `"Connect to QarOS"` while the panel is still connected, that is, a press with no pause before it.

## Inviting peers

A peer is a device or an application that receives a stream. Each admitted peer gets its own stereo stream of the scene, rendered from the head pose that QarOS reports for it. The drop-down next to `"Invite peer"` offers four kinds. Press the button when the label shows `"Qar runtime: RUNNING (live)"`: before the connection, and after `"Disconnect (pause)"`, the button adds a simulated peer to the list instead of inviting a device, which is not what you want.

| Kind | Needs the host name | What the invitation addresses |
|---|---|---|
| `Visualizer` | no | the QarOS Visualizer that the runtime launcher starts on this machine from the bin folder of the SDK |
| `"HoloLens 2"` | yes | the HoloLens with that host name; it is the only kind that QarOS can address by name |
| `"Meta Quest"` | no | a Quest |
| `"ZED camera"` | no | a ZED camera |

**Several Visualizers.** The connection string of a Visualizer that the SDK builds is bare, so every invitation would resolve to the same peer. The plugin therefore appends a payload: with the default `auto`, a new random identifier for every invitation, so that each invitation opens a Visualizer of its own, and QarOS starts one mixer process for each identifier. `QAR_PANEL_VISUALIZER_GUID` set to `off` restores the bare string, so that repeated invitations address the same Visualizer and return the peer that exists; any other value is used as the payload and addresses one instance. A text in the field of the host name wins over all of them. An unset variable and an empty one both mean `auto`. After the first invitation of a Visualizer the panel writes a note about this to the log.

One Visualizer is one head. The launcher parameter `-Visualizers`, from 0 to 8, sets `QAR_PANEL_VISUALIZERS`, the number of Visualizers that the plugin invites when it connects; the default is 0. `QAR_PANEL_MAX_PEERS` sets the ceiling of the peers that are served at once, 8 by default. A peer above the ceiling is refused with a logged warning, because every peer costs four scene renders per frame, a colour render and a depth render for each of its two eyes.

**Invitations at connect.** `QAR_PANEL_INVITE_PEERS` holds entries of the form `kind[:hostname]`, separated by commas, and the launcher parameter `-InvitePeers` fills it; the plugin reads it when it connects. The kinds are `visualizer`, `hololens`, `quest` and `zed`, in any case, with the aliases `viz`, `hl`, `hololens2` and `metaquest`, and only `hololens` needs a host name. An entry that fails is logged and skipped, so that one unreachable headset does not stop the others.

**Admission.** The subscription of QarOS to the peers reports everything that joins the session, including the internal peers of the runtime, and with an SDK drop earlier than that of 2026-09-28 one Visualizer appeared as five identifiers. A sender built for an internal peer would mismatch the application identifiers, and the runtime would reject the frames of the real Visualizer. The admission policy therefore decides which of the reported peers get a stream. `QAR_PANEL_PEER_ADMISSION` selects the policy, and the launcher parameter `-RealPeers` sets `new`. An unknown value is reported as an error and leaves the default in force.

| Policy | Admits | When to use it |
|---|---|---|
| `invited` | only the peers that the plugin invited, by exact identifier; the default | the Visualizers of this machine and every device that you invite |
| `new` | the invited peers and any peer that first appears after the settle time `QAR_PANEL_PEER_SETTLE_MS`, 5000 ms by default | a demo with devices that join by onboarding and that the plugin never invited; it works on timing, so inviting is better |
| `named` | the invited peers and the peers whose display name contains the text of `QAR_PANEL_PEER_NAME_FILTER` | not usable in practice, because QarOS gives a peer another generated name at each update |
| `any` | every peer except the plugin | diagnosis only: it admits the internal peers of the runtime too |

Pressing `"Invite peer"` is a synchronous call into QarOS on the GUI thread, and RViz does not repaint while it runs. The panel logs the invitation and, a little later, that the stream of the peer opened. The list of the group `"Peers"` shows only the simulated peers of the preview; the heartbeat shows one field for each stream of QarOS.

## Stream settings

The group `"Stream settings"` holds the size and the rate of the pictures and two switches for the timing of the render loop. `"Resolution:"` and `"FPS:"` are read when a stream is created, for each new peer, and they do not resize a stream that exists. The scheduler renders the due streams that fit the budget in one batch, under one render lock, so that every peer sees the same snapshot of the scene. A batch gets a budget of about 16 ms: frames of peers beyond the budget are shed for that tick, except that a due peer that was not rendered for 1/30 s is always served, and the heartbeat counts the shed frames as `budget_skips=`. Every peer costs four scene renders per frame, a colour render and a depth render for each of its two eyes, so choose a lower resolution when you serve several headsets.

`"Render tick:"` is the period of the timer that drives everything that the panel does each frame: the heartbeat, the AR panels, the AR tools, the spatial picks, the floor plane and the streams. The drop-down offers 16, 8, 4, 2, 1 and 0.1 ms, a value of `QAR_PANEL_TICK_MS` that is not among them is added to the list, and the start value is 0.1 ms. The switches are live: they apply at once, with no restart. They tell a wait that the render tick causes apart from a mismatch of the SDK, and they do not change the frame budget of a peer, which still decides how often the peer is due.

The timer is always a precise timer of Qt, because the coarse timer is served from the system tick of Windows of about 15.6 ms and would turn even a period of 16 ms into about 31 ms. A timer cannot have a period below 1 ms, so `"0.1 ms"` runs a timer without an interval, which fires whenever the event loop is idle, and the body of the tick runs at most once in each 100 microseconds.

`"Prime next frame"` decides when the wait for the next frame of QarOS starts. On, which is the start value and the default of `QAR_ADAPTER_PRIME`, the wait starts right after a frame was shown, so that one tick serves one frame. Off, the wait starts at the next tick, so that two ticks serve one frame. The heartbeat shows the values in use as `tick=` and `prime=`.

## AR GUI panels

The group `"AR GUI panels"` streams a Qt window of RViz as a QarOS GUI panel. The drop-down lists what can be streamed: first `"This QAR panel"`, then every dock of RViz by its title, then one entry `Video: <topic>` for each video panel that an AR tool opened, and last the hidden Displays copy. The list is filled when the panel loads and once more at the next turn of the event loop; `"Refresh"` fills it again and logs how many windows it found, so press it after you add a dock.

`"Stream panel to AR"` starts the stream of the selected entry and `"Stop streaming"` ends it. The press does no call into QarOS: the QarOS panel is created on the next render tick, on the GUI thread, with synchronous calls into the runtime, so that RViz can stand still for a moment then. The first panel is placed at a height of 1.5 m and 1 m in front of the origin, and each further panel is placed 0.5 m further along the x axis, because no call of QarOS asks where the operator is: the pose of the viewer comes only with a render sender, and the plugin does not use it to place panels. A panel is captured at 15 frames per second at most, and at 30 when its dock holds an Image or Camera display.

A panel that is started again under a title that an earlier AR panel of the same RViz process used carries a number after the title: the second start of the dock Views is titled Views (2), and the third Views (3). The key of the panel and the entries of the list do not change, and the count lasts until RViz is restarted. When the hidden Displays panel is started again after a stop, the Visualizer has crashed in every case that the authors of qar_rviz_plugin observed, with a numbered title as well, and it has crashed at times when another panel was closed or created; QarOS starts a Visualizer again after several seconds, during which the 3D streams stand still. Do not close and start panels in quick succession, and wait for the Visualizer to come back before you press the next button. [The Visualizer crashes when the hidden Displays panel is started again](TROUBLESHOOTING.md#the-visualizer-crashes-when-the-hidden-displays-panel-is-started-again) has the details.

The readout of the group starts as `"AR panels: none"` and shows live counters while a panel streams; it is refreshed up to four times a second. Its first line has the form `input: in %1, missed %2, dropped %3`, followed by the last reason of a drop: it counts the samples that came from the input source, those that pointed at no panel and those that the state machine refused, and an `in` of 0 means that the runtime sends nothing. The second line has the form `panels: live=%1 closed=%2 (destroyed) notice=%3`, and `arclosed=` follows when the operator closed a panel in AR. Then comes one line for each panel with its key, the size of its buffer in pixels and the size of the panel in metres, the frames that were sent and those that were left out because nothing changed, and the input that was received, delivered and dropped. A panel that waits because no receiver attached to its stream says `"waiting for a receiver"` with the number of ticks. A panel that was rebuilt more than once says `"rebuilds %1"` with the number, which is the sign of a window that changes its size. The marks `NOTICE`, `"CLOSED IN AR"` and `"WIDGET HIDDEN"` say that the panel shows a notice, that the operator closed it in AR, and that its widget is hidden, so that the AR panel looks blank.

A click and a hover in AR reach the real widget of RViz as Qt events, which includes the lists of drop-down boxes and the dialogs that the widget opens. A dialog becomes a panel of its own in AR, placed 0.5 m further along the x axis than the panel that opened it. The authors of qar_rviz_plugin confirmed the click, the hover, the drop-down lists and the dialogs in live sessions; the drag of a slider and the use of two hands are covered by unit tests, the tap count and the long press by neither a unit test nor a live session, and no live session has checked any of the four; [What the live sessions showed](VERIFICATION.md#what-the-live-sessions-showed) lists the evidence. [The hidden Displays panel](#the-hidden-displays-panel) and [Closing panels](#closing-panels) describe the two cases that need more.

## The hidden Displays panel

When the connection is up, the plugin makes a second instance of the Displays panel of RViz, shows it off-screen only, and streams it into AR as `hidden:Displays` with the title `"Displays (AR)"`. A start after a stop in the same RViz process gives the copy a number after the title, Displays (AR) (2) for the first restart and Displays (AR) (3) for the next, and `"Disconnect (pause)"` does not reset the count. At a restart the Visualizer has crashed in every case that the authors of qar_rviz_plugin observed, and QarOS starts it again after several seconds; [The Visualizer crashes when the hidden Displays panel is started again](TROUBLESHOOTING.md#the-visualizer-crashes-when-the-hidden-displays-panel-is-started-again) says what to do. The copy is bound to the same display tree as the real Displays dock, so a check box that you toggle, a property that you edit, and a display that you add, remove or rename in the headset change the displays of RViz. The AR panel hangs at x = -1.2 m, y = 1.5 m and z = -1.0 m, and the dialogs that it opens, such as the one of its `Add` button, are placed 0.5 m to its +x by the manager.

The copy never reaches your desktop. It is shown with the Qt attribute that maps a window without putting it on the screen, it does not accept keyboard focus, so that its shortcuts cannot fire and it cannot take the foreground from RViz, and it does not keep RViz alive after the main window closes. The drop-down lists, menus and completer lists that it opens appear in AR and never on the desktop, because the plugin keeps every popup of an off-screen panel off the screen; tooltips on Windows are not covered. Dialogs are the exception: they stay ordinary windows and also become AR panels. The dragging of items is switched off on the copy, because a drag that began with input from AR must never enter the drag and drop of Windows.

| Setting | Meaning |
|---|---|
| `QAR_PANEL_HIDDEN_DISPLAYS` | `off`, `0`, `false` or `no` stops the automatic start on connect, and the panel says so once; the entry of the drop-down still starts the copy by hand; the default is on |
| `QAR_PANEL_HIDDEN_DISPLAYS_SIZE` | the size of the copy in logical pixels as `WIDTHxHEIGHT`, 480 by 720 by default; each side is limited to 200 to 2000, a text that is not a size keeps the default, and each correction is logged as a warning |

The size of the copy is read once, with the widget, and never changes, because a change of size would rebuild the sender; a tall display tree scrolls inside. The last entry of the drop-down of `"AR GUI panels"` always offers the copy, whether or not it exists yet. `"Stream panel to AR"` on it creates the widget the first time and starts the stream, and later it starts the stream again. `"Stop streaming"` ends the stream only: the widget stays alive and off-screen, so that the next start finds it. When you close the panel in AR, the stream entry stays, because the window of the copy is never closed, and `"Stream panel to AR"` clears that entry before it starts the stream again. `"Disconnect (pause)"` leaves the copy streaming like every other panel.

If something hides the copy, for example a call of `hide` or `close` on it, the plugin shows it off-screen again at the next tick and logs this once, because a hidden widget would stream a blank rectangle. A failure to make the copy is logged with its reason, and the plugin carries on without it. [Hidden Displays panel lines](LOG_REFERENCE.md#hidden-displays-panel-lines) lists the lines.

## AR tools from the ROS graph

An AR tool is a panel that the plugin generates from a topic, a service or an action of the live ROS 2 graph and serves as a web page, which the browser of QarOS opens as a URL panel. The plugin needs no generated message code: it reads the layout of a type from the introspection typesupport of the interface package at run time, picks a template for the type, and binds the controls of the page to the fields of the type.

**The dialog.** `"Add from ROS graph..."` opens the modal dialog `"Add from the ROS graph"`. It lists the topics, services and actions of the graph as trees by namespace with their types, under the headings `Topics`, `Services` and `Actions`. The field `"Filter:"` narrows the list by a part of a name, of a type, or of the kind word, so that `"service"` shows only the services, and `"Refresh"` asks the graph again and keeps your selection. A double click on an entry does the same as `"Add"`. For a topic, `"Add"` creates the RViz display that accepts the type and pins the topic to the list of watched topics; `"Remove topic"` forgets a watched topic and leaves its display alone.

The check box `"Expose as a QAR AR tool"` generates the AR panel. For a service and for an action it is always on and cannot be changed, because RViz has no display for either. The check box `"Allow publishing from AR"` exists for topics only: it is off at the start and enabled only when the topic is exposed, because a publication to a topic is a command to a robot. The lines under the tree say what `"Add"` will do before you press it: which display it creates or that none is created, and whether the AR panel is read-only or has a publish form. An exposed topic of type `sensor_msgs/Image` or `sensor_msgs/CompressedImage` gets no RViz display at all, because exposing a topic as an AR tool must not add a display that you did not ask for.

**Where the page is served.** The first tool starts a web server in RViz, and the panel logs where it listens. The server listens on all network interfaces, so that a headset on the network can reach it, on a port that the operating system picks, so that two RViz instances on one machine do not collide. `QAR_PANEL_WEB_PORT` pins the port, for a firewall rule, and `QAR_PANEL_WEB_HOST` sets the host name that the addresses of the pages contain. A panel is reached at `http://<host>:<port>/p/<token>/`, and a wrong or missing token gets a 404. The address starts with `http://`, the traffic is not encrypted, and the token of 128 random bits is the only protection. [Network prerequisites](GETTING_STARTED.md#the-same-on-hololens-2) lists what the network has to allow.

**The list of tools.** Each tool is a row of the list: the kind, the name and the type, followed by `"[in AR]"` when its QarOS panel exists or `"[pending]"` when it does not yet, and by `"[writable]"` for a tool that may publish. The label under the list says how many tools exist and where the pages are served, and it carries the viewport that the last page reported. The tooltip of a row has the address of its page, the template that laid it out, and the last error. The buttons act on the selected row:

- `"Remove tool"` closes the AR panel, forgets the token first, so that a page that reconnects meets a 404, drops the subscription or the publisher, and removes the row.
- `"Re-open in AR"` creates the QarOS panel again on the next tick, after a restart of the session or when the panel was closed in the headset; the page and its token do not change.
- `"Copy URL"` puts the address of the page on the clipboard, to open it in a browser of the desktop while you debug.
- The spin box with the prefix `size x` is a multiplier from 0.25 to 4.00 on the size that the plugin derives from the page. It starts at the value of `QAR_PANEL_WEB_SCALE`, or at 1.00, and it applies with the next `"Re-open in AR"` and with the next report of the viewport. `QAR_PANEL_WEB_SIZE` pins the size in metres.

The size of a tool panel is derived from its page, because a generated form is portrait and a fixed landscape size would show only its top. The page measures the viewport that the browser of QarOS gave it and reports it, and the plugin resizes the QarOS panel when the size moved by more than 5 mm. The first tool is placed at a height of 1.5 m and 1.2 m in front of the origin, and each further tool 0.7 m further to the left. A page that is taller than its viewport is reached with the buttons of a pager and not with a scroll bar, because a scroll bar is a poor target for a hand ray.

**The templates.** A semantic template lays out the types that it claims, and a type that no template claims gets the generic form, which has one widget for each field. A semantic template scores 100 and the generic form 1, so that a template always wins for its types. A template whose probes do not recognise the interface definition of the installed distribution hands the type to the generic form. The plugin registers these 17 templates:

| Template | Claims | Layout |
|---|---|---|
| `pose` | `Pose`, `PoseStamped`, their covariance forms, `Point`, `Point32`, `PointStamped`, `Quaternion`, `QuaternionStamped`, `Transform`, `TransformStamped` | position, orientation as angles and as a quaternion, frame and stamp |
| `velocity-command` | `Twist`, `TwistStamped` and their covariance forms | forward, strafe and turn sliders and a one-press stop; charts when the panel is read-only |
| `battery` | `BatteryState` | charge gauge, decoded supply, health and chemistry, cell table |
| `diagnostics` | `DiagnosticArray`, `DiagnosticStatus` | a light for each component and the key and value table |
| `odometry` | `Odometry` | pose, body velocity and a forward-speed chart |
| `path` | `Path`, `Goals` | waypoints as a paged table |
| `navigation-goal` | the actions `nav2_msgs/action/NavigateToPose`, `nav2_msgs/action/NavigateThroughPoses`, `nav2_msgs/action/FollowPath`, `nav2_msgs/action/ComputePathToPose` and `nav2_msgs/action/FollowWaypoints` | goal pose in, distance and time left out, status and cancel |
| `tf` | `TFMessage` | parent and child with the translation, paged |
| `scalar-status` | the `std_msgs` booleans, strings and numbers | one value, big: a light for a boolean, a chart for a number |
| `joint-state` | `JointState` | name, position, velocity and effort for each joint |
| `robot-service` | `Trigger`, `SetBool`, `Empty` | one switch or one button and the success light with its message |
| `occupancy-grid` | `OccupancyGrid` | resolution, size and origin; the cells are never streamed |
| `laser-scan` | `LaserScan` | angular span, range limits and timing; the beams are never streamed |
| `image` | `Image`, `CompressedImage` | dimensions, encoding and stride; the pixels are never streamed; the button that opens the video |
| `point-cloud` | `PointCloud2` | shape, strides and the table of channels; the points are never streamed |
| `imu` | `Imu` | orientation, angular rate, acceleration and a yaw-rate chart |
| `navsat` | `NavSatFix` | the light of the fix, latitude and longitude to 7 decimals, altitude |

**Trying the templates without a robot.** The script `scripts\publish_template_topics.py` publishes a topic for each of ten templates of this table and serves a Trigger service for an eleventh, so that you can expose each of them as an AR tool and look at the page that the plugin generates, with no robot and no data of yours. One node, `qar_template_publishers`, publishes plausible values that change slowly, and each topic gets messages at the frequency that `--rate` sets in Hz, 1.0 by default:

| Topic or service | Type | Template |
|---|---|---|
| `/qar_demo/battery` | `sensor_msgs/msg/BatteryState` | `battery` |
| `/qar_demo/diagnostics` | `diagnostic_msgs/msg/DiagnosticArray` | `diagnostics` |
| `/qar_demo/odom` | `nav_msgs/msg/Odometry` | `odometry` |
| `/qar_demo/path` | `nav_msgs/msg/Path` | `path` |
| `/qar_demo/joint_states` | `sensor_msgs/msg/JointState` | `joint-state` |
| `/qar_demo/map` | `nav_msgs/msg/OccupancyGrid` | `occupancy-grid` |
| `/qar_demo/scan` | `sensor_msgs/msg/LaserScan` | `laser-scan` |
| `/qar_demo/imu` | `sensor_msgs/msg/Imu` | `imu` |
| `/qar_demo/temperature` | `std_msgs/msg/Float64` | `scalar-status` |
| `/qar_demo/pose` | `geometry_msgs/msg/PoseStamped` | `pose` |
| `/qar_demo/trigger` (service) | `std_srvs/srv/Trigger` | `robot-service` |

The service answers every call with success and the message `trigger <n>`, where `<n>` counts the calls, and the script prints a line for each call. The script ends by itself after `--seconds` seconds, 1500 by default, so that no process has to be stopped. It needs the environment of the ROS 2 distribution that `scripts\env_setup.ps1` imports, and it reaches RViz only when its shell has the same `ROS_DOMAIN_ID` as the session of RViz. The clean-up of the live launcher with `-Confirm` stops only the Python processes whose command line names `publish_test_markers.py`, `publish_test_tf.py`, `publish_test_robot.py` or `publish_test_pointcloud.py`, so it leaves this script running. To try it, prepare a window of its own with the two lines below: the first goes to the folder of the clone, and the second, with the leading dot and the space, imports the environment of the ROS 2 distribution.

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
. .\scripts\env_setup.ps1
```
```text
...
=== Qar RViz2 environment set up in this PowerShell session ===
...
python  -> C:\qar\venv\Scripts\python.exe
...
```

In that window run `python scripts\publish_template_topics.py` with `--seconds 600`, then open `"Add from ROS graph..."`, type `qar_demo` in the field `"Filter:"`, select a topic or the service, tick `"Expose as a QAR AR tool"` for a topic and press `"Add"`; the tooltip of the new row names the template that laid the page out.

A panel is limited to 48 fields for each section and a depth of 4 levels, and what does not fit is reachable in a widget that shows all fields of the section as raw JSON. An array that the plugin pushes to a page is cut to 256 elements, so that the large arrays of a message do not travel to the page.

**Who may write what.** The data document of a panel has the sections `msg`, `draft`, `request`, `response`, `goal`, `feedback`, `result` and `status`; only `draft`, `request` and `goal` are writable, and `draft` exists only when you allowed publishing. The plugin refuses a control event of a page when the widget id is unknown, when the frame names another action than the widget declares, when the action id is unknown or does not apply to the entity, when it writes into a read-only section, and when the value does not fit the schema, and it answers with the reason and logs a line that begins with `"AR CONTROL: refused"`. A refused edit changes nothing. The buttons `Publish` and `"Send goal"` ask for a second press within 4 s, and the label of the button says `"Press again to confirm"`; `"Call service"` and `"Cancel goal"` act on the first press, and cancelling is the safe direction. A button may carry a payload, as the stop button of the velocity template does, and then it sends that message in place of the staged one, after the same validation; the stop button asks for no second press and publishes its all-zero command on the first press.

**How the data moves.** The page of a topic is refreshed at 5 Hz, however fast the topic is, because a person reads a value more slowly: the subscription keeps only the newest message, and the plugin converts it on the tick. A call of a service and the goal of an action are asynchronous, and the plugin never waits for the answer; the page shows the outcome of the call and, for an action, its feedback, its status and its result. All of this runs on the GUI thread of RViz, on the node that RViz spins, so that no thread of its own exists.

## Video in AR

A topic of images reaches AR in two ways. The first is the button `"Open video in AR"` on the page of an exposed image topic, which opens a new AR panel, titled with the topic, that shows the video. The panel has the key `video:<topic>`, it appears in the drop-down of `"AR GUI panels"` as `Video: <topic>`, and `"Stop streaming"` closes it. The second way is a dock of RViz that holds an Image or Camera display: the render window of such a display cannot be captured, so the plugin subscribes to the topic of the display and streams the video into the AR panel directly. A Camera display is shown without the 3D overlay behind the image, and the panel says so in the log.

Both ways receive the topic in a private ROS 2 context, a DDS participant of its own, so that the Image display of RViz cannot starve the video. The QoS follows the publishers: the subscription is reliable when every publisher is reliable and best effort as soon as one publisher is not, with a history depth of 2. A topic that both ways receive uses one subscription and one decode. The video is decoded on a thread of its own into a slot that holds only the newest frame, so that a late frame is replaced and never queued. The panel runs at up to 30 frames per second.

The plugin decodes the encodings `rgb8`, `bgr8`, `8UC3`, `rgba8`, `bgra8`, `8UC4`, `mono8`, `8UC1`, `mono16`, `16UC1`, `32FC1`, `yuv422`, `uyvy`, `yuyv` and `yuv422_yuy2` of `sensor_msgs/msg/Image`. A `sensor_msgs/msg/CompressedImage` is decoded when its data is PNG or JPEG, which the first bytes of the data decide whatever the `format` string says; any other format is refused, whatever Qt plugins are installed. A frame larger than 1280 by 1280 pixels is scaled down to fit and never scaled up. The plugin reads the type that the topic advertises, `sensor_msgs/msg/CompressedImage` first and then `sensor_msgs/msg/Image`, and when no publisher exists it takes a topic whose name ends with `/compressed` for compressed images and any other topic for raw images.

Large video topics need the Fast-DDS profile `scripts\fastdds_large_data.xml`, which the launcher exports and which `scripts\publish_video_file.py` applies to itself. With the default segment of 512 KB of the shared-memory transport, 5 to 8 percent of the frames of a 6 MB stream lose fragments, and the repair of a lost fragment delays the same frame for every reader; the profile enlarges the segment to 64 MB. The launcher parameter `-TimingCsv` sets `QAR_VIDEO_TIMING_DIR` to a fresh folder, and the plugin then records the timing of every stage into CSV files there. `scripts\analyze_video_timing.py` reads those files and attributes a stall to the publisher, to the plugin or to QarOS; [Timing CSV files](LOG_REFERENCE.md#timing-csv-files) describes the files. A video that has stopped is no stall: the panel submits its last picture again once a second, and the analyzer leaves those idle gaps out and reports how many it ignored.

## Closing panels

What happens to an AR panel when its window goes away is the close mode, which `QAR_PANEL_CLOSE_MODE` selects when the first panel is streamed. `destroy` is the default: the AR panel is closed, its video sender is destroyed, and the stream is forgotten. `notice` keeps the AR panel as a notice that says that the window was closed, and keeps the video sender until the session ends. The mode `notice` exists as an escape for a QarOS runtime that does not survive the destruction of a sender in the middle of a session. The vendor states that destroying a video sender while a session runs is safe in the current binaries, and crash dumps from before that statement exist. Another value runs the default and logs one warning, and the line `AR PANEL: close mode` says which mode is in force and where it came from.

| Cause | What the plugin does |
|---|---|
| `"Stop streaming"` | closes the stream through the close mode; when the click was pressed in AR on the very panel that it stops, the teardown waits until the click has returned |
| the dock of a streamed window is closed in RViz | closes the stream; showing the dock again does not restart it, so press `"Stream panel to AR"` |
| the widget of a streamed window is destroyed | closes the stream at the next tick, before anything is captured |
| a streamed dialog is dismissed | retires its panel as soon as the dialog hides, closes or is destroyed |
| the operator closes the AR panel in QarOS | skips the close call of QarOS, stops the capture, marks the panel `"CLOSED IN AR"`, and counts it as `arclosed=` in the heartbeat |
| a closed `Video: <topic>` panel in `destroy` mode | also drops the ROS subscription and the row in the drop-down |

A panel that is started again after it was closed gets a number after its title, as [AR GUI panels](#ar-gui-panels) says. In the mode `notice` a start brings the notice panel back to life and creates no QarOS panel, and no Visualizer crash occurred in the 4 restarts that the authors of qar_rviz_plugin tried in that mode; the price is a notice panel in AR for each closed window, which you close in AR. Close and start panels at a calm pace: a quick burst of closes and starts once gave a black scene in trials of the authors.

In `destroy` mode a close writes a line that says that the close was requested. The library of Qt writes one line that begins with `closed`, with the frames served, the reason and the outcome of each step, and the adapter writes the three steps with a time of its own: the subscriptions are cancelled, the QarOS panel is closed or the close is skipped because the operator already closed it, and the video sender is destroyed. [AR panel lines](LOG_REFERENCE.md#ar-panel-lines) lists them. The heartbeat counts `closed=` for the panels that were closed and destroyed and `notice=` for those that show a notice, and a failure of the close call appears as a field named `"CLOSEFAIL"`.

## Spatial picking

The group `"Spatial AR interaction"` turns the pointer of the headset into ROS messages. Arming is explicit: the mode starts at `Off`, so that a gesture can never move a robot because a mode was left on. `Mode` chooses what a pointer means in the 3D scene, `Target` what it is resolved against, and `Frame` the frame in the header of every published pick, `map` at the start. The plugin subscribes to the click and hover gestures of the app volume of the session when it connects; a session without an app volume subscribes to nothing, and the panel logs that.

| Mode | Gesture | Topic | Type |
|---|---|---|---|
| `"Pick a point -> /clicked_point"` | a click | `/clicked_point` | `geometry_msgs/msg/PointStamped` |
| `"Set a navigation goal -> /goal_pose"` | press to fix the position, drag to set the heading, release | `/goal_pose` | `geometry_msgs/msg/PoseStamped` |
| `"Set the initial pose -> /initialpose"` | the same | `/initialpose` | `geometry_msgs/msg/PoseWithCovarianceStamped` |
| `"Select a rectangle -> polygon"` | press at the first corner, drag to the opposite corner, release | `/qar/selected_area` | `geometry_msgs/msg/PolygonStamped` |
| `"Select a circle -> polygon"` | press at the centre, drag to the rim, release | `/qar/selected_area` | `geometry_msgs/msg/PolygonStamped` |
| `"Select a polygon (tap vertices)"` | tap each vertex, then `"Close polygon"` | `/qar/selected_area` | `geometry_msgs/msg/PolygonStamped` |

The topics and the types are the ones that the tools Publish Point, 2D Goal Pose and 2D Pose Estimate of RViz publish, so subscribers of those tools work unchanged and the headset is one more producer on topics that exist. The message is built at run time from the type of its fields, so that no message header is compiled into the plugin. The covariance of an initial pose has the diagonal that the tool of RViz uses: 0.25 for x and y and the square of 15 degrees for the heading, which AMCL reads as the trust in the guess. A circle is published as a polygon of 32 segments. The header of every pick carries the frame of the field `Frame` and the time stamp zero, which tf resolves as the latest available transform and which suits a headset whose clock is not the clock of the robot; the panel never sets a time, so a subscriber that needs the time of a pick has to stamp the message itself.

**Confirmation.** A pick becomes pending when its gesture is complete, and the status says `"waiting for confirmation"`. `"Confirm pick"` publishes it and `"Cancel"` drops it. While a pick is pending, the controller ignores more input, so that what you confirm cannot change under your hand, and a change of the mode, of the target or of the area shape drops the pending pick. `"Close polygon"` finishes the region that the taps collected and needs at least three vertices. `"Publish without confirming"` removes the extra step: the pick is published as soon as you release. Confirmation is on at the start, so that a stray gesture cannot drive the robot. The buttons follow the state: `"Confirm pick"` is enabled only while a pick is pending, `"Cancel"` while a mode is armed, `"Close polygon"` only for the polygon shape, and the fields `Target` and `Frame` only while a mode is armed.

| Target | What it publishes | Needs a ray |
|---|---|---|
| `"Action Point (QarOS scene intersect)"`, the default | the action point that QarOS computed from its own slightly curved ray, verbatim, in the chosen frame; the SDK does not document which geometry that ray meets | no |
| `"Ground plane (z = 0) - straight-ray approximation"` | where the straight aiming ray crosses z = 0, so that the answer is always on the floor | yes; without a ray, the reported point is projected straight down onto the plane |
| `"Fixed depth, 2 m along the ray - local approximation"` | the point 2 m along the straight aiming ray | yes |

The action point leads the list because it is the only target that the runtime itself computed, so it is the only one that can be exactly what you saw. It needs the drop after the minimum drop or a newer one, because the minimum drop returned a near-field point, a point near the hand, and not the intersection with the scene: with the minimum drop, check a pick against the scene before you rely on it, or choose another target. [SDK compatibility](SDK_COMPATIBILITY.md#identifying-an-sdk-drop) shows how to tell the drops apart. The two other targets are approximations: QarOS aims along a slightly curved line and a straight line cannot reproduce the curve, and the difference was centimetres in every session measured so far. A plane target is a promise about where the answer is, and a navigation goal belongs on the floor. The aiming ray is the one that QarOS gives, from the start point of the gesture, which is your hand, through its action point; the plugin falls back to a straight line from the eye through the hand only when the runtime intersected nothing and the two points coincide.

**Refusals.** A gesture that carries no action point is refused and is never published as the origin of the map. When QarOS intersected nothing, it hands back the start point, which is your hand, as the action point; the plugin detects that case for each gesture, refuses to publish your hand, and counts it as `clicks-on-nothing=`. A hover previews and never commits a pick; only a tap commits. Degenerate geometry, such as a polygon of fewer than three points, is refused with a reason.

The second line of the status label shows the counters of the pointer. It begins with `"AR gestures: rx=%1 (click=%2 hover=%3 other=%4) queued=%5 dropped=%6"` and goes on with `" | picker: seen=%1 used=%2 picks=%3 ignored=%4"`. `"AR gestures: NOT SUBSCRIBED (%1)"` says that no subscription exists, and `"AR gestures: not subscribed yet (connect to QarOS first)"` says that you did not connect yet. The heartbeat carries the same counters on every beat, so that `rx=0` on every heartbeat says that QarOS sends nothing. The first 20 gestures and then one in 50 are also written to the log, with the raw point, the converted point and the pick that the armed target resolved, on lines that begin with `"SPATIAL: sample #%1"`, and a published pick writes `"SPATIAL: published %1 to %2"`.

**Coordinates.** QarOS reports the points of a gesture in the content space of the app, in the units of the scene, and not in metres; the pick therefore takes the change of basis from the room to ROS and nothing else. The change uses the same variables as the camera, `QAR_POSE_WORLD` and its companions, so that a pick lands where the headset looked. The plugin reads where QarOS displays the scene once for each tick and logs every move, because the grab and the pinch of the operator change it, but it does not apply it to a pick.

## Geolocation

The panel can tell QarOS where the origin of the streamed scene lies on Earth, from the GPS receiver of the robot. It subscribes to a `sensor_msgs/msg/NavSatFix` topic, which is `/gps/fix` unless `QAR_PANEL_GPS_TOPIC` names another, with the QoS profile of sensor data, because a reliable subscription would receive nothing from the usual best-effort driver of a receiver. The subscription is armed when the panel loads and does nothing while the topic is silent, and `QAR_PANEL_GEO` set to `off` switches the whole path off.

The callback of the subscription only stores the fix. The render tick applies it, on the GUI thread, so that every call into QarOS stays on one thread, and only while a session is connected. A fix with the status of no fix is ignored. The anchor is set on the app volume of the session from the latitude, the longitude and the altitude, as an ECEF origin with East, North and Up axes, and it moves again only when the position has changed by 2 m or more, because a receiver at 10 Hz would otherwise cost a call of QarOS for every message of jitter. The altitude of a `NavSatFix` is the height above the WGS 84 ellipsoid and not above mean sea level. Without an app volume there is nothing to anchor, and a failure to set the anchor is logged once and stops further attempts, while the streaming goes on without it.

You can try the path with a synthetic fix. The command publishes one fix a second for thirty seconds, and it needs a window that you prepared with the two lines of [AR tools from the ROS graph](#ar-tools-from-the-ros-graph); start it while the panel is connected:

```powershell
python "$env:ROS2_INSTALL_ROOT\Scripts\ros2-script.py" topic pub --rate 1 --times 30 --wait-matching-subscriptions 0 /gps/fix sensor_msgs/msg/NavSatFix "{latitude: 50.0755, longitude: 14.4378, altitude: 200.0}"
```
```text
...
publisher: beginning loop
publishing #1: sensor_msgs.msg.NavSatFix(header=std_msgs.msg.Header(stamp=builtin_interfaces.msg.Time(sec=0, nanosec=0), frame_id=''), status=sensor_msgs.msg.NavSatStatus(status=0, service=0), latitude=50.0755, longitude=14.4378, altitude=200.0, position_covariance=array([0., 0., 0., 0., 0., 0., 0., 0., 0.]), position_covariance_type=0)

publishing #2: sensor_msgs.msg.NavSatFix(header=std_msgs.msg.Header(stamp=builtin_interfaces.msg.Time(sec=0, nanosec=0), frame_id=''), status=sensor_msgs.msg.NavSatStatus(status=0, service=0), latitude=50.0755, longitude=14.4378, altitude=200.0, position_covariance=array([0., 0., 0., 0., 0., 0., 0., 0., 0.]), position_covariance_type=0)
...
publishing #29: sensor_msgs.msg.NavSatFix(header=std_msgs.msg.Header(stamp=builtin_interfaces.msg.Time(sec=0, nanosec=0), frame_id=''), status=sensor_msgs.msg.NavSatStatus(status=0, service=0), latitude=50.0755, longitude=14.4378, altitude=200.0, position_covariance=array([0., 0., 0., 0., 0., 0., 0., 0., 0.]), position_covariance_type=0)

publishing #30: sensor_msgs.msg.NavSatFix(header=std_msgs.msg.Header(stamp=builtin_interfaces.msg.Time(sec=0, nanosec=0), frame_id=''), status=sensor_msgs.msg.NavSatStatus(status=0, service=0), latitude=50.0755, longitude=14.4378, altitude=200.0, position_covariance=array([0., 0., 0., 0., 0., 0., 0., 0., 0.]), position_covariance_type=0)
```

A successful anchor writes a line that has this form:

```text
GEO: app volume anchored at lat=%1 lon=%2 alt=%3 m (ECEF WGS84, ENU axes).
```

```powershell
$log = Get-ChildItem -Path "$env:TEMP\qar_rviz_logs" -Filter 'rviz_live_*.err.log' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
Select-String -Path $log.FullName -Pattern 'GEO: ' | ForEach-Object { $_.Line }
```
```text
[qar-panel] 18:37:55.319 GEO: listening for NavSatFix on '/gps/fix' (QAR_PANEL_GPS_TOPIC; QAR_PANEL_GEO=off disables).
[qar-panel] 20:12:32.383 GEO: app volume anchored at lat=50.0755000 lon=14.4378000 alt=200.00 m (ECEF WGS84, ENU axes).
```

The geolocation is covered by unit tests, and the authors of qar_rviz_plugin have never checked it against a live GPS source; the geodetic arithmetic under it is tested in the repository `qar_ogre_adapter`.

## Floor plane and scene lighting

**The floor plane.** The Grid display of RViz draws thin line segments only, so the depth picture that the plugin streams would show gaps between the lines. The plugin therefore adds an opaque grey rectangle under every Grid display, so that the depth picture shows a solid floor. The plane lies 1 mm below the plane of the grid, it has the size of the grid, which is the number of cells times the size of a cell, and it follows the frame, the plane (XY, XZ or YZ) and the offset of the grid, and it is visible only while the grid is enabled. It is lit by its own colour only, a grey of 0.4, so that the lights of the scene do not shade it, and it carries a depth bias that pushes it away from the camera, so that the lines of the grid always win. It is excluded from the ray queries of the scene, so that tools such as Publish Point never pick it. The panel logs a line when it creates or changes a plane and one line when no Grid display exists. `QAR_PANEL_FLOOR_PLANE` set to `off` disables the feature.

**The lighting.** RViz lights its scene with one directional light, a headlight, that it re-aims at its own camera every frame, so that a peer that stands anywhere in the scene would see its shading swing around whenever the operator of RViz orbits the view. While the plugin streams, it adds a rig of lights that is fixed in the world, unless the mode is `off`, and it hides the headlight, unless you keep it, and it restores both when the streaming stops. `QAR_SCENE_LIGHT` selects the rig:

| Value | What it adds |
|---|---|
| `off` | nothing; the lighting of RViz stays as it is |
| `omni` | a raised ambient light and four directional lights on a tetrahedron, so that every surface faces at least one light, with the two brighter lights shining from above |
| `per-peer` | one point light at the head of each connected peer, moved every frame |
| `both` | the two together |

The live mode starts with `both` and the offline preview with `off`, so that the preview and the scripted captures keep the lighting of RViz. A value that is not a mode keeps the default, and the panel logs the mode and the state of the headlight when it loads. `QAR_SCENE_LIGHT_HEADLIGHT` set to `keep` or `on` leaves the headlight on, for comparison only. The brightness is tuned with `QAR_SCENE_LIGHT_AMBIENT`, 0.30 at the start, `QAR_SCENE_LIGHT_INTENSITY`, the brightness of the two lights from above, 0.75, `QAR_SCENE_LIGHT_FILL`, the share of that brightness for the two lights from below, 0.55, `QAR_SCENE_LIGHT_PEER`, the personal light, 0.35, and `QAR_SCENE_LIGHT_RANGE`, the attenuation range, where 0 means no falloff with distance. A fill of 1.0 gives uniform light without any shape, and a fill of 0 lights from above only.

## Offline preview mode

Without `QAR_PANEL_LIVE_STREAM` the panel runs without QarOS: the simulated peers that the panel itself provides replace the session, and nothing is sent to QarOS. The preview shows that the plugin loads, that the render path works on the real scene of RViz, and that several peers can be rendered in one batch. After `"Start Qar"` the state label says `"Qar runtime: RUNNING (offline preview)"`. `"Add simulated peer"` adds an entry named `"mock-peer-%02u"` to the list, with its size and its frame rate, and the mark `"STREAMING"` appears on an entry whose stream runs. `"Start stream"` and `"Stop stream"` act on the selected entry and `"Remove peer"` removes it.

The script is run from the folder of the clone, so go there first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

The script `scripts\run_rviz_with_plugin.ps1` starts RViz with the configuration `qar_streaming_dev.rviz`, which loads the panel, in this mode:

```powershell
.\scripts\run_rviz_with_plugin.ps1
```

The buttons `"Capture offscreen frame"` and `"Capture stereo pair"` of the group `"Diagnostics"` write pictures of the scene without QarOS. The first renders the scene of RViz into a colour target and a depth target and writes the files `_color.png`, `_depth.png`, `_depth.f32` and `_capture.txt`, with the prefix `qar_offscreen_` and the time in the current folder, or with the prefix in `QAR_PANEL_AUTOCAPTURE` when that is set. The second needs a simulated peer whose stream runs, and writes the colour and depth pictures of the left and the right eye and the file `_stereo.txt`, with the prefix `qar_stereo_` and the time. The four scripted modes of the script, which write a result file and quit RViz, are described in [Scripted checks in a window](TESTING.md#scripted-checks-in-a-window). The five scenes in `qar_rviz_streaming/config/test_scenes` are test content for this mode and for the live mode:

| Scene | Content | Publisher |
|---|---|---|
| `scene1_grid.rviz` | the grid | none |
| `scene2_grid_tf.rviz` | the grid and a tree of transforms | `scripts\publish_test_tf.py` |
| `scene3_grid_marker.rviz` | the grid and markers | `scripts\publish_test_markers.py` |
| `scene4_grid_robot.rviz` | the grid and a small robot model | `scripts\publish_test_robot.py` |
| `scene5_grid_pointcloud.rviz` | the grid and a point cloud | `scripts\publish_test_pointcloud.py` |

The publishers are synthetic and need no data of yours. The pages of the AR tools have a synthetic publisher of their own, `scripts\publish_template_topics.py`, which [AR tools from the ROS graph](#ar-tools-from-the-ros-graph) describes. `scripts\play_demo_rosbag.ps1` records the four publishers for 12 seconds into a bag in `%TEMP%\qar_demo_rosbag` when no bag exists there, and plays the bag in a loop. The script stops the recorder itself, and a recorder that is stopped that way writes no `metadata.yaml`, the index that every `ros2 bag` command needs. So the script writes the file before it plays the bag, with `ros2 bag reindex` and the storage `sqlite3` first and, when that leaves no file, with `scripts\rosbag_write_metadata.py`. A folder that holds a `.db3` file and no `metadata.yaml`, which an interrupted run leaves, is not recorded over: the script writes the missing file and plays that bag. rosbag2 records only into a folder that does not exist yet, so a `-BagDir` that exists and holds no bag makes the recording fail, and the warning of the script says so. When no bag comes of the recording, or the file cannot be written, the script runs the publishers directly, without a bag. RViz ends with the exit code 0xC0000409 as soon as the robot description of scene 4 reaches the Robot model display while the folder of `rviz2.exe` holds no `tinyxml.dll`, which the release of the distribution does not ship; the script `scripts\fix_missing_tinyxml_dll.ps1` of [Repairs to the ROS 2 Windows distribution](INSTALL.md#repairs-to-the-ros-2-windows-distribution) builds the file, and [RViz and plugin problems](TROUBLESHOOTING.md#rviz-and-plugin-problems) lists the symptom.

## Diagnostics view and status lines

The group `"Diagnostics"` shows what the panel knows and what it did. The label `"DisplayContext: (not initialized)"` is replaced by a summary of the display context that RViz gave the panel and of its Ogre scene: whether the context and the scene manager are valid, the number of cameras, the root children of the scene and the frame count. `"Refresh diagnostics"` updates that summary and the readout of the AR panels, and writes a line that begins with `"Diagnostics: streaming=%1"` and one line for each stream. The log view below keeps the last 2000 lines of the panel, each with the time of day, and the same lines go to the standard error of RViz with the prefix `[qar-panel]`, which is where the launcher keeps them in the file `rviz_live_<stamp>.err.log`. The label `"Last error: <none>"` turns red and shows the last error, which is also logged with the prefix `ERROR:`, and a later successful action clears it.

Four readouts show the state at a glance, and each has its own section: `"Qar runtime: STOPPED"` for the connection in [Connecting and pausing](#connecting-and-pausing), `"AR panels: none"` in [AR GUI panels](#ar-gui-panels), `"AR tools: none"` in [AR tools from the ROS graph](#ar-tools-from-the-ros-graph), and `"Spatial: off"` in [Spatial picking](#spatial-picking). Every 2 s the panel writes the heartbeat, which has the beat of the timer, the streams, the panels, the pointer and the displays of images in one line; `QAR_PANEL_HEARTBEAT_S` sets the period and 0 switches it off. The log view of the panel does not survive RViz, and the heartbeat is the line in the log file that tells you most about a run, so read it first when something stalls: `tick_fps=` far below the rate of the stream, or a `max_gap=` of hundreds of milliseconds, says that the GUI thread of RViz was blocked. [The heartbeat line](LOG_REFERENCE.md#the-heartbeat-line) explains every field, [Status lines](LOG_REFERENCE.md#status-lines) the readouts, and [Reading the logs](TROUBLESHOOTING.md#reading-the-logs) the first checks.
