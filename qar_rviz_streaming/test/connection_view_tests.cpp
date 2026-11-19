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
using qar_rviz_streaming::make_connection_view;

ConnectionInput input_of(ConnectionPhase phase)
{
  ConnectionInput input;
  input.phase = phase;
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

TEST(ConnectionView, RunningCountsTheTargets)
{
  ConnectionInput local = input_of(ConnectionPhase::Running);
  local.target_count = 3;
  local.served_count = 2;
  EXPECT_EQ(
    make_connection_view(local).state_text,
    "QarOS: connected (local app) - 3 target(s), 2 served");
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

  // Once it is done the ordinary text and the invite button are back.
  local.attaching = false;
  view = make_connection_view(local);
  EXPECT_EQ(view.state_text, "QarOS: connected (local app) - 1 target(s), 0 served");
  EXPECT_TRUE(view.invite_enabled);
}

TEST(ConnectionView, CancelIsOfferedWhileTheJoinIsUnderWay)
{
  for (const ConnectionPhase phase : {ConnectionPhase::Loading,
      ConnectionPhase::WaitingForApproval, ConnectionPhase::Reconnecting, ConnectionPhase::Lost})
  {
    EXPECT_TRUE(make_connection_view(input_of(phase)).cancel_enabled) << static_cast<int>(phase);
  }
  for (const ConnectionPhase phase : {ConnectionPhase::Preview, ConnectionPhase::Idle,
      ConnectionPhase::Running, ConnectionPhase::Stopping, ConnectionPhase::Failed})
  {
    EXPECT_FALSE(make_connection_view(input_of(phase)).cancel_enabled) << static_cast<int>(phase);
  }
}

TEST(ConnectionView, TheIdentityIsThisPc)
{
  // Before the join, and in the preview, which has no join at all.
  for (const ConnectionPhase phase : {ConnectionPhase::Preview, ConnectionPhase::Idle,
      ConnectionPhase::Loading, ConnectionPhase::WaitingForApproval,
      ConnectionPhase::Stopping, ConnectionPhase::Failed})
  {
    EXPECT_EQ(make_connection_view(input_of(phase)).identity_text, "This PC (not joined yet)")
      << static_cast<int>(phase);
  }
  // Joined: the program the person finds in the Hub when revoking the approval.
  ConnectionInput local = input_of(ConnectionPhase::Running);
  local.program_name = "rviz2.exe";
  for (const ConnectionPhase phase : {ConnectionPhase::Running, ConnectionPhase::Reconnecting,
      ConnectionPhase::Lost})
  {
    local.phase = phase;
    EXPECT_EQ(
      make_connection_view(local).identity_text, "This PC: approved local app (rviz2.exe)")
      << static_cast<int>(phase);
  }
}

TEST(ConnectionView, InvitesNeedASession)
{
  // Running: enabled, unless one is in flight.
  ConnectionInput running = input_of(ConnectionPhase::Running);
  ConnectionView view = make_connection_view(running);
  EXPECT_TRUE(view.invite_enabled);
  running.invite_in_flight = true;
  view = make_connection_view(running);
  EXPECT_FALSE(view.invite_enabled);
  EXPECT_EQ(view.invite_tooltip, "An invite is in progress.");

  // No session: disabled, and the tool tip says what it needs.
  for (const ConnectionPhase phase : {ConnectionPhase::Preview, ConnectionPhase::Idle,
      ConnectionPhase::Loading, ConnectionPhase::Reconnecting, ConnectionPhase::Lost,
      ConnectionPhase::Failed})
  {
    view = make_connection_view(input_of(phase));
    EXPECT_FALSE(view.invite_enabled) << static_cast<int>(phase);
    EXPECT_NE(view.invite_tooltip.find("needs a QarOS session"), std::string::npos)
      << static_cast<int>(phase);
  }
}

TEST(ConnectionView, ReconnectingCountsTheSilentSeconds)
{
  ConnectionInput input = input_of(ConnectionPhase::Reconnecting);
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: Hub not reachable - waiting for it to come back");
  input.hub_silent_s = 0;
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: Hub not reachable - waiting for it to come back (0 s)");
  input.hub_silent_s = 17;
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: Hub not reachable - waiting for it to come back (17 s)");
}

TEST(ConnectionView, LostSaysWhenTheNextAttemptStarts)
{
  ConnectionInput input = input_of(ConnectionPhase::Lost);
  input.auto_rejoin = true;
  input.rejoin_attempt = 3;
  input.rejoin_in_s = 8;
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: the QAROS Hub is gone - joining it again in 8 s (attempt 3); RViz keeps working");
  // An attempt that runs has no countdown.
  input.rejoin_in_s = -1;
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: the QAROS Hub is gone - joining it again (attempt 3); RViz keeps working");
}

TEST(ConnectionView, LostWhileReleasingSaysSo)
{
  ConnectionInput input = input_of(ConnectionPhase::Lost);
  input.releasing = true;
  input.auto_rejoin = true;
  input.rejoin_in_s = 4;
  EXPECT_EQ(
    make_connection_view(input).state_text,
    "QarOS: the QAROS Hub is gone - releasing the session ...");
}

TEST(ConnectionView, AnAutomaticAttemptSaysJoiningAgainUnlessAPromptIsOpen)
{
  {
    const ConnectionPhase phase = ConnectionPhase::WaitingForApproval;
    ConnectionInput input = input_of(phase);
    input.auto_rejoin = true;
    input.rejoin_attempt = 2;
    ConnectionView view = make_connection_view(input);
    EXPECT_EQ(view.state_text, "QarOS: joining the QAROS Hub again (attempt 2) ...");
    EXPECT_EQ(view.cancel_text, "Stop rejoining");
    EXPECT_TRUE(view.cancel_enabled);

    // The Hub asks for Allow: the person reads the approval text with its countdown.
    input.rejoin_prompt_open = true;
    input.approval_seconds_left = 75;
    view = make_connection_view(input);
    EXPECT_NE(view.state_text.find("click Allow in the QAROS window"), std::string::npos)
      << static_cast<int>(phase);
    EXPECT_NE(view.state_text.find("1:15 left"), std::string::npos) << static_cast<int>(phase);
    EXPECT_EQ(view.cancel_text, "Cancel");
    EXPECT_TRUE(view.cancel_enabled);
  }
  // A join that the person started keeps today's words.
  ConnectionInput manual = input_of(ConnectionPhase::WaitingForApproval);
  EXPECT_EQ(
    make_connection_view(manual).state_text,
    "QarOS: waiting for approval in the QAROS Hub - click Allow in the QAROS window");
  EXPECT_EQ(make_connection_view(manual).cancel_text, "Cancel");
}

TEST(ConnectionView, CancelIsJoinAgainWhileReconnectingAndStopRejoiningWhileLost)
{
  ConnectionView view = make_connection_view(input_of(ConnectionPhase::Reconnecting));
  EXPECT_EQ(view.cancel_text, "Join again");
  EXPECT_TRUE(view.cancel_enabled);
  EXPECT_NE(view.cancel_tooltip.find("release it and join the Hub again"), std::string::npos);

  for (const bool releasing : {true, false}) {
    ConnectionInput lost = input_of(ConnectionPhase::Lost);
    lost.releasing = releasing;
    view = make_connection_view(lost);
    EXPECT_EQ(view.cancel_text, "Stop rejoining") << releasing;
    EXPECT_TRUE(view.cancel_enabled) << releasing;
    EXPECT_NE(view.cancel_tooltip.find("Stop joining the QAROS Hub again"), std::string::npos)
      << releasing;
  }

  // The other phases keep the plain button.
  for (const ConnectionPhase phase : {ConnectionPhase::Idle, ConnectionPhase::Running,
      ConnectionPhase::Loading, ConnectionPhase::Failed})
  {
    view = make_connection_view(input_of(phase));
    EXPECT_EQ(view.cancel_text, "Cancel") << static_cast<int>(phase);
    EXPECT_TRUE(view.cancel_tooltip.empty()) << static_cast<int>(phase);
  }
}

TEST(ConnectionView, ADegradedStreamIsAddedToTheRunningText)
{
  ConnectionInput input = input_of(ConnectionPhase::Running);
  input.target_count = 2;
  input.served_count = 1;
  const std::string plain = make_connection_view(input).state_text;
  EXPECT_EQ(plain, "QarOS: connected (local app) - 2 target(s), 1 served");

  input.degraded_text = "the receiver is reconnecting (5 s); the picture comes back by itself";
  const ConnectionView view = make_connection_view(input);
  EXPECT_EQ(
    view.state_text,
    plain + "; the receiver is reconnecting (5 s); the picture comes back by itself");
  EXPECT_EQ(view.cancel_text, "Cancel");
  EXPECT_FALSE(view.cancel_enabled);

  // The text is for a session that runs; no other phase carries it.
  input.phase = ConnectionPhase::Idle;
  EXPECT_EQ(make_connection_view(input).state_text, "QarOS: not connected");
}

TEST(ConnectionView, InviteIsOffWhileLostOrReconnecting)
{
  for (const ConnectionPhase phase : {ConnectionPhase::Reconnecting, ConnectionPhase::Lost}) {
    ConnectionInput input = input_of(phase);
    input.auto_rejoin = true;
    const ConnectionView view = make_connection_view(input);
    EXPECT_FALSE(view.invite_enabled) << static_cast<int>(phase);
    EXPECT_NE(view.invite_tooltip.find("needs a QarOS session"), std::string::npos)
      << static_cast<int>(phase);
  }
}

TEST(ConnectionView, LostReadsAsJoinedForTheIdentityLine)
{
  const ConnectionInput input = input_of(ConnectionPhase::Lost);
  EXPECT_EQ(
    make_connection_view(input).identity_text, "This PC: approved local app (rviz2.exe)");
}

TEST(ConnectionView, TheAbandonCapTextSaysRestartRviz)
{
  ConnectionInput input = input_of(ConnectionPhase::Failed);
  input.failure_message =
    "QAROS stopped answering inside a call (qar_session_get_my_spec) twice. RViz keeps working; "
    "restart RViz to stream again.";
  const ConnectionView view = make_connection_view(input);
  EXPECT_EQ(view.state_text, "QarOS: " + input.failure_message);
  EXPECT_NE(view.state_text.find("restart RViz to stream again."), std::string::npos);
  EXPECT_EQ(view.cancel_text, "Cancel");
  EXPECT_FALSE(view.cancel_enabled);
}
