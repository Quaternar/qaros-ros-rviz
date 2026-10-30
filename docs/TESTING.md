# Testing

## Running the tests

All tests of this repository are registered with CTest and run through the test preset `vs2019-x64`. The preset runs the configuration `RelWithDebInfo` and prints the output of every test that fails. Configure and build first, as described in [Build and install qar_rviz_plugin](INSTALL.md#build-and-install-qar_rviz_plugin); the tests are built with the rest.

The tests need no QarOS SDK and open no window, and you do not have to prepare the shell: a plain PowerShell 7 window is enough, because each test executable finds its libraries by itself.

- A Qt test executable gets the Qt DLLs copied next to it after it is built.
- The tests that use ROS 2 get the `bin` folder of the distribution put in front of `PATH` by a test property.

### Run all tests

Every command of this document runs in the folder of the clone; go there first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

```powershell
ctest --preset vs2019-x64
```
```text
Test project C:/qar/src/qar_rviz_plugin/build/vs2019-x64
        Start   1: check_layering_qar_ui_model
  1/889 Test   #1: check_layering_qar_ui_model ..................................................................   Passed    0.15 sec
        Start   2: check_layering_qar_web_panels
  2/889 Test   #2: check_layering_qar_web_panels ................................................................   Passed    0.08 sec
        Start   3: check_layering_qar_ros_ui
  3/889 Test   #3: check_layering_qar_ros_ui ....................................................................   Passed    0.12 sec
        Start   4: check_layering_qar_rviz_streaming
  4/889 Test   #4: check_layering_qar_rviz_streaming ............................................................   Passed    0.35 sec
...
100% tests passed, 0 tests failed out of 889

Total Test time (real) =  82.14 sec
```

The last lines are the summary: how many tests passed, and the names of those that failed. A test that is skipped is reported as skipped and does not count as a failure.

### List and select tests

Every GoogleTest test is registered as one CTest test named `Suite.Test`, and the guard tests of the repository have names that start with `check_layering`.
The first command lists the registered tests without running them; the second runs only the tests whose name matches a regular expression, here the self-test of the layering guard.

```powershell
ctest --preset vs2019-x64 -N
```
```text
Test project C:/qar/src/qar_rviz_plugin/build/vs2019-x64
  Test   #1: check_layering_qar_ui_model
  Test   #2: check_layering_qar_web_panels
  Test   #3: check_layering_qar_ros_ui
  Test   #4: check_layering_qar_rviz_streaming
  Test   #5: check_layering_examples
  Test   #6: check_layering_selftest
  Test   #7: UiValue.DefaultIsNull
...
  Test #634: RosImageFrameSourceTiming.EveryReceivedMessageGetsOneRowWithTheStagesInOrder
  Test #635: RosImageFrameSourceTiming.ADecodeFailureIsARowWithNoMailboxTime
  Test #636: RosImageFrameSourceTiming.WithTimingOffNothingIsWrittenAndFramesStillDecode

Total Tests: 636
```

```powershell
ctest --preset vs2019-x64 -R check_layering_selftest
```
```text
Test project C:/qar/src/qar_rviz_plugin/build/vs2019-x64
    Start 6: check_layering_selftest
1/1 Test #6: check_layering_selftest ..........   Passed    0.05 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.08 sec
```

Use `-R` with a part of a suite name to run one suite, and add `-j` followed by a number to run tests in parallel; tests that share a resource, such as the live ROS tests, never run at the same time, because they hold a common lock. When a test fails, its output is printed by the preset; add the option `--rerun-failed` to the command to run only the tests that failed in the last run.

## What the tests cover

The unit tests are eight GoogleTest executables, three for the web libraries and five for the plugin, and the layering guard adds its own tests. The list command above prints every registered test by name, with the number of tests at the end. `qar_rviz_streaming_tests` compiles the pure units of the plugin into one executable that needs neither RViz, nor a ROS 2 graph, nor QarOS: they use the standard library, `qar_ui_model` and QtCore only. `qar_rviz_streaming_widget_tests` builds the tabbed layout of the panel from the same pieces as the panel, with Qt Widgets and no RViz, under the offscreen platform of Qt, so that no window opens on the desktop, and it asserts on sizes that must not change and never on pixels or absolute metrics. The other three executables of the plugin start real ROS 2 participants and are, with `qar_ros_ui_tests`, the live ROS tests of [Live ROS tests and the re-run rule](#live-ros-tests-and-the-re-run-rule). The rules with the edge cases of the plugin, which are the generation of a panel from a ROS 2 type, the decision what a control event from a headset may do, the picking of points and the choice of display classes, live in the pure units on purpose: the layer of Qt and RViz above them is a thin shell, and these are the parts that a test can reach.

`qar_ui_model_tests` works on strings and value trees only; `qar_web_panels_tests` runs a real server and talks to it over real sockets on the loopback interface of the test process; and `qar_ros_ui_tests` loads the typesupport libraries of the distribution and, for its live suites, starts real ROS 2 participants.

| Executable | Suites | What the tests assert |
|---|---|---|
| `qar_rviz_streaming_tests` | `TopicTree`, `TopicFilter`, `NormalizeMessageType` | topics that share a namespace share its nodes in the tree, a name can be a topic and a namespace at once, the children are sorted so that a refresh does not reshuffle the view, listing a topic twice changes nothing, and a degenerate name is skipped instead of making an unnamed level; the filter matches the name or the type without regard to case and keeps the order; the `msg` infix of a ROS 2 type name is collapsed and the short form is left alone |
| | `ParseManifest`, `Catalog` | the manifest of the display classes of RViz is read for every class, also one that names no message type, a class can accept several message types, a manifest that wraps several libraries is handled, and malformed XML contributes nothing instead of a partial result; the catalog finds the display class for a message type in either spelling and lists every class that accepts a type, sorted; for `sensor_msgs/Image` the Image display beats Camera whatever the order of registration, DepthCloud is never picked for a colour video, and a compressed image maps to Camera |
| | `IsVideoDisplayClass`, `VideoDisplayClassCaveat`, `ShouldSkipDisplayForExposedTopic`, `IsImageMessageType`, `FormatVideoDisplayHealth` | Image and Camera are video displays and nothing else is, a partial or a differently written name does not match, and Camera carries a note about its overlay; a topic that is exposed as an AR tool gets no Image or Camera display whatever the catalog picked, a topic that is not exposed still gets its display, and a display of another kind is kept even when its topic is exposed; the health of a video display is one word, `WAITING` for a matched publisher with nothing received, `NOPUB` for a topic that RViz sees no publisher of, and `UNKNOWN` for a status row that cannot be read, so that it never raises a false alarm |
| | `RosGraphTreeTest` | the entries of the graph are split by kind, each kind forms its own namespace tree, an absent kind gives an empty tree, the filter matches name and type without regard to case, and typing the word of a kind narrows the list to that kind |
| | `ArPanelIdTest`, `ArDocumentSchemaTest`, `ArPanelGeneratorTest`, `ArPanelTemplateRegistryTest` | a ROS name becomes a valid widget id, the kind keeps entities of the same name apart, and hostile characters are replaced and the id is bounded; a read-only topic has only the section of the latest message, a topic that may be published adds the draft, a service has a request and a response, an action has goal, feedback, result and a status string, and only the staged sections are writable; a nested triplet of numbers becomes one vector widget, a primitive gets the input of its kind, a writable topic gets editable draft widgets and a publish button that confirms, a service gets a call button, and an action gets send, cancel and a status; a message with more fields than the limit is capped and what is left out stays reachable as raw JSON, too deep a nesting ends in an inspector, and a missing description, an unnamed entity and an empty message are refused; a specific template outscores the generic form and a template that produces an invalid model is caught |
| | `ArControlRouterTest` | a field edit is staged by its path and the staged values accumulate into the outbound message; a value of the wrong kind, an integer out of range, a string longer than its bound and an unknown field in a message are refused and stage nothing; writing what ROS reported, writing a response of a service, an unknown widget, an unknown action, an action that does not apply to the entity and a control without an action are refused; a frame cannot relabel the action of a widget; a call carries the staged request, a goal the staged goal and a cancel nothing, and the payload of a button replaces the staged message if the schema accepts it; opening a video is accepted for an image topic and for a compressed image topic only |
| | `ArExposureRegistryTest`, `ArRosEntityKindTest` | an entity can be exposed once, a topic and a service of the same name coexist, the order of insertion is kept, a duplicate panel id and an unnamed entity are refused, an entry is removed by its kind, and the registry finds the panel of a token and the entry of a panel id |
| | `ArSemanticCatalogTest`, `ArPoseTemplateTest`, `ArTwistTemplateTest`, `ArBatteryTemplateTest`, `ArDiagnosticsTemplateTest`, `ArNavigationTemplateTest`, `ArTfTemplateTest`, `ArScalarTemplateTest`, `ArJointStateTemplateTest`, `ArServiceTemplateTest`, `ArSensorTemplateTest`, `ArImageTemplateTest`, `ArSemanticFallbackTest`, `ArElideLargeArraysTest` | every message type belongs to exactly one template, the generic form stays last in the registry, and a type that no template claims gets the generic form; the pose template shows position, orientation, frame and stamp, treats a bare quaternion as an orientation, and shows a covariance as an inspector; a read-only velocity shows charts, and the command form is sliders and a one-press stop, with a range from the interface definition winning over the default; a battery shows its charge as a gauge with decoded enumerations; diagnostics get a light for each component; a path is a paged table; a boolean is a light, a number is a chart, and a writable boolean is a toggle; no panel of a sensor type binds its bulk payload; both image types offer the button that opens the video, and no service or action panel does; a claimed type whose schema no probe recognises falls back to the form; a long array that the host pushes is capped and the text says how much was dropped |
| | `SpatialPublishDefaults`, `SpatialPointPublish`, `SpatialPosePublish`, `SpatialNavGoalPublish`, `SpatialAreaPublish` | the topics are the conventional endpoints of RViz and the frame is `map`; a point becomes a stamped point or a bare point; an invalid pick, a custom target without a configuration, a type that cannot be built and a stamped message without a frame are refused with a reason; a pose becomes a stamped pose, the initial pose carries the covariance diagonal of RViz and the covariance is configurable; a navigation goal uses the frame of the goal; a rectangle becomes four points of a polygon, a circle is sampled into segments, and too few segments and an invalid area are refused |
| | `SpatialInteraction`, `SpatialTargetOptions` | the controller starts off and ignores samples, and a pick waits for confirmation by default; a point pick becomes pending and confirming hands the message over and clears it, confirming with nothing pending is refused, and cancelling drops the pick; a pending pick blocks further input and switching the mode abandons what was in flight; the automatic confirmation makes a pick ready at once but never publishes a hover, and a hover never commits a navigation goal; a pose is pressed, dragged and released, an area is swept or collected by taps and closed; the ground target publishes on the ground even for a hit in the scene, a ray from the hand makes it a true intersection, and the target of the action point publishes the runtime point verbatim, refuses a gesture without an action point and publishes nothing for a click on empty space, and says so; the action point is the first entry and the default, the two straight-ray targets are still offered and say that they approximate, and an unknown id falls back to the default |
| | `GridFloorPlaneGeometry`, `ParseGridPlaneKind`, `IsValidGridExtent` | the side of the floor is the cell count times the cell size, the floor is pushed down along the local minus Z axis for the planes XY and YZ and along plus Y for the plane XZ, an unknown plane means XY, and a zero, negative or non-finite extent is rejected; the drop below the grid is one millimetre, the push is the drop plus half the thickness of the plane, so that its near face lies exactly the drop below the grid, and the depth bias of the pass of the plane pushes it away from the camera, by 16 steps of the depth resolution and a small slope term |
| | `PanelDisplayNames` | the first start under a title keeps the title and each later start under it gets the next number after it, so that the fourth start of the hidden Displays panel is titled Displays (AR) (4); each title has a count of its own; a title that already ends in a number is another title with a count of its own; an empty title stays empty; the function that numbers a title returns it unchanged for no earlier start and for a negative count |
| | `ImageTopicTransportTest`, `WindowsScheduling`, `RttiGuardTypeName`, `RttiGuardCast` | an advertised compressed image wins, an advertised raw image wins, compressed beats raw when both are advertised, nothing advertised falls back to the suffix convention of the compressed topic, and an unrelated type is ignored; applying the scheduling opt-out twice and releasing it twice is safe, its summary is one line that matches what happened, and without a window the foreground state is unknown; the guard in front of the factory of RViz accepts the two spellings of the factory name and no other name, hands back the same object for the expected type, and rejects another factory over the same base, an unrelated object, a null pointer, a pointer into the middle of an object, and an object without the target base even when its name is accepted |
| | `PanelSettings` | the table of settings has unique keys in a fixed order; with nothing saved and nothing set, every setting has its default; a saved value wins over the default, and an acceptable variable wins over the saved value; an empty variable does not win, an invalid one is reported and ignored, and a variable may hold a value that the controls do not offer; an invalid saved value falls back to the default with a warning, an unknown key is reported, a newer version of the configuration still reads the keys that are known, and a configuration without a version reads as version one; saving never writes a value that only a variable supplied, writes the version key, and keeps the value of the operator while a variable wins; the keys that must not persist are not saved; the words of a switch match the words of the environment, the words of the pick targets survive a round trip, the pick frame is validated, and the scale of the web panels is kept in its range and rounded; a configuration of the first version loads all twelve keys, the default size of the dock is wanted below version two, and what is saved never asks for it again |
| | `PanelGroundRule` | the centre of the app volume is the ground when the pose of the app is the identity, a moved pose moves the ground, a scale does not change a level ground, and a turn about the vertical axis keeps the height; a small tilt makes the height vary with x and z, and a near-vertical plane, an unknown placement and numbers that are not finite give the fallback; a panel is raised by exactly the missing amount, a panel that is high enough is left as it is, a pose is never lowered, the orientation and the horizontal position are kept, the margin is a parameter, and the lower edge is never under the line; after a grab that moves a panel, the ground decides the raise; the rows of the table of the diagnosis are fixed |
| | `FixedPoses` | the mode of the fixed poses is the room unless the text says ground, and any other text gives the room with a warning that names the text; the room keeps the top edge at a fixed height whatever the scene does, and the ground hangs it that height above the RViz ground; the ground falls back where the rule finds no ground, and still raises a panel that reaches below the ground; neighbouring slots are further apart than a panel is wide |
| | `ArFrame` | with nothing set, or a blank text, the frame is the default without a warning; four numbers are the frame in the order top, bottom, left and right, and the fit margin stays the default; five numbers are the frame and the fit margin; a fit margin that is not acceptable refuses the whole frame; any other text is the default with a warning that names the text; the headroom is the top inset when the frame is not set, a refused headroom is the default with a warning, and the frame wins over the headroom even when it is refused |
| | `AppVolumeSize` | the edge of the app volume is the default when neither the size nor the scale is stated, follows the scale when only the scale is stated, is the stated size when a size is stated, and a number that is not finite and positive counts as not stated |
| | `HeartbeatBrightness`, `RenderDeviceLine` | a measured frame gives the mean luminance of all rows, of the top rows and of the rest, and the mean alpha, as whole numbers; values are rounded and a half goes up; a panel with nothing measured gives a dash, whichever of the three values is missing; the alpha part is left out while the alpha is not measured; every form matches, as a whole, the pattern that reads the line back, and the field ends the part of a panel after its other tails; the line of the render device names the render system and the device that it renders on, says so when there is no render system, whatever the device, and shows a device that is not named as unknown and only printable characters |
| | `ArControlPageModel`, `ArControlPanelRouter` | the model of the page of the AR control panel validates and renders as a page, its ids and its actions are those of the router, every slot carries a binding for its caption and one for its visibility, the initial document hides every slot, and the block of the pick comes first; the pick mode accepts its values and refuses any other, confirm and cancel are routed, the switch of a panel slot and the switch of a tool slot are routed and need a boolean, an unknown control and an action of another page are refused, and the commands that call QarOS are marked as deferred to the render tick |
| | `ArControlPanelState` | the first update publishes everything, and a resync replaces the whole document; an unchanged snapshot publishes nothing; slots stay stable while entries come and go, a freed slot stays empty for one update, and an overflow is counted; a panel switch follows the state of the AR panel and a tool switch follows the snapshot; without a live session the page lists nothing, while connecting and while paused it still lists the desktop, and back in a live session the slots return; the titles mark a panel that is starting and one that was closed in AR; the connection words are fixed, and the text of an error is cut at 200 bytes; the caption of the pick says what to do for each mode, shows a pick that waits for the confirmation with its summary, shows a refusal with its reason, puts a waiting pick before an error, and shows nothing of a pick while no mode is armed |
| | `ControlPanelPlacement`, `ControlPanelPlacementPolicy`, `ControlPanelFallback` | the panel faces the yaw of the head only, its front points at the viewer, a vertical gaze is refused, the head is derived from the eyes with the placement of the scene, and a flip turns the panel by half a turn; the key of an instance names the peer; in the mode for each peer an instance is created at the first pose of the peer and none without a pose, and later peers get their own; the shared mode uses the first pose, falls back after the wait and ignores later peers; each instance is created once; an instance is hidden when its stream goes and shown again when it returns; the top edge of the fallback is a parameter whose default is the fixed height, and a shared instance without a pose is created at the height that the caller said |
| | `ConnectionView` | every phase of the join says its state in plain words, the approval wait counts down, the states of another PC's Hub name the Hub, a running session counts its targets and the served ones, a paused session stays connected and says so, a session that is being set up is joined and not yet streaming; the remote fields show only for another PC's Hub, the code field is enabled only when the Hub waits for a code, cancel is offered while the join is under way, forget is for a pairing only, the identity line says who this program is, invitations are for the Hub of this PC and need a session, the words of the Hub choice round-trip, and a port is a whole number from 1 to 65535 |
| `qar_rviz_streaming_ros_tests` | `ArSemanticRosTest` | every type that a template claims generates a valid panel from the description that the installed distribution reports, and so does the command form of every claimed topic type; the probes of the templates match the real interface definitions and do not silently fall back to the form; the real battery, odometry, velocity command, transform message, image, trigger and switch services get what their templates promise, no panel of a real sensor type binds its bulk payload, a real type that no template claims still gets the generic form, and every generated panel renders as an AR page |
| `qar_rviz_streaming_video_source_tests` | `VideoSourceExecutor` | the executor creates its node in a ROS context of its own and joins the DDS domain of the host, every instance gets a node and a context of its own name, destroying it shuts down its own context and leaves the one of the host alone, it survives that context being shut down from outside and never hangs when it is created and destroyed at once, and it services a subscription that a publisher of another context feeds, on a thread of its own |
| `qar_rviz_streaming_image_source_tests` | `RosImageFrameSourceTiming` | every received message gets one row of the timing file with the stages in order, a decode failure is a row without a mailbox time, and with the timing off nothing is written and the frames still decode |
| `qar_rviz_streaming_widget_tests` | `HubConnectionWidget` | the group `"QAROS Hub"` shows in the offline preview with its settings editable and no pairing; host and port show only for another PC's Hub; the code field and Pair are enabled only in the state that needs a code, Enter submits the code at once and empties the field, a code without a usable Hub is refused and stays in its field, and the code is held in its field only; forget needs a second click within the confirmation time and the confirmation lapses on its own timer, and the mode This PC offers no forget; cancel reports itself, only an edit of a person reports a setting, the list of targets has one row for each target, no text can widen the group, showing and hiding the remote fields moves nothing, and the mode drop-down offers the three Hubs |
| | `StablePanelLayout` | the size hints of the stand-in panel stay constant across tab switches, text changes and hiding and showing; a scroll page never scrolls sideways; the status strip has a fixed height, and a shortened label keeps its full text as the tool tip; the tab widget takes the height that the strip leaves and adds nothing to the layout hints; the hints follow the pages once and honour the cap; a combo box or spin box with the wheel guard does not change on a wheel step unless it has the keyboard focus, and leaves the event to the page under it; the start size leaves room for a scroll bar, the hint width covers the tab bar and keeps the widest page, and the width of the tab bar does not follow the width of the panel; the default size of the dock fits the height of the hint, is kept to the room that is available, is never below the minimum and gives every page its room; the height of a dock in a shared column is set as asked and held even when the column has room to spare, a dock that is high enough is not held, the window grows by what the others cannot give, a maximized or full-screen window is left alone, the window grows only as far as the room goes, moves up when its lower edge would leave the room, and takes the room from its own screen by default; docks that are kept keep their height while another dock grows, and the window grows when nothing else can give way, but not when a kept dock stands in another column or is floating or hidden; holds on one dock end in any order of their releases, and releasing a hold on a dock that is gone is harmless; a floating dock is resized directly; the vertical overflow of a scroll area is the range of its bar, zero for a bar that is always off and for an area that is not on show, follows the range before the bar is shown, and counts rows for a view that scrolls by item; the missing height is the largest overflow of the visible areas inside; a fit adds eight pixels to what is missing and resizes six times at most; the fit of a tree to its view grows until the tree needs no bar, makes a short tree lower but not below the minimum, stops at the maximum, counts rows that were added just before, adds the slack to what the bar says is missing, and measures only the view that it is given; the description of a page says that it fits or scrolls, and the shortfall of the pages is the worst scroll of all pages, or zero when every page fits |
| | `SettledHeightFit`, `DockGrowthReason`, `StartFit` | the fit is done when two looks agree, and two looks at different heights do not agree; rows added on a queued call are found by the settled fit; an area that cannot be measured is never said to need no bar; two trees in a splitter are summed and not maximised; an area that scrolls by design never makes the widget taller; the fit gives up at the height limit, at the round limit and when the widget does not grow at all, and says so; a tree that needs less is made lower once but not below the minimum; a trim leaves the slack and one spare row, a spare row is added to every growth, and a widget that had to grow is never trimmed; a dock is looked at again after its hold is released; a need that the census cannot see is counted and named; a horizontal bar is no reason to grow and is not said to be no bar, though a horizontal bar next to a vertical overflow counts with it; a widget that cannot be made higher is given up only when two looks agree, and a stuck look that the next one does not repeat is no give-up; the log lines have the shape that the checks of the logs read; a window that limited the dock is named, a window that grew or did not have to is not blamed, a dock that lost its height after the hold was released says so, and the words take the place of the reason in the last line of the fit; the reasons not to fit the dock at start are given in order, and the line says what became of the dock |
| | `ReadoutBox` | the box is built so that nothing can move it or scroll it; four panels fit without a bar or one more line; panel lines that do not fit are dropped from the end and counted; a wrapped line counts for all the lines that it takes; the measure agrees with the label itself; every line that fits is kept and one more is not |
| `qar_ui_model_tests` | `UiValue`, `JsonWrite`, `JsonParse`, `JsonRoundTrip`, `Utf8`, `HtmlEscape` | an integer and a double stay different kinds, and an unsigned value above the largest `int64` becomes a double; an object keeps the order of its keys; a double survives a write and a parse bit for bit and a non-finite double becomes null; ill-formed UTF-8 is replaced and never forwarded; the writer's HTML-safe mode escapes whatever could end a script element; the parser reports line and column of an error, takes the last of duplicate keys and refuses nesting beyond its limit; the five significant characters are escaped and an attempt to break out of an attribute is neutralised |
| | `FieldPath`, `ResolveFieldPath`, `ValuePaths`, `DefaultValues`, `ValidateValue`, `MessageDescription` | the dotted path grammar and its resolution against a message description; `set_path` creates the missing levels and, when a kind clashes, refuses and leaves the document untouched; a default follows the default of the interface definition; a value is checked against its description and every kind of mismatch is reported, including the extremes of `int64` and `uint64` |
| | `Widget`, `WidgetIds`, `UiModel`, `Validate`, `Serialization` | the rules for widget ids; every structural, binding and type-specific rule of validation, with and without a schema; a model survives a round trip through its value tree and its JSON, only members that differ from the default are written, and malformed input is refused |
| | `UiModelBindings` | a widget can name a path that gives its caption, or the text of a button, and a path that shows or hides it; both keys survive the value tree and the JSON of a model, the validation refuses a path that is not a dotted path with optional indexes, a model without the two fields renders exactly as before, the renderer marks a caption that a binding can replace and starts a widget with a visibility binding hidden, and the script that reads them is plain ES5 and pure ASCII |
| | `Protocol`, `ApplyPatch`, `Viewport` | the exact bytes of every frame of `qar-ui/1`; every frame decodes into the type that its sender built; an unknown frame type decodes and is ignored, while a foreign version, a missing member or a bad body is an error; a patch is applied all or nothing; the viewport frame omits what the page could not measure and is ignored by a peer that does not know it |
| | `ArTheme`, `ArThemeCss`, `ThemeCss`, `PageMetrics`, `ScrollPaging`, `ViewportFit` | the default theme meets the readability floor, a scaled theme scales sizes only, and a theme with CSS injection is refused; the style sheet has no hover rule, defines every variable that it uses and holds no pixel literal outside a short list; the page measure follows the model and the theme; the click pager splits an overflowing page into overlapping pages; the fit of a page to the viewport is deterministic, drops columns before it shrinks text, stops at a legibility floor and ignores resize noise |
| | `HtmlRenderer` | the page is self-contained and byte for byte the same for the same input; every widget appears once with its attributes in order; untrusted strings are escaped and the initial data cannot end the script element; an invalid model or theme is refused; the script is plain ASCII with no `innerHTML`, `eval` or arrow function, probes the browser features that it needs and carries the diagnostic that it hides when it runs; the script clears an error line of the status after six seconds, a newer line cancels the pending clear, and only an error arms a new one |
| `qar_web_panels_tests` | `HttpParse`, `HttpMessage`, `PanelToken`, `HttpServerTest` | the parser of requests and the cases that give 400, 414, 431 and 505; the framing of responses; a token is random, well-formed and compared in constant time; the server answers `HEAD`, 404 and keep-alive, pipelined requests in order, an oversized head with 431, a slow request with a timeout and a connection above the cap with 503, and hands an upgrade to its handler with the request unread |
| | `WebPanelServerTest`, `WebPanelServerHosts`, `WebPanelServerBind` | the server starts on a port that the operating system picks and builds addresses with the token; the page is served only with the right token; the hello of a viewer carries the whole document; a publish reaches every viewer in order, each connection with its own counter; a control event reaches the sink and the signal and can be acknowledged; a foreign version, a wrong panel or a first frame that is not a hello is refused; a socket without a valid token never upgrades and a handshake of another origin is refused; a removed panel closes its viewers and forgets its token; the cap on viewers holds before the upgrade; a call from another thread is refused; a model that does not render is refused; the page reports the viewport it was given |
| | `UrlPanelSizing`, `ReportedViewport`, `ViewportFitSizing`, `UrlPanelSizingLoop`, `UrlPanelResizeGuard` | text on a URL panel has the same angular size as text on a Qt panel; a scaled theme keeps the physical size and gains pixels; the aspect of the panel is the aspect of the page, or of the viewport that the sizing holds; the content scale is 1; the bounds scale both axes together so that nothing is squeezed; a pinned size and a malformed override behave as documented; a theme shrunk to fit is paid for by a larger panel; a viewport that follows the panel cannot shrink a panel that is sized from the design viewport, while the size from the reported viewport runs away on such a runtime; the guard allows three resizes in ten seconds and refuses the fourth, the budget comes back when the old resizes leave the window, and a width below the floor is refused without being counted |
| `qar_ros_ui_tests` | `TypeSupportRegistry`, `Introspection` | the naming rules match the generated symbols; both handles of a message load and are cached; an unknown package or type is reported and a malformed name is refused before anything loads; a service loads with its two halves and an action with its eight messages; every typesupport type id maps to a kind of the schema; the description of `PoseStamped` matches the message field for field, and defaults are read from the generated constructor |
| | `DynamicMessage`, `QosSelection` | a value tree converts to a message of a type without a header and back: defaults, partial objects, unknown fields, kind and range errors with their paths, integral doubles for integers, `NaN` and null, fixed and bounded arrays, string bounds in bytes, wide strings, and the limits that truncate long arrays and stop deep trees; the serialised bytes equal those of the compile-time type; the QoS rules follow the publishers that exist |
| | `GraphDiscovery`, `TopicBridgeLive`, `ServiceBridgeLive`, `ActionBridge`, `ActionBridgeLive`, `GraphDiscoveryLive` | hidden names follow the ROS convention and an action is recovered from its services and topics; the snapshot sees topics, services and actions with their types and detects a change; an adaptive subscription hears a best-effort and a reliable publisher, and a latched message arrives through transient local; `reconcile` resubscribes when a publisher appears; a push callback runs on the thread of the executor; a service and an action report a timeout, a rejected goal and a failure as results and never as exceptions, and a deferred request waits for a late server |



### The analyzer of the timing files

The tool that reads the timing files of the plugin, `scripts\analyze_video_timing.py`, has a self-test of its own that CTest does not register. It writes four fabricated recordings, each with one known cause, which are a stall of the publisher, a stall of the GUI thread, back-pressure from QarOS and a clean run, and it checks that the analyzer names the cause, finds the stage that went quiet first, and recovers the period of the stalls and the fabricated latencies. It then writes recordings of a panel that is open before and after the video, which polls while no frame comes and submits its last picture again every second, and it checks that those submits are no episodes and that the real stalls beside them are still found: a video that ends in 20 seconds of such submits; the three stalls again, each with such an ending, and the stall of QarOS once more without the rx file; a last frame that is submitted late; and a video that starts after about 5 seconds of such submits, with its first frame submitted at once and with its first frame submitted late, each with and without the rx file. It also checks that a keep-alive submit that QarOS refuses for several seconds is an episode, and that it is not counted into the seconds in which frames were waiting. The rate over those seconds must be that of the video, and every real stall must keep its verdict. [Timing CSV files](LOG_REFERENCE.md#timing-csv-files) has the formats of the files that the analyzer reads. The command needs only the Python standard library, so the Python of your toolchain runs it; in a shell that you prepared with `scripts\env_setup.ps1` that is the virtual environment. It prints one line and exits with 0, or it lists the failures and exits with 1.

```powershell
python scripts\analyze_video_timing.py --self-test
```

```text
SELF-TEST PASSED: 4 fabricated recordings (source stall, GUI-thread stall, QarOS back-pressure, clean) attributed correctly; latency tables and the 15 s period recovered; idle keep-alive submits at the end and at the start of the video ignored, the stalls beside them still found.
```

### The colcon build is a gate

The file `qar_streaming_panel.cpp`, which holds the panel, and the other sources that need RViz are compiled by the `colcon` build of the plugin and by no other build of this repository, so that build is the test of that code: a change to the panel needs `scripts\build_plugin.ps1`, described in [Build and install qar_rviz_plugin](INSTALL.md#build-and-install-qar_rviz_plugin), as well as `ctest`. A mistake in the panel can pass every test of this document and break only the build of the plugin.

## Test doubles

No test of this repository calls the QarOS C API, and none of the three web libraries calls it either, so there is no stand-in for the QarOS SDK. The tests use real objects wherever they can, and these stand in for the rest; the last three stand in for the other end of a connection of the web libraries.

| Test helper | What it stands in for |
|---|---|
| `FakeRecordingFactory`, `FakePanelFactory` and the other classes of `rtti_guard_test_types.hpp` | the factories of RViz that the guard in front of the hidden Displays panel meets: a root that is not a template, a class template derived from it, and concrete factories that the test code cannot name and receives only as an opaque pointer, the way `PanelFactory` is defined only in the library of RViz |
| the message descriptions of `ar_semantic_templates_tests.cpp` | the description of a ROS 2 interface that `qar_ros_ui` reads from the introspection library of the distribution; they are written by hand to match the real interface definitions field by field, and `qar_rviz_streaming_ros_tests` checks every template again against the descriptions of the installed distribution |
| the publisher of `ros_image_frame_source_timing_tests.cpp` | the camera node of a robot: a publisher of the host context of the test, while the source under test receives in a context of its own |
| the stand-in panel of `stable_panel_layout_tests.cpp` | the panel `QarStreamingPanel`, which needs RViz: a status strip over a tab widget whose pages are scroll pages, built from the same pieces as the panel, with size hints that are measured once |
| `RawHttpClient` | a client that speaks just enough HTTP to read one reply: it stands in for the browser that fetches a page, and it is driven by the event loop of the server under test, so that no wait starves the server |
| `WsClient` | the script of the page: a `QWebSocket` with recorded signals that says hello, sends control events and viewport reports and records every frame it receives |
| `TestNode` | the rest of the ROS 2 graph: an in-process node with an executor and bounded helpers that spin until a condition holds or a timeout passes; the typed publishers, subscriptions and service servers that the tests create beside the bridge under test play the other nodes |



## What the automated tests do not cover

- **The panel in RViz.** No test loads the plugin into RViz or constructs the panel. The panel, the dialog of the topic browser, the adapters of the display context and of the render lock, the access to the panel factory of RViz, the floor plane in the Ogre scene and the reader of the video are sources that only `colcon` builds, so only a live session shows what they do in a running RViz. The tabbed layout, the fit of the docks and the readout box are tested on a stand-in panel that is built from the same pieces, with Qt Widgets and no RViz.
- **QarOS.** The connection, the invitation of a peer, the hand-over of the pictures, the creation of panels and the gestures of the runtime are not called by any test. Whether a Visualizer shows the stream and whether a pointer reaches a panel is not tested here. [What the live sessions showed](VERIFICATION.md#what-the-live-sessions-showed) says what the logs of the live sessions show and what the authors of qar_rviz_plugin confirmed. The URL panel itself is created by the adapter of `qar_ogre_adapter`, which no test of this repository uses; whether a Visualizer shows the page and whether its pointing reaches the page is not tested here.
- **The accuracy of a pick.** The tests construct the gestures of the runtime and check the arithmetic and the rules of a pick. How near to the aimed point a pick lands in a live session has no measured figure.
- **A real network.** The live ROS tests use a domain of their own, entities in the test process and the UDP transport only. Another machine, another domain and the middleware settings of a real robot are not exercised. The server tests connect over the loopback interface. A firewall, a second network interface, a headset on Wi-Fi and the choice of the advertised host are not exercised.
- **Message packages that are missing.** The live tests of the video source need `std_msgs`, and those of the image source need `sensor_msgs` and the library `qar_qt_streaming`; without them the tests are compiled out or skipped, and the rest still runs. The live tests need `test_msgs`, `geometry_msgs` and `std_srvs`; without all three they are compiled out, and the rest of the tests still run.
- **The script of the page.** No test runs the script in a JavaScript engine: the executables link none, and the tests search the script and the page as text for what must and must not be in them. What a page does in the browser of QarOS, how it is laid out there and how its viewport behaves is shown by a live session only.

### Scripted checks in a window

The script `scripts\run_rviz_with_plugin.ps1` starts RViz with the plugin without QarOS and can run one of four scripted modes. Each mode makes the panel, a few seconds after it has loaded, write a result file and quit RViz, and the script reads the file and prints a line that ends in the word PASS or FAIL and exits with 0 for a pass. The modes open a real RViz window and need the local console session, so a person starts them, and they are no part of `ctest`. [Offline preview mode](OPERATOR_GUIDE.md#offline-preview-mode) describes what the preview contains. A plain file name goes into the folder `qar_rviz_preview` of `TEMP`, and you can pass a path instead.

The first mode loads the panel, checks that RViz gave it a valid display context and a scene, and runs a round trip with a simulated peer: the peer is added, its stream is started and stopped, and the peer is removed. It passes when every step succeeded and no stream is left.

```powershell
.\scripts\run_rviz_with_plugin.ps1 -AutoDump autodump.txt
```

The second mode renders the live scene of RViz into the offscreen colour and depth targets and writes a colour picture, a depth picture, the raw depth values and a summary. It passes when the colour picture holds a pixel that is not background and the depth values are not all zero and not all equal.

```powershell
.\scripts\run_rviz_with_plugin.ps1 -AutoCapture capture
```

The third mode adds a simulated peer, renders a short sequence for it and captures the left and the right picture of two poses; the parameter `-IpdMetres` sets the distance of the two eyes and `-StereoFrames` the length of the sequence. It passes when the pictures of both poses pass the checks of the second mode and the two poses are more than half a metre apart. The fourth mode starts several simulated peers with views of their own, renders paced batches for all of them, and writes the achieved rate of each peer and the timing of the batches; `-MultiPeers` and `-MultiTicks` set the number of peers and of batches.

```powershell
.\scripts\run_rviz_with_plugin.ps1 -AutoStereo stereo
```

```powershell
.\scripts\run_rviz_with_plugin.ps1 -AutoMulti multi
```

## The layering guard

The layering guard is a small Python program, `scripts\check_layering.py`, that reads the `#include` lines of the C++ sources and enforces the dependency table of the QarOS integration code: which module may include which. It exists because the structure of this stack (a core that calls no QarOS function, an adapter, and the GUI and ROS 2 layers above them) decays quietly when one convenient include crosses a layer. It needs only the Python standard library.

CTest registers one guard test per module that this repository holds, named `check_layering_<module>`, and the test `check_layering_selftest`, which proves that the guard still reports a violation of every rule. The guard checks every module of its table whose folder exists, because the table describes the whole family of repositories and a repository holds only some of the modules.

| Rule | What the guard enforces |
|---|---|
| L1, include classes | A module includes only the foreign header families listed for it: the standard library, Qt, Ogre, ROS 2, RViz, platform headers, GoogleTest, stb. |
| L2, project edges | A module includes only the project modules listed for it, and some edges only through named headers: no upward and no sideways edge. Every example folder has its own entry. |
| L3, vendored header | Only the wrapper header of the core includes the vendored QarOS C header. |
| L4, core calls no API | The core calls no QarOS C function; it uses the types of the C API only, so it never needs the SDK DLL. |
| L5, adapter components | Every file of the adapter belongs to one component and includes only headers of its own component and of the components it may use. |
| L6, loader | The dynamic-loading implementation macro of the C header appears exactly once, in the loader file of the adapter, and nothing defines the dynamic-loading switch except CMake. |

Comments and string literals are ignored; every `#include` line is classified.

### Run the guard by hand

The first command checks all modules of this repository and prints one summary line per module; the second runs the self-test. Both exit with 0 when everything is clean, with 1 when a rule is violated and with 2 when the arguments are wrong or no module folder exists.

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --all
```
```text
check_layering examples: OK (2 files)
check_layering qar_ui_model: OK (37 files)
check_layering qar_web_panels: OK (18 files)
check_layering qar_ros_ui: OK (35 files)
check_layering qar_rviz_streaming: OK (97 files)
```

```powershell
& "$env:QAR_VENV\Scripts\python.exe" scripts\check_layering.py --self-test
```
```text
check_layering self-test: OK
```

### Read a failure

For a clean module the guard prints the module name, the word OK and the number of files it read. For a module with violations it prints the number of violations and the number of files, and then one indented line per violation in this form: the file and line of the include, the rule that it breaks and what is not allowed there.

```text
path:line: [rule] detail
```

Fix the include rather than the table: move the code to the layer that may use that header, or pass the dependency in through an interface. Change the table in the script only when the architecture itself changes, and then change the documentation of the layers with it.

### The guard in this repository

The module `qar_rviz_streaming` may include the standard library, Qt, Ogre, ROS 2, RViz, `stb` and the platform headers `windows.h`, `mmsystem.h`, `process.h` and `unistd.h`, and it may include the headers of every project module of the stack; its tests may in addition include GoogleTest. It is the one module of the stack that may use the headers of RViz, because it is the one that is loaded into RViz: the libraries below it must not know RViz, and the guard is what keeps them so.

The guard checks the modules `qar_ui_model`, `qar_web_panels`, `qar_ros_ui` and `qar_rviz_streaming`, and `examples` when that folder exists. CTest registers one test for each, `check_layering_qar_ui_model`, `check_layering_qar_web_panels`, `check_layering_qar_ros_ui`, `check_layering_qar_rviz_streaming` and `check_layering_examples`, and the test `check_layering_selftest`. What the rules mean here:

- `qar_ui_model` includes the standard library and its own headers, and nothing else: no Qt, no ROS 2, no Ogre.
- `qar_web_panels` includes the standard library, Qt, its own headers and the headers of `qar_ui_model`, and from `qar_ar_core` only the wrapper header of the QarOS C header, which it needs for the type of a panel size. It includes no ROS 2 header.
- `qar_ros_ui` includes the standard library, ROS 2 headers, its own headers and those of `qar_ui_model`, and no Qt header.
- Every folder under `examples` has its own entry. The folder `web_panel_example` may include the standard library, Qt, `qar_ui_model`, `qar_web_panels`, `qar_ar_core` and the headers of the components `render` and `url` of `qar_ogre_adapter`; the folder `ros_topic_panel_example` may in addition include ROS 2 headers and `qar_ros_ui`.

## Adding a test

[Developing](DEVELOPING.md#adding-code-and-tests) explains where a new test goes, how to register it and how to name it.

## Live ROS tests and the re-run rule

Most tests of this repository need no ROS graph. A few tests, the live ROS tests, create real ROS 2 participants in the test process: a node, a typed publisher or subscription, and the bridge under test, with bounded waits and no daemon. They are in these executables:

- `qar_ros_ui_tests`, `qar_rviz_streaming_ros_tests`, `qar_rviz_streaming_video_source_tests` and `qar_rviz_streaming_image_source_tests`

Four properties make them safe to run, and you should know them when one fails.

1. Each test process joins its own ROS domain, with a number derived from its process id, so two processes do not meet by accident.
2. Each test runs with the environment variable `FASTDDS_BUILTIN_TRANSPORTS` set to UDPv4. The tests exercise the bridges, not the transport, and the shared-memory transport of Fast-DDS is switched off for these short-lived processes, because participant teardown with shared memory crashes intermittently and leaves stale segments behind.
3. All live ROS tests hold the same CTest resource lock, `qar_ros_dds`, so no two of them run at the same time, not even when you run CTest in parallel; they share the transports of the host.
4. A live test that needs a ROS 2 message package is compiled only when the distribution has that package; without it the test is compiled out and the other tests still run.

### The re-run rule

Even so, a live ROS test can fail once and pass at the next run, because the teardown of a ROS 2 participant is not always clean on Windows. The rule is: when the only failures of a run are in the live ROS executables listed above, run the failed tests again, up to three times, with the command below. A test that passes in one of the runs counts as passed. A failure in any other executable is real, and so is a live test that fails in every one of the three runs.

```powershell
ctest --preset vs2019-x64 --rerun-failed --repeat until-pass:3
```
```text
Test project C:/qar/src/qar_rviz_plugin/build/vs2019-x64
Problem reading file: C:/qar/src/qar_rviz_plugin/build/vs2019-x64/Testing/Temporary/ while generating list of previously failed tests.
        Start   1: check_layering_qar_ui_model
  1/636 Test   #1: check_layering_qar_ui_model ..................................................................   Passed    0.12 sec
        Start   2: check_layering_qar_web_panels
...
100% tests passed, 0 tests failed out of 636

Total Test time (real) =  52.22 sec
```

The command re-runs the tests that failed in the last run. When the last run had no failed test, CTest finds no list of failed tests, says so in the second line of its output, and runs every test again; nothing is lost, the run is only longer.

If a live test still fails, [Run-time problems](TROUBLESHOOTING.md#run-time-problems) in the troubleshooting document explains how to clean stale Fast-DDS shared-memory segments and how to find a program that holds a network port that the tests need.
