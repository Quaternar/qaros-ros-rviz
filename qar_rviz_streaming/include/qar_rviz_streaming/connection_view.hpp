// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__CONNECTION_VIEW_HPP_
#define QAR_RVIZ_STREAMING__CONNECTION_VIEW_HPP_

/// @file connection_view.hpp
/// @brief What the Connection tab and the status strip say about the QAROS session, as pure
/// functions of a few facts. Standard library only: no Qt, no RViz, no QAROS call.
///
/// The panel collects the facts (the state of the session owner, the settings in force, the
/// targets) into a @ref ConnectionInput and gives the @ref ConnectionView it gets back to the
/// widgets. Every text that the person at the keyboard reads for the join is made here, so a test
/// can check all of them without a window.

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
  Loading,             ///< Loading the runtime, choosing the mode.
  WaitingForApproval,  ///< This-PC mode: waiting for Allow in the Hub.
  Rejoining,           ///< Joining again with the stored pairing.
  NeedsCode,           ///< Waiting for host, port and the pairing code.
  Pairing,             ///< Pairing with the code.
  Running,             ///< Joined.
  Reconnecting,        ///< Joined, but the Hub does not answer.
  Stopping,            ///< Releasing the session.
  Failed               ///< Stopped by an error.
};

/// @brief The "Hub Mode" setting.
enum class HubChoice
{
  Auto,
  Local,
  Remote
};

/// @brief "auto", "local" or "remote".
const char * hub_choice_word(HubChoice choice);
/// @brief The choice a setting word names; any other text reads as Auto.
HubChoice hub_choice_from_word(const std::string & word);

/// @brief The facts the Connection tab is made from.
struct ConnectionInput
{
  ConnectionPhase phase = ConnectionPhase::Idle;
  /// What the "Hub Mode" setting says.
  HubChoice mode_setting = HubChoice::Auto;
  /// True once the owner resolved Auto to a mode (it does so while loading); @ref remote_resolved
  /// is that mode.
  bool mode_resolved = false;
  bool remote_resolved = false;
  /// A pairing with another PC's Hub is stored for this program.
  bool has_remote_identity = false;
  /// Host and port of the Hub of another PC, as typed in the fields; a port of 0 is unset.
  std::string host;
  int port = 0;
  /// Host, port and the first eight characters of the id of the stored pairing, for the identity
  /// line; empty or 0 when unknown.
  std::string identity_host;
  int identity_port = 0;
  std::string pairing_id;
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
};

/// @brief What the widgets show.
struct ConnectionView
{
  /// The text of the status strip and of the State label.
  std::string state_text;
  /// The text of the Identity label.
  std::string identity_text;

  /// Host and port fields are shown (the Hub of another PC is in play).
  bool remote_fields_visible = false;
  bool code_enabled = false;
  bool pair_enabled = false;
  bool cancel_enabled = false;
  bool forget_enabled = false;
  std::string forget_text;
  std::string forget_tooltip;

  /// "Invite a device" can be pressed.
  bool invite_enabled = false;
  std::string invite_tooltip;
};

/// @brief The view of @p input.
ConnectionView make_connection_view(const ConnectionInput & input);

/// @brief The mode in force: Remote when the setting says so, or when it is Auto and the owner
/// resolved it to the Hub of another PC; Local likewise. Auto that the owner has not resolved yet
/// reads as the pairing that is stored (Remote) or as Auto (nothing known).
/// @return true when the join is with the Hub of another PC.
bool remote_in_force(const ConnectionInput & input);

/// @brief "m:ss" for a count of seconds (negative reads as 0).
std::string format_countdown(int seconds);

/// @brief Reads a Hub port: digits only, 1 to 65535.
bool parse_hub_port(const std::string & text, int & out_port);

/// @brief "host:port", or just "host" when the port is unset, or empty when both are.
std::string hub_address(const std::string & host, int port);

/// The tool tip of the Forget button while this-PC mode is in force.
constexpr const char * kForgetLocalToolTip =
  "This PC approved this program in the QAROS Hub. Revoke the approval in the Visualizer "
  "(Source Applications, Approved apps).";

/// The tool tip of the Invite button while the Hub of another PC is in force.
/// The tool tip of the Invite button while streaming is paused.
constexpr const char * kInvitePausedToolTip =
  "Streaming is paused: press Connect to QarOS to resume, then invite the device.";

constexpr const char * kInviteRemoteToolTip =
  "Device invites are served only to apps that joined the Hub on its own PC; invite devices "
  "from the Visualizer's Connection panel, Devices, instead.";

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__CONNECTION_VIEW_HPP_
