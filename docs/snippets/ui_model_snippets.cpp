// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Snippets of API.md for qar_ui_model: values, the widget model, the schema, the renderer and the frames.
// The functions are compiled and linked and never run. Their parameters stand for what the host owns.

// The headers of qar_ui_model come first in every file: a header that includes <windows.h> would define the
// macro `interface` and break type_description.hpp.
#include "qar_ui_model/html_renderer.hpp"
#include "qar_ui_model/json.hpp"
#include "qar_ui_model/ui_model.hpp"
#include "qar_ui_model/ui_protocol.hpp"
#include "qar_ui_model/ui_value.hpp"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace ui = qar_ui_model;

std::string snippet_values()
{
  // [qar-snippet:values]
  ui::UiValue pose = ui::UiValue::object();                  // objects keep the order of their keys
  pose.set("frame", "map").set("x", 1.5).set("seq", 7);      // a String, a Double and an Int: the kinds stay apart
  const ui::UiValue * x = pose.find("x");                    // nullptr when the key is absent
  const double meters = x != nullptr ? x->as_double(0.0) : 0.0;   // the fallback is used when the kind does not match

  const std::string text = ui::json_write(pose);             // {"frame":"map","x":1.5,"seq":7}
  ui::UiValue back;
  ui::JsonParseError error;
  if (!ui::json_parse(text, back, error)) {
    std::printf("%s\n", error.to_string().c_str());          // the message, with the line and the column
  }
  // [qar-snippet-end:values]
  return meters > 0.0 && back == pose ? text : std::string();
}

ui::UiModel snippet_model()
{
  // [qar-snippet:model]
  ui::UiModel model;
  model.root = ui::widgets::panel("battery_panel", "Battery");        // the root, and the only Panel of the tree
  ui::Widget status = ui::widgets::section("status", "Status");
  status.add(ui::widgets::value("percent", "battery.percentage", "Charge").with_unit("%").with_precision(0));
  status.add(ui::widgets::gauge("level", "battery.percentage", "Level", 0.0, 1.0));
  model.root.add(std::move(status));
  model.root.add(ui::widgets::slider("limit", "limit.percent", "Limit", 0.0, 100.0).with_action("set_limit"));
  model.root.add(ui::widgets::button("stop", "Stop", "stop").with_confirm());   // a second press within 4 s fires it
  model.initial_data = ui::UiValue::object()                                    // what the page shows before any frame
    .set("battery", ui::UiValue::object().set("percentage", 0.8))
    .set("limit", ui::UiValue::object().set("percent", 20.0));
  // [qar-snippet-end:model]
  return model;
}

std::string snippet_render(const ui::UiModel & model, const ui::MessageDescription & schema)
{
  // [qar-snippet:render]
  const std::vector<ui::UiProblem> found = ui::validate(model, &schema);   // without a schema: the syntax only
  std::printf("%zu problem(s), valid: %s\n", found.size(), ui::is_valid(found) ? "yes" : "no");

  ui::HtmlRenderOptions options;
  options.theme = ui::ArTheme::dark().scaled(1.0);   // every size in pixels, times the factor
  options.websocket_url = "";                        // empty: the page derives the address from its own location
  options.reconnect_ms = 2000;                       // the timings that the page script reads
  options.ping_interval_ms = 5000;
  options.stale_after_ms = 5000;

  std::string html;
  std::vector<ui::UiProblem> problems;
  if (!ui::render_html_page(model, options, html, problems)) {   // false: an Error in the model or the theme, html is empty
    for (const ui::UiProblem & problem : problems) {
      std::printf("%s %s: %s\n", ui::to_string(problem.severity), problem.widget_id.c_str(), problem.message.c_str());
    }
  }
  return html;
  // [qar-snippet-end:render]
}

void snippet_frames(ui::UiValue & document)
{
  // [qar-snippet:frames]
  std::vector<ui::PatchEntry> patch;
  patch.push_back({"battery.percentage", 0.79});                  // one assignment of a data frame
  const ui::ProtocolMessage frame = ui::make_data(42, patch);
  const std::string wire = ui::encode_message(frame);             // one line of compact JSON

  ui::ProtocolMessage received;
  std::string error;
  if (ui::decode_message(wire, received, error)) {                // an unknown type decodes as MessageType::Unknown
    if (const ui::DataBody * data = received.as<ui::DataBody>()) {
      if (!ui::apply_patch(document, *data, error)) {             // all or nothing: a failure leaves the document as it was
        std::printf("%s\n", error.c_str());
      }
    }
  }
  // [qar-snippet-end:frames]
}
