// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ui_model/ui_protocol.hpp"

#include <utility>

#include "qar_ui_model/json.hpp"
#include "qar_ui_model/type_description.hpp"

namespace qar_ui_model
{

namespace
{

struct TypeName
{
  MessageType type;
  const char * name;
};

const TypeName kTypeNames[] = {
  {MessageType::Hello, "hello"},
  {MessageType::Data, "data"},
  {MessageType::Control, "control"},
  {MessageType::Status, "status"},
  {MessageType::Ack, "ack"},
  {MessageType::Ping, "ping"},
  {MessageType::Pong, "pong"},
  {MessageType::Viewport, "viewport"},
};

ProtocolMessage
envelope(MessageType type, std::uint64_t seq)
{
  ProtocolMessage message;
  message.type = type;
  message.type_name = to_string(type);
  message.seq = seq;
  return message;
}

}  // namespace

const char *
to_string(MessageType type)
{
  for (const TypeName & entry : kTypeNames) {
    if (entry.type == type) {
      return entry.name;
    }
  }
  return "unknown";
}

bool
message_type_from_string(std::string_view text, MessageType & out_type)
{
  for (const TypeName & entry : kTypeNames) {
    if (text == entry.name) {
      out_type = entry.type;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

ProtocolMessage
make_hello(std::uint64_t seq, std::string panel, std::string runtime, UiValue model, UiValue data)
{
  ProtocolMessage message = envelope(MessageType::Hello, seq);
  HelloBody body;
  body.panel = std::move(panel);
  body.runtime = std::move(runtime);
  body.model = std::move(model);
  body.data = std::move(data);
  message.body = std::move(body);
  return message;
}

ProtocolMessage
make_data(std::uint64_t seq, std::vector<PatchEntry> patch)
{
  ProtocolMessage message = envelope(MessageType::Data, seq);
  message.body = DataBody{std::move(patch)};
  return message;
}

ProtocolMessage
make_data_replace(std::uint64_t seq, UiValue document)
{
  std::vector<PatchEntry> patch;
  patch.push_back(PatchEntry{"", std::move(document)});
  return make_data(seq, std::move(patch));
}

ProtocolMessage
make_control(
  std::uint64_t seq, std::string control, std::string action, std::string path, UiValue value)
{
  ProtocolMessage message = envelope(MessageType::Control, seq);
  message.body = ControlBody{std::move(control), std::move(action), std::move(path), std::move(value)};
  return message;
}

ProtocolMessage
make_status(std::uint64_t seq, StatusLevel level, std::string text)
{
  ProtocolMessage message = envelope(MessageType::Status, seq);
  message.body = StatusBody{level, std::move(text)};
  return message;
}

ProtocolMessage
make_ack(std::uint64_t seq, std::uint64_t event_seq, bool ok, std::string text)
{
  ProtocolMessage message = envelope(MessageType::Ack, seq);
  message.body = AckBody{event_seq, ok, std::move(text)};
  return message;
}

ProtocolMessage
make_ping(std::uint64_t seq)
{
  ProtocolMessage message = envelope(MessageType::Ping, seq);
  message.body = PingBody{};
  return message;
}

ProtocolMessage
make_pong(std::uint64_t seq, std::uint64_t ping_seq)
{
  ProtocolMessage message = envelope(MessageType::Pong, seq);
  message.body = PongBody{ping_seq};
  return message;
}

ProtocolMessage
make_viewport(std::uint64_t seq, ViewportBody viewport)
{
  ProtocolMessage message = envelope(MessageType::Viewport, seq);
  message.body = std::move(viewport);
  return message;
}

// ---------------------------------------------------------------------------
// Encoding
// ---------------------------------------------------------------------------

UiValue
to_value(const ProtocolMessage & message)
{
  UiValue out = UiValue::object();
  out.set("v", static_cast<std::int64_t>(message.version));
  out.set("type", message.type == MessageType::Unknown ? message.type_name : to_string(message.type));
  out.set("seq", static_cast<std::int64_t>(message.seq));
  if (message.timestamp) {
    out.set("ts", *message.timestamp);
  }
  if (const HelloBody * hello = message.as<HelloBody>()) {
    out.set("panel", hello->panel);
    if (!hello->runtime.empty()) {
      out.set("runtime", hello->runtime);
    }
    if (!hello->model.is_null()) {
      out.set("model", hello->model);
    }
    if (!hello->data.is_null()) {
      out.set("data", hello->data);
    }
  } else if (const DataBody * data = message.as<DataBody>()) {
    UiValue patch = UiValue::array();
    for (const PatchEntry & entry : data->patch) {
      UiValue item = UiValue::object();
      item.set("path", entry.path);
      item.set("value", entry.value);
      patch.push_back(std::move(item));
    }
    out.set("patch", std::move(patch));
  } else if (const ControlBody * control = message.as<ControlBody>()) {
    out.set("control", control->control);
    out.set("action", control->action);
    out.set("path", control->path);
    out.set("value", control->value);
  } else if (const StatusBody * status = message.as<StatusBody>()) {
    out.set("level", to_string(status->level));
    out.set("text", status->text);
  } else if (const AckBody * ack = message.as<AckBody>()) {
    out.set("event_seq", static_cast<std::int64_t>(ack->event_seq));
    out.set("ok", ack->ok);
    if (!ack->text.empty()) {
      out.set("text", ack->text);
    }
  } else if (const PongBody * pong = message.as<PongBody>()) {
    out.set("ping_seq", static_cast<std::int64_t>(pong->ping_seq));
  } else if (const ViewportBody * viewport = message.as<ViewportBody>()) {
    // Only what was actually measured goes on the wire: a member the engine
    // could not answer is absent, not zero, so a receiver can tell the two
    // apart without a sentinel.
    out.set("css_w", viewport->css_width);
    out.set("css_h", viewport->css_height);
    if (viewport->device_pixel_ratio > 0.0) {
      out.set("dpr", viewport->device_pixel_ratio);
    }
    if (viewport->content_width > 0.0) {
      out.set("content_w", viewport->content_width);
    }
    if (viewport->content_height > 0.0) {
      out.set("content_h", viewport->content_height);
    }
    if (viewport->root_width > 0.0) {
      out.set("root_w", viewport->root_width);
    }
    if (viewport->root_height > 0.0) {
      out.set("root_h", viewport->root_height);
    }
    if (!viewport->reason.empty()) {
      out.set("reason", viewport->reason);
    }
  }
  return out;
}

std::string
encode_message(const ProtocolMessage & message)
{
  return json_write(to_value(message));
}

// ---------------------------------------------------------------------------
// Decoding
// ---------------------------------------------------------------------------

namespace
{

bool
require_string(const UiValue & object, const char * key, std::string & out, std::string & error,
  bool required = true)
{
  const UiValue * v = object.find(key);
  if (v == nullptr || v->is_null()) {
    if (required) {
      error = std::string("missing '") + key + "'";
      return false;
    }
    out.clear();
    return true;
  }
  if (!v->is_string()) {
    error = std::string("'") + key + "' must be a string";
    return false;
  }
  out = v->as_string();
  return true;
}

bool
require_uint(const UiValue & object, const char * key, std::uint64_t & out, std::string & error)
{
  const UiValue * v = object.find(key);
  if (v == nullptr || !v->is_number()) {
    error = std::string("missing or non-numeric '") + key + "'";
    return false;
  }
  const std::int64_t n = v->as_int(-1);
  if (n < 0) {
    error = std::string("'") + key + "' must be a non-negative integer";
    return false;
  }
  out = static_cast<std::uint64_t>(n);
  return true;
}

/// A viewport member: absent is "not reported" (0), present must be a finite
/// non-negative number. A negative or NaN pixel count is a broken measurement,
/// and silently keeping it would poison the panel size computed from it.
bool
optional_extent(const UiValue & object, const char * key, double & out, std::string & error)
{
  const UiValue * v = object.find(key);
  if (v == nullptr || v->is_null()) {
    out = 0.0;
    return true;
  }
  if (!v->is_number()) {
    error = std::string("'") + key + "' must be a number";
    return false;
  }
  const double value = v->as_double();
  if (!(value >= 0.0) || value > 1e9) {
    error = std::string("'") + key + "' must be a non-negative finite number";
    return false;
  }
  out = value;
  return true;
}

}  // namespace

bool
message_from_value(const UiValue & value, ProtocolMessage & out_message, std::string & out_error)
{
  if (!value.is_object()) {
    out_error = "frame must be a JSON object";
    return false;
  }
  const UiValue * v = value.find("v");
  if (v == nullptr || !v->is_number()) {
    out_error = "missing protocol version 'v'";
    return false;
  }
  if (v->as_int(-1) != kProtocolVersion) {
    out_error = "unsupported protocol version " + json_write(*v) + " (this library speaks " +
      std::to_string(kProtocolVersion) + ")";
    return false;
  }
  ProtocolMessage message;
  message.version = kProtocolVersion;
  if (!require_string(value, "type", message.type_name, out_error)) {
    return false;
  }
  if (!require_uint(value, "seq", message.seq, out_error)) {
    return false;
  }
  if (const UiValue * ts = value.find("ts"); ts != nullptr && !ts->is_null()) {
    if (!ts->is_number()) {
      out_error = "'ts' must be a number";
      return false;
    }
    message.timestamp = ts->as_double();
  }
  if (!message_type_from_string(message.type_name, message.type)) {
    message.type = MessageType::Unknown;  // Forward compatible: the caller ignores it.
    out_message = std::move(message);
    out_error.clear();
    return true;
  }

  switch (message.type) {
    case MessageType::Hello: {
      HelloBody body;
      if (!require_string(value, "panel", body.panel, out_error) ||
        !require_string(value, "runtime", body.runtime, out_error, false))
      {
        return false;
      }
      if (const UiValue * model = value.find("model"); model != nullptr) {
        if (!model->is_object() && !model->is_null()) {
          out_error = "'model' must be an object";
          return false;
        }
        body.model = *model;
      }
      if (const UiValue * data = value.find("data"); data != nullptr) {
        if (!data->is_object() && !data->is_null()) {
          out_error = "'data' must be an object";
          return false;
        }
        body.data = *data;
      }
      message.body = std::move(body);
      break;
    }
    case MessageType::Data: {
      DataBody body;
      const UiValue * patch = value.find("patch");
      if (patch == nullptr || !patch->is_array()) {
        out_error = "data frame without a 'patch' array";
        return false;
      }
      for (const UiValue & item : patch->elements()) {
        if (!item.is_object()) {
          out_error = "each patch entry must be an object";
          return false;
        }
        PatchEntry entry;
        if (!require_string(item, "path", entry.path, out_error, false)) {
          return false;
        }
        FieldPath parsed;
        std::string path_error;
        if (!FieldPath::parse(entry.path, parsed, path_error)) {
          out_error = "patch entry: " + path_error;
          return false;
        }
        const UiValue * entry_value = item.find("value");
        if (entry_value == nullptr) {
          out_error = "patch entry without a 'value'";
          return false;
        }
        entry.value = *entry_value;
        body.patch.push_back(std::move(entry));
      }
      message.body = std::move(body);
      break;
    }
    case MessageType::Control: {
      ControlBody body;
      if (!require_string(value, "control", body.control, out_error) ||
        !require_string(value, "action", body.action, out_error, false) ||
        !require_string(value, "path", body.path, out_error, false))
      {
        return false;
      }
      if (!is_valid_widget_id(body.control)) {
        out_error = "'control' is not a valid widget id";
        return false;
      }
      if (const UiValue * control_value = value.find("value"); control_value != nullptr) {
        body.value = *control_value;
      }
      message.body = std::move(body);
      break;
    }
    case MessageType::Status: {
      StatusBody body;
      std::string level;
      if (!require_string(value, "level", level, out_error, false) ||
        !require_string(value, "text", body.text, out_error, false))
      {
        return false;
      }
      if (!level.empty() && !status_level_from_string(level, body.level)) {
        out_error = "unknown status level '" + level + "'";
        return false;
      }
      message.body = std::move(body);
      break;
    }
    case MessageType::Ack: {
      AckBody body;
      if (!require_uint(value, "event_seq", body.event_seq, out_error)) {
        return false;
      }
      const UiValue * ok = value.find("ok");
      if (ok == nullptr || !ok->is_bool()) {
        out_error = "ack frame without a boolean 'ok'";
        return false;
      }
      body.ok = ok->as_bool();
      if (!require_string(value, "text", body.text, out_error, false)) {
        return false;
      }
      message.body = std::move(body);
      break;
    }
    case MessageType::Ping:
      message.body = PingBody{};
      break;
    case MessageType::Pong: {
      PongBody body;
      if (!require_uint(value, "ping_seq", body.ping_seq, out_error)) {
        return false;
      }
      message.body = body;
      break;
    }
    case MessageType::Viewport: {
      ViewportBody body;
      if (!optional_extent(value, "css_w", body.css_width, out_error) ||
        !optional_extent(value, "css_h", body.css_height, out_error) ||
        !optional_extent(value, "dpr", body.device_pixel_ratio, out_error) ||
        !optional_extent(value, "content_w", body.content_width, out_error) ||
        !optional_extent(value, "content_h", body.content_height, out_error) ||
        !optional_extent(value, "root_w", body.root_width, out_error) ||
        !optional_extent(value, "root_h", body.root_height, out_error) ||
        !require_string(value, "reason", body.reason, out_error, false))
      {
        return false;
      }
      if (!(body.css_width > 0.0) || !(body.css_height > 0.0)) {
        out_error = "viewport frame without a positive 'css_w' and 'css_h'";
        return false;
      }
      message.body = std::move(body);
      break;
    }
    case MessageType::Unknown:
      break;
  }
  out_message = std::move(message);
  out_error.clear();
  return true;
}

bool
decode_message(std::string_view text, ProtocolMessage & out_message, std::string & out_error)
{
  UiValue value;
  JsonParseError parse_error;
  if (!json_parse(text, value, parse_error)) {
    out_error = "malformed JSON: " + parse_error.to_string();
    return false;
  }
  return message_from_value(value, out_message, out_error);
}

bool
apply_patch(UiValue & document, const DataBody & data, std::string & out_error)
{
  UiValue working = document;
  for (std::size_t i = 0; i < data.patch.size(); ++i) {
    const PatchEntry & entry = data.patch[i];
    FieldPath path;
    std::string error;
    if (!FieldPath::parse(entry.path, path, error)) {
      out_error = "patch entry " + std::to_string(i) + ": " + error;
      return false;
    }
    if (!set_path(working, path, entry.value, error)) {
      out_error = "patch entry " + std::to_string(i) + ": " + error;
      return false;
    }
  }
  document = std::move(working);
  out_error.clear();
  return true;
}

}  // namespace qar_ui_model
