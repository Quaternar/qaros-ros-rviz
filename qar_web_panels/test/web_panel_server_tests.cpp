// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// The whole delivery path on loopback: page over HTTP, the qar-ui/1 socket
// over the same port, tokens, sessions, and the served page's own consistency.
// The page's JavaScript is NOT executed here (no browser in this build); what
// is pinned is that the bytes it would run are well-formed and agree with the
// server that produced them.

#include <gtest/gtest.h>

#include <QNetworkRequest>
#include <QSignalSpy>

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "qar_ui_model/json.hpp"
#include "qar_ui_model/ui_model.hpp"
#include "qar_web_panels/panel_token.hpp"
#include "qar_web_panels/web_panel_server.hpp"
#include "test_helpers.hpp"

using namespace qar_web_panels;
using namespace qar_web_panels_test;
using qar_ui_model::MessageType;
using qar_ui_model::ProtocolMessage;
using qar_ui_model::UiModel;
using qar_ui_model::UiValue;
namespace widgets = qar_ui_model::widgets;

namespace
{

UiModel
demo_model()
{
  UiModel model;
  model.root = widgets::panel("robot", "Robot")
    .add(widgets::slider("speed", "speed", "Speed", 0.0, 2.0).with_action("publish:/cmd_vel"))
    .add(widgets::value("battery", "battery.percentage", "Battery"))
    .add(widgets::button("stop", "Stop", "call:/estop"));
  model.initial_data = UiValue::object()
    .set("speed", 0.5)
    .set("battery", UiValue::object().set("percentage", 0.8));
  return model;
}

struct RecordingSink : IPanelControlSink
{
  std::vector<PanelControlEvent> events;
  void on_control(PanelSession &, const PanelControlEvent & event) override
  {
    events.push_back(event);
  }
};

std::string
extract_json_block(const std::string & html, const std::string & id)
{
  const std::string open = "<script type=\"application/json\" id=\"" + id + "\">";
  const std::size_t start = html.find(open);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t body = start + open.size();
  const std::size_t end = html.find("</script>", body);
  return html.substr(body, end - body);
}

UiValue
parse_json(const std::string & text)
{
  UiValue value;
  qar_ui_model::JsonParseError error;
  EXPECT_TRUE(qar_ui_model::json_parse(text, value, error)) << error.to_string();
  return value;
}

class WebPanelServerTest : public ::testing::Test
{
protected:
  void start(WebPanelServerConfig config = {})
  {
    config.bind_address = QHostAddress::LocalHost;
    server = std::make_unique<WebPanelServer>(config);
    std::string error;
    ASSERT_TRUE(server->start(&error)) << error;
    ASSERT_NE(server->port(), 0);
    session = server->add_panel(demo_model(), &error);
    ASSERT_NE(session, nullptr) << error;
  }

  std::string page_path() const { return "/p/" + session->token() + "/"; }
  std::string ws_url(const std::string & token) const
  {
    return "ws://127.0.0.1:" + std::to_string(server->port()) + "/ws/" + token;
  }
  std::string ws_url() const { return ws_url(session->token()); }

  RawReply http_get(const std::string & path)
  {
    RawHttpClient c(server->port());
    EXPECT_TRUE(c.wait_connected());
    c.get(path);
    return c.read_reply();
  }

  std::unique_ptr<WebPanelServer> server;
  std::shared_ptr<PanelSession> session;
};

}  // namespace

TEST_F(WebPanelServerTest, StartsOnAnEphemeralPortAndBuildsTokenedUrls)
{
  start();
  EXPECT_TRUE(server->is_running());
  EXPECT_TRUE(is_well_formed_token(session->token()));
  EXPECT_EQ(session->panel_id(), "robot");
  EXPECT_EQ(server->advertised_host(), "127.0.0.1") << "a concrete bind address is advertised as-is";
  EXPECT_EQ(
    server->page_url(*session),
    "http://127.0.0.1:" + std::to_string(server->port()) + "/p/" + session->token() + "/");
  EXPECT_EQ(server->websocket_url(*session, "10.0.0.2:81"), "ws://10.0.0.2:81/ws/" + session->token());
  EXPECT_EQ(server->find_panel(session->token()), session);
  EXPECT_EQ(server->find_panel(generate_panel_token()), nullptr);
  EXPECT_EQ(server->panels().size(), 1u);
  EXPECT_EQ(server->diagnostics().panels, 1u);
}

TEST_F(WebPanelServerTest, ServesThePageOnlyWithTheRightToken)
{
  start();
  const RawReply page = http_get(page_path());
  EXPECT_EQ(page.status, 200);
  EXPECT_EQ(page.header("Content-Type"), "text/html; charset=utf-8");
  EXPECT_NE(page.body.find("<!DOCTYPE html>"), std::string::npos);
  EXPECT_EQ(http_get("/p/" + session->token()).status, 200) << "without the trailing slash";

  EXPECT_EQ(http_get("/p/" + generate_panel_token() + "/").status, 404);
  EXPECT_EQ(http_get("/p/").status, 404);
  EXPECT_EQ(http_get("/p/short/").status, 404);
  std::string wrong_case = session->token();
  wrong_case[0] = wrong_case[0] == 'a' ? 'b' : 'a';
  EXPECT_EQ(http_get("/p/" + wrong_case + "/").status, 404);
  EXPECT_EQ(http_get("/").status, 200);
  EXPECT_EQ(http_get("/favicon.ico").status, 404);
  const RawReply socket_route = http_get("/ws/" + session->token());
  EXPECT_EQ(socket_route.status, 426);
  EXPECT_EQ(socket_route.header("Upgrade"), "websocket");
  EXPECT_EQ(server->diagnostics().token_misses, 2u);
}

TEST_F(WebPanelServerTest, ServedPageIsWellFormedAndAgreesWithTheServer)
{
  start();
  const RawReply page = http_get(page_path());
  ASSERT_EQ(page.status, 200);
  const std::string & html = page.body;

  const UiValue config = parse_json(extract_json_block(html, "qar-config"));
  ASSERT_TRUE(config.is_object());
  EXPECT_EQ(config.find("protocol_version")->as_int(), qar_ui_model::kProtocolVersion);
  EXPECT_EQ(config.find("runtime")->as_string(), qar_ui_model::kRuntimeName);
  EXPECT_EQ(config.find("panel_id")->as_string(), "robot");
  // The Host the client used, reflected: the socket reaches the same server.
  EXPECT_EQ(config.find("websocket_url")->as_string(), ws_url());

  UiValue expected_model = qar_ui_model::to_value(demo_model());
  expected_model.erase("data");
  EXPECT_EQ(parse_json(extract_json_block(html, "qar-model")), expected_model);
  EXPECT_EQ(parse_json(extract_json_block(html, "qar-data")), demo_model().initial_data);

  // The page runtime's public contract, present in the served bytes.
  EXPECT_NE(html.find("window.QarPanel"), std::string::npos);
  EXPECT_NE(html.find("data-qar-id=\"speed\""), std::string::npos);
  EXPECT_NE(html.find("data-qar-bind=\"battery.percentage\""), std::string::npos);
  EXPECT_NE(html.find("Content-Security-Policy"), std::string::npos);

  // And the URL the page would dial is one this server answers.
  WsClient client(config.find("websocket_url")->as_string());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());
  EXPECT_EQ(client.frame(0).type, MessageType::Hello);
}

TEST_F(WebPanelServerTest, HelloHandshakeCarriesTheFullDocument)
{
  start();
  WsClient client(ws_url());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());
  const ProtocolMessage hello = client.frame(0);
  ASSERT_EQ(hello.type, MessageType::Hello);
  EXPECT_EQ(hello.seq, 1u);
  EXPECT_TRUE(hello.timestamp.has_value());
  const auto * body = hello.as<qar_ui_model::HelloBody>();
  ASSERT_NE(body, nullptr);
  EXPECT_EQ(body->panel, "robot");
  EXPECT_EQ(body->data, demo_model().initial_data);
  EXPECT_EQ(session->client_count(), 1u);
}

TEST_F(WebPanelServerTest, PublishReachesEveryViewerInOrderWithPerConnectionSeq)
{
  start();
  QSignalSpy count_spy(session.get(), &PanelSession::client_count_changed);
  WsClient a(ws_url());
  WsClient b(ws_url());
  ASSERT_TRUE(a.wait_connected());
  ASSERT_TRUE(b.wait_connected());
  ASSERT_TRUE(a.hello());
  ASSERT_TRUE(b.hello());
  EXPECT_EQ(count_spy.count(), 2);

  std::string error;
  ASSERT_TRUE(session->set_value("speed", 0.7, &error)) << error;
  std::vector<qar_ui_model::PatchEntry> patch;
  patch.push_back({"battery.percentage", 0.5});
  patch.push_back({"battery.present", true});
  ASSERT_TRUE(session->publish(patch, &error)) << error;
  EXPECT_EQ(session->document().find("speed")->as_double(), 0.7);
  EXPECT_EQ(session->document().find("battery")->find("present")->as_bool(), true);

  ASSERT_TRUE(a.wait_frames(3));
  ASSERT_TRUE(b.wait_frames(3));
  for (WsClient * c : {&a, &b}) {
    const ProtocolMessage first = c->frame(1);
    const ProtocolMessage second = c->frame(2);
    EXPECT_EQ(first.type, MessageType::Data);
    EXPECT_EQ(first.seq, 2u);
    EXPECT_EQ(second.seq, 3u);
    ASSERT_EQ(first.as<qar_ui_model::DataBody>()->patch.size(), 1u);
    EXPECT_EQ(first.as<qar_ui_model::DataBody>()->patch[0].path, "speed");
    EXPECT_EQ(first.as<qar_ui_model::DataBody>()->patch[0].value, UiValue(0.7));
    ASSERT_EQ(second.as<qar_ui_model::DataBody>()->patch.size(), 2u);
    EXPECT_EQ(second.as<qar_ui_model::DataBody>()->patch[1].path, "battery.present");
  }

  // A viewer arriving later starts from the document the others reached.
  WsClient late(ws_url());
  ASSERT_TRUE(late.wait_connected());
  ASSERT_TRUE(late.hello());
  EXPECT_EQ(late.frame(0).as<qar_ui_model::HelloBody>()->data, session->document());

  // A patch that cannot apply changes nothing and sends nothing.
  patch.clear();
  patch.push_back({"speed.x", 1});
  EXPECT_FALSE(session->publish(patch, &error));
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(session->document().find("speed")->as_double(), 0.7);
  EXPECT_FALSE(a.wait_frames(4, 150));
  EXPECT_EQ(session->diagnostics().publishes, 2u);
  EXPECT_EQ(session->diagnostics().publishes_refused, 1u);
  EXPECT_EQ(session->diagnostics().clients, 3u);
}

TEST_F(WebPanelServerTest, ControlEventsReachTheSinkAndTheSignalAndCanBeAcked)
{
  start();
  RecordingSink sink;
  session->set_control_sink(&sink);
  QSignalSpy spy(session.get(), &PanelSession::control_received);
  WsClient client(ws_url());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());

  client.send(qar_ui_model::make_control(2, "speed", "publish:/cmd_vel", "speed", 0.25));
  ASSERT_TRUE(wait_until([&]() {return sink.events.size() == 1;}));
  EXPECT_EQ(spy.count(), 1);
  const PanelControlEvent & event = sink.events[0];
  EXPECT_EQ(event.seq, 2u);
  EXPECT_EQ(event.control.control, "speed");
  EXPECT_EQ(event.control.action, "publish:/cmd_vel");
  EXPECT_EQ(event.control.path, "speed");
  EXPECT_EQ(event.control.value, UiValue(0.25));
  // The server's document is NOT changed by a control: the host confirms it.
  EXPECT_EQ(session->document().find("speed")->as_double(), 0.5);

  ASSERT_TRUE(session->ack(event, false, "service /go unavailable"));
  ASSERT_TRUE(client.wait_frames(2));
  const ProtocolMessage ack = client.frame(1);
  ASSERT_EQ(ack.type, MessageType::Ack);
  EXPECT_EQ(ack.seq, 2u);
  EXPECT_EQ(ack.as<qar_ui_model::AckBody>()->event_seq, 2u);
  EXPECT_FALSE(ack.as<qar_ui_model::AckBody>()->ok);
  EXPECT_EQ(ack.as<qar_ui_model::AckBody>()->text, "service /go unavailable");

  session->send_status(qar_ui_model::StatusLevel::Warning, "no publisher on /odom");
  ASSERT_TRUE(client.wait_frames(3));
  EXPECT_EQ(client.frame(2).type, MessageType::Status);
  EXPECT_EQ(client.frame(2).as<qar_ui_model::StatusBody>()->level, qar_ui_model::StatusLevel::Warning);

  EXPECT_FALSE(session->ack(event.client_id + 99, 2, true)) << "unknown viewer";
  EXPECT_EQ(session->diagnostics().controls, 1u);
}

TEST_F(WebPanelServerTest, PingIsAnsweredWithPongAndUnknownFramesAreIgnored)
{
  start();
  WsClient client(ws_url());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());

  client.send_raw("{\"v\":1,\"type\":\"bogus\",\"seq\":2,\"whatever\":true}");
  client.send_raw("not json at all");
  client.send_raw("{\"v\":7,\"type\":\"control\",\"seq\":3,\"control\":\"speed\"}");
  client.send_raw("{\"v\":1,\"type\":\"control\",\"seq\":1,\"control\":\"speed\"}");  // replayed seq
  client.send(qar_ui_model::make_ping(4));
  ASSERT_TRUE(client.wait_frames(2));
  const ProtocolMessage pong = client.frame(1);
  ASSERT_EQ(pong.type, MessageType::Pong);
  EXPECT_EQ(pong.as<qar_ui_model::PongBody>()->ping_seq, 4u);
  EXPECT_EQ(client.disconnected_spy.count(), 0) << "nothing above is fatal after hello";
  EXPECT_EQ(session->diagnostics().frames_ignored, 4u);
  EXPECT_EQ(session->diagnostics().controls, 0u);
}

TEST_F(WebPanelServerTest, RefusesForeignVersionWrongPanelAndNonHelloFirstFrames)
{
  start();
  {
    WsClient client(ws_url());
    ASSERT_TRUE(client.wait_connected());
    client.send_raw("{\"v\":2,\"type\":\"hello\",\"seq\":1,\"panel\":\"robot\",\"runtime\":\"x\"}");
    ASSERT_TRUE(client.wait_closed());
    EXPECT_EQ(client.socket.closeCode(), QWebSocketProtocol::CloseCodeProtocolError);
  }
  {
    WsClient client(ws_url());
    ASSERT_TRUE(client.wait_connected());
    client.send(qar_ui_model::make_hello(1, "other", qar_ui_model::kRuntimeName));
    ASSERT_TRUE(client.wait_closed());
    EXPECT_EQ(client.socket.closeCode(), QWebSocketProtocol::CloseCodePolicyViolated);
  }
  {
    WsClient client(ws_url());
    ASSERT_TRUE(client.wait_connected());
    client.send(qar_ui_model::make_control(1, "speed", "", "speed", 1));
    ASSERT_TRUE(client.wait_closed());
    EXPECT_EQ(client.socket.closeCode(), QWebSocketProtocol::CloseCodeProtocolError);
  }
  {
    WsClient client(ws_url());
    ASSERT_TRUE(client.wait_connected());
    ASSERT_TRUE(client.hello());
    client.socket.sendBinaryMessage(QByteArray("\x01\x02"));
    ASSERT_TRUE(client.wait_closed());
    EXPECT_EQ(client.socket.closeCode(), QWebSocketProtocol::CloseCodeDatatypeNotSupported);
  }
  EXPECT_EQ(session->diagnostics().closed_by_policy, 4u);
  EXPECT_TRUE(wait_until([this]() {return session->client_count() == 0;}));
}

TEST_F(WebPanelServerTest, SocketWithoutAValidTokenNeverUpgrades)
{
  start();
  WsClient wrong(ws_url(generate_panel_token()));
  EXPECT_FALSE(wrong.wait_connected());
  WsClient short_token("ws://127.0.0.1:" + std::to_string(server->port()) + "/ws/abc");
  EXPECT_FALSE(short_token.wait_connected());
  WsClient trailing(ws_url() + "/extra");
  EXPECT_FALSE(trailing.wait_connected());
  EXPECT_EQ(session->client_count(), 0u);
  EXPECT_EQ(server->diagnostics().upgrades_refused, 3u);
  EXPECT_EQ(server->diagnostics().http.upgrades, 0u);
}

TEST_F(WebPanelServerTest, CrossOriginHandshakesAreRefusedUnlessAllowed)
{
  start();
  QNetworkRequest foreign(QUrl(QString::fromStdString(ws_url())));
  foreign.setRawHeader("Origin", "http://evil.example");
  WsClient refused(foreign);
  EXPECT_FALSE(refused.wait_connected());

  QNetworkRequest own(QUrl(QString::fromStdString(ws_url())));
  own.setRawHeader("Origin", QByteArray::fromStdString("http://127.0.0.1:" + std::to_string(server->port())));
  WsClient accepted(own);
  EXPECT_TRUE(accepted.wait_connected());
  EXPECT_EQ(server->diagnostics().upgrades_refused, 1u);

  WebPanelServerConfig permissive;
  permissive.allow_any_origin = true;
  start(permissive);
  QNetworkRequest again(QUrl(QString::fromStdString(ws_url())));
  again.setRawHeader("Origin", "http://evil.example");
  WsClient allowed(again);
  EXPECT_TRUE(allowed.wait_connected());
}

TEST_F(WebPanelServerTest, ResourcesAreServedUnderThePanelToken)
{
  start();
  session->set_resource("logo.svg", "image/svg+xml", "<svg/>");
  const RawReply asset = http_get(page_path() + "logo.svg");
  EXPECT_EQ(asset.status, 200);
  EXPECT_EQ(asset.header("Content-Type"), "image/svg+xml");
  EXPECT_EQ(asset.body, "<svg/>");
  EXPECT_EQ(http_get(page_path() + "missing.png").status, 404);
  EXPECT_EQ(http_get("/p/" + generate_panel_token() + "/logo.svg").status, 404);
  EXPECT_TRUE(session->remove_resource("logo.svg"));
  EXPECT_FALSE(session->remove_resource("logo.svg"));
  EXPECT_EQ(http_get(page_path() + "logo.svg").status, 404);
}

TEST_F(WebPanelServerTest, RemovingAPanelClosesItsViewersAndForgetsTheToken)
{
  start();
  QSignalSpy removed(server.get(), &WebPanelServer::panel_removed);
  WsClient client(ws_url());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());
  const std::string token = session->token();
  EXPECT_TRUE(server->remove_panel(token));
  EXPECT_FALSE(server->remove_panel(token));
  EXPECT_EQ(removed.count(), 1);
  ASSERT_TRUE(client.wait_closed());
  EXPECT_EQ(client.socket.closeCode(), QWebSocketProtocol::CloseCodeGoingAway);
  EXPECT_EQ(http_get("/p/" + token + "/").status, 404);
  EXPECT_EQ(server->panels().size(), 0u);
}

TEST_F(WebPanelServerTest, StopClosesViewersAndSessionsSurviveARestart)
{
  start();
  QSignalSpy stopped(server.get(), &WebPanelServer::stopped);
  WsClient client(ws_url());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());
  server->stop();
  EXPECT_EQ(stopped.count(), 1);
  EXPECT_FALSE(server->is_running());
  ASSERT_TRUE(client.wait_closed());
  EXPECT_EQ(client.socket.closeCode(), QWebSocketProtocol::CloseCodeGoingAway);

  std::string error;
  ASSERT_TRUE(server->start(&error)) << error;
  EXPECT_EQ(http_get(page_path()).status, 200);
  WsClient again(ws_url());
  ASSERT_TRUE(again.wait_connected());
  ASSERT_TRUE(again.hello());
}

TEST_F(WebPanelServerTest, ViewerCapIsEnforcedBeforeTheUpgrade)
{
  WebPanelServerConfig config;
  config.session.max_clients = 1;
  start(config);
  WsClient first(ws_url());
  ASSERT_TRUE(first.wait_connected());
  WsClient second(ws_url());
  EXPECT_FALSE(second.wait_connected());
  EXPECT_EQ(session->client_count(), 1u);
}

TEST_F(WebPanelServerTest, SessionRefusesCallsFromAnotherThread)
{
  start();
  bool result = true;
  std::string error;
  std::thread other([&]() {result = session->set_value("speed", 1.0, &error);});
  other.join();
  EXPECT_FALSE(result);
  EXPECT_NE(error.find("thread"), std::string::npos);
  EXPECT_EQ(session->document().find("speed")->as_double(), 0.5);
}

TEST_F(WebPanelServerTest, RejectsAModelThatDoesNotRender)
{
  start();
  UiModel broken;
  broken.root = widgets::label("l", "not a panel");
  std::string error;
  EXPECT_EQ(server->add_panel(broken, &error), nullptr);
  EXPECT_FALSE(error.empty());
  EXPECT_EQ(server->panels().size(), 1u);
}

TEST(WebPanelServerHosts, DetectsSomethingReachable)
{
  const std::string host = WebPanelServer::detect_advertised_host();
  EXPECT_FALSE(host.empty());
  EXPECT_FALSE(QHostAddress(QString::fromStdString(host)).isNull()) << host;
  for (const std::string & candidate : WebPanelServer::candidate_hosts()) {
    const QHostAddress address(QString::fromStdString(candidate));
    EXPECT_FALSE(address.isNull()) << candidate;
    EXPECT_FALSE(address.isLoopback()) << candidate;
  }
}

TEST(WebPanelServerBind, AdvertisesTheConfiguredHostAndReportsListenFailures)
{
  WebPanelServerConfig config;
  config.bind_address = QHostAddress::LocalHost;
  config.advertised_host = "robot-pc.local";
  WebPanelServer server(config);
  std::string error;
  ASSERT_TRUE(server.start(&error)) << error;
  EXPECT_EQ(server.advertised_host(), "robot-pc.local");
  std::shared_ptr<PanelSession> session = server.add_panel(demo_model(), &error);
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(
    server.page_url(*session),
    "http://robot-pc.local:" + std::to_string(server.port()) + "/p/" + session->token() + "/");
  EXPECT_TRUE(server.start(&error)) << "start is idempotent";

  WebPanelServerConfig clash;
  clash.bind_address = QHostAddress::LocalHost;
  clash.port = server.port();
  WebPanelServer second(clash);
  EXPECT_FALSE(second.start(&error));
  EXPECT_NE(error.find("cannot listen"), std::string::npos);
}

TEST_F(WebPanelServerTest, APageReportsTheViewportItWasActuallyGiven)
{
  start();
  QSignalSpy viewport_spy(session.get(), &PanelSession::viewport_reported);
  WsClient client(ws_url());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());
  EXPECT_FALSE(session->last_viewport().has_value()) << "nothing is assumed before a page speaks";

  qar_ui_model::ViewportBody body;
  body.css_width = 1024.0;
  body.css_height = 640.0;
  body.device_pixel_ratio = 1.5;
  body.content_width = 1024.0;
  body.content_height = 1580.0;
  body.root_width = 1024.0;
  body.root_height = 1580.0;
  body.reason = "connect";
  client.send(qar_ui_model::make_viewport(2, body));

  ASSERT_TRUE(wait_until([&viewport_spy]() {return viewport_spy.count() == 1;}));
  ASSERT_TRUE(session->last_viewport().has_value());
  const qar_web_panels::PanelViewportReport & report = *session->last_viewport();
  EXPECT_DOUBLE_EQ(report.viewport.css_width, 1024.0);
  EXPECT_DOUBLE_EQ(report.viewport.css_height, 640.0);
  EXPECT_DOUBLE_EQ(report.viewport.content_height, 1580.0);
  EXPECT_NEAR(report.aspect(), 1024.0 / 640.0, 1e-9);
  EXPECT_NEAR(report.visible_fraction(), 640.0 / 1580.0, 1e-9);
  EXPECT_NE(report.client_id, 0u);
  EXPECT_EQ(session->diagnostics().viewports, 1u);
  EXPECT_EQ(report.describe(),
    "viewport 1024x640 css, dpr 1.50, content 1024x1580, root 1024x1580 (connect)");

  // A resize replaces it; the newest report is the one a panel is sized from.
  body.css_width = 720.0;
  body.css_height = 720.0;
  body.reason = "resize";
  client.send(qar_ui_model::make_viewport(3, body));
  ASSERT_TRUE(wait_until([&viewport_spy]() {return viewport_spy.count() == 2;}));
  EXPECT_DOUBLE_EQ(session->last_viewport()->viewport.css_width, 720.0);
  EXPECT_EQ(session->diagnostics().viewports, 2u);
  EXPECT_EQ(session->client_count(), 1u) << "the socket is still up";
}

TEST_F(WebPanelServerTest, AnOlderPageThatNeverReportsIsNotPenalised)
{
  // Adding a frame type is not a version change: a page built before `viewport`
  // existed simply never sends one, and a frame type this server has never
  // heard of is ignored rather than fatal. Both directions, one test.
  start();
  WsClient client(ws_url());
  ASSERT_TRUE(client.wait_connected());
  ASSERT_TRUE(client.hello());

  client.send_raw("{\"v\":1,\"type\":\"viewport2\",\"seq\":2,\"css_w\":800,\"css_h\":600}");
  ASSERT_TRUE(wait_until([this]() {return session->diagnostics().frames_ignored >= 1;}));
  EXPECT_FALSE(session->last_viewport().has_value());
  EXPECT_EQ(session->diagnostics().viewports, 0u);

  // Still a live, usable session: the unknown frame cost nothing.
  std::string error;
  ASSERT_TRUE(session->set_value("speed", 0.25, &error)) << error;
  ASSERT_TRUE(client.wait_frames(2));
  EXPECT_EQ(client.frame(1).type, MessageType::Data);
  EXPECT_EQ(session->client_count(), 1u);
  EXPECT_EQ(session->diagnostics().closed_by_policy, 0u);
}
