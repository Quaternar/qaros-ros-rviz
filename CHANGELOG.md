# Changelog

All notable changes to this repository are listed in this file, newest version first. The format follows Keep a Changelog, and the versions follow Semantic Versioning. While the major version is 0, the minor version marks changes that may not be backward compatible, and the CMake packages of this stack accept a request only for the same minor version.

Each version lists what it adds, the compatibility it was verified with (the QAROS version, the ROS 2 distribution, Qt, the toolchain and the versions of the repositories it builds on), and its known limitations. The version of a repository is the version of its CMake package, 0.1.0 for this release.

## 0.1.0

### Added

The first version of the repository. It adds the features that the [README](README.md#features) describes:

- the panel `"Qar Streaming"` of RViz2: the join to the QAROS Hub through a session owner on a thread of its own, with the Hub of this PC approved once in QAROS or the Hub of another PC paired with a code, a stream for every target that the Hub lists, the invitation of a HoloLens or a headset by the Hub of this PC, a status strip above six tabs, and fourteen settings that are saved with the configuration of RViz2, where a variable of the environment wins over the saved value;
- the live scene for every peer: stereo rendering from the head pose that QarOS reports, one batch under one render lock, a budget of about 16 ms, a floor of 30 frames per second and a ceiling of 8 peers;
- the opaque floor under the Grid display, the lighting rig that is fixed in the world, and the geographic anchor from a `NavSatFix` topic;
- AR GUI panels of the docks of RViz2, a dialog as a panel of its own, the hidden Displays panel, hiding and parking of the AR panel of a closed window instead of closing it, which is the default, the close modes `destroy` and `notice` for closing, and a number after the title of a QarOS panel that is started again under a title that an earlier panel of the process used;
- the AR control panel: a page that hangs in front of each user, switches the AR panels and the AR tools, arms and confirms a pick, and shows the state of the connection;
- the placement of the AR panels: a ground rule that keeps the lower edge of a panel 0.10 m above the RViz ground, fixed poses that are counted from the room or from the ground, the sizing of a panel for the frame that QarOS draws around the video, and an app volume of 10 m whose edge follows its scale;
- the fit of a dock to its content before it is streamed, so that its AR copy shows no scroll bar, and the fit of a dialog to its scroll areas;
- log lines for the diagnosis of a panel that is black or frozen: the brightness of the frames of each panel, the cause of a panel that has no destination to write into, and the state of every panel;
- the launcher parameters `-QarLogSeverity`, which defaults to `info`, `-TestMarkers`, `-HubMode`, `-HubHost`, `-HubPort` and `-RuntimeDir`, the copies of the logs of the runtime launcher and of the crash reports of a run, the defaults of `QAR_PANEL_FIXED_POSES` and `QAR_PANEL_VOLUME_Y`, and a record of the start checks of each run in a file of its own;
- AR tools from the ROS 2 graph: 17 semantic templates and a generic form, the web server of the pages, the rules for writing and the confirmation of the buttons that command a robot, and a publisher of ten topics and a service for trying the templates without a robot;
- video in AR by the button of an image tool and by an Image or Camera dock, the Fast-DDS profile for large frames, the timing CSV files and their analyzer;
- spatial picking of a point, a navigation goal, an initial pose and an area on the topics of the tools of RViz2, with a confirmation and the refusal of a click that hits nothing;
- the launchers `run_live_rviz_stream_real.ps1` and `run_rviz_with_plugin.ps1`, the offline preview with its scripted modes, and the five test scenes with their publishers;
- the demo bag of the test publishers: `play_demo_rosbag.ps1` records it when none exists, writes the index that the stopped recorder does not write, and plays it in a loop, and it writes the missing index of a bag folder that holds a database and no index; the publisher scripts print one message, and no traceback, when the Python that runs them has no ROS 2 packages;
- the repair `fix_old_vc_runtime.ps1` of the C++ runtime that the ROS 2 distribution ships next to `rviz2.exe`, which is too old for the QarOS library, and the check of that runtime in the live launcher, which stops before it starts RViz and prints the command of the repair;
- the repair `fix_missing_tinyxml_dll.ps1` of the `tinyxml.dll` that the URDF libraries of the ROS 2 distribution import and the release does not ship, because RViz ends with the exit code 0xC0000409 when a robot description reaches the Robot model display without it, and the check of that file in the live launcher, which stops before it starts RViz and prints the command of the repair;
- the pure units of the plugin with five test executables, and the layering guard;
- the UI model of `qar_ui_model`: twenty kinds of widget, bindings into one data document, also for the caption and the visibility of a widget, action ids and a validation that refuses a broken model before it reaches a page;
- the page renderer: one self-contained page from a model, with the AR theme, a script in ES5 syntax, a Content-Security-Policy and escaping, and the `qar-ui/1` wire protocol between the page and its server;
- the panel server of `qar_web_panels`: one HTTP listener for the page and its WebSocket, a token for each panel, the origin check and the limits on connections, viewers and messages;
- the sizing of URL panels from the measured page and a fixed design viewport, the resize guard, the fit of a page to its viewport and the click pager;
- the ROS 2 library `qar_ros_ui`: graph discovery, introspection of message types at run time, bridges for topics, services and actions, and QoS profiles that follow the publishers;
- the examples `qar_web_panel_example` and `qar_ros_topic_panel_example`, and the unit tests of the three libraries.

### Compatibility

| Item | Version |
|---|---|
| QAROS | verified against the installed 0.9.0-dev.3008+65dc9aeb; no other version was checked |
| Qt | 5.15.2, the kit `win64_msvc2019_64`, with the modules Core, Gui, Widgets, Network and WebSockets, and Test for the tests |
| Compiler | Microsoft C++ toolset v142 of Visual Studio 2019 |
| CMake | 3.22 or newer is required; verified with 3.29.5 as bundled with Visual Studio 2022 version 17.11; CMake 4 not tested |
| ROS 2 distribution | Humble, release `release-humble-20260220`; the plugin is an ament package that depends on `rviz_common`, `rviz_default_plugins` and `rclcpp` |
| C++ runtime | 14.40 or newer next to `rviz2.exe`, from the Microsoft Visual C++ Redistributable for x64 (the distribution ships 14.29, which the QarOS library does not run on; `fix_old_vc_runtime.ps1` replaces it) |
| colcon | colcon-core 0.21.0 and colcon-common-extensions 0.3.0, installed in the virtual environment from the pinned list |
| Python | 3.10.11, for the virtual environment of the build and for the layering guard, which needs only the Python standard library |
| GoogleTest | 1.14.0, downloaded when you configure, for the tests only |
| Other repositories | `qar_ogre_adapter` and `qar_qt_streaming`, each 0.1.0 |



### Known limitations

[SDK compatibility](docs/SDK_COMPATIBILITY.md#known-sdk-limitations) lists what the QarOS SDK does not document or does not do and [The QarOS in-AR browser](docs/SDK_COMPATIBILITY.md#the-qaros-in-ar-browser) what is known about the browser of QarOS, and [Troubleshooting](docs/TROUBLESHOOTING.md#rviz-and-plugin-problems) and [Web panel problems](docs/TROUBLESHOOTING.md#web-panel-problems) list the symptoms that you may meet. These limits affect what you can build on this repository:

- The code that calls the QarOS C API, the session owner, the render sender, the panel sender, the input sources and the URL adapter, is tested against a fake of the C API and not against the runtime, and only live sessions exercise the runtime.
- The logs of two earlier live sessions, held with a runtime earlier than the verified version, show the hover and click events with the ray of the runtime and 21 published picks, 12 of the 17 templates of the AR tools with the page of each loaded in the browser of QarOS, and a velocity command published from AR. In those sessions the authors of qar_rviz_plugin gave no verdict on how near a pick lands to the aimed point or on how the pages look. Live checks of the authors with the installed QAROS covered the join to the Hub of this PC and the scene in a Visualizer, and no session with a headset has run this release. Real multi-user devices and geographic anchoring against a live GPS source have not been checked in a live session, the authors reported no result of a service call or of an action goal from AR, and the templates `diagnostics`, `imu`, `robot-service` and `navigation-goal` appear in no log. [What the live sessions showed](docs/VERIFICATION.md#what-the-live-sessions-showed) lists the evidence.
- The drag of a slider and the use of two hands on Qt panels are covered by unit tests and by no live session, and the tap count and the long press by neither a unit test nor a live session.
- The two examples `qar_web_panel_example` and `qar_ros_topic_panel_example` were not run against a Visualizer.
- A call of the plugin that the GUI thread of RViz2 makes itself and that takes longer than 20 ms is logged by a guard; the calls into QAROS that can wait for the Hub run on the thread of the session owner.
- The vendor states that destroying a video sender while a session runs is safe in the current binaries, and crash dumps from before that statement exist; the close mode `notice` is the fallback.
- With a Visualizer attached, the Visualizer of QarOS crashed in each of the 25 cases that the authors of qar_rviz_plugin observed in which the plugin created its hidden Displays panel again after it had been closed, and QarOS started it again after several seconds. It also crashed five more times in one longer session, each within a second of the close or the creation of a panel, with the same fault in every crash report. This is a defect of the Visualizer. The plugin numbers the titles of the panels that it starts from the second start on, which does not prevent the crash. It does not number the panel of a dialog of a streamed window, the panel that the library of Qt creates again when the size of a streamed window changes, or the URL panels of the AR tools. The plugin hides and parks the AR panels by default instead of closing them, and no crash occurred in the live checks of the authors on the desktop Visualizer with hiding on, in which panels were hidden and shown again and RViz was closed.
- The plugin has to be built in the release configuration, because a plugin in the debug configuration mixes its runtimes with the release build of `rviz2.exe` and crashes on load.
- The live ROS tests of this repository, in `qar_ros_ui_tests`, `qar_rviz_streaming_ros_tests`, `qar_rviz_streaming_video_source_tests` and `qar_rviz_streaming_image_source_tests`, can fail once at the teardown of a ROS 2 participant on Windows; the re-run rule in [Testing](docs/TESTING.md#live-ros-tests-and-the-re-run-rule) says which failures to repeat.
- The server speaks plain HTTP and WebSocket and has no TLS mode of its own, so the token in the address of a panel is the only protection, and anyone who can read that address can open the panel; this holds for the web server of the AR tools as well.
- The browser of QarOS gives a page a viewport that follows the size of its panel, and no header field of the SDK describes it. The plugin sizes the panel for a design viewport of 500 by 500 CSS pixels and only logs the viewport that the page reports; a page that is still taller than the viewport is reached with the click pager.
- A control event of a viewer does not change the document that the server keeps: the page shows the change at once, and the host confirms or corrects it with its next publish, or with `set_value` from its control sink.
- The tests of the page script check it as text and never run it in a browser, so only a live session shows what a page does in the browser of QarOS.
