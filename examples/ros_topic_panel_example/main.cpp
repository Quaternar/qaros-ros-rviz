// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Exposes one ROS 2 topic as an AR panel: a label shows the newest message of /qar_example/text and a button
// publishes on /qar_example/pressed. qar_ros_ui reads and writes the messages without a generated header,
// qar_web_panels serves the page, and with --sdk-bin QarOS opens the page as a URL panel.

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"

// qar_ui_model first: the QarOS header includes <windows.h>, whose macro `interface` breaks type_description.hpp.
#include "qar_ui_model/ui_model.hpp"
#include "qar_ros_ui/topic_bridge.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"
#include "qar_web_panels/web_panel_server.hpp"

#include "qar_ar_core/qar_streaming_c.hpp"
#include "qar_qar_adapter/qar_capi_render_sender_adapter.hpp"
#include "qar_qar_adapter/qar_capi_url_panel_adapter.hpp"

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

int main(int argc, char ** argv)
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::string sdk_bin;
  bool loopback = false;
  int seconds = 60;
  const bool help = argc == 2 && std::string(argv[1]) == "--help";
  for (int i = 1; i < argc && !help; ++i) {
    const std::string arg = argv[i];
    if (arg == "--loopback") { loopback = true; }
    else if (arg == "--sdk-bin" && i + 1 < argc) { sdk_bin = argv[++i]; }
    else if (arg == "--serve-seconds" && i + 1 < argc) { seconds = std::atoi(argv[++i]); }
    else { seconds = 0; break; }
  }
  if (help || seconds < 1) {
    std::printf("Usage: qar_ros_topic_panel_example [--serve-seconds N] [--loopback] [--sdk-bin DIR]\n"
      "  --serve-seconds N  run the node and serve the panel for N seconds, then exit (default 60)\n"
      "  --loopback         serve the page on 127.0.0.1 only, so that no other machine can open it (default: all interfaces)\n"
      "  --sdk-bin DIR      bin folder of the QarOS SDK: also open the page as a URL panel and invite a Visualizer (opens a window)\n");
    return help ? 0 : 2;
  }
  QCoreApplication app(argc, argv);
  std::shared_ptr<qqa::QarCApiSenderFactory> qaros;  // stays empty without --sdk-bin
  if (!sdk_bin.empty()) {
    qqa::QarCApiSenderFactory::Config config;
    config.runtime_binaries_folder_path = sdk_bin;
    config.log_severity = QAR_LOG_SEVERITY_WARN;
    config.enable_console_logging = false;
    qaros = std::make_shared<qqa::QarCApiSenderFactory>(config);  // blocks until the session is up
    QarPeerId visualizer{};
    const std::string failed = qaros->ready() ? qaros->invite_additional_visualizer(visualizer) : qaros->last_error();
    if (!failed.empty()) { std::printf("example: QarOS is not ready: %s\n", failed.c_str()); return 1; }
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
  if (qaros) {
    qar_ar_core::UrlPanelDesc desc{server.page_url(*page), "ROS topic", {}};
    desc.placement.pose.position = QarVector3{0.0f, 1.5f, -1.2f};  // the top centre of the panel, in the room
    desc.placement.size = wp::size_url_panel_for(page->model(), server.config().render.theme).size;
    qqa::QarCApiUrlPanelFactory factory(qaros.get());
    ar_panel = factory.create_url_panel(desc);
    if (!ar_panel) { std::printf("example: could not open the URL panel: %s\n", factory.last_error().c_str()); return 1; }
  }
  // [qar-snippet:tick]
  int ticks = 0;
  QTimer timer;  // every 10 ms: run the ROS callbacks, then show the newest message
  QObject::connect(&timer, &QTimer::timeout, [&] {
    executor.spin_some();
    if (!rclcpp::ok()) { app.quit(); }  // Ctrl+C has shut ROS 2 down: the loop ends too
    if (++ticks % 100 == 0) { bridge.reconcile(); }  // follow the QoS of publishers that come and go
    ui::UiValue message;
    if (subscription->take_latest(message)) {  // true once for each newest message; older ones are skipped
      page->set_value("message", message);
      const ui::UiValue * data = message.find("data");
      std::printf("example: rx /qar_example/text: %s\n", data != nullptr ? data->as_string().c_str() : "");
    }
  });
  timer.start(10);
  // [qar-snippet-end:tick]
  QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
  app.exec();
  rclcpp::shutdown();
  std::printf("example: done\n");
  return 0;
}
