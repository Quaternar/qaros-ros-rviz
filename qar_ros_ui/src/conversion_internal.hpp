// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__CONVERSION_INTERNAL_HPP_
#define QAR_ROS_UI__CONVERSION_INTERNAL_HPP_

// Shared between dynamic_message.cpp (the conversion) and introspection.cpp
// (which reads IDL defaults off a fresh instance). Not part of the public API.

#include <cstddef>

#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

#include "qar_ui_model/ui_value.hpp"

#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{
namespace detail
{

/// One member of @p message (which must be initialised) as a UiValue.
qar_ui_model::UiValue member_to_ui_value(
  const void * message,
  const rosidl_typesupport_introspection_cpp::MessageMember & member,
  const ConversionLimits & limits,
  ConversionReport * report,
  std::size_t depth);

/// false / 0 / 0.0 / "" / an empty array / an array of zeros / null.
bool ui_value_is_zero(const qar_ui_model::UiValue & value);

}  // namespace detail
}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__CONVERSION_INTERNAL_HPP_
