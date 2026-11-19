// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/pick_topic_choice.hpp"

#include <algorithm>
#include <utility>

namespace qar_rviz_streaming
{
namespace
{

constexpr SpatialMode kModes[] = {
  SpatialMode::Point, SpatialMode::NavGoal, SpatialMode::InitialPose, SpatialMode::Area};

bool contains(const std::vector<std::string> & list, const std::string & text)
{
  return std::find(list.begin(), list.end(), text) != list.end();
}

const GraphTopic * find_topic(const std::vector<GraphTopic> & graph, const std::string & name)
{
  for (const GraphTopic & topic : graph) {
    if (topic.name == name) {
      return &topic;
    }
  }
  return nullptr;
}

std::string join(const std::vector<std::string> & list)
{
  std::string out;
  for (const std::string & item : list) {
    if (!out.empty()) {
      out += ", ";
    }
    out += item;
  }
  return out;
}

std::vector<std::string> short_names(const std::vector<std::string> & types)
{
  std::vector<std::string> names;
  for (const std::string & type : types) {
    names.push_back(short_type(type));
  }
  return names;
}

std::vector<std::string> split(const std::string & text, char separator)
{
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (true) {
    const std::size_t end = text.find(separator, begin);
    if (end == std::string::npos) {
      parts.push_back(text.substr(begin));
      break;
    }
    parts.push_back(text.substr(begin, end - begin));
    begin = end + 1;
  }
  return parts;
}

}  // namespace

const char * to_string(PickTopicSource source)
{
  switch (source) {
    case PickTopicSource::Default:
      return "default";
    case PickTopicSource::Setting:
      return "setting";
    case PickTopicSource::Graph:
      return "graph";
    case PickTopicSource::Typed:
      return "typed";
  }
  return "default";
}

const std::vector<std::string> & pick_types_for(SpatialMode mode)
{
  static const std::vector<std::string> kPoint = {
    "geometry_msgs/msg/PointStamped", "geometry_msgs/msg/Point"};
  static const std::vector<std::string> kNavGoal = {
    "geometry_msgs/msg/PoseStamped", "geometry_msgs/msg/PoseWithCovarianceStamped",
    "geometry_msgs/msg/Pose"};
  static const std::vector<std::string> kInitialPose = {
    "geometry_msgs/msg/PoseWithCovarianceStamped", "geometry_msgs/msg/PoseStamped"};
  static const std::vector<std::string> kArea = {
    "geometry_msgs/msg/PolygonStamped", "geometry_msgs/msg/Polygon"};
  static const std::vector<std::string> kNone;
  switch (mode) {
    case SpatialMode::Point:
      return kPoint;
    case SpatialMode::NavGoal:
      return kNavGoal;
    case SpatialMode::InitialPose:
      return kInitialPose;
    case SpatialMode::Area:
      return kArea;
    case SpatialMode::Off:
      break;
  }
  return kNone;
}

const char * pick_mode_word(SpatialMode mode)
{
  switch (mode) {
    case SpatialMode::Point:
      return "point";
    case SpatialMode::NavGoal:
      return "nav_goal";
    case SpatialMode::InitialPose:
      return "initial_pose";
    case SpatialMode::Area:
      return "area";
    case SpatialMode::Off:
      break;
  }
  return "";
}

bool pick_mode_from_word(const std::string & word, SpatialMode & out)
{
  for (const SpatialMode mode : kModes) {
    if (word == pick_mode_word(mode)) {
      out = mode;
      return true;
    }
  }
  return false;
}

bool pick_topic_name_ok(const std::string & topic, std::string & out_reason)
{
  if (topic.empty()) {
    out_reason = "the topic name is empty";
    return false;
  }
  if (topic.size() > kMaxPickTopicBytes) {
    out_reason = "the topic name is longer than 255 bytes";
    return false;
  }
  if (topic[0] != '/') {
    out_reason = "the topic name must start with /";
    return false;
  }
  for (const char c : topic) {
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (!letter && !(c >= '0' && c <= '9') && c != '_' && c != '/') {
      out_reason = "the topic name may hold only letters, digits, _ and /";
      return false;
    }
  }
  if (topic.find("//") != std::string::npos) {
    out_reason = "the topic name has an empty part (//)";
    return false;
  }
  if (topic.size() > 1 && topic.back() == '/') {
    out_reason = "the topic name must not end with /";
    return false;
  }
  if (topic.size() == 1) {
    out_reason = "the topic name has no part after /";
    return false;
  }
  for (std::size_t i = 1; i < topic.size(); ++i) {
    if (topic[i - 1] == '/' && topic[i] >= '0' && topic[i] <= '9') {
      out_reason = "a part of the topic name must not start with a digit";
      return false;
    }
  }
  return true;
}

std::string short_type(const std::string & type_name)
{
  const std::size_t slash = type_name.rfind('/');
  return slash == std::string::npos ? type_name : type_name.substr(slash + 1);
}

std::string pick_type_for_topic(
  SpatialMode mode, const std::string & topic, const std::vector<GraphTopic> & graph)
{
  const std::vector<std::string> & wanted = pick_types_for(mode);
  if (wanted.empty()) {
    return std::string();
  }
  const GraphTopic * known = find_topic(graph, topic);
  if (known != nullptr) {
    for (const std::string & type : wanted) {
      if (contains(known->types, type)) {
        return type;
      }
    }
  }
  return wanted.front();
}

bool pick_topic_acceptable(
  SpatialMode mode, const std::string & topic, const std::string & type_name,
  const std::vector<GraphTopic> & graph, std::string & out_reason)
{
  const std::vector<std::string> & wanted = pick_types_for(mode);
  if (wanted.empty()) {
    out_reason = "no pick mode is armed";
    return false;
  }
  if (!pick_topic_name_ok(topic, out_reason)) {
    return false;
  }
  if (!contains(wanted, type_name)) {
    out_reason = "a " + short_type(type_name) + " is not a message that this mode publishes";
    return false;
  }
  const GraphTopic * known = find_topic(graph, topic);
  if (known != nullptr && !known->types.empty() && !contains(known->types, type_name)) {
    out_reason = "the topic carries " + join(short_names(known->types)) +
      ", this mode publishes " + join(short_names(wanted));
    return false;
  }
  return true;
}

std::map<SpatialMode, std::vector<std::string>> parse_pick_topics(
  const std::string & text, std::vector<std::string> & warnings)
{
  std::map<SpatialMode, std::vector<std::string>> result;
  if (text.empty()) {
    return result;
  }
  for (const std::string & part : split(text, ';')) {
    if (part.empty()) {
      continue;
    }
    const std::size_t equals = part.find('=');
    if (equals == std::string::npos) {
      warnings.push_back("pick topics: '" + part + "' has no '='");
      continue;
    }
    const std::string word = part.substr(0, equals);
    SpatialMode mode = SpatialMode::Off;
    if (!pick_mode_from_word(word, mode)) {
      warnings.push_back("pick topics: '" + word + "' is not a pick mode");
      continue;
    }
    for (const std::string & topic : split(part.substr(equals + 1), ',')) {
      if (topic.empty()) {
        continue;
      }
      std::string reason;
      if (!pick_topic_name_ok(topic, reason)) {
        warnings.push_back("pick topics: '" + topic + "' for " + word + " dropped: " + reason);
        continue;
      }
      std::vector<std::string> & list = result[mode];
      if (contains(list, topic)) {
        warnings.push_back("pick topics: '" + topic + "' is listed twice for " + word);
        continue;
      }
      list.push_back(topic);
    }
  }
  return result;
}

std::string format_pick_topics(const std::map<SpatialMode, std::string> & chosen)
{
  return format_pick_topics(chosen, std::map<SpatialMode, std::vector<std::string>>());
}

std::string format_pick_topics(
  const std::map<SpatialMode, std::string> & chosen,
  const std::map<SpatialMode, std::vector<std::string>> & configured)
{
  std::vector<std::pair<SpatialMode, std::vector<std::string>>> lists;
  for (const SpatialMode mode : kModes) {
    std::vector<std::string> list;
    const auto pick = chosen.find(mode);
    if (pick != chosen.end() && !pick->second.empty()) {
      list.push_back(pick->second);
    }
    const auto extra = configured.find(mode);
    if (extra != configured.end()) {
      for (const std::string & topic : extra->second) {
        if (!topic.empty() && !contains(list, topic)) {
          list.push_back(topic);
        }
      }
    }
    if (!list.empty()) {
      lists.emplace_back(mode, std::move(list));
    }
  }

  const auto render = [&lists]() {
      std::string out;
      for (const auto & entry : lists) {
        if (!out.empty()) {
          out += ';';
        }
        out += pick_mode_word(entry.first);
        out += '=';
        for (std::size_t i = 0; i < entry.second.size(); ++i) {
          if (i > 0) {
            out += ',';
          }
          out += entry.second[i];
        }
      }
      return out;
    };

  std::string text = render();
  while (text.size() > kMaxPickTopicsSettingBytes && !lists.empty()) {
    // Drop the last topic that is not the first of its mode; when there is none, the last mode.
    bool dropped = false;
    for (std::size_t i = lists.size(); i > 0 && !dropped; --i) {
      std::vector<std::string> & list = lists[i - 1].second;
      if (list.size() > 1) {
        list.pop_back();
        dropped = true;
      }
    }
    if (!dropped) {
      lists.pop_back();
    }
    text = render();
  }
  return text;
}

std::vector<PickTopicOption> pick_topic_options(
  SpatialMode mode, const std::vector<std::string> & configured,
  const std::vector<GraphTopic> & graph, const std::string & chosen)
{
  std::vector<PickTopicOption> options;
  const std::vector<std::string> & wanted = pick_types_for(mode);
  if (wanted.empty()) {
    return options;
  }
  const auto listed = [&options](const std::string & topic) {
      return std::any_of(
        options.begin(), options.end(),
        [&topic](const PickTopicOption & option) {return option.topic == topic;});
    };
  const auto add = [&](const std::string & topic, PickTopicSource source) {
      if (options.size() >= kMaxPickTopicOptions || listed(topic)) {
        return;
      }
      options.push_back(PickTopicOption{topic, pick_type_for_topic(mode, topic, graph), source});
    };

  add(default_publish_config(publish_target_for(mode)).topic, PickTopicSource::Default);
  for (const std::string & topic : configured) {
    std::string reason;
    if (pick_topic_name_ok(topic, reason)) {
      add(topic, PickTopicSource::Setting);
    }
  }
  if (kPickTopicsFromGraph) {
    std::vector<std::string> names;
    for (const GraphTopic & topic : graph) {
      const bool fits = std::any_of(
        wanted.begin(), wanted.end(),
        [&topic](const std::string & type) {return contains(topic.types, type);});
      std::string reason;
      if (fits && pick_topic_name_ok(topic.name, reason)) {
        names.push_back(topic.name);
      }
    }
    std::sort(names.begin(), names.end());
    for (const std::string & name : names) {
      add(name, PickTopicSource::Graph);
    }
  }

  std::string reason;
  if (!chosen.empty() && pick_topic_name_ok(chosen, reason)) {
    const auto found = std::find_if(
      options.begin(), options.end(),
      [&chosen](const PickTopicOption & option) {return option.topic == chosen;});
    const std::size_t at = static_cast<std::size_t>(found - options.begin());
    if (found == options.end()) {
      options.insert(
        options.begin() + 1,
        PickTopicOption{chosen, pick_type_for_topic(mode, chosen, graph), PickTopicSource::Typed});
    } else if (at >= kPickTopicSlots) {
      std::rotate(options.begin() + 1, found, found + 1);
    }
    if (options.size() > kMaxPickTopicOptions) {
      options.resize(kMaxPickTopicOptions);
    }
  }
  return options;
}

}  // namespace qar_rviz_streaming
