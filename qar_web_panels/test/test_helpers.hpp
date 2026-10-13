// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Test-side clients for the server: a raw TCP client that speaks just enough
// HTTP to read one reply, and a QWebSocket wrapper with QSignalSpy'd signals.
// Both are driven by the SAME event loop as the server under test, so every
// wait pumps events instead of blocking — a QTcpSocket::waitFor* here would
// starve the server and deadlock the test.

#ifndef QAR_WEB_PANELS__TEST_HELPERS_HPP_
#define QAR_WEB_PANELS__TEST_HELPERS_HPP_

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QString>
#include <QTcpSocket>
#include <QTest>
#include <QUrl>
#include <QWebSocket>

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "qar_ui_model/ui_protocol.hpp"
#include "qar_web_panels/http_message.hpp"

namespace qar_web_panels_test
{

inline bool
wait_until(const std::function<bool()> & predicate, int timeout_ms = 3000)
{
  QElapsedTimer timer;
  timer.start();
  while (!predicate()) {
    if (timer.elapsed() > timeout_ms) {
      return false;
    }
    QTest::qWait(5);
  }
  return true;
}

struct RawReply
{
  int status = 0;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string raw;
  bool complete = false;

  std::string header(std::string_view name) const
  {
    for (const auto & h : headers) {
      if (qar_web_panels::ascii_iequals(h.first, name)) {
        return h.second;
      }
    }
    return {};
  }
  bool has_header(std::string_view name) const
  {
    for (const auto & h : headers) {
      if (qar_web_panels::ascii_iequals(h.first, name)) {
        return true;
      }
    }
    return false;
  }
};

class RawHttpClient
{
public:
  explicit RawHttpClient(quint16 port)
  : port_(port)
  {
    QObject::connect(&socket_, &QTcpSocket::readyRead, [this]() {buffer_ += socket_.readAll();});
    // Latched, not sampled: a server that answers and closes at once (the 503
    // connection cap, a 404 for a bad token) can take the socket back out of
    // ConnectedState inside a single poll interval, so the instantaneous state
    // is not evidence that the connect failed. The `connected` signal is.
    QObject::connect(&socket_, &QTcpSocket::connected, [this]() {connected_ = true;});
    QObject::connect(&socket_, &QTcpSocket::disconnected, [this]() {
      buffer_ += socket_.readAll();
      disconnected_ = true;
    });
    // `stateChanged`, not the deprecated `error` overload: reaching
    // UnconnectedState is how a refused connect settles, and it lets the wait
    // below fail fast instead of burning its whole timeout.
    QObject::connect(
      &socket_, &QTcpSocket::stateChanged, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState) {
          settled_ = true;
        }
      });
    socket_.connectToHost(QHostAddress::LocalHost, port);
  }

  // The lambdas above have no context object, so they would still fire from
  // the socket's own destructor — after buffer_ is gone. Detach them first.
  ~RawHttpClient()
  {
    socket_.disconnect();
    socket_.abort();
  }

  /// True if the TCP connect ever succeeded — even if the peer has already
  /// closed it again by the time this returns.
  bool wait_connected(int timeout_ms = 3000)
  {
    wait_until([this]() {return connected_ || settled_;}, timeout_ms);
    return connected_;
  }

  void send(const std::string & bytes)
  {
    socket_.write(QByteArray::fromStdString(bytes));
  }

  std::string host_header() const
  {
    return "127.0.0.1:" + std::to_string(port_);
  }

  /// A minimal HTTP/1.1 request; @p extra_headers are complete `Name: value\r\n` lines.
  void request(const std::string & method, const std::string & path, const std::string & extra_headers = {})
  {
    send(method + " " + path + " HTTP/1.1\r\nHost: " + host_header() + "\r\n" + extra_headers + "\r\n");
  }

  void get(const std::string & path, const std::string & extra_headers = {})
  {
    request("GET", path, extra_headers);
  }

  /// Reads one reply: the head, then Content-Length bytes unless @p expect_body is false.
  RawReply read_reply(bool expect_body = true, int timeout_ms = 3000)
  {
    RawReply reply;
    wait_until([this]() {return buffer_.indexOf("\r\n\r\n") >= 0 || disconnected_;}, timeout_ms);
    const int head_end = buffer_.indexOf("\r\n\r\n");
    if (head_end < 0) {
      reply.raw = buffer_.toStdString();
      return reply;
    }
    const std::string head = buffer_.left(head_end).toStdString();
    std::size_t pos = 0;
    std::size_t line_no = 0;
    while (pos <= head.size()) {
      std::size_t nl = head.find("\r\n", pos);
      if (nl == std::string::npos) {
        nl = head.size();
      }
      const std::string line = head.substr(pos, nl - pos);
      if (line_no == 0) {
        if (line.size() >= 12 && line.compare(0, 9, "HTTP/1.1 ") == 0) {
          reply.status = std::stoi(line.substr(9, 3));
        }
      } else {
        const std::size_t colon = line.find(':');
        if (colon != std::string::npos) {
          std::string value = line.substr(colon + 1);
          while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
          }
          reply.headers.emplace_back(line.substr(0, colon), value);
        }
      }
      ++line_no;
      pos = nl + 2;
    }
    std::size_t content_length = 0;
    const std::string length = reply.header("Content-Length");
    if (expect_body && !length.empty()) {
      content_length = static_cast<std::size_t>(std::stoull(length));
    }
    const int needed = head_end + 4 + static_cast<int>(content_length);
    wait_until([this, needed]() {return buffer_.size() >= needed || disconnected_;}, timeout_ms);
    reply.complete = buffer_.size() >= needed;
    reply.body = buffer_.mid(head_end + 4, static_cast<int>(content_length)).toStdString();
    reply.raw = buffer_.left(needed).toStdString();
    buffer_.remove(0, needed);
    return reply;
  }

  bool wait_disconnected(int timeout_ms = 3000)
  {
    return wait_until([this]() {return disconnected_;}, timeout_ms);
  }

  bool disconnected() const { return disconnected_; }
  int buffered() const { return buffer_.size(); }
  QTcpSocket & socket() { return socket_; }

private:
  quint16 port_;
  QTcpSocket socket_;
  QByteArray buffer_;
  bool connected_ = false;
  bool settled_ = false;
  bool disconnected_ = false;
};

class WsClient
{
public:
  explicit WsClient(const std::string & url)
  : connected_spy(&socket, &QWebSocket::connected),
    disconnected_spy(&socket, &QWebSocket::disconnected)
  {
    hook();
    socket.open(QUrl(QString::fromStdString(url)));
  }

  explicit WsClient(const QNetworkRequest & request)
  : connected_spy(&socket, &QWebSocket::connected),
    disconnected_spy(&socket, &QWebSocket::disconnected)
  {
    hook();
    socket.open(request);
  }

  ~WsClient()
  {
    socket.disconnect();
    socket.abort();
  }

  bool wait_connected(int timeout_ms = 3000)
  {
    wait_until(
      [this]() {return connected_spy.count() > 0 || disconnected_spy.count() > 0 || errored;},
      timeout_ms);
    return connected_spy.count() > 0;
  }

  bool wait_frames(std::size_t count, int timeout_ms = 3000)
  {
    return wait_until([this, count]() {return frames.size() >= count;}, timeout_ms);
  }

  /// True if the socket was disconnected within @p timeout_ms. Qt 5.15.2 emits `disconnected`
  /// as soon as the TCP connection ends, but handles the bytes that arrived before it (the
  /// close frame of the peer, which sets closeCode()) in a queued call that can still be
  /// pending at that moment. The posted events are delivered before this returns, so that
  /// closeCode() is not read before that call has run.
  bool wait_closed(int timeout_ms = 3000)
  {
    if (!wait_until([this]() {return disconnected_spy.count() > 0;}, timeout_ms)) {
      return false;
    }
    QCoreApplication::sendPostedEvents(nullptr, 0);
    return true;
  }

  void send(const qar_ui_model::ProtocolMessage & message)
  {
    socket.sendTextMessage(QString::fromStdString(qar_ui_model::encode_message(message)));
  }

  void send_raw(const char * text)
  {
    socket.sendTextMessage(QString::fromUtf8(text));
  }

  /// Sends the page's hello and waits for the server's.
  bool hello(const std::string & panel = "robot")
  {
    send(qar_ui_model::make_hello(1, panel, qar_ui_model::kRuntimeName));
    return wait_frames(1);
  }

  qar_ui_model::ProtocolMessage frame(std::size_t index) const
  {
    qar_ui_model::ProtocolMessage message;
    std::string error;
    EXPECT_LT(index, frames.size());
    if (index < frames.size()) {
      EXPECT_TRUE(qar_ui_model::decode_message(frames[index].toStdString(), message, error)) << error;
    }
    return message;
  }

  QWebSocket socket;
  QSignalSpy connected_spy;
  QSignalSpy disconnected_spy;
  std::vector<QString> frames;
  bool errored = false;

private:
  void hook()
  {
    QObject::connect(&socket, &QWebSocket::textMessageReceived, [this](const QString & text) {
      frames.push_back(text);
    });
    QObject::connect(
      &socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error),
      [this](QAbstractSocket::SocketError) {errored = true;});
  }
};

}  // namespace qar_web_panels_test

#endif  // QAR_WEB_PANELS__TEST_HELPERS_HPP_
