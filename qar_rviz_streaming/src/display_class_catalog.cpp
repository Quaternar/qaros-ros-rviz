// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/display_class_catalog.hpp"

#include <QXmlStreamReader>

#include <algorithm>

namespace qar_rviz_streaming
{

QString
normalize_message_type(const QString & type)
{
  QString trimmed = type.trimmed();
  if (trimmed.isEmpty()) {
    return trimmed;
  }
  // "sensor_msgs/msg/PointCloud2" -> "sensor_msgs/PointCloud2"; anything already
  // in pkg/Type form is returned unchanged.
  const QStringList parts = trimmed.split('/', Qt::SkipEmptyParts);
  if (parts.size() >= 3 && parts[parts.size() - 2] == QStringLiteral("msg")) {
    return parts.front() + '/' + parts.back();
  }
  return trimmed;
}

QMap<QString, QSet<QString>>
parse_display_manifest(const QString & xml)
{
  QMap<QString, QSet<QString>> result;
  QXmlStreamReader reader(xml);

  QString current_class;
  bool in_class = false;

  while (!reader.atEnd()) {
    const auto token = reader.readNext();

    if (token == QXmlStreamReader::StartElement) {
      if (reader.name() == QStringLiteral("class")) {
        const auto attributes = reader.attributes();
        // RViz's own rule: the lookup name wins; without one the class id IS the
        // fully-qualified C++ type.
        current_class = attributes.value(QStringLiteral("name")).toString();
        if (current_class.isEmpty()) {
          current_class = attributes.value(QStringLiteral("type")).toString();
        }
        in_class = !current_class.isEmpty();
        if (in_class && !result.contains(current_class)) {
          // Record the class even if it declares no message type, so callers can
          // tell "not a topic display" from "manifest not read".
          result.insert(current_class, QSet<QString>());
        }
      } else if (in_class && reader.name() == QStringLiteral("message_type")) {
        const QString type = normalize_message_type(reader.readElementText());
        if (!type.isEmpty()) {
          result[current_class].insert(type);
        }
      }
    } else if (token == QXmlStreamReader::EndElement) {
      if (reader.name() == QStringLiteral("class")) {
        in_class = false;
        current_class.clear();
      }
    }
  }

  if (reader.hasError()) {
    // Partial results from a malformed manifest would be arbitrary; a plugin
    // whose XML does not parse is better treated as contributing nothing.
    return {};
  }
  return result;
}

void
DisplayClassCatalog::add_manifest(const QString & xml)
{
  const auto per_class = parse_display_manifest(xml);
  for (auto it = per_class.constBegin(); it != per_class.constEnd(); ++it) {
    for (const auto & type : it.value()) {
      by_type_[type].insert(it.key());
    }
  }
}

QStringList
DisplayClassCatalog::classes_for_message_type(const QString & message_type) const
{
  const auto it = by_type_.constFind(normalize_message_type(message_type));
  if (it == by_type_.constEnd()) {
    return {};
  }
  QStringList classes = QStringList(it.value().values());
  classes.sort();
  return classes;
}

namespace
{

/// @brief Tie-break rank for @ref DisplayClassCatalog::best_class_for_message_type.
/// Lower sorts first. `classes_for_message_type` is plain alphabetical (needed for
/// a stable dialog listing), which for `sensor_msgs/Image` yields
/// [Camera, DepthCloud, Image] — exactly backwards for a plain video topic:
///  - Image only needs the topic itself, so it is the right default (rank -1).
///  - Camera additionally needs camera_info + TF and is empty without them (rank 1).
///  - DepthCloud turns a DEPTH map into a point cloud: it also declares
///    sensor_msgs/Image, but for a colour video it is nonsense, and with Camera
///    demoted it would sort before "Image" and be created for a topic such as
///    /qar_test_video/image_raw (rank 2, last resort).
/// Anything not listed keeps its alphabetical position (rank 0).
int display_class_preference_rank(const QString & class_id)
{
  if (class_id == QStringLiteral("rviz_default_plugins/Image")) {
    return -1;
  }
  if (class_id == QStringLiteral("rviz_default_plugins/Camera")) {
    return 1;
  }
  if (class_id == QStringLiteral("rviz_default_plugins/DepthCloud")) {
    return 2;
  }
  return 0;
}

}  // namespace

QString
DisplayClassCatalog::best_class_for_message_type(const QString & message_type) const
{
  QStringList classes = classes_for_message_type(message_type);
  if (classes.isEmpty()) {
    return QString();
  }
  // stable_sort: only reorders classes whose rank actually differs, so ties keep
  // the alphabetical order classes_for_message_type already established.
  std::stable_sort(
    classes.begin(), classes.end(),
    [](const QString & a, const QString & b) {
      return display_class_preference_rank(a) < display_class_preference_rank(b);
    });
  return classes.front();
}

bool
is_video_display_class(const std::string & class_id)
{
  return class_id == "rviz_default_plugins/Image" || class_id == "rviz_default_plugins/Camera";
}

std::string
video_display_class_caveat(const std::string & class_id)
{
  if (class_id == "rviz_default_plugins/Camera") {
    return "Camera's 3D overlay behind the image is not included — only the raw camera "
           "topic is streamed.";
  }
  return {};
}

bool
is_image_message_type(const std::string & message_type)
{
  const QString normalized = normalize_message_type(QString::fromStdString(message_type));
  return normalized == QStringLiteral("sensor_msgs/Image") ||
         normalized == QStringLiteral("sensor_msgs/CompressedImage");
}

bool
should_skip_display_for_exposed_topic(bool expose_as_ar_tool, const std::string & display_class)
{
  return expose_as_ar_tool && is_video_display_class(display_class);
}

bool
should_skip_display_for_exposed_topic(
  bool expose_as_ar_tool, const std::string & message_type, const std::string & display_class)
{
  return expose_as_ar_tool &&
         (is_image_message_type(message_type) || is_video_display_class(display_class));
}

std::string
format_video_display_health(
  const std::string & display_name, const std::string & topic, std::size_t publishers_seen,
  const std::string & topic_status, bool enabled)
{
  if (topic.empty()) {
    return display_name + "{NOTOPIC}";
  }
  if (!enabled) {
    return display_name + "{" + topic + " OFF}";
  }
  const char * verdict = "WAITING";
  if (topic_status.empty()) {
    // No Status/Topic row to read (a display class that words its rows differently,
    // or one that has not subscribed yet): claiming WAITING would be a false alarm.
    verdict = "UNKNOWN";
  } else if (topic_status.rfind("Error", 0) == 0) {
    verdict = "ERR";
  } else if (topic_status.find("messages received") != std::string::npos) {
    // ImageTransportDisplay::incomingMessage writes "N messages received at X hz."
    // into the Topic row on every frame it takes; the row is just "OK" until then.
    verdict = "RX";
  } else if (publishers_seen == 0) {
    verdict = "NOPUB";
  }
  return display_name + "{" + topic + " pubs=" + std::to_string(publishers_seen) + " " + verdict +
         "}";
}

}  // namespace qar_rviz_streaming
