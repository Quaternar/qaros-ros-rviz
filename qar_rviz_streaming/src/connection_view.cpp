// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/connection_view.hpp"

namespace qar_rviz_streaming
{
namespace
{

enum class Resolved
{
  Unknown,
  Local,
  Remote
};

Resolved resolve(const ConnectionInput & input)
{
  switch (input.mode_setting) {
    case HubChoice::Remote:
      return Resolved::Remote;
    case HubChoice::Local:
      return Resolved::Local;
    case HubChoice::Auto:
      break;
  }
  if (input.mode_resolved) {
    return input.remote_resolved ? Resolved::Remote : Resolved::Local;
  }
  return input.has_remote_identity ? Resolved::Remote : Resolved::Unknown;
}

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

std::string state_text_of(const ConnectionInput & input, Resolved mode)
{
  const std::string address = hub_address(input.host, input.port);
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
    case ConnectionPhase::Rejoining:
      return address.empty() ? "QarOS: rejoining the Hub ..." : "QarOS: rejoining " + address +
             " ...";
    case ConnectionPhase::NeedsCode:
      return address.empty() ?
             std::string("QarOS: type the host, the port and the pairing code of the Hub "
             "(Connection tab)") :
             "QarOS: type the pairing code of the Hub at " + address + " (Connection tab)";
    case ConnectionPhase::Pairing:
      return address.empty() ? "QarOS: pairing with the Hub ..." : "QarOS: pairing with " +
             address + " ...";
    case ConnectionPhase::Running: {
        const char * how = mode == Resolved::Remote ? "paired" : "local app";
        if (input.attaching) {
          return std::string("QarOS: joined (") + how + ") - setting up the session ...";
        }
        return std::string("QarOS: connected (") + how + ")" +
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

std::string identity_text_of(const ConnectionInput & input, Resolved mode)
{
  if (input.phase == ConnectionPhase::Preview) {
    return "preview (no QAROS)";
  }
  if (mode == Resolved::Local) {
    const bool approved = input.phase == ConnectionPhase::Running ||
      input.phase == ConnectionPhase::Reconnecting;
    return approved ?
           "This PC: approved local app (" + input.program_name + ")" :
           "This PC: local app (" + input.program_name + "), approved once in the QAROS Hub";
  }
  if (mode == Resolved::Remote) {
    if (!input.has_remote_identity) {
      return "Not paired";
    }
    std::string text = "Paired with " + hub_address(input.identity_host, input.identity_port);
    if (!input.pairing_id.empty()) {
      text += " (id " + input.pairing_id + ")";
    }
    return text;
  }
  return "Not connected yet";
}

}  // namespace

const char * hub_choice_word(HubChoice choice)
{
  switch (choice) {
    case HubChoice::Auto:
      return "auto";
    case HubChoice::Local:
      return "local";
    case HubChoice::Remote:
      return "remote";
  }
  return "auto";
}

HubChoice hub_choice_from_word(const std::string & word)
{
  if (word == "local") {
    return HubChoice::Local;
  }
  if (word == "remote") {
    return HubChoice::Remote;
  }
  return HubChoice::Auto;
}

bool remote_in_force(const ConnectionInput & input)
{
  return resolve(input) == Resolved::Remote;
}

std::string format_countdown(int seconds)
{
  if (seconds < 0) {
    seconds = 0;
  }
  return std::to_string(seconds / 60) + ":" + two_digits(seconds % 60);
}

bool parse_hub_port(const std::string & text, int & out_port)
{
  if (text.empty() || text.size() > 5) {
    return false;
  }
  int value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  if (value < 1 || value > 65535) {
    return false;
  }
  out_port = value;
  return true;
}

std::string hub_address(const std::string & host, int port)
{
  if (host.empty()) {
    return std::string();
  }
  return port > 0 ? host + ":" + std::to_string(port) : host;
}

ConnectionView make_connection_view(const ConnectionInput & input)
{
  const Resolved mode = resolve(input);
  const ConnectionPhase phase = input.phase;
  ConnectionView view;
  view.state_text = state_text_of(input, mode);
  view.identity_text = identity_text_of(input, mode);
  view.remote_fields_visible = mode == Resolved::Remote;

  const bool no_session_activity = phase == ConnectionPhase::Idle ||
    phase == ConnectionPhase::Failed || phase == ConnectionPhase::NeedsCode ||
    phase == ConnectionPhase::Running || phase == ConnectionPhase::Reconnecting;

  view.code_enabled = phase == ConnectionPhase::NeedsCode;
  view.pair_enabled = phase == ConnectionPhase::NeedsCode;
  // The wait for a code ends with Cancel as well: the way back to "not connected".
  view.cancel_enabled = phase == ConnectionPhase::Loading ||
    phase == ConnectionPhase::WaitingForApproval || phase == ConnectionPhase::Rejoining ||
    phase == ConnectionPhase::NeedsCode || phase == ConnectionPhase::Pairing;

  if (mode == Resolved::Local) {
    view.forget_text = "Approval: revoke in the Visualizer";
    view.forget_tooltip = kForgetLocalToolTip;
    view.forget_enabled = false;
  } else {
    view.forget_text = "Forget pairing";
    view.forget_enabled = mode == Resolved::Remote && input.has_remote_identity &&
      no_session_activity;
    if (phase == ConnectionPhase::Preview) {
      view.forget_enabled = false;
      view.forget_tooltip = "The preview has no QAROS session and no pairing.";
    } else if (mode == Resolved::Unknown || !input.has_remote_identity) {
      view.forget_tooltip = "No pairing with another PC's Hub is stored.";
    } else if (!no_session_activity) {
      view.forget_tooltip = "Wait for the join to end, or press Cancel.";
    } else {
      view.forget_tooltip =
        "Erase the stored pairing. The next connect needs a new code from the Hub.";
    }
  }

  if (mode == Resolved::Remote) {
    view.invite_enabled = false;
    view.invite_tooltip = kInviteRemoteToolTip;
  } else if (phase == ConnectionPhase::Running && input.attaching) {
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
