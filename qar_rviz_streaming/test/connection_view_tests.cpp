// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The texts and the states of the Connection tab and of the status strip (connection_view.hpp):
// pure functions of a few facts, so every state of the join is checked here without a window.
// The plugin joins the Hub of this PC as a local app; there is no pairing with another PC.

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
  const ConnectionView view = make_connection_view(local);
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
      ConnectionPhase::WaitingForApproval})
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

TEST(ConnectionView, TheIdentityLineSaysWhoThisProgramIs)
{
  EXPECT_EQ(
    make_connection_view(input_of(ConnectionPhase::Preview)).identity_text,
    "preview (no QAROS)");
  ConnectionInput local = input_of(ConnectionPhase::Running);
  local.program_name = "rviz2.exe";
  EXPECT_EQ(
    make_connection_view(local).identity_text, "This PC: approved local app (rviz2.exe)");
  local.phase = ConnectionPhase::Reconnecting;
  EXPECT_EQ(
    make_connection_view(local).identity_text, "This PC: approved local app (rviz2.exe)");
  for (const ConnectionPhase phase : {ConnectionPhase::Idle, ConnectionPhase::Loading,
      ConnectionPhase::WaitingForApproval, ConnectionPhase::Failed})
  {
    local.phase = phase;
    EXPECT_EQ(
      make_connection_view(local).identity_text,
      "This PC: local app (rviz2.exe), approved once in the QAROS Hub") << static_cast<int>(phase);
  }
  // The tool tip of the line names the way to the Hub of another PC.
  EXPECT_NE(
    std::string(qar_rviz_streaming::kIdentityToolTip).find("a member of it"), std::string::npos);
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
      ConnectionPhase::Loading, ConnectionPhase::Reconnecting, ConnectionPhase::Failed})
  {
    view = make_connection_view(input_of(phase));
    EXPECT_FALSE(view.invite_enabled) << static_cast<int>(phase);
    EXPECT_NE(view.invite_tooltip.find("needs a QarOS session"), std::string::npos)
      << static_cast<int>(phase);
  }
}
