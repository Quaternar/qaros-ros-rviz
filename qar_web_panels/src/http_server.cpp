// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_web_panels/http_server.hpp"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <string_view>
#include <vector>

namespace qar_web_panels
{

/// One accepted TCP connection: a request deadline, a peek-based parser and
/// the response writer. Owns its socket until an upgrade hands it away.
class HttpServer::Connection : public QObject
{
public:
  Connection(HttpServer & server, QTcpSocket * socket)
  : QObject(&server),
    server_(server),
    socket_(socket)
  {
    socket_->setParent(this);
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    attach_socket_signals();
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &Connection::on_timeout);
    arm_deadline();
  }

  ~Connection() override
  {
    server_.connection_closed(this);
  }

private:
  void attach_socket_signals()
  {
    connect(socket_, &QTcpSocket::readyRead, this, &Connection::on_ready_read);
    connect(socket_, &QTcpSocket::disconnected, this, &Connection::on_disconnected);
  }

  void arm_deadline()
  {
    timer_.start(server_.options_.request_timeout_ms);
  }

  void on_ready_read()
  {
    while (socket_ != nullptr && !closing_ && socket_->bytesAvailable() > 0) {
      const HttpParseLimits & limits = server_.options_.limits;
      // peek, not read: an upgrade request must stay in the socket for the
      // WebSocket handshake parser that takes the connection over.
      const QByteArray head = socket_->peek(static_cast<qint64>(limits.max_head_bytes));
      HttpRequest request;
      const HttpParseResult parsed = parse_http_request(
        std::string_view(head.constData(), static_cast<std::size_t>(head.size())), limits,
        request);
      if (parsed.status == HttpParseStatus::Incomplete) {
        return;
      }
      if (parsed.status == HttpParseStatus::Invalid) {
        reject(HttpResponse::text(parsed.http_status, parsed.reason + "\n"));
        return;
      }
      timer_.stop();
      if (request.is_websocket_upgrade()) {
        handle_upgrade(request);
        return;
      }
      socket_->skip(static_cast<qint64>(request.head_bytes));
      handle_request(request);
    }
  }

  void handle_upgrade(const HttpRequest & request)
  {
    QTcpSocket * socket = socket_;
    HttpResponse rejection = HttpResponse::not_found();
    rejection.close = true;

    if (server_.upgrade_handler_) {
      // Detach FIRST: QWebSocketServer::handleConnection re-emits readyRead on
      // the spot when bytes are already buffered, and that must not re-enter
      // this parser with the very same request.
      disconnect(socket, nullptr, this, nullptr);
      socket->setParent(nullptr);
      if (server_.upgrade_handler_(request, socket, rejection)) {
        ++server_.diagnostics_.upgrades;
        socket_ = nullptr;
        deleteLater();
        return;
      }
      socket->setParent(this);
      attach_socket_signals();
    }
    socket->skip(static_cast<qint64>(request.head_bytes));
    reject(std::move(rejection));
  }

  void handle_request(const HttpRequest & request)
  {
    HttpResponse response;
    bool served = false;
    if ((request.content_length.has_value() && *request.content_length > 0) ||
      request.has_transfer_encoding)
    {
      // No route takes a body, and keeping the connection in sync would mean
      // reading it: refuse and close instead.
      response = HttpResponse::text(413, "request bodies are not accepted\n");
      response.close = true;
    } else if (request.find_header("Expect") != nullptr) {
      response = HttpResponse::text(417, "expectations are not supported\n");
      response.close = true;
    } else if (request.method != "GET" && request.method != "HEAD") {
      response = HttpResponse::text(405, "method not allowed\n");
      response.extra_headers.push_back({"Allow", "GET, HEAD"});
      response.close = true;
    } else if (server_.handler_) {
      response = server_.handler_(request);
      served = true;
    } else {
      response = HttpResponse::not_found();
      served = true;
    }
    if (served) {
      ++server_.diagnostics_.requests_served;
    } else {
      ++server_.diagnostics_.requests_rejected;
    }

    const bool persist = request.keep_alive() && !response.close;
    socket_->write(QByteArray::fromStdString(
      serialize_http_response(response, request.method == "HEAD", persist, http_date_now())));
    if (persist) {
      arm_deadline();
    } else {
      close_after_flush();
    }
  }

  void reject(HttpResponse response)
  {
    ++server_.diagnostics_.requests_rejected;
    response.close = true;
    socket_->write(QByteArray::fromStdString(
      serialize_http_response(response, false, false, http_date_now())));
    close_after_flush();
  }

  void close_after_flush()
  {
    closing_ = true;
    timer_.stop();
    if (socket_->state() == QAbstractSocket::ConnectedState) {
      // Waits for the write buffer to drain, then emits disconnected.
      socket_->disconnectFromHost();
    } else {
      deleteLater();
    }
  }

  void on_disconnected()
  {
    deleteLater();
  }

  void on_timeout()
  {
    ++server_.diagnostics_.timeouts;
    if (socket_ == nullptr || closing_) {
      return;
    }
    if (socket_->bytesAvailable() > 0) {
      reject(HttpResponse::text(408, "request timeout\n"));
      return;
    }
    closing_ = true;
    socket_->abort();
    deleteLater();
  }

  HttpServer & server_;
  QTcpSocket * socket_ = nullptr;
  QTimer timer_;
  bool closing_ = false;
};

HttpServer::HttpServer(HttpServerOptions options, QObject * parent)
: QObject(parent),
  options_(options),
  tcp_(new QTcpServer(this))
{
  tcp_->setMaxPendingConnections(static_cast<int>(options_.max_connections));
  connect(tcp_, &QTcpServer::newConnection, this, &HttpServer::on_new_connection);
}

HttpServer::~HttpServer()
{
  close();
}

bool
HttpServer::listen(const QHostAddress & address, quint16 port, std::string & out_error)
{
  if (tcp_->isListening()) {
    out_error = "already listening";
    return false;
  }
  if (!tcp_->listen(address, port)) {
    out_error = tcp_->errorString().toStdString();
    return false;
  }
  out_error.clear();
  return true;
}

void
HttpServer::close()
{
  tcp_->close();
  // Connections are children of this object, but deleting them here, while the
  // members they report into still exist, keeps ~Connection well-defined.
  const std::vector<Connection *> open(connections_.begin(), connections_.end());
  for (Connection * c : open) {
    delete c;
  }
  connections_.clear();
}

bool
HttpServer::is_listening() const
{
  return tcp_->isListening();
}

quint16
HttpServer::server_port() const
{
  return tcp_->serverPort();
}

QHostAddress
HttpServer::server_address() const
{
  return tcp_->serverAddress();
}

HttpServerDiagnostics
HttpServer::diagnostics() const
{
  HttpServerDiagnostics d = diagnostics_;
  d.open_connections = connections_.size();
  return d;
}

void
HttpServer::on_new_connection()
{
  while (tcp_->hasPendingConnections()) {
    QTcpSocket * socket = tcp_->nextPendingConnection();
    if (socket == nullptr) {
      break;
    }
    if (connections_.size() >= options_.max_connections) {
      ++diagnostics_.connections_refused;
      HttpResponse busy = HttpResponse::text(503, "too many connections\n");
      busy.extra_headers.push_back({"Retry-After", "1"});
      socket->write(QByteArray::fromStdString(
        serialize_http_response(busy, false, false, http_date_now())));
      connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      socket->disconnectFromHost();
      continue;
    }
    ++diagnostics_.connections_accepted;
    connections_.insert(new Connection(*this, socket));
  }
}

void
HttpServer::connection_closed(Connection * connection)
{
  connections_.erase(connection);
}

}  // namespace qar_web_panels
