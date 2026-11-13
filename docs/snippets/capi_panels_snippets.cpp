// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Snippet of API.md for the module qar_capi_panels: the QarOS panel factory and its input source. The function is
// compiled and linked and never run. Its parameters stand for what the host owns, so each one is a pointer or a
// reference that the documentation names in its text.

#include "qar_capi_panels/qar_capi_panel_input_source.hpp"
#include "qar_capi_panels/qar_capi_panel_sender_adapter.hpp"
#include "qar_qar_adapter/qar_session_owner.hpp"
#include "qar_qt_streaming/seam/common_name.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace seam = qar_qt_streaming::seam;

void snippet_capi_panels(qar_qar_adapter::IQarSessionOwner * owner, const seam::NameScope & scope)
{
  // [qar-snippet:panels]
  // One input source serves all panels; the factory hands it to every panel that it creates.
  auto input = std::make_shared<qar_capi_panels::QarCApiPanelInputSource>(owner->session());
  qar_capi_panels::QarCApiPanelSenderFactory factory(owner, input);

  seam::PanelDesc desc;
  desc.key = "status";
  desc.common_name = seam::make_panel_common_name(desc.key, scope);
  desc.buffer_width = 1024;
  desc.buffer_height = 768;
  desc.content_width = 1000;                           // the part of the buffer that the widget paints;
  desc.content_height = 760;                           // zero means the whole buffer
  const std::shared_ptr<seam::IPanelSender> sender = factory.create_panel_sender(desc);
  if (!sender) {
    std::printf("panel: %s\n", factory.last_error().c_str());
    return;
  }

  // The panel is subscribed to its gestures. On the GUI thread, once for each tick:
  std::vector<seam::PanelPointerSample> samples;
  input->drain(samples);
  std::vector<std::string> closed;
  input->drain_closed_panels(closed);                  // the keys of the panels that the operator closed in AR
  std::printf("%zu samples, %zu closed, %zu subscriptions\n", samples.size(), closed.size(),
    input->subscription_count());
  // [qar-snippet-end:panels]
}
