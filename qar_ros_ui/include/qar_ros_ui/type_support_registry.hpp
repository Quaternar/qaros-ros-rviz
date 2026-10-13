// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_ROS_UI__TYPE_SUPPORT_REGISTRY_HPP_
#define QAR_ROS_UI__TYPE_SUPPORT_REGISTRY_HPP_

/// @file type_support_registry.hpp
/// @brief Runtime lookup of rosidl typesupport handles from a type-name string.
///
/// Two handles matter for every interface type, and they live in two different
/// shared libraries of the interface package:
///
///   * the **introspection** handle (`<pkg>__rosidl_typesupport_introspection_cpp`)
///     whose `data` is a `MessageMembers` table: field names, type ids, offsets,
///     array accessors, init/fini. This is what lets us read and write an
///     instance we have no C++ type for;
///   * the **rmw** handle (`<pkg>__rosidl_typesupport_cpp`), the dispatch
///     typesupport rclcpp itself uses. `rmw_serialize`, publishers,
///     subscriptions, clients and action clients all take this one.
///
/// Both describe the *same* C++ struct (the generated `<pkg>::msg::<Name>_`),
/// so an instance allocated through introspection can be handed to the rmw
/// layer, and vice versa. That equivalence is what the whole library rests on.
///
/// Libraries are cached and **never unloaded**: a `MessageMembers` table is
/// static data inside the DLL, and every description, instance and subscription
/// in the process points into it. Unloading would turn all of them into
/// dangling pointers, so the registry keeps each library for the process
/// lifetime. See docs/DEVELOPING.md ("ROS 2 Humble introspection notes") for
/// the resolution rules.

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rcpputils/shared_library.hpp"
#include "rosidl_runtime_c/action_type_support_struct.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/service_type_support_struct.h"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "rosidl_typesupport_introspection_cpp/service_introspection.hpp"

#include "qar_ros_ui/types.hpp"

namespace qar_ros_ui
{

/// Typesupport identifiers as the generated symbol names spell them.
constexpr const char * kIntrospectionIdentifier = "rosidl_typesupport_introspection_cpp";
constexpr const char * kRmwIdentifier = "rosidl_typesupport_cpp";

/// @brief Both handles of one message type, plus the libraries that own them.
struct MessageTypeSupport
{
  std::string type_name;  ///< Canonical "pkg/msg/Name" (or "pkg/srv/Name_Request", ...).
  const rosidl_message_type_support_t * introspection = nullptr;
  const rosidl_message_type_support_t * rmw = nullptr;
  std::shared_ptr<rcpputils::SharedLibrary> introspection_library;
  std::shared_ptr<rcpputils::SharedLibrary> rmw_library;

  bool valid() const { return introspection != nullptr && rmw != nullptr; }
  /// The introspection table. Null unless valid().
  const rosidl_typesupport_introspection_cpp::MessageMembers * members() const;
};

struct ServiceTypeSupport
{
  std::string type_name;  ///< "pkg/srv/Name"
  const rosidl_service_type_support_t * introspection = nullptr;
  const rosidl_service_type_support_t * rmw = nullptr;
  std::shared_ptr<rcpputils::SharedLibrary> introspection_library;
  std::shared_ptr<rcpputils::SharedLibrary> rmw_library;
  MessageTypeSupport request;
  MessageTypeSupport response;

  bool valid() const
  {
    return introspection != nullptr && rmw != nullptr && request.valid() && response.valid();
  }
  const rosidl_typesupport_introspection_cpp::ServiceMembers * members() const;
};

/// @brief An action is one rmw handle (five sub-handles inside) plus the eight
/// messages the protocol is built from. Only the rmw library exports an
/// action-level symbol; introspection exposes the parts.
struct ActionTypeSupport
{
  std::string type_name;  ///< "pkg/action/Name"
  const rosidl_action_type_support_t * rmw = nullptr;
  std::shared_ptr<rcpputils::SharedLibrary> rmw_library;
  MessageTypeSupport goal;               ///< pkg/action/Name_Goal
  MessageTypeSupport result;             ///< pkg/action/Name_Result
  MessageTypeSupport feedback;           ///< pkg/action/Name_Feedback
  MessageTypeSupport send_goal_request;  ///< pkg/action/Name_SendGoal_Request
  MessageTypeSupport send_goal_response;
  MessageTypeSupport get_result_request;
  MessageTypeSupport get_result_response;
  MessageTypeSupport feedback_message;   ///< pkg/action/Name_FeedbackMessage

  bool valid() const;
};

/// @brief Loads and caches typesupport libraries and handles.
///
/// Thread-safe. `global()` is what the bridges use by default; a private
/// instance is only useful for tests that want to exercise resolution rules.
class TypeSupportRegistry
{
public:
  TypeSupportRegistry();
  ~TypeSupportRegistry();
  TypeSupportRegistry(const TypeSupportRegistry &) = delete;
  TypeSupportRegistry & operator=(const TypeSupportRegistry &) = delete;

  static TypeSupportRegistry & global();

  /// Install prefixes (…/ros2-windows) are searched before AMENT_PREFIX_PATH.
  void add_search_prefix(std::string prefix);
  std::vector<std::string> search_prefixes() const;

  Result message(std::string_view type_name, MessageTypeSupport & out);
  Result service(std::string_view type_name, ServiceTypeSupport & out);
  Result action(std::string_view type_name, ActionTypeSupport & out);

  /// The library `<package>__<identifier>`, loaded once and kept.
  Result library(
    std::string_view package,
    std::string_view identifier,
    std::shared_ptr<rcpputils::SharedLibrary> & out);

  std::size_t loaded_library_count() const;

  // --- Naming rules, exposed for tests and diagnostics ----------------------

  /// "pkg__rosidl_typesupport_cpp" (no prefix, no extension).
  static std::string library_basename(std::string_view package, std::string_view identifier);
  /// "<id>__get_message_type_support_handle__<pkg>__<interface>__<name>"
  static std::string message_symbol(
    std::string_view identifier, std::string_view package,
    std::string_view interface, std::string_view name);
  static std::string service_symbol(
    std::string_view identifier, std::string_view package,
    std::string_view interface, std::string_view name);
  static std::string action_symbol(
    std::string_view identifier, std::string_view package, std::string_view name);

  /// Candidate paths in search order for a library basename, ending with the
  /// bare platform file name (resolved by the OS loader's search path).
  std::vector<std::string> candidate_paths(std::string_view basename) const;

private:
  Result message_unlocked(std::string_view type_name, MessageTypeSupport & out);
  Result library_unlocked(
    std::string_view package, std::string_view identifier,
    std::shared_ptr<rcpputils::SharedLibrary> & out);
  std::vector<std::string> candidate_paths_unlocked(std::string_view basename) const;

  mutable std::mutex mutex_;
  std::vector<std::string> prefixes_;
  std::unordered_map<std::string, std::shared_ptr<rcpputils::SharedLibrary>> libraries_;
  std::unordered_map<std::string, MessageTypeSupport> messages_;
  std::unordered_map<std::string, ServiceTypeSupport> services_;
  std::unordered_map<std::string, ActionTypeSupport> actions_;
};

}  // namespace qar_ros_ui

#endif  // QAR_ROS_UI__TYPE_SUPPORT_REGISTRY_HPP_
