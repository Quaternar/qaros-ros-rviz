// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__UI_VALUE_HPP_
#define QAR_UI_MODEL__UI_VALUE_HPP_

/// @file ui_value.hpp
/// @brief Dynamic value tree: the ONE data shape that flows in both directions
/// between a ROS message (qar_ros_ui), the AR page (qar_web_panels) and the
/// widget model here.
///
/// Seven kinds — exactly JSON's data model (null / bool / integer / double /
/// string / array / object) — so every value has one obvious wire encoding and
/// nothing is lost between C++ and the browser. Integer and double are kept
/// apart although JSON does not distinguish them: a ROS `int64` field must not
/// silently pass through a double and lose its low bits.
///
/// Objects keep INSERTION order. A ROS message's field order is meaningful
/// (header first, x before y before z) and a generated UI must not reshuffle it
/// alphabetically; it also keeps serialization deterministic. Lookups are linear,
/// which is the right trade for the few dozen fields a message has.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace qar_ui_model
{

/// @brief The seven value kinds. Numbered so they are stable in diagnostics.
enum class UiValueKind
{
  Null = 0,
  Bool,
  Int,
  Double,
  String,
  Array,
  Object
};

/// @brief Human-readable name for a @ref UiValueKind ("null", "bool", ...).
const char * to_string(UiValueKind kind);

class UiValue;
struct UiObjectEntry;

using UiArray = std::vector<UiValue>;
using UiObject = std::vector<UiObjectEntry>;

class UiValue
{
public:
  UiValue();
  UiValue(std::nullptr_t);  // NOLINT(google-explicit-constructor): literal syntax is the point
  UiValue(bool value);      // NOLINT(google-explicit-constructor)
  UiValue(std::int64_t value);  // NOLINT(google-explicit-constructor)
  UiValue(double value);        // NOLINT(google-explicit-constructor)
  UiValue(const char * value);  // NOLINT(google-explicit-constructor)
  UiValue(std::string value);   // NOLINT(google-explicit-constructor)
  UiValue(std::string_view value);  // NOLINT(google-explicit-constructor)
  UiValue(UiArray elements);    // NOLINT(google-explicit-constructor)
  UiValue(UiObject entries);    // NOLINT(google-explicit-constructor)

  /// Any other integral type (int, unsigned, long, uint64_t, ...). Character
  /// types are excluded so a `char` never turns into its code point by accident.
  /// An unsigned value above INT64_MAX cannot be an Int and becomes a Double —
  /// the same loss JSON/JavaScript would impose anyway.
  template<
    typename T,
    std::enable_if_t<
      std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> &&
      !std::is_same_v<T, wchar_t> && !std::is_same_v<T, char16_t> &&
      !std::is_same_v<T, char32_t>, int> = 0>
  UiValue(T value)  // NOLINT(google-explicit-constructor)
  : UiValue(from_integral(value)) {}

  /// Any other floating-point type (float, long double).
  template<typename T, std::enable_if_t<std::is_floating_point_v<T> && !std::is_same_v<T, double>, int> = 0>
  UiValue(T value)  // NOLINT(google-explicit-constructor)
  : UiValue(static_cast<double>(value)) {}

  UiValue(const UiValue & other);
  UiValue(UiValue && other) noexcept;
  UiValue & operator=(const UiValue & other);
  UiValue & operator=(UiValue && other) noexcept;
  ~UiValue();

  static UiValue array();
  static UiValue object();

  UiValueKind kind() const;
  bool is_null() const { return kind() == UiValueKind::Null; }
  bool is_bool() const { return kind() == UiValueKind::Bool; }
  bool is_int() const { return kind() == UiValueKind::Int; }
  bool is_double() const { return kind() == UiValueKind::Double; }
  bool is_number() const { return is_int() || is_double(); }
  bool is_string() const { return kind() == UiValueKind::String; }
  bool is_array() const { return kind() == UiValueKind::Array; }
  bool is_object() const { return kind() == UiValueKind::Object; }

  // --- Scalar access. Each returns @p fallback when the kind does not match. --
  // Numbers convert between Int and Double (a Double is only an Int when it is
  // integral and in range); nothing else converts. Strings never coerce to
  // numbers and numbers never coerce to strings — that is a schema decision
  // (see type_description.hpp), not a value decision.

  bool as_bool(bool fallback = false) const;
  std::int64_t as_int(std::int64_t fallback = 0) const;
  double as_double(double fallback = 0.0) const;
  const std::string & as_string() const;  ///< Empty string unless String.
  std::string as_string(std::string fallback) const;

  // --- Container access. ---------------------------------------------------

  /// Element / entry count for Array / Object; 0 for every other kind.
  std::size_t size() const;
  bool empty() const { return size() == 0; }

  /// Append to an Array. A Null converts to an Array first; any other kind is
  /// left untouched (appending to a string is a caller bug, not a conversion).
  UiValue & push_back(UiValue element);
  const UiValue * at(std::size_t index) const;  ///< nullptr unless Array and in range.
  UiValue * at(std::size_t index);
  const UiArray & elements() const;  ///< Empty for non-arrays.
  UiArray & elements();              ///< A Null converts to an empty Array first.

  /// Insert or replace a key in an Object, keeping the original position when
  /// replacing. A Null converts to an Object first; any other kind is left
  /// untouched. Returns *this so calls chain.
  UiValue & set(std::string key, UiValue value);
  const UiValue * find(std::string_view key) const;  ///< nullptr unless Object and present.
  UiValue * find(std::string_view key);
  bool contains(std::string_view key) const { return find(key) != nullptr; }
  bool erase(std::string_view key);
  const UiObject & entries() const;  ///< Empty for non-objects.
  UiObject & entries();              ///< A Null converts to an empty Object first.

  /// Strict: kinds must match (Int 5 != Double 5.0), objects compare in order.
  bool operator==(const UiValue & other) const;
  bool operator!=(const UiValue & other) const { return !(*this == other); }

private:
  template<typename T>
  static UiValue from_integral(T value)
  {
    if constexpr (std::is_unsigned_v<T>) {
      if (static_cast<std::uint64_t>(value) > static_cast<std::uint64_t>(INT64_MAX)) {
        return UiValue(static_cast<double>(value));
      }
    }
    return UiValue(static_cast<std::int64_t>(value));
  }

  std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, UiArray, UiObject> data_;
};

/// @brief One key/value pair of an Object, in insertion order.
struct UiObjectEntry
{
  std::string key;
  UiValue value;
};

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__UI_VALUE_HPP_
