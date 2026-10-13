// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_WEB_PANELS__PANEL_SESSION_HPP_
#define QAR_WEB_PANELS__PANEL_SESSION_HPP_

/// @file panel_session.hpp
/// @brief One served panel: a UiModel, its live data document, its token, and
/// the WebSocket clients viewing it — the server half of `qar-ui/1`.
///
/// The host publishes value updates (an ordered patch, applied to the server's
/// own copy first and only then broadcast, so a viewer that connects later
/// receives in its hello exactly the document the earlier viewers reached) and
/// receives control events through @ref IPanelControlSink and/or the
/// @ref control_received signal. Acks, status lines and pong replies go back
/// per connection with that connection's own `seq`.
///
/// The session does NOT apply a control event's value to its document: the
/// page applies it optimistically, and the host's next publish (typically the
/// ROS echo of what it just sent) confirms or corrects it — exactly the
/// protocol's contract. A host that wants viewers to see each other's edits
/// without a round trip calls set_value from its sink.
///
/// Single-threaded by design: every method must be called on the thread that
/// owns the session (the server's thread). A call from any other thread is
/// refused with an error, never queued, never executed. Cross-thread hosts
/// connect to the signals with a queued connection — the event type is a
/// registered metatype for that reason.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <QMetaType>
#include <QObject>
#include <QWebSocketProtocol>

#include "qar_ui_model/ui_model.hpp"
#include "qar_ui_model/ui_protocol.hpp"
#include "qar_ui_model/ui_value.hpp"

class QWebSocket;

namespace qar_web_panels
{

/// @brief What one viewer's page measured about the box it was laid out into.
///
/// The page sends this on connect and whenever the layout moves. It exists
/// because the panel's physical size has to be chosen BEFORE the browser has
/// laid anything out, and nothing in the QarOS SDK says how a website panel's
/// CSS viewport follows from the metres we declare (see
/// docs/SDK_COMPATIBILITY.md, "The QarOS in-AR browser"). Rather than guess,
/// the page measures and the host resizes.
struct PanelViewportReport
{
  std::uint64_t client_id = 0;
  std::optional<double> timestamp;
  qar_ui_model::ViewportBody viewport;

  /// width / height of the reported CSS viewport; > 1 is landscape. This is
  /// the number a panel's physical aspect has to agree with, or the runtime
  /// scales the page non-uniformly and the operator sees a squeeze.
  double aspect() const
  {
    return viewport.css_height > 0.0 ? viewport.css_width / viewport.css_height : 1.0;
  }

  /// How much of the page is inside the viewport. < 1 means the tail is below
  /// the fold and only the click pager can reach it.
  double visible_fraction() const
  {
    return viewport.content_height > viewport.css_height && viewport.content_height > 0.0
           ? viewport.css_height / viewport.content_height
           : 1.0;
  }

  /// "viewport 1024x640 css, dpr 1.0, content 1024x1580" — the log line.
  std::string describe() const;
};

/// @brief One inbound control event, with what an ack needs to find its way back.
struct PanelControlEvent
{
  std::uint64_t client_id = 0;  ///< Which viewer; stable for the connection's life.
  std::uint64_t seq = 0;        ///< The page's `seq` for this frame: what an ack names.
  std::optional<double> timestamp;  ///< The page's `ts`, if it sent one.
  qar_ui_model::ControlBody control;
};

class PanelSession;

/// @brief Host-side receiver of control events. Called on the session's thread.
class IPanelControlSink
{
public:
  virtual ~IPanelControlSink() = default;
  virtual void on_control(PanelSession & session, const PanelControlEvent & event) = 0;
};

struct PanelSessionLimits
{
  std::size_t max_clients = 16;
  /// A viewer whose unsent backlog exceeds this is dropped (1008): the
  /// alternative is unbounded memory on the GUI thread.
  std::int64_t max_client_backlog_bytes = 4 * 1024 * 1024;
  /// An inbound frame or message larger than this closes the socket (1009).
  std::uint64_t max_message_bytes = 1024 * 1024;
};

struct PanelSessionDiagnostics
{
  std::size_t clients = 0;
  std::uint64_t clients_total = 0;
  std::uint64_t clients_refused = 0;    ///< Over max_clients.
  std::uint64_t frames_in = 0;
  std::uint64_t frames_out = 0;
  std::uint64_t frames_ignored = 0;     ///< Malformed, foreign, unknown type, non-increasing seq.
  std::uint64_t controls = 0;
  std::uint64_t viewports = 0;          ///< Viewport reports accepted from pages.
  std::uint64_t publishes = 0;
  std::uint64_t publishes_refused = 0;  ///< Patch failed to apply (nothing was sent).
  std::uint64_t closed_by_policy = 0;   ///< Bad first frame, foreign version, wrong panel, binary.
  std::uint64_t slow_consumers = 0;
};

/// @brief A token-gated static asset served under the panel's URL.
struct PanelResource
{
  std::string content_type;
  std::string bytes;
};

class PanelSession : public QObject
{
  Q_OBJECT

public:
  PanelSession(
    qar_ui_model::UiModel model, std::string token, PanelSessionLimits limits = {},
    QObject * parent = nullptr);
  ~PanelSession() override;

  PanelSession(const PanelSession &) = delete;
  PanelSession & operator=(const PanelSession &) = delete;

  /// @brief The root widget's id: what the page's hello names.
  const std::string & panel_id() const { return model_.root.id; }
  const std::string & token() const { return token_; }
  const qar_ui_model::UiModel & model() const { return model_; }
  /// @brief The server's copy of the data document. Always an object.
  const qar_ui_model::UiValue & document() const { return document_; }

  /// @brief The most recent viewport any viewer reported, or nullopt while no
  /// page has measured itself yet (a page that never loaded, or a runtime
  /// older than this frame type). A host sizing a panel treats the absence as
  /// "fall back to the estimate", never as zero.
  const std::optional<PanelViewportReport> & last_viewport() const { return last_viewport_; }

  // --- Host -> viewers -----------------------------------------------------

  /// @brief Apply @p patch to the document (all-or-nothing) and, if it
  /// applied, send it to every viewer as one `data` frame.
  bool publish(std::vector<qar_ui_model::PatchEntry> patch, std::string * out_error = nullptr);
  /// @brief One-entry patch: `{path, value}`.
  bool set_value(std::string path, qar_ui_model::UiValue value, std::string * out_error = nullptr);
  /// @brief Path "" patch: replaces the whole document. Must be an object.
  bool replace_document(qar_ui_model::UiValue document, std::string * out_error = nullptr);
  /// @brief A `status` frame to every viewer (empty text clears the line).
  void send_status(qar_ui_model::StatusLevel level, std::string text);
  /// @brief Answer one control event. False when that viewer is gone.
  bool ack(const PanelControlEvent & event, bool ok, std::string text = {});
  bool ack(std::uint64_t client_id, std::uint64_t event_seq, bool ok, std::string text = {});

  // --- Viewers -> host -------------------------------------------------------

  /// @brief Borrowed; may be null. Called before @ref control_received is emitted.
  void set_control_sink(IPanelControlSink * sink) { sink_ = sink; }
  IPanelControlSink * control_sink() const { return sink_; }

  // --- Static assets ------------------------------------------------------------

  /// @brief Serve @p bytes at `<page url><relative_path>` (no leading '/').
  void set_resource(std::string relative_path, std::string content_type, std::string bytes);
  bool remove_resource(std::string_view relative_path);
  const PanelResource * find_resource(std::string_view relative_path) const;

  // --- Server side ---------------------------------------------------------------

  /// @brief Adopt a freshly upgraded socket (ownership passes). Over
  /// max_clients it is closed with 1008 instead.
  void attach_client(QWebSocket * socket);
  /// @brief Close every viewer with @p code; they will reconnect per protocol.
  void close_all(
    QWebSocketProtocol::CloseCode code = QWebSocketProtocol::CloseCodeGoingAway,
    const QString & reason = QString());

  std::size_t client_count() const { return clients_.size(); }
  PanelSessionDiagnostics diagnostics() const;

Q_SIGNALS:
  /// @brief After the sink (if any) has seen the event.
  void control_received(const qar_web_panels::PanelControlEvent & event);
  /// @brief A viewer reported the CSS viewport it was laid out into. Emitted
  /// for every accepted report, including repeats with the same numbers.
  void viewport_reported(const qar_web_panels::PanelViewportReport & report);
  void client_count_changed(std::size_t count);

private:
  struct Client;
  using ClientPtr = std::shared_ptr<Client>;

  bool on_owner_thread(std::string * out_error) const;
  void on_text_message(const ClientPtr & client, const QString & text);
  void on_client_disconnected(const ClientPtr & client);
  bool send(Client & client, qar_ui_model::ProtocolMessage message);
  void broadcast(const qar_ui_model::ProtocolMessage & message);
  void drop(Client & client, QWebSocketProtocol::CloseCode code, const char * reason);
  std::vector<ClientPtr> snapshot() const { return clients_; }

  qar_ui_model::UiModel model_;
  std::string token_;
  PanelSessionLimits limits_;
  qar_ui_model::UiValue document_;
  IPanelControlSink * sink_ = nullptr;
  std::vector<ClientPtr> clients_;
  std::vector<std::pair<std::string, PanelResource>> resources_;
  std::optional<PanelViewportReport> last_viewport_;
  std::uint64_t next_client_id_ = 1;
  bool destroying_ = false;
  PanelSessionDiagnostics diagnostics_;
};

}  // namespace qar_web_panels

Q_DECLARE_METATYPE(qar_web_panels::PanelControlEvent)
Q_DECLARE_METATYPE(qar_web_panels::PanelViewportReport)

#endif  // QAR_WEB_PANELS__PANEL_SESSION_HPP_
