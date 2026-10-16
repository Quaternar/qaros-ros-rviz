# Developing

This page is for a contributor who changes the code of the plugin: where things live, which rules the build and the layering guard enforce, how to add a unit with its tests, how to write documentation, which interfaces of RViz the plugin uses and which pitfalls of the compiler and the headers it has met. [Architecture](ARCHITECTURE.md) explains how the parts work together, [API](API.md) lists where each feature lives, and [Testing](TESTING.md) describes the tests.

## Repository layout

| File | What it holds |
|---|---|
| `qar_rviz_streaming` | the ament package: `include\qar_rviz_streaming`, `src`, `test`, `config`, `plugin_description.xml`, `package.xml` and the `CMakeLists.txt` that colcon builds |
| `CMakeLists.txt` and `CMakePresets.json` | the plain-CMake project of the root, which builds the three web libraries, the two examples, the pure units of the plugin and their tests without RViz |
| `cmake\QarBuildSupport.cmake` | the functions that find the two upstream packages or their source checkouts, the warning flags, the download of GoogleTest and the tests of the layering guard |
| `scripts` | the scripts that build, start and test the plugin, the repairs of the ROS 2 distribution, the layering guard `check_layering.py`, and the helper programs: the publishers of test data, the helpers for rosbag files and the analyser of the timing files |
| `third_party\stb` | the vendored single-header PNG writer and its README |
| `docs` | the documents, and `docs\snippets`, the compile-checked snippets of the documents |


The units of the package are listed with their roles in [Components](ARCHITECTURE.md#components) and in [Module guide for contributors](API.md#module-guide-for-contributors). The repository holds the plugin and the three web libraries that it uses; the libraries of qar_ogre_adapter and qar_qt_streaming live in those repositories, and a change in one of them is made there and installed into the prefix before the plugin is built again.

The web libraries, the examples and the build files around them are laid out like this.

```text
qar_rviz_plugin/
  CMakeLists.txt         top-level build of the libraries, the examples and the unit tests
  CMakePresets.json      the preset vs2019-x64
  cmake/                 QarBuildSupport.cmake, the build helpers
  qar_ui_model/          UI model, page renderer and wire protocol; the standard library only
    include/qar_ui_model/    public headers
    src/                     sources, among them the page script and the style sheet
    test/                    the test executable qar_ui_model_tests
  qar_web_panels/        HTTP and WebSocket delivery of pages to QarOS URL panels; Qt Core, Network and WebSockets
    include/qar_web_panels/  public headers
    src/                     sources
    test/                    the test executable qar_web_panels_tests
  qar_ros_ui/            ROS 2 graph discovery, introspection and the topic, service and action bridges
    include/qar_ros_ui/      public headers; detail/ holds the two internal headers of the dynamic clients
    src/                     sources
    test/                    the test executable qar_ros_ui_tests
  examples/              CMakeLists.txt, web_panel_example/ and ros_topic_panel_example/
  docs/                  these documents; snippets/ holds the compile-checked snippets
  scripts/               check_layering.py, the layering guard, and the scripts of the install kit
  requirements.txt       the Python packages of the virtual environment
```

## Layering rules

The layering guard `scripts\check_layering.py` reads the `#include` lines of the sources and enforces which module may include which; [The layering guard](TESTING.md#the-layering-guard) describes the rules, the tests and how to read a failure. In this repository it checks the modules `qar_ui_model`, `qar_web_panels`, `qar_ros_ui` and `qar_rviz_streaming` and the folder `examples`. In production code `qar_rviz_streaming` may include the standard library, Qt, Ogre, ROS 2, RViz, stb and the platform headers `windows.h`, `mmsystem.h`, `process.h` and `unistd.h`, and every project module; its tests may also include GoogleTest. It is the only module of the stack that may include the headers of RViz, because it is the one that RViz loads.

The three libraries form a stack, and the layering guard keeps it that way. `qar_ui_model` is the base and includes the standard library only. `qar_web_panels` adds Qt and the delivery of pages, and `qar_ros_ui` adds ROS 2 and the bridges. Neither of the two upper libraries includes the other: a host that needs both, as the example `ros_topic_panel_example` does, joins them itself.

The guard cannot see a smaller boundary that the plugin keeps inside itself: the pure units must not include RViz, Qt widgets or rclcpp, and the test executable `qar_rviz_streaming_tests` of the plain-CMake project is what catches a violation, because it links none of them, so that a pure unit that includes an RViz header does not compile there. When a unit needs RViz for a small part, the part is split off and the rest stays pure. The precedents are `grid_floor_plane.cpp`, which touches Ogre and RViz, next to `grid_floor_plane_geometry.cpp`, which does not, and `hidden_rviz_panel.cpp` next to `rtti_guard.hpp`, which uses the standard library only; the panel asks `panel_display_names.hpp`, which uses the standard library only, for the title of a new QarOS panel; `display_class_catalog.cpp` reads the manifests with QtCore alone.

| Library | May include | Why that much and no more |
|---|---|---|
| `qar_ui_model` | the standard library and its own headers | no Qt, no ROS 2, no Ogre and no JSON library of a third party, so that the guard can enforce that it includes the standard library only |
| `qar_web_panels` | the standard library, Qt, its own headers, those of `qar_ui_model`, and from `qar_ar_core` only the wrapper header of the QarOS C header | it is reusable by any Qt application, with no dependency on RViz, ament, `rclcpp`, Ogre or Qt Widgets |
| `qar_ros_ui` | the standard library, ROS 2 headers, its own headers and those of `qar_ui_model` | the ROS 2 client libraries are its point, and Qt and Ogre stay out so that a web server, a command line tool or a test can use it |


A violation is fixed in the code: move the code to the layer that may use the header, or pass the dependency in through an interface. The table is changed only when the architecture changes. It is in three dictionaries and a list at the top of `scripts\check_layering.py`: `POLICY` for the libraries, `EDGE_ONLY` for the edges that may use named headers only, `EXAMPLE_POLICY` for the folders under `examples`, and `MODULES` for the names. A new library needs an entry in `POLICY` and its name in `MODULES`, and the top-level build passes the name to the function `qar_add_layering_guard_tests`, which registers one test for it. A new example folder needs an entry in `EXAMPLE_POLICY`. The self test has to keep passing.

## Build conventions

The plugin is C++17 and is built in the configuration `RelWithDebInfo`, which the package selects when nothing else is given. A plugin in the configuration `Debug` would mix its C++ runtime and its Qt libraries with those of the release build of `rviz2.exe` and would crash when RViz loads it.

Every library is a static library with the alias target `qar_rviz_plugin::<library>`, compiled as C++17 through a public compile feature and with position-independent code, and its sources are listed one by one in its CMake file; nothing is found by a glob.

- **colcon builds the package, and only `scripts\build_plugin.ps1` starts it.** The repository root is not a colcon package, so the script passes the folder of the package with `--paths`, which makes it the one supported way to build the plugin. [Build and install qar_rviz_plugin](INSTALL.md#build-and-install-qar_rviz_plugin) lists its steps.
- **The libraries come from installed packages or from checkouts.** The default is the packages in the prefix, which `-DQAR_PREFIX` names, and a developer who changes a library of those repositories passes the two `-DQAR_..._SOURCE_DIR` options to build against the checkouts; the web libraries always come from the tree of this repository ([Build options](CONFIGURATION.md#build-options)).
- **Moc and the headers.** Qt moc is switched on after the upstream libraries are found, so that the libraries, which have no `Q_OBJECT` types, are not scanned. The headers that declare `Q_OBJECT` types are listed among the sources of the library, so that the automatic moc processes them: a new class with `Q_OBJECT` goes into that list.
- **Link signatures.** Every `target_link_libraries` on the target uses the plain signature, because `ament_target_dependencies` does, and mixing the keyword form into one target is an error.
- **Warnings.** The test targets get the warning flags of the repository, `/W4` and `/permissive-` where the module sets it, from `qar_apply_warning_flags`.
- **The test executables** register their tests with `gtest_discover_tests`, so that a new test is not listed by hand anywhere.

A library links publicly what its public headers need. `qar_web_panels` links `qar_ui_model`, Qt and `qar_ar_core`, because its headers derive from `QObject` and one of them returns a type of the QarOS C header; `qar_ros_ui` links `rclcpp` and the other ROS 2 libraries, because its headers hand out their types. [Header map](API.md#header-map) lists the headers of each library.

`qar_web_panels` uses Qt AUTOMOC. Its headers are under `include`, away from the sources, so the headers of the classes that have `Q_OBJECT` are listed as sources of the library, and a new class of that kind has to be added to that list.

GoogleTest is fetched when the build is configured and is never installed with the project. The preset, the commands and the options are in [Installation](INSTALL.md#build-and-install-qar_rviz_plugin) and [Configuration](CONFIGURATION.md).

## Coding conventions

These conventions apply to every C++ file of this repository. Keep them when you add code; the layering guard and the tests catch some violations, the rest is up to review.

**Language level and warnings.** The code is C++17. Targets are compiled with the warning level `/W4`, and with `/permissive-` where the module sets it.

**Configuration.** Build `RelWithDebInfo`, never Debug. The ROS 2 and Qt binaries that this code links or loads are release builds, and a debug C runtime in the same process crashes on load.

**File header.** Every C++ source and header starts with these two lines, and the year and owner are the same in every file:

```text
// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
```

**Names.** The namespace of a module is the name of the module. Header guards follow the ROS 2 style, the module name in capitals, two underscores, the file name in capitals and a trailing underscore, for example `QAR_QAR_ADAPTER__ENV_CONFIG_HPP_` for the file `env_config.hpp` of the module `qar_qar_adapter`.

**Comments and documentation.** Write in English. A comment states what the code does and why it does it that way, as the code is now. It contains no dates, no milestone or ticket tags, no names of people or tools and no account of how the code came to be. Documentation comments describe the contract of a function or type: what it expects, what it returns, who owns what, and on which thread it may be called.

**Logic that talks to another system.** Code that reads the environment, Qt, ROS 2 or QarOS is kept thin; the decisions it makes are put into functions that take plain values, so that they can be tested without a QarOS session, a window or a ROS 2 graph. For example, the helpers that parse environment variables take the raw text and never read the environment themselves; their callers pass it in. There is no fake of the QarOS SDK.

This repository adds these rules to the shared ones.

- **Qt keywords.** Write `Q_SIGNALS` and `Q_SLOTS`, and the uppercase form of `emit` when a signal is emitted, and never the lowercase Qt keywords, because the target is compiled with `QT_NO_KEYWORDS` ([Qt keyword and header pitfalls](#qt-keyword-and-header-pitfalls)).
- **The GUI thread.** Every touch of a widget and every call into the session happens on the GUI thread, and a callback of QarOS or of a ROS executor only queues what it received and returns. A call that blocks the thread stops RViz, so that a new call into QarOS that is synchronous needs a reason in its comment ([Threading summary](ARCHITECTURE.md#threading-summary)).
- **Declaration order.** The order of the members of the panel is a contract: a member that borrows another is declared after it and so is destroyed before it. The comments at the members say which order matters, and a new member that borrows the session, the manager or a node is declared after the thing it borrows.
- **Environment variables.** The helpers that parse a value take its raw text and never read the environment, for example `parse_bool_env` and `parse_tick_ms_env`, and the panel reads the variable where its feature starts and passes the text in.
- **Log lines.** Everything that the panel logs goes through `appendLog`, which writes to the view and to the standard error of RViz. A line starts with the keyword of its feature, such as `LIVE:` or `"AR PANEL:"`, and says what happened and why, so that [Log reference](LOG_REFERENCE.md) can list it.
- **Guards of headers in a folder.** A header in a subfolder of `include/<library>/` has the folder in its guard, as in `QAR_ROS_UI__DETAIL__DYNAMIC_ACTION_CLIENT_HPP_` for a header in `detail`.
- **Include order.** Include the headers of `qar_ui_model` before any header that includes `<windows.h>`, which the wrapper of the QarOS C header does. The macro `interface` of `<windows.h>` breaks `type_description.hpp` when it comes first, and [The compiler stops at a member named interface](TROUBLESHOOTING.md#the-compiler-stops-at-a-member-named-interface) shows the failure.
- **Nothing waits.** A call of `qar_ros_ui` queues its work and returns, and the outcome arrives through a callback or a future. Callbacks are called after the lock of the bridge is released, and a callback that is registered with the node holds only a weak pointer to the state of its bridge. [Threading](ARCHITECTURE.md#threading-nothing-waits) has the rules.
- **One thread for a session.** A `PanelSession` is called on the thread that owns it, and a call from another thread is refused.

## Adding code and tests

**A new unit.** Put the header in `include\qar_rviz_streaming` and the source in `src`, and list the source in `add_library` of the package, which is the colcon target. A unit that needs neither RViz nor Qt widgets nor rclcpp is also listed in the executable `qar_rviz_streaming_tests` by its path in `src`, together with its test file, so that the plain-CMake project compiles it.

**A source file.** Put the header in `include/<library>/` and the source in the source folder of the library, with the header guard and the file header above, and add the source to the list of `add_library` in the CMake file of the library.

**A new test.** Tests are GoogleTest files in `test`, named after the unit, such as `ar_control_router_tests.cpp`, with one suite for each behaviour of the unit and test names that are sentences about what the unit does, such as `EveryTypeIsClaimedByExactlyOneTemplate`. Nothing registers a test by hand, because `gtest_discover_tests` finds it. A test that needs ROS goes into a separate executable, so that the first executable stays free of ROS: `qar_rviz_streaming_ros_tests` for the checks against the interface packages of the distribution, and one executable of its own for a suite that has its own `main`, like the one of the video executor. A live ROS test holds the resource lock `qar_ros_dds` and turns the shared-memory transport of Fast-DDS off, because participants of short-lived processes crash at their teardown now and then ([The re-run rule](TESTING.md#the-re-run-rule)).

**A change of the panel.** Nothing of `qar_streaming_panel.cpp` is compiled by the test project, so a change there needs the colcon build ([Build gates](#build-gates)). Keep the decision of a feature in a function that takes plain values and test that function: the panel then only reads the controls, calls the function and shows the result.

**What to update with the code.** A new control goes into [The Qar Streaming panel at a glance](OPERATOR_GUIDE.md#the-qar-streaming-panel-at-a-glance), a new environment variable into the tables of [Configuration](CONFIGURATION.md#environment-variables), a new line of the log into [Log reference](LOG_REFERENCE.md), and a new template into the table of the templates and into [API](API.md#adding-a-semantic-template).

**A test.** Tests use GoogleTest. Put the file in `test/` of the library and add it to the list of `add_executable` of the test executable, `qar_ui_model_tests`, `qar_web_panels_tests` or `qar_ros_ui_tests`. GoogleTest discovery registers the tests with CTest, so there is no list of tests to keep. The names of the tests say the property that has to stay true, for example `HtmlRenderer.CarriesABootDiagnosticTheRuntimeHides`.

**What a test may use.** The tests of `qar_ui_model` are plain C++ over strings and value trees. The tests of `qar_web_panels` open real sockets on loopback and need no window and no browser, so the page script is never executed in them, and a test checks the text of the page and the C++ mirror of the script instead. A new Qt module that a test uses has to be added to the list of libraries that the build copies next to the test executable.

**Tests that need ROS 2.** Most tests of `qar_ros_ui` need no ROS 2 graph: they load the typesupport libraries of the distribution and exercise introspection, conversion and serialization over real message types. A few are live: they create a node, a typed publisher or server and a bridge in the test process, with a domain id of their own, which is 50 plus the process id modulo 50, with bounded waits and without a daemon, and they are compiled out when `test_msgs`, `geometry_msgs` or `std_srvs` is missing. The distribution is put on `PATH` for the tests by a script that the test directory adds to the test include files, and the test setup does not set `AMENT_PREFIX_PATH`, so the registry has to find the libraries without it. Every test of the executable holds the resource lock `qar_ros_dds` and runs with `FASTDDS_BUILTIN_TRANSPORTS=UDPv4`, so that no two run at once and the shared-memory transport stays off. [Live ROS tests and the re-run rule](TESTING.md#live-ros-tests-and-the-re-run-rule) says what to do when one fails.

**An example.** An example is a folder under `examples` with an entry in `EXAMPLE_POLICY`, a `CMakeLists.txt` of its own and a program that links the public targets of this repository only.

## Documentation and snippets

The documents are written in English, the commands in them are PowerShell 7 commands, and a block of C++ or CMake code in a document is a region of a file in `docs\snippets` or of an example, never typed text. The folder is a CMake target of the root project, `qar_rviz_plugin_doc_snippets`, which is built and never run and registers no test, so that the code that a document shows compiles and links. To show code, add a region to a file of that folder and include it from the document. A statement about behaviour in a document describes what the code does at this commit, and when the code and a comment disagree the documents follow the code.

A region is the part of a file between two comment lines in the comment syntax of the file: the first has the word qar-snippet, a colon and the name of the region in square brackets, and the second has qar-snippet-end, a colon and the same name in square brackets. The target `qar_rviz_plugin_doc_snippets` is built when the CMake option for the documentation snippets is on, which is its default, and [Configuration](CONFIGURATION.md#build-options) names the option. When you change a snippet, change the block of the document that shows it in the same commit.

## RViz APIs used

The plugin calls the interfaces of RViz that the table lists, and nothing else of RViz. The header files are those of `rviz_common` of the ROS 2 Humble distribution, and the table is the list to check when RViz changes.

| File | What the plugin uses from it |
|---|---|
| `panel.hpp` | the base class `Panel`, its `onInitialize()` and `getDisplayContext()` |
| `display_context.hpp` | `getSceneManager()`, `getViewManager()`, `getRootDisplayGroup()`, `getRosNodeAbstraction()`, `getWindowManager()`, `getFrameCount()`, `lockRender()`, `unlockRender()` and `queueRender()` |
| `ros_node_abstraction_iface.hpp` | `get_raw_node()` of the node that RViz gives its plugins |
| `display_group.hpp` and `display.hpp` | `createDisplay()` and `addDisplay()` of the group and, for a display, `initialize()`, `setName()`, `setTopic()`, `setEnabled()`, `isEnabled()`, `getClassId()`, `getName()`, `getAssociatedWidget()` and its properties |
| `panel_dock_widget.hpp` | the signal `closed()` of the dock that hosts a panel |
| `visualization_frame.hpp`, `class_id_recording_factory.hpp` | the main window and the factory template, for the Displays panel that is made without a dock ([The hidden Displays panel](ARCHITECTURE.md#the-hidden-displays-panel)) |
| `rviz_rendering` and `rviz_default_plugins` | the class `Shape` and the camera of the current view for the floor plane and the cameras of the peers, and `GridDisplay`, which is the public handle onto the properties of a Grid display |
| `pluginlib/class_list_macros.hpp` | the macro that exports the panel class; the class loader of pluginlib is not used |
| `ament_index_cpp` | `get_resources()` and `get_resource()`, which find the manifests of the display classes through the resource index |

A display is made the way RViz makes one for its own Add by topic window: the group of the root creates it from the class identifier, adds it, initialises it with the display context, names it and gives it the topic and the type. The four calls are what `VisualizationManager::createDisplay` and `addDisplay` are made of, which the plugin cannot call, because the header of that class reaches `pluginlib/class_loader.hpp` and with it `tinyxml2.h`, which the Windows distribution does not install.

## Build gates

Four gates stand between a change and a release, and each proves something that the others do not.

| Gate | What it proves | What it does not prove |
|---|---|---|
| the plain-CMake test project with `ctest` | the web libraries and the pure units of the plugin compile without RViz and their tests pass | that the panel compiles, and anything that touches RViz, Ogre or QarOS |
| the colcon build through `scripts\build_plugin.ps1` | every source compiles and links against the distribution, the panel included, and the result installs into the prefix | that any test passes |
| the layering tests `check_layering_*` | the includes follow the table of the guard | that a pure unit stays pure |
| the target of the snippets | the code that the documents show compiles and links | that the documents are right |

A change of a pure unit needs the first and the third gate. A change of the panel or of any unit that touches RViz needs the second, because no other build compiles it: a mistake in the panel can pass every test and break only the build of the plugin. [The colcon build is a gate](TESTING.md#the-colcon-build-is-a-gate) says the same from the side of the tests. Close every `rviz2.exe` before the colcon build, because Windows does not let a running RViz lose the DLL that it has loaded ([The plugin cannot be rebuilt because its DLL is in use](TROUBLESHOOTING.md#the-plugin-cannot-be-rebuilt-because-its-dll-is-in-use)). What no gate proves is how the panel behaves in RViz and against QarOS, and that is checked by hand in the scripted modes of the offline preview and in a live session ([Scripted checks in a window](TESTING.md#scripted-checks-in-a-window)).

## Qt keyword and header pitfalls

The compiler and the installed headers of the distribution have caused these problems, and each has a rule.

- **`emit` is a method name.** The reusable library `qar_ogre_streaming` has a method named `emit`, and Qt defines `emit` as a macro, so the target of the plugin is compiled with `QT_NO_KEYWORDS`, and the plugin writes `Q_OBJECT`, `Q_SLOTS` and `Q_SIGNALS`, and the uppercase form of `emit`, as the public headers of `rviz_common` do.
- **One source opts out.** `hidden_rviz_panel.cpp` needs the header of the main window of RViz, which includes `render_window.hpp`, and that installed header declares `public slots:` with the plain Qt keyword, which does not compile under `QT_NO_KEYWORDS` (error C2146). That source therefore removes the definition for itself with `-UQT_NO_KEYWORDS`. The removal comes after the definition on the command line and the compiler takes the last one, with a harmless note D9025. The clash that the definition prevents cannot arise in that file, because it includes nothing from the reusable libraries, and **it must stay that way**: do not include a header of `qar_ogre_streaming`, `qar_qt_streaming` or `qar_ros_ui` there.
- **`interface` is a macro on Windows.** The Windows SDK defines `interface` as `struct`, and the headers reach it through rclcpp, rcutils and `windows.h`. Both `TypeNameParts` of `qar_ui_model` and `TypeSupportRegistry` of `qar_ros_ui` have a field named `interface`, which is the ROS word for message, service or action, so that the header of the panel removes the macro with `#undef interface` before it includes their headers. Nothing in the plugin uses the COM alias, and the headers of the SDK that come later may define it again.
- **tinyxml2 and pluginlib.** The distribution ships the CMake configuration of tinyxml2 and its DLL and not its header, so a plugin must not include a header that reaches `<tinyxml2.h>`: neither `pluginlib/class_loader.hpp` nor `visualization_manager.hpp`, which reaches it through `transformation_manager.hpp` and `pluginlib_factory.hpp`. Include `display_group.hpp`, which pulls in only `display.hpp`, discover display classes through the resource index of ament, and export the panel class with `pluginlib/class_list_macros.hpp`.
- **A complete type for the node.** `getDisplayContext()` is declared by `Panel`, but reaching `getRosNodeAbstraction()` through it needs the complete `DisplayContext`, so that the panel includes `display_context.hpp` and `ros_node_abstraction_iface.hpp` itself.
- **`typeid` of a class template.** [The RTTI guard rule](#the-rtti-guard-rule) states the rule for the compiler bug that makes `typeid` of a class-template type answer with its static type.

## The RTTI guard rule

**Never apply `typeid` to a class-template type in this code.** MSVC evaluates `typeid(*p)` statically, at compile time, when `*p` is of a class-template specialization and that `typeid` is the first thing in the translation unit that needs the specialization instantiated; the answer is then always the static type, whatever the object is. It reproduces on VS2019 14.29 and on VS2022 14.41, at /O2 and at /Od, and a guard written as `typeid(*factory)` on the pointer to the factory of RViz rejected every factory, the right one included. `typeid` through a non-template polymorphic base is a true run-time lookup.

The function template `rtti_cast_from_opaque<Root, Target>(raw, accept_type_name)` in `rtti_guard.hpp` is the one way in which the plugin identifies an object behind an opaque `void *`. `Root` is a non-template polymorphic class that is the subobject at offset 0 of the object, and `typeid` is applied to it and to nothing else; `Target` is the class that the caller will use the object as, which may be a class-template specialization, and it is only reached by `dynamic_cast`. The checks run in this order, and the first that fails gives the error:

1. `typeid(*root)` gives the most derived type of the object, which must not be `Root` or `Target` themselves, because a complete object is never of an abstract type, and seeing one of them means that `typeid` was evaluated statically after all.
2. The name of that type must be accepted by the predicate of the caller, which compares exactly: the MSVC and the Itanium spelling of `rviz_common::PanelFactory` are accepted, and a different class in the same namespace, the same name in another namespace and the template layers underneath are rejected.
3. `dynamic_cast<void *>` of the root must equal `raw`, so that `raw` is the start of that object and not a pointer into its middle.
4. `dynamic_cast<Target *>` must succeed, and only that pointer is returned.


Where `raw` is not an object at all, the run-time type information of MSVC throws `std::__non_rtti_object` and the function turns that and every other `std::exception` into the error of the result; elsewhere reading it would be undefined behaviour, so the caller makes sure that `raw` was read from a sane place, which `hidden_rviz_panel.cpp` does with a layout canary. When you add another access of this kind, use the same function, write the predicate for the exact name of the type, and test it with stand-ins: the tests meet the concrete classes as opaque pointers, because their types are defined in a second file only, the way the plugin meets the factory of RViz.

## Adding a semantic template

[Adding a semantic template](API.md#adding-a-semantic-template) describes the interface, the score, the three rules and the tests. These are the rules of the workflow in addition.

- The name of a template is lower case words joined by hyphens, such as `velocity-command`, it is unique, and the catalog is the order of registration; the generic form is registered first by the registry and still can never win, because a semantic template scores 100 and the generic form 1.
- Claim a type by its canonical name `pkg/msg/Type`, and claim it once: a type that two templates claim is a failure of the catalog test.
- A layout may use a field only after it resolved it, so a template for a type that this repository cannot introspect, such as an action of Nav2 that is not in every distribution, is safe, and its test against the real description is skipped where the distribution lacks the type.
- Keep the number of layouts in the documents in step with the catalog: the catalog has 17 entries, and the operator guide has one row for each of them.
- Run the tests of the templates before you build the plugin: `ar_semantic_templates_tests.cpp` needs no ROS, and `ar_semantic_templates_ros_tests.cpp` needs the installed distribution; [List and select tests](TESTING.md#list-and-select-tests) shows how to run one suite.

## JavaScript and CSS rules for generated pages

The browser of a URL panel is older than the browsers that web developers use, and the page script cannot be executed by the tests, so the rules below are kept by tests of the text that the renderer produces. [The QarOS in-AR browser](SDK_COMPATIBILITY.md#the-qaros-in-ar-browser) says what is known about the engine.

**The script.** It is ES5 JavaScript and pure ASCII, and it contains no `innerHTML`, no `eval`, no `document.write` and no text that closes a script element. The tests forbid exactly these things: arrow functions, template literals, the declarations with the keywords let and const, optional chaining, nullish coalescing and `Number.isInteger`. Classes, default parameters, spread and `for...of` are not tested for, and the rule of the code is that none of them is used.

**Literals of the script.** The script is split into six string literals, because MSVC limits one literal to 16 KB, and the pieces are joined when the page is built. A new piece of script goes into a literal that has room, and the text may never contain the sequence that closes a script element, because the script sits inside one.

**The style sheet.** It has no hover rule and no `:has()`, `aspect-ratio` or `backdrop-filter`. It has no fixed positioning, because only absolute positioning and `overflow: auto` are on the list of what the authors of qar_rviz_plugin have seen work in the engine. It has no length in pixels outside the variables of the theme except the entries of an exemption list that a test holds, each with its reason, because the page scales every pixel variable by one factor when it fits itself to its viewport, and a literal would stay at full size while its neighbours shrink. A test fails when a literal is added, and again when an exemption is no longer needed; the same holds for fixed heights. The pieces of a range input are the one place where literals stay, as a fallback that a later rule replaces with the scaled value, and a test checks that the replacement exists.

**Spacing and shrinking.** The theme uses `gap` for spacing and gives its uses of `gap` a counterpart under `[data-qar-nogap]` that uses margins, because the engine does not apply `gap` to flex containers. A test pins the fallback for rows, and the rest is a matter for review. The children of the column containers carry `flex-shrink: 0` for the same reason: the engine does not apply `min-height: auto`.

**A new rule.** Check a new property or selector against the table in [The QarOS in-AR browser](SDK_COMPATIBILITY.md#the-qaros-in-ar-browser). A property that the engine does not know is ignored, which is harmless for a refinement, and a selector that it does not know drops the whole rule that contains it, so give it a rule of its own.

## ROS 2 Humble introspection notes

`qar_ros_ui` works with message types that it has no C++ type for, so it rests on facts about the ROS 2 distribution. The facts below are those of the Humble binary distribution for Windows, release-humble-20260220: `rclcpp` 16.0.18, `rcl_action` 5.3.12 and `rmw_fastrtps_cpp` 6.2.10 as the default middleware. The authors of qar_rviz_plugin read them off an installed distribution, and the tests of the library confirm them. They are facts about Humble: a later release differs where this section says so, and the command at the end of the section checks one of the differences on another distribution. Header paths are relative to the `include` folder of the distribution, where every package has a directory of its own.

### The introspection typesupport

A generated message is described by a table, `MessageMembers`, with one `MessageMember` for each field. The library reads from the table the members `message_namespace_`, `message_name_`, `member_count_`, `size_of_`, `init_function` and `fini_function`, and from each field `name_`, `type_id_`, `string_upper_bound_`, `members_`, which describes a nested message, `is_array_`, `array_size_`, `is_upper_bound_`, `offset_` and the accessor functions below. The namespace names the kind of interface, `<pkg>::msg`, `<pkg>::srv` or `<pkg>::action`. There is no introspection structure for actions in Humble.

The type of a field is a constant, and the member of the generated structure that holds it follows from the constant:

| Constant | Member type |
|---|---|
| `ROS_TYPE_FLOAT`, `ROS_TYPE_DOUBLE`, `ROS_TYPE_LONG_DOUBLE` | `float`, `double`, `long double` |
| `ROS_TYPE_CHAR`, `ROS_TYPE_OCTET`, `ROS_TYPE_UINT8` | `std::uint8_t`; a `char` of a message definition is an unsigned 8-bit integer in ROS 2 |
| `ROS_TYPE_WCHAR` | `char16_t` |
| `ROS_TYPE_BOOLEAN` | `bool` |
| `ROS_TYPE_INT8` to `ROS_TYPE_INT64`, `ROS_TYPE_UINT16` to `ROS_TYPE_UINT64` | the integer type of the same width and sign from `<cstdint>` |
| `ROS_TYPE_STRING`, `ROS_TYPE_WSTRING` | `std::string`, `std::u16string` |
| `ROS_TYPE_MESSAGE` | the structure of a nested message, described by `members_` |

Which accessor functions a field has depends on its kind, as the generator of Humble writes them:

| Field | `size_function` | `get_const_function` and `get_function` | `fetch_function` and `assign_function` | `resize_function` |
|---|---|---|---|---|
| scalar | null | null | null | null |
| fixed array | set | set | set | null |
| sequence, bounded or not | set | set | set | set |
| sequence of `bool`, bounded or not | set | null | set | set |

The library follows three rules from this. It never reinterprets a container itself, because a bounded sequence is not a `std::vector` and a sequence of `bool` is bit-packed. It reads and writes the elements of a sequence of `bool` through `fetch_function` and `assign_function` with a `bool` scalar. And it checks `is_upper_bound_` and `array_size_` before it calls `resize_function`, because a resize beyond the bound throws `std::length_error`.

Two things are not in the table. The member `default_value_` is null for every field of every type, because the generator leaves it to be set, so the library constructs an instance with `MessageInitialization::ALL`, which carries the defaults of the interface definition, and reads the primitive fields back. Constants of an interface are not in any Humble typesupport, so `MessageDescription::constants` stays empty. The generator gives a message without fields one placeholder member, `structure_needs_at_least_one_member`, and introspection reports it.

### Handles from a type name

A package has two typesupport libraries that the library needs, both in the `bin` folder of the prefix on Windows. The introspection library, for the identifier `rosidl_typesupport_introspection_cpp`, exports the handles of messages and services and none of actions. The dispatch library, for `rosidl_typesupport_cpp`, exports the same handles and the handle of an action.

The dispatch handle is what every entry point of the middleware takes, so it goes to `rmw_serialize`, to publishers, subscriptions, clients and action clients, and the introspection handle is only for reading and writing instances. On first use the dispatch loads the library of the concrete typesupport by its file name alone, through `rcpputils::SharedLibrary`, which loads with `LoadLibrary` on Windows, so the search order of the operating system decides, and the `bin` folder of the distribution has to be on `PATH` at run time whatever this library does.

The function `rclcpp::get_typesupport_library` finds the package through the ament index, so it needs `AMENT_PREFIX_PATH` and fails without it, and `rclcpp::get_typesupport_handle` builds the names of message symbols only. `TypeSupportRegistry` therefore finds the libraries itself, in the prefixes that were added, then in the folders of `AMENT_PREFIX_PATH`, and last through the loader, and it builds the symbols of messages, services and actions. It never unloads a library, because the tables are static data in it and every description and instance points into them. `GenericPublisher` and `GenericSubscription` call only the half of the helpers that takes a symbol from a library that is passed in, so they need no ament index.

### Serialization and generic topics

`rmw_serialize` and `rmw_deserialize` take the dispatch handle and a constructed instance, which `init_function` provides. Two serialisations of equal messages can differ in padding, because Fast CDR skips alignment padding without writing it, so compare decoded values and not bytes. Humble has `rclcpp::GenericPublisher` and `rclcpp::GenericSubscription`, with public constructors that take the typesupport library of the package of the type, and the bridge adds what it creates to the node through the topics interface. A middleware without QoS events refuses the event callbacks in the constructor, and the bridge then creates the subscription again without them.

### Services and actions without generic clients

Humble has no `rclcpp::GenericClient`, and no `rclcpp_action::GenericClient`; later releases have both. The executor drives any `rclcpp::ClientBase` through three virtual functions, `create_response`, `create_request_header` and `handle_response`, and knows nothing of the C++ type, so `DynamicServiceClient` is a `ClientBase` over a typesupport that is obtained at run time. The sequence number that `rcl_send_request` returns is the one that `handle_response` sees, which is how a response is matched to its call. A request is sent only when `service_is_ready` says that a server is there, so a call may have to wait until one appears, and it ends with a timeout when none does.

`rclcpp_action::ClientBase` is a waitable of the node, and its constructor takes the action typesupport as a run-time pointer, so `DynamicActionClient` derives from it and uses its protected senders for goal, result and cancel requests. The typesupport of an action holds five pointers, those of the goal, result and cancel services and of the feedback and status messages, and it comes from the dispatch library. The cancel and status types are fixed by the protocol, so they are compile-time types of `action_msgs`, and the messages that belong to the action itself are dynamic messages.

**Feedback can arrive before the goal response.** In Humble `take_data` of the client base drains the feedback subscription before the goal client, and a server that starts to execute at once publishes feedback within microseconds of accepting a goal. The typed client ignores feedback for a goal that it does not know. The bridge keeps it, because the goal id is its own.

### The graph

The graph of a node carries only the protocol entities of an action, a service for the goal, one for the result and one for the cancel request, and the topics for feedback and status, all with the hidden token `_action` in their names. `recover_actions` folds the goal service and the feedback topic back into the name and type of the action, and a live test checks that `rcl_action_get_names_and_types` reports the same list. The calls that list topics and services return the hidden names too, because hiding is a rule of ROS names that the command line tools apply, and `is_hidden_name` reproduces it.

### Building against the distribution from plain CMake

The files of the distribution are ordinary CMake, with two exceptions that need the environment of the configure step. The configuration of `ament_cmake_core` runs a Python script that imports `ament_package`, so the Python that `find_package(Python3)` picks must find that module, and the distribution holds it in `Lib\site-packages`. The typesupport of `rosidl_typesupport_c` reads the ament index from `AMENT_PREFIX_PATH`. The build support sets both for the configure process only, and [Troubleshooting](TROUBLESHOOTING.md#cmake-cannot-find-ros-2-packages) shows the errors that appear when they are missing.

The one difference that decides the design of the clients is the absence of the generic client headers. The command prints whether three headers of `rclcpp` exist on the distribution that `ROS2_INSTALL_ROOT` names.

```powershell
foreach ($name in 'generic_client', 'generic_publisher', 'generic_subscription') {
    '{0} {1}' -f $name, (Test-Path -LiteralPath (Join-Path $env:ROS2_INSTALL_ROOT "include\rclcpp\rclcpp\$name.hpp"))
}
```
```text
generic_client False
generic_publisher True
generic_subscription True
```

On Humble the first line ends with `False` and the other two with `True`. A distribution that prints `True` for `generic_client` has a generic client, and the dynamic service client of this library can then be replaced by it.
