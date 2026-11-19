// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The end of a session on the owner's thread: the owner runs the queued tasks one after the other,
// so a destroy of a video sender that does not return holds back every task queued behind it. The
// hide of a panel that is queued BEFORE the detach of another one therefore runs, and the hide that
// is queued after it does not (the order of the release in the RViz panel: the browser panels'
// hides come first). The destroy is held with the call hook of the fake.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "qar_ar_core/common_name.hpp"
#include "qar_capi_panels/qar_capi_panel_sender_adapter.hpp"
#include "qar_capi_panels/seam_c.hpp"

namespace seam = qar_qt_streaming::seam;

namespace
{

using seam::PanelOpResult;  // read by panel_test_kit.hpp

}  // namespace

#include "panel_test_kit.hpp"

namespace
{

seam::PanelDesc exit_desc(const std::string & key)
{
  seam::PanelDesc desc;
  desc.key = key;
  desc.display_name = key;
  desc.common_name = qar_ar_core::make_panel_common_name(
    key, qar_ar_core::make_name_scope("test-program", "", "lab-pc"));
  desc.buffer_width = 64;
  desc.buffer_height = 48;
  desc.placement.size = seam::PanelSize{0.4f, 0.3f, 1.0f};
  return desc;
}

/// The state of the panel made for @p key in the fake, or -1 when there is none.
int state_of(Harness & h, const std::string & key)
{
  const std::string name = exit_desc(key).common_name;
  for (const auto & panel : h.fake().panels()) {
    if (panel.common_name == name) {
      return static_cast<int>(panel.state);
    }
  }
  return -1;
}

/// Two panels, "hidden" and "leaving". The destroy of a video sender is held. The hide of
/// "hidden" and the detach of "leaving" are queued in the order asked for; returns the state of
/// "hidden" while the destroy is held.
int state_of_hidden_panel_while_destroy_is_held(bool hide_first)
{
  Harness h;
  h.start();
  EXPECT_TRUE(h.connect_and_run());
  qar_capi_panels::QarCApiPanelSenderFactory factory(&h.owner());
  auto hidden = factory.create_panel_sender(exit_desc("hidden"));
  auto leaving = factory.create_panel_sender(exit_desc("leaving"));
  EXPECT_NE(hidden, nullptr);
  EXPECT_NE(leaving, nullptr);
  if (hidden == nullptr || leaving == nullptr) {
    return -1;
  }
  std::vector<PanelOpResult> hidden_results;
  std::vector<PanelOpResult> leaving_results;
  EXPECT_TRUE(drain_until(*hidden, hidden_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));
  EXPECT_TRUE(drain_until(*leaving, leaving_results, [](const std::vector<PanelOpResult> & r) {
        return has_result(r, PanelOpResult::Op::Create);
      }));

  std::atomic<bool> hold{true};
  std::atomic<bool> entered{false};
  h.fake().set_call_hook([&hold, &entered](const std::string & name) {
      if (name == "video_sender_handle_destroy") {
        entered.store(true);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (hold.load() && std::chrono::steady_clock::now() < end) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
      }
    });

  if (hide_first) {
    EXPECT_TRUE(hidden->set_state(seam::PanelState::Hidden));
    leaving->detach();
  } else {
    leaving->detach();
    EXPECT_TRUE(hidden->set_state(seam::PanelState::Hidden));
  }
  EXPECT_TRUE(wait_until([&entered] { return entered.load(); }));
  // The owner is inside the destroy now. Give the task behind it the time it would need.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const int state = state_of(h, "hidden");
  hold.store(false);
  EXPECT_TRUE(wait_until([&h] { return h.fake().live_video_senders() == 1; }));
  EXPECT_TRUE(owner_caught_up(h));
  EXPECT_EQ(state_of(h, "hidden"), static_cast<int>(QAR_GUI_PANEL_STATE_HIDDEN))
    << "both orders end hidden";
  EXPECT_EQ(h.fake().count("gui_panels_close_panel"), 0) << "nothing is closed";
  h.fake().set_call_hook([](const std::string &) {});
  return state;
}

TEST(ExitOrder, AHideQueuedBeforeAHangingDestroyIsDone)
{
  EXPECT_EQ(
    state_of_hidden_panel_while_destroy_is_held(true),
    static_cast<int>(QAR_GUI_PANEL_STATE_HIDDEN));
}

TEST(ExitOrder, AHideQueuedAfterAHangingDestroyWaitsForIt)
{
  // The reverse order, which shows that the test above can fail.
  EXPECT_EQ(
    state_of_hidden_panel_while_destroy_is_held(false),
    static_cast<int>(QAR_GUI_PANEL_STATE_VISIBLE));
}

}  // namespace
