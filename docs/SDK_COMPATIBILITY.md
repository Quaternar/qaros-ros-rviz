# QarOS SDK compatibility

## Supported QarOS SDK drops

A QarOS SDK drop has no version number: its C header carries no version macro, and its libraries carry no version resource. QuaternAR names a drop by its date, and this stack states the drops it supports by those dates.

| SDK drop | Status | Meaning |
|---|---|---|
| 2026-09-28 | verified | The drop that this stack was built, tested and run against. The vendored C header is the header of this drop. |
| 2026-09-21 | minimum | The earliest drop that provides everything the code calls. The four items below set this minimum. |
| earlier drops | not supported | They lack at least one of the four items below. |

The minimum is set by these parts of the C API, all of which the code uses and which the vendored header declares:

- the function `qar_session_id_unique`, which makes a session id that is unique and not zero;
- the function `qar_gui_panels_subscribe_all_gesture_events`, which subscribes to all gesture events of a GUI panel;
- the parameter structure `QarStreamParamsOpenGlWgl`, which hands the Windows OpenGL context of the host to a render stream;
- the field `app_volume_id` of `QarRenderSenderInit`, which must not be NULL: a NULL id is rejected with the status `QAR_STATUS_APP_VOLUME_INVALID_ID`.

At the time of writing, the public QarOS repository <https://github.com/Quaternar/qaros> publishes a later generation of the C API. This stack does not build against it: the session set-up (create, serialise, join and invite), the hand of a gesture, the time points and the init structures of volumes and panels would have to be ported.

## Identifying an SDK drop

Because a drop has no version number, you identify it by the SHA256 of its C header, the file `include\qar_streaming.h` in the SDK folder. Two drops with the same header have the same C API; the hash does not prove that every binary of the two drops is identical.

The first command prints the hash of the header in your SDK folder.

```powershell
(Get-FileHash -LiteralPath (Join-Path $env:QAR_SDK_ROOT 'include\qar_streaming.h') -Algorithm SHA256).Hash
```
```text
5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA
```

The header that this stack compiles against is the header of the verified drop, and the second command prints its hash.
The repository qar_ogre_adapter installs the header with its libraries, so the command reads it from the install prefix.

```powershell
(Get-FileHash -LiteralPath (Join-Path $env:QAR_PREFIX 'include\qar_streaming.h') -Algorithm SHA256).Hash
```
```text
5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA
```

If the two hashes are equal, your SDK has the C API of the verified drop. If they differ, your drop is another one. The third command checks whether it still provides the four items that set the minimum drop; it prints True for each item that the header of your SDK declares.

```powershell
$header = Join-Path $env:QAR_SDK_ROOT 'include\qar_streaming.h'
foreach ($name in 'qar_session_id_unique', 'qar_gui_panels_subscribe_all_gesture_events', 'QarStreamParamsOpenGlWgl', 'app_volume_id') {
    '{0,-46} {1}' -f $name, (Select-String -LiteralPath $header -Pattern $name -SimpleMatch -Quiet)
}
```
```text
qar_session_id_unique                          True
qar_gui_panels_subscribe_all_gesture_events    True
QarStreamParamsOpenGlWgl                       True
app_volume_id                                  True
```

A drop that has all four items and a different header is not verified: the stack may work with it, but nobody has checked. Build the repositories, run the tests, and read the changes listed for each drop before you rely on it.

## Updating the QarOS header

This repository does not hold the QarOS C header. It comes with the repository qar_ogre_adapter, which vendors it and installs it into the prefix next to its libraries, so that every repository of this stack compiles against the same file. To move to another SDK drop, update the header there first, following the document "SDK compatibility" of qar_ogre_adapter, install qar_ogre_adapter into the prefix again, and then rebuild this repository.

### Rebuild order

After the header has changed, rebuild the repositories in the order of their dependencies: qar_ogre_adapter first, then qar_qt_streaming, and qar_rviz_plugin last. Each repository is built and installed into the prefix as described in its installation document, and the later ones find the new header and libraries there.

Rebuild from scratch rather than incrementally: an incremental build can keep objects that were compiled against the old header.

Go to the folder of the clone of this repository first:

```powershell
Set-Location "$env:QAR_ROOT\src\qar_rviz_plugin"
```

```powershell
.\scripts\build_plugin.ps1 -Clean
```
```text
...
=== Clean: removing C:\qar\src\qar_rviz_plugin\build\colcon ===
...
  colcon : C:\qar\src\qar_rviz_plugin\build\colcon
Starting >>> qar_rviz_streaming
...
Finished <<< qar_rviz_streaming [18.7s]
...
Summary: 1 package finished [19.1s]
...
=== BUILD OK ===
Plugin DLL: C:\qar\install\bin\qar_rviz_streaming.dll
plugin_description.xml installed: C:\qar\install\share\qar_rviz_streaming\plugin_description.xml
Overlay (dot-source it): C:\qar\install\local_setup.ps1
```

## API changes by SDK drop

The table lists the changes of the C header from the minimum drop to the verified drop, grouped by drop. The rows of the minimum drop say what its header declares and what this stack needs from it; the rows of the later drops are the differences between the headers of the drops. The drop that lies between the minimum drop and the verified drop is called "after the minimum drop". Only the rows that concern this repository are shown. In the third column, the adapter is the QarOS C API adapter in qar_ogre_adapter:

| SDK drop | Change in the header | Effect on this stack |
|---|---|---|
| 2026-09-21 (minimum drop) | Panel input is a gesture stream: the per-kind click and hover subscriptions and their flat pointer event are replaced by `qar_gui_panels_subscribe_all_gesture_events`, which takes every kind through one reader, and by the opaque handle `QarGuiPanelGesture`, which is read through accessors. The kinds are hover, tap, long press and drag, each with a phase. Every panel, URL panels included, reports gestures. | The adapter subscribes once per stream panel and reads the handle at the top of the callback. The adapter of URL panels does not subscribe, because the page handles pointer input itself and reports it over its own WebSocket. |
| 2026-09-21 (minimum drop) | The two kind enumerations disagree about the value 0: it is hover for a panel, `QAR_GUI_PANEL_GESTURE_KIND_HOVER`, and click for an app volume, `QAR_GESTURE_CLICK`. | The adapter converts each enumeration with its own function, and the seam pins the values at compile time, so that a renumbering fails the build. |
| 2026-09-21 (minimum drop) | A panel gesture names its hand, `qar_gui_panel_gesture_get_hand`, and counts its taps, `qar_gui_panel_gesture_get_tap_count`. A position, `QarGuiPanelPoint`, holds signed integers. | Every hand of every peer is its own pointer with its own grab, and the adapter reads the tap count. |
| 2026-09-21 (minimum drop) | The gesture of an app volume is the opaque handle `QarAppVolumeGesture`, read through accessors. The subscription for gesture updates is renamed `qar_app_volumes_subscribe_gesture_events`, and `qar_app_volumes_subscribe_all_gesture_events` takes every kind through one reader. | The adapter subscribes once for the volume and dispatches on the kind. |
| 2026-09-21 (minimum drop) | The function `qar_session_id_unique` makes a unique session id. | The adapter uses it for the session that it creates. |
| 2026-09-21 (minimum drop) | The parameters of an OpenGL stream are one structure for each type of context, `QarStreamParamsOpenGlEgl`, `QarStreamParamsOpenGlWgl` and `QarStreamParamsOpenGlGlx`, with a shared `QarOpenGlContextCallbacks`. The single structure for all three types is retired, and the callbacks capture and restore the binding of the thread instead of unbinding it. | The adapter chains `QarStreamParamsOpenGlWgl` and supplies the four callbacks. |
| 2026-09-21 (minimum drop) | The field `app_volume_id` of `QarRenderSenderInit` is required: a NULL id is rejected with `QAR_STATUS_APP_VOLUME_INVALID_ID`. | The adapter creates its volume before any sender and refuses to create a sender without one. |
| 2026-09-21 (minimum drop) | The header documents the frame of a panel: the origin is the midpoint of the top edge, +X runs to the viewer's left, the front normal is -Z, and panel content space runs backwards along both panel axes. | The pose of a panel is passed to the header unchanged, and the hit test of the Qt library uses this frame. |
| after the minimum drop | A section on coordinate systems defines the spaces of the C API: room space, app volume space, app content space, view space, panel space, panel content space and texture pixels. | This stack uses the spaces by these names. |
| after the minimum drop | The view poses, the hand poses and the start and action points of a mapped gesture are in app content space, in app units. The gesture points were stated to be in room or app volume space, in metres. | The pick code uses the points without the placement of the volume. |
| after the minimum drop | The start point is the hand and the action point is the intersection, or the start point when nothing was intersected, so that the pair is the ray that the user points along. An absent point had meant a default such as the centre of the volume. | The adapter prefers the ray from the start point to the action point, and reconstructs a ray from the hands when the two points are equal. |
| after the minimum drop | The default gesture configuration has four rules: hover with a dotted ray and click with a light green ray come after the two manipulation rules, whose rays are green. It had the two manipulation rules only. | A volume that is created without a configuration delivers hover and click gestures with these rays; this stack passes NULL. |
| after the minimum drop | `qar_render_sender_last_hands` returns the last sample again for a second and reports untracked hands after that; the hand poses are in app content space, and the radii and velocities of the joints are not converted. | The adapter polls the hands once per frame. |
| after the minimum drop | `qar_render_frame_info_get_view_pose` returns the pose of a view in app content space and `qar_render_frame_info_get_view_fov` the field of view in view space; `rendered_near_far` is in app units, and the sender folds the app scale into the depth itself. | The adapter reads the pose and the field of view of both eyes and reports the near and far distances and the depth scale with every frame. |
| 2026-09-28 (verified drop) | A stream panel ignores `content_scale`: its video is drawn at the full panel width and scrolled vertically when it is taller than the panel. | This stack sends 1.0 by default, so its panels are not affected. |
| 2026-09-28 (verified drop) | The position of a gesture on a stream panel refers to the video as its sender draws it, at the resolution of the sender, and for a video that the renderer has scrolled it is the pixel under the finger. | The adapter normalises the position against the content size that its caller names. |

The SHA256 of the file `include\qar_streaming.h` tells the three headers apart:

| SDK drop | SHA256 of the header |
|---|---|
| 2026-09-21 (minimum drop) | D26892604A186E5CC0AEC149614410F7DD648570FE28510A9D49C65220034C50 |
| after the minimum drop | BF0C8C2DE23584B02E7CC1E96B66282908792477FB48C7990CF7AE4E1E78D0FE |
| 2026-09-28 (verified drop) | 5223B56251F3085B376A8BDE5ECEFCF661D6E8F8B800F6AA46414ADC1F4DB6EA |

## QarOS behaviour this repo relies on

The sections below state the QarOS behaviour that this repository relies on.

### Depth encoding

The header says which units the depth values of a frame are related to, but not what they are. A frame carries its depth in views of the type depth. The call that shows a frame takes the near and far distances that the frame was rendered with, in app units, and a `depth_scale`, the factor from the depth buffer's units to the view poses' units. The header does not document the encoding of the depth values, and it does not say what a receiver does with those two fields.

The authors of qar_rviz_plugin observed the following with the QarOS Visualizer and an SDK drop earlier than that of 2026-09-28. It reads the depth texture as the raw window-space depth value in the range 0 to 1, the value that an OpenGL depth buffer holds, and changing `depth_scale` or `rendered_near_far` does not change what it shows. Metric distances in metres therefore display wrongly there: every surface farther than one metre saturates and the scene appears in one flat colour. What other peers do with the depth values is not documented by the SDK.

This stack can write either encoding. The default is the metric linear eye-space distance in metres. With the environment variable `QAR_DEPTH_ENCODING` set to `ndc` it writes the window-space value, `gl_FragCoord.z`, the standard non-linear window depth, and it clears the depth view to 1.0, the value of a depth buffer where nothing was drawn. Set `ndc` for the Visualizer.

The live launcher of the plugin sets `QAR_DEPTH_ENCODING` to `ndc` unless the variable is already set.

The window-space value is reciprocal in distance. A far plane that is wider than the content needs leaves little of the range for the near surfaces, and everything beyond the far plane is clipped away, so choose the far plane for the content. With every frame that it shows, the sender reports the near and far distances it rendered with and the `depth_scale`, 1.0 by default; the Visualizer does not use them.

### World basis

Every 3D space of the C API follows the OpenXR convention: right-handed, +Y up and -Z forward, so a view looks along -Z, and a pose is a position vector and an orientation quaternion stored x, y, z, w.

The ROS world that RViz renders follows REP 103 instead: right-handed, +X forward, +Y left and +Z up.

The camera-local axes of QarOS and of an Ogre camera agree: X right, Y up, looking along -Z. This stack therefore copies a view pose component for component, and it only reorders the quaternion to the w, x, y, z storage of Ogre; the rotation is the same. The world axes do not agree, and a Y-up pose that is fed into a Z-up world tips the viewer on its side.

For Z-up content this stack rotates the QarOS world into the ROS world with ros = (-z, -x, y), which is the unit quaternion (w, x, y, z) = (0.5, 0.5, -0.5, -0.5): QarOS up becomes ROS up and QarOS forward, -Z, becomes ROS forward, +X. The rotation is applied only when the environment variable `QAR_POSE_WORLD` is `zup`; unset, the mapping is the identity. Unit tests check that forward and up map as stated and that the identity orientation becomes the quaternion above.

In `zup` mode the stack then applies a viewer anchor after the change of axes: a yaw about the ROS +Z axis, `QAR_POSE_YAW_DEG`, 180 degrees by default; an eye height along +Z, `QAR_POSE_HEIGHT_M`; and a stand-off behind the facing direction, `QAR_POSE_BACK_M`. The height and the stand-off default to 0, so by default the change affects the orientation only. The code that turns a gesture into a pick reads the same variables, so that a pick uses the same map as the camera.

### App volume

An app volume is a cuboid that is placed in the room and that reserves the region a remote 3D application renders into. It is required: `QarRenderSenderInit::app_volume_id` must name a volume that already exists, because the volume is what places the stream in the shared 3D scene, and a stream without one is not positioned in the scene and no viewer can show it. A NULL id is rejected with `QAR_STATUS_APP_VOLUME_INVALID_ID`. Create the volume first with `qar_app_volumes_add_volume` and pass the id that it returns.

The pose of the volume, the field `pose` of `QarAppVolumeInit`, places the centre of the cuboid in room space. The app pose, `app_pose`, is the origin of the rendering application inside app volume space, in volume metres, relative to the cuboid centre; the app scale does not scale it. The app scale, `app_scale`, converts app metres into volume metres: 1.0 renders 1 m of the application as 1 m in the volume, 0.5 makes the application appear smaller and 10.0 bigger. The header states that nothing clips the application to the cuboid: the volume says where the application is meant to be, and gestures and layout are derived from it.

This stack creates one volume when it connects, before any sender exists, and keys every render stream to it. The volume is a cube of 40 m, its centre is raised 1 m along +Y of room space so that the box does not sink below y = 0, its app pose is the identity and its app scale is 1.0. When the volume cannot be created the session stays usable, but no render stream can be created: the sender factory then refuses with a message that says why.

A volume can be pinned to the Earth. The header defines world space as optional and Earth-fixed, ECEF WGS84 in metres in double precision, and a world anchor, `QarGeoAnchorFrame`, as an origin, three unit axes and a handedness in that space. `qar_app_volumes_change_app_world_anchor` sets the anchor of a volume and `qar_app_volumes_clear_app_world_anchor` removes it.

This stack sets the anchor from a geodetic fix. It converts latitude, longitude and the height above the WGS84 ellipsoid, which is the height that the ROS message `NavSatFix` carries and not the height above mean sea level, to the ECEF origin, and it takes the east, north and up directions at that point as the +X, +Y and +Z axes of the anchor, which is the ROS convention. A fix that is NaN or out of range is refused, because NavSatFix reports NaN when the receiver has no lock, and without a volume there is nothing to anchor. Unit tests check that the east, north and up basis is right-handed and that a NaN fix is rejected.

### Peers, invites and admission

A peer enters a session by invitation. `qar_session_invite_peer` takes a connection string, which the SDK builds per device type with `qar_session_invite_connection_string_hololens`, `qar_session_invite_connection_string_quest`, `qar_session_invite_connection_string_visualizer` or `qar_session_invite_connection_string_zed`; only the HoloLens builder takes a parameter, the host name of the device. The launcher reserves an id for the device, starts or attaches it, and returns that id. This is the only source of the id of a target peer: `qar_peer_id_unique` names no peer, and a stream that is created with such an id never produces a frame.

The call returns once the launcher has accepted the invite, which is before the device has joined. Wait for the peer to appear through `qar_peer_subscribe_updates` and to reach the state `QAR_APP_STATE_RUNNING` before you stream to it. Inviting the same host again returns the same id.

`qar_peer_subscribe_updates` reports the peers of a session as they come and go. With an SDK drop earlier than that of 2026-09-28 the authors of qar_rviz_plugin observed that it reports every peer that joins, including the peers that the runtime starts itself: one invited Visualizer appeared as about five distinct peer ids. The description of a peer, `QarPeerSpec`, has an id, a display name, an application version, application-defined information, an application state, a version id and a room tag, and no field for the role or the kind of a peer. The display names are generated and, as observed, they change between updates for the same peer id.

This stack therefore streams only to the peers that it admits. The admission policy, `PeerAdmission`, decides which peers these are:

- `kInvitedOnly`, the policy that is in effect unless another one is chosen, admits only the peers that this stack invited itself. It admits every Visualizer that this stack spawned, and it cannot admit a peer that joined on its own.
- `kInvitedOrNew` admits the invited peers and every peer that first appears after a settle window of 5 s that starts when the session is up. It is a timing heuristic for devices that onboard themselves, such as a headset or a Visualizer on another machine, and it exists because the SDK exposes no field for the role of a peer.
- `kInvitedOrNamed` admits the invited peers and the peers whose display name contains a given text. It is not usable, because the display names change.
- `kAny` admits every peer except this process, internal peers of the runtime included. It is meant for diagnosis only.

The own peer is never admitted, and by default at most 8 peers stream at the same time, because every peer costs four renders of the scene per frame, a colour render and a depth render for each of its two eyes; a further peer is refused with a logged warning. A sender that is built for an internal peer of the runtime has the wrong app id, and the runtime then rejects the frames of the Visualizer, because it accepts the frames of a peer only against a sender that was built for its app id.

One Visualizer is one physical head, a pair of eye cameras, so n simultaneous users need n Visualizers, each with its own stereo stream. The connection string that the SDK builds for a Visualizer names no instance, and a repeated invite of the same host returns the same peer, so a second Visualizer cannot be started that way. QuaternAR suggested naming a distinct GUID after the colon of the connection string, because QarOS launches one mixer process per GUID. This stack appends a fresh UUID to the connection string of every Visualizer invite unless its configuration says otherwise, and in the tests of the authors of qar_rviz_plugin two Visualizers streamed at the same time.

### Gesture rules and the default configuration

The gestures that a peer performs against an app volume are recognised by the peer that renders the volume, and they are mapped through the `QarAppVolumeGestureConfiguration` of the volume before they arrive. What a subscriber receives is therefore already expressed in the frame of the target volume and already filtered by the mapping rules of the volume. The kinds are `QAR_GESTURE_CLICK`, `QAR_GESTURE_HOVER`, `QAR_GESTURE_SINGLE_POINTER_6DOF`, `QAR_GESTURE_DUAL_POINTER_TRANSLATE_DISTANCE` and `QAR_GESTURE_DUAL_POINTER_ROTATE`.

A configuration is a list of mapping rules, `mapping_rules`, and the order of the list is the priority: element 0 has the highest priority. When several gestures are active at the same time for the same volume and the same source peer, the runtime applies only the rule of the highest priority that is active, and it ignores the rules of lower priority until that gesture ends, is cancelled or becomes inactive. A rule names a gesture kind, whether it is enabled, a precision, how the scale of the app changes the sensitivity, the axes of translation and rotation, whether gestures from outside the volume count, the style of the ray (none, solid or dotted, with a colour), and which part of the app transform the gesture drives: none, the app pose or the app scale.

A volume that is created without a configuration, with `gesture_configuration` set to NULL, gets the default configuration of the runtime, which `qar_app_volume_gesture_configuration_default` is written to mirror. It has four rules, in this order of priority:

1. `QAR_GESTURE_DUAL_POINTER_TRANSLATE_DISTANCE`, with precision 2.0, a sensitivity that is based on the app scale, no rotation axes and the mapping to the app scale, so that two hands change the scale of the app; its ray is green.
2. `QAR_GESTURE_SINGLE_POINTER_6DOF`, with precision 1.0, no rotation axes and the mapping to the app pose, so that one hand moves the app; its ray is green.
3. `QAR_GESTURE_HOVER`, with no mapping; its ray is dotted and white.
4. `QAR_GESTURE_CLICK`, with no mapping; its ray is solid and light green.

Pass NULL rather than a configuration that you build by hand: a rule whose ray style is left zeroed has the line style `QAR_APP_VOLUME_CONTROLLER_RAY_LINE_STYLE_NONE`, which is no ray at all. This stack always passes NULL. It reads the configuration back from the runtime for its diagnostic log, and it uses only the click and hover gestures as pointer input, because the manipulation gestures move and scale the volume themselves.

A configuration with a rule that could never work, such as an enabled two-hand rotation that moves the app while it allows no rotation axis, is refused with `QAR_STATUS_APP_VOLUME_GESTURE_CONFIGURATION_INVALID`. The volume keeps its previous configuration, and adding a volume with such a configuration fails the same way.

Whatever the configuration, only one peer at a time moves or scales the app: the first peer whose manipulation binds holds the volume until that manipulation ends, and the manipulations of the other peers are refused meanwhile, while their hover and click gestures still arrive. The extension `QarAppVolumeGestureAllowedPeersExt`, chained from the configuration, restricts the peers whose gestures may move or scale the app. Without it, or with an empty list, every peer may, and a gesture of a peer that is not in the list still reaches the application, unmapped. This stack does not use the extension.

Since the 2026-09-28 drop a volume has gesture modes. A mode is one set of mapping rules that the peers of a volume can be switched between with `qar_app_volumes_select_gesture_mode`. The mode `QAR_APP_VOLUME_GESTURE_MODE_DEFAULT` always exists and cannot be removed, every peer is in it until it is switched, and the configuration of the volume sets its rules. A gesture that is running keeps the mode it began in. This stack defines no other mode, and it does not call `qar_app_volumes_select_gesture_mode`.

### The pointing ray

The pointing ray comes from the gestures of an app volume. The start point of a gesture is the hand: where it was when the gesture began and, for a hover, where it is now; a gesture of two hands starts midway between the hands. The action point is the intersection that the gesture acts on, or the start point when nothing was intersected, so it is absent only when the start point is too, and an absent point is zeroed. The header says that from the start point to the action point is the ray the user is pointing along, which an application can draw itself. Both points are in app content space, the space in which the application renders, in app units.

Equal start and action points therefore mean that nothing was intersected: the aim went into empty space. A hover gesture carries one ray per hand, told apart with `qar_app_volume_gesture_get_hand`, and a gesture of both hands names no hand. The header does not say which geometry the runtime intersects the ray with; that is not documented by the SDK.

This stack subscribes once for the volume with `qar_app_volumes_subscribe_all_gesture_events`, which takes every kind through one reader, and it dispatches on the kind. If that call fails, it falls back to one subscription for clicks and one for hovers. It takes the line from the start point through the action point as its ray whenever the two points are further apart than 0.001 app units, and it treats a closer pair as equal. Unit tests check that the pair is the ray and that equal points are refused as the no-intersection case and not turned into a zero ray.

When the two points are equal, this stack reconstructs the ray as a straight line from the eye through the hand. It does this on the render thread, once per frame, from the newest hand sample of `qar_render_sender_last_hands` and from the view pose of the frame, so that a pick whose target is a plane or a distance along the ray still has a direction. QuaternAR states that QarOS casts a slightly curved ray, so the straight reconstruction agrees with the ray of the runtime closely near the hand and not exactly further away.

A pick whose target is the action point publishes the point that the runtime reported, and only a point that is an intersection with the scene. A click on empty space publishes nothing and is refused with a message, because the action point is then the hand itself; a unit test holds this in place.

The logs of the live sessions of this stack with the 2026-09-28 SDK drop show the hover and click events with the ray of the runtime: in one launch of the RViz plugin the counters of the pointer reached 78,315 hovers and 53 clicks, 1,587 sampled points carried the ray and the point where it met the scene, 21 clicks became published points, and 32 clicks were reported without an intersection and were not published. The authors of qar_rviz_plugin accept a pick when it lands near the aimed object or floor spot, about 0.1 m to 0.2 m at a few metres; they gave no verdict on how near the picks of those sessions landed, and no accuracy figure has been measured.

### Teardown and destroy order

The header documents no order of teardown. It gives these facts about the calls: `qar_library_destroy` frees the process-wide state of the library, and no other call may be used after it until the next `qar_library_init`; `qar_session_destroy` leaves and destroys a session; a subscription whose cancel token is NULL stays alive until the session is destroyed; `qar_gui_panels_close_panel` closes and removes a panel; and `qar_video_sender_handle_destroy` destroys a logical video sender and returns nothing.

This stack closes a GUI panel in three steps, in this order. First it cancels the input subscriptions of the panel, the gesture subscription and the panel-update subscription; the call does not return until no callback for the panel is running, so no callback can name a panel that is being destroyed, and the close that the stack itself makes is not mistaken for a close by the operator in AR. Second it calls `qar_gui_panels_close_panel`, unless the operator already closed the panel in AR, and it reports a failure of the call. Third it destroys the video sender with `qar_video_sender_handle_destroy`, whatever the second step did, because closing the sender first would leave a live panel that refers to a retired stream.

The operator can close a panel in AR. The state of a panel is reported by the subscription of `qar_gui_panels_subscribe_panel_updates`: the state `QAR_GUI_PANEL_STATE_CLOSED` means that the operator closed it, and the runtime republishes a closed panel freely, so this stack reports it once. The states minimised and hidden mean that the operator put the panel away, and the stream continues. When the operator closes a panel in AR, the Qt library stops capturing for the panel and does not touch its adapter or its sender; the teardown comes with the close of the Qt window or the shutdown of the session, and it tells the adapter that the operator already closed the panel.

A subscription is retired in a fixed order too. This stack cancels the token, so that the runtime stops delivering, then gives up the callback slot, so that a callback that is already running finds nothing to touch, and only then destroys the token handle.

QuaternAR states that the current runtime survives the destruction of a stream while other streams run. This stack therefore destroys the sender of a closed panel and forgets the stream.

The variable `QAR_PANEL_CLOSE_MODE` set to `notice` is the escape hatch for a runtime on which destroying a sender while other streams run is not safe: the stream and its sender are kept, and the AR panel shows a notice until the session ends.

At shutdown, each render stream first destroys a frame handle that was fetched and not used, and then the stream. The factory destroys the session, then the invite handle, then the runtime, and then, when the library is loaded, calls `qar_library_destroy`. The OpenGL contexts that this stack created for the senders are deleted only after the library is destroyed, so that no context is deleted while a thread of QarOS may still have it current, and the library is unloaded last.

### Logging control

The library initialisation takes three logging settings: whether the log lines are also written to the console, the folder for the log files, and a severity filter with the levels trace, debug, info, warn, error and fatal. A `QarLibraryInit` that comes from `qar_library_init_default` writes to the console, names no folder and filters at info.

The session factory of this stack asks for trace by default, because QuaternAR asks for full trace-level library logs with every bug report, and it keeps the console copy of the lines on. When the host names no folder, the library writes its files to the folder `quaternar` inside the `TEMP` folder.

The environment variable `QAR_LOG_SEVERITY` takes precedence over the severity field of the initialisation, and a trace log is large. The places of the log files, the precedence rule and the volume of a trace log are described in [Where the QarOS library writes its logs](TROUBLESHOOTING.md#where-the-qaros-library-writes-its-logs), [Which severity applies](TROUBLESHOOTING.md#which-severity-applies) and [How much a trace log writes](TROUBLESHOOTING.md#how-much-a-trace-log-writes).

### The in-AR browser for URL panels

A GUI panel with the extension `QarGuiPanelUrlExt` shows a website: the browser is part of QarOS, and the URL string is copied while the panel is added. The header names no browser engine, no version and no feature set; that is not documented by the SDK. The SDK folder holds the libraries `Ultralight.dll`, `UltralightCore.dll`, `WebCore.dll` and `AppCore.dll`, and the user-agent string in `Ultralight.dll` names Ultralight 1.3.0 on WebKit 610.4.3.1.4, presented as Safari 14.0.3.

The authors of qar_rviz_plugin measured the following with test pages that were opened as URL panels. JavaScript runs, and a WebSocket connection to a ws address works. The CSS property gap on flex containers, the property min-height with the value auto on flex items and the selector :focus-visible are not supported. The viewport of a page is 500 by 500 CSS pixels at a device pixel ratio of 2.0, whatever the size of the panel. Whether text can be typed in AR has not been determined, and whether the viewport can be configured is not documented by the SDK.

### Known SDK limitations

These limits of the SDK are either observations of the authors of qar_rviz_plugin from live sessions, which the SDK documentation does not describe and which have not been repeated on every drop, or places where the header leaves a question open:

- **No call retires a logical video stream.** `qar_video_sender_handle_destroy` returns nothing, and `qar_gui_panels_close_panel` closes the panel and not the stream behind it; the header declares no call that stops, retires or withdraws a stream. The authors of qar_rviz_plugin observed that a receiver that is attached to the stream is not told that the stream is gone: it enters a reconnecting state and retries about every ten seconds, each retry fails with the status 507, and this goes on until its peer goes away. This stack destroys the sender and closes the panel when it retires a panel.
- **A cancelled gesture subscription may leave its runtime subscription.** Cancelling the token stops the delivery of events to the callback, but the header does not promise that a callback that is already running has returned when the cancel call returns, so this stack gives up the callback slot under a lock and never frees the slot. The authors of qar_rviz_plugin also observed that the runtime-side subscription of a panel persisted until the session was destroyed, so a session that opens many panels keeps a pair of subscriptions for each of them.
- **A widget resize needs a new sender.** The receiver of a stream panel takes its resolution from the layout of the video sender, and the SDK does not document whether a change of that layout with `qar_video_sender_change_layout` reaches a receiver that is already attached. This stack therefore creates a new sender when the size of a widget changes.
- **What the runtime intersects a gesture with is not documented by the SDK.** The header names the action point as the intersection that the gesture acts on, and it does not say which geometry is intersected.
- **The viewport of URL panels is fixed.** The page of a URL panel sees a viewport of 500 by 500 CSS pixels at a device pixel ratio of 2.0, whatever the size of the panel, and the SDK does not document a way to configure it.
- **No call asks where the operator is.** The header declares the pose of the viewer, its head and its eyes, only with a render sender: `qar_render_frame_info_get_view_pose` on the frame information of a frame returns it in the app content space of the volume that the sender is bound to, and `qar_render_sender_last_hands` returns the hands of the same sender. A program that streams only panels has no render sender, so it cannot know where the operator stands or looks, and the code of this stack places its panels at fixed positions.
- **A panel that is created again after it was closed can crash the Visualizer.** With a Visualizer attached, the Visualizer crashed in each of the 25 cases that the authors of qar_rviz_plugin observed in which the plugin of this stack, qar_rviz_plugin, created its hidden Displays panel, a GUI panel, again after it had been closed. The crash came within a second of the creation, every running Visualizer crashed with it, and QarOS started a Visualizer again after several seconds. It happened with the display name unchanged, with a numbered display name, with a shifted position and with a pause of 8 seconds before the start. In one longer session the Visualizer crashed five more times, each within a second of the close or the creation of a panel: twice at the close of the panel of a dialog, once at the close and the creation again of a video panel, and twice at the opening of the page of an AR tool in a URL panel. It did not crash at the other closes and creations of panels in that session. Every crash report names the same access violation at the same place inside `WebCore.dll`, the browser engine library of QarOS. The crash reports are files of the QarOS log folder that are named after the program `qar-stereokit-gui.exe`, which the authors call the Visualizer GUI process. The SDK `bin` folder holds this program next to `qar-streaming-viz.exe`, which this documentation names as the Visualizer elsewhere; how the two programs relate is not documented here. The SDK documents no rule for display names, and the defect is the Visualizer's. The plugin gives every QarOS panel that it starts for a window of RViz or for a video a display name that no earlier panel of its process used: the name that was asked for at the first start, and `<name> (2)`, `<name> (3)` and so on at the later ones. This does not prevent the crash. It does not number the panel that the Qt library of this stack, qar_qt_streaming, creates for a dialog of a streamed window, the panel that this library creates again when the size of a streamed window changes, or the URL panels of the AR tools.
- **Further limits are named where they apply.** The OpenGL frame has no field for synchronisation, the SDK exposes no field for the role of a peer, and the encoding of the depth values is not documented.

### Panel pixel coordinates and content_scale

A position on a GUI panel is reported in panel content space, the pixel raster that is drawn onto the panel. The origin is the top-left corner of the displayed image as the viewer sees it, x increases to the right and y increases downward. `QarGuiPanelPoint` holds two signed 32-bit integers, `x_pixels` and `y_pixels`, and both are clamped to the image that the renderer is currently displaying: x is in 0 to content width - 1 and y in 0 to content height - 1.

For a stream panel that image is the video exactly as its sender draws it: the renderer receives the video at the resolution of the sender, and when the renderer has scrolled the video, the position is the pixel under the finger and not a position relative to the visible part. The receiver resolution of a stream panel is taken from the layout of the video sender.

The field `content_scale` of `QarGuiPanelSize` is a zoom of the rendered raster: 1.0 fits the content across the full panel surface, 2.0 shows it at twice the size so that only half of it fits, and 0.5 shows it at half size. It changes how the content is drawn and how a contact maps back to content pixels, and it does not change `width_meters` or `height_meters`. A stream panel ignores it: its video is always drawn at the full panel width and is scrolled vertically when it is taller than the panel.

This stack sends 1.0 as `content_scale` for the panels of the Qt library and by default for URL panels, and never a resolution or pixel ratio, because a value above 1.0 would crop the content and push the position of a contact off its target. The panel of a Qt widget may be rendered at a higher pixel density than its logical size; that ratio stays on this side and is not sent.

The adapter turns a reported pixel into a position on the panel in four steps. It adds 0.5 to x and y, so that the centre of the pixel is addressed, and it divides by the content width and height that the caller of the adapter names. It flips no axis, because the origin of the header, the origin of the panel seam and the origin of Qt are all top-left with y down, and it does not apply `content_scale` again, because the renderer has already mapped the contact through it into content space. A position that lies outside the content, for example after the panel was resized under a live subscription, is clamped to the edge.

The content size is the part of the buffer that the widget covers. This stack aligns the buffer of a panel up to an even number of pixels as a precaution, because video encoders widely reject odd dimensions; the few slack pixels are background, and the position of a contact is normalised against the content size and not against the buffer.

## Rebuilding after an SDK update

The plugin is tied to the SDK in two places, and an update touches them differently. The header is compiled in: qar_ogre_adapter vendors `qar_streaming.h` and installs it into the prefix, and the plugin compiles against that copy. The library is not compiled in: nothing links an import library, and the adapter loads `qar-streaming-c.dll` at run time from the folder that `QAR_RUNTIME_BINARIES_DIR` names, which the launcher sets from `-SdkRoot` or from `QAR_SDK_ROOT`.

**The header is the same.** Compare the hashes as [Identifying an SDK drop](#identifying-an-sdk-drop) shows. When they are equal, the C API is the one that the plugin was built against and you rebuild nothing: unpack the new drop, point `QAR_SDK_ROOT` or `-SdkRoot` at it, and run the launcher without `-Confirm`. The launcher stops when the folder does not hold `bin\qar-streaming-c.dll`, and it warns when the Visualizer or the runtime launcher of QarOS is missing from it.

**The header changed.** Update the header in qar_ogre_adapter and rebuild and install the repositories in the order of [Rebuild order](#rebuild-order), the plugin last. For the plugin, `scripts\build_plugin.ps1` with `-Clean` deletes only the colcon build tree `build\colcon`, so that no object that was compiled against the old header survives, and the build then replaces `qar_rviz_streaming.dll` in the prefix. Windows refuses to replace a library that a running `rviz2.exe` has loaded, so close RViz first. The prefix is never deleted, and the script checks only that the two upstream packages exist in it and not that they are the new ones: install them again before you build the plugin.

**After the rebuild.** The build does not say whether the drop behaves, so run the launcher without `-Confirm`, then connect once and look at the first lines of the log, as [Getting started](GETTING_STARTED.md#connect-to-qaros) shows. The default target of a spatial pick, `Action Point (QarOS scene intersect)`, assumes the drop after the minimum drop or a newer one: the code states that earlier drops returned a point near the hand and not the intersection of the ray with the scene, so check a pick against the scene before you rely on it with the minimum drop. [API changes by SDK drop](#api-changes-by-sdk-drop) lists what else changed.

## The QarOS in-AR browser

The sections above state what the SDK and the measurements of the authors of qar_rviz_plugin say about the browser of a URL panel. This section holds the detail that the web panels of this repository depend on: what the engine is, what the generated pages ask of it, what is known about each of those features, what a page does when one is missing, and how to check a new SDK drop.

### The engine

The browser of a URL panel is Ultralight, which its makers describe as built on a lightweight fork of WebKit; it is not Chromium. The bin folder of the SDK holds no Chromium Embedded Framework: no file in it has cef or chrome in its name, and none has the extension pak. None of the four Ultralight libraries carries a Win32 version resource, so the engine is identified by strings in its binaries, which the commands in [Checking a new SDK drop](#checking-a-new-sdk-drop) read.

The number to remember is the WebKit version, 610, that the user agent names; the same user agent presents the engine as Safari 14.0.3. Safari 14.1 added `gap` for flex containers and Safari 15.4 added the selector `:focus-visible`, so an engine at this version has neither, which agrees with what the authors of qar_rviz_plugin measured.

### What the generated pages ask of the browser

A generated page is self-contained: it loads no external script, style sheet or font. Its script is ES5 JavaScript and opens a WebSocket to the server. Its style sheet is built on CSS custom properties and flexbox, has no hover rule, because a pointer in the air has no reliable hover state, and uses none of `:has()`, `aspect-ratio` and `backdrop-filter`, which are newer than the engine.

### What is known, feature by feature

Measured means that the authors of qar_rviz_plugin saw it with generated pages opened as URL panels. A count is the number of occurrences of a text in a library of the verified drop, 2026-09-28, as the commands below print it, and a count of 0 means that the name is not in the library. Evidenced is the authors' own list of what has been seen to work in this engine, and the code keeps to it. Not determined means that neither a measurement nor the SDK says.

| Feature | Status | Evidence | What the pages do |
|---|---|---|---|
| JavaScript | runs | measured | the script starts, and says in words when it cannot |
| WebSocket to a ws address | works | measured; the header names `Sec-WebSocket-Accept`, `Sec-WebSocket-Extensions`, `Sec-WebSocket-Key`, `Sec-WebSocket-Protocol` and `Sec-WebSocket-Version` occur in `WebCore.dll` | one socket carries every frame |
| `gap` on flex containers | not applied | measured | the script measures it, and margins stand in for it where it fails |
| `min-height: auto` on flex items | not applied | measured | the children of the column containers carry `flex-shrink: 0` |
| `:focus-visible` | not known | measured; count 0 in `WebCore.dll`, while `focus-within` has count 2 | the focus ring has a rule for `:focus` as well |
| `overscroll-behavior` and `touch-action` | not known | count 0 for both in `WebCore.dll` | the style sheet declares them, and the engine ignores the declarations |
| Inline SVG polyline | present | count 3 for `SVGPolylineElement` in `WebCore.dll` | the chart widget; the script probes for it when the model has a chart |
| `position: absolute` and `overflow: auto` | evidenced | the authors' list | the rail of the click pager is positioned absolutely, and the panel scrolls inside a body that does not |
| `position: fixed` | not evidenced | the authors' list | the style sheet has none |
| a custom property inside the pseudo-elements of a range input | not evidenced | none | pixel literals come first, and the scaled values follow in a second rule that an engine drops whole when it cannot resolve them |
| text entry in AR | not determined | none | text and number fields commit on Enter, which the script accepts as a key name or as key code 13; the steppers change a number without typing |
| a way to configure the viewport | none documented | none | the page fits itself to the viewport it is given |

### The viewport

Nothing in the SDK header says how the viewport of a URL panel follows from the metres of the panel, and the viewport that the authors of qar_rviz_plugin measured does not follow them at all. The page therefore measures the box that it was laid out into, reports it to the server in a `viewport` frame, and fits its own theme to it. [Panel sizing](ARCHITECTURE.md#panel-sizing) explains the fit, and [Frames](WIRE_PROTOCOL.md#frames) the frame.

### When the browser lacks a feature

A page tells the operator what is wrong instead of showing a blank rectangle.

- **The start-up message.** A page that has the script starts with a visible message, `This panel's script did not run`, and the last step of the script's start hides it. A browser that cannot run the script leaves the message in place. A script that finds a feature missing rewrites it to name every missing feature and the user agent of the engine, and a script that throws while it starts replaces it with `The panel runtime stopped while starting up` and the error text. [The page says that the browser is missing features](TROUBLESHOOTING.md#the-page-says-that-the-browser-is-missing-features) lists what the script probes.
- **The gap probe.** The script measures `gap` instead of guessing from the version: in a hidden flex column it lays out two rows of 1 px with a row gap of 11 px and reads the height, 13 px when gap applies and 2 px when it does not. When it does not, the script sets `data-qar-nogap` on the root element, and rules that depend on that attribute put margins where the gap would be.
- **The focus ring twice.** The focus ring is declared in a rule for `:focus` and, in a separate rule, for `:focus-visible`, because a group of selectors in which one selector is invalid is dropped as a whole.
- **Column children that do not shrink.** The engine does not apply `min-height: auto`, so the theme states what that value implies: the children of the column containers carry `flex-shrink: 0`, and a page taller than the viewport is not squeezed to fit it.

### Checking a new SDK drop

A drop has no version number and its libraries have no version resource, so the engine of a drop is identified by strings in its binaries. The three commands read the libraries in the bin folder of the drop that `QAR_SDK_ROOT` names. The first prints the user agent of the engine.

```powershell
$dll = Join-Path $env:QAR_SDK_ROOT 'bin\Ultralight.dll'
$text = [Text.Encoding]::GetEncoding('iso-8859-1').GetString([IO.File]::ReadAllBytes($dll))
[regex]::Matches($text, 'Mozilla/5\.0 \(Windows[^)]*\)[ -~]{0,120}').Value | Sort-Object -Unique
```
```text
Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/610.4.3.1.4 (KHTML, like Gecko) Ultralight/1.3.0 Version/14.0.3 Safari/610.4.3.1.4
```

The second command prints the handshake header names of the WebSocket implementation, and then how often each of seven names occurs in `WebCore.dll`: the selectors and properties that decide the rows of the table above, and the class of the SVG polyline.

```powershell
$dll = Join-Path $env:QAR_SDK_ROOT 'bin\WebCore.dll'
$text = [Text.Encoding]::GetEncoding('iso-8859-1').GetString([IO.File]::ReadAllBytes($dll))
[regex]::Matches($text, 'Sec-WebSocket-[A-Za-z]+').Value | Sort-Object -Unique
foreach ($name in 'focus-visible', 'focus-within', 'touch-action', 'overscroll-behavior', 'backdrop-filter', 'content-visibility', 'SVGPolylineElement') {
    '{0} {1}' -f $name, [regex]::Matches($text, [regex]::Escape($name)).Count
}
```
```text
Sec-WebSocket-Accept
Sec-WebSocket-Extensions
Sec-WebSocket-Key
Sec-WebSocket-Protocol
Sec-WebSocket-Version
focus-visible 0
focus-within 2
touch-action 0
overscroll-behavior 0
backdrop-filter 0
content-visibility 0
SVGPolylineElement 3
```

The third command counts the files of the bin folder that have cef or chrome in their names or the extension pak, and prints the file version of each of the four libraries, which is empty when the library has no version resource.

```powershell
$bin = Join-Path $env:QAR_SDK_ROOT 'bin'
(Get-ChildItem -LiteralPath $bin -Recurse -File | Where-Object { $_.Name -match 'cef|chrome|\.pak$' }).Count
foreach ($name in 'Ultralight', 'UltralightCore', 'WebCore', 'AppCore') {
    '{0}.dll [{1}]' -f $name, (Get-Item -LiteralPath (Join-Path $bin "$name.dll")).VersionInfo.FileVersion
}
```
```text
0
Ultralight.dll []
UltralightCore.dll []
WebCore.dll []
AppCore.dll []
```

On the verified drop the first command prints one user agent, the second prints the five header names followed by `focus-visible 0`, `focus-within 2`, `touch-action 0`, `overscroll-behavior 0`, `backdrop-filter 0`, `content-visibility 0` and `SVGPolylineElement 3`, and the third prints 0 followed by the four libraries with empty brackets. A different result changes what to check:

- A user agent with another Ultralight or WebKit version means that the rows of the table above have to be measured again with generated pages opened as URL panels, as [Open the panel in QarOS](GETTING_STARTED.md#open-the-panel-in-qaros) describes: read the start-up message of the page and the viewport that the page reports.
- A count above 0 for `focus-visible` means that the engine knows the selector; the rule for `:focus` stays harmless.
- Header names that are missing mean that the page may not connect, and a page without a WebSocket stays offline.
- Files with cef or chrome in their names mean that the browser is no longer the one that this section describes.
