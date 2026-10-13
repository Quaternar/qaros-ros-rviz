// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__TYPES_HPP_
#define QAR_ROS_UI__TYPES_HPP_

/// @file types.hpp
/// @brief Status/Result, the diagnostics sink and the conversion knobs shared by
/// every component of qar_ros_ui.
///
/// Nothing in this library throws across its public API: every operation that
/// can fail returns a @ref Result, because the callers are GUI threads and web
/// handlers that must stay alive whatever the ROS graph does. rclcpp exceptions
/// are caught at the boundary and folded into Status::RclError.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace qar_ros_ui
{

enum class Status
{
  Ok = 0,
  InvalidArgument,         ///< A caller-supplied argument was empty/malformed.
  InvalidState,            ///< Operation not valid in the current object state.
  NotFound,                ///< Topic/service/action/type absent from the graph or registry.
  TypeSupportUnavailable,  ///< The typesupport library or its symbol could not be loaded.
  ConversionFailed,        ///< UiValue <-> message instance mismatch (kind, range, length).
  SerializationFailed,     ///< rmw_serialize / rmw_deserialize refused the message.
  RclError,                ///< An rcl/rmw/rclcpp call failed; the message carries its text.
  Timeout,                 ///< No response within the caller's deadline.
  Rejected,                ///< A server refused (goal rejected, cancel refused).
  Unsupported              ///< Valid ROS, but outside what this library maps.
};
const char * to_string(Status status);

struct Result
{
  Status status = Status::Ok;
  std::string message;

  bool ok() const { return status == Status::Ok; }
  explicit operator bool() const { return ok(); }

  static Result success() { return {Status::Ok, {}}; }
  static Result error(Status s, std::string msg) { return {s, std::move(msg)}; }
};

enum class DiagnosticSeverity
{
  Info,
  Warning,
  Error
};
const char * to_string(DiagnosticSeverity severity);

/// @brief Optional host-supplied sink for things worth telling the operator
/// (a re-subscription because a publisher's QoS changed, a message that could
/// not be converted, ...). Called on whichever thread produced the event; the
/// implementation must be cheap and thread-safe. Categories: "typesupport",
/// "graph", "topic", "service", "action".
class RosUiDiagnosticsSink
{
public:
  virtual ~RosUiDiagnosticsSink() = default;
  virtual void on_message(
    DiagnosticSeverity severity,
    const std::string & category,
    const std::string & message) = 0;
};

/// @brief Caps applied when a message instance becomes a UiValue. A point cloud
/// has millions of elements and a UI shows none of them; the caps keep the
/// conversion bounded on whatever thread runs it. 0 = unlimited.
struct ConversionLimits
{
  std::size_t max_array_elements = 4096;
  std::size_t max_depth = 64;
};

/// @brief Rules applied when a UiValue becomes a message instance.
struct ConversionOptions
{
  /// Fail on keys the type does not declare. Off: they are ignored.
  bool reject_unknown_fields = true;
  /// Fail when a declared field is absent. Off: absent fields keep the
  /// instance's current value (its IDL default for a fresh instance).
  bool require_all_fields = false;
};

/// @brief What the instance -> UiValue conversion had to leave out.
struct ConversionReport
{
  std::size_t truncated_arrays = 0;
  std::size_t elements_dropped = 0;
  bool depth_exceeded = false;

  bool clean() const { return truncated_arrays == 0 && !depth_exceeded; }
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__TYPES_HPP_
