// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Serves one AR panel that is built by hand from a UI model: a counter and a button. WebPanelServer serves the
// page; a desktop browser can open it, and QAROS opens it as a URL panel for the targets of the Hub. The program
// joins the QAROS Hub first. On the PC that runs the Hub, QAROS asks once to allow the program (and again after each
// rebuild of it); a Hub on another PC is joined with a pairing code.

// [qar-snippet:includes]
#include <QCoreApplication>
#include <QSysInfo>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// qar_ui_model first: the QarOS header includes <windows.h>, whose macro `interface` breaks type_description.hpp.
#include "qar_ui_model/ui_model.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"
#include "qar_web_panels/web_panel_server.hpp"

#include "qar_ar_core/common_name.hpp"
#include "qar_ar_core/qar_streaming_c.hpp"
#include "qar_qar_adapter/qar_capi_url_panel_adapter.hpp"
#include "qar_qar_adapter/qar_session_owner.hpp"
// [qar-snippet-end:includes]

namespace ui = qar_ui_model;
namespace wp = qar_web_panels;
namespace qqa = qar_qar_adapter;

// [qar-snippet:model]
// A widget tree and the data it starts with. The value display is bound to the path "count" of the data
// document; the button carries an action id, which the page sends back when the button is pressed.
ui::UiModel make_model()
{
  ui::UiModel model;
  model.root = ui::widgets::panel("counter_panel", "Counter");
  model.root.add(ui::widgets::value("count", "count", "Presses").with_precision(0));  // no decimals
  model.root.add(ui::widgets::button("increment", "Increment", "increment"));
  model.initial_data = ui::UiValue::object().set("count", 0);
  return model;
}
// [qar-snippet-end:model]

// [qar-snippet:sink]
// Receives the control events of every viewer, on the thread that owns the session.
struct CounterSink : wp::IPanelControlSink
{
  void on_control(wp::PanelSession & session, const wp::PanelControlEvent & event) override
  {
    if (event.control.action != "increment") { session.ack(event, false, "unknown action"); return; }
    session.set_value("count", ++count);  // the document of the server first, then every viewer
    session.ack(event, true);
    std::printf("example: increment: count %lld\n", static_cast<long long>(count));
  }
  std::int64_t count = 0;
};
// [qar-snippet-end:sink]

namespace
{

constexpr const char * kProgramTag = "web-panel-example";

void print_usage()
{
  std::printf(
    "Usage: qar_web_panel_example [--local | --hub HOST:PORT [--code CODE] | --forget | --no-qaros]\n"
    "                             [--connect-only] [--serve-seconds N] [--loopback] [--port N]\n"
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
    "                     targets, and exit; no server, no panel\n"
    "  --serve-seconds N  serve the panel for N seconds, then exit (default 60)\n"
    "  --loopback         listen on 127.0.0.1 only, so that no other machine can connect (default: all interfaces)\n"
    "  --port N           TCP port of the panel server (default 0: the operating system picks a free port)\n");
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
  int port = 0;
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
    else if (arg == "--port" && i + 1 < argc) { port = std::atoi(argv[++i]); }
    else { bad_usage = true; }
  }
  qqa::HubEndpoint hub;
  if (!help && !bad_usage && !hub_text.empty() && !parse_hub(hub_text, hub)) {
    std::printf("example: --hub needs HOST:PORT with a port from 1 to 65535\n");
    bad_usage = true;
  }
  const bool conflicting = (local && (!hub_text.empty() || forget)) ||
    (no_qaros && (local || connect_only || forget || !hub_text.empty() || !code.empty()));
  if (!help && !bad_usage && (conflicting || (!code.empty() && hub_text.empty()) || seconds < 1 ||
      port < 0 || port > 65535))
  {
    bad_usage = true;
  }
  if (help || bad_usage) {
    print_usage();
    return help ? 0 : 2;
  }
  QCoreApplication app(argc, argv);
  const std::string host = QSysInfo::machineHostName().toStdString();
  // [qar-snippet:session]
  std::shared_ptr<qqa::QarSessionOwner> owner;  // stays empty with --no-qaros: no QAROS is needed
  if (!no_qaros) {
    qqa::SessionOwnerConfig owner_config;
    owner_config.program_tag = kProgramTag;
    owner_config.display_name = std::string("Web panel example on ") + host;
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
  // [qar-snippet-end:session]
  // [qar-snippet:server]
  CounterSink sink;  // declared before the server, so that it outlives every session
  wp::WebPanelServerConfig server_config;
  server_config.port = static_cast<quint16>(port);
  if (loopback) { server_config.bind_address = QHostAddress::LocalHost; }
  wp::WebPanelServer server(server_config);
  std::string error;
  if (!server.start(&error)) { std::printf("example: cannot start the panel server: %s\n", error.c_str()); return 1; }
  const std::shared_ptr<wp::PanelSession> page = server.add_panel(make_model(), &error);  // renders the page once
  if (!page) { std::printf("example: the panel server refused the model: %s\n", error.c_str()); return 1; }
  page->set_control_sink(&sink);
  const std::string url = server.page_url(*page);
  std::printf("example: panel page: %s\n", url.c_str());
  // [qar-snippet-end:server]
  // [qar-snippet:url_panel]
  wp::UrlPanelSizing sizing;
  // The size comes from a fixed design viewport, never from the viewport QAROS gives the page:
  // that one follows the panel's own size, so a size derived from it never settles.
  wp::use_design_viewport(sizing);
  const auto fit = [&] { return wp::size_url_panel_for(page->model(), server.config().render.theme, sizing).size; };
  qar_ar_core::UrlPanelDesc desc;
  desc.url = url;
  desc.display_name = "Counter";
  desc.placement.pose.position = QarVector3{0.0f, 1.5f, -1.2f};  // the top centre of the panel, in the room
  desc.placement.size = fit();
  // The same name finds the same QAROS panel again at the next start.
  desc.common_name = qar_ar_core::make_panel_common_name(
    "counter", qar_ar_core::make_name_scope(kProgramTag, "", host));
  std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> ar_panel;
  std::unique_ptr<qqa::QarCApiUrlPanelFactory> factory;
  if (owner) {
    factory = std::make_unique<qqa::QarCApiUrlPanelFactory>(owner.get());
    ar_panel = factory->create_url_panel(desc);
    if (!ar_panel) { std::printf("example: could not open the URL panel: %s\n", factory->last_error().c_str()); return 1; }
  }
  // [qar-snippet-end:url_panel]
  // [qar-snippet:viewport]
  QObject::connect(page.get(), &wp::PanelSession::viewport_reported, &app, [&](const wp::PanelViewportReport & report) {
    std::printf("example: %s\n", report.describe().c_str());
    // The report is only printed: the panel keeps the size it was created with.
  });
  // [qar-snippet-end:viewport]
  QTimer events_timer;  // the owner's news: a lost Hub, a target that came or went
  if (owner) {
    QObject::connect(&events_timer, &QTimer::timeout, &app, [&] {
      std::vector<qqa::SessionEvent> events;
      owner->poll(events);
      print_events(events);
      if (ar_panel) { print_panel_failures(*ar_panel); }
    });
    events_timer.start(50);
  }
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
  std::printf("example: done\n");
  return 0;
}
