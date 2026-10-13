// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_control_router.hpp"

#include <utility>

#include "qar_rviz_streaming/ar_panel_generator.hpp"

namespace qar_rviz_streaming
{
namespace
{
using qar_ui_model::FieldPath;
using qar_ui_model::UiValue;

bool action_kind_from_id(const std::string & action, RosActionKind & out_kind)
{
  if (action == ar_actions::kSetField) {
    out_kind = RosActionKind::SetField;
    return true;
  }
  if (action == ar_actions::kPublish) {
    out_kind = RosActionKind::PublishTopic;
    return true;
  }
  if (action == ar_actions::kCall) {
    out_kind = RosActionKind::CallService;
    return true;
  }
  if (action == ar_actions::kSendGoal) {
    out_kind = RosActionKind::SendGoal;
    return true;
  }
  if (action == ar_actions::kCancelGoal) {
    out_kind = RosActionKind::CancelGoal;
    return true;
  }
  if (action == ar_actions::kOpenVideo) {
    out_kind = RosActionKind::OpenVideoPanel;
    return true;
  }
  return false;
}

/// An action only means something for the entity it was generated for; a
/// `ros.publish` arriving on a service panel is a bug or an attack, never a typo.
bool action_applies(RosEntityKind entity_kind, RosActionKind action)
{
  switch (action) {
    case RosActionKind::SetField:
      return true;
    case RosActionKind::PublishTopic:
      return entity_kind == RosEntityKind::Topic;
    case RosActionKind::CallService:
      return entity_kind == RosEntityKind::Service;
    case RosActionKind::SendGoal:
    case RosActionKind::CancelGoal:
      return entity_kind == RosEntityKind::Action;
    case RosActionKind::OpenVideoPanel:
      // Coarse gate: right ENTITY KIND. The finer "which message type" check
      // (Image / CompressedImage only) happens in route(), which is the only
      // place that can see the entity's type_name.
      return entity_kind == RosEntityKind::Topic;
    case RosActionKind::None:
      return false;
  }
  return false;
}

bool is_streamable_image_type(const std::string & type_name)
{
  return type_name == "sensor_msgs/msg/Image" || type_name == "sensor_msgs/msg/CompressedImage";
}

ControlRouting refuse(std::string error)
{
  ControlRouting routing;
  routing.ok = false;
  routing.error = std::move(error);
  return routing;
}

}  // namespace

const char * to_string(RosActionKind kind)
{
  switch (kind) {
    case RosActionKind::None:
      return "none";
    case RosActionKind::SetField:
      return "set";
    case RosActionKind::PublishTopic:
      return "publish";
    case RosActionKind::CallService:
      return "call";
    case RosActionKind::SendGoal:
      return "send_goal";
    case RosActionKind::CancelGoal:
      return "cancel_goal";
    case RosActionKind::OpenVideoPanel:
      return "open_video_panel";
  }
  return "none";
}

const char * ArControlRouter::outbound_key(RosActionKind kind)
{
  switch (kind) {
    case RosActionKind::PublishTopic:
      return ar_doc::kDraft;
    case RosActionKind::CallService:
      return ar_doc::kRequest;
    case RosActionKind::SendGoal:
      return ar_doc::kGoal;
    default:
      return nullptr;
  }
}

ArControlRouter::ArControlRouter(
  RosEntityRef entity,
  qar_ui_model::UiModel model,
  qar_ui_model::MessageDescriptionPtr schema)
: entity_(std::move(entity)),
  model_(std::move(model)),
  schema_(std::move(schema))
{
  document_ = schema_ ? qar_ui_model::make_default_value(*schema_) : UiValue::object();
  if (!document_.is_object()) {
    document_ = UiValue::object();
  }
}

bool ArControlRouter::set_document_value(
  const std::string & path, UiValue value, std::string * out_error)
{
  FieldPath parsed;
  std::string error;
  if (!FieldPath::parse(path, parsed, error)) {
    if (out_error != nullptr) {
      *out_error = "invalid path '" + path + "': " + error;
    }
    return false;
  }
  if (!qar_ui_model::set_path(document_, parsed, std::move(value), error)) {
    if (out_error != nullptr) {
      *out_error = "could not write '" + path + "': " + error;
    }
    return false;
  }
  return true;
}

ControlRouting ArControlRouter::route(const qar_ui_model::ControlBody & control)
{
  if (control.control.empty()) {
    return refuse("control event names no widget");
  }
  const qar_ui_model::Widget * widget = model_.find(control.control);
  if (widget == nullptr) {
    return refuse("unknown control '" + control.control + "'");
  }

  // The frame's action wins only when the widget declares none: a page must not
  // be able to turn a "set this field" widget into a "publish" by relabelling it.
  std::string action_id = widget->action;
  if (action_id.empty()) {
    action_id = control.action;
  } else if (!control.action.empty() && control.action != action_id) {
    return refuse(
      "control '" + control.control + "' declares action '" + action_id +
      "' but the event named '" + control.action + "'");
  }
  if (action_id.empty()) {
    return refuse("control '" + control.control + "' has no action");
  }

  RosActionKind kind = RosActionKind::None;
  if (!action_kind_from_id(action_id, kind)) {
    return refuse("unknown action '" + action_id + "'");
  }
  if (!action_applies(entity_.kind, kind)) {
    return refuse(
      std::string("action '") + action_id + "' does not apply to a " +
      to_string(entity_.kind));
  }
  if (!schema_) {
    return refuse("panel has no schema");
  }

  ControlRouting routing;
  routing.intent.kind = kind;
  routing.intent.entity = entity_;

  if (kind == RosActionKind::SetField) {
    std::string path_text = control.path.empty() ? widget->binding : control.path;
    if (path_text.empty()) {
      return refuse("control '" + control.control + "' has no binding");
    }
    FieldPath path;
    std::string error;
    if (!FieldPath::parse(path_text, path, error)) {
      return refuse("invalid binding '" + path_text + "': " + error);
    }
    if (path.empty()) {
      return refuse("control '" + control.control + "' binds the whole document");
    }
    // Only the staged sections are writable. `msg`, `response`, `feedback`,
    // `result` and `status` are what ROS said; a page may not rewrite them.
    const std::string & section = path.segments.front().name;
    if (!is_writable_document_section(section)) {
      return refuse("'" + path_text + "' is not editable");
    }
    qar_ui_model::ResolvedField resolved;
    if (!qar_ui_model::resolve_field_path(*schema_, path, resolved, error)) {
      return refuse("'" + path_text + "' does not resolve: " + error);
    }

    // Validate on a COPY: a rejected value must leave the staged message exactly
    // as it was, and validating the whole document is what catches a kind
    // mismatch, an out-of-range integer and an over-long string with its path.
    UiValue candidate = document_;
    if (!qar_ui_model::set_path(candidate, path, control.value, error)) {
      return refuse("could not write '" + path_text + "': " + error);
    }
    const auto problems = qar_ui_model::validate_value(*schema_, candidate);
    if (!problems.empty()) {
      return refuse(problems.front().path + ": " + problems.front().message);
    }
    document_ = std::move(candidate);

    routing.ok = true;
    routing.intent.document_path = path.to_string();
    routing.intent.value = control.value;
    return routing;
  }

  if (kind == RosActionKind::CancelGoal) {
    routing.ok = true;
    return routing;
  }

  if (kind == RosActionKind::OpenVideoPanel) {
    // action_applies already confirmed a Topic; this is the finer check only
    // route() can make, since it alone sees the entity's type_name.
    if (!is_streamable_image_type(entity_.type_name)) {
      return refuse(
        std::string("action '") + action_id + "' only applies to a sensor_msgs/msg/Image or "
        "sensor_msgs/msg/CompressedImage topic, not " + entity_.type_name);
    }
    routing.ok = true;
    return routing;
  }

  const char * key = outbound_key(kind);
  if (key == nullptr) {
    return refuse(std::string("action '") + action_id + "' carries no payload");
  }

  // A button may carry a preset (`{"linear":{"x":0}}` on a Stop button); that
  // IS the message. Otherwise the staged section is sent as composed.
  UiValue payload;
  if (control.value.is_object()) {
    payload = control.value;
  } else if (const UiValue * staged = document_.find(key)) {
    payload = *staged;
  } else {
    payload = UiValue::object();
  }

  const qar_ui_model::FieldDescription * section = schema_->find_field(key);
  if (section == nullptr || !section->message) {
    return refuse(std::string("the panel has no '") + key + "' section");
  }
  const auto problems = qar_ui_model::validate_value(*section->message, payload);
  if (!problems.empty()) {
    return refuse(problems.front().path + ": " + problems.front().message);
  }

  routing.ok = true;
  routing.intent.payload = std::move(payload);
  return routing;
}

}  // namespace qar_rviz_streaming
