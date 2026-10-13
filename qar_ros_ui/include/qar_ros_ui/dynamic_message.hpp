// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__DYNAMIC_MESSAGE_HPP_
#define QAR_ROS_UI__DYNAMIC_MESSAGE_HPP_

/// @file dynamic_message.hpp
/// @brief A message instance owned without knowing its C++ type, and the two
/// conversions between such an instance and a `qar_ui_model::UiValue`.
///
/// Memory: `size_of_` bytes from `operator new`, constructed in place by the
/// table's `init_function` (which runs the generated constructor, defaults
/// included) and destroyed by `fini_function`. Fields are reached through
/// `offset_`; arrays through the table's accessor functions, never by guessing
/// the container type — `std::vector<bool>` has no element addresses and a
/// `BoundedVector` is not a `std::vector`, and the accessors hide both.
///
/// Value conventions (the schema's, see `qar_ui_model/type_description.hpp`):
///   * Byte/Char/WChar and every integer kind travel as Int; a UInt64 above
///     INT64_MAX becomes a Double, and comes back saturating at UINT64_MAX;
///   * Float32/Float64/LongDouble travel as Double; NaN and +-Inf become Null
///     on the way out, and Null becomes NaN on the way in (JSON has no NaN);
///   * String is carried as-is (bytes); WString is transcoded UTF-16 <-> UTF-8
///     with U+FFFD for anything ill-formed;
///   * an integer kind accepts a Double only when integral and in range;
///   * fixed arrays must arrive with exactly N elements, bounded ones with at
///     most N, bounded strings within their byte bound.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "rclcpp/serialized_message.hpp"
#include "rosidl_runtime_cpp/message_initialization.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

#include "qar_ui_model/ui_value.hpp"

#include "qar_ros_ui/type_support_registry.hpp"
#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

/// @brief Instance -> UiValue over raw memory + table. Used on sub-messages too.
qar_ui_model::UiValue message_to_ui_value(
  const void * message,
  const rosidl_typesupport_introspection_cpp::MessageMembers & members,
  const ConversionLimits & limits = {},
  ConversionReport * report = nullptr);

/// @brief UiValue -> instance over raw memory + table. The instance must be
/// initialised. Fields absent from the value keep what they hold. On failure
/// the instance may be partially written; callers that need atomicity convert
/// into a scratch instance first (DynamicMessage::from_ui_value does).
Result ui_value_to_message(
  const qar_ui_model::UiValue & value,
  void * message,
  const rosidl_typesupport_introspection_cpp::MessageMembers & members,
  const ConversionOptions & options = {});

std::string utf16_to_utf8(std::u16string_view text);
std::u16string utf8_to_utf16(std::string_view text);

class DynamicMessage
{
public:
  DynamicMessage() = default;
  /// Allocates and constructs (defaults applied for ALL). Invalid if @p ts is.
  explicit DynamicMessage(
    MessageTypeSupport ts,
    rosidl_runtime_cpp::MessageInitialization init =
    rosidl_runtime_cpp::MessageInitialization::ALL);
  ~DynamicMessage();
  DynamicMessage(DynamicMessage && other) noexcept;
  DynamicMessage & operator=(DynamicMessage && other) noexcept;
  DynamicMessage(const DynamicMessage &) = delete;
  DynamicMessage & operator=(const DynamicMessage &) = delete;

  /// Type name -> fresh default instance through @p registry (global when null).
  static Result create(
    std::string_view type_name,
    DynamicMessage & out,
    TypeSupportRegistry * registry = nullptr);

  bool valid() const { return data_ != nullptr; }
  void * data() { return data_; }
  const void * data() const { return data_; }
  const MessageTypeSupport & type_support() const { return ts_; }
  const std::string & type_name() const { return ts_.type_name; }
  const rosidl_typesupport_introspection_cpp::MessageMembers & members() const;

  /// Destroy and re-construct with defaults.
  void reset();

  /// Atomic: converts into a scratch instance first, then swaps.
  Result from_ui_value(const qar_ui_model::UiValue & value, const ConversionOptions & options = {});
  qar_ui_model::UiValue to_ui_value(
    const ConversionLimits & limits = {}, ConversionReport * report = nullptr) const;

  /// rmw_serialize with the rmw handle (CDR under every rmw shipped).
  Result serialize(rclcpp::SerializedMessage & out) const;
  Result deserialize(const rclcpp::SerializedMessage & in);

  /// Hands the instance to a `std::shared_ptr<void>` whose deleter destroys it —
  /// the shape rclcpp's type-erased client/action APIs want.
  static std::shared_ptr<void> release_to_shared(DynamicMessage && message);

private:
  void destroy();

  MessageTypeSupport ts_;
  void * data_ = nullptr;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__DYNAMIC_MESSAGE_HPP_
