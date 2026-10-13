// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/dynamic_message.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <utility>

#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"

#include "qar_ui_model/unicode.hpp"

#include "conversion_internal.hpp"

namespace qar_ros_ui
{

namespace ric = rosidl_typesupport_introspection_cpp;
using qar_ui_model::UiArray;
using qar_ui_model::UiObject;
using qar_ui_model::UiObjectEntry;
using qar_ui_model::UiValue;

namespace
{

constexpr std::size_t kMaxWriteDepth = 64;
constexpr double kTwoPow63 = 9223372036854775808.0;
constexpr double kTwoPow64 = 18446744073709551616.0;

const ric::MessageMembers * sub_members(const ric::MessageMember & member)
{
  if (member.members_ == nullptr || member.members_->data == nullptr) {
    return nullptr;
  }
  return static_cast<const ric::MessageMembers *>(member.members_->data);
}

const char * type_id_name(std::uint8_t type_id)
{
  switch (type_id) {
    case ric::ROS_TYPE_FLOAT: return "float32";
    case ric::ROS_TYPE_DOUBLE: return "float64";
    case ric::ROS_TYPE_LONG_DOUBLE: return "long double";
    case ric::ROS_TYPE_CHAR: return "char";
    case ric::ROS_TYPE_WCHAR: return "wchar";
    case ric::ROS_TYPE_BOOLEAN: return "bool";
    case ric::ROS_TYPE_OCTET: return "byte";
    case ric::ROS_TYPE_UINT8: return "uint8";
    case ric::ROS_TYPE_INT8: return "int8";
    case ric::ROS_TYPE_UINT16: return "uint16";
    case ric::ROS_TYPE_INT16: return "int16";
    case ric::ROS_TYPE_UINT32: return "uint32";
    case ric::ROS_TYPE_INT32: return "int32";
    case ric::ROS_TYPE_UINT64: return "uint64";
    case ric::ROS_TYPE_INT64: return "int64";
    case ric::ROS_TYPE_STRING: return "string";
    case ric::ROS_TYPE_WSTRING: return "wstring";
    case ric::ROS_TYPE_MESSAGE: return "message";
    default: return "unknown";
  }
}

template<typename T>
UiValue finite_or_null(T value)
{
  const double as_double = static_cast<double>(value);
  return std::isfinite(as_double) ? UiValue(as_double) : UiValue(nullptr);
}

template<typename T>
UiValue integer_value(const void * ptr)
{
  return UiValue(static_cast<std::int64_t>(*static_cast<const T *>(ptr)));
}

UiValue primitive_to_ui_value(const void * ptr, std::uint8_t type_id)
{
  switch (type_id) {
    case ric::ROS_TYPE_FLOAT: return finite_or_null(*static_cast<const float *>(ptr));
    case ric::ROS_TYPE_DOUBLE: return finite_or_null(*static_cast<const double *>(ptr));
    case ric::ROS_TYPE_LONG_DOUBLE: return finite_or_null(*static_cast<const long double *>(ptr));
    case ric::ROS_TYPE_CHAR:
    case ric::ROS_TYPE_OCTET:
    case ric::ROS_TYPE_UINT8: return integer_value<std::uint8_t>(ptr);
    case ric::ROS_TYPE_WCHAR: return integer_value<char16_t>(ptr);
    case ric::ROS_TYPE_BOOLEAN: return UiValue(*static_cast<const bool *>(ptr));
    case ric::ROS_TYPE_INT8: return integer_value<std::int8_t>(ptr);
    case ric::ROS_TYPE_UINT16: return integer_value<std::uint16_t>(ptr);
    case ric::ROS_TYPE_INT16: return integer_value<std::int16_t>(ptr);
    case ric::ROS_TYPE_UINT32: return integer_value<std::uint32_t>(ptr);
    case ric::ROS_TYPE_INT32: return integer_value<std::int32_t>(ptr);
    case ric::ROS_TYPE_UINT64: return UiValue(*static_cast<const std::uint64_t *>(ptr));
    case ric::ROS_TYPE_INT64: return integer_value<std::int64_t>(ptr);
    case ric::ROS_TYPE_STRING: return UiValue(*static_cast<const std::string *>(ptr));
    case ric::ROS_TYPE_WSTRING:
      return UiValue(utf16_to_utf8(*static_cast<const std::u16string *>(ptr)));
    default: return UiValue(nullptr);
  }
}

UiValue message_to_ui_value_impl(
  const void * message,
  const ric::MessageMembers & members,
  const ConversionLimits & limits,
  ConversionReport * report,
  std::size_t depth)
{
  if (limits.max_depth != 0 && depth > limits.max_depth) {
    if (report != nullptr) {
      report->depth_exceeded = true;
    }
    return UiValue(nullptr);
  }
  UiObject entries;
  entries.reserve(members.member_count_);
  for (std::uint32_t i = 0; i < members.member_count_; ++i) {
    const ric::MessageMember & member = members.members_[i];
    entries.push_back(
      {member.name_ ? member.name_ : "",
        detail::member_to_ui_value(message, member, limits, report, depth)});
  }
  return UiValue(std::move(entries));
}

// --- Writing ----------------------------------------------------------------

Result fail(const std::string & path, const std::string & what)
{
  return Result::error(Status::ConversionFailed, (path.empty() ? "<root>" : path) + ": " + what);
}

std::string join(const std::string & path, const std::string & name)
{
  return path.empty() ? name : path + "." + name;
}

std::string indexed(const std::string & path, std::size_t index)
{
  return path + "[" + std::to_string(index) + "]";
}

bool integral_double(const UiValue & value, double & out)
{
  if (!value.is_double()) {
    return false;
  }
  out = value.as_double();
  return std::isfinite(out) && out == std::floor(out);
}

template<typename T>
Result write_integer(const UiValue & value, void * ptr, const std::string & path, const char * name)
{
  constexpr std::int64_t lo = std::numeric_limits<T>::min();
  constexpr std::int64_t hi = std::numeric_limits<T>::max();
  std::int64_t result = 0;
  if (value.is_int()) {
    result = value.as_int();
  } else if (value.is_double()) {
    double as_double = 0.0;
    if (!integral_double(value, as_double)) {
      return fail(path, std::string("expected ") + name + ", got a non-integral double");
    }
    if (as_double < static_cast<double>(lo) || as_double > static_cast<double>(hi)) {
      return fail(path, std::string("value out of range for ") + name);
    }
    // 2^63 is what INT64_MAX rounds to as a double: saturate rather than overflow.
    result = as_double >= kTwoPow63 ? hi : static_cast<std::int64_t>(as_double);
  } else {
    return fail(
      path, std::string("expected ") + name + ", got " + qar_ui_model::to_string(value.kind()));
  }
  if (result < lo || result > hi) {
    return fail(path, "value " + std::to_string(result) + " out of range for " + name);
  }
  *static_cast<T *>(ptr) = static_cast<T>(result);
  return Result::success();
}

Result write_uint64(const UiValue & value, void * ptr, const std::string & path)
{
  std::uint64_t result = 0;
  if (value.is_int()) {
    const std::int64_t as_int = value.as_int();
    if (as_int < 0) {
      return fail(path, "value " + std::to_string(as_int) + " out of range for uint64");
    }
    result = static_cast<std::uint64_t>(as_int);
  } else if (value.is_double()) {
    double as_double = 0.0;
    if (!integral_double(value, as_double)) {
      return fail(path, "expected uint64, got a non-integral double");
    }
    if (as_double < 0.0 || as_double > kTwoPow64) {
      return fail(path, "value out of range for uint64");
    }
    // UINT64_MAX rounds to 2^64 as a double; that is the value the sender meant.
    result = as_double >= kTwoPow64 ?
      std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(as_double);
  } else {
    return fail(path, "expected uint64, got " + std::string(qar_ui_model::to_string(value.kind())));
  }
  *static_cast<std::uint64_t *>(ptr) = result;
  return Result::success();
}

template<typename T>
Result write_float(const UiValue & value, void * ptr, const std::string & path, const char * name)
{
  if (value.is_null()) {
    *static_cast<T *>(ptr) = std::numeric_limits<T>::quiet_NaN();
    return Result::success();
  }
  if (!value.is_number()) {
    return fail(
      path, std::string("expected ") + name + ", got " + qar_ui_model::to_string(value.kind()));
  }
  *static_cast<T *>(ptr) = static_cast<T>(value.as_double());
  return Result::success();
}

Result write_string(const UiValue & value, void * ptr, std::size_t bound, const std::string & path)
{
  if (!value.is_string()) {
    return fail(path, "expected string, got " + std::string(qar_ui_model::to_string(value.kind())));
  }
  const std::string & text = value.as_string();
  if (bound != 0 && text.size() > bound) {
    return fail(path, "string longer than its bound of " + std::to_string(bound) + " bytes");
  }
  *static_cast<std::string *>(ptr) = text;
  return Result::success();
}

Result write_wstring(const UiValue & value, void * ptr, std::size_t bound, const std::string & path)
{
  if (!value.is_string()) {
    return fail(path, "expected wstring, got " + std::string(qar_ui_model::to_string(value.kind())));
  }
  std::u16string text = utf8_to_utf16(value.as_string());
  if (bound != 0 && text.size() > bound) {
    return fail(path, "wstring longer than its bound of " + std::to_string(bound) + " characters");
  }
  *static_cast<std::u16string *>(ptr) = std::move(text);
  return Result::success();
}

Result write_primitive(
  const UiValue & value, void * ptr, const ric::MessageMember & member, const std::string & path)
{
  switch (member.type_id_) {
    case ric::ROS_TYPE_BOOLEAN:
      if (!value.is_bool()) {
        return fail(path, "expected bool, got " + std::string(qar_ui_model::to_string(value.kind())));
      }
      *static_cast<bool *>(ptr) = value.as_bool();
      return Result::success();
    case ric::ROS_TYPE_FLOAT: return write_float<float>(value, ptr, path, "float32");
    case ric::ROS_TYPE_DOUBLE: return write_float<double>(value, ptr, path, "float64");
    case ric::ROS_TYPE_LONG_DOUBLE: return write_float<long double>(value, ptr, path, "long double");
    case ric::ROS_TYPE_CHAR: return write_integer<std::uint8_t>(value, ptr, path, "char");
    case ric::ROS_TYPE_OCTET: return write_integer<std::uint8_t>(value, ptr, path, "byte");
    case ric::ROS_TYPE_UINT8: return write_integer<std::uint8_t>(value, ptr, path, "uint8");
    case ric::ROS_TYPE_WCHAR: return write_integer<char16_t>(value, ptr, path, "wchar");
    case ric::ROS_TYPE_INT8: return write_integer<std::int8_t>(value, ptr, path, "int8");
    case ric::ROS_TYPE_UINT16: return write_integer<std::uint16_t>(value, ptr, path, "uint16");
    case ric::ROS_TYPE_INT16: return write_integer<std::int16_t>(value, ptr, path, "int16");
    case ric::ROS_TYPE_UINT32: return write_integer<std::uint32_t>(value, ptr, path, "uint32");
    case ric::ROS_TYPE_INT32: return write_integer<std::int32_t>(value, ptr, path, "int32");
    case ric::ROS_TYPE_INT64: return write_integer<std::int64_t>(value, ptr, path, "int64");
    case ric::ROS_TYPE_UINT64: return write_uint64(value, ptr, path);
    case ric::ROS_TYPE_STRING: return write_string(value, ptr, member.string_upper_bound_, path);
    case ric::ROS_TYPE_WSTRING: return write_wstring(value, ptr, member.string_upper_bound_, path);
    default:
      return fail(path, std::string("unsupported field type id ") + std::to_string(member.type_id_));
  }
}

Result write_message(
  const UiValue & value,
  void * message,
  const ric::MessageMembers & members,
  const ConversionOptions & options,
  const std::string & path,
  std::size_t depth);

Result write_member(
  const UiValue & value,
  void * message,
  const ric::MessageMember & member,
  const ConversionOptions & options,
  const std::string & path,
  std::size_t depth)
{
  void * base = static_cast<std::uint8_t *>(message) + member.offset_;
  const ric::MessageMembers * sub = sub_members(member);

  if (!member.is_array_) {
    if (member.type_id_ == ric::ROS_TYPE_MESSAGE) {
      if (sub == nullptr) {
        return fail(path, "nested type has no introspection");
      }
      return write_message(value, base, *sub, options, path, depth + 1);
    }
    return write_primitive(value, base, member, path);
  }

  if (!value.is_array()) {
    return fail(
      path, "expected an array, got " + std::string(qar_ui_model::to_string(value.kind())));
  }
  const std::size_t count = value.size();
  const bool fixed = !member.is_upper_bound_ && member.array_size_ > 0;
  if (fixed) {
    if (count != member.array_size_) {
      return fail(
        path, "fixed array of size " + std::to_string(member.array_size_) + " has " +
        std::to_string(count) + " elements");
    }
  } else {
    if (member.is_upper_bound_ && count > member.array_size_) {
      return fail(
        path, "bounded array (<=" + std::to_string(member.array_size_) + ") has " +
        std::to_string(count) + " elements");
    }
    if (member.resize_function == nullptr) {
      return fail(path, "sequence has no resize function");
    }
    member.resize_function(base, count);
  }

  for (std::size_t i = 0; i < count; ++i) {
    const UiValue & element = *value.at(i);
    const std::string element_path = indexed(path, i);
    if (member.type_id_ == ric::ROS_TYPE_MESSAGE) {
      if (sub == nullptr || member.get_function == nullptr) {
        return fail(element_path, "nested element has no introspection");
      }
      Result result =
        write_message(element, member.get_function(base, i), *sub, options, element_path, depth + 1);
      if (!result.ok()) {
        return result;
      }
    } else if (member.get_function != nullptr) {
      Result result = write_primitive(element, member.get_function(base, i), member, element_path);
      if (!result.ok()) {
        return result;
      }
    } else if (member.assign_function != nullptr && member.type_id_ == ric::ROS_TYPE_BOOLEAN) {
      // std::vector<bool> has no element addresses; the table offers assign().
      bool scalar = false;
      Result result = write_primitive(element, &scalar, member, element_path);
      if (!result.ok()) {
        return result;
      }
      member.assign_function(base, i, &scalar);
    } else {
      return fail(element_path, "array element is not writable through introspection");
    }
  }
  return Result::success();
}

Result write_message(
  const UiValue & value,
  void * message,
  const ric::MessageMembers & members,
  const ConversionOptions & options,
  const std::string & path,
  std::size_t depth)
{
  if (depth > kMaxWriteDepth) {
    return fail(path, "nesting deeper than " + std::to_string(kMaxWriteDepth) + " levels");
  }
  if (!value.is_object()) {
    return fail(
      path, "expected an object for " + std::string(members.message_name_ ? members.message_name_ : "") +
      ", got " + qar_ui_model::to_string(value.kind()));
  }
  for (const UiObjectEntry & entry : value.entries()) {
    const ric::MessageMember * member = nullptr;
    for (std::uint32_t i = 0; i < members.member_count_; ++i) {
      if (members.members_[i].name_ != nullptr && entry.key == members.members_[i].name_) {
        member = &members.members_[i];
        break;
      }
    }
    if (member == nullptr) {
      if (options.reject_unknown_fields) {
        return fail(join(path, entry.key), "unknown field");
      }
      continue;
    }
    Result result = write_member(entry.value, message, *member, options, join(path, entry.key), depth);
    if (!result.ok()) {
      return result;
    }
  }
  if (options.require_all_fields) {
    for (std::uint32_t i = 0; i < members.member_count_; ++i) {
      const char * name = members.members_[i].name_;
      if (name != nullptr && value.find(name) == nullptr) {
        return fail(join(path, name), "missing field");
      }
    }
  }
  return Result::success();
}

const ric::MessageMembers & empty_members()
{
  static const ric::MessageMembers instance{};
  return instance;
}

}  // namespace

namespace detail
{

UiValue member_to_ui_value(
  const void * message,
  const ric::MessageMember & member,
  const ConversionLimits & limits,
  ConversionReport * report,
  std::size_t depth)
{
  const void * base = static_cast<const std::uint8_t *>(message) + member.offset_;
  const ric::MessageMembers * sub = sub_members(member);

  if (!member.is_array_) {
    if (member.type_id_ == ric::ROS_TYPE_MESSAGE) {
      return sub ? message_to_ui_value_impl(base, *sub, limits, report, depth + 1) : UiValue(nullptr);
    }
    return primitive_to_ui_value(base, member.type_id_);
  }

  const std::size_t count = member.size_function ? member.size_function(base) : 0;
  std::size_t kept = count;
  if (limits.max_array_elements != 0 && count > limits.max_array_elements) {
    kept = limits.max_array_elements;
    if (report != nullptr) {
      ++report->truncated_arrays;
      report->elements_dropped += count - kept;
    }
  }
  UiArray elements;
  elements.reserve(kept);
  if (member.type_id_ == ric::ROS_TYPE_MESSAGE) {
    if (sub != nullptr && member.get_const_function != nullptr) {
      for (std::size_t i = 0; i < kept; ++i) {
        elements.push_back(
          message_to_ui_value_impl(
            member.get_const_function(base, i), *sub, limits, report, depth + 1));
      }
    }
  } else if (member.get_const_function != nullptr) {
    for (std::size_t i = 0; i < kept; ++i) {
      elements.push_back(primitive_to_ui_value(member.get_const_function(base, i), member.type_id_));
    }
  } else if (member.fetch_function != nullptr && member.type_id_ == ric::ROS_TYPE_BOOLEAN) {
    // std::vector<bool>: no element addresses, so the table offers fetch().
    for (std::size_t i = 0; i < kept; ++i) {
      bool scalar = false;
      member.fetch_function(base, i, &scalar);
      elements.push_back(UiValue(scalar));
    }
  }
  return UiValue(std::move(elements));
}

bool ui_value_is_zero(const UiValue & value)
{
  switch (value.kind()) {
    case qar_ui_model::UiValueKind::Null: return true;
    case qar_ui_model::UiValueKind::Bool: return !value.as_bool();
    case qar_ui_model::UiValueKind::Int: return value.as_int() == 0;
    case qar_ui_model::UiValueKind::Double: return value.as_double() == 0.0;
    case qar_ui_model::UiValueKind::String: return value.as_string().empty();
    case qar_ui_model::UiValueKind::Array:
      for (const UiValue & element : value.elements()) {
        if (!ui_value_is_zero(element)) {
          return false;
        }
      }
      return true;
    case qar_ui_model::UiValueKind::Object: return false;
  }
  return false;
}

}  // namespace detail

UiValue message_to_ui_value(
  const void * message,
  const ric::MessageMembers & members,
  const ConversionLimits & limits,
  ConversionReport * report)
{
  if (message == nullptr) {
    return UiValue(nullptr);
  }
  return message_to_ui_value_impl(message, members, limits, report, 0);
}

Result ui_value_to_message(
  const UiValue & value,
  void * message,
  const ric::MessageMembers & members,
  const ConversionOptions & options)
{
  if (message == nullptr) {
    return Result::error(Status::InvalidArgument, "message instance is null");
  }
  return write_message(value, message, members, options, "", 0);
}

std::string utf16_to_utf8(std::u16string_view text)
{
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    char32_t code_point = text[i];
    if (code_point >= 0xD800 && code_point <= 0xDBFF) {
      if (i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
        code_point = 0x10000 + ((code_point - 0xD800) << 10) + (text[i + 1] - 0xDC00);
        ++i;
      } else {
        code_point = qar_ui_model::utf8::kReplacementCharacter;
      }
    } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
      code_point = qar_ui_model::utf8::kReplacementCharacter;
    }
    if (!qar_ui_model::utf8::encode(code_point, out)) {
      qar_ui_model::utf8::encode(qar_ui_model::utf8::kReplacementCharacter, out);
    }
  }
  return out;
}

std::u16string utf8_to_utf16(std::string_view text)
{
  std::u16string out;
  out.reserve(text.size());
  std::size_t pos = 0;
  while (pos < text.size()) {
    char32_t code_point = 0;
    const std::size_t before = pos;
    if (!qar_ui_model::utf8::decode_next(text, pos, code_point)) {
      code_point = qar_ui_model::utf8::kReplacementCharacter;
      pos = before + 1;
    }
    if (code_point >= 0x10000) {
      code_point -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (code_point >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (code_point & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(code_point));
    }
  }
  return out;
}

// --- DynamicMessage ---------------------------------------------------------

DynamicMessage::DynamicMessage(MessageTypeSupport ts, rosidl_runtime_cpp::MessageInitialization init)
: ts_(std::move(ts))
{
  const ric::MessageMembers * members = ts_.members();
  if (!ts_.valid() || members == nullptr || members->size_of_ == 0 ||
    members->init_function == nullptr || members->fini_function == nullptr)
  {
    ts_ = MessageTypeSupport{};
    return;
  }
  data_ = ::operator new(members->size_of_);
  members->init_function(data_, init);
}

DynamicMessage::~DynamicMessage()
{
  destroy();
}

DynamicMessage::DynamicMessage(DynamicMessage && other) noexcept
: ts_(std::move(other.ts_)), data_(other.data_)
{
  other.data_ = nullptr;
  other.ts_ = MessageTypeSupport{};
}

DynamicMessage & DynamicMessage::operator=(DynamicMessage && other) noexcept
{
  if (this != &other) {
    destroy();
    ts_ = std::move(other.ts_);
    data_ = other.data_;
    other.data_ = nullptr;
    other.ts_ = MessageTypeSupport{};
  }
  return *this;
}

Result DynamicMessage::create(
  std::string_view type_name, DynamicMessage & out, TypeSupportRegistry * registry)
{
  TypeSupportRegistry & reg = registry ? *registry : TypeSupportRegistry::global();
  MessageTypeSupport ts;
  Result result = reg.message(type_name, ts);
  if (!result.ok()) {
    return result;
  }
  DynamicMessage message(ts);
  if (!message.valid()) {
    return Result::error(
      Status::TypeSupportUnavailable, "cannot instantiate " + std::string(type_name));
  }
  out = std::move(message);
  return Result::success();
}

const ric::MessageMembers & DynamicMessage::members() const
{
  const ric::MessageMembers * members = ts_.members();
  return members ? *members : empty_members();
}

void DynamicMessage::destroy()
{
  if (data_ != nullptr) {
    const ric::MessageMembers * members = ts_.members();
    if (members != nullptr && members->fini_function != nullptr) {
      members->fini_function(data_);
    }
    ::operator delete(data_);
    data_ = nullptr;
  }
}

void DynamicMessage::reset()
{
  if (!valid()) {
    return;
  }
  const ric::MessageMembers & members = this->members();
  members.fini_function(data_);
  members.init_function(data_, rosidl_runtime_cpp::MessageInitialization::ALL);
}

Result DynamicMessage::from_ui_value(const UiValue & value, const ConversionOptions & options)
{
  if (!valid()) {
    return Result::error(Status::InvalidState, "message instance is empty");
  }
  DynamicMessage scratch(ts_);
  if (!scratch.valid()) {
    return Result::error(Status::InvalidState, "cannot allocate a scratch instance");
  }
  Result result = ui_value_to_message(value, scratch.data_, members(), options);
  if (!result.ok()) {
    return result;
  }
  std::swap(data_, scratch.data_);
  return Result::success();
}

UiValue DynamicMessage::to_ui_value(const ConversionLimits & limits, ConversionReport * report) const
{
  if (!valid()) {
    return UiValue(nullptr);
  }
  return message_to_ui_value(data_, members(), limits, report);
}

Result DynamicMessage::serialize(rclcpp::SerializedMessage & out) const
{
  if (!valid()) {
    return Result::error(Status::InvalidState, "message instance is empty");
  }
  const rmw_ret_t ret = rmw_serialize(data_, ts_.rmw, &out.get_rcl_serialized_message());
  if (ret != RMW_RET_OK) {
    std::string message = rmw_get_error_string().str;
    rmw_reset_error();
    return Result::error(
      Status::SerializationFailed, "rmw_serialize(" + ts_.type_name + ") failed: " + message);
  }
  return Result::success();
}

Result DynamicMessage::deserialize(const rclcpp::SerializedMessage & in)
{
  if (!valid()) {
    return Result::error(Status::InvalidState, "message instance is empty");
  }
  const rmw_ret_t ret = rmw_deserialize(&in.get_rcl_serialized_message(), ts_.rmw, data_);
  if (ret != RMW_RET_OK) {
    std::string message = rmw_get_error_string().str;
    rmw_reset_error();
    return Result::error(
      Status::SerializationFailed, "rmw_deserialize(" + ts_.type_name + ") failed: " + message);
  }
  return Result::success();
}

std::shared_ptr<void> DynamicMessage::release_to_shared(DynamicMessage && message)
{
  if (!message.valid()) {
    return nullptr;
  }
  auto * holder = new DynamicMessage(std::move(message));
  void * data = holder->data();
  return std::shared_ptr<void>(data, [holder](void *) {delete holder;});
}

}  // namespace qar_ros_ui
