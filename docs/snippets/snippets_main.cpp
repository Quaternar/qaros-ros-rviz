// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Entry point of the documentation snippet target. Every snippet function is declared here and
// called inside `if (argc > 1000)`: the code is compiled and linked, all references resolve, and
// nothing runs. The arguments are null pointers or empty objects, which the functions would never
// get from a host.

// qar_ui_model first: the headers after it include <windows.h> (through the QarOS header), and the
// macro `interface` of that header breaks type_description.hpp when it comes first.
#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_model.hpp"
#include "qar_web_panels/url_panel_sizing.hpp"
#include "qar_web_panels/web_panel_server.hpp"

#include "rclcpp/rclcpp.hpp"

#include <memory>
#include <string>

namespace ui = qar_ui_model;
namespace wp = qar_web_panels;

// ui_model_snippets.cpp
std::string snippet_values();
ui::UiModel snippet_model();
std::string snippet_render(const ui::UiModel & model, const ui::MessageDescription & schema);
void snippet_frames(ui::UiValue & document);

// schema_snippets.cpp
ui::MessageDescriptionPtr snippet_schema();
void snippet_paths(const ui::MessageDescription & schema);

// web_panel_snippets.cpp
class CounterSink;
std::string snippet_server(ui::UiModel model);
void snippet_session(wp::PanelSession & session, CounterSink & sink);
void snippet_signals(wp::PanelSession & session, QObject * host);
QarGuiPanelSize snippet_sizing(const wp::PanelSession & session, const wp::PanelViewportReport & report);

// ros_ui_snippets.cpp
void snippet_graph(const std::shared_ptr<rclcpp::Node> & node);
void snippet_introspection();
void snippet_topics(const std::shared_ptr<rclcpp::Node> & node);
void snippet_service(const std::shared_ptr<rclcpp::Node> & node);
void snippet_action(const std::shared_ptr<rclcpp::Node> & node);

int main(int argc, char** argv) {
  (void)argv;
  if (argc > 1000) {
    ui::UiValue document;
    const std::shared_ptr<rclcpp::Node> node;
    const ui::MessageDescriptionPtr schema = snippet_schema();
    const wp::PanelViewportReport report;
    CounterSink * sink = nullptr;
    wp::PanelSession * session = nullptr;

    snippet_values();
    snippet_render(snippet_model(), *schema);
    snippet_frames(document);
    snippet_paths(*schema);

    snippet_server(ui::UiModel{});
    snippet_session(*session, *sink);
    snippet_signals(*session, nullptr);
    snippet_sizing(*session, report);

    snippet_graph(node);
    snippet_introspection();
    snippet_topics(node);
    snippet_service(node);
    snippet_action(node);
  }
  return 0;
}
