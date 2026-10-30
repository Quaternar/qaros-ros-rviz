// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Exposes one ROS 2 topic as an AR panel: a label shows the newest message of /qar_example/text and a button
// publishes on /qar_example/pressed. qar_ros_ui reads and writes the messages without a generated header,
// qar_web_panels serves the page, and QAROS opens the page as a URL panel for the targets of the Hub. The program
// joins the QAROS Hub first. On the PC that runs the Hub, QAROS asks once to allow the program (and again after each
// rebuild of it); a Hub on another PC is joined with a pairing code.

#include <QCoreApplication>
#include <QSysInfo>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "rclcpp/rclcpp.hpp"

// qar_ui_model first: the QarOS header includes <windows.h>, whose macro `interface` breaks type_description.hpp.
#include "qar_ui_model/ui_model.hpp"
#include "qar_ros_ui/topic_bridge.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"
#include "qar_web_panels/web_panel_server.hpp"

#include "qar_ar_core/common_name.hpp"
#include "qar_ar_core/qar_streaming_c.hpp"
#include "qar_qar_adapter/qar_capi_url_panel_adapter.hpp"
#include "qar_qar_adapter/qar_session_owner.hpp"

namespace ui = qar_ui_model;
namespace wp = qar_web_panels;
namespace qqa = qar_qar_adapter;

// [qar-snippet:model]
// The label is bound to the field "data" of the message, which the example keeps under the path "message" of
// the data document. The button carries the action id that the sink below answers.
ui::UiModel make_model()
{
  ui::UiModel model;
  model.root = ui::widgets::panel("topic_panel", "ROS topic");
  model.root.add(ui::widgets::value("received", "message.data", "Last message on /qar_example/text"));
  model.root.add(ui::widgets::button("publish", "Publish", "publish"));
  model.initial_data = ui::UiValue::object().set("message", ui::UiValue::object().set("data", "(none yet)"));
  return model;
}
// [qar-snippet-end:model]

// [qar-snippet:sink]
// A press of the button becomes one message on the topic; the publisher converts the value to a String.
struct PublishSink : wp::IPanelControlSink
{
  void on_control(wp::PanelSession & session, const wp::PanelControlEvent & event) override
  {
    if (event.control.action != "publish") { session.ack(event, false, "unknown action"); return; }
    const std::string text = "pressed " + std::to_string(++presses);
    const qar_ros_ui::Result sent = publisher->publish(ui::UiValue::object().set("data", text));
    session.ack(event, sent.ok(), sent.message);
    std::printf("example: tx /qar_example/pressed: %s (%s)\n", text.c_str(), sent.ok() ? "ok" : sent.message.c_str());
  }
  std::shared_ptr<qar_ros_ui::TopicPublisher> publisher;
  int presses = 0;
};
// [qar-snippet-end:sink]

namespace
{

constexpr const char * kProgramTag = "ros-topic-panel-example";

void print_usage()
{
  std::printf(
    "Usage: qar_ros_topic_panel_example [--local | --hub HOST:PORT [--code CODE] | --forget | --no-qaros]\n"
    "                                   [--connect-only] [--serve-seconds N] [--loopback]\n"
    "  --local            join the QAROS Hub of this PC (the default). QAROS asks once to allow\n"
    "                     this program, and again after each rebuild of it: click Allow in the\n"
    "                     QAROS window\n"
    "  --hub H:P          join the Hub of another PC at host H and onboarding port P (the port is\n"
    "                     shown with the pairing code in the Visualizer of that PC)\n"
    "  --code CODE        the pairing code that Hub shows; it works once and lives about 10 seconds.\n"
    "                     Only the first join needs it: the pairing is stored for the next start\n"
    "  --forget           erase the stored pairing with another PC's Hub and exit\n"
    "  --no-qaros         do not join QAROS: only serve the page, for a desktop browser\n"
    "  --connect-only     join QAROS, print the runtime, the mode, this program's peer and the\n"
    "                     targets, and exit; no node, no server, no panel\n"
    "  --serve-seconds N  run the node and serve the panel for N seconds, then exit (default 60)\n"
    "  --loopback         serve the page on 127.0.0.1 only, so that no other machine can open it (default: all interfaces)\n");
}

// "HOST:PORT" with a port from 1 to 65535.
bool parse_hub(const std::string & text, qqa::HubEndpoint & hub)
{
  const std::size_t colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) { return false; }
  const long port = std::strtol(text.c_str() + colon + 1, nullptr, 10);
  if (port < 1 || port > 65535) { return false; }
  hub.host = text.substr(0, colon);
  hub.port = static_cast<std::uint16_t>(port);
  return true;
}

// Prints what the Hub-facing events say, the plain-words status first.
void print_events(const std::vector<qqa::SessionEvent> & events)
{
  for (const qqa::SessionEvent & event : events) {
    if (event.kind == qqa::SessionEvent::Kind::kState && !event.status.message.empty()) {
      std::printf("example: QAROS: %s\n", event.status.message.c_str());
    } else if (event.kind == qqa::SessionEvent::Kind::kProgress && !event.text.empty()) {
      std::printf("example: QAROS: %s\n", event.text.c_str());
    }
  }
}

// The panel calls are only queued; a call that QAROS refused is known when its outcome is
// drained. One line for each.
void print_panel_failures(qar_ar_core::IQarUrlPanelAdapter & panel)
{
  std::vector<qar_ar_core::PanelOpResult> results;
  panel.drain_results(results);
  for (const qar_ar_core::PanelOpResult & result : results) {
    if (!result.ok) {
      std::printf(
        "example: a panel call failed: %s\n",
        result.error.empty() ? "no reason given" : result.error.c_str());
    }
  }
}

std::string peer_text(const QarPeerId & id)
{
  char buffer[16] = {0};
  std::snprintf(
    buffer, sizeof(buffer), "%02x%02x%02x%02x", id.data[0], id.data[1], id.data[2], id.data[3]);
  return buffer;
}

}  // namespace

int main(int argc, char ** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  bool no_qaros = false;
  bool connect_only = false;
  bool forget = false;
  bool local = false;
  bool loopback = false;
  std::string hub_text;
  std::string code;
  int seconds = 60;
  const bool help = argc == 2 && std::string(argv[1]) == "--help";
  bool bad_usage = false;
  for (int i = 1; i < argc && !help && !bad_usage; ++i) {
    const std::string arg = argv[i];
    if (arg == "--loopback") { loopback = true; }
    else if (arg == "--no-qaros") { no_qaros = true; }
    else if (arg == "--connect-only") { connect_only = true; }
    else if (arg == "--local") { local = true; }
    else if (arg == "--forget") { forget = true; }
    else if (arg == "--hub" && i + 1 < argc) { hub_text = argv[++i]; }
    else if (arg == "--code" && i + 1 < argc) { code = argv[++i]; }
    else if (arg == "--serve-seconds" && i + 1 < argc) { seconds = std::atoi(argv[++i]); }
    else { bad_usage = true; }
  }
  qqa::HubEndpoint hub;
  if (!help && !bad_usage && !hub_text.empty() && !parse_hub(hub_text, hub)) {
    std::printf("example: --hub needs HOST:PORT with a port from 1 to 65535\n");
    bad_usage = true;
  }
  const bool conflicting = (local && (!hub_text.empty() || forget)) ||
    (no_qaros && (local || connect_only || forget || !hub_text.empty() || !code.empty()));
  if (!help && !bad_usage && (conflicting || (!code.empty() && hub_text.empty()) || seconds < 1)) {
    bad_usage = true;
  }
  if (help || bad_usage) {
    print_usage();
    return help ? 0 : 2;
  }
  QCoreApplication app(argc, argv);
  const std::string host = QSysInfo::machineHostName().toStdString();
  std::shared_ptr<qqa::QarSessionOwner> owner;  // stays empty with --no-qaros: no QAROS is needed
  if (!no_qaros) {
    qqa::SessionOwnerConfig owner_config;
    owner_config.program_tag = kProgramTag;
    owner_config.display_name = std::string("ROS topic panel example on ") + host;
    owner_config.mode =
      (!hub_text.empty() || forget) ? qqa::HubMode::kRemote : qqa::HubMode::kLocal;
    owner_config.remote = hub;
    owner_config.log_severity = QAR_LOG_SEVERITY_WARN;
    owner_config.enable_console_logging = false;
    owner = std::make_shared<qqa::QarSessionOwner>(owner_config);  // starts its own thread
    if (forget) {
      owner->forget();
      for (int i = 0; i < 750; ++i) {  // 15 s
        std::vector<qqa::SessionEvent> events;
        owner->poll(events);
        for (const qqa::SessionEvent & event : events) {
          if (event.kind == qqa::SessionEvent::Kind::kForgetDone) {
            std::printf("example: %s\n", event.status.message.c_str());
            return 0;
          }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      std::printf("example: no answer from QAROS to --forget\n");
      return 1;
    }
    if (owner_config.mode == qqa::HubMode::kLocal) {
      std::printf("example: joining the QAROS Hub of this PC; click Allow if QAROS asks\n");
    } else {
      std::printf("example: joining the QAROS Hub at %s:%u\n", hub.host.c_str(),
        static_cast<unsigned>(hub.port));
    }
    owner->connect();
    std::vector<qqa::SessionEvent> seen;
    // The approval prompt waits 120 s by default; the wait here is a little longer.
    qqa::SessionState state = qqa::wait_for_session(*owner, std::chrono::seconds(150), &seen);
    if (state == qqa::SessionState::kNeedsCode && !code.empty()) {
      owner->submit_code(code, hub);
      code.assign(code.size(), '\0');  // the code works once; do not keep it around
      state = qqa::wait_for_session(*owner, std::chrono::seconds(60), &seen);
    }
    print_events(seen);
    if (state == qqa::SessionState::kNeedsCode) {
      std::printf("example: not paired with that Hub; run with --hub HOST:PORT --code CODE\n");
      return 1;
    }
    if (state != qqa::SessionState::kRunning) {
      std::printf("example: QAROS connect failed (state %d)\n", static_cast<int>(state));
      return 1;
    }
    std::printf("example: connected to QarOS: ready=1\n");
    if (connect_only) {
      // The peer list arrives right after the join; give it a moment before it is printed.
      std::vector<qqa::SessionEvent> late;
      for (int i = 0; i < 75; ++i) {  // 1.5 s
        owner->poll(late);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      std::printf("example: runtime: %s\n", owner->runtime_description().c_str());
      const bool is_local = owner->effective_mode() == qqa::HubMode::kLocal;
      std::printf("example: mode: %s\n", is_local ? "local" : "remote");
      std::printf("example: this program is peer %s\n", peer_text(owner->self_peer()).c_str());
      const std::vector<qqa::TargetInfo> targets = owner->targets();
      std::printf("example: %u target(s)\n", static_cast<unsigned>(targets.size()));
      for (const qqa::TargetInfo & target : targets) {
        std::printf("example:   target %s '%s'\n", peer_text(target.id).c_str(),
          target.device_name.empty() ? target.display_name.c_str() : target.device_name.c_str());
      }
      return 0;
    }
  }
  // [qar-snippet:ros]
  rclcpp::init(0, nullptr);
  const auto node = std::make_shared<rclcpp::Node>("qar_ros_topic_panel_example");
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  qar_ros_ui::TopicBridge bridge(qar_ros_ui::NodeHandles::from(node));
  std::shared_ptr<qar_ros_ui::TopicSubscription> subscription;
  PublishSink sink;
  const qar_ros_ui::Result subscribed = bridge.subscribe("/qar_example/text", "std_msgs/msg/String", {}, subscription);
  const qar_ros_ui::Result advertised = bridge.advertise("/qar_example/pressed", "std_msgs/msg/String", {}, sink.publisher);
  if (!subscribed.ok() || !advertised.ok()) {
    std::printf("example: ROS entities failed: %s %s\n", subscribed.message.c_str(), advertised.message.c_str());
    return 1;
  }
  // [qar-snippet-end:ros]
  wp::WebPanelServerConfig server_config;
  if (loopback) { server_config.bind_address = QHostAddress::LocalHost; }
  wp::WebPanelServer server(server_config);
  std::string error;
  if (!server.start(&error)) { std::printf("example: cannot start the panel server: %s\n", error.c_str()); return 1; }
  const std::shared_ptr<wp::PanelSession> page = server.add_panel(make_model(), &error);
  if (!page) { std::printf("example: the panel server refused the model: %s\n", error.c_str()); return 1; }
  page->set_control_sink(&sink);
  std::printf("example: panel page: %s\n", server.page_url(*page).c_str());
  std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> ar_panel;
  std::unique_ptr<qqa::QarCApiUrlPanelFactory> factory;
  if (owner) {
    qar_ar_core::UrlPanelDesc desc{server.page_url(*page), "ROS topic", {}};
    desc.placement.pose.position = QarVector3{0.0f, 1.5f, -1.2f};  // the top centre of the panel, in the room
    // The size comes from a fixed design viewport, never from the viewport QAROS gives the page:
    // that one follows the panel's own size, so a size derived from it never settles.
    wp::UrlPanelSizing sizing;
    wp::use_design_viewport(sizing);
    desc.placement.size =
      wp::size_url_panel_for(page->model(), server.config().render.theme, sizing).size;
    // The same name finds the same QAROS panel again at the next start.
    desc.common_name = qar_ar_core::make_panel_common_name(
      "topic", qar_ar_core::make_name_scope(kProgramTag, "", host));
    factory = std::make_unique<qqa::QarCApiUrlPanelFactory>(owner.get());
    ar_panel = factory->create_url_panel(desc);
    if (!ar_panel) { std::printf("example: could not open the URL panel: %s\n", factory->last_error().c_str()); return 1; }
  }
  // [qar-snippet:tick]
  int ticks = 0;
  QTimer timer;  // every 10 ms: run the ROS callbacks, then show the newest message
  QObject::connect(&timer, &QTimer::timeout, [&] {
    executor.spin_some();
    if (!rclcpp::ok()) { app.quit(); }  // Ctrl+C has shut ROS 2 down: the loop ends too
    if (++ticks % 100 == 0) { bridge.reconcile(); }  // follow the QoS of publishers that come and go
    if (owner && ticks % 5 == 0) {
      std::vector<qqa::SessionEvent> events;  // the owner's news: a lost Hub, a target that came or went
      owner->poll(events);
      print_events(events);
    }
    ui::UiValue message;
    if (subscription->take_latest(message)) {  // true once for each newest message; older ones are skipped
      page->set_value("message", message);
      const ui::UiValue * data = message.find("data");
      std::printf("example: rx /qar_example/text: %s\n", data != nullptr ? data->as_string().c_str() : "");
    }
  });
  timer.start(10);
  // [qar-snippet-end:tick]
  QTimer panel_results;  // the outcomes of the panel calls that were only queued
  QObject::connect(&panel_results, &QTimer::timeout, [&] {
    if (ar_panel) { print_panel_failures(*ar_panel); }
  });
  panel_results.start(200);
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&deadline, &QTimer::timeout, &app, &QCoreApplication::quit);
  deadline.start(seconds * 1000);
  app.exec();
  if (ar_panel) { print_panel_failures(*ar_panel); }
  // [qar-snippet:shutdown]
  if (ar_panel) {
    ar_panel->set_state(QAR_GUI_PANEL_STATE_HIDDEN);  // hidden, never closed: the next start finds it by name
    ar_panel->detach();
    ar_panel.reset();
  }
  // [qar-snippet-end:shutdown]
  rclcpp::shutdown();
  std::printf("example: done\n");
  return 0;
}
