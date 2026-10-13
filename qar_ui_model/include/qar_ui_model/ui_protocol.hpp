// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_UI_MODEL__UI_PROTOCOL_HPP_
#define QAR_UI_MODEL__UI_PROTOCOL_HPP_

/// @file ui_protocol.hpp
/// @brief The versioned JSON envelope between a panel page and its server.
///
/// This is the contract the panel server (qar_web_panels) implements on the
/// server side and the embedded JavaScript runtime implements on the page side;
/// the C++ here is the reference encoder/decoder both are tested against. The
/// normative text is docs/WIRE_PROTOCOL.md; in short:
///
/// Every frame is one JSON object: `{"v": 1, "type": "...", "seq": N, ...}`.
///   * `v`    — protocol major version. A receiver ignores frames whose `v`
///              it does not speak (and shows "protocol mismatch").
///   * `type` — one of hello / data / control / status / ack / ping / pong /
///              viewport.
///              Unknown types are ignored, not fatal: forward compatibility.
///   * `seq`  — only grows: server per connection, page per load, both from 1.
///   * `ts`   — optional, seconds since the Unix epoch on the sender's clock.
///
/// Server -> page: hello (optionally with the model and a full data document),
/// data (a list of `{path, value}` assignments applied in order; path "" is
/// the whole document), status (a text line with a level), ack (outcome of a
/// control event, by its seq), pong.
/// Page -> server: hello (which panel, which runtime), control (widget id,
/// action id, binding path, value), viewport (the CSS viewport the page was
/// actually given, measured by the page itself), ping.
///
/// A data frame is applied ALL-OR-NOTHING; a malformed patch leaves the
/// document as it was.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "qar_ui_model/ui_model.hpp"
#include "qar_ui_model/ui_value.hpp"

namespace qar_ui_model
{

constexpr int kProtocolVersion = 1;

/// @brief What the page announces in its hello.
constexpr const char * kRuntimeName = "qar-ui-runtime/1";

enum class MessageType
{
  Hello = 0,
  Data,
  Control,
  Status,
  Ack,
  Ping,
  Pong,
  /// Appended rather than inserted, so no existing value moves. See
  /// @ref ViewportBody for why a new frame type is not a version change.
  Viewport,
  Unknown
};
const char * to_string(MessageType type);
bool message_type_from_string(std::string_view text, MessageType & out_type);

/// @brief One assignment of a data frame. `path` uses the FieldPath grammar;
/// "" replaces the whole document.
struct PatchEntry
{
  std::string path;
  UiValue value;
};

struct HelloBody
{
  std::string panel;    ///< Panel id (the root widget's id).
  std::string runtime;  ///< Page -> server only: kRuntimeName.
  UiValue model;        ///< Server -> page, optional: the serialized UiModel.
  UiValue data;         ///< Server -> page, optional: a full data document.
};

struct DataBody
{
  std::vector<PatchEntry> patch;
};

struct ControlBody
{
  std::string control;  ///< Widget id.
  std::string action;   ///< Widget's action id (may be empty).
  std::string path;     ///< Widget's binding (may be empty).
  UiValue value;        ///< New value, or the button's payload.
};

struct StatusBody
{
  StatusLevel level = StatusLevel::Neutral;
  std::string text;
};

struct AckBody
{
  std::uint64_t event_seq = 0;  ///< `seq` of the control frame being answered.
  bool ok = true;
  std::string text;
};

/// @brief What the page measured about the box it was actually laid out into.
///
/// The one frame that exists because a fact could not be looked up. Nothing in
/// the QarOS SDK says how a website panel's browser viewport is derived from
/// the panel's declared metres (what is known is in docs/SDK_COMPATIBILITY.md,
/// "The QarOS in-AR browser"), and the panel has to commit to a physical size
/// BEFORE the browser has laid anything out. So the page measures the viewport
/// it was handed and tells the server, and the server sizes the panel to what
/// it hears instead of to what it assumed.
///
/// Every member is in CSS pixels except @ref device_pixel_ratio. A receiver
/// that gets 0 for a member treats it as "not reported" — the page sends what
/// its engine can answer and omits the rest.
struct ViewportBody
{
  /// `window.innerWidth` / `innerHeight`: the CSS viewport the page was laid
  /// out into. This is the number the panel's aspect must agree with.
  double css_width = 0.0;
  double css_height = 0.0;

  /// `window.devicePixelRatio`: device pixels per CSS pixel. Diagnostic — it
  /// says how much raster is behind the page, not how big the page is.
  double device_pixel_ratio = 0.0;

  /// `document.documentElement.scrollWidth` / `scrollHeight`: how much page
  /// there is. Taller than @ref css_height means content below the fold.
  double content_width = 0.0;
  double content_height = 0.0;

  /// The computed CSS box of the panel root (`.qar-panel`), border box.
  double root_width = 0.0;
  double root_height = 0.0;

  /// Why this frame was sent: "connect", "resize" or "content". Free text;
  /// a receiver must not switch on it.
  std::string reason;
};

struct PingBody
{
};

struct PongBody
{
  std::uint64_t ping_seq = 0;
};

struct ProtocolMessage
{
  int version = kProtocolVersion;
  MessageType type = MessageType::Unknown;
  std::uint64_t seq = 0;
  std::optional<double> timestamp;
  /// The raw `type` string; meaningful when type == Unknown.
  std::string type_name;
  std::variant<std::monostate, HelloBody, DataBody, ControlBody, StatusBody, AckBody, PingBody,
    PongBody, ViewportBody> body;

  template<typename T>
  const T * as() const
  {
    return std::get_if<T>(&body);
  }
};

// --- Builders ---------------------------------------------------------------

ProtocolMessage make_hello(
  std::uint64_t seq, std::string panel, std::string runtime = {}, UiValue model = {},
  UiValue data = {});
ProtocolMessage make_data(std::uint64_t seq, std::vector<PatchEntry> patch);
/// Single-entry data frame with path "": replaces the whole document.
ProtocolMessage make_data_replace(std::uint64_t seq, UiValue document);
ProtocolMessage make_control(
  std::uint64_t seq, std::string control, std::string action, std::string path, UiValue value);
ProtocolMessage make_status(std::uint64_t seq, StatusLevel level, std::string text);
ProtocolMessage make_ack(std::uint64_t seq, std::uint64_t event_seq, bool ok, std::string text = {});
ProtocolMessage make_ping(std::uint64_t seq);
ProtocolMessage make_pong(std::uint64_t seq, std::uint64_t ping_seq);
/// @brief Page -> server. Adding this type is NOT a version change: the wire
/// protocol (docs/WIRE_PROTOCOL.md, "Versioning") makes an unknown `type`
/// ignorable, so a page built before this frame existed never sends one and a
/// server built before it existed decodes it as MessageType::Unknown and drops
/// it. Both keep working.
ProtocolMessage make_viewport(std::uint64_t seq, ViewportBody viewport);

// --- Codec ------------------------------------------------------------------

/// @brief Compact JSON, one line, no trailing newline.
std::string encode_message(const ProtocolMessage & message);
UiValue to_value(const ProtocolMessage & message);

/// @brief Parse one frame. An unknown `type` decodes successfully as
/// MessageType::Unknown (with type_name set); a missing/foreign `v`, a missing
/// `type`/`seq`, or a body that violates the schema fails with a reason.
bool decode_message(std::string_view text, ProtocolMessage & out_message, std::string & out_error);
bool message_from_value(const UiValue & value, ProtocolMessage & out_message, std::string & out_error);

/// @brief Apply a data frame to a document, all-or-nothing.
bool apply_patch(UiValue & document, const DataBody & data, std::string & out_error);

}  // namespace qar_ui_model

#endif  // QAR_UI_MODEL__UI_PROTOCOL_HPP_
