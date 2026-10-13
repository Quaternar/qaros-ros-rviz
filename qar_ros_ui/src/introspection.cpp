// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/introspection.hpp"

#include <map>
#include <mutex>
#include <new>
#include <string>
#include <utility>

#include "rosidl_typesupport_introspection_cpp/field_types.hpp"

#include "conversion_internal.hpp"

namespace qar_ros_ui
{

namespace ric = rosidl_typesupport_introspection_cpp;
using qar_ui_model::FieldDescription;
using qar_ui_model::FieldKind;
using qar_ui_model::MessageDescriptionBuilder;
using qar_ui_model::MessageDescriptionPtr;
using qar_ui_model::UiValue;

bool field_kind_from_type_id(std::uint8_t type_id, FieldKind & out_kind)
{
  switch (type_id) {
    case ric::ROS_TYPE_FLOAT: out_kind = FieldKind::Float32; return true;
    case ric::ROS_TYPE_DOUBLE: out_kind = FieldKind::Float64; return true;
    case ric::ROS_TYPE_LONG_DOUBLE: out_kind = FieldKind::LongDouble; return true;
    case ric::ROS_TYPE_CHAR: out_kind = FieldKind::Char; return true;
    case ric::ROS_TYPE_WCHAR: out_kind = FieldKind::WChar; return true;
    case ric::ROS_TYPE_BOOLEAN: out_kind = FieldKind::Bool; return true;
    case ric::ROS_TYPE_OCTET: out_kind = FieldKind::Byte; return true;
    case ric::ROS_TYPE_UINT8: out_kind = FieldKind::UInt8; return true;
    case ric::ROS_TYPE_INT8: out_kind = FieldKind::Int8; return true;
    case ric::ROS_TYPE_UINT16: out_kind = FieldKind::UInt16; return true;
    case ric::ROS_TYPE_INT16: out_kind = FieldKind::Int16; return true;
    case ric::ROS_TYPE_UINT32: out_kind = FieldKind::UInt32; return true;
    case ric::ROS_TYPE_INT32: out_kind = FieldKind::Int32; return true;
    case ric::ROS_TYPE_UINT64: out_kind = FieldKind::UInt64; return true;
    case ric::ROS_TYPE_INT64: out_kind = FieldKind::Int64; return true;
    case ric::ROS_TYPE_STRING: out_kind = FieldKind::String; return true;
    case ric::ROS_TYPE_WSTRING: out_kind = FieldKind::WString; return true;
    case ric::ROS_TYPE_MESSAGE: out_kind = FieldKind::Message; return true;
    default: return false;
  }
}

std::size_t primitive_size(std::uint8_t type_id)
{
  switch (type_id) {
    case ric::ROS_TYPE_FLOAT: return sizeof(float);
    case ric::ROS_TYPE_DOUBLE: return sizeof(double);
    case ric::ROS_TYPE_LONG_DOUBLE: return sizeof(long double);
    case ric::ROS_TYPE_CHAR: return sizeof(unsigned char);
    case ric::ROS_TYPE_WCHAR: return sizeof(char16_t);
    case ric::ROS_TYPE_BOOLEAN: return sizeof(bool);
    case ric::ROS_TYPE_OCTET: return sizeof(unsigned char);
    case ric::ROS_TYPE_UINT8: return sizeof(std::uint8_t);
    case ric::ROS_TYPE_INT8: return sizeof(std::int8_t);
    case ric::ROS_TYPE_UINT16: return sizeof(std::uint16_t);
    case ric::ROS_TYPE_INT16: return sizeof(std::int16_t);
    case ric::ROS_TYPE_UINT32: return sizeof(std::uint32_t);
    case ric::ROS_TYPE_INT32: return sizeof(std::int32_t);
    case ric::ROS_TYPE_UINT64: return sizeof(std::uint64_t);
    case ric::ROS_TYPE_INT64: return sizeof(std::int64_t);
    case ric::ROS_TYPE_STRING: return sizeof(std::string);
    case ric::ROS_TYPE_WSTRING: return sizeof(std::u16string);
    default: return 0;
  }
}

std::string type_name_from_members(const MessageMembers & members)
{
  std::string name = members.message_namespace_ ? members.message_namespace_ : "";
  for (std::size_t pos = name.find("::"); pos != std::string::npos; pos = name.find("::", pos)) {
    name.replace(pos, 2, "/");
    pos += 1;
  }
  if (!name.empty()) {
    name += "/";
  }
  name += members.message_name_ ? members.message_name_ : "";
  return name;
}

const MessageMember * find_member(const MessageMembers & members, std::string_view name)
{
  for (std::uint32_t i = 0; i < members.member_count_; ++i) {
    const MessageMember & member = members.members_[i];
    if (member.name_ != nullptr && name == member.name_) {
      return &member;
    }
  }
  return nullptr;
}

namespace
{

const MessageMembers * sub_members(const MessageMember & member)
{
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    return nullptr;
  }
  return static_cast<const MessageMembers *>(member.members_->data);
}

// Recursive: describing a type describes its nested types first, under the
// same lock, so it cannot be a plain mutex.
std::recursive_mutex & cache_mutex()
{
  static std::recursive_mutex mutex;
  return mutex;
}

std::map<std::pair<const MessageMembers *, bool>, MessageDescriptionPtr> & cache()
{
  static std::map<std::pair<const MessageMembers *, bool>, MessageDescriptionPtr> instance;
  return instance;
}

/// A fresh ALL-initialised instance, or null when the table cannot build one.
struct ScratchInstance
{
  const MessageMembers & members;
  void * memory = nullptr;

  explicit ScratchInstance(const MessageMembers & m)
  : members(m)
  {
    if (m.size_of_ == 0 || m.init_function == nullptr || m.fini_function == nullptr) {
      return;
    }
    memory = ::operator new(m.size_of_);
    m.init_function(memory, rosidl_runtime_cpp::MessageInitialization::ALL);
  }
  ~ScratchInstance()
  {
    if (memory != nullptr) {
      members.fini_function(memory);
      ::operator delete(memory);
    }
  }
  ScratchInstance(const ScratchInstance &) = delete;
  ScratchInstance & operator=(const ScratchInstance &) = delete;
};

}  // namespace

MessageDescriptionPtr describe_message(
  const MessageMembers & members, const IntrospectionOptions & options)
{
  std::lock_guard<std::recursive_mutex> lock(cache_mutex());
  const auto key = std::make_pair(&members, options.record_defaults);
  const auto cached = cache().find(key);
  if (cached != cache().end()) {
    return cached->second;
  }

  MessageDescriptionBuilder builder(type_name_from_members(members));

  // Defaults come off one instance built by the generated constructor; only
  // primitives (and arrays of them) are recorded, and only when non-zero, so
  // a description says exactly what the IDL said and nothing redundant.
  ScratchInstance instance(members);
  const bool read_defaults = options.record_defaults && instance.memory != nullptr;
  ConversionLimits unlimited;
  unlimited.max_array_elements = 0;

  for (std::uint32_t i = 0; i < members.member_count_; ++i) {
    const MessageMember & member = members.members_[i];
    const std::string name = member.name_ ? member.name_ : "";
    FieldKind kind = FieldKind::Message;
    const bool known = field_kind_from_type_id(member.type_id_, kind);

    FieldDescription field;
    if (known && kind == FieldKind::Message) {
      const MessageMembers * sub = sub_members(member);
      field = FieldDescription::nested(name, sub ? describe_message(*sub, options) : nullptr);
    } else {
      // An unknown id has no schema home; Byte keeps the bytes visible.
      field = FieldDescription::primitive(name, known ? kind : FieldKind::Byte);
      if ((kind == FieldKind::String || kind == FieldKind::WString) && member.string_upper_bound_ > 0) {
        field.string_bound(member.string_upper_bound_);
      }
    }

    if (member.is_array_) {
      if (member.is_upper_bound_) {
        field.bounded_array(member.array_size_);
      } else if (member.array_size_ > 0) {
        field.fixed_array(member.array_size_);
      } else {
        field.unbounded_array();
      }
    }

    if (read_defaults && known && kind != FieldKind::Message) {
      UiValue value = detail::member_to_ui_value(instance.memory, member, unlimited, nullptr, 0);
      if (!detail::ui_value_is_zero(value)) {
        field.with_default(std::move(value));
      }
    }

    builder.field(std::move(field));
  }

  MessageDescriptionPtr description = builder.build();
  cache()[key] = description;
  return description;
}

Result introspect_message(
  std::string_view type_name,
  MessageDescriptionPtr & out,
  TypeSupportRegistry * registry,
  const IntrospectionOptions & options)
{
  TypeSupportRegistry & reg = registry ? *registry : TypeSupportRegistry::global();
  MessageTypeSupport ts;
  Result result = reg.message(type_name, ts);
  if (!result.ok()) {
    return result;
  }
  const MessageMembers * members = ts.members();
  if (members == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "no introspection table for " + ts.type_name);
  }
  out = describe_message(*members, options);
  return Result::success();
}

Result introspect_service(
  std::string_view type_name,
  qar_ui_model::ServiceDescription & out,
  TypeSupportRegistry * registry,
  const IntrospectionOptions & options)
{
  TypeSupportRegistry & reg = registry ? *registry : TypeSupportRegistry::global();
  ServiceTypeSupport ts;
  Result result = reg.service(type_name, ts);
  if (!result.ok()) {
    return result;
  }
  const MessageMembers * request = ts.request.members();
  const MessageMembers * response = ts.response.members();
  if (request == nullptr || response == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "no introspection table for " + ts.type_name);
  }
  out.type_name = ts.type_name;
  out.request = describe_message(*request, options);
  out.response = describe_message(*response, options);
  return Result::success();
}

Result introspect_action(
  std::string_view type_name,
  qar_ui_model::ActionDescription & out,
  TypeSupportRegistry * registry,
  const IntrospectionOptions & options)
{
  TypeSupportRegistry & reg = registry ? *registry : TypeSupportRegistry::global();
  ActionTypeSupport ts;
  Result result = reg.action(type_name, ts);
  if (!result.ok()) {
    return result;
  }
  const MessageMembers * goal = ts.goal.members();
  const MessageMembers * res = ts.result.members();
  const MessageMembers * feedback = ts.feedback.members();
  if (goal == nullptr || res == nullptr || feedback == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "no introspection table for " + ts.type_name);
  }
  out.type_name = ts.type_name;
  out.goal = describe_message(*goal, options);
  out.result = describe_message(*res, options);
  out.feedback = describe_message(*feedback, options);
  return Result::success();
}

}  // namespace qar_ros_ui
