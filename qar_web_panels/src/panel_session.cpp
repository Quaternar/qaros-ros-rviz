// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_web_panels/panel_session.hpp"

#include <QThread>
#include <QWebSocket>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "qar_ui_model/json.hpp"

namespace qar_web_panels
{

using qar_ui_model::ControlBody;
using qar_ui_model::HelloBody;
using qar_ui_model::MessageType;
using qar_ui_model::PatchEntry;
using qar_ui_model::ProtocolMessage;
using qar_ui_model::StatusLevel;
using qar_ui_model::UiModel;
using qar_ui_model::UiValue;

namespace
{

double
now_seconds()
{
  using namespace std::chrono;
  return duration<double>(system_clock::now().time_since_epoch()).count();
}

std::string
px(double value)
{
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.0f", value);
  return buffer;
}

}  // namespace

std::string
PanelViewportReport::describe() const
{
  std::string out = "viewport " + px(viewport.css_width) + "x" + px(viewport.css_height) + " css";
  if (viewport.device_pixel_ratio > 0.0) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", viewport.device_pixel_ratio);
    out += ", dpr ";
    out += buffer;
  }
  if (viewport.content_width > 0.0 || viewport.content_height > 0.0) {
    out += ", content " + px(viewport.content_width) + "x" + px(viewport.content_height);
  }
  if (viewport.root_width > 0.0 || viewport.root_height > 0.0) {
    out += ", root " + px(viewport.root_width) + "x" + px(viewport.root_height);
  }
  if (!viewport.reason.empty()) {
    out += " (" + viewport.reason + ")";
  }
  return out;
}

struct PanelSession::Client
{
  QWebSocket * socket = nullptr;  ///< Null once disconnected.
  std::uint64_t id = 0;
  std::uint64_t next_out_seq = 1;  ///< Server -> page, per connection, from 1.
  std::uint64_t last_in_seq = 0;   ///< Page -> server: strictly increasing.
  bool hello_done = false;
};

PanelSession::PanelSession(UiModel model, std::string token, PanelSessionLimits limits, QObject * parent)
: QObject(parent),
  model_(std::move(model)),
  token_(std::move(token)),
  limits_(limits)
{
  // Queued (cross-thread) connections to control_received need the type registered.
  qRegisterMetaType<PanelControlEvent>("qar_web_panels::PanelControlEvent");
  qRegisterMetaType<PanelViewportReport>("qar_web_panels::PanelViewportReport");
  qRegisterMetaType<std::size_t>("std::size_t");
  document_ = model_.initial_data.is_object() ? model_.initial_data : UiValue::object();
}

PanelSession::~PanelSession()
{
  destroying_ = true;
  close_all(QWebSocketProtocol::CloseCodeGoingAway, QStringLiteral("panel closed"));
}

bool
PanelSession::on_owner_thread(std::string * out_error) const
{
  if (QThread::currentThread() == thread()) {
    return true;
  }
  if (out_error != nullptr) {
    *out_error = "PanelSession must be driven from the thread that owns it";
  }
  return false;
}

// --- Host -> viewers ------------------------------------------------------------

bool
PanelSession::publish(std::vector<PatchEntry> patch, std::string * out_error)
{
  if (!on_owner_thread(out_error)) {
    return false;
  }
  qar_ui_model::DataBody body;
  body.patch = std::move(patch);
  std::string error;
  // The server copy first, all-or-nothing: what a late viewer gets in its
  // hello must be what the earlier viewers were patched to.
  if (!qar_ui_model::apply_patch(document_, body, error)) {
    ++diagnostics_.publishes_refused;
    if (out_error != nullptr) {
      *out_error = error;
    }
    return false;
  }
  ++diagnostics_.publishes;
  if (out_error != nullptr) {
    out_error->clear();
  }
  ProtocolMessage message = qar_ui_model::make_data(0, std::move(body.patch));
  message.timestamp = now_seconds();
  broadcast(message);
  return true;
}

bool
PanelSession::set_value(std::string path, UiValue value, std::string * out_error)
{
  std::vector<PatchEntry> patch;
  patch.push_back({std::move(path), std::move(value)});
  return publish(std::move(patch), out_error);
}

bool
PanelSession::replace_document(UiValue document, std::string * out_error)
{
  if (!document.is_object()) {
    if (out_error != nullptr) {
      *out_error = "the data document must be an object";
    }
    return false;
  }
  return set_value(std::string(), std::move(document), out_error);
}

void
PanelSession::send_status(StatusLevel level, std::string text)
{
  if (!on_owner_thread(nullptr)) {
    return;
  }
  ProtocolMessage message = qar_ui_model::make_status(0, level, std::move(text));
  message.timestamp = now_seconds();
  broadcast(message);
}

bool
PanelSession::ack(const PanelControlEvent & event, bool ok, std::string text)
{
  return ack(event.client_id, event.seq, ok, std::move(text));
}

bool
PanelSession::ack(std::uint64_t client_id, std::uint64_t event_seq, bool ok, std::string text)
{
  if (!on_owner_thread(nullptr)) {
    return false;
  }
  for (const ClientPtr & client : clients_) {
    if (client->id == client_id && client->socket != nullptr && client->hello_done) {
      ProtocolMessage message = qar_ui_model::make_ack(0, event_seq, ok, std::move(text));
      message.timestamp = now_seconds();
      return send(*client, std::move(message));
    }
  }
  return false;
}

// --- Static assets ------------------------------------------------------------------

void
PanelSession::set_resource(std::string relative_path, std::string content_type, std::string bytes)
{
  for (auto & entry : resources_) {
    if (entry.first == relative_path) {
      entry.second = PanelResource{std::move(content_type), std::move(bytes)};
      return;
    }
  }
  resources_.emplace_back(
    std::move(relative_path), PanelResource{std::move(content_type), std::move(bytes)});
}

bool
PanelSession::remove_resource(std::string_view relative_path)
{
  const auto it = std::find_if(
    resources_.begin(), resources_.end(),
    [relative_path](const auto & entry) {return entry.first == relative_path;});
  if (it == resources_.end()) {
    return false;
  }
  resources_.erase(it);
  return true;
}

const PanelResource *
PanelSession::find_resource(std::string_view relative_path) const
{
  for (const auto & entry : resources_) {
    if (entry.first == relative_path) {
      return &entry.second;
    }
  }
  return nullptr;
}

// --- Viewers ----------------------------------------------------------------------------

void
PanelSession::attach_client(QWebSocket * socket)
{
  if (socket == nullptr) {
    return;
  }
  if (clients_.size() >= limits_.max_clients) {
    ++diagnostics_.clients_refused;
    socket->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("too many viewers"));
    socket->deleteLater();
    return;
  }

  auto client = std::make_shared<Client>();
  client->socket = socket;
  client->id = next_client_id_++;
  socket->setParent(this);
  socket->setMaxAllowedIncomingFrameSize(limits_.max_message_bytes);
  socket->setMaxAllowedIncomingMessageSize(limits_.max_message_bytes);

  connect(
    socket, &QWebSocket::textMessageReceived, this,
    [this, client](const QString & text) {on_text_message(client, text);});
  connect(
    socket, &QWebSocket::binaryMessageReceived, this,
    [this, client](const QByteArray &) {
      drop(*client, QWebSocketProtocol::CloseCodeDatatypeNotSupported,
        "qar-ui/1 uses text frames only");
    });
  connect(
    socket, &QWebSocket::disconnected, this, [this, client]() {on_client_disconnected(client);});

  clients_.push_back(client);
  ++diagnostics_.clients_total;
  emit client_count_changed(clients_.size());
}

void
PanelSession::close_all(QWebSocketProtocol::CloseCode code, const QString & reason)
{
  // close() may emit disconnected synchronously, which edits clients_: walk a copy.
  for (const ClientPtr & client : snapshot()) {
    if (client->socket != nullptr) {
      client->socket->close(code, reason);
    }
  }
}

PanelSessionDiagnostics
PanelSession::diagnostics() const
{
  PanelSessionDiagnostics d = diagnostics_;
  d.clients = clients_.size();
  return d;
}

void
PanelSession::on_client_disconnected(const ClientPtr & client)
{
  if (client->socket != nullptr) {
    client->socket->deleteLater();
    client->socket = nullptr;
  }
  const auto it = std::find(clients_.begin(), clients_.end(), client);
  if (it != clients_.end()) {
    clients_.erase(it);
  }
  if (!destroying_) {
    emit client_count_changed(clients_.size());
  }
}

void
PanelSession::drop(Client & client, QWebSocketProtocol::CloseCode code, const char * reason)
{
  ++diagnostics_.closed_by_policy;
  if (client.socket != nullptr) {
    client.socket->close(code, QString::fromUtf8(reason));
  }
}

bool
PanelSession::send(Client & client, ProtocolMessage message)
{
  if (client.socket == nullptr) {
    return false;
  }
  if (client.socket->bytesToWrite() > limits_.max_client_backlog_bytes) {
    ++diagnostics_.slow_consumers;
    drop(client, QWebSocketProtocol::CloseCodePolicyViolated, "viewer is not keeping up");
    return false;
  }
  message.seq = client.next_out_seq++;
  client.socket->sendTextMessage(QString::fromStdString(qar_ui_model::encode_message(message)));
  ++diagnostics_.frames_out;
  return true;
}

void
PanelSession::broadcast(const ProtocolMessage & message)
{
  for (const ClientPtr & client : snapshot()) {
    if (client->socket != nullptr && client->hello_done) {
      send(*client, message);
    }
  }
}

void
PanelSession::on_text_message(const ClientPtr & client_ptr, const QString & text)
{
  Client & client = *client_ptr;
  if (client.socket == nullptr) {
    return;
  }
  ++diagnostics_.frames_in;

  // Parse in two steps so a foreign protocol version can be told apart from
  // a malformed frame: the former is REFUSED (closed) when it is the first
  // frame, per protocol; anything later that we do not understand is ignored.
  const std::string bytes = text.toStdString();
  UiValue value;
  qar_ui_model::JsonParseError parse_error;
  if (!qar_ui_model::json_parse(bytes, value, parse_error) || !value.is_object()) {
    ++diagnostics_.frames_ignored;
    if (!client.hello_done) {
      drop(client, QWebSocketProtocol::CloseCodeProtocolError, "malformed first frame");
    }
    return;
  }
  const UiValue * version = value.find("v");
  if (version == nullptr || !version->is_number() ||
    version->as_int(-1) != qar_ui_model::kProtocolVersion)
  {
    ++diagnostics_.frames_ignored;
    if (!client.hello_done) {
      drop(client, QWebSocketProtocol::CloseCodeProtocolError, "unsupported protocol version");
    }
    return;
  }
  ProtocolMessage message;
  std::string error;
  if (!qar_ui_model::message_from_value(value, message, error)) {
    ++diagnostics_.frames_ignored;
    if (!client.hello_done) {
      drop(client, QWebSocketProtocol::CloseCodeProtocolError, "malformed hello");
    }
    return;
  }
  // The page's counter is strictly increasing; a replayed or reordered frame is dropped.
  if (message.seq <= client.last_in_seq) {
    ++diagnostics_.frames_ignored;
    return;
  }
  client.last_in_seq = message.seq;

  if (!client.hello_done) {
    if (message.type != MessageType::Hello) {
      drop(client, QWebSocketProtocol::CloseCodeProtocolError, "expected hello first");
      return;
    }
    const HelloBody & hello = *message.as<HelloBody>();
    if (hello.panel != panel_id()) {
      drop(client, QWebSocketProtocol::CloseCodePolicyViolated, "unknown panel");
      return;
    }
    client.hello_done = true;
    ProtocolMessage reply = qar_ui_model::make_hello(0, panel_id(), {}, {}, document_);
    reply.timestamp = now_seconds();
    send(client, std::move(reply));
    return;
  }

  switch (message.type) {
    case MessageType::Control: {
      PanelControlEvent event;
      event.client_id = client.id;
      event.seq = message.seq;
      event.timestamp = message.timestamp;
      event.control = *message.as<ControlBody>();
      ++diagnostics_.controls;
      if (sink_ != nullptr) {
        sink_->on_control(*this, event);
      }
      emit control_received(event);
      return;
    }
    case MessageType::Viewport: {
      PanelViewportReport report;
      report.client_id = client.id;
      report.timestamp = message.timestamp;
      report.viewport = *message.as<qar_ui_model::ViewportBody>();
      ++diagnostics_.viewports;
      last_viewport_ = report;
      emit viewport_reported(report);
      return;
    }
    case MessageType::Ping: {
      send(client, qar_ui_model::make_pong(0, message.seq));
      return;
    }
    case MessageType::Pong:
      return;  // Liveness only; the arrival itself is the information.
    case MessageType::Hello:  // A second hello.
    case MessageType::Data:   // Server -> page frames arriving from a page.
    case MessageType::Status:
    case MessageType::Ack:
    case MessageType::Unknown:
    default:
      ++diagnostics_.frames_ignored;
      return;
  }
}

}  // namespace qar_web_panels
