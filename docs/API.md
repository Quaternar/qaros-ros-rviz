# API reference

The plugin is a panel for RViz and not a library, so this page lists the surface that an operator, a ROS graph and a contributor can see: the class that RViz loads, the ROS interfaces that the plugin publishes and uses, the actions and documents of the pages that it generates, where each feature lives in the source, and how to add a layout for a ROS type. [Architecture](ARCHITECTURE.md) explains how the parts work together, and [Configuration](CONFIGURATION.md) lists the settings.

## The plugin's public surface

The plugin offers one class to RViz and no C++ API to integrators. RViz loads the class by its identifier through pluginlib and does not link it: the class is declared without an export macro, and the headers that the package installs serve the units of the plugin among themselves. A program that wants QarOS in a scene of its own uses the libraries of qar_ogre_adapter and qar_qt_streaming, which have API documents of their own, and a program that wants a panel of its own on a web page uses the three libraries of this repository, which [Header map](#header-map) and the sections after it document.

| Item | Where | What it is |
|---|---|---|
| `qar_rviz_streaming/QarStreamingPanel` | `plugin_description.xml` | the class identifier of the panel; the class is `qar_rviz_streaming::QarStreamingPanel` and its base class is `rviz_common::Panel` |
| `qar_rviz_streaming.dll` | `bin` below the prefix | the library that the manifest names |
| `plugin_description.xml` | `share\qar_rviz_streaming` below the prefix | the manifest, which is registered for `rviz_common` in the resource index of ament under the resource type `rviz_common__pluginlib__plugin` |
| `config` | `share\qar_rviz_streaming` below the prefix | the configuration `qar_streaming_dev.rviz` and the folder `qar_rviz_streaming/config/test_scenes` of the source tree, which holds five scenes |
| `include` | the prefix | the headers of the units |

The manifest describes the panel as the control panel that connects to the QarOS runtime, invites peers, streams RViz docks and ROS topics as AR GUI panels, picks points and poses in the AR scene and shows live render diagnostics, and it says that there is no UI for the authorization of a peer: the identifier of a peer only identifies and routes a stream. The entry that loads the panel from a configuration of RViz is these two lines. They hold the class and the title, and nothing of the state of the panel: every setting is an environment variable or a control of the panel.

```yaml
# Qar Streaming panel provided by qar_rviz_streaming.
- Class: qar_rviz_streaming/QarStreamingPanel
  Name: Qar Streaming
```

## ROS interfaces

The plugin is a client of the ROS graph, and it publishes only what the operator asks for. It uses the node of RViz for the discovery of the graph, for the bridges of the tools and for the geolocation, and it uses a node of its own, in a private context on the same DDS domain, for the video. The names that the node of the video gets are `qar_video_sources_<N>`, with a number that makes them unique.

### What the plugin publishes

| Topic | Type | Published when |
|---|---|---|
| `/clicked_point` | `geometry_msgs/msg/PointStamped` | a pick in the mode `"Pick a point -> /clicked_point"` is confirmed |
| `/goal_pose` | `geometry_msgs/msg/PoseStamped` | a navigation goal is confirmed |
| `/initialpose` | `geometry_msgs/msg/PoseWithCovarianceStamped` | an initial pose is confirmed |
| `/qar/selected_area` | `geometry_msgs/msg/PolygonStamped` | a rectangle, a circle or a polygon is confirmed |
| the topic of an AR tool | the type of that topic | `"Publish"` is pressed on a tool that was exposed with permission to publish |

The first four are the defaults of the tools Publish Point, 2D Goal Pose and 2D Pose Estimate of RViz, so that the subscribers of those tools work unchanged, and the headset is one more producer on topics that exist. The mapping from a pick to a message is a pure function of the target, the declared type and the pick, and it builds the message as a `UiValue` that has the shape of the fields of the type, which `TopicPublisher::publish()` of `qar_ros_ui` converts, serialises and publishes, so that no message header is compiled in.

- **Frame and time.** The header carries the frame of the field `Frame`, `map` at the start, and the time stamp zero, which tf resolves as the latest transform. The plugin only labels the pick: the numbers are the point of the scene of QarOS with the axes changed from the room to ROS, and nothing looks up a transform into the frame that you name, so that you name the frame in which the origin of your scene is right. A stamped type without a frame is refused.
- **Navigation goal.** A goal carries the frame that the operator chose when the goal was made, and that frame wins over the default of the publisher, so that a goal made in `odom` is not relabelled. The goal is flattened to the floor by the pick model of `qar_ar_core`.
- **Initial pose.** The covariance is a matrix of 6 by 6 in row-major order that is filled on the diagonal only, x at index 0, y at 7 and the yaw at 35, with 0.25 for x and y and the square of 15 degrees for the yaw, the numbers of the tool of RViz, which AMCL reads as the trust in the guess.
- **Regions.** A rectangle publishes its four corners, a polygon its vertices, and a circle a polygon of 32 segments; a region needs at least three points.
- **Other topics.** The controller can be given another topic and type for one mode, and a custom target is refused until both are named, because the plugin never publishes somewhere by default. The shape of the message follows the declared type: a point becomes a `PointStamped` or a `Point`, a pose a `PoseStamped`, a `PoseWithCovarianceStamped` or a `Pose`, and a region a `PolygonStamped` or a `Polygon`.

The page of an AR tool can also publish, call and send goals on the entity that it was generated for, and only on that one; [Web panel actions and documents](#web-panel-actions-and-documents) lists the actions. A call of a service goes through `ServiceBridge::call` and a goal through `ActionBridge::send_goal`, and both answer through a callback and never wait.

### What the plugin subscribes to

| Topic | Type | Why | Quality of service |
|---|---|---|---|
| `QAR_PANEL_GPS_TOPIC`, `/gps/fix` by default | `sensor_msgs/msg/NavSatFix` | the geolocation of the app volume | `rclcpp::SensorDataQoS()`, best effort |
| every topic that is exposed as an AR tool | the type of the topic | the page of the tool | adaptive, decided from the publishers that exist |
| the topic of an Image or CompressedImage tool, and of an Image or Camera display of a streamed dock | `sensor_msgs/msg/Image` or `sensor_msgs/msg/CompressedImage` | the video of an AR panel | adaptive with the depth 2, decided from the graph of the node of RViz |

The geolocation callback ignores a fix whose status says that there is none, and it only stores the fix; the render tick re-anchors the app volume when the fix has moved by more than 2 m, so that a receiver that publishes at 10 Hz does not cause a call of QarOS for each message. The altitude is the height above the WGS 84 ellipsoid, which is what the message carries, and not the height above the sea. The adaptive quality of service of the tools and of the video chooses RELIABLE when every publisher of the topic offers it and BEST_EFFORT as soon as one does not, and the plugin decides it again about once a second for the tools, because publishers come and go. The type of an image topic is decided from what the graph advertises: a compressed image wins, then a raw image, and without an advertisement the suffix `/compressed` of the name decides.

## Web panel actions and documents

An AR tool is a page whose widgets carry an action and a binding. The action ids are a small closed vocabulary, so that the router can be a pure function of the model, the schema and the event, and so that a template only decides the layout.

| Action id | Applies to | What it does |
|---|---|---|
| `ros.set` | every entity | stages one field of the outbound message in the document |
| `ros.publish` | a topic | publishes the staged `draft` to the topic |
| `ros.call` | a service | calls the service with the staged `request` |
| `ros.send_goal` | an action | sends the staged `goal` |
| `ros.cancel_goal` | an action | cancels the goal in flight |
| `ar.open_video` | a topic of the type `sensor_msgs/msg/Image` or `sensor_msgs/msg/CompressedImage` | opens the topic as its own AR video panel; it is the one action of the host side, and it performs no ROS operation |

The panel is bound to one data document, so that the request and the response of a service, and the goal, the feedback, the result and the status of an action, are sections of one synthetic root. A control may write only the sections `draft`, `request` and `goal`, and everything else is what ROS said.

| Entity | Sections | Writable |
|---|---|---|
| topic | `msg`, the latest received message; and `draft`, the message being composed, when the tool was exposed with permission to publish | `draft` |
| service | `request` and `response` | `request` |
| action | `goal`, `feedback` and `result`, and `status`, the goal status as text | `goal` |

The generator caps what it lays out: at most 48 fields and a depth of 4, so that a message of 900 fields does not become a page of 900 widgets in a headset, and the host shortens every array of more than 256 elements before it pushes a message to a page. A widget id becomes an identifier on the wire, and a name of ROS starts with a slash, which an identifier of the model cannot do: every character outside the allowed set becomes an underscore and the kind of the entity is put in front, so that `/turtle1/cmd_vel` is `topic._turtle1_cmd_vel` and a topic and a service of one name cannot collide; the id is cut to the limit of 128 bytes. The frames between the page and the host are those of the protocol qar-ui/1, which [Wire protocol](WIRE_PROTOCOL.md) specifies.

The router decides what a control event means, and it answers every event with an acknowledgement that carries the outcome. It refuses an unknown widget, a widget whose declared action contradicts the action of the event, an unknown action, an action that does not exist for the kind of the entity, a write to a section that is read-only, a binding that does not resolve, and any value that the schema does not accept, each with a reason that names what to change. The action of a widget wins over the action of the event, so that a page cannot turn a widget that sets a field into a widget that publishes by relabelling it. A staged value is validated on a copy of the document, so that a rejected value leaves the staged message as it was, and a button may carry a preset message, such as the all-zero message of a stop button, which then is the message that is published.

## Module guide for contributors

Every file is in `qar_rviz_streaming`, headers in `include\qar_rviz_streaming` and sources in `src`. The pure units compile into the test target of the plain-CMake project, and the others build only under colcon ([Build gates](DEVELOPING.md#build-gates)).

| File | What it holds | Tests |
|---|---|---|
| `qar_streaming_panel.cpp` | `buildUi`, `onInitialize`, the connect (`onStartQar`, `pollPendingConnect`, `finishLiveConnect`), `inviteSelectedPeer`, the render tick (`onRenderTick`, `driveLiveStreaming`, `applyTickPeriodMs`), the heartbeat (`emitHeartbeat`), the panels (`ensurePanelManager`, `onStreamSelectedPanel`, `closeStreamedPanel`, `tickArPanels`), the tools (`exposeEntity`, `openArPanelFor`, `pushArToolData`, `on_control`), the video (`acquireFrameSource`, `openVideoPanel`, `attachVideoRegions`), the spatial path (`startSpatialGestureSource`, `tickSpatialInteraction`, `publishSpatialPick`) and the geolocation | none: it builds only under colcon, and the scripted modes of the preview exercise it |
| `rviz_display_context_adapter.cpp`, `rviz_render_lock.hpp`, `rviz_render_thread_executor.cpp` | the seam to RViz | none |
| `hidden_rviz_panel.cpp`, `rtti_guard.hpp` | the second Displays panel and the type check | `rtti_guard_tests.cpp` |
| `panel_display_names.hpp` | the title under which a new QarOS panel is created | `panel_display_names_tests.cpp` |
| `grid_floor_plane.cpp`, `grid_floor_plane_geometry.cpp` | the floor plane and its placement math | `grid_floor_plane_geometry_tests.cpp` |
| `windows_scheduling.cpp` | the scheduling opt-out | `windows_scheduling_tests.cpp` |
| `ros_image_frame_source.cpp`, `video_source_executor.cpp`, `image_topic_transport.cpp` | the video | `image_topic_transport_tests.cpp`, `video_source_executor_tests.cpp`, `ros_image_frame_source_timing_tests.cpp` |
| `display_class_catalog.cpp`, `ros_topic_tree.cpp`, `ros_graph_tree.cpp`, `add_topic_dialog.cpp` | the Add dialog | `display_class_catalog_tests.cpp`, `ros_topic_tree_tests.cpp`, `ros_graph_tree_tests.cpp` |
| `ar_ros_entity.cpp`, `ar_exposure_registry.cpp` | what an AR tool points at, and the list of exposures | `ar_exposure_registry_tests.cpp` |
| `ar_panel_generator.cpp` | the action ids, the document sections, the registry of templates and the generic form | `ar_panel_generator_tests.cpp` |
| `ar_semantic_templates.cpp` | the 17 semantic layouts and the shortening of arrays | `ar_semantic_templates_tests.cpp`, `ar_semantic_templates_ros_tests.cpp` |
| `ar_control_router.cpp` | the meaning of a control event | `ar_control_router_tests.cpp` |
| `spatial_interaction.cpp`, `spatial_publish.cpp` | the armed mode and the targets, and the message of a pick | `spatial_interaction_tests.cpp`, `spatial_publish_tests.cpp` |
| `qar_offscreen_capture.cpp`, `rviz_peer_panel_model.cpp`, `mock_sender_factory.hpp` | the capture files and the offline preview | none |
| `panel_settings.cpp` | the twelve settings of the panel and the rule that decides between a variable, the saved value and the default | `panel_settings_tests.cpp` |
| `panel_ground_rule.cpp` | the ground rule, the fixed poses, the frame of the AR panels, the size of the app volume, and the brightness field and the render device line of the log | `panel_ground_rule_tests.cpp` |
| `ar_control_panel_model.cpp`, `ar_control_panel_router.cpp`, `ar_control_panel_state.cpp`, `ar_control_panel_placement.cpp` | the page of the AR control panel, the meaning of its commands, its state as patches, and its place for each peer | `ar_control_panel_model_tests.cpp`, `ar_control_panel_router_tests.cpp`, `ar_control_panel_state_tests.cpp`, `ar_control_panel_placement_tests.cpp` |
| `stable_panel_layout.cpp` | the scroll pages, the shortened labels, the size hints, the fit of a dock and the readout box of the tabbed layout | `stable_panel_layout_tests.cpp`, in the widget tests |

The public methods of the panel that a host could call are few: `onInitialize()`, which RViz calls, `submitSpatialSample()`, the one entry point of a pointer sample for the spatial picker, which any thread may call because the sample is queued and drained on the GUI thread, `spatialInteraction()`, which returns the controller, and `on_control()`, which the session of a page calls.

## Adding a semantic template

A semantic template lays out the widgets of the page for a family of ROS types, and the generic form lays out any type field by field and has no opinion. The panel never builds a widget tree itself: it fills a request and hands it to the registry of templates, which picks the template that scores best.

1. **The interface.** A template implements `IPanelTemplate`: `name()`, `match()` and `build()`. `match()` returns how well the template fits the request, and a value of 0 or less means that it does not apply. A semantic template returns `kSemanticScore`, which is 100, for the types that it claims and 0 for every other type, the generic form returns 1, and a tie goes to the template that was registered first, so that the generic form can never win against a real template.
2. **The layout.** Write a layout function in `ar_semantic_templates.cpp` that adds widgets to the panel with the helpers of that file, `add_scalar`, `add_status`, `add_gauge`, `add_chart`, `add_vector3`, `add_quaternion`, `add_slider`, `add_table`, `add_inspector`, `add_note`, `add_header` and `add_submit`, and returns whether it laid out anything. A layout is built for each section of the document, `msg` or `draft`, `request` and `response`, `goal`, `feedback` and `result`, by the function `sectioned` or by hand.
3. **The catalog entry.** Add an entry to the catalog with the name of the template, the canonical type names that it claims in the form `pkg/msg/Type`, the score, a one-line summary, and the layout function. The matching normalises the type name of the request first, so that the old two-part form of a name still matches. Registration puts every entry in the registry ahead of the generic form, and the panel does it when it is constructed.
4. **The rules.** Three rules hold for every template, and the tests check them. Layout only: the action ids and the document sections are fixed by the generator, and a template chooses widgets and their places, never invents an action and never writes outside `draft`, `request` and `goal`. Probe, never assume: every widget is emitted only after its binding has been resolved against the schema of the document, so that a field that a distribution renamed or dropped costs one missing widget and not a refused panel, and a type whose schema no probe recognises falls back to the generic form. Never stream a bulk payload: no template binds `Image.data`, `PointCloud2.data`, `OccupancyGrid.data` or `LaserScan.ranges`, and the panels show dimensions, encoding, resolution and frame instead.
5. **The tests.** `ar_semantic_templates_tests.cpp` checks each layout against schemas that are written by hand and checks the catalog, among other things that every type is claimed by exactly one template and that the generic form stays last. `ar_semantic_templates_ros_tests.cpp` checks every claimed type against the description that `qar_ros_ui` reads from the interface packages that are installed: that it generates a valid panel, that the probes find the real fields and do not silently fall back, that no panel binds a bulk payload, and that every page renders. It is a separate executable because the first one has to stay free of ROS, and it is skipped when the distribution is not found.
6. **The documents.** Add the template to the table of [The templates](OPERATOR_GUIDE.md#ar-tools-from-the-ros-graph) of the operator guide, and keep the number of layouts in the documents in step with the catalog.

[Developing](DEVELOPING.md#adding-a-semantic-template) has the checks that a change has to pass.

## Header map

Every public header of the repository, by module. The sections below describe the types.

**`qar_ui_model`**: the UI model, the renderer and the codec of the frames, with nothing but the C++ standard library.

| Header | What it declares |
|---|---|
| `ui_value.hpp` | `UiValue`, `UiValueKind`, `UiObjectEntry`: the value tree |
| `json.hpp` | `json_write`, `json_parse`, `JsonWriteOptions`, `JsonParseOptions`, `JsonParseError`, `json_format_double`, `json_format_int`, `json_escape_string_to` |
| `unicode.hpp` | the namespace `utf8`: `is_valid`, `sanitize`, `encode`, `decode_next` |
| `html_escape.hpp` | `html_escape`, `html_escape_to` |
| `type_description.hpp` | the schema: `FieldKind`, `FieldDescription`, `MessageDescription`, `ServiceDescription`, `ActionDescription`, `MessageDescriptionBuilder`, `FieldPath`, `resolve_field_path`, `walk_fields`, `get_path`, `set_path`, `make_default_value`, `validate_value` |
| `ui_model.hpp` | `UiModel`, `Widget`, `WidgetType`, the factories of the namespace `widgets`, `StatusLevel`, `validate`, `UiProblem`, and `to_value` and `from_value` of a widget and a model |
| `ar_theme.hpp` | `ArTheme`, `render_theme_css`, `theme_pixel_variables` |
| `html_renderer.hpp` | `render_html_page`, `HtmlRenderOptions`, `render_widget_fragment`, `runtime_javascript` |
| `ui_protocol.hpp` | the frames: `MessageType`, `ProtocolMessage`, the bodies, the `make_*` builders, `encode_message`, `decode_message`, `apply_patch` |
| `page_metrics.hpp` | `measure_page`, `PageMetrics`, `measure_widget_height` |
| `scroll_paging.hpp` | `plan_paging`, `PagingPlan`, `page_up_target`, `page_down_target`, `pager_rail_width_px`, `pager_content_reserve_px` |
| `viewport_fit.hpp` | `fit_page_to_viewport`, `ViewportFit`, `ViewportFitOptions`, `natural_page_widths`, `columns_for_viewport`, `theme_scale_for`, `quantise_theme_scale` |

**`qar_web_panels`**: the server of the pages and the size of a URL panel; it needs Qt and the module `qar_ar_core`.

| Header | What it declares |
|---|---|
| `web_panel_server.hpp` | `WebPanelServer`, `WebPanelServerConfig`, `WebPanelServerDiagnostics` |
| `panel_session.hpp` | `PanelSession`, `IPanelControlSink`, `PanelControlEvent`, `PanelViewportReport`, `PanelSessionLimits`, `PanelSessionDiagnostics`, `PanelResource` |
| `url_panel_sizing.hpp` | `UrlPanelSizing`, `UrlPanelSizeResult`, `size_url_panel`, `size_url_panel_for`, `min_theme_scale`, `meters_per_css_px`, `apply_sizing_overrides` |
| `http_server.hpp` | `HttpServer`, `HttpServerOptions`, `HttpServerDiagnostics` |
| `http_message.hpp` | `parse_http_request`, `HttpRequest`, `HttpResponse`, `serialize_http_response`, `decode_request_target`, `http_date` |
| `panel_token.hpp` | `generate_panel_token`, `is_well_formed_token`, `tokens_equal` |

**`qar_ros_ui`**: the ROS 2 graph as values; it needs the ROS 2 client libraries.

| Header | What it declares |
|---|---|
| `qar_ros_ui.hpp` | the umbrella header that includes all the others except `detail/` |
| `types.hpp` | `Status`, `Result`, `RosUiDiagnosticsSink`, `ConversionLimits`, `ConversionOptions`, `ConversionReport` |
| `node_handles.hpp` | `NodeHandles`, with its member `callback_group` |
| `graph_discovery.hpp` | `GraphDiscovery`, `GraphSnapshot`, `GraphEntity`, `GraphDelta`, `take_graph_snapshot`, `recover_actions` |
| `type_support_registry.hpp` | `TypeSupportRegistry`, `MessageTypeSupport`, `ServiceTypeSupport`, `ActionTypeSupport` |
| `introspection.hpp` | `introspect_message`, `introspect_service`, `introspect_action`, `describe_message` |
| `dynamic_message.hpp` | `DynamicMessage`, `message_to_ui_value`, `ui_value_to_message`, `utf16_to_utf8` |
| `qos_selection.hpp` | `QosPreset`, `choose_subscription_profile`, `choose_publisher_profile`, `describe_profile` |
| `topic_bridge.hpp` | `TopicBridge`, `TopicSubscription`, `TopicPublisher` and their options and diagnostics |
| `service_bridge.hpp` | `ServiceBridge`, `ServiceCallOptions`, `ServiceCallOutcome` |
| `action_bridge.hpp` | `ActionBridge`, `ActionGoalHandle`, `ActionGoalOptions`, `GoalEvent`, `GoalStatus` |
| `qar_ros_ui/detail/dynamic_service_client.hpp`, `qar_ros_ui/detail/dynamic_action_client.hpp` | `DynamicServiceClient` and `DynamicActionClient`: internal to the bridges and installed for the tests |

## qar_ui_model

### Values and JSON

`UiValue` is the one shape that data has on every leg of the trip between a ROS message, the page and the model: null, bool, integer, double, string, array and object, which is the data model of JSON. An integer and a double stay different kinds, because a ROS `int64` must not pass through a double, and an object keeps the order of its keys, because the order of the fields of a message is meaningful. An unsigned value above the largest `int64` becomes a double. `as_bool`, `as_int`, `as_double` and `as_string` return the fallback that you pass when the kind does not match, and numbers convert between the two kinds when the value fits. `push_back` appends to an array and `set` inserts or replaces a key; both turn a null into an array or an object first and leave any other kind alone.

```cpp
ui::UiValue pose = ui::UiValue::object();                  // objects keep the order of their keys
pose.set("frame", "map").set("x", 1.5).set("seq", 7);      // a String, a Double and an Int: the kinds stay apart
const ui::UiValue * x = pose.find("x");                    // nullptr when the key is absent
const double meters = x != nullptr ? x->as_double(0.0) : 0.0;   // the fallback is used when the kind does not match

const std::string text = ui::json_write(pose);             // {"frame":"map","x":1.5,"seq":7}
ui::UiValue back;
ui::JsonParseError error;
if (!ui::json_parse(text, back, error)) {
  std::printf("%s\n", error.to_string().c_str());          // the message, with the line and the column
}
```

`json_write` writes compact JSON, or indented JSON with `JsonWriteOptions::indent`, and with `html_safe` it also escapes the characters that could end a script element. `json_parse` is strict: it refuses trailing commas, comments, single quotes, leading zeros, NaN and Infinity, unescaped control characters, invalid escapes, lone surrogates and ill-formed UTF-8, and it names the line and the column of the error. It skips a leading byte order mark, takes the last of duplicate keys, as `JSON.parse` does, and refuses nesting deeper than `JsonParseOptions::max_depth`, 256 by default. `utf8::is_valid` and `utf8::sanitize` check and repair UTF-8, and `html_escape` is the one function through which an untrusted string reaches markup.

### The widget model

`UiModel` is the root `Widget`, the initial data that the page shows before the first frame, and free-form metadata. A `Widget` has an id, a type, a label, a description, a binding, two optional bindings for its caption and its visibility, a unit, a range and a step, a precision, an action and the options of its type. The factories of the namespace `widgets` give each type its defaults, inputs starting editable, and the chainable setters, such as `with_unit`, `with_range`, `with_action` and `with_confirm`, return the widget. `with_label_binding` takes the caption of a widget, or the text of a button, from the data document once its path holds a string, and `with_visible_binding` shows the widget only while its path holds `true` and starts it hidden; their JSON keys are `labelBind` and `showBind`, and the validation refuses a path that is not a dotted path. The containers are `Panel`, `Section`, `Row` and `Column`, and the leaves are `Label`, `ValueDisplay`, `NumberInput`, `TextInput`, `Toggle`, `Slider`, `Choice`, `Button`, `Vector3`, `Quaternion`, `Table`, `Gauge`, `StatusLight`, `TimeSeries`, `Image` and `JsonInspector`. An id has the form `[A-Za-z_][A-Za-z0-9_.-]*` with at most 128 bytes, because it becomes a DOM id and a wire identifier. `walk_widgets` visits a tree depth first, `UiModel::find` finds a widget by id, and `to_value` and `from_value` write and read the JSON that the page embeds, in which only the members that differ from the default are written.

```cpp
ui::UiModel model;
model.root = ui::widgets::panel("battery_panel", "Battery");        // the root, and the only Panel of the tree
ui::Widget status = ui::widgets::section("status", "Status");
status.add(ui::widgets::value("percent", "battery.percentage", "Charge").with_unit("%").with_precision(0));
status.add(ui::widgets::gauge("level", "battery.percentage", "Level", 0.0, 1.0));
model.root.add(std::move(status));
model.root.add(ui::widgets::slider("limit", "limit.percent", "Limit", 0.0, 100.0).with_action("set_limit"));
model.root.add(ui::widgets::button("stop", "Stop", "stop").with_confirm());   // a second press within 4 s fires it
model.initial_data = ui::UiValue::object()                                    // what the page shows before any frame
  .set("battery", ui::UiValue::object().set("percentage", 0.8))
  .set("limit", ui::UiValue::object().set("percent", 20.0));
```

### The schema

A schema describes a message, a service or an action without naming ROS. A `FieldDescription` has a name, a `FieldKind`, one for each type id of rosidl, so that an introspected member always has a home, the name of the element type, an `ArrayKind`, which is `Fixed` for `T[N]`, `Bounded` for `T[<=N]` and `Unbounded` for `T[]`, the bound of a string, the nested description, the IDL default, and optional metadata for the panel: a label, a unit, a description and a range and step. `MessageDescription` holds the fields in declaration order and the constants; `ServiceDescription` has a request and a response, and `ActionDescription` a goal, a result and a feedback. A nested description is a `std::shared_ptr<const MessageDescription>`, shared by every message that embeds it and immutable once built. `MessageDescriptionBuilder` builds one readably, and `validate_description` finds structural problems, such as a message field without a type, a duplicate name and a fixed array of size 0.

```cpp
ui::MessageDescriptionBuilder position("geometry_msgs/msg/Point");
position.field(ui::FieldDescription::primitive("x", ui::FieldKind::Float64).with_unit("m"))
  .field(ui::FieldDescription::primitive("y", ui::FieldKind::Float64).with_unit("m"))
  .field(ui::FieldDescription::primitive("z", ui::FieldKind::Float64).with_unit("m"));
const ui::MessageDescriptionPtr point = position.build();     // immutable, and shared by every message that embeds it

ui::MessageDescriptionBuilder battery("sensor_msgs/msg/BatteryState");
battery.description("State of a battery")
  .field(ui::FieldDescription::primitive("percentage", ui::FieldKind::Float32).with_range(0.0, 1.0))
  .field(ui::FieldDescription::primitive("present", ui::FieldKind::Bool))
  .field(ui::FieldDescription::primitive("cell_voltage", ui::FieldKind::Float32).unbounded_array().with_unit("V"))
  .field(ui::FieldDescription::primitive("serial", ui::FieldKind::String).string_bound(16))   // string<=16
  .field(ui::FieldDescription::nested("position", point))
  .constant("POWER_SUPPLY_STATUS_CHARGING", ui::FieldKind::UInt8, 1);
```

The grammar of a path is that of [Wire protocol](WIRE_PROTOCOL.md#paths). `resolve_field_path` says what a path names in a schema: an array has to be indexed before it is entered, so `points.x` is an error and `points[2].x` is not, an index on a non-array is an error, an index at or beyond a fixed size or a bounded maximum is an error, and the empty path is an error, because it names the root. `walk_fields` visits every field depth first in declaration order, descends into nested messages that are not arrays, and lets the visitor answer `Continue`, `SkipChildren` or `Stop`. `get_path` reads a value of a document, `set_path` writes one and creates what is missing, `make_default_value` makes a document with every field at its default, and `validate_value` checks a value against a schema: kinds, integer ranges, array lengths, string bounds, nested messages and, unless `ValidateValueOptions::reject_unknown_fields` is off, unknown keys. `split_type_name` and `normalize_type_name` read a type name, `pkg/Name` meaning `pkg/msg/Name`, and `humanize_identifier` turns `linear_velocity` into a label.

```cpp
ui::ResolvedField resolved;
std::string error;
if (!ui::resolve_field_path(schema, "cell_voltage[2]", resolved, error)) {   // an array has to be indexed before it is entered
  std::printf("%s\n", error.c_str());
}

ui::UiValue document = ui::make_default_value(schema);   // every field at its default: the IDL default, else zero
ui::FieldPath path;
if (ui::FieldPath::parse("position.x", path, error)) {
  ui::set_path(document, path, 1.5, error);              // creates what is missing; a kind clash fails and changes nothing
}
for (const ui::ValueProblem & problem : ui::validate_value(schema, document)) {   // kinds, ranges, lengths, unknown keys
  std::printf("%s: %s\n", problem.path.c_str(), problem.message.c_str());
}
```

### Rendering a page

`render_html_page` is explained in [Page generation](ARCHITECTURE.md#page-generation). `HtmlRenderOptions` holds the theme, the address of the socket, the title, the language, the three timings of the script, and two switches: `include_runtime` false gives a static page for debugging or printing, and `include_csp` false leaves out the policy. `ArTheme` holds the sizes and the colours of the page, with `dark()`, `light()`, `scaled(factor)` and `validate`: a colour must be `#rrggbb`, a size must be positive and the font list must hold no character of CSS syntax, because the strings of the theme are written into the style sheet as they are. `render_widget_fragment` returns the markup of one widget subtree exactly as it is in the page, and `runtime_javascript` returns the script.

```cpp
const std::vector<ui::UiProblem> found = ui::validate(model, &schema);   // without a schema: the syntax only
std::printf("%zu problem(s), valid: %s\n", found.size(), ui::is_valid(found) ? "yes" : "no");

ui::HtmlRenderOptions options;
options.theme = ui::ArTheme::dark().scaled(1.0);   // every size in pixels, times the factor
options.websocket_url = "";                        // empty: the page derives the address from its own location
options.reconnect_ms = 2000;                       // the timings that the page script reads
options.ping_interval_ms = 5000;
options.stale_after_ms = 5000;

std::string html;
std::vector<ui::UiProblem> problems;
if (!ui::render_html_page(model, options, html, problems)) {   // false: an Error in the model or the theme, html is empty
  for (const ui::UiProblem & problem : problems) {
    std::printf("%s %s: %s\n", ui::to_string(problem.severity), problem.widget_id.c_str(), problem.message.c_str());
  }
}
return html;
```

### Frames

The frames of [Wire protocol](WIRE_PROTOCOL.md) are the types of `ui_protocol.hpp`. A `ProtocolMessage` holds the version, the type, the counter, an optional time stamp and a variant of the bodies `HelloBody`, `DataBody`, `ControlBody`, `StatusBody`, `AckBody`, `PingBody`, `PongBody` and `ViewportBody`, and `as<T>()` returns a body or null. `make_hello`, `make_data`, `make_data_replace`, `make_control`, `make_status`, `make_ack`, `make_ping`, `make_pong` and `make_viewport` build a frame, `encode_message` writes it as one line of compact JSON, and `decode_message` and `message_from_value` read one. They fail with a reason for a missing or foreign version, a missing member and a body that breaks the schema, and they decode an unknown type as `MessageType::Unknown`, with its name in `type_name`. `apply_patch` applies a data frame to a document, all or nothing.

```cpp
std::vector<ui::PatchEntry> patch;
patch.push_back({"battery.percentage", 0.79});                  // one assignment of a data frame
const ui::ProtocolMessage frame = ui::make_data(42, patch);
const std::string wire = ui::encode_message(frame);             // one line of compact JSON

ui::ProtocolMessage received;
std::string error;
if (ui::decode_message(wire, received, error)) {                // an unknown type decodes as MessageType::Unknown
  if (const ui::DataBody * data = received.as<ui::DataBody>()) {
    if (!ui::apply_patch(document, *data, error)) {             // all or nothing: a failure leaves the document as it was
      std::printf("%s\n", error.c_str());
    }
  }
}
```

### Sizes of a page

Three groups of pure functions do the arithmetic of [Panel sizing](ARCHITECTURE.md#panel-sizing). `measure_page(model, theme, max_columns)` returns a `PageMetrics`: the width and the height that the page wants in CSS pixels, the aspect, the number of leaves and of containers, the widest row and the columns that the measure assumed; it never fails, because a caller needs a number more than an error. `fit_page_to_viewport(model, base, viewport_css_width, options)` returns a `ViewportFit`: the scale and the effective scale, the columns, the flags `shrunk`, `clamped` and `measured`, the fitted theme with its metrics, and a note for a log; with a width of 0 or less it is the identity. `plan_paging(viewport, content, scroll)` returns a `PagingPlan`: whether the pager is needed, the pages, the current page, the step in pixels and the text of the readout; for input that cannot be measured it degrades to one page. `page_up_target` and `page_down_target` give the offset that a click lands on. The script of the page mirrors these functions, so that the server and the page decide the same.

## qar_web_panels

### The server

`WebPanelServer` is a `QObject` that one host owns, on the thread of its Qt event loop. [Configuration](CONFIGURATION.md#runtime-option-structs) lists the fields of `WebPanelServerConfig` and their defaults.

| Call | What it does |
|---|---|
| `start(error)` | starts to listen on the bind address and the port; returns false with the reason when it cannot |
| `stop()` | stops listening and closes every viewer with 1001; the sessions and their tokens survive, so `start` serves them again |
| `add_panel(model, error)` | renders the page once to validate the model, makes a token and a `PanelSession`, and returns it; returns null, with the first problem, for a model that does not render |
| `remove_panel(token)` | forgets the token and then closes the viewers; false for an unknown token |
| `find_panel(token)`, `panels()` | the session of a token, and every session |
| `page_url(session)` | `http://<advertised host>:<port>/p/<token>/`, what a QarOS URL panel loads |
| `websocket_url(session, host_and_port)` | `ws://<host and port>/ws/<token>`, what the served page dials |
| `port()`, `advertised_host()`, `is_running()`, `config()` | queries; `start` resolves the advertised host |
| `candidate_hosts()`, `detect_advertised_host()` | static: the addresses at which the machine could be reached, best first, and the address that it routes through, or 127.0.0.1 when there is none |
| `diagnostics()` | the counters of the HTTP server, the number of panels and viewers, the requests that named no session and the upgrades that were refused |
| the signals `started`, `stopped`, `panel_added`, `panel_removed` | the lifecycle of the server and of its panels |

```cpp
wp::WebPanelServerConfig config;
config.bind_address = QHostAddress::LocalHost;   // the default is every interface
config.port = 0;                                 // 0: the operating system picks a free port
config.advertised_host = "";                     // empty: the bind address, or the address that the machine routes through
config.allow_any_origin = false;                 // refuse a WebSocket handshake of another origin
config.session.max_clients = 16;                 // viewers of one panel
config.http.max_connections = 64;                // connections of the server
config.render.reconnect_ms = 2000;               // the page script dials again after this

wp::WebPanelServer server(config);               // lives on the thread of a Qt event loop
std::string error;
if (!server.start(&error)) {
  return error;                                  // cannot listen on <address>:<port>: <reason>
}
const std::shared_ptr<wp::PanelSession> session = server.add_panel(std::move(model), &error);   // renders the page once
if (!session) {
  return error;                                  // model does not render: the first Error of the model
}
const std::string url = server.page_url(*session);   // http://<host>:<port>/p/<token>/
server.remove_panel(session->token());               // closes its viewers and forgets the token
return url;
```

### The session and the control sink

A `PanelSession` is one served panel: its model, the server's copy of the data document, the token and the viewers. Every call of a session must be made on the thread that owns it. A call from another thread is refused: it changes nothing and is never queued, and `publish` says so in its error string.

| Call | What it does |
|---|---|
| `publish(patch, error)` | applies the patch to the document all or nothing and, when it applied, sends it to every viewer as one data frame |
| `set_value(path, value, error)`, `replace_document(document, error)` | a patch of one entry, and a patch of the empty path, which needs an object |
| `send_status(level, text)` | a status frame to every viewer; an empty text clears the line |
| `ack(event, ok, text)`, `ack(client_id, event_seq, ok, text)` | answers one control event to the viewer that sent it; false when that viewer is gone |
| `set_control_sink(sink)` | the sink is borrowed and has to outlive the session |
| `set_resource(path, content_type, bytes)`, `remove_resource`, `find_resource` | a resource that is served below the address of the page, behind the same token |
| `panel_id()`, `token()`, `model()`, `document()`, `client_count()` | queries; `document()` is the copy of the server |
| `last_viewport()` | the latest `PanelViewportReport` of any viewer, or nothing until a page has reported |
| `diagnostics()` | viewers, frames in and out, frames that were ignored, control events, reports of the viewport, patches that were refused, sockets closed by policy and viewers that did not keep up |
| the signals `control_received`, `viewport_reported`, `client_count_changed` | what the pages do; `control_received` comes after the sink |

A `PanelControlEvent` names the viewer, the counter of the page for that frame, the time stamp of the page and the `ControlBody`, which holds the widget id, the action string, the binding and the value. A `PanelViewportReport` holds the `ViewportBody` and three helpers: `aspect()` is the width over the height of the reported viewport, `visible_fraction()` is how much of the page is inside it, and `describe()` is a line for a log. The session does not apply the value of a control event to its document: the page does that itself, and the next value that the host publishes confirms or corrects it. A host that wants the viewers to see each other's edits calls `set_value` from its sink.

```cpp
// The host's answer to the buttons of a page. It runs on the thread that owns the session.
class CounterSink : public wp::IPanelControlSink
{
public:
  void on_control(wp::PanelSession & session, const wp::PanelControlEvent & event) override
  {
    if (event.control.action != "increment") {
      session.ack(event, false, "unknown action");               // the outcome goes back to the viewer that sent the event
      return;
    }
    std::string error;
    if (!session.set_value("count", ++count_, &error)) {         // the document of the server first, then every viewer
      session.ack(event, false, error);
      return;
    }
    session.ack(event, true);
  }

private:
  std::int64_t count_ = 0;
};
```

```cpp
session.set_control_sink(&sink);                               // borrowed: the sink has to outlive the session
std::string error;
std::vector<ui::PatchEntry> patch;
patch.push_back({"battery", ui::UiValue::object().set("percentage", 0.5)});
patch.push_back({"odom.x", 1.25});
if (!session.publish(std::move(patch), &error)) {              // all or nothing: nothing is sent when it fails
  std::printf("%s\n", error.c_str());
}
session.send_status(ui::StatusLevel::Warning, "no publisher on /odom");   // an empty text clears the line

if (session.last_viewport()) {                                 // empty until a page has reported its viewport
  std::printf("%s\n", session.last_viewport()->describe().c_str());
}
const wp::PanelSessionDiagnostics counters = session.diagnostics();   // viewers, frames, refusals
std::printf("%zu viewer(s), %llu control event(s)\n", counters.clients,
  static_cast<unsigned long long>(counters.controls));
```

A host whose ROS 2 executor runs on another thread connects to the signals with a queued connection; the control event and the viewport report are registered as metatypes for that:

```cpp
// A host whose ROS 2 executor runs on another thread connects with a queued connection: the lambdas then run
// on the thread of `host`, which has to be the thread that owns the session.
QObject::connect(&session, &wp::PanelSession::control_received, host,
  [&session](const wp::PanelControlEvent & event) {session.ack(event, true);}, Qt::QueuedConnection);
QObject::connect(&session, &wp::PanelSession::viewport_reported, host,
  [](const wp::PanelViewportReport & report) {std::printf("%s\n", report.describe().c_str());}, Qt::QueuedConnection);
```

### Sizing a URL panel

`size_url_panel(metrics, theme, sizing)` turns a measured page into the metres of a panel, and `size_url_panel_for(model, theme, sizing)` is the whole chain in one call: it chooses the columns and the theme scale that the page will give itself, measures the page at that scale, and sizes the panel from the reported viewport, or from the measurement while no page has reported. A size that was pinned with `fixed_width_m` and `fixed_height_m` wins over both. `UrlPanelSizing` holds the knobs and the bounds, `use_reported_viewport` copies a report into it, and `min_theme_scale` gives the smallest theme scale that still leaves room for the panel to grow. The result is a `UrlPanelSizeResult`: the `QarGuiPanelSize`, the metres of one CSS pixel that were used, the fraction of the page that is above the fold, whether a bound applied, whether the size came from a reported viewport, the theme scale, the columns, the aspect and a note of one line. [Panel sizing](ARCHITECTURE.md#panel-sizing) explains the rules.

`apply_sizing_overrides(sizing, lookup, note)` applies `QAR_PANEL_WEB_SIZE` and `QAR_PANEL_WEB_SCALE` through a lookup function that returns the value of a variable or null, so that a test can inject the environment, and `process_environment()` is the lookup of the real environment. It returns true when an override applied and puts one line about what it did, or about a value that it ignored, into the note. A pinned size moves the bounds out of its way, so that the pin is not quietly undone. [Configuration](CONFIGURATION.md#environment-variables) has the rules of parsing.

```cpp
wp::UrlPanelSizing sizing;                                  // bounds and knobs; CONFIGURATION lists the defaults
std::string note;
wp::apply_sizing_overrides(sizing, wp::process_environment(), note);   // QAR_PANEL_WEB_SIZE and QAR_PANEL_WEB_SCALE
wp::use_reported_viewport(sizing, report.viewport);                    // the viewport that the page reported

const wp::UrlPanelSizeResult result =
  wp::size_url_panel_for(session.model(), ui::ArTheme::dark(), sizing);   // fit the page, then size the panel
std::printf("%s\n", result.note.c_str());                  // one line: the page, the panel and the bounds that applied
return result.size;                                         // metres, with content_scale 1.0
```

### The parts of the server

`http_message.hpp` is pure: `parse_http_request` answers complete, incomplete or invalid for a buffer, with the status 400, 414, 431 or 505 and a reason for an invalid one; `decode_request_target` splits and decodes a target and refuses a shape that climbs out of a folder, a backslash and a bad escape; `is_acceptable_host_header` says whether a `Host` value is safe to reflect into a page; `serialize_http_response` writes a response; and `http_date` formats a time by hand, so that the locale of the process cannot change the names. `HttpServer` takes a handler for requests and one for upgrades, and `listen`, `close` and `diagnostics` do what they say. `panel_token.hpp` has `generate_panel_token`, `is_well_formed_token`, which is true for exactly 32 characters of `[0-9a-f]`, and `tokens_equal`, which compares in constant time.

## qar_ros_ui

### Results, sinks and node handles

`Status` is one of `Ok`, `InvalidArgument`, `InvalidState`, `NotFound`, `TypeSupportUnavailable`, `ConversionFailed`, `SerializationFailed`, `RclError`, `Timeout`, `Rejected` and `Unsupported`, and a `Result` is a `Status` with a message; it is true when it is `Ok`, and `Result::success()` and `Result::error` make one. A `RosUiDiagnosticsSink` is optional: the bridges tell it, with a severity and a category, which is `topic`, `service` or `action`, what an operator should hear, such as a subscription that was created again or a message that could not be converted. It is called on whichever thread produced the event and has to be cheap and thread-safe. `NodeHandles::from` takes any node, an `rclcpp::Node`, a lifecycle node or a shared pointer to one, and collects the seven interfaces that the bridges need; `valid()` says whether all seven are set. `NodeHandles::callback_group` is the callback group in which the bridges create every entity: null, the default, stands for the default group of the node, and `valid()` does not look at it. A host that can be unloaded while its node lives on passes a group of its own, made on that node, and releases it after the bridges; the bridges and the subscriptions and publishers that they hand out keep a copy of the handles, and with it the group, until they are destroyed. `ConversionLimits` and `ConversionOptions` are the knobs of the conversions, and a `ConversionReport` says what an outbound conversion left out.

### Graph discovery

`take_graph_snapshot` and `GraphDiscovery::refresh` are described in [ROS introspection design](ARCHITECTURE.md#ros-introspection-design). A `GraphSnapshot` holds the topics, the services and the actions, each a `GraphEntity` with a name and the types seen for it, sorted by name; the types are normally one, and a UI has to be told when several publishers disagree. A `GraphDelta` holds what was added, what was removed and what changed its type, and `empty()` says whether nothing changed. `is_hidden_name`, `action_name_from_send_goal_service`, `action_name_from_feedback_topic`, `action_type_from_send_goal_type` and `action_type_from_feedback_type` are pure helpers, and `diff_graph` compares two snapshots.

```cpp
const ros_ui::NodeHandles handles = ros_ui::NodeHandles::from(node);   // any node type: the library never names rclcpp::Node
ros_ui::GraphDiscovery discovery(handles);
ros_ui::GraphDelta delta;
const ros_ui::Result refreshed = discovery.refresh(&delta);          // one pass, which never waits for discovery
if (refreshed.ok() && !delta.empty()) {                              // the revision grows only when something changed
  for (const ros_ui::GraphEntity & topic : discovery.current().topics) {
    std::printf("%s [%s]\n", topic.name.c_str(), topic.primary_type().c_str());
  }
  // An action is recovered from its hidden services and topics.
  const ros_ui::GraphEntity * action = discovery.current().find_action("/fib");
  std::printf("%s\n", action != nullptr ? action->primary_type().c_str() : "no such action");
}
```

### Introspection and dynamic messages

`introspect_message`, `introspect_service` and `introspect_action` take a type name, load the typesupport library through the registry and return the description. `describe_message` does the work for a `MessageMembers` table and caches the result for each table, so that one introspected `std_msgs/msg/Header` is shared by everything that embeds it; `IntrospectionOptions::record_defaults` makes it read the IDL defaults from an instance that it constructs once. A `DynamicMessage` owns an instance without knowing its C++ type: its memory comes from `operator new` and the init function of the table constructs it, with the defaults. `from_ui_value` and `to_ui_value` convert, `serialize` and `deserialize` use `rmw_serialize` and `rmw_deserialize` with the handle of the rmw layer, and `release_to_shared` hands the instance to a `std::shared_ptr<void>`, which is the shape that the type-erased calls of rclcpp want. `message_to_ui_value` and `ui_value_to_message` do the same for raw memory and a table, sub-messages included, and `utf16_to_utf8` and `utf8_to_utf16` transcode wide strings. [Values](ARCHITECTURE.md#ros-introspection-design) lists the conversions.

```cpp
ui::MessageDescriptionPtr description;
const ros_ui::Result found = ros_ui::introspect_message("geometry_msgs/msg/Pose", description);   // loads the typesupport library
if (!found.ok()) {
  std::printf("%s: %s\n", ros_ui::to_string(found.status), found.message.c_str());
  return;
}
const ui::UiValue defaults = ui::make_default_value(*description);   // what a fresh message holds

ros_ui::DynamicMessage message;                                      // an instance without a C++ type
if (ros_ui::DynamicMessage::create("geometry_msgs/msg/Pose", message).ok()) {
  const ros_ui::Result converted = message.from_ui_value(defaults);  // atomic: a failure leaves the instance as it was
  rclcpp::SerializedMessage bytes;
  if (converted.ok() && message.serialize(bytes).ok()) {             // CDR, with the handle of the rmw layer
    const ui::UiValue again = message.to_ui_value();                 // bounded by ConversionLimits
    std::printf("%zu bytes, %zu fields\n", bytes.size(), again.size());
  }
}
```

`TypeSupportRegistry::global()` is the registry that the bridges use unless you pass one. It is thread-safe, and it looks for the library of a package in the prefixes that `add_search_prefix` added, then in the folders of `AMENT_PREFIX_PATH`, and last in the search of the operating system; it exposes its naming rules, `library_basename`, `message_symbol`, `service_symbol` and `action_symbol`, and `candidate_paths`, for tests and diagnostics, and `loaded_library_count` tells how many libraries it holds, because it never unloads one.

### Topics

`TopicBridge::subscribe` makes a `TopicSubscription` and `advertise` a `TopicPublisher`; both are shared pointers, and the entity leaves the node when the last one is dropped. The options of a subscription are the QoS preset, `Adaptive` unless you choose another, a depth of 10 and the `ConversionLimits`; an empty type name is looked up on the graph and gives `NotFound` when the topic is absent. `take_latest` converts the newest message once, `latest` converts it again whether or not it was taken, `sequence` counts the messages that arrived, `set_callback` sets the push callback, which a null clears, and `diagnostics` counts messages, conversions, failures, incompatible-QoS events and re-subscriptions and keeps the last error. `publish` converts a value to the type, takes the default for a field that the value does not hold, serialises and publishes. `reconcile` derives the adaptive profiles again from the endpoints that exist and returns how many entities it created again.

```cpp
ros_ui::TopicBridge bridge(ros_ui::NodeHandles::from(node));
std::shared_ptr<ros_ui::TopicSubscription> subscription;
const ros_ui::Result subscribed =
  bridge.subscribe("/odom", "", ros_ui::TopicSubscriptionOptions{}, subscription);   // "": look the type up on the graph
std::shared_ptr<ros_ui::TopicPublisher> publisher;
const ros_ui::Result advertised =
  bridge.advertise("/cmd_vel", "geometry_msgs/msg/Twist", ros_ui::TopicPublisherOptions{}, publisher);
if (!subscribed.ok() || !advertised.ok()) {
  return;
}

ui::UiValue message;
std::uint64_t sequence = 0;
if (subscription->take_latest(message, &sequence)) {                 // true once for each newest message
  std::printf("message %llu\n", static_cast<unsigned long long>(sequence));
}
const ui::UiValue twist = ui::UiValue::object().set("linear", ui::UiValue::object().set("x", 0.2));
publisher->publish(twist);                                           // an absent field takes its default
bridge.reconcile();                                                  // about once a second: follows publishers that come and go
```

`QosPreset` is `Adaptive`, `SystemDefault`, `Reliable`, `SensorData` or `TransientLocal`. `choose_subscription_profile` and `choose_publisher_profile` are the pure rules over `rmw_qos_profile_t` that [ROS introspection design](ARCHITECTURE.md#ros-introspection-design) states, `same_matching_policies` compares the policies that decide matching and delivery, and `describe_profile` gives text of the form reliable/volatile/keep_last(10).

### Services and actions

A call of the `ServiceBridge` returns as soon as the request is queued. `ServiceCallOptions` holds a timeout of 5 s, a flag that makes the call wait for a server that is not up yet instead of failing at once, and the options and the limits of the conversion. The outcome, a `ServiceCallOutcome`, has the `Result` that is `Ok` with a response or `Timeout`, `RclError` or `ConversionFailed`, the response, the service and type names, the id of the call and the time that it took. `call` delivers it to a callback exactly once, and `call_async` through a future, with a failure to queue as the outcome of the future, so that the future is always valid. `resolve_service_type`, `describe` and `server_available` ask the graph, and `poll` expires the calls that are overdue and sends the deferred ones, and returns the number of outcomes that it delivered.

```cpp
ros_ui::ServiceBridge bridge(ros_ui::NodeHandles::from(node));
ros_ui::ServiceCallOptions options;                                  // 5 s timeout, and wait for a server that is not up yet
options.timeout = std::chrono::milliseconds(2000);
const ui::UiValue request = ui::UiValue::object().set("data", true);
const ros_ui::Result queued = bridge.call("/set_bool", "std_srvs/srv/SetBool", request, options,
  [](const ros_ui::ServiceCallOutcome & outcome) {                   // exactly once, on the thread that spins the node
    std::printf("%s: %s\n", outcome.service.c_str(), outcome.ok() ? "ok" : outcome.status.message.c_str());
  });
std::shared_future<ros_ui::ServiceCallOutcome> pending =
  bridge.call_async("/set_bool", "std_srvs/srv/SetBool", request, options);   // the same with a future; nothing waits on it
std::printf("%s, %zu pending\n", queued.ok() ? "queued" : queued.message.c_str(), bridge.pending_count());
bridge.poll();                                                       // only for a host that spins no timers
(void)pending;
```

The `ActionBridge` sends a goal as a value tree and exposes its life in two ways. The `ActionGoalHandle` is a thread-safe view: `status`, `accepted`, `rejected`, `finished`, `take_feedback`, `latest_feedback`, `result` with the terminal status, `error` and `diagnostics`. The `GoalEvent`s go to `ActionGoalOptions::on_event` on the thread that spins the node: `Rejected`, `Accepted`, `Feedback`, `Status`, `Result`, `CancelResponse` and `Error`. The values of `GoalStatus` are the `STATUS_*` constants of the message action_msgs/msg/GoalStatus, and `goal_id_to_string` writes a goal id as 32 lowercase hex digits. `ActionGoalOptions::response_timeout` bounds the answer of the server, 5 s by default, and nothing bounds the execution. `cancel` asks the server to cancel, and the answer arrives as a `CancelResponse` event.

```cpp
ros_ui::ActionBridge bridge(ros_ui::NodeHandles::from(node));
ros_ui::ActionGoalOptions options;
options.response_timeout = std::chrono::milliseconds(3000);          // for the answer of the server; the execution has none
options.on_event = [](const std::shared_ptr<ros_ui::ActionGoalHandle> & handle, const ros_ui::GoalEvent & event) {
  std::printf("%s: %s\n", handle->goal_id_string().c_str(), ros_ui::to_string(event.kind));   // Accepted, Feedback, Result ...
};
std::shared_ptr<ros_ui::ActionGoalHandle> goal;
const ros_ui::Result sent =
  bridge.send_goal("/fib", "", ui::UiValue::object().set("order", 8), options, goal);
if (sent.ok()) {
  ui::UiValue feedback;
  const bool news = goal->take_feedback(feedback);                   // the newest feedback since the last take
  std::printf("%s, %s\n", ros_ui::to_string(goal->status()), news || goal->finished() ? "news" : "quiet");
  bridge.cancel(goal);                                               // the answer arrives as a CancelResponse event
}
```

## Wire protocol

The frames between a page and its server are specified in [Wire protocol](WIRE_PROTOCOL.md). `ui_protocol.hpp` is the reference encoder and decoder, `qar_web_panels` is the server half, and the script that `render_html_page` embeds is the page half.

## Thread rules

| Call | Thread | Blocking | Failure is reported as |
|---|---|---|---|
| `WebPanelServer`, `HttpServer` and every call of a `PanelSession` | the thread that owns them, in practice the thread of the Qt event loop; a call of a session from another thread is refused | none: the sockets are asynchronous | `false` with a reason, or a null pointer |
| `IPanelControlSink::on_control`, the signals of the session | the thread that owns the session; a queued connection moves a signal to another thread | must not block | none |
| `render_html_page`, `validate`, the JSON functions, the frames and the arithmetic of sizes | any thread: they are functions of their arguments | the CPU time of the call | a `bool` with the problems or an error string |
| `TopicBridge::subscribe`, `advertise`, `reconcile`, `ServiceBridge::call`, `ActionBridge::send_goal` | the thread of the host | none: the entity is created or the request is queued, and the call returns | a `Result` |
| `TopicSubscription::take_latest`, `latest`, the handles and the diagnostics | any thread: each takes a mutex for a moment | one deserialise and one conversion | a `bool` or a `Result` |
| callbacks of the bridges and the `RosUiDiagnosticsSink` | the thread that spins the node | must not block | none |
| `TypeSupportRegistry` | any thread: it is thread-safe | loading a library takes as long as the loader of the system needs | a `Result` |

[Threading: nothing waits](ARCHITECTURE.md#threading-nothing-waits) explains why the rules are what they are.
