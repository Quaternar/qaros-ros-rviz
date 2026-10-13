// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/type_support_registry.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

#include "qar_ui_model/type_description.hpp"

namespace qar_ros_ui
{

namespace
{

using MessageHandleFn = const rosidl_message_type_support_t * (*)();
using ServiceHandleFn = const rosidl_service_type_support_t * (*)();
using ActionHandleFn = const rosidl_action_type_support_t * (*)();

struct TypeParts
{
  std::string package;
  std::string interface;
  std::string name;
};

bool is_identifier(std::string_view text)
{
  if (text.empty()) {
    return false;
  }
  const auto alpha = [](char c) {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
  if (!alpha(text[0])) {
    return false;
  }
  for (char c : text) {
    if (!alpha(c) && !(c >= '0' && c <= '9')) {
      return false;
    }
  }
  return true;
}

bool parse_type_name(std::string_view type_name, TypeParts & out, std::string & error)
{
  qar_ui_model::TypeNameParts parts;
  if (!qar_ui_model::split_type_name(type_name, parts)) {
    error = "'" + std::string(type_name) +
      "' is not a ROS type name (expected pkg/msg/Name, pkg/srv/Name or pkg/action/Name)";
    return false;
  }
  if (!is_identifier(parts.package) || !is_identifier(parts.name)) {
    error = "'" + std::string(type_name) + "' has a package or type name that is not an identifier";
    return false;
  }
  out.package = std::move(parts.package);
  out.interface = std::move(parts.interface);
  out.name = std::move(parts.name);
  return true;
}

std::string canonical_name(const TypeParts & parts)
{
  return parts.package + "/" + parts.interface + "/" + parts.name;
}

std::vector<std::string> split_path_list(const char * value)
{
  std::vector<std::string> out;
  if (value == nullptr) {
    return out;
  }
#ifdef _WIN32
  const char separator = ';';
#else
  const char separator = ':';
#endif
  std::string_view text(value);
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find(separator, start);
    const std::string_view part =
      text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (!part.empty()) {
      out.emplace_back(part);
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return out;
}

std::vector<std::string> ament_prefix_path_entries()
{
#ifdef _MSC_VER
  char * value = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&value, &length, "AMENT_PREFIX_PATH") != 0 || value == nullptr) {
    return {};
  }
  std::vector<std::string> entries = split_path_list(value);
  std::free(value);
  return entries;
#else
  return split_path_list(std::getenv("AMENT_PREFIX_PATH"));
#endif
}

std::string library_path_of(rcpputils::SharedLibrary & library)
{
  try {
    return library.get_library_path();
  } catch (const std::exception &) {
    return "<unknown library path>";
  }
}

template<typename Fn>
Fn resolve_symbol(rcpputils::SharedLibrary & library, const std::string & symbol, std::string & error)
{
  try {
    if (!library.has_symbol(symbol)) {
      error = "symbol '" + symbol + "' is not exported by " + library_path_of(library);
      return nullptr;
    }
    return reinterpret_cast<Fn>(library.get_symbol(symbol));
  } catch (const std::exception & e) {
    error = e.what();
    return nullptr;
  }
}

}  // namespace

const rosidl_typesupport_introspection_cpp::MessageMembers * MessageTypeSupport::members() const
{
  if (introspection == nullptr || introspection->data == nullptr) {
    return nullptr;
  }
  return static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>(
    introspection->data);
}

const rosidl_typesupport_introspection_cpp::ServiceMembers * ServiceTypeSupport::members() const
{
  if (introspection == nullptr || introspection->data == nullptr) {
    return nullptr;
  }
  return static_cast<const rosidl_typesupport_introspection_cpp::ServiceMembers *>(
    introspection->data);
}

bool ActionTypeSupport::valid() const
{
  return rmw != nullptr && goal.valid() && result.valid() && feedback.valid() &&
         send_goal_request.valid() && send_goal_response.valid() &&
         get_result_request.valid() && get_result_response.valid() && feedback_message.valid();
}

TypeSupportRegistry::TypeSupportRegistry() = default;
TypeSupportRegistry::~TypeSupportRegistry() = default;

TypeSupportRegistry & TypeSupportRegistry::global()
{
  // Leaked on purpose: the libraries must outlive every static that may still
  // point into them during process teardown.
  static TypeSupportRegistry * instance = new TypeSupportRegistry();
  return *instance;
}

void TypeSupportRegistry::add_search_prefix(std::string prefix)
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (const std::string & existing : prefixes_) {
    if (existing == prefix) {
      return;
    }
  }
  prefixes_.push_back(std::move(prefix));
}

std::vector<std::string> TypeSupportRegistry::search_prefixes() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return prefixes_;
}

std::size_t TypeSupportRegistry::loaded_library_count() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return libraries_.size();
}

std::string TypeSupportRegistry::library_basename(
  std::string_view package, std::string_view identifier)
{
  return std::string(package) + "__" + std::string(identifier);
}

std::string TypeSupportRegistry::message_symbol(
  std::string_view identifier, std::string_view package,
  std::string_view interface, std::string_view name)
{
  return std::string(identifier) + "__get_message_type_support_handle__" +
         std::string(package) + "__" + std::string(interface) + "__" + std::string(name);
}

std::string TypeSupportRegistry::service_symbol(
  std::string_view identifier, std::string_view package,
  std::string_view interface, std::string_view name)
{
  return std::string(identifier) + "__get_service_type_support_handle__" +
         std::string(package) + "__" + std::string(interface) + "__" + std::string(name);
}

std::string TypeSupportRegistry::action_symbol(
  std::string_view identifier, std::string_view package, std::string_view name)
{
  return std::string(identifier) + "__get_action_type_support_handle__" +
         std::string(package) + "__action__" + std::string(name);
}

std::vector<std::string> TypeSupportRegistry::candidate_paths(std::string_view basename) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return candidate_paths_unlocked(basename);
}

std::vector<std::string> TypeSupportRegistry::candidate_paths_unlocked(std::string_view basename) const
{
  std::string file_name;
  try {
    file_name = rcpputils::get_platform_library_name(std::string(basename));
  } catch (const std::exception &) {
#ifdef _WIN32
    file_name = std::string(basename) + ".dll";
#else
    file_name = "lib" + std::string(basename) + ".so";
#endif
  }
#ifdef _WIN32
  const char * library_dir = "bin";
#else
  const char * library_dir = "lib";
#endif

  std::vector<std::string> prefixes = prefixes_;
  for (const std::string & entry : ament_prefix_path_entries()) {
    prefixes.push_back(entry);
  }

  std::vector<std::string> candidates;
  for (const std::string & prefix : prefixes) {
    const std::string path = prefix + "/" + library_dir + "/" + file_name;
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(path), ec)) {
      candidates.push_back(path);
    }
  }
  // Last resort: the OS loader's own search (PATH on Windows), which is how the
  // process found rclcpp.dll in the first place.
  candidates.push_back(file_name);
  return candidates;
}

Result TypeSupportRegistry::library(
  std::string_view package,
  std::string_view identifier,
  std::shared_ptr<rcpputils::SharedLibrary> & out)
{
  std::lock_guard<std::mutex> lock(mutex_);
  return library_unlocked(package, identifier, out);
}

Result TypeSupportRegistry::library_unlocked(
  std::string_view package,
  std::string_view identifier,
  std::shared_ptr<rcpputils::SharedLibrary> & out)
{
  const std::string key = library_basename(package, identifier);
  const auto cached = libraries_.find(key);
  if (cached != libraries_.end()) {
    out = cached->second;
    return Result::success();
  }
  std::string last_error = "no candidate location";
  std::size_t attempts = 0;
  for (const std::string & path : candidate_paths_unlocked(key)) {
    ++attempts;
    try {
      auto library = std::make_shared<rcpputils::SharedLibrary>(path);
      libraries_[key] = library;
      out = library;
      return Result::success();
    } catch (const std::exception & e) {
      last_error = e.what();
    }
  }
  return Result::error(
    Status::TypeSupportUnavailable,
    "cannot load typesupport library '" + key + "' (" + std::to_string(attempts) +
    " location(s) tried; last error: " + last_error + ")");
}

Result TypeSupportRegistry::message(std::string_view type_name, MessageTypeSupport & out)
{
  std::lock_guard<std::mutex> lock(mutex_);
  return message_unlocked(type_name, out);
}

Result TypeSupportRegistry::message_unlocked(std::string_view type_name, MessageTypeSupport & out)
{
  TypeParts parts;
  std::string error;
  if (!parse_type_name(type_name, parts, error)) {
    return Result::error(Status::InvalidArgument, error);
  }
  const std::string key = canonical_name(parts);
  const auto cached = messages_.find(key);
  if (cached != messages_.end()) {
    out = cached->second;
    return Result::success();
  }

  MessageTypeSupport ts;
  ts.type_name = key;
  Result result = library_unlocked(parts.package, kIntrospectionIdentifier, ts.introspection_library);
  if (!result.ok()) {
    return result;
  }
  result = library_unlocked(parts.package, kRmwIdentifier, ts.rmw_library);
  if (!result.ok()) {
    return result;
  }

  auto introspection_fn = resolve_symbol<MessageHandleFn>(
    *ts.introspection_library,
    message_symbol(kIntrospectionIdentifier, parts.package, parts.interface, parts.name), error);
  if (introspection_fn == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "no introspection for " + key + ": " + error);
  }
  ts.introspection = introspection_fn();
  if (ts.introspection == nullptr || ts.introspection->data == nullptr ||
    ts.introspection->typesupport_identifier == nullptr ||
    std::string_view(ts.introspection->typesupport_identifier) != kIntrospectionIdentifier)
  {
    return Result::error(
      Status::TypeSupportUnavailable, "introspection handle for " + key + " is malformed");
  }

  auto rmw_fn = resolve_symbol<MessageHandleFn>(
    *ts.rmw_library, message_symbol(kRmwIdentifier, parts.package, parts.interface, parts.name),
    error);
  if (rmw_fn == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "no rmw typesupport for " + key + ": " + error);
  }
  ts.rmw = rmw_fn();
  if (ts.rmw == nullptr) {
    return Result::error(Status::TypeSupportUnavailable, "rmw typesupport handle for " + key + " is null");
  }

  messages_[key] = ts;
  out = ts;
  return Result::success();
}

Result TypeSupportRegistry::service(std::string_view type_name, ServiceTypeSupport & out)
{
  std::lock_guard<std::mutex> lock(mutex_);
  TypeParts parts;
  std::string error;
  if (!parse_type_name(type_name, parts, error)) {
    return Result::error(Status::InvalidArgument, error);
  }
  const std::string key = canonical_name(parts);
  const auto cached = services_.find(key);
  if (cached != services_.end()) {
    out = cached->second;
    return Result::success();
  }

  ServiceTypeSupport ts;
  ts.type_name = key;
  Result result = library_unlocked(parts.package, kIntrospectionIdentifier, ts.introspection_library);
  if (!result.ok()) {
    return result;
  }
  result = library_unlocked(parts.package, kRmwIdentifier, ts.rmw_library);
  if (!result.ok()) {
    return result;
  }
  auto introspection_fn = resolve_symbol<ServiceHandleFn>(
    *ts.introspection_library,
    service_symbol(kIntrospectionIdentifier, parts.package, parts.interface, parts.name), error);
  if (introspection_fn == nullptr) {
    return Result::error(
      Status::TypeSupportUnavailable, "no service introspection for " + key + ": " + error);
  }
  ts.introspection = introspection_fn();
  if (ts.introspection == nullptr || ts.introspection->data == nullptr) {
    return Result::error(
      Status::TypeSupportUnavailable, "service introspection handle for " + key + " is malformed");
  }
  auto rmw_fn = resolve_symbol<ServiceHandleFn>(
    *ts.rmw_library, service_symbol(kRmwIdentifier, parts.package, parts.interface, parts.name),
    error);
  if (rmw_fn == nullptr) {
    return Result::error(
      Status::TypeSupportUnavailable, "no rmw service typesupport for " + key + ": " + error);
  }
  ts.rmw = rmw_fn();
  if (ts.rmw == nullptr) {
    return Result::error(
      Status::TypeSupportUnavailable, "rmw service typesupport handle for " + key + " is null");
  }
  // The halves are ordinary messages living in the service's namespace.
  result = message_unlocked(key + "_Request", ts.request);
  if (!result.ok()) {
    return result;
  }
  result = message_unlocked(key + "_Response", ts.response);
  if (!result.ok()) {
    return result;
  }

  services_[key] = ts;
  out = ts;
  return Result::success();
}

Result TypeSupportRegistry::action(std::string_view type_name, ActionTypeSupport & out)
{
  std::lock_guard<std::mutex> lock(mutex_);
  TypeParts parts;
  std::string error;
  if (!parse_type_name(type_name, parts, error)) {
    return Result::error(Status::InvalidArgument, error);
  }
  if (parts.interface != "action") {
    return Result::error(
      Status::InvalidArgument, "'" + std::string(type_name) + "' is not an action type");
  }
  const std::string key = canonical_name(parts);
  const auto cached = actions_.find(key);
  if (cached != actions_.end()) {
    out = cached->second;
    return Result::success();
  }

  ActionTypeSupport ts;
  ts.type_name = key;
  Result result = library_unlocked(parts.package, kRmwIdentifier, ts.rmw_library);
  if (!result.ok()) {
    return result;
  }
  auto rmw_fn = resolve_symbol<ActionHandleFn>(
    *ts.rmw_library, action_symbol(kRmwIdentifier, parts.package, parts.name), error);
  if (rmw_fn == nullptr) {
    return Result::error(
      Status::TypeSupportUnavailable, "no rmw action typesupport for " + key + ": " + error);
  }
  ts.rmw = rmw_fn();
  if (ts.rmw == nullptr || ts.rmw->goal_service_type_support == nullptr ||
    ts.rmw->result_service_type_support == nullptr ||
    ts.rmw->cancel_service_type_support == nullptr ||
    ts.rmw->feedback_message_type_support == nullptr ||
    ts.rmw->status_message_type_support == nullptr)
  {
    return Result::error(
      Status::TypeSupportUnavailable, "rmw action typesupport handle for " + key + " is incomplete");
  }

  struct Part
  {
    const char * suffix;
    MessageTypeSupport * target;
  };
  const Part parts_to_load[] = {
    {"_Goal", &ts.goal},
    {"_Result", &ts.result},
    {"_Feedback", &ts.feedback},
    {"_SendGoal_Request", &ts.send_goal_request},
    {"_SendGoal_Response", &ts.send_goal_response},
    {"_GetResult_Request", &ts.get_result_request},
    {"_GetResult_Response", &ts.get_result_response},
    {"_FeedbackMessage", &ts.feedback_message},
  };
  for (const Part & part : parts_to_load) {
    result = message_unlocked(key + part.suffix, *part.target);
    if (!result.ok()) {
      return result;
    }
  }

  actions_[key] = ts;
  out = ts;
  return Result::success();
}

}  // namespace qar_ros_ui
