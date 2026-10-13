// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_WEB_PANELS__WEB_PANEL_SERVER_HPP_
#define QAR_WEB_PANELS__WEB_PANEL_SERVER_HPP_

/// @file web_panel_server.hpp
/// @brief The facade a host drives: one HTTP+WebSocket listener, N panel sessions.
///
/// Routes (all under one port, so the page can derive its socket URL from
/// its own location and one firewall rule covers both):
///
///   GET  /p/<token>/            the panel's page (self-contained HTML)
///   GET  /p/<token>/<asset>     a PanelSession resource
///   WS   /ws/<token>            the `qar-ui/1` socket for that panel
///   GET  /                      a one-line identification, no panel list
///
/// A token names exactly one session; a wrong or missing token is a 404 with
/// no hint whether the token exists. The page is rendered per request with
/// `websocket_url = ws://<Host as the client sent it>/ws/<token>` so the
/// socket reaches the same server through whatever address reached the page.
///
/// Bind policy: the headset must reach this server, so the default binds every
/// interface (`QHostAddress::Any`) on an OS-chosen port, and the panel URL
/// advertises the address the machine uses to reach the LAN (overridable).
/// The token is what stands between "on the same network" and "can see and
/// drive the panel". See docs/ARCHITECTURE.md, "Security posture".

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <QHostAddress>
#include <QObject>

#include "qar_ui_model/html_renderer.hpp"
#include "qar_ui_model/ui_model.hpp"
#include "qar_web_panels/http_server.hpp"
#include "qar_web_panels/panel_session.hpp"

class QTcpSocket;
class QWebSocketServer;

namespace qar_web_panels
{

struct WebPanelServerConfig
{
  /// All interfaces, IPv4 and IPv6. Loopback for a hermetic desktop session
  /// (the QarOS Visualizer on this machine) — then nothing else can connect.
  QHostAddress bind_address = QHostAddress::Any;
  /// 0: OS-assigned, so two hosts on one machine never collide. Pin it when a
  /// firewall rule needs a fixed number; the URL carries whichever it is.
  quint16 port = 0;
  /// Host name or address written into panel URLs. Empty: the bind address
  /// when it is a concrete one, otherwise auto-detected (see candidate_hosts).
  std::string advertised_host;
  /// Accept WebSocket handshakes whose Origin is not this server (or absent).
  /// Off by default: the token already gates access, but a page served by
  /// someone else must not be able to ride a viewer's browser into a panel.
  bool allow_any_origin = false;

  HttpServerOptions http;
  PanelSessionLimits session;
  /// Theme, timings, language. `websocket_url` is overwritten per request.
  qar_ui_model::HtmlRenderOptions render;
};

struct WebPanelServerDiagnostics
{
  HttpServerDiagnostics http;
  std::size_t panels = 0;
  std::size_t clients = 0;
  std::uint64_t token_misses = 0;        ///< Requests naming no session (404).
  std::uint64_t upgrades_refused = 0;    ///< Handshakes refused before upgrading.
};

class WebPanelServer : public QObject
{
  Q_OBJECT

public:
  explicit WebPanelServer(WebPanelServerConfig config = {}, QObject * parent = nullptr);
  ~WebPanelServer() override;

  WebPanelServer(const WebPanelServer &) = delete;
  WebPanelServer & operator=(const WebPanelServer &) = delete;

  bool start(std::string * out_error = nullptr);
  /// Stops listening and closes every viewer (1001). Sessions and their tokens
  /// survive, so start() serves them again (on the same port if it was pinned).
  void stop();
  bool is_running() const;
  quint16 port() const;
  /// @brief The host written into panel URLs; resolved at start().
  const std::string & advertised_host() const { return advertised_host_; }
  const WebPanelServerConfig & config() const { return config_; }

  /// @brief Register a panel. The model is validated by rendering it once;
  /// an invalid model is refused here rather than at the first request.
  std::shared_ptr<PanelSession> add_panel(
    qar_ui_model::UiModel model, std::string * out_error = nullptr);
  /// @brief Close the panel's viewers and forget its token.
  bool remove_panel(std::string_view token);
  std::shared_ptr<PanelSession> find_panel(std::string_view token) const;
  std::vector<std::shared_ptr<PanelSession>> panels() const { return sessions_; }

  /// @brief `http://<advertised host>:<port>/p/<token>/` — what a QarOS URL panel loads.
  std::string page_url(const PanelSession & session) const;
  /// @brief `ws://<host_and_port>/ws/<token>` — what the served page dials.
  std::string websocket_url(const PanelSession & session, std::string_view host_and_port) const;

  /// @brief Addresses this machine could be reached at, best guess first.
  static std::vector<std::string> candidate_hosts();
  /// @brief The address the machine uses to reach the network; loopback if none.
  static std::string detect_advertised_host();

  WebPanelServerDiagnostics diagnostics() const;

Q_SIGNALS:
  void started();
  void stopped();
  void panel_added(const std::string & token);
  void panel_removed(const std::string & token);

private:
  HttpResponse handle_http(const HttpRequest & request);
  bool handle_upgrade(const HttpRequest & request, QTcpSocket * socket, HttpResponse & out_reject);
  void on_websocket_connection();
  HttpResponse render_page(const PanelSession & session, const HttpRequest & request);
  std::string host_for_urls(const HttpRequest & request) const;
  std::string authority() const;

  WebPanelServerConfig config_;
  HttpServer * http_ = nullptr;
  QWebSocketServer * websockets_ = nullptr;
  std::vector<std::shared_ptr<PanelSession>> sessions_;
  std::string advertised_host_;
  std::uint64_t token_misses_ = 0;
  std::uint64_t upgrades_refused_ = 0;
};

}  // namespace qar_web_panels

#endif  // QAR_WEB_PANELS__WEB_PANEL_SERVER_HPP_
