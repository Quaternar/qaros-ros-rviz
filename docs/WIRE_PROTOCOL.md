# Panel wire protocol (qar-ui/1)

This page specifies the frames between a generated AR panel page and the server that feeds it. The reference encoder and decoder is `qar_ui_model/src/ui_protocol.cpp`, the page side is `qar_ui_model/src/html_runtime.cpp`, the server side is `qar_web_panels/src/panel_session.cpp`, and `qar_ui_model/test/ui_protocol_tests.cpp` pins the exact bytes of every frame. A difference between this page and those files is a defect to report. [Architecture](ARCHITECTURE.md#data-and-control-flow) explains how a host uses the protocol, and [API](API.md#wire-protocol) lists the functions that build and read the frames.

## Transport

A page holds one WebSocket at a time. The socket carries text frames only, every frame is one JSON object, and the frames are independent: no frame spans two WebSocket messages and no message carries two frames. The server answers a binary message by closing the socket.

The page dials the address in the member `websocket_url` of its configuration, which is `HtmlRenderOptions::websocket_url`. When that is empty, the page derives `ws://<page host>/ws/<panel id>` from its own location, with the scheme wss for a page that came over HTTPS. `qar_web_panels` never leaves the address empty: its route carries the token of the panel and not the panel id, so it writes `ws://<host>/ws/<token>` into every page that it serves, where `<host>` is the Host header of the request. A page that has no address and no host, because it was opened from a file, stays offline and shows its initial data.

Whenever the socket closes, and whenever the browser refuses to create one, the page dials again after `reconnect_ms`, 2000 ms by default, and goes on for as long as the page lives.

Text frames must be well-formed UTF-8: a receiver that gets a text frame that is not closes the socket with code 1007 (RFC 6455, section 8.1). `json_write` replaces every ill-formed byte of a string with U+FFFD, so a frame that was made from a `UiValue` is well-formed whatever bytes a publisher put into a string.

The server closes a socket with these codes:

| Code | When |
|---|---|
| 1001 | `stop()` of the server, `remove_panel`, or the destruction of the session; the page reconnects |
| 1002 | the first frame is not a JSON object, has a foreign version, breaks the schema of a hello, or is not a hello |
| 1003 | a binary message arrives |
| 1008 | the hello names another panel, the session has `max_clients` viewers already, or the unsent data of a viewer exceeds `max_client_backlog_bytes` |
| 1009 | a frame or a message is larger than `max_message_bytes`; Qt closes the socket, because the session sets that limit |

After the first frame, a frame that is malformed, has a foreign version, has a type that no page sends (a second `hello`, `data`, `status` or `ack`), or does not have a `seq` above the last one of the connection is ignored and counted in `frames_ignored`; the server does not answer it.

## Envelope

```json
{"v": 1, "type": "<frame type>", "seq": 17, "ts": 1725000000.5, ...}
```

| Member | Type | Required | Meaning |
|---|---|---|---|
| `v` | integer | yes | The major version of the protocol, 1. A receiver that does not speak it ignores the frame; the page also writes "Protocol mismatch" into its status line, and the server closes a socket whose first frame has another version. |
| `type` | string | yes | The frame type, as listed below. A type that the receiver does not know is ignored and is not an error, so that a newer sender can add frames without breaking an older receiver. |
| `seq` | integer, at least 0 | yes | Counts the frames of one sender and only grows. The server counts for each connection, from 1. The page counts from 1 when it loads and does not start again when it reconnects, so its hello is `seq` 1 on the first connection only. The `ack` names a control event by this number. |
| `ts` | number | no | Seconds since the Unix epoch on the sender's clock. Informational. |

A receiver ignores the members that a frame type does not list, which is forward compatibility again. The C++ decoder rejects a frame that lacks a required member or has a member of the wrong JSON type, and says why; the page reads the members that it uses and ignores a frame that it cannot use.

## Frames

### hello

Both sides send it once the socket is open, as the first frame of each.

Page to server:

```json
{"v":1,"type":"hello","seq":1,"ts":1725000000.1,"panel":"robot","runtime":"qar-ui-runtime/1"}
```

Server to page, with the full data document:

```json
{"v":1,"type":"hello","seq":1,"panel":"robot","data":{"battery":{"percentage":0.8}}}
```

| Member | Required | Meaning |
|---|---|---|
| `panel` | yes | The panel id, which is the id of the root widget. The server closes a socket whose hello names another panel. |
| `runtime` | page only | `qar-ui-runtime/1`. |
| `model` | no | A serialised `UiModel`, from `to_value`. The runtime ignores it, so a new model is a new page. |
| `data` | no | A complete data document. It replaces the document of the page and must be an object. |

The hello of `qar_web_panels` carries the whole current document in `data`, so every viewer, including one that reconnects, starts from the state that the others have reached.

### data

Server to page. A list of assignments that the receiver applies in order and all or nothing: if one entry fails, because a path has a bad syntax or a kind clashes, as when it indexes into a string, the whole frame is discarded and the document stays as it was.

```json
{"v":1,"type":"data","seq":42,"ts":1725000000.2,
 "patch":[
   {"path":"battery","value":{"percentage":0.79,"present":true}},
   {"path":"diagnostics[0].level","value":2},
   {"path":"","value":{"battery":{"percentage":0.5}}}
 ]}
```

Each `path` follows the grammar of [Paths](#paths). The empty path replaces the whole document, and its value must then be an object. Every entry must carry `value`, and `null` clears a value. A missing intermediate object or array is created, and an array grows with `null` entries up to the index. A host that mirrors a topic can send one entry for it, such as `{"path":"odom","value":<whole message>}`, which replaces that subtree whole: there is no deep merge, and none is needed. After a data frame the page evaluates every bound widget again.

### control

Page to server. The page sends it when the operator changes an editable control or presses a button.

```json
{"v":1,"type":"control","seq":7,"ts":1725000000.3,
 "control":"increment","action":"increment","path":"","value":null}
```

| Member | Required | Meaning |
|---|---|---|
| `control` | yes | The id of the widget, which has the form `[A-Za-z_][A-Za-z0-9_.-]*` and at most 128 bytes. A frame with another id is rejected. |
| `action` | no | The `action` string of the widget as it is, possibly empty. The library gives it no meaning: the host decides what the strings stand for. |
| `path` | no | The binding of the widget, so that a server can map by path instead of by id. Empty for a widget without a binding. |
| `value` | no, default `null` | The new value: a bool for a toggle, a number for a number input and a slider, a string for a text input, the value of the chosen option for a choice, an object with the three components for a vector (`x`, `y` and `z` unless the widget names others), an object with `x`, `y`, `z` and `w` for a quaternion, and the `payload` of the widget for a button, or `null` without one. |

The page applies the change to its own document at once, and the next data frame of the server confirms or corrects it. A control event is not queued: when the socket is not open at that moment the event is lost, although the listeners that were registered with `onEvent` still see it.

### status

Server to page. One line of text with a level, which the page shows in the status line of its footer.

```json
{"v":1,"type":"status","seq":3,"level":"warning","text":"no publisher on /odom"}
```

`level` is one of `neutral`, `ok`, `warning`, `error` and `stale`, and `neutral` when it is absent. An empty `text` clears the line.

### ack

Server to page. The outcome of one control event, named by the `seq` of that event.

```json
{"v":1,"type":"ack","seq":8,"event_seq":7,"ok":false,"text":"service /go unavailable"}
```

The page shows the text next to the control that sent the event, in the line that each button has, for 4 seconds, and marks a button for 1.5 seconds with the outcome. A failed ack for a control that has no such line goes to the status line. A server does not have to acknowledge anything, and an ack for an event that the page does not know is ignored.

### viewport

Page to server. What the page measured about the box that it was laid out into.

```json
{"v":1,"type":"viewport","seq":2,"ts":1725000000.4,
 "css_w":1024.0,"css_h":640.0,"dpr":1.5,
 "content_w":1024.0,"content_h":1580.0,"root_w":1024.0,"root_h":1580.0,
 "reason":"connect"}
```

| Member | Required | Meaning |
|---|---|---|
| `css_w`, `css_h` | yes | `window.innerWidth` and `window.innerHeight`: the CSS viewport that the page was laid out into. Both must be above 0. |
| `dpr` | no | `window.devicePixelRatio`. A diagnostic: it says how much raster is behind the page, not how big the page is. |
| `content_w`, `content_h` | no | `document.documentElement.scrollWidth` and `scrollHeight`: how much page there is. A `content_h` above `css_h` means content below the fold. Once the click pager of the page owns the scrolling, `content_h` is the `scrollHeight` of the scrolling element instead. |
| `root_w`, `root_h` | no | The size of the box of the panel root, `.qar-panel`, in CSS pixels. |
| `reason` | no | `connect`, `resize` or `content`. Free text: a receiver must not switch on it. |

A member that the engine could not answer is absent and not zero, so a receiver can tell a member that was not reported from one that was reported as 0. The C++ decoder rejects a negative extent and one above 1e9, like any other violation of the schema, because a panel is sized from these numbers and a broken measurement would come back as a physically wrong panel.

The page sends a report after its hello, when the window is resized, and after a data frame or a page jump that may have moved the layout. It waits 120 ms after the last trigger and sends nothing unless a number has changed by more than one, so a resize storm does not become a frame storm. A reconnect forgets what was sent and reports again, because the server on the other end may not be the one that heard last.

**Why this frame exists.** A QarOS website panel has to commit to a physical size in metres before its browser has laid anything out, and nothing in the SDK states how the viewport of that browser follows from those metres. The page therefore measures the box that it was given and reports it, and the server sizes the panel from the report ([Panel sizing](ARCHITECTURE.md#panel-sizing)). In the measurements of the maintainers the viewport was 500 by 500 CSS pixels at a device pixel ratio of 2.0 whatever size the panel declared ([The QarOS in-AR browser](SDK_COMPATIBILITY.md#the-qaros-in-ar-browser)), while a generated page wants about 756 CSS pixels of width. Matching the aspect is therefore not enough, and the page also has to fit: it scales its theme down by a factor at its first paint, and the server scales the panel up by the same factor in metres, which leaves text, touch targets and gaps at the same angular size. `css_w` is thus the input of two decisions, the metres of the panel and the theme that the page draws itself with. The numbers of the report do not change when the page restyles itself; `content_h` does, which is why a report with the reason `content` follows a fit.

### ping and pong

Either direction. Application-level liveness, because the WebSocket interface of a browser has no call that sends a protocol-level ping. The page sends `ping` when it has sent nothing and received nothing for `ping_interval_ms`, 5000 ms by default, and it checks once a second. A receiver answers with `pong`, whose `ping_seq` is the `seq` of the ping. When no frame of any kind has arrived for more than `stale_after_ms`, 5000 ms by default, the connection indicator of the page changes from Live to Stale, and the next frame changes it back. The server never sends a ping itself.

```json
{"v":1,"type":"ping","seq":10}
{"v":1,"type":"pong","seq":11,"ping_seq":10}
```

## Values

Values are JSON with these conventions, which are those of `UiValue`:

- Objects keep the order of their keys, which is the declaration order of a ROS message.
- Integers and doubles are different kinds in C++, `Int` and `Double`, and JSON does not tell them apart. The C++ writer gives a double a fraction or an exponent, `5.0` or `1e+21`, and the reader takes a literal without either as an integer. The page cannot keep the difference, because JavaScript has one kind of number, so a server converts by the kind of the field in its `MessageDescription` and never by the JSON token. `validate_value` accepts an integral double for an integer field.
- NaN and the infinities are written as `null`, and a page shows an em dash for them.
- Strings are UTF-8, and the writer replaces ill-formed bytes with U+FFFD.
- Binary data is not a concept of this protocol. Send an image as a `data:image/...;base64,` string that is bound to an image widget, or reduce a large array on the server. The runtime refuses an image URL that has a scheme other than http, https, blob or data:image/, and one that is longer than 4 MiB.

## Paths

Bindings, which link a widget to the document, and patches, which change the document, use one grammar. `FieldPath::parse` in C++ and `parsePath` in the page implement it the same way:

```
path       = segment ('.' segment)*       | ""        (the whole document)
segment    = identifier ('[' digits ']')?
identifier = [A-Za-z_][A-Za-z0-9_]*
```

Examples are `pose.position.x`, `ranges[3]` and `points[2].x`. A path has no spaces, no quotes, no negative indices, no nested indices such as `a[1][2]`, and no trailing dot. The rules for resolving a path against a schema are in `type_description.hpp`. Against a document, a missing segment gives no value, which a page shows as an em dash.

## Page runtime API

`render_html_page` embeds the runtime `qar-ui-runtime/1`, which sets `window.QarPanel`. An embedding host can drive the object, a test harness in a browser can call it, and a later runtime has to keep it:

| Member | Meaning |
|---|---|
| `protocolVersion` | `1` |
| `runtime` | `qar-ui-runtime/1` |
| `model` | The parsed model JSON: `to_value(UiModel)` without the data. |
| `getData()` | The current data document, as the live object. Do not change it. |
| `getValue(path)`, `setValue(path, value)` | Read and write by path. `setValue` refreshes the widgets and returns `false` for a bad path or a kind clash; the empty path replaces the document and needs an object. |
| `applyUpdate(frame)` | Applies a parsed `data` frame, all or nothing, and returns `true` on success. |
| `handleMessage(textOrObject)` | The whole inbound dispatch: parse, check the version, route by type. It returns `false` for a frame that was rejected. |
| `emit(control, action, path, value)` | Builds and sends a `control` frame and returns it. It calls the `onEvent` listeners whether or not a socket is open. |
| `onEvent(fn)` | Registers a listener for outbound control frames: `fn(frame)`. |
| `connect(url)`, `disconnect()` | Socket control. `connect()` without an argument uses the configured or derived address. |
| `state()` | The state of the connection: online, stale or offline. |
| `refresh()` | Draws every bound widget again from the document. |
| `parsePath(text)` | The grammar above; returns the segments, or `null`. |
| `viewport(reason)` | Measures the viewport of the page and returns the `viewport` frame that it would send. |
| `reportViewport(reason)` | Measures and sends a report when a number has changed; returns whether it was sent. |
| `planPaging(viewport, content, scroll)` | The arithmetic of the click pager, as `qar_ui_model::plan_paging` does it. |
| `pageBy(direction)`, `scrollToOffset(px)` | One click of page up or page down, or an absolute jump. |
| `pagerState()` | Recomputes and returns the current plan of the pager. |
| `applyFit(reason)` | Fits the theme of the page to the viewport that it was given, as `qar_ui_model::fit_page_to_viewport` does it. It returns an object with the members `scale`, `columns`, `viewport` and `reason`, or `null`. The page calls it at boot before it draws anything, and on every resize. |
| `fitState()` | The fit that was applied last, or `null` when none was. |

The DOM contract, which is what the runtime looks for, comes from `render_widget_fragment` and is pinned by golden tests: every widget is an element with `data-qar-id`, `data-qar-type` and, when it is bound, `data-qar-bind`. The parts that the runtime fills carry `data-qar-role`, and the parts of a component, such as the three fields of a vector, also `data-qar-component`.

The page chrome outside the widgets is the boot diagnostic `#qar-diag` and the click pager `#qar-scroll`, whose parts have the roles `scroll-top`, `scroll-up`, `scroll-pos`, `scroll-down` and `scroll-bottom`. The page always contains the pager and hides it until the runtime has measured real overflow, and it never shows it on hover. It is paging and not a scrollbar by choice: a thumb is a few millimetres of panel that an operator has to hit and hold in the air, and discrete jumps that are each complete fit that better ([Design notes](ARCHITECTURE.md#paging-by-click-not-a-scrollbar)).

## Versioning

- `v` is the major version, and it is the only version on the wire. Adding a frame type or an optional member is not a change of version, because a receiver ignores unknown types and members. Changing the meaning of an existing member, or making a new member required, is one: raise `v`, and let the page show the mismatch. The `viewport` frame was added in this way: a page that predates it never sends one, and a server that predates it decodes the frame as an unknown type and ignores it.
- `UiModel::kModelVersion`, which is the JSON layout of the model, and the version after the slash in `qar-ui-runtime/1`, which names the page script, are versioned separately. They travel inside the page and not over the socket.

## A session, end to end

The frames of one session of the counter panel of the example, in the order in which they cross the socket. The page counts its own frames and the server counts its own:

```
page   -> server   {"v":1,"type":"hello","seq":1,"panel":"counter_panel","runtime":"qar-ui-runtime/1"}
server -> page     {"v":1,"type":"hello","seq":1,"panel":"counter_panel","data":{"count":0}}
page   -> server   {"v":1,"type":"viewport","seq":2,"css_w":1024.0,"css_h":640.0,"dpr":1.5,"content_h":1580.0,"reason":"connect"}
server -> page     {"v":1,"type":"data","seq":2,"patch":[{"path":"count","value":1}]}
page   -> server   {"v":1,"type":"control","seq":3,"control":"increment","action":"increment","path":"","value":null}
server -> page     {"v":1,"type":"data","seq":3,"patch":[{"path":"count","value":2}]}
server -> page     {"v":1,"type":"ack","seq":4,"event_seq":3,"ok":true}
  (5 s of silence)
page   -> server   {"v":1,"type":"ping","seq":4}
server -> page     {"v":1,"type":"pong","seq":5,"ping_seq":4}
```
