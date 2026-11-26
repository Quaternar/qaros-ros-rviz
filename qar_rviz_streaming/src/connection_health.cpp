// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/connection_health.hpp"

#include <cctype>
#include <cstddef>
#include <iterator>
#include <utility>

namespace qar_rviz_streaming
{
namespace
{

std::string whole_seconds(double seconds)
{
  return std::to_string(seconds < 0.0 ? 0LL : static_cast<long long>(seconds));
}

const char * words_of(StreamTrouble trouble)
{
  switch (trouble) {
    case StreamTrouble::ReceiverReconnecting:
      return "receiver reconnecting";
    case StreamTrouble::NoRenderRequest:
      return "no 3D frame requested";
    case StreamTrouble::SenderSetUpAgain:
      return "3D stream set up again";
    case StreamTrouble::FrameCallsStopped:
      return "frame calls paused";
    case StreamTrouble::ReceiverLeft:
      return "receiver left";
    case StreamTrouble::None:
      break;
  }
  return "";
}

/// The sentence of one trouble.
std::string sentence_of(
  StreamTrouble trouble, double seconds, const std::string & name, int attempt)
{
  switch (trouble) {
    case StreamTrouble::ReceiverReconnecting:
      return "the receiver is reconnecting (" + whole_seconds(seconds) +
             " s); the picture comes back by itself";
    case StreamTrouble::NoRenderRequest:
      return "QAROS has not asked for 3D frames since it reconnected (" + whole_seconds(seconds) +
             " s); RViz makes the 3D stream again if this goes on"
             "; a known QAROS issue after a Hub restart, the 3D view can take minutes";
    case StreamTrouble::SenderSetUpAgain:
      return "the 3D stream of '" + name + "' is being set up again (attempt " +
             std::to_string(attempt) + "); if it stays, the Visualizer may still wait for an "
             "earlier RViz: close and start the Visualizer again";
    case StreamTrouble::FrameCallsStopped:
      return "the 3D stream of '" + name +
             "' is paused (a frame call blocked); the frame calls are tried again";
    case StreamTrouble::ReceiverLeft:
      return "receiver '" + name + "' left; waiting for it to come back";
    case StreamTrouble::None:
      break;
  }
  return std::string();
}

/// True when @p text holds the status @p number written as a field: "code" (any case), "=" or
/// ":", optional blanks, the number, and no digit right after it.
bool has_status_code(const std::string & text, const std::string & number)
{
  const auto lower = [](char c) {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
  const std::string word = "code";
  for (std::size_t at = 0; at + word.size() <= text.size(); ++at) {
    std::size_t i = 0;
    while (i < word.size() && lower(text[at + i]) == word[i]) {
      ++i;
    }
    if (i < word.size()) {
      continue;
    }
    std::size_t pos = at + word.size();
    if (pos >= text.size() || (text[pos] != '=' && text[pos] != ':')) {
      continue;
    }
    ++pos;
    while (pos < text.size() && text[pos] == ' ') {
      ++pos;
    }
    if (text.compare(pos, number.size(), number) != 0) {
      continue;
    }
    pos += number.size();
    if (pos >= text.size() || std::isdigit(static_cast<unsigned char>(text[pos])) == 0) {
      return true;
    }
  }
  return false;
}

}  // namespace

SkipDetailClass classify_skip_detail(const std::string & detail)
{
  SkipDetailClass out;
  out.reconnecting = has_status_code(detail, "603") ||
    detail.find("state=reconnecting") != std::string::npos ||
    detail.find("econnecting") != std::string::npos;
  out.render_request_missing = !out.reconnecting &&
    detail.find("try_begin_frame failed") != std::string::npos;
  return out;
}

void StreamOutageTracker::sample(double now_s, const std::vector<StreamSample> & streams)
{
  last_time_s_ = now_s;
  std::map<std::string, Stream> kept;
  for (const StreamSample & sample : streams) {
    Stream stream;
    const auto found = streams_.find(sample.peer8);
    if (found != streams_.end()) {
      stream = found->second;
    } else {
      stream.progress_s = now_s;
      stream.frames = sample.frames_submitted;
    }
    stream.name = sample.name;
    stream.create_attempt = sample.create_attempt;
    // The clock of a stall runs while the stream is not ready and no frame goes through.
    if (sample.frames_submitted != stream.frames || !sample.not_ready) {
      stream.progress_s = now_s;
    }
    stream.frames = sample.frames_submitted;

    const StreamTrouble before = stream.trouble;
    StreamTrouble trouble = StreamTrouble::None;
    if (sample.not_ready && sample.reconnecting) {
      trouble = StreamTrouble::ReceiverReconnecting;
    } else if (sample.not_ready && sample.render_request_missing) {
      trouble = StreamTrouble::NoRenderRequest;
    } else if (sample.create_attempt > 0) {
      trouble = StreamTrouble::SenderSetUpAgain;
    } else if (sample.tripped) {
      trouble = StreamTrouble::FrameCallsStopped;
    }
    // Both count from the last moment the stream could work: a restart of the receiver that turns
    // into a missing render request is one stall.
    if (trouble == StreamTrouble::ReceiverReconnecting ||
      trouble == StreamTrouble::NoRenderRequest)
    {
      stream.trouble_since_s = stream.progress_s;
    } else if (trouble != stream.trouble) {
      stream.trouble_since_s = now_s;
    }
    // The words of a missing render request count from the end of the restart of the receiver (or,
    // without a restart, from the last moment the stream could work).
    if (trouble == StreamTrouble::NoRenderRequest) {
      if (before == StreamTrouble::ReceiverReconnecting) {
        stream.no_request_since_s = now_s;
      } else if (before != StreamTrouble::NoRenderRequest) {
        stream.no_request_since_s = stream.progress_s;
      }
    }
    stream.trouble = trouble;

    // A trouble is reported once it lasted long enough, and its end once it was reported.
    const StreamTrouble visible =
      trouble != StreamTrouble::None && now_s - stream.trouble_since_s >= kReportAfterS ?
      trouble : StreamTrouble::None;
    if (visible != stream.announced && visible != StreamTrouble::None) {
      log_lines_.push_back(
        "QarOS: stream " + sample.peer8 + " degraded: " + words_of(visible) + " for " +
        whole_seconds(now_s - stream.trouble_since_s) + " s");
      stream.announced_since_s = stream.trouble_since_s;
      stream.announced_frames = sample.frames_submitted;
      stream.announced = visible;
    } else if (visible == StreamTrouble::None && stream.announced != StreamTrouble::None) {
      // The end of a reported trouble is told once frames go through again; a stream that was made
      // again counts from its own start.
      if (sample.frames_submitted < stream.announced_frames) {
        stream.announced_frames = sample.frames_submitted;
      }
      if (sample.frames_submitted > stream.announced_frames) {
        log_lines_.push_back(
          "QarOS: stream " + sample.peer8 + " back after " +
          whole_seconds(now_s - stream.announced_since_s) + " s");
        stream.announced = StreamTrouble::None;
      }
    }
    stream.vanished = false;
    kept[sample.peer8] = std::move(stream);
  }
  // A stream that is reported as troubled and has no sample is being made again (the panel closes
  // the old one first): it keeps its place and its words for kVanishedGraceS, so that its return
  // is told as "back" and the strip does not fall silent in between.
  for (auto & entry : streams_) {
    Stream & stream = entry.second;
    if (kept.count(entry.first) != 0 || stream.announced == StreamTrouble::None) {
      continue;
    }
    if (!stream.vanished) {
      stream.vanished = true;
      stream.vanished_since_s = now_s;
    }
    if (now_s - stream.vanished_since_s < kVanishedGraceS) {
      kept[entry.first] = std::move(stream);
    }
  }
  streams_ = std::move(kept);

  for (auto it = left_.begin(); it != left_.end();) {
    it = now_s - it->second.since_s >= kLeftNamedS ? left_.erase(it) : std::next(it);
  }
}

void StreamOutageTracker::target_left(
  double now_s, const std::string & peer8, const std::string & name)
{
  last_time_s_ = now_s;
  const auto found = left_.find(peer8);
  if (found != left_.end()) {
    found->second.name = name;
    return;
  }
  Left left;
  left.name = name;
  left.since_s = now_s;
  left_[peer8] = left;
  log_lines_.push_back(
    "QarOS: stream " + peer8 + " degraded: " + words_of(StreamTrouble::ReceiverLeft) + " for 0 s");
}

void StreamOutageTracker::target_back(const std::string & peer8)
{
  const auto found = left_.find(peer8);
  if (found == left_.end()) {
    return;
  }
  log_lines_.push_back(
    "QarOS: stream " + peer8 + " back after " +
    whole_seconds(last_time_s_ - found->second.since_s) + " s");
  left_.erase(found);
}

std::string StreamOutageTracker::degraded_text(double now_s) const
{
  StreamTrouble worst = StreamTrouble::None;
  double worst_seconds = -1.0;
  std::string worst_name;
  int worst_attempt = 0;
  const auto consider =
    [&](StreamTrouble trouble, double seconds, const std::string & name, int attempt) {
      if (seconds > worst_seconds ||
        (seconds == worst_seconds && static_cast<int>(trouble) < static_cast<int>(worst))) {
        worst = trouble;
        worst_seconds = seconds;
        worst_name = name;
        worst_attempt = attempt;
      }
    };
  for (const auto & entry : streams_) {
    const Stream & stream = entry.second;
    const double seconds = now_s - stream.trouble_since_s;
    if (stream.trouble != StreamTrouble::None && seconds >= kReportAfterS) {
      consider(
        stream.trouble,
        stream.trouble == StreamTrouble::NoRenderRequest ?
        now_s - stream.no_request_since_s : seconds,
        stream.name, stream.create_attempt);
    }
  }
  for (const auto & entry : left_) {
    const double seconds = now_s - entry.second.since_s;
    if (seconds < kLeftNamedS) {
      consider(StreamTrouble::ReceiverLeft, seconds, entry.second.name, 0);
    }
  }
  return sentence_of(worst, worst_seconds, worst_name, worst_attempt);
}

std::vector<std::string> StreamOutageTracker::take_log_lines()
{
  std::vector<std::string> lines;
  lines.swap(log_lines_);
  return lines;
}

}  // namespace qar_rviz_streaming
