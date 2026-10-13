// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__INTROSPECTION_HPP_
#define QAR_ROS_UI__INTROSPECTION_HPP_

/// @file introspection.hpp
/// @brief `MessageMembers` -> `qar_ui_model::MessageDescription`.
///
/// The mapping is mechanical (the schema was shaped after `MessageMember`),
/// with two things introspection does not hand over directly:
///
///   * **defaults** — Humble's C++ introspection leaves `default_value_` null
///     for every member. The generated constructor knows them, though: an
///     instance initialised with `MessageInitialization::ALL` carries every
///     IDL default, so the description is filled by instantiating the type once
///     and reading the primitive fields back. Nested types get their own
///     defaults through their own description;
///   * **constants** — not present in any Humble typesupport. Left empty; a
///     template or a .msg parser is the only source (documented in the schema).
///
/// Descriptions are cached per `MessageMembers` table, so one introspected
/// `std_msgs/msg/Header` is shared by everything that embeds it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

#include "qar_ui_model/type_description.hpp"

#include "qar_ros_ui/type_support_registry.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

using rosidl_typesupport_introspection_cpp::MessageMember;
using rosidl_typesupport_introspection_cpp::MessageMembers;

struct IntrospectionOptions
{
  /// Instantiate the type once to recover IDL defaults (see file comment).
  bool record_defaults = true;
};

/// @brief rosidl type id (`ROS_TYPE_*`) -> schema kind. False for an unknown id.
bool field_kind_from_type_id(std::uint8_t type_id, qar_ui_model::FieldKind & out_kind);

/// @brief sizeof the C++ element behind a primitive type id; 0 for messages
/// and unknown ids. (`long double` is 8 on MSVC, 16 elsewhere: computed, not assumed.)
std::size_t primitive_size(std::uint8_t type_id);

/// @brief "pkg::msg" + "Name" -> "pkg/msg/Name".
std::string type_name_from_members(const MessageMembers & members);

/// @brief Member by name, or nullptr.
const MessageMember * find_member(const MessageMembers & members, std::string_view name);

/// @brief Describe a table (cached; nested types resolved recursively).
qar_ui_model::MessageDescriptionPtr describe_message(
  const MessageMembers & members,
  const IntrospectionOptions & options = {});

/// @brief Type name -> description, loading typesupport through @p registry
/// (the global one when null).
Result introspect_message(
  std::string_view type_name,
  qar_ui_model::MessageDescriptionPtr & out,
  TypeSupportRegistry * registry = nullptr,
  const IntrospectionOptions & options = {});

Result introspect_service(
  std::string_view type_name,
  qar_ui_model::ServiceDescription & out,
  TypeSupportRegistry * registry = nullptr,
  const IntrospectionOptions & options = {});

Result introspect_action(
  std::string_view type_name,
  qar_ui_model::ActionDescription & out,
  TypeSupportRegistry * registry = nullptr,
  const IntrospectionOptions & options = {});

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__INTROSPECTION_HPP_
