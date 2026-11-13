// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/connection_view.hpp"

namespace qar_rviz_streaming
{
namespace
{

std::string two_digits(int value)
{
  return (value < 10 ? "0" : "") + std::to_string(value);
}

constexpr const char * kPreviewText = "QarOS: preview mode - set QAR_PANEL_LIVE_STREAM to connect";
constexpr const char * kInviteNeedsSessionToolTip =
  "Inviting a device needs a QarOS session: start RViz with the live launcher and press "
  "Connect to QarOS.";
constexpr const char * kInviteReadyToolTip =
  "Ask the QAROS Hub of this PC to start the app of the selected device. Available while "
  "the panel is connected to QarOS.";
constexpr const char * kInviteBusyToolTip = "An invite is in progress.";
constexpr const char * kInviteAttachingToolTip =
  "The session is being set up; an invite is possible when that is done.";

std::string state_text_of(const ConnectionInput & input)
{
  switch (input.phase) {
    case ConnectionPhase::Preview:
      return kPreviewText;
    case ConnectionPhase::Idle:
      return "QarOS: not connected";
    case ConnectionPhase::Loading:
      return "QarOS: loading the QAROS runtime ...";
    case ConnectionPhase::WaitingForApproval: {
        std::string text =
          "QarOS: waiting for approval in the QAROS Hub - click Allow in the QAROS window";
        if (input.approval_seconds_left >= 0) {
          text += " (" + format_countdown(input.approval_seconds_left) + " left)";
        }
        return text;
      }
    case ConnectionPhase::Running: {
        if (input.attaching) {
          return "QarOS: joined (local app) - setting up the session ...";
        }
        return std::string("QarOS: connected (local app)") +
               (input.streaming_paused ? ", streaming paused - " : " - ") +
               std::to_string(input.target_count) + " target(s), " +
               std::to_string(input.served_count) + " served";
      }
    case ConnectionPhase::Reconnecting:
      return "QarOS: Hub not reachable - waiting for it to come back";
    case ConnectionPhase::Stopping:
      return "QarOS: disconnecting ...";
    case ConnectionPhase::Failed:
      return "QarOS: " + (input.failure_message.empty() ? std::string("the join failed") :
             input.failure_message);
  }
  return "QarOS: not connected";
}

std::string identity_text_of(const ConnectionInput & input)
{
  if (input.phase == ConnectionPhase::Preview) {
    return "preview (no QAROS)";
  }
  const bool approved = input.phase == ConnectionPhase::Running ||
    input.phase == ConnectionPhase::Reconnecting;
  return approved ?
         "This PC: approved local app (" + input.program_name + ")" :
         "This PC: local app (" + input.program_name + "), approved once in the QAROS Hub";
}

}  // namespace

std::string format_countdown(int seconds)
{
  if (seconds < 0) {
    seconds = 0;
  }
  return std::to_string(seconds / 60) + ":" + two_digits(seconds % 60);
}

ConnectionView make_connection_view(const ConnectionInput & input)
{
  const ConnectionPhase phase = input.phase;
  ConnectionView view;
  view.state_text = state_text_of(input);
  view.identity_text = identity_text_of(input);
  view.cancel_enabled = phase == ConnectionPhase::Loading ||
    phase == ConnectionPhase::WaitingForApproval;

  if (phase == ConnectionPhase::Running && input.attaching) {
    view.invite_enabled = false;
    view.invite_tooltip = kInviteAttachingToolTip;
  } else if (phase == ConnectionPhase::Running && input.streaming_paused) {
    view.invite_enabled = false;
    view.invite_tooltip = kInvitePausedToolTip;
  } else if (phase == ConnectionPhase::Running && !input.invite_in_flight) {
    view.invite_enabled = true;
    view.invite_tooltip = kInviteReadyToolTip;
  } else if (phase == ConnectionPhase::Running) {
    view.invite_enabled = false;
    view.invite_tooltip = kInviteBusyToolTip;
  } else {
    view.invite_enabled = false;
    view.invite_tooltip = kInviteNeedsSessionToolTip;
  }
  return view;
}

}  // namespace qar_rviz_streaming
