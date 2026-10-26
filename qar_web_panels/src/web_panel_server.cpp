// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_web_panels/web_panel_server.hpp"

#include <QNetworkInterface>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

#include <algorithm>

#include "qar_web_panels/panel_token.hpp"

namespace qar_web_panels
{
namespace
{

constexpr std::string_view kPagePrefix = "/p/";
constexpr std::string_view kSocketPrefix = "/ws/";

/// Splits `/p/<token>/rest` into token and rest ("" or "/..."). The token is
/// only shape-checked here; whether it names a session is the caller's lookup.
bool
split_prefixed(std::string_view path, std::string_view prefix, std::string & out_token, std::string & out_rest)
{
  if (path.size() <= prefix.size() || path.substr(0, prefix.size()) != prefix) {
    return false;
  }
  const std::string_view tail = path.substr(prefix.size());
  const std::size_t slash = tail.find('/');
  const std::string_view token = tail.substr(0, slash);
  if (!is_well_formed_token(token)) {
    return false;
  }
  out_token.assign(token);
  out_rest.assign(slash == std::string_view::npos ? std::string_view() : tail.substr(slash));
  return true;
}

std::string
bracketed_if_v6(const std::string & host)
{
  const QHostAddress address(QString::fromStdString(host));
  if (!address.isNull() && address.protocol() == QAbstractSocket::IPv6Protocol &&
    host.find('[') == std::string::npos)
  {
    return "[" + host + "]";
  }
  return host;
}

bool
is_wildcard(const QHostAddress & address)
{
  return address == QHostAddress::Any || address == QHostAddress::AnyIPv4 ||
         address == QHostAddress::AnyIPv6;
}

bool
usable_lan_address(const QHostAddress & address)
{
  if (address.isNull() || address.isLoopback()) {
    return false;
  }
  if (address.protocol() == QAbstractSocket::IPv4Protocol) {
    // 169.254/16 is what a machine gives itself when nobody else did.
    return !address.isInSubnet(QHostAddress("169.254.0.0"), 16);
  }
  if (address.protocol() == QAbstractSocket::IPv6Protocol) {
    return !address.isLinkLocal() && !address.isSiteLocal() && !address.isUniqueLocalUnicast();
  }
  return false;
}

}  // namespace

WebPanelServer::WebPanelServer(WebPanelServerConfig config, QObject * parent)
: QObject(parent),
  config_(std::move(config)),
  http_(new HttpServer(config_.http, this)),
  websockets_(new QWebSocketServer(
      QStringLiteral("qar_web_panels"), QWebSocketServer::NonSecureMode, this))
{
  // The WebSocket server never listens: sockets reach it through handleConnection
  // from the HTTP server, once the request has been parsed and the token checked.
  // Per-socket limits (message size, backlog) are applied by the session.
  connect(
    websockets_, &QWebSocketServer::newConnection, this, &WebPanelServer::on_websocket_connection);

  http_->set_handler([this](const HttpRequest & request) {return handle_http(request);});
  http_->set_upgrade_handler(
    [this](const HttpRequest & request, QTcpSocket * socket, HttpResponse & reject) {
      return handle_upgrade(request, socket, reject);
    });
}

WebPanelServer::~WebPanelServer()
{
  stop();
}

bool
WebPanelServer::start(std::string * out_error)
{
  if (is_running()) {
    if (out_error != nullptr) {
      out_error->clear();
    }
    return true;
  }
  std::string error;
  if (!http_->listen(config_.bind_address, config_.port, error)) {
    if (out_error != nullptr) {
      *out_error = "cannot listen on " + config_.bind_address.toString().toStdString() + ":" +
        std::to_string(config_.port) + ": " + error;
    }
    return false;
  }
  if (!config_.advertised_host.empty()) {
    advertised_host_ = config_.advertised_host;
  } else if (!is_wildcard(config_.bind_address)) {
    advertised_host_ = config_.bind_address.toString().toStdString();
  } else {
    advertised_host_ = detect_advertised_host();
  }
  if (out_error != nullptr) {
    out_error->clear();
  }
  emit started();
  return true;
}

void
WebPanelServer::stop()
{
  const bool was_running = is_running();
  http_->close();
  for (const auto & session : sessions_) {
    session->close_all(QWebSocketProtocol::CloseCodeGoingAway, QStringLiteral("server stopping"));
  }
  if (was_running) {
    emit stopped();
  }
}

bool
WebPanelServer::is_running() const
{
  return http_->is_listening();
}

quint16
WebPanelServer::port() const
{
  return http_->server_port();
}

std::shared_ptr<PanelSession>
WebPanelServer::add_panel(qar_ui_model::UiModel model, std::string * out_error)
{
  std::string html;
  std::vector<qar_ui_model::UiProblem> problems;
  if (!qar_ui_model::render_html_page(model, config_.render, html, problems)) {
    if (out_error != nullptr) {
      *out_error = "model does not render";
      for (const auto & p : problems) {
        if (p.severity == qar_ui_model::ProblemSeverity::Error) {
          *out_error += ": " + (p.widget_id.empty() ? std::string() : p.widget_id + ": ") + p.message;
          break;
        }
      }
    }
    return nullptr;
  }

  std::string token = generate_panel_token();
  while (find_panel(token) != nullptr) {  // 2^-128 per draw; the loop is for the reader.
    token = generate_panel_token();
  }
  auto session = std::make_shared<PanelSession>(std::move(model), token, config_.session);
  sessions_.push_back(session);
  if (out_error != nullptr) {
    out_error->clear();
  }
  emit panel_added(token);
  return session;
}

bool
WebPanelServer::remove_panel(std::string_view token)
{
  const auto it = std::find_if(
    sessions_.begin(), sessions_.end(),
    [token](const auto & s) {return tokens_equal(s->token(), token);});
  if (it == sessions_.end()) {
    return false;
  }
  std::shared_ptr<PanelSession> session = *it;
  sessions_.erase(it);
  session->close_all(QWebSocketProtocol::CloseCodeGoingAway, QStringLiteral("panel removed"));
  emit panel_removed(session->token());
  return true;
}

std::shared_ptr<PanelSession>
WebPanelServer::find_panel(std::string_view token) const
{
  // Every session is compared, in constant time each, so the answer's timing
  // says nothing about how close a guess came.
  std::shared_ptr<PanelSession> found;
  for (const auto & session : sessions_) {
    if (tokens_equal(session->token(), token)) {
      found = session;
    }
  }
  return found;
}

std::string
WebPanelServer::authority() const
{
  return bracketed_if_v6(advertised_host_) + ":" + std::to_string(port());
}

std::string
WebPanelServer::page_url(const PanelSession & session) const
{
  return "http://" + authority() + std::string(kPagePrefix) + session.token() + "/";
}

std::string
WebPanelServer::websocket_url(const PanelSession & session, std::string_view host_and_port) const
{
  return "ws://" + std::string(host_and_port) + std::string(kSocketPrefix) + session.token();
}

std::string
WebPanelServer::host_for_urls(const HttpRequest & request) const
{
  // Reflect the Host the client used, so the socket URL reaches this server by
  // the same address the page did — whichever of our interfaces that was.
  const std::string host = request.header("Host");
  if (is_acceptable_host_header(host)) {
    return host;
  }
  return authority();
}

// --- HTTP ---------------------------------------------------------------------------------

HttpResponse
WebPanelServer::render_page(const PanelSession & session, const HttpRequest & request)
{
  qar_ui_model::HtmlRenderOptions options = config_.render;
  options.websocket_url = websocket_url(session, host_for_urls(request));
  std::string html;
  std::vector<qar_ui_model::UiProblem> problems;
  if (!qar_ui_model::render_html_page(session.model(), options, html, problems)) {
    return HttpResponse::text(500, "panel failed to render\n");
  }
  return HttpResponse::html(std::move(html));
}

HttpResponse
WebPanelServer::handle_http(const HttpRequest & request)
{
  const std::string_view path = request.path;
  if (path == "/") {
    return HttpResponse::text(200, "qar_web_panels\n");
  }

  std::string token;
  std::string rest;
  if (split_prefixed(path, kPagePrefix, token, rest)) {
    const std::shared_ptr<PanelSession> session = find_panel(token);
    if (session == nullptr) {
      ++token_misses_;
      return HttpResponse::not_found();
    }
    if (rest.empty() || rest == "/") {
      return render_page(*session, request);
    }
    const PanelResource * resource = session->find_resource(std::string_view(rest).substr(1));
    if (resource == nullptr) {
      return HttpResponse::not_found();
    }
    HttpResponse response;
    response.content_type = resource->content_type;
    response.body = resource->bytes;
    return response;
  }

  if (path.size() > kSocketPrefix.size() && path.substr(0, kSocketPrefix.size()) == kSocketPrefix) {
    // A plain GET on the socket route, token valid or not: say what it is for,
    // without confirming whether the token exists.
    HttpResponse response = HttpResponse::text(426, "this route speaks WebSocket\n");
    response.extra_headers.push_back({"Upgrade", "websocket"});
    response.close = true;
    return response;
  }

  return HttpResponse::not_found();
}

bool
WebPanelServer::handle_upgrade(
  const HttpRequest & request, QTcpSocket * socket, HttpResponse & out_reject)
{
  std::string token;
  std::string rest;
  if (!split_prefixed(request.path, kSocketPrefix, token, rest) || !rest.empty() ||
    find_panel(token) == nullptr)
  {
    ++token_misses_;
    ++upgrades_refused_;
    out_reject = HttpResponse::not_found();
    return false;
  }
  if (!config_.allow_any_origin) {
    // A browser always sends Origin; it must be the page we served, i.e. the
    // very Host this handshake arrived on. Non-browser clients send none and
    // are admitted on the token alone.
    const std::string origin = request.header("Origin");
    if (!origin.empty() && !ascii_iequals(origin_authority(origin), request.header("Host"))) {
      ++upgrades_refused_;
      out_reject = HttpResponse::text(403, "cross-origin websocket refused\n");
      return false;
    }
  }
  const std::shared_ptr<PanelSession> session = find_panel(token);
  if (session->client_count() >= config_.session.max_clients) {
    ++upgrades_refused_;
    out_reject = HttpResponse::text(503, "too many viewers\n");
    return false;
  }
  websockets_->handleConnection(socket);
  return true;
}

void
WebPanelServer::on_websocket_connection()
{
  while (QWebSocket * socket = websockets_->nextPendingConnection()) {
    std::string token;
    std::string rest;
    const std::string path = socket->requestUrl().path().toStdString();
    const std::shared_ptr<PanelSession> session =
      split_prefixed(path, kSocketPrefix, token, rest) && rest.empty() ? find_panel(token) : std::shared_ptr<PanelSession>();
    if (session == nullptr) {
      socket->close(QWebSocketProtocol::CloseCodePolicyViolated, QStringLiteral("unknown panel"));
      socket->deleteLater();
      continue;
    }
    session->attach_client(socket);
  }
}

// --- Addresses -------------------------------------------------------------------------------

std::string
WebPanelServer::detect_advertised_host()
{
  // A UDP "connect" sends nothing, but makes the OS pick the interface it
  // would route through — the address the rest of the network knows us by.
  // TEST-NET-1 is never assigned, so no real destination is implied.
  QUdpSocket probe;
  probe.connectToHost(QHostAddress(QStringLiteral("192.0.2.1")), 9);
  if (probe.state() == QAbstractSocket::ConnectedState && usable_lan_address(probe.localAddress())) {
    return probe.localAddress().toString().toStdString();
  }
  const std::vector<std::string> hosts = candidate_hosts();
  return hosts.empty() ? std::string("127.0.0.1") : hosts.front();
}

std::vector<std::string>
WebPanelServer::candidate_hosts()
{
  std::vector<std::string> ipv4;
  std::vector<std::string> ipv6;
  for (const QNetworkInterface & iface : QNetworkInterface::allInterfaces()) {
    const auto flags = iface.flags();
    if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning) ||
      (flags & QNetworkInterface::IsLoopBack))
    {
      continue;
    }
    for (const QNetworkAddressEntry & entry : iface.addressEntries()) {
      const QHostAddress address = entry.ip();
      if (!usable_lan_address(address)) {
        continue;
      }
      (address.protocol() == QAbstractSocket::IPv4Protocol ? ipv4 : ipv6)
      .push_back(address.toString().toStdString());
    }
  }
  // IPv4 first: a headset on a plain Wi-Fi network is far likelier to reach it.
  ipv4.insert(ipv4.end(), ipv6.begin(), ipv6.end());
  return ipv4;
}

WebPanelServerDiagnostics
WebPanelServer::diagnostics() const
{
  WebPanelServerDiagnostics d;
  d.http = http_->diagnostics();
  d.panels = sessions_.size();
  for (const auto & session : sessions_) {
    d.clients += session->client_count();
  }
  d.token_misses = token_misses_;
  d.upgrades_refused = upgrades_refused_;
  return d;
}

}  // namespace qar_web_panels
