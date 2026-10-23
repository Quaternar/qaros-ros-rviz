// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__PANEL_GROUND_RULE_HPP_
#define QAR_RVIZ_STREAMING__PANEL_GROUND_RULE_HPP_

/// @file panel_ground_rule.hpp
/// @brief The rule that keeps an AR panel above the ground of the RViz scene, how the panels with
/// a fixed top edge are hung, and the brightness field of the heartbeat line. Pure: the standard
/// library and qar_ar_core only.
///
/// ## The ground
///
/// The RViz scene is streamed as app content. Its ground is the plane `y = 0` of app content
/// space, which is the plane `z = 0` of the ROS world and so the plane of the RViz grid. QarOS
/// puts that space into the room with the live scene placement
/// (@ref qar_ar_core::ScenePlacement: the app scale, the app pose and the pose of the app
/// volume), and the user can move, turn and scale the scene by grabbing it. The ground is
/// therefore not a fixed height of the room. @ref scene_ground_room_y maps the plane through the
/// placement and reads its height on the vertical line through a point of the room.
///
/// ## The rule
///
/// The lower edge of every AR panel lies at least a margin above that ground.
/// @ref keep_above_ground only raises a pose: it never lowers one, it keeps the orientation and
/// it keeps x and z. The pose of a QarOS panel is the midpoint of its top edge, and the panel
/// hangs below it; an upright panel of height `h` therefore reaches down to `top.y - h`.
///
/// All heights are in room space (+Y up), in metres.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "qar_ar_core/panel_types.hpp"       // PanelStreamDiagnostics
#include "qar_ar_core/qar_streaming_c.hpp"  // QarPose
#include "qar_ar_core/spatial_frames.hpp"   // ScenePlacement

namespace qar_rviz_streaming
{

/// The default distance between the ground and the lower edge of a panel.
constexpr float kPanelGroundMarginM = 0.10f;

/// The smallest vertical share of the ground's unit normal for which the ground is read as a
/// floor: cos(60 degrees). A scene that is tilted further has no floor to keep a panel above, and
/// @ref scene_ground_room_y answers with its fallback.
constexpr float kGroundMinNormalY = 0.5f;

/// @brief Room height (m) of the RViz ground - the plane app content `y = 0` - on the vertical
/// line through the room point (@p room_x, @p room_z), under the live scene placement.
///
/// The plane passes through the app origin and has the scene's up axis as its normal, both
/// mapped to the room with @p placement. A scene that is only moved, turned about the vertical
/// axis or scaled gives one height everywhere; a scene that is tilted gives a height that varies
/// with @p room_x and @p room_z.
/// @return @p fallback_room_y when the placement is not known (@ref
/// qar_ar_core::ScenePlacement::known is false), when the plane is tilted by more than 60 degrees
/// (@ref kGroundMinNormalY) or when a number is not finite.
float scene_ground_room_y(
  const qar_ar_core::ScenePlacement & placement, float room_x, float room_z,
  float fallback_room_y);

/// @brief The pose that keeps a panel above the ground.
///
/// @p top_centre is the room-space midpoint of the top edge of a panel of @p height_m hanging
/// below it. The pose is raised until `top.y - height_m >= ground_room_y + margin_m`. It is never
/// lowered; the orientation, x and z are unchanged. A negative height counts as zero, and a
/// pose or a number that is not finite is returned unchanged.
QarPose keep_above_ground(
  const QarPose & top_centre, float height_m, float ground_room_y,
  float margin_m = kPanelGroundMarginM);

/// @brief How far @ref keep_above_ground raises @p top_centre (m); 0 when it does not.
float ground_raise_m(
  const QarPose & top_centre, float height_m, float ground_room_y,
  float margin_m = kPanelGroundMarginM);

/// ## Panels with a fixed top edge
///
/// Docks, the panel itself, video panels, the Displays copy and the AR tools hang from a fixed
/// height of the room: their top edge is at @ref kFixedPanelTopM, and @ref keep_above_ground then
/// raises the ones that would reach below the ground. A scene that was moved up or down leaves
/// them where they were. How they are hung is a choice: `room` keeps the top edge at a fixed
/// height of the room, `ground` puts it @ref kFixedPanelTopM above the RViz ground, wherever the
/// scene has been moved to.
enum class FixedPoseMode : std::uint8_t { Room = 0, Ground };

/// "room" or "ground".
const char * to_string(FixedPoseMode mode);

/// The top edge of a panel with a fixed top edge, in metres above the room origin (mode `room`)
/// or above the ground (mode `ground`).
constexpr float kFixedPanelTopM = 1.5f;

/// What @ref parse_fixed_pose_mode made of a text.
struct FixedPoseChoice
{
  FixedPoseMode mode = FixedPoseMode::Room;
  /// Not empty when the text was not acceptable: the sentence for the log.
  std::string warning;
};

/// @brief `room` or `ground`, in any case and with blanks around it. A null or blank text, and
/// the default, is `room`; any other text is `room` as well, with a warning that names @p source.
FixedPoseChoice parse_fixed_pose_mode(
  const char * value, const char * source = "QAR_PANEL_FIXED_POSES");

/// @brief The top edge, in room metres, of a panel with a fixed top edge at the room point
/// (@p room_x, @p room_z): @ref kFixedPanelTopM for `room`; for `ground` the room height of the
/// RViz ground there (@ref scene_ground_room_y, with @p fallback_ground_y when the placement is
/// not known) plus @ref kFixedPanelTopM.
float fixed_panel_top_y(
  FixedPoseMode mode, const qar_ar_core::ScenePlacement & placement, float room_x, float room_z,
  float fallback_ground_y);

/// ## The frame of an AR panel
///
/// QarOS draws the video of a stream panel inside a frame of its own: four insets in metres (a
/// title bar on top in the QarOS desktop Visualizer) and a fit margin, which is how much taller
/// than the video the area under the insets is made, so that the video fits it and QarOS needs no
/// scroll track. The library makes an AR panel as tall as its video needs inside that frame, plus
/// the margin, and gives a dialog's panel the width its height needs (qar_qt_streaming's
/// panel_geometry.hpp). The panel states the frame once, at the first connect, from the
/// environment variable `QAR_PANEL_AR_FRAME` (`<top>,<bottom>,<left>,<right>[,<fit margin>]` in
/// metres), or, when that is not set, from `QAR_PANEL_AR_HEADROOM`, one number that sets the top
/// inset and leaves the rest.

/// The largest inset the variables may state, in metres.
constexpr double kMaxArFrameInsetM = 0.5;

/// The largest fit margin the variables may state, in metres.
constexpr double kMaxArFitMarginM = 0.05;

/// The four insets of a frame and its fit margin, in metres.
struct ArFrameInsets
{
  double top_m = 0.0;
  double bottom_m = 0.0;
  double left_m = 0.0;
  double right_m = 0.0;
  double fit_margin_m = 0.0;
};

/// Which variable the frame came from.
enum class ArFrameSource : std::uint8_t
{
  Default = 0,  ///< neither variable is set, or the one that is set was refused
  Frame,        ///< QAR_PANEL_AR_FRAME
  Headroom,     ///< QAR_PANEL_AR_HEADROOM: the top inset only
};

/// "default", "QAR_PANEL_AR_FRAME" or "QAR_PANEL_AR_HEADROOM (the top inset only)".
const char * to_string(ArFrameSource source);

/// What @ref parse_ar_frame made of the two texts.
struct ArFrameChoice
{
  /// The frame in metres: the stated one, or the default.
  ArFrameInsets insets;
  /// The variable the insets came from.
  ArFrameSource source = ArFrameSource::Default;
  /// True when QAR_PANEL_AR_HEADROOM was set as well but QAR_PANEL_AR_FRAME took its place.
  bool headroom_ignored = false;
  /// Not empty when a text was not acceptable: the sentence for the log.
  std::string warning;
};

/// @brief The frame that the two variables state. @p frame_text is QAR_PANEL_AR_FRAME: four
/// decimal numbers from 0 to @ref kMaxArFrameInsetM, `top,bottom,left,right`, and optionally a
/// fifth, the fit margin, from 0 to @ref kMaxArFitMarginM (without it the margin is the one of
/// @p defaults), with a point as the separator and blanks around every one allowed.
/// @p headroom_text is QAR_PANEL_AR_HEADROOM: one such inset number, the top inset, the rest
/// being those of @p defaults. A null or blank text counts as not set. @p headroom_text is read
/// only when @p frame_text is not set. Neither set gives @p defaults, and so does a text that is
/// not acceptable, with a warning that names its variable.
ArFrameChoice parse_ar_frame(
  const char * frame_text, const char * headroom_text, const ArFrameInsets & defaults);

/// ## The size of the app volume
///
/// The app volume is the box in the room that the RViz scene is streamed into: a cube whose edge
/// is `QAR_PANEL_VOLUME_SIZE` when that is stated. The box covers `edge / app_scale` metres of the
/// scene, so when only the scale `QAR_PANEL_VOLUME_SCALE` is stated the edge follows it: the
/// default edge times the scale, which keeps the box holding the same part of the scene.

/// Where the edge of the app volume came from.
enum class AppVolumeSizeSource : std::uint8_t
{
  Default = 0,  ///< neither the size nor the scale is stated
  Stated,       ///< the size is stated (it wins over the scale)
  FromScale,    ///< only the scale is stated: the edge is the default edge times the scale
};

/// The edge of the app volume and where it came from.
struct AppVolumeSize
{
  float edge_m = 0.0f;
  AppVolumeSizeSource source = AppVolumeSizeSource::Default;
};

/// @brief The edge of the app volume, in metres. A stated size or scale that is not finite and
/// positive counts as not stated.
AppVolumeSize choose_app_volume_size(
  std::optional<float> stated_edge_m, std::optional<float> stated_scale, float default_edge_m);

/// ## The render device
///
/// The scene that is streamed is rendered on the graphics card RViz's OpenGL context is made on,
/// which on a machine with more than one card is not always the one that is expected. The panel
/// names the render system and the device once, at startup, in one line.

/// @brief The line that names the render system and its device: `RENDER: <render system>, device
/// '<device>'`. A null or empty @p render_system (the renderer has no render system yet, as in a
/// preview or a test) gives `RENDER: no render system yet`; a null or empty @p device is shown as
/// `<unknown>`. Only printable ASCII goes into the line.
std::string render_device_line(const char * render_system, const char * device);

/// The distance in x between two neighbouring slots of the panels the operator streams. The
/// panels are about 0.57 to 0.60 m wide, so a smaller step would let neighbours overlap.
constexpr float kPanelSlotStepM = 0.65f;

/// The x of the panel in slot @p slot (0 is the first).
constexpr float panel_slot_x(std::size_t slot)
{
  return kPanelSlotStepM * static_cast<float>(slot);
}

/// ## The brightness field of the heartbeat
///
/// The part of every streamed panel in the heartbeat line ends with the brightness of the frame it
/// sent last. A panel that shows black in AR while its frames carry content is a receiver problem,
/// one whose frames are black is a capture problem, and this field is where the log tells them
/// apart. A tool reads the field from the log, so its format is fixed.

/// @brief ` lum=<all>/<top>/<rest> a=<alpha>`: the mean luminance of the frame over all rows, over
/// the top rows and over the other rows, and its mean alpha, each in whole numbers (halves round
/// up), with a blank in front. ` lum=-` while nothing has been measured, that is, when any of the
/// three luminance values is negative or not a number. The ` a=<alpha>` part is left out while the
/// alpha has not been measured.
std::string brightness_field(const qar_ar_core::PanelStreamDiagnostics & panel);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__PANEL_GROUND_RULE_HPP_
