// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__CONNECTION_HEALTH_HPP_
#define QAR_RVIZ_STREAMING__CONNECTION_HEALTH_HPP_

/// @file connection_health.hpp
/// @brief What is wrong with the streams of a joined session, in a few plain words for the
/// status strip. Standard library only: no Qt, no RViz, no QAROS call.
///
/// A joined session can be healthy while one stream is not: the receiver restarts, a 3D sender
/// is set up again, a frame call of a sender blocked and its calls are paused, the receiver
/// asks for no 3D frames, or the receiver left. The panel feeds the tracker a sample of every
/// stream a few times per second and asks it for the sentence that goes behind the running text
/// of the strip (ConnectionInput::degraded_text). The tracker never changes the session.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace qar_rviz_streaming
{

/// @brief The first line of an error text, at most @p max_chars characters of it: the text a
/// heartbeat line carries for the reason of a skipped frame. A library error can run over
/// several lines; the heartbeat line is one line, so that the fields after the detail stay on it.
inline std::string first_line_limited(const std::string & text, std::size_t max_chars)
{
  std::size_t end = text.find_first_of("\n\r");
  if (end == std::string::npos) {
    end = text.size();
  }
  end = std::min(end, max_chars);
  while (end > 0 && text[end - 1] == ' ') {
    --end;
  }
  return text.substr(0, end);
}

/// @brief What the reason text of a skipped frame says about the stream.
struct SkipDetailClass
{
  /// The receiver restarts: the runtime answers status 603, its message names it, or the stream
  /// state the sender read is "reconnecting".
  bool reconnecting = false;
  /// The begin-frame call failed with an error other than the restart of the receiver.
  bool render_request_missing = false;
};

/// @brief Reads the reason text that a render sender gives for a frame it skipped. Only the
/// tokens the sender writes count: the status as "code=603" (also "Code: 603" inside a message
/// of the runtime), the stream state as "state=reconnecting" and the words of the runtime's own
/// message. A number inside another number, such as the pending counter "consecutive=1603",
/// does not.
SkipDetailClass classify_skip_detail(const std::string & detail);

/// @brief The frame rate a heartbeat line prints for one stream. @p reported is the rolling
/// rate of the last submits; it keeps its last value while no frame lands. When the submitted
/// count did not grow since the previous heartbeat (@p have_before), no frame landed in the
/// window and the rate is 0. A count that fell is a stream made again: the reported rate stands.
inline double heartbeat_fps(
  double reported, std::uint64_t submitted, bool have_before, std::uint64_t submitted_before)
{
  return have_before && submitted == submitted_before ? 0.0 : reported;
}

/// The seconds after which the Visualizer closes a panel whose video sender went away.
constexpr int kOrphanedPanelCloseSeconds = 300;

/// @brief The line of the session end for the QarOS panels that stay in the Hub without a sender:
/// the Visualizer closes such a panel about @ref kOrphanedPanelCloseSeconds seconds after its
/// sender went away, unless the next session reattaches it by name before.
inline std::string panels_losing_stream_text(std::size_t count)
{
  return "QarOS: " + std::to_string(count) +
         " QarOS panel(s) lose their stream now; QAROS closes them in about " +
         std::to_string(kOrphanedPanelCloseSeconds) + " s unless RViz connects again before";
}

/// @brief What is wrong with one stream.
enum class StreamTrouble
{
  None,
  ReceiverReconnecting,  ///< The receiver restarts; frames wait.
  NoRenderRequest,       ///< The receiver asks for no 3D frames although it is connected.
  SenderSetUpAgain,      ///< The 3D sender is being made again after an attempt that failed.
  FrameCallsStopped,     ///< A frame call of the sender blocked; its calls are paused.
  ReceiverLeft           ///< The receiver of the stream went away.
};

/// @brief What the panel knows about one stream at one moment.
struct StreamSample
{
  /// The first eight characters of the id of the stream's target.
  std::string peer8;
  /// The name of the target as the person knows it.
  std::string name;
  /// Frames handed to the stream so far; a stream that works makes it grow.
  std::uint64_t frames_submitted = 0;
  /// The stream cannot take a frame at this moment.
  bool not_ready = false;
  /// The reason of not_ready is the restart of the receiver (status 603 or the text
  /// "MediaStreamingStreamIsReconnecting").
  bool reconnecting = false;
  /// The begin-frame call of the sender fails with an error other than the restart of the
  /// receiver: the runtime has no render request from the receiver.
  bool render_request_missing = false;
  /// Attempt of the sender creation that is under way; 0 when none failed.
  int create_attempt = 0;
  /// The sender's frame calls are paused because they blocked.
  bool tripped = false;
};

/// @brief Turns samples of streams into the words of the strip and into log lines.
class StreamOutageTracker
{
public:
  /// Seconds a stream must be troubled before it is reported.
  static constexpr double kReportAfterS = 3.0;
  /// Seconds a receiver that left stays named.
  static constexpr double kLeftNamedS = 60.0;
  /// Seconds a stream that was reported as troubled is kept while it has no sample (its sender is
  /// being made again); longer than the time a sender creation may take.
  static constexpr double kVanishedGraceS = 75.0;

  /// @brief Takes the state of every stream; called a few times per second. A stream that is
  /// not in @p streams is forgotten, except one that was reported as troubled: that one is kept
  /// for kVanishedGraceS and, when its sample comes back, is told as back once its frames grow
  /// (a counter that restarted lower is taken as a new start).
  void sample(double now_s, const std::vector<StreamSample> & streams);
  /// @brief The receiver of a stream went away.
  void target_left(double now_s, const std::string & peer8, const std::string & name);
  /// @brief The receiver is back.
  void target_back(const std::string & peer8);
  /// @brief The sentence for the worst stream: the one troubled for the longest time, ties in
  /// the order of StreamTrouble. Empty when no stream has been troubled for kReportAfterS.
  std::string degraded_text(double now_s) const;
  /// @brief The lines for the log, one per change: a stream reported as troubled, or back. A
  /// stream is back only once it has handed frames to the receiver again. Taking them empties the
  /// list.
  std::vector<std::string> take_log_lines();

private:
  struct Stream
  {
    std::string name;
    std::uint64_t frames = 0;
    double progress_s = 0.0;  ///< Last moment the stream could work.
    StreamTrouble trouble = StreamTrouble::None;
    double trouble_since_s = 0.0;
    /// Where the seconds of the NoRenderRequest words start: the end of the restart of the
    /// receiver, or the last moment the stream could work.
    double no_request_since_s = 0.0;
    int create_attempt = 0;
    StreamTrouble announced = StreamTrouble::None;
    double announced_since_s = 0.0;
    /// The frame count when the trouble was reported; the stream is back when it grows.
    std::uint64_t announced_frames = 0;
    /// The last sample did not name the stream; it is kept until kVanishedGraceS is over.
    bool vanished = false;
    double vanished_since_s = 0.0;
  };
  struct Left
  {
    std::string name;
    double since_s = 0.0;
  };

  std::map<std::string, Stream> streams_;
  std::map<std::string, Left> left_;
  std::vector<std::string> log_lines_;
  /// The latest time any call gave; the end of a trouble without a time of its own.
  double last_time_s_ = 0.0;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__CONNECTION_HEALTH_HPP_
