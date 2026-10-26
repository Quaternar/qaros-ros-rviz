// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__DISPLAY_CLASS_CATALOG_HPP_
#define QAR_RVIZ_STREAMING__DISPLAY_CLASS_CATALOG_HPP_

/// @file display_class_catalog.hpp
/// @brief Maps a ROS message type to the RViz Display classes that can show it.
///
/// ## Why this is re-implemented rather than reused
///
/// RViz2 already does exactly this, in `rviz_common::DisplayFactory::getMessageTypes()`
/// — but `display_factory.hpp` lives in rviz_common's `src/` and is **NOT
/// installed**. From an out-of-tree plugin, `DisplayContext::getDisplayFactory()`
/// returns a pointer to an incomplete type, so none of its methods are callable.
/// The mapping therefore has to be rebuilt from the same source of truth RViz
/// uses: the pluginlib manifest XML, whose `<class name=...>` elements carry
/// `<message_type>` children.
///
/// Parsing is done with QXmlStreamReader rather than tinyxml2 (which RViz uses
/// internally but the distro does not expose to us) — a real streaming XML
/// parser, already available through the Qt dependency this package has anyway.
///
/// QtCore ONLY here: no rclcpp, no rviz_common, no pluginlib, so the parsing
/// rules are unit-tested in the plain-CMake build against fixture XML instead of
/// needing a live plugin environment. The panel feeds it manifest paths obtained
/// from `pluginlib::ClassLoader`.

#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <string>

namespace qar_rviz_streaming
{

/// @brief Normalize a message type to `pkg/Type` for comparison.
///
/// ROS 2 reports `sensor_msgs/msg/PointCloud2` and the RViz manifests use the
/// same form, but ROS 1-era manifests (and hand-written ones) say
/// `sensor_msgs/PointCloud2`. Collapsing both to `pkg/Type` makes the lookup
/// insensitive to which convention a plugin author used.
QString normalize_message_type(const QString & type);

/// @brief Message types declared by each class in one pluginlib manifest.
///
/// Understands both accepted manifest roots (`<library>` and a `<class_libraries>`
/// wrapper around several `<library>` elements), matching RViz's own reader.
/// Classes without a `name` attribute fall back to their `type` attribute, again
/// as RViz does. Malformed XML yields an empty map rather than throwing.
QMap<QString, QSet<QString>> parse_display_manifest(const QString & xml);

/// @brief Reverse index: message type -> the Display class ids that accept it.
class DisplayClassCatalog
{
public:
  /// @brief Merge one manifest's declarations into the index.
  void add_manifest(const QString & xml);

  /// @brief Display class ids that can show @p message_type, sorted.
  ///
  /// Sorted so the dialog offers a stable choice; several displays legitimately
  /// accept one type (e.g. PointCloud2 is handled by both PointCloud2 and, via
  /// a different plugin, a costmap view).
  QStringList classes_for_message_type(const QString & message_type) const;

  /// @brief The single best Display class for @p message_type, or an empty
  /// string when nothing handles it. First of @ref classes_for_message_type.
  QString best_class_for_message_type(const QString & message_type) const;

  /// @brief Whether any type at all was indexed (i.e. discovery worked).
  bool empty() const { return by_type_.isEmpty(); }

  /// @brief Number of distinct message types indexed.
  int type_count() const { return by_type_.size(); }

private:
  QMap<QString, QSet<QString>> by_type_;
};

/// @brief Whether @p class_id is an RViz Display class that paints into a
/// NATIVE child window `QWidget::render()` cannot capture, for which the
/// panel substitutes the display's own ROS topic into the AR panel directly
/// (variant (b) of the video-panel feature — see `attachVideoRegions` in
/// qar_streaming_panel.cpp).
///
/// True for `"rviz_default_plugins/Image"` and `"rviz_default_plugins/Camera"`
/// (the class ids in rviz_default_plugins' plugins_description.xml). Both
/// displays call `setAssociatedWidget()` with an `rviz_common::RenderPanel`
/// that embeds Ogre's render window via `QWidget::createWindowContainer`
/// (render_panel.cpp in rviz_common, image_display.cpp and camera_display.cpp
/// in rviz_default_plugins; https://github.com/ros2/rviz/tree/humble).
bool is_video_display_class(const std::string & class_id);

/// @brief A one-sentence caveat to log/show alongside the region substitution
/// for @p class_id, or empty if @p class_id has none.
///
/// Camera's render panel additionally overlays the rendered 3D scene BEHIND
/// the image (RViz's "look through the camera, annotated with the scene"
/// feature); substituting the raw camera topic in AR omits that overlay,
/// since only the topic's own pixels are ever streamed. Image has no such
/// gap — its render panel shows nothing but the topic's pixels already.
std::string video_display_class_caveat(const std::string & class_id);

/// @brief Whether @p message_type is one of the two image message types the AR
/// video panel streams: `sensor_msgs/msg/Image` or `sensor_msgs/msg/CompressedImage`
/// (either the ROS 2 `pkg/msg/Type` or the short `pkg/Type` spelling).
///
/// Exact match on the normalized type — `sensor_msgs/msg/CameraInfo` or
/// `sensor_msgs/msg/PointCloud2` are not image types.
bool is_image_message_type(const std::string & message_type);

/// @brief Whether "expose as a QAR AR tool" should suppress RViz's own
/// Display for @p display_class rather than create it alongside the AR panel.
///
/// Pure decision, no Qt/RViz/ROS objects touched: true only when the topic is
/// both being exposed (@p expose_as_ar_tool) and would otherwise get one of the
/// @ref is_video_display_class displays, whose native render window the AR panel
/// makes redundant (and which, for Camera without camera_info/TF, is normally
/// empty anyway). Every other combination — not exposed, or a display that isn't
/// Image/Camera (e.g. PointCloud2, which has no associated widget and is useful
/// on its own dock even when the topic is also exposed) — keeps creating the
/// display.
///
/// Prefer the three-argument overload, which also looks at the message type:
/// this one decides on the display class alone, so it misses an image topic
/// whose chosen class is neither Image nor Camera (a DepthCloud, say).
bool should_skip_display_for_exposed_topic(
  bool expose_as_ar_tool, const std::string & display_class);

/// @brief Same decision, but an exposed IMAGE topic never gets any RViz display.
///
/// True when @p expose_as_ar_tool and either @p message_type is an
/// @ref is_image_message_type (sensor_msgs/Image or CompressedImage — whatever
/// display class the catalog picked for it) or @p display_class is an
/// @ref is_video_display_class. Exposing a topic as an AR web panel must not
/// add an RViz display that was not asked for, and an image topic can be offered
/// a class that is neither Image nor Camera (DepthCloud also declares
/// sensor_msgs/Image), which a decision on the display class alone would let
/// through. A non-image topic (PointCloud2, ...) still keeps its display — those
/// are useful on their own dock.
bool should_skip_display_for_exposed_topic(
  bool expose_as_ar_tool, const std::string & message_type, const std::string & display_class);

/// @brief One heartbeat field describing how an RViz Image/Camera display is
/// doing, e.g. `Image{/cam/image_raw pubs=1 WAITING}`.
///
/// Diagnostic only. RViz paints "No Image" (rviz_rendering's no_image.png) until
/// the display's own subscription delivers its first frame, and nothing else
/// says why: the display's Status row is visible only to someone looking at the
/// Displays panel. The two things that decide where to look are whether RViz's
/// OWN node sees a publisher and whether the display has received anything; both
/// are pure inputs here (@p publishers_seen from RViz's node graph,
/// @p topic_status the text of the display's Status/Topic row), so the wording is
/// unit-tested without RViz or ROS.
///
/// The verdict token, first match wins:
///  - `NOTOPIC` no topic is set.
///  - `OFF`   the display is disabled (it holds no subscription, by design).
///  - `UNKNOWN` the display has no readable Status/Topic row (empty text).
///  - `ERR`   the Topic status row starts with "Error" (subscribing failed).
///  - `RX`    the row reads "N messages received ..." (frames reach the display).
///  - `NOPUB` the display is subscribed but RViz's node sees no publisher.
///  - `WAITING` a publisher is visible yet the display has received nothing
///    (matched on the writer's side, silent in RViz).
std::string format_video_display_health(
  const std::string & display_name, const std::string & topic, std::size_t publishers_seen,
  const std::string & topic_status, bool enabled);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__DISPLAY_CLASS_CATALOG_HPP_
