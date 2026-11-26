// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__PICK_TOPIC_CHOICE_HPP_
#define QAR_RVIZ_STREAMING__PICK_TOPIC_CHOICE_HPP_

/// @file pick_topic_choice.hpp
/// @brief Which ROS topic a spatial pick is published to: the options of a pick mode, the check
/// of a name, the setting text that keeps the choice. Pure: standard library,
/// spatial_interaction.hpp and spatial_publish.hpp only.
///
/// Each pick mode has a conventional topic (`/clicked_point`, `/goal_pose`, `/initialpose`,
/// `/qar/selected_area`; see spatial_publish.hpp). The operator can choose another one, on the
/// desktop and on the AR control page. A choice has to leave a pick where the page said it would,
/// so a topic is accepted only when it has a legal name and its type, as the ROS graph reports it,
/// is one that the mode can publish.
///
/// ## The options of a mode
///
///  1. the conventional topic of the mode;
///  2. the topics of the setting for that mode, in the order of the setting;
///  3. when @ref kPickTopicsFromGraph is true, every topic of the ROS graph that has a type the
///     mode can publish, sorted by name.
///
/// A name that appears twice is listed once, at its first place. The list holds at most
/// @ref kMaxPickTopicOptions entries; the AR control page shows the first @ref kPickTopicSlots.
///
/// ## The setting text
///
/// `point=/a,/b;nav_goal=/c;initial_pose=/d;area=/e`: per mode a comma separated list of topics,
/// the modes separated by semicolons. The first topic of a mode is the one in force.

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "qar_rviz_streaming/ar_control_panel_model.hpp"
#include "qar_rviz_streaming/spatial_interaction.hpp"
#include "qar_rviz_streaming/spatial_publish.hpp"

namespace qar_rviz_streaming
{

/// The switches of the AR control page.
constexpr std::size_t kPickTopicSlots = ar_control::kPickTopicSlots;
/// The entries of the desktop's drop-down.
constexpr std::size_t kMaxPickTopicOptions = 16;
/// Whether the options include the topics of the ROS graph. One switch for the whole unit.
constexpr bool kPickTopicsFromGraph = true;
/// The longest topic name, in bytes.
constexpr std::size_t kMaxPickTopicBytes = 255;
/// The longest text of the setting, in bytes.
constexpr std::size_t kMaxPickTopicsSettingBytes = 1000;

/// A topic of the ROS graph and the message types it carries.
struct GraphTopic
{
  std::string name;
  std::vector<std::string> types;

  bool operator==(const GraphTopic & other) const
  {
    return name == other.name && types == other.types;
  }
};

/// Where an option comes from; the word is part of a log line.
enum class PickTopicSource : std::uint8_t { Default = 0, Setting, Graph, Typed };
/// "default", "setting", "graph", "typed"
const char * to_string(PickTopicSource source);

struct PickTopicOption
{
  std::string topic;
  std::string type_name;  ///< the full type name a pick is published as
  PickTopicSource source = PickTopicSource::Default;
};

/// The message types a mode can publish, the first being the type of its conventional topic:
/// Point: PointStamped, Point; NavGoal: PoseStamped, PoseWithCovarianceStamped, Pose;
/// InitialPose: PoseWithCovarianceStamped, PoseStamped; Area: PolygonStamped, Polygon; Off: none.
const std::vector<std::string> & pick_types_for(SpatialMode mode);

/// The word of a mode in the setting text and in log lines: "point", "nav_goal", "initial_pose",
/// "area"; empty for Off.
const char * pick_mode_word(SpatialMode mode);
/// @return false (and @p out unchanged) unless @p word is one of the four words.
bool pick_mode_from_word(const std::string & word, SpatialMode & out);

/// "point=/a,/b;nav_goal=/c;initial_pose=/d;area=/e" -> the topics per mode. An entry that
/// cannot be used (an unknown mode word, a name that is not a legal topic name, a repeated topic,
/// a part without "=") is dropped and says why in @p warnings.
std::map<SpatialMode, std::vector<std::string>> parse_pick_topics(
  const std::string & text, std::vector<std::string> & warnings);

/// The setting text that keeps @p chosen: per mode its topic. A mode without an entry, or whose
/// topic is empty, is left out.
std::string format_pick_topics(const std::map<SpatialMode, std::string> & chosen);

/// As above, and the other topics that @p configured holds for a mode follow its chosen one, so
/// that saving a choice keeps the list of the setting. The text never exceeds
/// @ref kMaxPickTopicsSettingBytes: topics that do not fit are left out, and the first topic of
/// a mode is the last of its mode to be left out.
std::string format_pick_topics(
  const std::map<SpatialMode, std::string> & chosen,
  const std::map<SpatialMode, std::vector<std::string>> & configured);

/// The options of @p mode, as described in the file comment. Empty for Off.
///
/// @p chosen is the topic in force, when it is not the conventional one. A page shows only the
/// first @ref kPickTopicSlots options, so a chosen topic that lies beyond them, or that the
/// options do not hold (a name that was typed), takes the place after the conventional topic and
/// the topics after that move down by one. A typed name is listed as PickTopicSource::Typed.
std::vector<PickTopicOption> pick_topic_options(
  SpatialMode mode, const std::vector<std::string> & configured,
  const std::vector<GraphTopic> & graph, const std::string & chosen = std::string());

/// A name the plugin may publish a pick to: a global ROS name ("/" first; letters, digits, "_" and
/// "/"; no "//", no trailing "/", no part that starts with a digit; at most 255 bytes), whose
/// graph types, when the graph lists it with types, include @p type_name, and @p type_name one of
/// pick_types_for(@p mode).
/// @return false and the words for the operator in @p out_reason otherwise.
bool pick_topic_acceptable(
  SpatialMode mode, const std::string & topic, const std::string & type_name,
  const std::vector<GraphTopic> & graph, std::string & out_reason);

/// The type to publish on @p topic for @p mode: the type of the graph when it is one of the
/// mode's types (the first of the mode's list that the topic carries), else the first type of the
/// mode.
std::string pick_type_for_topic(
  SpatialMode mode, const std::string & topic, const std::vector<GraphTopic> & graph);

/// "geometry_msgs/msg/PoseStamped" -> "PoseStamped"
std::string short_type(const std::string & type_name);

/// The name check alone: true for a legal topic name, else the reason in @p out_reason.
bool pick_topic_name_ok(const std::string & topic, std::string & out_reason);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__PICK_TOPIC_CHOICE_HPP_
