# qar_rviz_plugin - the RViz2 plugin and the web AR panels that bring RViz and the ROS 2 graph into QarOS

qar_rviz_plugin 0.1.0 streams RViz2 into QarOS. A panel inside RViz2 joins the QAROS Hub, renders the live 3D scene from the head pose of each target and sends it as stereo video, streams the Qt windows of RViz2 and tools that it generates from the ROS 2 graph into AR as GUI panels, and turns the pointer of the headset into ROS 2 messages. It is an RViz2 plugin for ROS 2, written in C++17 for Windows 11 and MSVC, that colcon installs as the ament package `qar_rviz_streaming`, together with a plain CMake project that builds the pure units of the plugin, the web libraries and the examples of this repository, and runs their tests. The AR tools are web pages that three C++17 static libraries of this repository generate, serve and connect to the ROS 2 graph, `qar_ui_model`, `qar_web_panels` and `qar_ros_ui`; a program of your own can use them without RViz, as the two examples of the repository do. It builds on the two repositories `qar_ogre_adapter` and `qar_qt_streaming`, and at run time it needs QAROS installed, which this repository does not contain.

## TL;DR

You need a Windows 11 machine with the toolchain, Qt, ROS 2 with its repairs (on Windows the Humble binary distribution, see [Installation](docs/INSTALL.md#ros-2-humble-for-windows)), the variables of the layout and the installed `qar_ogre_adapter` and `qar_qt_streaming` ([Installation](docs/INSTALL.md) walks through all of it), and QAROS installed on this PC. Then clone this repository, build and install the plugin, check the launch configuration and start RViz2:

```powershell
git clone https://github.com/Quaternar/qaros-ros-rviz "$env:QAR_ROOT\src\qar_rviz_plugin"
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
.\scripts\build_plugin.ps1
.\scripts\run_live_rviz_stream_real.ps1
```

The build ends with `=== BUILD OK ===`. Without `-Confirm` the launcher only prints what it would do and starts nothing; the block that it prints, recorded from a run, is in [Getting started](docs/GETTING_STARTED.md#check-the-launch-configuration).

Start QAROS from the Start menu, open a Visualizer window from it, and click `Add receiver` in the Visualizer if it has none. Then start RViz2 with the live panel, which opens RViz2:

```powershell
.\scripts\run_live_rviz_stream_real.ps1 -Confirm
```

In RViz2 press `"Connect to QarOS"` on the tab `Connection` of the panel `"Qar Streaming"`. The first time, QAROS asks in a window of its own whether `rviz2` may join: click Allow in the prompt of the QAROS Hub. The scene of RViz2 then appears in the Visualizer. The Hub of this PC is the default. A Hub on another PC needs `"Another PC (pairing code)"` in the group `"QAROS Hub"`, with the host and the port of that Hub and the pairing code that the Hub offers ([Connection panel](https://docs.quaternar.com/docs/next/user-guide/connection-panel/)).

The steps from a clean machine, with every output, are in [Quick start](docs/QUICKSTART.md); the details of each step are in the documents of the [Documentation](#documentation) table, and the web documentation at <https://docs.quaternar.com/> describes the stack in the context of QAROS: [Ogre and Qt in RViz](https://docs.quaternar.com/docs/next/developer-guide/ogre-qt-rviz/), [Ogre streaming](https://docs.quaternar.com/docs/next/developer-guide/ogre-streaming/), [Qt AR panels](https://docs.quaternar.com/docs/next/developer-guide/qt-ar-panels/), [SDK limits](https://docs.quaternar.com/docs/next/developer-guide/sdk-limits/) and [the RViz2 plugin](https://docs.quaternar.com/docs/next/operator-guide/rviz2-plugin/). The onboarding itself is described by the vendor in [Onboarding and sessions](https://docs.quaternar.com/docs/next/developer-guide/onboarding-and-sessions/), [Source applications](https://docs.quaternar.com/docs/next/operator-guide/source-applications/) and [Connection panel](https://docs.quaternar.com/docs/next/user-guide/connection-panel/).


## Who this is for

This repository is for QuaternAR engineers who integrate with QarOS. It assumes that you know QarOS and its C API, and it never explains them: where QarOS appears, the documentation states only what this stack relies on and how it uses the C API.

What it does explain, where it is used, is the part of the stack that is not QarOS:

- how a UI model becomes a web page that QarOS opens as a URL panel, and how the ROS 2 graph is read and written from it;
- RViz2 and how a plugin lives inside it, ROS 2 on Windows, Ogre 1.12.1 and Qt 5.15.2 in the extent that the plugin and its web libraries use them;
- colcon, the build tool of ROS 2, for the one package of this repository that is built with it;
- CMake and the Microsoft C++ toolchain, with the exact versions and the reason for each.

Everything that is not QarOS is installed from a fresh Windows 11 machine, step by step, in the installation document.

## Features

- **A panel in RViz2 that joins the QAROS Hub.** The panel `"Qar Streaming"` joins the Hub when you press `"Connect to QarOS"`, on a thread of its own, so that RViz2 stays responsive; nothing connects when the panel loads.
- **The live scene for every target.** For every target that the Hub lists and that is online, the plugin renders the scene of RViz2 from the head pose that QarOS reports and sends a colour picture and a depth picture for each eye.
- **RViz2 panels as AR GUI panels, and an AR control panel.** Any dock of RViz2 can be streamed into AR as a GUI panel, and the page `QAR control` switches the AR panels and the AR tools on and off from inside AR.
- **ROS 2 graph tools and video in AR.** The panel generates an AR panel for a topic, a service or an action of the ROS 2 graph, and a ROS 2 image topic reaches AR as a video panel.
- **Picking in AR.** The pointer of the headset publishes a point, a navigation goal, an initial pose or an area on the topics of the tools of RViz2.
- **Two launchers and an offline preview.** `scripts\run_live_rviz_stream_real.ps1` starts RViz2 with the live panel and without `-Confirm` only prints what it would do; `scripts\run_rviz_with_plugin.ps1` starts an offline preview with simulated peers.
- **Three web libraries.** `qar_ui_model`, `qar_web_panels` and `qar_ros_ui` turn a UI model into a page that QarOS opens as a URL panel, serve it and connect it to the ROS 2 graph; a program of your own can use them without RViz, as the two examples do.
- **Known limitations.** The code that calls the real QAROS library is tested against a fake of the C API, and the live checks with the installed QAROS covered the join to the Hub of this PC, the scene, the AR panels and the dialogs; the answers Deny and no answer, a stopped Hub, pairing with another PC, invitations and a session with a headset have not been run in a live session. [What the live sessions showed](docs/VERIFICATION.md#what-the-live-sessions-showed) lists the evidence, and [SDK compatibility](docs/SDK_COMPATIBILITY.md#known-sdk-limitations) what the SDK does not document.

Each feature is described in [Feature details](docs/ARCHITECTURE.md#feature-details).

## Where this fits

This repository is one of three that together stream an Ogre scene, Qt widgets, web pages and RViz into QarOS. Each repository has the version 0.1.0, and this documentation describes version 0.1.0 of all three: a repository asks for that version of the repositories it builds on, so the three have to be at matching versions. The table lists the three in the order in which they are installed: `qar_ogre_adapter` first, then `qar_qt_streaming`, then `qar_rviz_plugin`.

| Repository | Version | What it provides | Depends on | Also needs |
|---|---|---|---|---|
| qar_ogre_adapter | 0.1.0 | The QarOS integration layer: renders an Ogre scene for each eye and streams it to QarOS; the adapter for the QarOS C API; the vendored C header | none | The ROS 2 distribution, for Ogre only |
| qar_qt_streaming | 0.1.0 | Qt widgets as QarOS AR GUI panels | qar_ogre_adapter | Qt 5.15.2 |
| **qar_rviz_plugin** (this repository) | 0.1.0 | The RViz2 plugin that streams RViz into QarOS, and web AR panels from a UI model and from the ROS 2 graph | qar_ogre_adapter, qar_qt_streaming | The ROS 2 distribution with its repairs, Qt 5.15.2, colcon |

The installation document of each repository installs the repositories it depends on, with the commands for each, so you can start from any of them.

## Documentation

| Document | What it covers |
|---|---|
| [Quick start](docs/QUICKSTART.md) | The steps from a clean machine to a running example, with the output of each |
| [Installation](docs/INSTALL.md) | The machine, the toolchain, the dependencies and this repository, step by step, with the commands |
| [Getting started](docs/GETTING_STARTED.md) | The first program that you run, and what you see |
| [Architecture](docs/ARCHITECTURE.md) | How the code is structured, which part owns what, and why |
| [API](docs/API.md) | The public interface, with examples that are compiled with the repository |
| [Configuration](docs/CONFIGURATION.md) | Every environment variable and build option |
| [Testing](docs/TESTING.md) | How to run the tests, what they cover and what they do not |
| [Troubleshooting](docs/TROUBLESHOOTING.md) | Symptoms, causes and fixes |
| [SDK compatibility](docs/SDK_COMPATIBILITY.md) | The QAROS version that was verified and the QarOS behaviour this repository relies on |
| [Developing](docs/DEVELOPING.md) | The layout of the repository and the conventions for changing it |
| [Verification](docs/VERIFICATION.md) | The checks of each level, how to run them, and their status |
| [Wire protocol](docs/WIRE_PROTOCOL.md) | The protocol between the panel page and the server |
| [Operator guide](docs/OPERATOR_GUIDE.md) | Using the plugin in RViz: the panel, the Hub, targets, tools and modes |
| [Log reference](docs/LOG_REFERENCE.md) | The log lines of the plugin and what they mean |
| [Changelog](CHANGELOG.md) | What each version contains |
| [License](LICENSE) | The license of this repository |

## The QarOS SDK is not included

QAROS, which consists of `qar-streaming-c.dll`, `qar_streaming.dll`, the zenoh and GStreamer stacks, the Hub and the Visualizer `qar-streaming-viz.exe`, is QuaternAR's proprietary product and is not part of this repository. You obtain its setup program from QuaternAR and install it once on the PC. The crash reports of the Visualizer carry the name of a second program of the same installation, `qar-stereokit-gui.exe`.

No part of QAROS is copied into this repository. The C header of the QarOS C API is vendored by the repository qar_ogre_adapter and installed with its libraries.

You do not need QAROS to build this repository or to run its tests. Nothing links against QAROS: a program loads the installed library `qar-streaming-c.dll` at run time, and configuring this repository does not look at QAROS. You need QAROS to connect to it.

A program finds the installed QAROS by itself: the installer records the install folder in the registry, and the library is loaded from there. How a program joins the Hub is described on the pages of QuaternAR: [Onboarding and sessions](https://docs.quaternar.com/docs/next/developer-guide/onboarding-and-sessions/).

This stack was verified against the installed QAROS 0.9.0-dev.3008+65dc9aeb; the [SDK compatibility](docs/SDK_COMPATIBILITY.md) document says how to identify the version you have.

## Platform support

The stack runs on Windows 11, 64-bit, and is built with the Microsoft C++ toolset v142 (Visual Studio 2019). It is run from a local desktop session. Linux is not built or tested.

## License

This repository is licensed under the MIT license; the text is in [LICENSE](LICENSE), and every C++ source and header carries the identifier of the license and the copyright line of QuaternAR.

Third-party material in this repository:

- `third_party/stb` holds the single-header PNG writer `stb_image_write.h`; its README names the origin.
- GoogleTest is downloaded when you configure, only for the tests; it is not part of this repository and is not installed with it.
