// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_ros_entity.hpp"

#include <cstddef>

namespace qar_rviz_streaming
{
namespace
{
/// The id charset qar_ui_model::is_valid_widget_id accepts after the first byte.
bool is_id_char(char c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
         c == '_' || c == '.' || c == '-';
}

/// 128 is qar_ui_model's hard id limit; leave room for the section/field suffix
/// a generated widget id appends to the panel id.
constexpr std::size_t kMaxPanelIdBytes = 64;
}  // namespace

const char * to_string(RosEntityKind kind)
{
  switch (kind) {
    case RosEntityKind::Topic:
      return "topic";
    case RosEntityKind::Service:
      return "service";
    case RosEntityKind::Action:
      return "action";
  }
  return "topic";
}

bool ros_entity_kind_from_string(std::string_view text, RosEntityKind & out_kind)
{
  if (text == "topic") {
    out_kind = RosEntityKind::Topic;
    return true;
  }
  if (text == "service") {
    out_kind = RosEntityKind::Service;
    return true;
  }
  if (text == "action") {
    out_kind = RosEntityKind::Action;
    return true;
  }
  return false;
}

std::string make_panel_id(RosEntityKind kind, std::string_view name)
{
  std::string id = to_string(kind);
  id += '.';
  if (name.empty()) {
    id += '_';
    return id;
  }
  for (const char c : name) {
    id += is_id_char(c) ? c : '_';
  }
  if (id.size() > kMaxPanelIdBytes) {
    id.resize(kMaxPanelIdBytes);
  }
  return id;
}

}  // namespace qar_rviz_streaming
