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
constexpr const char * kJoinAgainToolTip =
  "Give up the session the QAROS Hub does not answer for, release it and join the Hub again.";
constexpr const char * kStopRejoiningToolTip =
  "Stop joining the QAROS Hub again. Connect to QarOS starts a join.";

/// An automatic attempt is under way and the Hub has not shown its approval prompt.
bool joining_again_quietly(const ConnectionInput & input)
{
  return input.auto_rejoin && !input.rejoin_prompt_open &&
         input.phase == ConnectionPhase::WaitingForApproval;
}

std::string approval_text_of(const ConnectionInput & input)
{
  std::string text =
    "QarOS: waiting for approval in the QAROS Hub - click Allow in the QAROS window";
  if (input.approval_seconds_left >= 0) {
    text += " (" + format_countdown(input.approval_seconds_left) + " left)";
  }
  return text;
}

std::string lost_text_of(const ConnectionInput & input)
{
  const std::string head = "QarOS: the QAROS Hub is gone - ";
  if (input.releasing) {
    return head + "releasing the session ...";
  }
  const std::string attempt = std::to_string(input.rejoin_attempt);
  if (input.rejoin_in_s < 0) {
    return head + "joining it again (attempt " + attempt + "); RViz keeps working";
  }
  return head + "joining it again in " + std::to_string(input.rejoin_in_s) + " s (attempt " +
         attempt + "); RViz keeps working";
}

std::string state_text_of(const ConnectionInput & input)
{
  switch (input.phase) {
    case ConnectionPhase::Preview:
      return kPreviewText;
    case ConnectionPhase::Idle:
      return "QarOS: not connected";
    case ConnectionPhase::Loading:
      return "QarOS: loading the QAROS runtime ...";
    case ConnectionPhase::WaitingForApproval:
      if (joining_again_quietly(input)) {
        return "QarOS: joining the QAROS Hub again (attempt " +
               std::to_string(input.rejoin_attempt) + ") ...";
      }
      return approval_text_of(input);
    case ConnectionPhase::Running: {
        if (input.attaching) {
          return "QarOS: joined (local app) - setting up the session ...";
        }
        std::string text = std::string("QarOS: connected (local app)") +
          (input.streaming_paused ? ", streaming paused - " : " - ") +
          std::to_string(input.target_count) + " target(s), " +
          std::to_string(input.served_count) + " served";
        if (!input.degraded_text.empty()) {
          text += "; " + input.degraded_text;
        }
        return text;
      }
    case ConnectionPhase::Reconnecting:
      return "QarOS: Hub not reachable - waiting for it to come back" +
             (input.hub_silent_s >= 0 ? " (" + std::to_string(input.hub_silent_s) + " s)" :
             std::string());
    case ConnectionPhase::Lost:
      return lost_text_of(input);
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
  const bool approved = input.phase == ConnectionPhase::Running ||
    input.phase == ConnectionPhase::Reconnecting || input.phase == ConnectionPhase::Lost;
  return approved ? "This PC: approved local app (" + input.program_name + ")" :
         "This PC (not joined yet)";
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
    phase == ConnectionPhase::WaitingForApproval || phase == ConnectionPhase::Reconnecting ||
    phase == ConnectionPhase::Lost;
  if (phase == ConnectionPhase::Reconnecting) {
    view.cancel_text = "Join again";
    view.cancel_tooltip = kJoinAgainToolTip;
  } else if (phase == ConnectionPhase::Lost || joining_again_quietly(input)) {
    view.cancel_text = "Stop rejoining";
    view.cancel_tooltip = kStopRejoiningToolTip;
  }

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
