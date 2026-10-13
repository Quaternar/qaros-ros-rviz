// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Serves one AR panel that is built by hand from a UI model: a counter and a button. WebPanelServer serves the
// page; a desktop browser can open it, and with --sdk-bin QarOS opens it as a URL panel in a Visualizer.

// [qar-snippet:includes]
#include <QCoreApplication>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

// qar_ui_model first: the QarOS header includes <windows.h>, whose macro `interface` breaks type_description.hpp.
#include "qar_ui_model/ui_model.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"
#include "qar_web_panels/web_panel_server.hpp"

#include "qar_ar_core/qar_streaming_c.hpp"
#include "qar_qar_adapter/qar_capi_render_sender_adapter.hpp"
#include "qar_qar_adapter/qar_capi_url_panel_adapter.hpp"
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

int main(int argc, char ** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string sdk_bin;
  bool loopback = false;
  int seconds = 60;
  int port = 0;
  const bool help = argc == 2 && std::string(argv[1]) == "--help";
  for (int i = 1; i < argc && !help; ++i) {
    const std::string arg = argv[i];
    if (arg == "--loopback") { loopback = true; }
    else if (arg == "--sdk-bin" && i + 1 < argc) { sdk_bin = argv[++i]; }
    else if (arg == "--serve-seconds" && i + 1 < argc) { seconds = std::atoi(argv[++i]); }
    else if (arg == "--port" && i + 1 < argc) { port = std::atoi(argv[++i]); }
    else { seconds = 0; break; }
  }
  if (help || seconds < 1 || port < 0 || port > 65535) {
    std::printf("Usage: qar_web_panel_example [--serve-seconds N] [--loopback] [--port N] [--sdk-bin DIR]\n"
      "  --serve-seconds N  serve the panel for N seconds, then exit (default 60)\n"
      "  --loopback         listen on 127.0.0.1 only, so that no other machine can connect (default: all interfaces)\n"
      "  --port N           TCP port of the panel server (default 0: the operating system picks a free port)\n"
      "  --sdk-bin DIR      bin folder of the QarOS SDK: also open the page as a URL panel and invite a Visualizer (opens a window)\n");
    return help ? 0 : 2;
  }
  QCoreApplication app(argc, argv);
  // [qar-snippet:session]
  std::shared_ptr<qqa::QarCApiSenderFactory> qaros;  // stays empty without --sdk-bin: no QarOS SDK is needed
  if (!sdk_bin.empty()) {
    qqa::QarCApiSenderFactory::Config config;
    config.runtime_binaries_folder_path = sdk_bin;  // QarOS starts its launcher and Visualizer from here
    config.log_severity = QAR_LOG_SEVERITY_WARN;
    config.enable_console_logging = false;
    qaros = std::make_shared<qqa::QarCApiSenderFactory>(config);  // blocks until the session is up
    QarPeerId visualizer{};
    const std::string failed = qaros->ready() ? qaros->invite_additional_visualizer(visualizer) : qaros->last_error();
    if (!failed.empty()) { std::printf("example: QarOS is not ready: %s\n", failed.c_str()); return 1; }
    std::printf("example: connected to QarOS, Visualizer invited\n");
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
  wp::UrlPanelSizing sizing;  // the page reports the viewport that QarOS gives it, and the size follows that
  const auto fit = [&] { return wp::size_url_panel_for(page->model(), server.config().render.theme, sizing).size; };
  qar_ar_core::UrlPanelDesc desc;
  desc.url = url;
  desc.display_name = "Counter";
  desc.placement.pose.position = QarVector3{0.0f, 1.5f, -1.2f};  // the top centre of the panel, in the room
  desc.placement.size = fit();
  std::shared_ptr<qar_ar_core::IQarUrlPanelAdapter> ar_panel;
  if (qaros) {
    qqa::QarCApiUrlPanelFactory factory(qaros.get());
    ar_panel = factory.create_url_panel(desc);
    if (!ar_panel) { std::printf("example: could not open the URL panel: %s\n", factory.last_error().c_str()); return 1; }
  }
  // [qar-snippet-end:url_panel]
  // [qar-snippet:viewport]
  QObject::connect(page.get(), &wp::PanelSession::viewport_reported, &app, [&](const wp::PanelViewportReport & report) {
    std::printf("example: %s\n", report.describe().c_str());
    wp::use_reported_viewport(sizing, report.viewport);
    const QarGuiPanelSize size = fit();
    // The page reports again after every resize, so only a change of the size is pushed to QarOS.
    if (std::abs(size.width_meters - desc.placement.size.width_meters) +
      std::abs(size.height_meters - desc.placement.size.height_meters) > 0.005f) {
      desc.placement.size = size;
      if (ar_panel) { ar_panel->set_placement(desc.placement); }
    }
  });
  // [qar-snippet-end:viewport]
  QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
  app.exec();
  ar_panel.reset();  // closes the QarOS panel while the session still exists; the session follows when main ends
  std::printf("example: done\n");
  return 0;
}
