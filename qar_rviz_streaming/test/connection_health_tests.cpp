// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// What the strip says about a stream that is in trouble (connection_health.hpp): pure functions
// of samples and times, so each sentence and each log line is checked without a session.

#include "qar_rviz_streaming/connection_health.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::classify_skip_detail;
using qar_rviz_streaming::first_line_limited;
using qar_rviz_streaming::heartbeat_fps;
using qar_rviz_streaming::panels_losing_stream_text;
using qar_rviz_streaming::StreamOutageTracker;
using qar_rviz_streaming::StreamSample;

StreamSample working(const std::string & peer8, std::uint64_t frames)
{
  StreamSample sample;
  sample.peer8 = peer8;
  sample.name = "Headset " + peer8;
  sample.frames_submitted = frames;
  return sample;
}

StreamSample reconnecting(const std::string & peer8, std::uint64_t frames)
{
  StreamSample sample = working(peer8, frames);
  sample.not_ready = true;
  sample.reconnecting = true;
  return sample;
}

/// The sender cannot begin a frame and the runtime answers an error that is not a restart of the
/// receiver (it has no render request from the receiver).
StreamSample no_request(const std::string & peer8, std::uint64_t frames)
{
  StreamSample sample = working(peer8, frames);
  sample.not_ready = true;
  sample.render_request_missing = true;
  return sample;
}

}  // namespace

TEST(StreamOutageTracker, ABriefGapIsNotTrouble)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  tracker.sample(1.0, {reconnecting("aaaa0001", 10)});
  tracker.sample(2.5, {reconnecting("aaaa0001", 10)});
  EXPECT_EQ(tracker.degraded_text(2.5), "");
  // The frames run again before the third second is over.
  tracker.sample(3.0, {working("aaaa0001", 11)});
  tracker.sample(6.0, {working("aaaa0001", 40)});
  EXPECT_EQ(tracker.degraded_text(6.0), "");
  EXPECT_TRUE(tracker.take_log_lines().empty());
}

TEST(StreamOutageTracker, AStreamThatIsReadyIsNotTroubledWhateverItsFrameCount)
{
  // A stream that takes frames but is given none (the scene is idle) is not in trouble.
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  tracker.sample(10.0, {working("aaaa0001", 10)});
  EXPECT_EQ(tracker.degraded_text(10.0), "");
}

TEST(StreamOutageTracker, ReconnectingCountsSeconds)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  tracker.sample(1.0, {reconnecting("aaaa0001", 10)});
  tracker.sample(4.5, {reconnecting("aaaa0001", 10)});
  // The stall began at the last moment the stream could work (second 0).
  EXPECT_EQ(
    tracker.degraded_text(4.5),
    "the receiver is reconnecting (4 s); the picture comes back by itself");
  tracker.sample(12.0, {reconnecting("aaaa0001", 10)});
  EXPECT_EQ(
    tracker.degraded_text(12.0),
    "the receiver is reconnecting (12 s); the picture comes back by itself");
  // Frames again: the words go.
  tracker.sample(13.0, {working("aaaa0001", 25)});
  EXPECT_EQ(tracker.degraded_text(13.0), "");
}

TEST(StreamOutageTracker, ASenderBeingSetUpAgainNamesTheStreamAndTheAttempt)
{
  StreamOutageTracker tracker;
  StreamSample sample = working("bbbb0002", 0);
  sample.not_ready = true;
  sample.create_attempt = 2;
  tracker.sample(0.0, {sample});
  EXPECT_EQ(tracker.degraded_text(2.0), "");
  tracker.sample(3.5, {sample});
  EXPECT_EQ(
    tracker.degraded_text(3.5), "the 3D stream of 'Headset bbbb0002' is being set up again "
    "(attempt 2); if it stays, the Visualizer may still wait for an earlier RViz: close and "
    "start the Visualizer again");
}

TEST(StreamOutageTracker, APausedFrameCallIsNamedAndStays)
{
  StreamOutageTracker tracker;
  StreamSample sample = working("cccc0003", 100);
  sample.tripped = true;
  tracker.sample(0.0, {sample});
  tracker.sample(5.0, {sample});
  EXPECT_EQ(
    tracker.degraded_text(5.0),
    "the 3D stream of 'Headset cccc0003' is paused (a frame call blocked); the frame calls are "
    "tried again");
}

TEST(StreamOutageTracker, TheWorstPeerIsNamed)
{
  // The stream with the longer trouble gives the words.
  StreamSample tripped = working("cccc0003", 100);
  tripped.tripped = true;
  StreamOutageTracker older_tripped;
  older_tripped.sample(0.0, {tripped, working("aaaa0001", 10)});
  older_tripped.sample(2.0, {tripped, working("aaaa0001", 20)});
  older_tripped.sample(3.0, {tripped, reconnecting("aaaa0001", 20)});
  older_tripped.sample(8.0, {tripped, reconnecting("aaaa0001", 20)});
  EXPECT_NE(
    older_tripped.degraded_text(8.0).find("is paused (a frame call blocked)"), std::string::npos);

  StreamOutageTracker older_receiver;
  older_receiver.sample(0.0, {working("aaaa0001", 10), working("cccc0003", 100)});
  older_receiver.sample(1.0, {reconnecting("aaaa0001", 10), working("cccc0003", 120)});
  older_receiver.sample(5.0, {reconnecting("aaaa0001", 10), tripped});
  older_receiver.sample(9.0, {reconnecting("aaaa0001", 10), tripped});
  EXPECT_NE(
    older_receiver.degraded_text(9.0).find("the receiver is reconnecting"), std::string::npos);
}

TEST(StreamOutageTracker, ALeftReceiverIsNamedForSixtySeconds)
{
  StreamOutageTracker tracker;
  tracker.target_left(100.0, "dddd0004", "Quest 3");
  EXPECT_EQ(
    tracker.degraded_text(101.0), "receiver 'Quest 3' left; waiting for it to come back");
  EXPECT_EQ(
    tracker.degraded_text(159.0), "receiver 'Quest 3' left; waiting for it to come back");
  EXPECT_EQ(tracker.degraded_text(160.0), "");

  // It returns: the words go at once.
  StreamOutageTracker back;
  back.target_left(10.0, "dddd0004", "Quest 3");
  back.target_back("dddd0004");
  EXPECT_EQ(back.degraded_text(11.0), "");
  // A receiver that was not named leaves nothing behind.
  back.target_back("eeee0005");
  EXPECT_EQ(back.degraded_text(12.0), "");
}

TEST(StreamOutageTracker, ASampleWithoutAStreamThatWasNotReportedForgetsIt)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  tracker.sample(1.0, {reconnecting("aaaa0001", 10)});
  tracker.sample(2.0, {reconnecting("aaaa0001", 10)});
  tracker.sample(3.0, {});
  // A stream that came back later starts anew: its trouble is counted from its own start.
  tracker.sample(4.0, {reconnecting("aaaa0001", 10)});
  tracker.sample(5.0, {reconnecting("aaaa0001", 10)});
  EXPECT_EQ(tracker.degraded_text(5.0), "");
  EXPECT_TRUE(tracker.take_log_lines().empty());
}

TEST(StreamOutageTracker, AReportedStreamKeepsItsWordsWhileItHasNoSample)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  tracker.sample(5.0, {reconnecting("aaaa0001", 10)});
  ASSERT_NE(tracker.degraded_text(5.0), "");
  tracker.sample(6.0, {});
  EXPECT_NE(tracker.degraded_text(6.0), "");
  tracker.sample(40.0, {});
  EXPECT_EQ(
    tracker.degraded_text(40.0),
    "the receiver is reconnecting (40 s); the picture comes back by itself");
}

TEST(StreamOutageTracker, AReportedStreamWithoutSampleIsDroppedSilentlyAfterTheGrace)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  tracker.sample(5.0, {no_request("aaaa0001", 10)});
  ASSERT_EQ(tracker.take_log_lines().size(), 1u);
  tracker.sample(10.0, {});
  // Still kept just before the grace is over, counted from the first sample without the stream.
  tracker.sample(10.0 + StreamOutageTracker::kVanishedGraceS - 0.5, {});
  EXPECT_NE(tracker.degraded_text(84.0), "");
  tracker.sample(10.0 + StreamOutageTracker::kVanishedGraceS, {});
  EXPECT_EQ(tracker.degraded_text(86.0), "");
  EXPECT_TRUE(tracker.take_log_lines().empty());
  // The same stream coming back later is a stream with no history: no line for it.
  tracker.sample(90.0, {working("aaaa0001", 4)});
  tracker.sample(91.0, {working("aaaa0001", 40)});
  EXPECT_TRUE(tracker.take_log_lines().empty());
}

TEST(StreamOutageTracker, AStreamMadeAgainAfterAGapOfSamplesIsBackExactlyOnce)
{
  // What the panel feeds while a sender is recycled: the old stream is closed and destroyed, there
  // is no sample for a while, then the new stream appears with a counter that starts at zero.
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 500)});
  tracker.sample(1.0, {no_request("aaaa0001", 500)});
  tracker.sample(6.0, {no_request("aaaa0001", 500)});
  std::vector<std::string> lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream aaaa0001 degraded: no 3D frame requested for 6 s");

  for (double t = 7.0; t <= 26.0; t += 1.0) {
    tracker.sample(t, {});
    EXPECT_EQ(
      tracker.degraded_text(t),
      "QAROS has not asked for 3D frames since it reconnected (" +
      std::to_string(static_cast<int>(t)) + " s); RViz makes the 3D stream again if this goes on"
      "; a known QAROS issue after a Hub restart, the 3D view can take minutes");
  }
  EXPECT_TRUE(tracker.take_log_lines().empty());

  tracker.sample(27.0, {working("aaaa0001", 0)});
  EXPECT_TRUE(tracker.take_log_lines().empty());
  EXPECT_EQ(tracker.degraded_text(27.0), "");
  tracker.sample(28.0, {working("aaaa0001", 6)});
  lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream aaaa0001 back after 28 s");
  tracker.sample(29.0, {working("aaaa0001", 60)});
  tracker.sample(30.0, {working("aaaa0001", 120)});
  EXPECT_TRUE(tracker.take_log_lines().empty());
}

TEST(StreamOutageTracker, LogLinesComeOnlyOnChange)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  EXPECT_TRUE(tracker.take_log_lines().empty());
  tracker.sample(1.0, {reconnecting("aaaa0001", 10)});
  tracker.sample(2.0, {reconnecting("aaaa0001", 10)});
  EXPECT_TRUE(tracker.take_log_lines().empty());

  tracker.sample(3.5, {reconnecting("aaaa0001", 10)});
  std::vector<std::string> lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream aaaa0001 degraded: receiver reconnecting for 3 s");

  // The same trouble says nothing again; taking the lines empties the list.
  tracker.sample(4.0, {reconnecting("aaaa0001", 10)});
  tracker.sample(9.0, {reconnecting("aaaa0001", 10)});
  EXPECT_TRUE(tracker.take_log_lines().empty());

  tracker.sample(10.0, {working("aaaa0001", 30)});
  lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream aaaa0001 back after 10 s");
  tracker.sample(11.0, {working("aaaa0001", 60)});
  EXPECT_TRUE(tracker.take_log_lines().empty());

  // A receiver that left: one line when it went, one when it came back.
  tracker.target_left(20.0, "dddd0004", "Quest 3");
  tracker.target_left(21.0, "dddd0004", "Quest 3");
  lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream dddd0004 degraded: receiver left for 0 s");
  tracker.sample(30.0, {working("aaaa0001", 90)});
  tracker.target_back("dddd0004");
  lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream dddd0004 back after 10 s");
}

TEST(StreamOutageTracker, ARestartThatTurnsIntoNoRenderRequestNeverLogsBackWhileFramesAreFrozen)
{
  // The receiver restarts (603), then the runtime answers that no render request comes (802):
  // the stream is troubled all the time and not one frame goes out.
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 100)});
  tracker.sample(1.0, {reconnecting("aaaa0001", 100)});
  tracker.sample(5.0, {reconnecting("aaaa0001", 100)});
  tracker.sample(40.0, {no_request("aaaa0001", 100)});
  tracker.sample(60.0, {no_request("aaaa0001", 100)});
  // The seconds of the sentence count from the end of the restart (second 40).
  EXPECT_EQ(
    tracker.degraded_text(60.0),
    "QAROS has not asked for 3D frames since it reconnected (20 s); RViz makes the 3D stream "
    "again if this goes on; a known QAROS issue after a Hub restart, the 3D view can take minutes");
  std::vector<std::string> lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 2u);
  EXPECT_EQ(lines[0], "QarOS: stream aaaa0001 degraded: receiver reconnecting for 5 s");
  EXPECT_EQ(lines[1], "QarOS: stream aaaa0001 degraded: no 3D frame requested for 40 s");
  for (const std::string & line : lines) {
    EXPECT_EQ(line.find("back after"), std::string::npos) << line;
  }

  // The error text goes away, the frames stay frozen (the stream says "not ready" without an
  // error): the strip is quiet, but the stream is not announced as back.
  StreamSample idle = working("aaaa0001", 100);
  idle.not_ready = true;
  tracker.sample(61.0, {idle});
  tracker.sample(70.0, {idle});
  EXPECT_EQ(tracker.degraded_text(70.0), "");
  EXPECT_TRUE(tracker.take_log_lines().empty());

  // Frames go out again: now it is back, counted from the start of the stall (second 0 to 71).
  tracker.sample(71.0, {working("aaaa0001", 140)});
  lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream aaaa0001 back after 71 s");
  tracker.sample(72.0, {working("aaaa0001", 200)});
  EXPECT_TRUE(tracker.take_log_lines().empty());
}

TEST(StreamOutageTracker, TheNoRenderRequestSentenceCountsFromTheEndOfTheRestart)
{
  // A long restart of the receiver (603), then the runtime answers 802 for a short time: the
  // sentence says how long QAROS has not asked since it reconnected, not how long the whole
  // outage lasts.
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 100)});
  for (double t = 1.0; t <= 60.0; t += 1.0) {
    tracker.sample(t, {reconnecting("aaaa0001", 100)});
  }
  tracker.sample(61.0, {no_request("aaaa0001", 100)});
  tracker.sample(70.0, {no_request("aaaa0001", 100)});
  EXPECT_EQ(
    tracker.degraded_text(70.0),
    "QAROS has not asked for 3D frames since it reconnected (9 s); RViz makes the 3D stream "
    "again if this goes on; a known QAROS issue after a Hub restart, the 3D view can take minutes");
  EXPECT_EQ(
    tracker.degraded_text(75.0),
    "QAROS has not asked for 3D frames since it reconnected (14 s); RViz makes the 3D stream "
    "again if this goes on; a known QAROS issue after a Hub restart, the 3D view can take minutes");
  // Frames go out again: the words end.
  tracker.sample(80.0, {working("aaaa0001", 150)});
  EXPECT_EQ(tracker.degraded_text(80.0), "");
}

TEST(StreamOutageTracker, ANoRenderRequestWithoutARestartCountsFromTheLastFrame)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 100)});
  tracker.sample(2.0, {no_request("aaaa0001", 100)});
  tracker.sample(30.0, {no_request("aaaa0001", 100)});
  EXPECT_EQ(
    tracker.degraded_text(30.0),
    "QAROS has not asked for 3D frames since it reconnected (30 s); RViz makes the 3D stream "
    "again if this goes on; a known QAROS issue after a Hub restart, the 3D view can take minutes");
}

TEST(StreamOutageTracker, ANoRenderRequestWithFramesGrowingIsNoTrouble)
{
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 10)});
  tracker.sample(5.0, {no_request("aaaa0001", 20)});
  tracker.sample(9.0, {no_request("aaaa0001", 30)});
  EXPECT_EQ(tracker.degraded_text(9.0), "");
  EXPECT_TRUE(tracker.take_log_lines().empty());
}

TEST(StreamOutageTracker, AStreamMadeAgainIsBackWhenItHandsFramesOverAgain)
{
  // A restarted stream counts its frames from zero: it is back at its first frame, not before.
  StreamOutageTracker tracker;
  tracker.sample(0.0, {working("aaaa0001", 500)});
  tracker.sample(1.0, {no_request("aaaa0001", 500)});
  tracker.sample(6.0, {no_request("aaaa0001", 500)});
  ASSERT_EQ(tracker.take_log_lines().size(), 1u);
  tracker.sample(8.0, {working("aaaa0001", 0)});
  EXPECT_TRUE(tracker.take_log_lines().empty());
  tracker.sample(9.0, {working("aaaa0001", 3)});
  const std::vector<std::string> lines = tracker.take_log_lines();
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "QarOS: stream aaaa0001 back after 9 s");
}

TEST(StreamOutageTracker, TheSetUpAgainSentenceNamesAnEarlierRViz)
{
  StreamOutageTracker tracker;
  StreamSample sample = working("bbbb0002", 0);
  sample.not_ready = true;
  sample.create_attempt = 3;
  tracker.sample(0.0, {sample});
  tracker.sample(4.0, {sample});
  EXPECT_EQ(
    tracker.degraded_text(4.0),
    "the 3D stream of 'Headset bbbb0002' is being set up again (attempt 3); if it stays, the "
    "Visualizer may still wait for an earlier RViz: close and start the Visualizer again");
}

TEST(FirstLineLimited, ALibraryErrorOfSeveralLinesIsCutAtItsFirstLine)
{
  const std::string text =
    "try_begin_frame failed: code=803 msg=reconnecting [Sender.hpp:727]\n\t-> second line";
  EXPECT_EQ(
    first_line_limited(text, 160),
    "try_begin_frame failed: code=803 msg=reconnecting [Sender.hpp:727]");
  EXPECT_EQ(first_line_limited("one\r\ntwo", 160), "one");
  EXPECT_EQ(first_line_limited("plain", 160), "plain");
  EXPECT_EQ(first_line_limited("", 160), "");
}

TEST(FirstLineLimited, ALongLineIsCutAtTheLimitWithoutTrailingBlanks)
{
  EXPECT_EQ(first_line_limited(std::string(200, 'x'), 160), std::string(160, 'x'));
  EXPECT_EQ(first_line_limited("abc   def", 6), "abc");
  EXPECT_EQ(first_line_limited("abc", 0), "");
}

TEST(PanelsLosingStream, TheLineSaysHowManyPanelsAndTheThreeHundredSeconds)
{
  EXPECT_EQ(
    panels_losing_stream_text(14),
    "QarOS: 14 QarOS panel(s) lose their stream now; QAROS closes them in about 300 s unless "
    "RViz connects again before");
  EXPECT_EQ(qar_rviz_streaming::kOrphanedPanelCloseSeconds, 300);
}

TEST(ClassifySkipDetail, ThePendingCounterIsNotTheStatus603)
{
  // The counter of pending answers can hold the digits 603 at any position.
  for (const char * detail : {
      "begin-frame pending (no render request from the viewer); consecutive=1603",
      "begin-frame pending (no render request from the viewer); consecutive=876031",
      "begin-frame pending (no render request from the viewer); consecutive=603",
      "begin-frame pending (no render request from the viewer); consecutive=6030"})
  {
    const auto got = classify_skip_detail(detail);
    EXPECT_FALSE(got.reconnecting) << detail;
    EXPECT_FALSE(got.render_request_missing) << detail;
  }
}

TEST(ClassifySkipDetail, TheStatusAsTheSenderWritesItIsTheRestartOfTheReceiver)
{
  EXPECT_TRUE(classify_skip_detail("try_begin_frame failed: code=603").reconnecting);
  EXPECT_TRUE(classify_skip_detail("try_begin_frame failed: code=603 msg=x").reconnecting);
  EXPECT_TRUE(classify_skip_detail(
      "try_begin_frame failed: code=803 msg=Code: 603 Desc: MediaStreamingStream [S.hpp:7]")
    .reconnecting);
  // A longer number that starts with 603 is another status.
  const auto other = classify_skip_detail("try_begin_frame failed: code=6031 msg=x");
  EXPECT_FALSE(other.reconnecting);
  EXPECT_TRUE(other.render_request_missing);
}

TEST(ClassifySkipDetail, TheStreamStateReconnectingIsTheRestartOfTheReceiver)
{
  const auto got = classify_skip_detail(
    "begin-frame pending (no render request from the viewer); state=reconnecting; "
    "consecutive=12");
  EXPECT_TRUE(got.reconnecting);
  EXPECT_FALSE(got.render_request_missing);
  EXPECT_FALSE(classify_skip_detail(
      "begin-frame pending (no render request from the viewer); state=paused; consecutive=12")
    .reconnecting);
}

TEST(ClassifySkipDetail, AnOtherFailedBeginFrameIsAMissingRenderRequest)
{
  const auto got = classify_skip_detail(
    "try_begin_frame failed: code=803 msg=Code: 803 Desc: RenderingProducerUnableToDoBeginFrame");
  EXPECT_FALSE(got.reconnecting);
  EXPECT_TRUE(got.render_request_missing);
  EXPECT_TRUE(classify_skip_detail("the receiver is reconnecting").reconnecting);
  EXPECT_FALSE(classify_skip_detail("").reconnecting);
  EXPECT_FALSE(classify_skip_detail("").render_request_missing);
}

TEST(HeartbeatFps, AStreamWithoutNewFramesPrintsZeroWhateverItsLastRate)
{
  EXPECT_EQ(heartbeat_fps(58.1, 21, true, 21), 0.0);
  EXPECT_EQ(heartbeat_fps(58.1, 2200, true, 2090), 58.1);
  // No previous heartbeat, or a counter that fell (a stream made again): the rate stands.
  EXPECT_EQ(heartbeat_fps(58.1, 21, false, 0), 58.1);
  EXPECT_EQ(heartbeat_fps(58.1, 5, true, 9000), 58.1);
  EXPECT_EQ(heartbeat_fps(0.0, 0, true, 0), 0.0);
}
