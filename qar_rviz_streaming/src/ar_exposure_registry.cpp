// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_exposure_registry.hpp"

#include <algorithm>
#include <utility>

namespace qar_rviz_streaming
{

std::string ExposedEntity::describe() const
{
  std::string text = to_string(entity.kind);
  text += ' ';
  text += entity.name;
  if (!entity.type_name.empty()) {
    text += " (" + entity.type_name + ")";
  }
  return text;
}

bool ExposureRegistry::add(ExposedEntity entry, std::string * out_error)
{
  const auto fail = [out_error](std::string message) {
      if (out_error != nullptr) {
        *out_error = std::move(message);
      }
      return false;
    };

  if (entry.entity.name.empty()) {
    return fail("cannot expose an entity with no name");
  }
  if (entry.panel_id.empty()) {
    entry.panel_id = make_panel_id(entry.entity.kind, entry.entity.name);
  }
  if (contains(entry.entity.kind, entry.entity.name)) {
    return fail(
      std::string(to_string(entry.entity.kind)) + " " + entry.entity.name +
      " is already exposed");
  }
  if (find_by_panel_id(entry.panel_id) != nullptr) {
    return fail("panel id '" + entry.panel_id + "' is already in use");
  }
  entries_.push_back(std::move(entry));
  return true;
}

bool ExposureRegistry::remove(RosEntityKind kind, std::string_view name)
{
  const auto it = std::find_if(
    entries_.begin(), entries_.end(),
    [kind, name](const ExposedEntity & entry) {
      return entry.entity.kind == kind && entry.entity.name == name;
    });
  if (it == entries_.end()) {
    return false;
  }
  entries_.erase(it);
  return true;
}

bool ExposureRegistry::remove_by_panel_id(std::string_view panel_id)
{
  const auto it = std::find_if(
    entries_.begin(), entries_.end(),
    [panel_id](const ExposedEntity & entry) { return entry.panel_id == panel_id; });
  if (it == entries_.end()) {
    return false;
  }
  entries_.erase(it);
  return true;
}

bool ExposureRegistry::contains(RosEntityKind kind, std::string_view name) const
{
  return find(kind, name) != nullptr;
}

const ExposedEntity * ExposureRegistry::find(RosEntityKind kind, std::string_view name) const
{
  for (const ExposedEntity & entry : entries_) {
    if (entry.entity.kind == kind && entry.entity.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

ExposedEntity * ExposureRegistry::find(RosEntityKind kind, std::string_view name)
{
  return const_cast<ExposedEntity *>(
    static_cast<const ExposureRegistry *>(this)->find(kind, name));
}

const ExposedEntity * ExposureRegistry::find_by_panel_id(std::string_view panel_id) const
{
  if (panel_id.empty()) {
    return nullptr;
  }
  for (const ExposedEntity & entry : entries_) {
    if (entry.panel_id == panel_id) {
      return &entry;
    }
  }
  return nullptr;
}

ExposedEntity * ExposureRegistry::find_by_panel_id(std::string_view panel_id)
{
  return const_cast<ExposedEntity *>(
    static_cast<const ExposureRegistry *>(this)->find_by_panel_id(panel_id));
}

const ExposedEntity * ExposureRegistry::find_by_token(std::string_view token) const
{
  if (token.empty()) {
    return nullptr;  // an exposure whose panel is not served yet has no token
  }
  for (const ExposedEntity & entry : entries_) {
    if (entry.token == token) {
      return &entry;
    }
  }
  return nullptr;
}

ExposedEntity * ExposureRegistry::find_by_token(std::string_view token)
{
  return const_cast<ExposedEntity *>(
    static_cast<const ExposureRegistry *>(this)->find_by_token(token));
}

}  // namespace qar_rviz_streaming
