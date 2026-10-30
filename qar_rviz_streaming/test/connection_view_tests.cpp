// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The texts and the states of the Connection tab and of the status strip (connection_view.hpp):
// pure functions of a few facts, so every state of the join is checked here without a window.

#include "qar_rviz_streaming/connection_view.hpp"

#include <string>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::ConnectionInput;
using qar_rviz_streaming::ConnectionPhase;
using qar_rviz_streaming::ConnectionView;
using qar_rviz_streaming::HubChoice;
using qar_rviz_streaming::make_connection_view;

ConnectionInput input_of(ConnectionPhase phase, HubChoice mode = HubChoice::Local)
{
  ConnectionInput input;
  input.phase = phase;
  input.mode_setting = mode;
  return input;
}

ConnectionInput remote_input(ConnectionPhase phase, bool paired)
{
  ConnectionInput input = input_of(phase, HubChoice::Remote);
  input.host = "hub.example.org";
  input.port = 19120;
  input.has_remote_identity = paired;
  if (paired) {
    input.identity_host = "hub.example.org";
    input.identity_port = 19120;
    input.pairing_id = "1a2b3c4d";
  }
  return input;
}

}  // namespace

TEST(ConnectionView, EveryPhaseSaysItsStateInPlainWords)
{
  EXPECT_EQ(
    make_connection_view(input_of(ConnectionPhase::Preview)).state_text,
    "QarOS: preview mode - set QAR_PANEL_LIVE_STREAM to connect");
  EXPECT_EQ(make_connection_view(input_of(ConnectionPhase::Idle)).state_text,
    "QarOS: not connected");
  EXPECT_EQ(
    make_connection_view(input_of(ConnectionPhase::Loading)).state_text,
    "QarOS: loading the QAROS runtime ...");
  EXPECT_EQ(
    make_connection_view(input_of(ConnectionPhase::Reconnecting)).state_text,
    "QarOS: Hub not reachable - waiting for it to come back");
  EXPECT_EQ(
    make_connection_view(input_of(ConnectionPhase::Stopping)).state_text,
    "QarOS: disconnecting ...");

  ConnectionInput failed = input_of(ConnectionPhase::Failed);
  failed.failure_message = "Nobody answered the approval prompt in the QAROS Hub in time.";
  EXPECT_EQ(
    make_connection_view(failed).state_text,
    "QarOS: Nobody answered the approval prompt in the QAROS Hub in time.");
  failed.failure_message.clear();
  EXPECT_EQ(make_connection_view(failed).state_text, "QarOS: the join failed");
}

TEST(ConnectionView, TheApprovalWaitCountsDown)
{
  ConnectionInput input = input_of(ConnectionPhase::WaitingForApproval);
  input.approval_seconds_left = 95;
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: waiting for approval in the QAROS Hub - click Allow in the QAROS window "
    "(1:35 left)");
  input.approval_seconds_left = 7;
  EXPECT_NE(make_connection_view(input).state_text.find("(0:07 left)"), std::string::npos);
  input.approval_seconds_left = -1;
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: waiting for approval in the QAROS Hub - click Allow in the QAROS window");
  EXPECT_EQ(qar_rviz_streaming::format_countdown(600), "10:00");
  EXPECT_EQ(qar_rviz_streaming::format_countdown(0), "0:00");
  EXPECT_EQ(qar_rviz_streaming::format_countdown(-5), "0:00");
}

TEST(ConnectionView, RemoteStatesNameTheHub)
{
  EXPECT_EQ(
    make_connection_view(remote_input(ConnectionPhase::Rejoining, true)).state_text,
    "QarOS: rejoining hub.example.org:19120 ...");
  EXPECT_EQ(
    make_connection_view(remote_input(ConnectionPhase::NeedsCode, false)).state_text,
    "QarOS: type the pairing code of the Hub at hub.example.org:19120 (Connection tab)");
  EXPECT_EQ(
    make_connection_view(remote_input(ConnectionPhase::Pairing, false)).state_text,
    "QarOS: pairing with hub.example.org:19120 ...");

  // Before the person has typed a host, the states do not name a Hub.
  ConnectionInput bare = input_of(ConnectionPhase::NeedsCode, HubChoice::Remote);
  EXPECT_EQ(
    make_connection_view(bare).state_text,
    "QarOS: type the host, the port and the pairing code of the Hub (Connection tab)");
  bare.phase = ConnectionPhase::Pairing;
  EXPECT_EQ(make_connection_view(bare).state_text, "QarOS: pairing with the Hub ...");
  bare.phase = ConnectionPhase::Rejoining;
  EXPECT_EQ(make_connection_view(bare).state_text, "QarOS: rejoining the Hub ...");
}

TEST(ConnectionView, RunningCountsTheTargets)
{
  ConnectionInput local = input_of(ConnectionPhase::Running);
  local.target_count = 3;
  local.served_count = 2;
  EXPECT_EQ(
    make_connection_view(local).state_text,
    "QarOS: connected (local app) - 3 target(s), 2 served");
  ConnectionInput remote = remote_input(ConnectionPhase::Running, true);
  EXPECT_EQ(
    make_connection_view(remote).state_text,
    "QarOS: connected (paired) - 0 target(s), 0 served");
}

TEST(ConnectionView, APausedSessionStaysConnectedAndSaysSo)
{
  ConnectionInput local = input_of(ConnectionPhase::Running);
  local.target_count = 2;
  local.served_count = 2;
  local.streaming_paused = true;
  ConnectionView view = make_connection_view(local);
  EXPECT_EQ(
    view.state_text, "QarOS: connected (local app), streaming paused - 2 target(s), 2 served");
  EXPECT_EQ(view.identity_text, "This PC: approved local app (rviz2.exe)");
  // Streaming is off, so a device cannot be invited until it is resumed.
  EXPECT_FALSE(view.invite_enabled);
  EXPECT_EQ(view.invite_tooltip, qar_rviz_streaming::kInvitePausedToolTip);

  ConnectionInput remote = remote_input(ConnectionPhase::Running, true);
  remote.streaming_paused = true;
  view = make_connection_view(remote);
  EXPECT_EQ(
    view.state_text, "QarOS: connected (paired), streaming paused - 0 target(s), 0 served");
  // The pairing can still be forgotten from a paused session.
  EXPECT_TRUE(view.forget_enabled);
  EXPECT_FALSE(view.cancel_enabled);
}

TEST(ConnectionView, ASessionBeingSetUpIsJoinedButNotYetStreaming)
{
  ConnectionInput local = input_of(ConnectionPhase::Running);
  local.target_count = 1;
  local.attaching = true;
  ConnectionView view = make_connection_view(local);
  EXPECT_EQ(view.state_text, "QarOS: joined (local app) - setting up the session ...");
  // No invite while the app volume is being made; the person is told why.
  EXPECT_FALSE(view.invite_enabled);
  EXPECT_EQ(
    view.invite_tooltip,
    "The session is being set up; an invite is possible when that is done.");
  EXPECT_FALSE(view.cancel_enabled);

  ConnectionInput remote = remote_input(ConnectionPhase::Running, true);
  remote.attaching = true;
  EXPECT_EQ(
    make_connection_view(remote).state_text,
    "QarOS: joined (paired) - setting up the session ...");

  // Once it is done the ordinary text and the invite button are back.
  local.attaching = false;
  view = make_connection_view(local);
  EXPECT_EQ(view.state_text, "QarOS: connected (local app) - 1 target(s), 0 served");
  EXPECT_TRUE(view.invite_enabled);
}

TEST(ConnectionView, RemoteFieldsShowOnlyForAnotherPcsHub)
{
  for (const ConnectionPhase phase : {ConnectionPhase::Preview, ConnectionPhase::Idle,
      ConnectionPhase::Running})
  {
    EXPECT_FALSE(make_connection_view(input_of(phase, HubChoice::Local)).remote_fields_visible);
    EXPECT_TRUE(make_connection_view(input_of(phase, HubChoice::Remote)).remote_fields_visible);
  }
  // Automatic: nothing is known before the owner chose; a stored pairing or the owner's choice
  // decides afterwards.
  ConnectionInput automatic = input_of(ConnectionPhase::Idle, HubChoice::Auto);
  EXPECT_FALSE(make_connection_view(automatic).remote_fields_visible);
  EXPECT_FALSE(qar_rviz_streaming::remote_in_force(automatic));
  automatic.has_remote_identity = true;
  EXPECT_TRUE(make_connection_view(automatic).remote_fields_visible);
  automatic.has_remote_identity = false;
  automatic.mode_resolved = true;
  automatic.remote_resolved = false;
  EXPECT_FALSE(make_connection_view(automatic).remote_fields_visible);
  automatic.remote_resolved = true;
  EXPECT_TRUE(make_connection_view(automatic).remote_fields_visible);
  EXPECT_TRUE(qar_rviz_streaming::remote_in_force(automatic));
}

TEST(ConnectionView, TheCodeFieldIsEnabledOnlyWhenTheHubWaitsForACode)
{
  for (const ConnectionPhase phase : {ConnectionPhase::Preview, ConnectionPhase::Idle,
      ConnectionPhase::Loading, ConnectionPhase::WaitingForApproval, ConnectionPhase::Rejoining,
      ConnectionPhase::Pairing, ConnectionPhase::Running, ConnectionPhase::Reconnecting,
      ConnectionPhase::Stopping, ConnectionPhase::Failed})
  {
    const ConnectionView view = make_connection_view(remote_input(phase, true));
    EXPECT_FALSE(view.code_enabled) << static_cast<int>(phase);
    EXPECT_FALSE(view.pair_enabled) << static_cast<int>(phase);
  }
  const ConnectionView needs = make_connection_view(
    remote_input(ConnectionPhase::NeedsCode, false));
  EXPECT_TRUE(needs.code_enabled);
  EXPECT_TRUE(needs.pair_enabled);
}

TEST(ConnectionView, CancelIsOfferedWhileTheJoinIsUnderWay)
{
  // Under way, and waiting for a code (the way back to "not connected").
  for (const ConnectionPhase phase : {ConnectionPhase::Loading,
      ConnectionPhase::WaitingForApproval, ConnectionPhase::Rejoining, ConnectionPhase::NeedsCode,
      ConnectionPhase::Pairing})
  {
    EXPECT_TRUE(make_connection_view(input_of(phase)).cancel_enabled) << static_cast<int>(phase);
  }
  for (const ConnectionPhase phase : {ConnectionPhase::Preview, ConnectionPhase::Idle,
      ConnectionPhase::Running, ConnectionPhase::Reconnecting, ConnectionPhase::Stopping,
      ConnectionPhase::Failed})
  {
    EXPECT_FALSE(make_connection_view(input_of(phase)).cancel_enabled) << static_cast<int>(phase);
  }
}

TEST(ConnectionView, ForgetIsForAPairingOnly)
{
  // This-PC mode: the approval lives in the Hub, so the button says where to revoke it.
  const ConnectionView local = make_connection_view(input_of(ConnectionPhase::Running));
  EXPECT_FALSE(local.forget_enabled);
  EXPECT_EQ(local.forget_text, "Approval: revoke in the Visualizer");
  EXPECT_EQ(local.forget_tooltip, qar_rviz_streaming::kForgetLocalToolTip);

  // Another PC's Hub with a stored pairing: enabled when no join is under way.
  for (const ConnectionPhase phase : {ConnectionPhase::Idle, ConnectionPhase::NeedsCode,
      ConnectionPhase::Running, ConnectionPhase::Reconnecting, ConnectionPhase::Failed})
  {
    const ConnectionView view = make_connection_view(remote_input(phase, true));
    EXPECT_TRUE(view.forget_enabled) << static_cast<int>(phase);
    EXPECT_EQ(view.forget_text, "Forget pairing");
  }
  for (const ConnectionPhase phase : {ConnectionPhase::Loading, ConnectionPhase::Rejoining,
      ConnectionPhase::Pairing, ConnectionPhase::Stopping})
  {
    EXPECT_FALSE(make_connection_view(remote_input(phase, true)).forget_enabled)
      << static_cast<int>(phase);
  }
  // Nothing stored: nothing to forget.
  const ConnectionView unpaired = make_connection_view(remote_input(ConnectionPhase::Idle, false));
  EXPECT_FALSE(unpaired.forget_enabled);
  EXPECT_EQ(unpaired.forget_tooltip, "No pairing with another PC's Hub is stored.");
  // The preview has no session at all.
  EXPECT_FALSE(make_connection_view(remote_input(ConnectionPhase::Preview, true)).forget_enabled);
}

TEST(ConnectionView, TheIdentityLineSaysWhoThisProgramIs)
{
  EXPECT_EQ(
    make_connection_view(input_of(ConnectionPhase::Preview, HubChoice::Auto)).identity_text,
    "preview (no QAROS)");
  ConnectionInput local = input_of(ConnectionPhase::Running);
  local.program_name = "rviz2.exe";
  EXPECT_EQ(
    make_connection_view(local).identity_text, "This PC: approved local app (rviz2.exe)");
  local.phase = ConnectionPhase::Idle;
  EXPECT_EQ(
    make_connection_view(local).identity_text,
    "This PC: local app (rviz2.exe), approved once in the QAROS Hub");
  EXPECT_EQ(
    make_connection_view(remote_input(ConnectionPhase::Idle, true)).identity_text,
    "Paired with hub.example.org:19120 (id 1a2b3c4d)");
  EXPECT_EQ(
    make_connection_view(remote_input(ConnectionPhase::NeedsCode, false)).identity_text,
    "Not paired");
  EXPECT_EQ(
    make_connection_view(input_of(ConnectionPhase::Idle, HubChoice::Auto)).identity_text,
    "Not connected yet");
}

TEST(ConnectionView, InvitesAreForThisPcsHubAndNeedASession)
{
  // Running in this-PC mode: enabled, unless one is in flight.
  ConnectionInput running = input_of(ConnectionPhase::Running);
  ConnectionView view = make_connection_view(running);
  EXPECT_TRUE(view.invite_enabled);
  running.invite_in_flight = true;
  view = make_connection_view(running);
  EXPECT_FALSE(view.invite_enabled);
  EXPECT_EQ(view.invite_tooltip, "An invite is in progress.");

  // Another PC's Hub refuses them, and the tool tip says where to invite instead.
  view = make_connection_view(remote_input(ConnectionPhase::Running, true));
  EXPECT_FALSE(view.invite_enabled);
  EXPECT_EQ(view.invite_tooltip, qar_rviz_streaming::kInviteRemoteToolTip);

  // No session: disabled, and the tool tip says what it needs.
  for (const ConnectionPhase phase : {ConnectionPhase::Preview, ConnectionPhase::Idle,
      ConnectionPhase::Loading, ConnectionPhase::Reconnecting, ConnectionPhase::Failed})
  {
    view = make_connection_view(input_of(phase));
    EXPECT_FALSE(view.invite_enabled) << static_cast<int>(phase);
    EXPECT_NE(view.invite_tooltip.find("needs a QarOS session"), std::string::npos)
      << static_cast<int>(phase);
  }
}

TEST(ConnectionView, TheHubWordsRoundTrip)
{
  for (const HubChoice choice : {HubChoice::Auto, HubChoice::Local, HubChoice::Remote}) {
    EXPECT_EQ(
      qar_rviz_streaming::hub_choice_from_word(qar_rviz_streaming::hub_choice_word(choice)),
      choice);
  }
  EXPECT_EQ(qar_rviz_streaming::hub_choice_from_word("nonsense"), HubChoice::Auto);
  EXPECT_EQ(qar_rviz_streaming::hub_choice_from_word(""), HubChoice::Auto);
}

TEST(ConnectionView, HubPortsAreWholeNumbersFromOneToSixtyFiveThousand)
{
  int port = 0;
  EXPECT_TRUE(qar_rviz_streaming::parse_hub_port("19120", port));
  EXPECT_EQ(port, 19120);
  EXPECT_TRUE(qar_rviz_streaming::parse_hub_port("1", port));
  EXPECT_EQ(port, 1);
  EXPECT_TRUE(qar_rviz_streaming::parse_hub_port("65535", port));
  EXPECT_EQ(port, 65535);
  port = 77;
  for (const char * bad : {"", "0", "65536", "-1", "12a", " 80", "123456", "1.5"}) {
    EXPECT_FALSE(qar_rviz_streaming::parse_hub_port(bad, port)) << bad;
    EXPECT_EQ(port, 77) << "a refused text leaves the port alone: " << bad;
  }
  EXPECT_EQ(qar_rviz_streaming::hub_address("hub", 19120), "hub:19120");
  EXPECT_EQ(qar_rviz_streaming::hub_address("hub", 0), "hub");
  EXPECT_EQ(qar_rviz_streaming::hub_address("", 19120), "");
}
