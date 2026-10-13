// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_WEB_PANELS__HTTP_SERVER_HPP_
#define QAR_WEB_PANELS__HTTP_SERVER_HPP_

/// @file http_server.hpp
/// @brief A small, correct HTTP/1.1 origin server over QTcpServer.
///
/// It serves whatever the handler returns — generated content from memory,
/// never a file — and hands WebSocket upgrade requests to an upgrade handler
/// with the request bytes still UNREAD in the socket, so QWebSocketServer's
/// own handshake parser can take over the same connection. That is what lets
/// the page and its socket share one port and one Host, and lets the page
/// derive its `ws://` URL from its own location.
///
/// Everything happens on the thread that owns the server (the GUI thread in
/// the RViz plugin). Nothing here blocks: Qt sockets are asynchronous, every
/// response is written in one call, and a slow or hostile client only costs
/// its own connection (head-size limit, request deadline, connection cap).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <set>
#include <string>

#include <QHostAddress>
#include <QObject>

#include "qar_web_panels/http_message.hpp"

class QTcpServer;
class QTcpSocket;

namespace qar_web_panels
{

struct HttpServerOptions
{
  HttpParseLimits limits;
  std::size_t max_connections = 64;
  /// A request head must arrive within this, and an idle keep-alive
  /// connection is dropped after it. Deliberately NOT reset by partial bytes,
  /// so a trickling client (slowloris) cannot hold a connection open.
  int request_timeout_ms = 15000;
};

struct HttpServerDiagnostics
{
  std::uint64_t connections_accepted = 0;
  std::uint64_t connections_refused = 0;  ///< Over max_connections: answered 503.
  std::uint64_t requests_served = 0;      ///< Handler responses, any status.
  std::uint64_t requests_rejected = 0;    ///< Parse failures, bodies, bad methods, refused upgrades.
  std::uint64_t upgrades = 0;             ///< Sockets handed to the upgrade handler.
  std::uint64_t timeouts = 0;
  std::size_t open_connections = 0;
};

class HttpServer : public QObject
{
  Q_OBJECT

public:
  using Handler = std::function<HttpResponse(const HttpRequest & request)>;

  /// Called for a well-formed upgrade request before any of its bytes are
  /// consumed. Return true to take the socket (it has been detached from the
  /// server and reparented to nullptr; ownership passes to the callee), or
  /// false and fill @p out_reject to answer the request instead.
  using UpgradeHandler =
    std::function<bool(const HttpRequest & request, QTcpSocket * socket, HttpResponse & out_reject)>;

  explicit HttpServer(HttpServerOptions options = {}, QObject * parent = nullptr);
  ~HttpServer() override;

  HttpServer(const HttpServer &) = delete;
  HttpServer & operator=(const HttpServer &) = delete;

  void set_handler(Handler handler) { handler_ = std::move(handler); }
  void set_upgrade_handler(UpgradeHandler handler) { upgrade_handler_ = std::move(handler); }

  bool listen(const QHostAddress & address, quint16 port, std::string & out_error);
  /// Stops listening and drops every open connection.
  void close();
  bool is_listening() const;
  quint16 server_port() const;
  QHostAddress server_address() const;

  const HttpServerOptions & options() const { return options_; }
  HttpServerDiagnostics diagnostics() const;

private:
  class Connection;
  friend class Connection;

  void on_new_connection();
  void connection_closed(Connection * connection);

  HttpServerOptions options_;
  Handler handler_;
  UpgradeHandler upgrade_handler_;
  QTcpServer * tcp_ = nullptr;
  std::set<Connection *> connections_;
  HttpServerDiagnostics diagnostics_;
};

}  // namespace qar_web_panels

#endif  // QAR_WEB_PANELS__HTTP_SERVER_HPP_
