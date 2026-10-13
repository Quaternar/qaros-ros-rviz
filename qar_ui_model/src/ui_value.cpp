// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ui_model/ui_value.hpp"

#include <cmath>
#include <utility>

namespace qar_ui_model
{

namespace
{

const std::string & empty_string()
{
  static const std::string kEmpty;
  return kEmpty;
}

const UiArray & empty_array()
{
  static const UiArray kEmpty;
  return kEmpty;
}

const UiObject & empty_object()
{
  static const UiObject kEmpty;
  return kEmpty;
}

}  // namespace

const char *
to_string(UiValueKind kind)
{
  switch (kind) {
    case UiValueKind::Null:
      return "null";
    case UiValueKind::Bool:
      return "bool";
    case UiValueKind::Int:
      return "int";
    case UiValueKind::Double:
      return "double";
    case UiValueKind::String:
      return "string";
    case UiValueKind::Array:
      return "array";
    case UiValueKind::Object:
      return "object";
  }
  return "unknown";
}

UiValue::UiValue()
: data_(std::in_place_type<std::nullptr_t>, nullptr) {}

UiValue::UiValue(std::nullptr_t)
: data_(std::in_place_type<std::nullptr_t>, nullptr) {}

UiValue::UiValue(bool value)
: data_(std::in_place_type<bool>, value) {}

UiValue::UiValue(std::int64_t value)
: data_(std::in_place_type<std::int64_t>, value) {}

UiValue::UiValue(double value)
: data_(std::in_place_type<double>, value) {}

UiValue::UiValue(const char * value)
: data_(std::in_place_type<std::string>, value == nullptr ? "" : value) {}

UiValue::UiValue(std::string value)
: data_(std::in_place_type<std::string>, std::move(value)) {}

UiValue::UiValue(std::string_view value)
: data_(std::in_place_type<std::string>, value) {}

UiValue::UiValue(UiArray elements)
: data_(std::in_place_type<UiArray>, std::move(elements)) {}

UiValue::UiValue(UiObject entries)
: data_(std::in_place_type<UiObject>, std::move(entries)) {}

UiValue::UiValue(const UiValue & other) = default;
UiValue::UiValue(UiValue && other) noexcept = default;
UiValue & UiValue::operator=(const UiValue & other) = default;
UiValue & UiValue::operator=(UiValue && other) noexcept = default;
UiValue::~UiValue() = default;

UiValue
UiValue::array()
{
  return UiValue(UiArray{});
}

UiValue
UiValue::object()
{
  return UiValue(UiObject{});
}

UiValueKind
UiValue::kind() const
{
  // The variant's alternative order IS the enum order; keep both in sync.
  return static_cast<UiValueKind>(data_.index());
}

bool
UiValue::as_bool(bool fallback) const
{
  if (const bool * v = std::get_if<bool>(&data_)) {
    return *v;
  }
  return fallback;
}

std::int64_t
UiValue::as_int(std::int64_t fallback) const
{
  if (const std::int64_t * v = std::get_if<std::int64_t>(&data_)) {
    return *v;
  }
  if (const double * d = std::get_if<double>(&data_)) {
    // Exact conversions only: 3.0 is the Int 3, 3.5 and 1e300 are not Ints.
    // The upper bound is 2^63 itself (exactly representable); anything at or
    // above it would overflow int64.
    if (std::isfinite(*d) && *d == std::floor(*d) &&
      *d >= -9223372036854775808.0 && *d < 9223372036854775808.0)
    {
      return static_cast<std::int64_t>(*d);
    }
  }
  return fallback;
}

double
UiValue::as_double(double fallback) const
{
  if (const double * d = std::get_if<double>(&data_)) {
    return *d;
  }
  if (const std::int64_t * v = std::get_if<std::int64_t>(&data_)) {
    return static_cast<double>(*v);
  }
  return fallback;
}

const std::string &
UiValue::as_string() const
{
  if (const std::string * s = std::get_if<std::string>(&data_)) {
    return *s;
  }
  return empty_string();
}

std::string
UiValue::as_string(std::string fallback) const
{
  if (const std::string * s = std::get_if<std::string>(&data_)) {
    return *s;
  }
  return fallback;
}

std::size_t
UiValue::size() const
{
  if (const UiArray * a = std::get_if<UiArray>(&data_)) {
    return a->size();
  }
  if (const UiObject * o = std::get_if<UiObject>(&data_)) {
    return o->size();
  }
  return 0;
}

UiValue &
UiValue::push_back(UiValue element)
{
  if (is_null()) {
    data_.emplace<UiArray>();
  }
  if (UiArray * a = std::get_if<UiArray>(&data_)) {
    a->push_back(std::move(element));
  }
  return *this;
}

const UiValue *
UiValue::at(std::size_t index) const
{
  if (const UiArray * a = std::get_if<UiArray>(&data_)) {
    if (index < a->size()) {
      return &(*a)[index];
    }
  }
  return nullptr;
}

UiValue *
UiValue::at(std::size_t index)
{
  return const_cast<UiValue *>(static_cast<const UiValue &>(*this).at(index));
}

const UiArray &
UiValue::elements() const
{
  if (const UiArray * a = std::get_if<UiArray>(&data_)) {
    return *a;
  }
  return empty_array();
}

UiArray &
UiValue::elements()
{
  if (is_null()) {
    data_.emplace<UiArray>();
  }
  if (UiArray * a = std::get_if<UiArray>(&data_)) {
    return *a;
  }
  // Not an array and not convertible: hand back scratch storage rather than
  // corrupting the value. Mirrors push_back's "leave other kinds untouched".
  static UiArray scratch;
  scratch.clear();
  return scratch;
}

UiValue &
UiValue::set(std::string key, UiValue value)
{
  if (is_null()) {
    data_.emplace<UiObject>();
  }
  if (UiObject * o = std::get_if<UiObject>(&data_)) {
    for (UiObjectEntry & entry : *o) {
      if (entry.key == key) {
        entry.value = std::move(value);
        return *this;
      }
    }
    o->push_back(UiObjectEntry{std::move(key), std::move(value)});
  }
  return *this;
}

const UiValue *
UiValue::find(std::string_view key) const
{
  if (const UiObject * o = std::get_if<UiObject>(&data_)) {
    for (const UiObjectEntry & entry : *o) {
      if (entry.key == key) {
        return &entry.value;
      }
    }
  }
  return nullptr;
}

UiValue *
UiValue::find(std::string_view key)
{
  return const_cast<UiValue *>(static_cast<const UiValue &>(*this).find(key));
}

bool
UiValue::erase(std::string_view key)
{
  if (UiObject * o = std::get_if<UiObject>(&data_)) {
    for (auto it = o->begin(); it != o->end(); ++it) {
      if (it->key == key) {
        o->erase(it);
        return true;
      }
    }
  }
  return false;
}

const UiObject &
UiValue::entries() const
{
  if (const UiObject * o = std::get_if<UiObject>(&data_)) {
    return *o;
  }
  return empty_object();
}

UiObject &
UiValue::entries()
{
  if (is_null()) {
    data_.emplace<UiObject>();
  }
  if (UiObject * o = std::get_if<UiObject>(&data_)) {
    return *o;
  }
  static UiObject scratch;
  scratch.clear();
  return scratch;
}

bool
UiValue::operator==(const UiValue & other) const
{
  if (data_.index() != other.data_.index()) {
    return false;
  }
  switch (kind()) {
    case UiValueKind::Null:
      return true;
    case UiValueKind::Bool:
      return std::get<bool>(data_) == std::get<bool>(other.data_);
    case UiValueKind::Int:
      return std::get<std::int64_t>(data_) == std::get<std::int64_t>(other.data_);
    case UiValueKind::Double:
      return std::get<double>(data_) == std::get<double>(other.data_);
    case UiValueKind::String:
      return std::get<std::string>(data_) == std::get<std::string>(other.data_);
    case UiValueKind::Array: {
      const UiArray & a = std::get<UiArray>(data_);
      const UiArray & b = std::get<UiArray>(other.data_);
      if (a.size() != b.size()) {
        return false;
      }
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (!(a[i] == b[i])) {
          return false;
        }
      }
      return true;
    }
    case UiValueKind::Object: {
      const UiObject & a = std::get<UiObject>(data_);
      const UiObject & b = std::get<UiObject>(other.data_);
      if (a.size() != b.size()) {
        return false;
      }
      for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].key != b[i].key || !(a[i].value == b[i].value)) {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}

}  // namespace qar_ui_model
