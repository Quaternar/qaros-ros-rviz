// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__CONNECTION_VIEW_HPP_
#define QAR_RVIZ_STREAMING__CONNECTION_VIEW_HPP_

/// @file connection_view.hpp
/// @brief What the Connection tab and the status strip say about the QAROS session, as pure
/// functions of a few facts. Standard library only: no Qt, no RViz, no QAROS call.
///
/// The plugin always joins the QAROS Hub of this PC.
///
/// The panel collects the facts (the state of the session owner, the targets) into a
/// @ref ConnectionInput and gives the @ref ConnectionView it gets back to the widgets. Every
/// text that the person at the keyboard reads for the join is made here, so a test can check all
/// of them without a window.

#include <cstddef>
#include <string>

namespace qar_rviz_streaming
{

/// @brief Where the plugin is in joining the QAROS Hub. The owner's states plus the preview,
/// which has no owner at all.
enum class ConnectionPhase
{
  Preview,             ///< No QAROS in this run (QAR_PANEL_LIVE_STREAM is not set).
  Idle,                ///< Not connected.
  Loading,             ///< Loading the runtime.
  WaitingForApproval,  ///< Waiting for Allow in the Hub.
  Running,             ///< Joined.
  Reconnecting,        ///< Joined, but the Hub does not answer.
  Lost,                ///< The session was given up; it is released and joined again by itself.
  Stopping,            ///< Releasing the session.
  Failed               ///< Stopped by an error.
};

/// @brief The facts the Connection tab is made from.
struct ConnectionInput
{
  ConnectionPhase phase = ConnectionPhase::Idle;
  /// Seconds left of the approval wait, or negative when not counting.
  int approval_seconds_left = -1;
  std::size_t target_count = 0;
  std::size_t served_count = 0;
  /// The plain-words cause of a Failed phase.
  std::string failure_message;
  /// A device invite is in flight.
  bool invite_in_flight = false;
  /// Joined, with streaming paused: the session stays open, "Connect to QarOS" resumes.
  bool streaming_paused = false;
  /// Joined, and the render factory is still making the app volume of the session on the owner's
  /// thread. Streaming, invites and panels wait for it.
  bool attaching = false;
  /// The name of this program as the Hub shows it ("rviz2.exe").
  std::string program_name = "rviz2.exe";
  /// Reconnecting: seconds the Hub has been silent; -1 when unknown.
  int hub_silent_s = -1;
  /// The owner joins the Hub again by itself (Lost, or an attempt that it started).
  bool auto_rejoin = false;
  /// The number this text gives to the automatic attempt: the one that runs, or the next one
  /// while the owner waits.
  int rejoin_attempt = 0;
  /// Seconds to the next automatic attempt; -1 while one runs.
  int rejoin_in_s = -1;
  /// The Hub showed its approval prompt for the automatic attempt.
  bool rejoin_prompt_open = false;
  /// Lost, before the consumer handed the session back to the owner.
  bool releasing = false;
  /// Running: what is wrong with a stream (connection_health.hpp); empty when nothing is.
  std::string degraded_text;
};

/// @brief What the widgets show.
struct ConnectionView
{
  /// The text of the status strip and of the State label.
  std::string state_text;
  /// The text of the Identity label.
  std::string identity_text;

  bool cancel_enabled = false;
  /// The text and the tool tip of the cancel button; the tool tip is empty for the default.
  std::string cancel_text = "Cancel";
  std::string cancel_tooltip;

  /// "Invite a device" can be pressed.
  bool invite_enabled = false;
  std::string invite_tooltip;
};

/// @brief The view of @p input.
ConnectionView make_connection_view(const ConnectionInput & input);

/// @brief "m:ss" for a count of seconds (negative reads as 0).
std::string format_countdown(int seconds);

/// The tool tip of the Invite button while streaming is paused.
constexpr const char * kInvitePausedToolTip =
  "Streaming is paused: press Connect to QarOS to resume, then invite the device.";

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__CONNECTION_VIEW_HPP_
