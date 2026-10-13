// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The HTTP server against a raw TCP client on loopback: real sockets, real
// bytes, the same event loop.

#include <gtest/gtest.h>

#include <QTcpSocket>

#include <memory>
#include <string>

#include "qar_web_panels/http_server.hpp"
#include "test_helpers.hpp"

using namespace qar_web_panels;
using namespace qar_web_panels_test;

namespace
{

class HttpServerTest : public ::testing::Test
{
protected:
  void start(HttpServerOptions options = {})
  {
    server = std::make_unique<HttpServer>(options);
    server->set_handler([this](const HttpRequest & request) {
      last_request = request;
      if (request.path == "/hello") {
        return HttpResponse::html("<p>hi</p>");
      }
      if (request.path == "/close") {
        HttpResponse r = HttpResponse::text(200, "bye\n");
        r.close = true;
        return r;
      }
      return HttpResponse::not_found();
    });
    std::string error;
    ASSERT_TRUE(server->listen(QHostAddress::LocalHost, 0, error)) << error;
    ASSERT_NE(server->server_port(), 0);
  }

  std::unique_ptr<RawHttpClient> client()
  {
    auto c = std::make_unique<RawHttpClient>(server->server_port());
    EXPECT_TRUE(c->wait_connected());
    return c;
  }

  std::unique_ptr<HttpServer> server;
  HttpRequest last_request;
};

}  // namespace

TEST_F(HttpServerTest, ServesTheHandlerResponseWithTheExpectedHeaders)
{
  start();
  auto c = client();
  c->get("/hello?x=1");
  const RawReply reply = c->read_reply();
  EXPECT_EQ(reply.status, 200);
  EXPECT_TRUE(reply.complete);
  EXPECT_EQ(reply.body, "<p>hi</p>");
  EXPECT_EQ(reply.header("Content-Type"), "text/html; charset=utf-8");
  EXPECT_EQ(reply.header("Content-Length"), "9");
  EXPECT_EQ(reply.header("Connection"), "keep-alive");
  EXPECT_EQ(reply.header("Cache-Control"), "no-store");
  EXPECT_EQ(reply.header("X-Content-Type-Options"), "nosniff");
  EXPECT_EQ(reply.header("Referrer-Policy"), "no-referrer");
  EXPECT_EQ(reply.header("Date").size(), 29u);
  EXPECT_EQ(last_request.query, "x=1");
  EXPECT_FALSE(c->disconnected());
  EXPECT_EQ(server->diagnostics().requests_served, 1u);
  EXPECT_EQ(server->diagnostics().open_connections, 1u);
}

TEST_F(HttpServerTest, HeadKeepsTheLengthAndDropsTheBody)
{
  start();
  auto c = client();
  c->request("HEAD", "/hello");
  const RawReply reply = c->read_reply(false);
  EXPECT_EQ(reply.status, 200);
  EXPECT_EQ(reply.header("Content-Length"), "9");
  EXPECT_TRUE(reply.body.empty());
  // Nothing after the head: a second request must be answered from a clean buffer.
  c->get("/hello");
  const RawReply second = c->read_reply();
  EXPECT_EQ(second.status, 200);
  EXPECT_EQ(second.body, "<p>hi</p>");
}

TEST_F(HttpServerTest, UnknownPathIs404)
{
  start();
  auto c = client();
  c->get("/nothing");
  const RawReply reply = c->read_reply();
  EXPECT_EQ(reply.status, 404);
  EXPECT_EQ(reply.header("Content-Type"), "text/plain; charset=utf-8");
}

TEST_F(HttpServerTest, KeepAliveServesSeveralRequestsAndHonoursClose)
{
  start();
  auto c = client();
  c->get("/hello");
  EXPECT_EQ(c->read_reply().status, 200);
  c->get("/nothing");
  EXPECT_EQ(c->read_reply().status, 404);
  EXPECT_FALSE(c->disconnected());
  c->get("/hello", "Connection: close\r\n");
  const RawReply last = c->read_reply();
  EXPECT_EQ(last.status, 200);
  EXPECT_EQ(last.header("Connection"), "close");
  EXPECT_TRUE(c->wait_disconnected());
  EXPECT_TRUE(wait_until([this]() {return server->diagnostics().open_connections == 0;}));
}

TEST_F(HttpServerTest, Http10ClosesUnlessKeepAliveAndHandlerCanForceClose)
{
  start();
  auto c = client();
  c->send("GET /hello HTTP/1.0\r\n\r\n");
  const RawReply reply = c->read_reply();
  EXPECT_EQ(reply.status, 200);
  EXPECT_EQ(reply.header("Connection"), "close");
  EXPECT_TRUE(c->wait_disconnected());

  auto d = client();
  d->get("/close");
  const RawReply forced = d->read_reply();
  EXPECT_EQ(forced.status, 200);
  EXPECT_EQ(forced.header("Connection"), "close");
  EXPECT_TRUE(d->wait_disconnected());
}

TEST_F(HttpServerTest, PipelinedRequestsAreAnsweredInOrder)
{
  start();
  auto c = client();
  c->send(
    "GET /hello HTTP/1.1\r\nHost: h\r\n\r\n"
    "GET /nothing HTTP/1.1\r\nHost: h\r\n\r\n"
    "HEAD /hello HTTP/1.1\r\nHost: h\r\n\r\n");
  EXPECT_EQ(c->read_reply().status, 200);
  EXPECT_EQ(c->read_reply().status, 404);
  EXPECT_EQ(c->read_reply(false).status, 200);
  EXPECT_EQ(server->diagnostics().requests_served, 3u);
}

TEST_F(HttpServerTest, MalformedInputIsRejectedAndTheConnectionClosed)
{
  start();
  struct Case { const char * bytes; int status; };
  const Case cases[] = {
    {"GET /hello\r\nHost: h\r\n\r\n", 400},
    {"GET /hello HTTP/1.1\r\n\r\n", 400},
    {"GET /../etc/passwd HTTP/1.1\r\nHost: h\r\n\r\n", 400},
    {"GET /p/%2e%2e/x HTTP/1.1\r\nHost: h\r\n\r\n", 400},
    {"GET /hello HTTP/2.0\r\nHost: h\r\n\r\n", 505},
    {"POST /hello HTTP/1.1\r\nHost: h\r\n\r\n", 405},
    {"GET /hello HTTP/1.1\r\nHost: h\r\nContent-Length: 5\r\n\r\nhello", 413},
    {"GET /hello HTTP/1.1\r\nHost: h\r\nTransfer-Encoding: chunked\r\n\r\n", 413},
    {"GET /hello HTTP/1.1\r\nHost: h\r\nExpect: 100-continue\r\n\r\n", 417},
  };
  for (const Case & k : cases) {
    auto c = client();
    c->send(k.bytes);
    const RawReply reply = c->read_reply();
    EXPECT_EQ(reply.status, k.status) << k.bytes;
    EXPECT_EQ(reply.header("Connection"), "close") << k.bytes;
    if (k.status == 405) {
      EXPECT_EQ(reply.header("Allow"), "GET, HEAD");
    }
    EXPECT_TRUE(c->wait_disconnected()) << k.bytes;
  }
  EXPECT_EQ(server->diagnostics().requests_rejected, 9u);
  EXPECT_EQ(server->diagnostics().requests_served, 0u);
}

TEST_F(HttpServerTest, OversizedHeadIs431)
{
  HttpServerOptions options;
  options.limits.max_head_bytes = 256;
  start(options);
  auto c = client();
  c->send("GET /hello HTTP/1.1\r\nHost: h\r\nX: " + std::string(400, 'y') + "\r\n\r\n");
  EXPECT_EQ(c->read_reply().status, 431);
  EXPECT_TRUE(c->wait_disconnected());

  // Also without any terminator at all: the buffer alone trips the limit.
  auto d = client();
  d->send(std::string(300, 'z'));
  EXPECT_EQ(d->read_reply().status, 431);
  EXPECT_TRUE(d->wait_disconnected());
}

TEST_F(HttpServerTest, SlowRequestHitsTheDeadline)
{
  HttpServerOptions options;
  options.request_timeout_ms = 200;
  start(options);
  auto c = client();
  c->send("GET /hel");
  const RawReply reply = c->read_reply(true, 2000);
  EXPECT_EQ(reply.status, 408);
  EXPECT_TRUE(c->wait_disconnected());
  EXPECT_EQ(server->diagnostics().timeouts, 1u);

  // An idle keep-alive connection is dropped too, silently.
  auto d = client();
  d->get("/hello");
  EXPECT_EQ(d->read_reply().status, 200);
  EXPECT_TRUE(d->wait_disconnected(2000));
}

TEST_F(HttpServerTest, ConnectionCapAnswers503)
{
  HttpServerOptions options;
  options.max_connections = 1;
  start(options);
  auto first = client();
  first->get("/hello");
  EXPECT_EQ(first->read_reply().status, 200);
  auto second = client();
  const RawReply busy = second->read_reply();
  EXPECT_EQ(busy.status, 503);
  EXPECT_EQ(busy.header("Retry-After"), "1");
  EXPECT_TRUE(second->wait_disconnected());
  EXPECT_EQ(server->diagnostics().connections_refused, 1u);
  EXPECT_EQ(server->diagnostics().connections_accepted, 1u);
}

TEST_F(HttpServerTest, UpgradeHandlerReceivesTheSocketWithTheRequestStillUnread)
{
  start();
  qint64 seen_bytes = -1;
  QByteArray seen_head;
  std::string seen_path;
  server->set_upgrade_handler(
    [&](const HttpRequest & request, QTcpSocket * socket, HttpResponse &) {
      seen_path = request.path;
      seen_bytes = socket->bytesAvailable();
      seen_head = socket->peek(4);
      socket->readAll();
      socket->write("HTTP/1.1 418 I'm a teapot\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
      socket->disconnectFromHost();
      return true;
    });
  const std::string request =
    "GET /ws/x HTTP/1.1\r\nHost: h\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n"
    "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
  auto c = client();
  c->send(request);
  const RawReply reply = c->read_reply(false);
  EXPECT_EQ(reply.status, 418);
  EXPECT_EQ(seen_path, "/ws/x");
  EXPECT_EQ(seen_bytes, static_cast<qint64>(request.size()));
  EXPECT_EQ(seen_head, QByteArray("GET "));
  EXPECT_TRUE(c->wait_disconnected());
  EXPECT_EQ(server->diagnostics().upgrades, 1u);
}

TEST_F(HttpServerTest, UpgradeWithoutAHandlerOrRefusedByItIsAnsweredNormally)
{
  start();
  const std::string request =
    "GET /ws/x HTTP/1.1\r\nHost: h\r\nConnection: Upgrade\r\nUpgrade: websocket\r\n\r\n";
  auto c = client();
  c->send(request);
  EXPECT_EQ(c->read_reply().status, 404);
  EXPECT_TRUE(c->wait_disconnected());

  server->set_upgrade_handler(
    [](const HttpRequest &, QTcpSocket *, HttpResponse & reject) {
      reject = HttpResponse::text(403, "no\n");
      return false;
    });
  auto d = client();
  d->send(request);
  const RawReply refused = d->read_reply();
  EXPECT_EQ(refused.status, 403);
  EXPECT_EQ(refused.body, "no\n");
  EXPECT_TRUE(d->wait_disconnected());
  EXPECT_EQ(server->diagnostics().upgrades, 0u);
  EXPECT_EQ(server->diagnostics().requests_rejected, 2u);
}

TEST_F(HttpServerTest, CloseDropsOpenConnections)
{
  start();
  auto c = client();
  c->get("/hello");
  EXPECT_EQ(c->read_reply().status, 200);
  server->close();
  EXPECT_FALSE(server->is_listening());
  EXPECT_TRUE(c->wait_disconnected());
  EXPECT_EQ(server->diagnostics().open_connections, 0u);
}
