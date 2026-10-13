// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for qar_web_panels: the server, the session with its control sink, and the size of a URL panel.
// The functions are compiled and linked and never run. Their parameters stand for what the host owns.

// qar_ui_model first: url_panel_sizing.hpp includes the QarOS header, which includes <windows.h>, and the macro
// `interface` of that header breaks type_description.hpp when it comes first.
#include "qar_ui_model/ui_model.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"
#include "qar_web_panels/web_panel_server.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ui = qar_ui_model;
namespace wp = qar_web_panels;

std::string snippet_server(ui::UiModel model)
{
  // [qar-snippet:server]
  wp::WebPanelServerConfig config;
  config.bind_address = QHostAddress::LocalHost;   // the default is every interface
  config.port = 0;                                 // 0: the operating system picks a free port
  config.advertised_host = "";                     // empty: the bind address, or the address that the machine routes through
  config.allow_any_origin = false;                 // refuse a WebSocket handshake of another origin
  config.session.max_clients = 16;                 // viewers of one panel
  config.http.max_connections = 64;                // connections of the server
  config.render.reconnect_ms = 2000;               // the page script dials again after this

  wp::WebPanelServer server(config);               // lives on the thread of a Qt event loop
  std::string error;
  if (!server.start(&error)) {
    return error;                                  // cannot listen on <address>:<port>: <reason>
  }
  const std::shared_ptr<wp::PanelSession> session = server.add_panel(std::move(model), &error);   // renders the page once
  if (!session) {
    return error;                                  // model does not render: the first Error of the model
  }
  const std::string url = server.page_url(*session);   // http://<host>:<port>/p/<token>/
  server.remove_panel(session->token());               // closes its viewers and forgets the token
  return url;
  // [qar-snippet-end:server]
}

// [qar-snippet:sink]
// The host's answer to the buttons of a page. It runs on the thread that owns the session.
class CounterSink : public wp::IPanelControlSink
{
public:
  void on_control(wp::PanelSession & session, const wp::PanelControlEvent & event) override
  {
    if (event.control.action != "increment") {
      session.ack(event, false, "unknown action");               // the outcome goes back to the viewer that sent the event
      return;
    }
    std::string error;
    if (!session.set_value("count", ++count_, &error)) {         // the document of the server first, then every viewer
      session.ack(event, false, error);
      return;
    }
    session.ack(event, true);
  }

private:
  std::int64_t count_ = 0;
};
// [qar-snippet-end:sink]

void snippet_session(wp::PanelSession & session, CounterSink & sink)
{
  // [qar-snippet:session]
  session.set_control_sink(&sink);                               // borrowed: the sink has to outlive the session
  std::string error;
  std::vector<ui::PatchEntry> patch;
  patch.push_back({"battery", ui::UiValue::object().set("percentage", 0.5)});
  patch.push_back({"odom.x", 1.25});
  if (!session.publish(std::move(patch), &error)) {              // all or nothing: nothing is sent when it fails
    std::printf("%s\n", error.c_str());
  }
  session.send_status(ui::StatusLevel::Warning, "no publisher on /odom");   // an empty text clears the line

  if (session.last_viewport()) {                                 // empty until a page has reported its viewport
    std::printf("%s\n", session.last_viewport()->describe().c_str());
  }
  const wp::PanelSessionDiagnostics counters = session.diagnostics();   // viewers, frames, refusals
  std::printf("%zu viewer(s), %llu control event(s)\n", counters.clients,
    static_cast<unsigned long long>(counters.controls));
  // [qar-snippet-end:session]
}

void snippet_signals(wp::PanelSession & session, QObject * host)
{
  // [qar-snippet:signals]
  // A host whose ROS 2 executor runs on another thread connects with a queued connection: the lambdas then run
  // on the thread of `host`, which has to be the thread that owns the session.
  QObject::connect(&session, &wp::PanelSession::control_received, host,
    [&session](const wp::PanelControlEvent & event) {session.ack(event, true);}, Qt::QueuedConnection);
  QObject::connect(&session, &wp::PanelSession::viewport_reported, host,
    [](const wp::PanelViewportReport & report) {std::printf("%s\n", report.describe().c_str());}, Qt::QueuedConnection);
  // [qar-snippet-end:signals]
}

QarGuiPanelSize snippet_sizing(const wp::PanelSession & session, const wp::PanelViewportReport & report)
{
  // [qar-snippet:sizing]
  wp::UrlPanelSizing sizing;                                  // bounds and knobs; CONFIGURATION lists the defaults
  std::string note;
  wp::apply_sizing_overrides(sizing, wp::process_environment(), note);   // QAR_PANEL_WEB_SIZE and QAR_PANEL_WEB_SCALE
  wp::use_reported_viewport(sizing, report.viewport);                    // the viewport that the page reported

  const wp::UrlPanelSizeResult result =
    wp::size_url_panel_for(session.model(), ui::ArTheme::dark(), sizing);   // fit the page, then size the panel
  std::printf("%s\n", result.note.c_str());                  // one line: the page, the panel and the bounds that applied
  return result.size;                                         // metres, with content_scale 1.0
  // [qar-snippet-end:sizing]
}
