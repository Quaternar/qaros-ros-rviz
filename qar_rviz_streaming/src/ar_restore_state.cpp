// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_restore_state.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <utility>

namespace qar_rviz_streaming
{

namespace
{

constexpr std::size_t kMaxKeyBytes = 256;
constexpr std::size_t kMaxNameBytes = 256;
constexpr std::size_t kMaxTypeBytes = 128;

constexpr const char * kVersionField = "Version";
constexpr const char * kPanelsField = "Panels";
constexpr const char * kToolsField = "Tools";
constexpr const char * kWatchedField = "Watched Topics";

std::string trimmed(const std::string & text)
{
  const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
  const auto first = std::find_if_not(text.begin(), text.end(), space);
  if (first == text.end()) {
    return std::string();
  }
  const auto last = std::find_if_not(text.rbegin(), text.rend(), space).base();
  return std::string(first, last);
}

std::string lowered(std::string text)
{
  for (char & c : text) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return text;
}

/// @p text for a sentence: control characters become spaces and a long text is cut.
std::string printable(const std::string & text)
{
  std::string out;
  for (const char c : text) {
    out.push_back(static_cast<unsigned char>(c) < 0x20 || c == 0x7f ? ' ' : c);
    if (out.size() >= 40) {
      out += "...";
      break;
    }
  }
  return out;
}

bool has_control(const std::string & text)
{
  return std::any_of(text.begin(), text.end(), [](char c) {
        return static_cast<unsigned char>(c) < 0x20 || c == 0x7f;
      });
}

bool has_space_or_control(const std::string & text)
{
  return std::any_of(text.begin(), text.end(), [](char c) {
        return static_cast<unsigned char>(c) <= 0x20 || c == 0x7f;
      });
}

bool parse_bool(const std::string & text, bool & out)
{
  const std::string word = lowered(trimmed(text));
  if (word == "true" || word == "1") {
    out = true;
    return true;
  }
  if (word == "false" || word == "0") {
    out = false;
    return true;
  }
  return false;
}

/// The text of @p field in @p record: null when the record has no such field.
const std::string * field_of(const ConfigRecord & record, const char * field)
{
  const auto found = record.find(field);
  return found == record.end() ? nullptr : &found->second;
}

/// One sentence for each field of @p record that is not in @p known (none for a newer version:
/// its fields are not ours to judge).
void warn_unknown_fields(
  const ConfigRecord & record, std::initializer_list<const char *> known, const char * list,
  std::size_t index, bool newer, std::vector<std::string> & warnings)
{
  if (newer) {
    return;
  }
  for (const auto & entry : record) {
    const bool is_known = std::any_of(known.begin(), known.end(), [&entry](const char * name) {
          return entry.first == name;
        });
    if (!is_known) {
      warnings.push_back(
        std::string("AR State: ") + list + " record " + std::to_string(index) + ": field '" +
        printable(entry.first) + "' is not known and is ignored");
    }
  }
}

std::string skipped(const char * list, std::size_t index, const std::string & why)
{
  return std::string("AR State: ") + list + " record " + std::to_string(index) + " skipped: " +
         why;
}

void read_panels(
  const ArStateText & text, bool newer, ArRestorePlan & plan, std::vector<std::string> & warnings)
{
  std::size_t index = 0;
  for (const ConfigRecord & record : text.panels) {
    ++index;
    if (index > kArStateMaxRecords) {
      warnings.push_back(
        "AR State: more than " + std::to_string(kArStateMaxRecords) +
        " Panels records; the rest are ignored");
      break;
    }
    warn_unknown_fields(record, {"Key", "On", "Size"}, kPanelsField, index, newer, warnings);
    ArPanelState panel;
    const std::string * key = field_of(record, "Key");
    if (key == nullptr || trimmed(*key).empty()) {
      warnings.push_back(skipped(kPanelsField, index, "it has no Key"));
      continue;
    }
    panel.key = trimmed(*key);
    if (panel.key.size() > kMaxKeyBytes || has_control(panel.key)) {
      warnings.push_back(skipped(kPanelsField, index, "the Key is not usable"));
      continue;
    }
    if (!ar_state_panel_key(panel.key)) {
      warnings.push_back(
        skipped(kPanelsField, index, "'" + printable(panel.key) + "' is not an RViz panel"));
      continue;
    }
    if (const std::string * on = field_of(record, "On")) {
      if (!parse_bool(*on, panel.on)) {
        warnings.push_back(
          skipped(kPanelsField, index, "On '" + printable(*on) + "' is not true or false"));
        continue;
      }
    }
    if (const std::string * size = field_of(record, "Size")) {
      ArCopySize parsed = ArCopySize::Enlarged;
      if (!ar_copy_size_from_word(*size, parsed)) {
        warnings.push_back(
          skipped(
            kPanelsField, index, "Size '" + printable(*size) + "' is not enlarged or native"));
        continue;
      }
      panel.size = parsed;
    }
    if (find_ar_panel_state(plan, panel.key) != nullptr) {
      warnings.push_back(
        skipped(kPanelsField, index, "'" + printable(panel.key) + "' is listed before"));
      continue;
    }
    plan.panels.push_back(std::move(panel));
  }
}

void read_tools(
  const ArStateText & text, bool newer, ArRestorePlan & plan, std::vector<std::string> & warnings)
{
  std::size_t index = 0;
  for (const ConfigRecord & record : text.tools) {
    ++index;
    if (index > kArStateMaxRecords) {
      warnings.push_back(
        "AR State: more than " + std::to_string(kArStateMaxRecords) +
        " Tools records; the rest are ignored");
      break;
    }
    warn_unknown_fields(
      record, {"Kind", "Name", "Type", "Writable", "Shown"}, kToolsField, index, newer, warnings);
    ArToolState tool;
    const std::string * kind = field_of(record, "Kind");
    if (kind == nullptr || !ros_entity_kind_from_string(lowered(trimmed(*kind)), tool.kind)) {
      warnings.push_back(
        skipped(
          kToolsField, index,
          kind == nullptr ? std::string("it has no Kind")
          : "Kind '" + printable(*kind) + "' is not topic, service or action"));
      continue;
    }
    const std::string * name = field_of(record, "Name");
    const std::string * type = field_of(record, "Type");
    if (name == nullptr || trimmed(*name).empty() || type == nullptr || trimmed(*type).empty()) {
      warnings.push_back(skipped(kToolsField, index, "it has no Name or no Type"));
      continue;
    }
    tool.name = trimmed(*name);
    tool.type = trimmed(*type);
    if (tool.name.size() > kMaxNameBytes || has_space_or_control(tool.name) ||
      tool.type.size() > kMaxTypeBytes || has_space_or_control(tool.type) ||
      tool.type.find('/') == std::string::npos)
    {
      warnings.push_back(
        skipped(kToolsField, index, "'" + printable(tool.name) + "' has no usable Name or Type"));
      continue;
    }
    if (const std::string * writable = field_of(record, "Writable")) {
      if (!parse_bool(*writable, tool.writable)) {
        warnings.push_back(
          skipped(
            kToolsField, index, "Writable '" + printable(*writable) + "' is not true or false"));
        continue;
      }
    }
    if (const std::string * shown = field_of(record, "Shown")) {
      if (!parse_bool(*shown, tool.shown)) {
        warnings.push_back(
          skipped(kToolsField, index, "Shown '" + printable(*shown) + "' is not true or false"));
        continue;
      }
    }
    const bool listed = std::any_of(
      plan.tools.begin(), plan.tools.end(), [&tool](const ArToolState & other) {
        return other.kind == tool.kind && other.name == tool.name;
      });
    if (listed) {
      warnings.push_back(
        skipped(kToolsField, index, "'" + printable(tool.name) + "' is listed before"));
      continue;
    }
    plan.tools.push_back(std::move(tool));
  }
}

void read_watched(
  const ArStateText & text, bool newer, ArRestorePlan & plan, std::vector<std::string> & warnings)
{
  std::size_t index = 0;
  for (const ConfigRecord & record : text.watched) {
    ++index;
    if (index > kArStateMaxRecords) {
      warnings.push_back(
        "AR State: more than " + std::to_string(kArStateMaxRecords) +
        " Watched Topics records; the rest are ignored");
      break;
    }
    warn_unknown_fields(record, {"Name", "Type"}, kWatchedField, index, newer, warnings);
    WatchedTopicState topic;
    const std::string * name = field_of(record, "Name");
    const std::string * type = field_of(record, "Type");
    if (name == nullptr || trimmed(*name).empty() || type == nullptr || trimmed(*type).empty()) {
      warnings.push_back(skipped(kWatchedField, index, "it has no Name or no Type"));
      continue;
    }
    topic.name = trimmed(*name);
    topic.type = trimmed(*type);
    if (topic.name.size() > kMaxNameBytes || has_space_or_control(topic.name) ||
      topic.type.size() > kMaxTypeBytes || has_space_or_control(topic.type))
    {
      warnings.push_back(
        skipped(
          kWatchedField, index, "'" + printable(topic.name) + "' has no usable Name or Type"));
      continue;
    }
    const bool listed = std::any_of(
      plan.watched.begin(), plan.watched.end(), [&topic](const WatchedTopicState & other) {
        return other.name == topic.name;
      });
    if (listed) {
      warnings.push_back(
        skipped(kWatchedField, index, "'" + printable(topic.name) + "' is listed before"));
      continue;
    }
    plan.watched.push_back(std::move(topic));
  }
}

const char * bool_text(bool value)
{
  return value ? "true" : "false";
}

/// The fields of @p record in the order of @p order, then any other.
ConfigNode record_node(const ConfigRecord & record, std::initializer_list<const char *> order)
{
  ConfigNode node;
  for (const char * field : order) {
    if (const std::string * text = field_of(record, field)) {
      node.set(field, ConfigNode::make_value(*text));
    }
  }
  for (const auto & entry : record) {
    if (node.find(entry.first) == nullptr) {
      node.set(entry.first, ConfigNode::make_value(entry.second));
    }
  }
  return node;
}

ConfigNode list_node(
  const std::vector<ConfigRecord> & records, std::initializer_list<const char *> order)
{
  ConfigNode node;
  node.kind = ConfigNode::Kind::List;
  for (const ConfigRecord & record : records) {
    node.list.push_back(record_node(record, order));
  }
  return node;
}

/// The records of the list under @p key of @p map; a child that is not a list is one sentence.
std::vector<ConfigRecord> read_records(
  const ConfigNode & map, const char * key, std::vector<std::string> & warnings)
{
  std::vector<ConfigRecord> records;
  const ConfigNode * list = map.find(key);
  if (list == nullptr || list->kind == ConfigNode::Kind::Empty) {
    return records;
  }
  if (list->kind != ConfigNode::Kind::List) {
    warnings.push_back(std::string("AR State: ") + key + " is not a list and is ignored");
    return records;
  }
  std::size_t index = 0;
  for (const ConfigNode & element : list->list) {
    ++index;
    if (element.kind != ConfigNode::Kind::Map) {
      warnings.push_back(
        std::string("AR State: ") + key + " record " + std::to_string(index) +
        " is not a map and is ignored");
      records.emplace_back();  // keeps the index of the later records right; it has no fields
      continue;
    }
    ConfigRecord record;
    for (std::size_t i = 0; i < element.map_keys.size(); ++i) {
      const ConfigNode & child = element.map_values[i];
      if (child.kind == ConfigNode::Kind::Value) {
        record[element.map_keys[i]] = child.value;
      } else if (child.kind != ConfigNode::Kind::Empty) {
        warnings.push_back(
          std::string("AR State: ") + key + " record " + std::to_string(index) + ": field '" +
          printable(element.map_keys[i]) + "' is not a plain value and is ignored");
      }
    }
    records.push_back(std::move(record));
  }
  return records;
}

}  // namespace

bool ar_state_panel_key(const std::string & key)
{
  return key == "qar_streaming_panel" || key == kArStateDisplaysKey ||
         (key.size() > 5 && key.compare(0, 5, "dock:") == 0);
}

ArRestorePlan parse_ar_state(const ArStateText & text, std::vector<std::string> & warnings)
{
  ArRestorePlan plan;
  bool newer = false;
  const std::string version = trimmed(text.version);
  if (!version.empty()) {
    const bool digits = std::all_of(version.begin(), version.end(), [](char c) {
          return std::isdigit(static_cast<unsigned char>(c)) != 0;
        });
    if (!digits || version.size() > 6) {
      warnings.push_back(
        "AR State: Version '" + printable(version) +
        "' is not a version number - reading the record as version " +
        std::to_string(kArStateVersion));
    } else if (std::stoi(version) > kArStateVersion) {
      newer = true;
      warnings.push_back(
        "AR State: written by a newer plugin (version " + version +
        "); the fields that are known are read");
    }
  }
  read_panels(text, newer, plan, warnings);
  read_tools(text, newer, plan, warnings);
  read_watched(text, newer, plan, warnings);
  return plan;
}

ArStateText make_ar_state(const ArRestorePlan & plan)
{
  ArStateText text;
  text.version = std::to_string(kArStateVersion);
  for (const ArPanelState & panel : plan.panels) {
    ConfigRecord record;
    record["Key"] = panel.key;
    record["On"] = bool_text(panel.on);
    if (panel.size.has_value()) {
      record["Size"] = to_string(*panel.size);
    }
    text.panels.push_back(std::move(record));
  }
  for (const ArToolState & tool : plan.tools) {
    ConfigRecord record;
    record["Kind"] = to_string(tool.kind);
    record["Name"] = tool.name;
    record["Type"] = tool.type;
    record["Writable"] = bool_text(tool.writable);
    record["Shown"] = bool_text(tool.shown);
    text.tools.push_back(std::move(record));
  }
  for (const WatchedTopicState & topic : plan.watched) {
    ConfigRecord record;
    record["Name"] = topic.name;
    record["Type"] = topic.type;
    text.watched.push_back(std::move(record));
  }
  return text;
}

std::vector<ArPanelState> merge_ar_panels(
  const std::vector<ArPanelState> & base, const std::vector<ArPanelLive> & streams,
  const std::vector<std::string> & queued, const std::map<std::string, ArCopySize> & choices,
  bool displays_on_by_setting)
{
  std::vector<ArPanelState> panels = base;
  const auto entry_for = [&panels](const std::string & key) -> ArPanelState & {
      for (ArPanelState & panel : panels) {
        if (panel.key == key) {
          return panel;
        }
      }
      panels.push_back(ArPanelState{key, false, std::nullopt});
      return panels.back();
    };
  for (const ArPanelLive & stream : streams) {
    if (ar_state_panel_key(stream.key) && (stream.on || stream.key == kArStateDisplaysKey)) {
      entry_for(stream.key).on = stream.on;
    }
  }
  for (const std::string & key : queued) {
    if (ar_state_panel_key(key)) {
      entry_for(key).on = true;
    }
  }
  for (ArPanelState & panel : panels) {
    const auto choice = choices.find(panel.key);
    panel.size = choice == choices.end() ? std::nullopt
      : std::optional<ArCopySize>(choice->second);
  }
  for (const auto & choice : choices) {
    if (ar_state_panel_key(choice.first)) {
      entry_for(choice.first).size = choice.second;
    }
  }
  panels.erase(
    std::remove_if(
      panels.begin(), panels.end(),
      [displays_on_by_setting](const ArPanelState & panel) {
        if (panel.key == kArStateDisplaysKey) {
          return panel.on == displays_on_by_setting && !panel.size.has_value();
        }
        return !panel.on && !panel.size.has_value();
      }),
    panels.end());
  return panels;
}

const ArPanelState * find_ar_panel_state(const ArRestorePlan & plan, const std::string & key)
{
  for (const ArPanelState & panel : plan.panels) {
    if (panel.key == key) {
      return &panel;
    }
  }
  return nullptr;
}

std::vector<std::string> panels_to_start(
  const ArRestorePlan & plan, const std::vector<std::string> & already)
{
  std::vector<std::string> keys;
  for (const ArPanelState & panel : plan.panels) {
    if (!panel.on || panel.key == kArStateDisplaysKey) {
      continue;
    }
    const auto taken = [&panel](const std::string & key) { return key == panel.key; };
    if (std::any_of(already.begin(), already.end(), taken) ||
      std::any_of(keys.begin(), keys.end(), taken))
    {
      continue;
    }
    keys.push_back(panel.key);
  }
  return keys;
}

bool hidden_displays_starts(const ArRestorePlan & plan, bool setting_on)
{
  const ArPanelState * entry = find_ar_panel_state(plan, kArStateDisplaysKey);
  return entry != nullptr ? entry->on : setting_on;
}

const ConfigNode * ConfigNode::find(const std::string & key) const
{
  if (kind != Kind::Map) {
    return nullptr;
  }
  for (std::size_t i = 0; i < map_keys.size(); ++i) {
    if (map_keys[i] == key) {
      return &map_values[i];
    }
  }
  return nullptr;
}

void ConfigNode::set(const std::string & key, ConfigNode child)
{
  if (kind != Kind::Map) {
    *this = ConfigNode();
    kind = Kind::Map;
  }
  for (std::size_t i = 0; i < map_keys.size(); ++i) {
    if (map_keys[i] == key) {
      map_values[i] = std::move(child);
      return;
    }
  }
  map_keys.push_back(key);
  map_values.push_back(std::move(child));
}

ConfigNode ConfigNode::make_value(std::string text)
{
  ConfigNode node;
  node.kind = Kind::Value;
  node.value = std::move(text);
  return node;
}

std::optional<ArStateText> read_ar_state_text(
  const ConfigNode & panel_map, std::vector<std::string> & warnings)
{
  const ConfigNode * state = panel_map.find(kArStateKey);
  if (state == nullptr || state->kind == ConfigNode::Kind::Empty) {
    return std::nullopt;
  }
  if (state->kind != ConfigNode::Kind::Map) {
    warnings.push_back(std::string(kArStateKey) + " is not a map and is ignored");
    return std::nullopt;
  }
  ArStateText text;
  if (const ConfigNode * version = state->find(kVersionField)) {
    if (version->kind == ConfigNode::Kind::Value) {
      text.version = version->value;
    }
  }
  text.panels = read_records(*state, kPanelsField, warnings);
  text.tools = read_records(*state, kToolsField, warnings);
  text.watched = read_records(*state, kWatchedField, warnings);
  return text;
}

void write_ar_state_text(ConfigNode & panel_map, const ArStateText & text)
{
  ConfigNode state;
  state.kind = ConfigNode::Kind::Map;
  state.set(kVersionField, ConfigNode::make_value(text.version));
  if (!text.panels.empty()) {
    state.set(kPanelsField, list_node(text.panels, {"Key", "On", "Size"}));
  }
  if (!text.tools.empty()) {
    state.set(kToolsField, list_node(text.tools, {"Kind", "Name", "Type", "Writable", "Shown"}));
  }
  if (!text.watched.empty()) {
    state.set(kWatchedField, list_node(text.watched, {"Name", "Type"}));
  }
  panel_map.set(kArStateKey, std::move(state));
}

std::optional<ArStateText> load_ar_state_text(
  const ArStateConfigIo & io, std::vector<std::string> & warnings)
{
  if (!io.read) {
    return std::nullopt;
  }
  return read_ar_state_text(io.read(), warnings);
}

void store_ar_state_text(const ArStateConfigIo & io, const ArStateText & text)
{
  if (!io.read || !io.write) {
    return;
  }
  ConfigNode panel_map = io.read();
  write_ar_state_text(panel_map, text);
  io.write(panel_map);
}

}  // namespace qar_rviz_streaming
