// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_capi_panels/qar_capi_panel_sender_adapter.hpp"

#include "qar_ar_core/panel_attach.hpp"
#include "qar_capi_panels/seam_c.hpp"
#include "qar_qar_adapter/gui_call_guard.hpp"
#include "qar_qar_adapter/panel_state_guard.hpp"
#include "qar_qar_adapter/panel_state_log.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace qar_capi_panels
{

using qar_qar_adapter::first_panel_frame_line;
using qar_qar_adapter::reconnecting_line;
using qar_qar_adapter::thread_number;

namespace
{

/// Local wall-clock time as `HH:MM:SS.mmm` — the same shape (and the same clock)
/// as the host panel's own `[qar-panel]` log lines, so a close can be lined up
/// against them and against the QarOS crash dumps in %TEMP%\quaternar.
std::string
local_time_hms_ms()
{
  const auto now = std::chrono::system_clock::now();
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &seconds);
#else
  localtime_r(&seconds, &local);
#endif
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
    now.time_since_epoch()).count() % 1000;
  char buf[16] = {0};
  std::snprintf(
    buf, sizeof(buf) - 1, "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
    static_cast<int>(millis));
  return std::string(buf);
}

/// Rate limit for the stderr echo, per DISTINCT message: all of the first few,
/// then one in @ref kErrorLogEvery.
///
/// A panel failure is almost never a single event — whatever is wrong is wrong
/// again on the next capture, so an unlimited echo turns one broken panel into
/// tens of thousands of identical lines a minute in a log that already reaches
/// hundreds of megabytes, and buries the line that says what changed. The first
/// occurrences are what diagnose the fault; the periodic repeat, carrying its
/// occurrence count, is what shows it is still happening.
constexpr std::uint64_t kErrorLogFirst = 3;
constexpr std::uint64_t kErrorLogEvery = 600;
/// Distinct messages remembered before the counters are dropped and the limit
/// starts over. Messages embed sizes, so the set is not closed.
constexpr std::size_t kErrorLogDistinct = 128;

/// @return true if this occurrence of @p message should be printed, and its
///         1-based occurrence count in @p out_count.
///
/// Failures are reported by the GUI thread, the frame workers and the threads that own the
/// session, so the counters are guarded.
bool
should_log_panel_error(const std::string & message, std::uint64_t & out_count)
{
  static std::mutex guard;
  static std::map<std::string, std::uint64_t> seen;
  std::lock_guard<std::mutex> lock(guard);
  if (seen.size() >= kErrorLogDistinct && seen.find(message) == seen.end()) {
    seen.clear();
  }
  out_count = ++seen[message];
  return out_count <= kErrorLogFirst || (out_count % kErrorLogEvery) == 0;
}

/// Same shape as the render adapter's helper: a readable "<call> failed: ..."
/// line, also echoed to stderr (rate limited) because the panel UI only shows
/// the last error and a bootstrap can fail several calls deep.
std::string
panel_result_error(const char * what, const QarResult & r)
{
  char buf[512] = {0};
  qar_result_message(r, buf, sizeof(buf) - 1);
  std::string out = std::string(what) + " failed: code=" +
    std::to_string(static_cast<long long>(r.code));
  if (buf[0] != '\0') {
    out += " msg=";
    out += buf;
  }
  std::uint64_t occurrence = 0;
  if (should_log_panel_error(out, occurrence)) {
    if (occurrence <= kErrorLogFirst) {
      std::fprintf(stderr, "[qar-panel-adapter] *** %s\n", out.c_str());
    } else {
      std::fprintf(
        stderr, "[qar-panel-adapter] *** %s (x%llu, still failing)\n", out.c_str(),
        static_cast<unsigned long long>(occurrence));
    }
    std::fflush(stderr);
  }
  // The RETURNED string is unchanged and un-throttled: it is the panel's
  // last_error, which is read, not appended to.
  return out;
}

/// Canonical 8-4-4-4-12 UUID text for one of the SDK's 16-byte ids.
///
/// The header calls QarGuiPanelId::data / QarStreamId::data an "identifier
/// string", but the bytes are a UUID in network order, and the runtime names
/// the SAME id in its own log as a canonical UUID. Printing the bytes as text
/// (an unprintable byte as '.') gives `.Xh...@....C.Eb?` where the runtime logs
/// `f75868aa-9802-40b3-92c1-10438545623f` — the same sixteen bytes — and
/// matching a stalled stream to its widget means byte-comparing the two.
/// Printed as a canonical UUID the two are the same string and the match is a
/// grep.
std::string
qar_id_uuid(const std::uint8_t (& data)[QAR_MAX_ID_LENGTH])
{
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (std::size_t i = 0; i < QAR_MAX_ID_LENGTH; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out.push_back('-');
    }
    out.push_back(kHex[(data[i] >> 4) & 0x0F]);
    out.push_back(kHex[data[i] & 0x0F]);
  }
  return out;
}

std::string
panel_id_text(const QarGuiPanelId & id)
{
  return qar_id_uuid(id.data);
}

/// The name of a panel state as the log prints it.
std::string
panel_state_text(QarGuiPanelState state)
{
  switch (state) {
    case QAR_GUI_PANEL_STATE_VISIBLE:
      return "VISIBLE";
    case QAR_GUI_PANEL_STATE_MINIMIZED:
      return "MINIMIZED";
    case QAR_GUI_PANEL_STATE_HIDDEN:
      return "HIDDEN";
    case QAR_GUI_PANEL_STATE_CLOSED:
      return "CLOSED";
  }
  return "state " + std::to_string(static_cast<int>(state));
}

/// True when two panel sizes are the same to a tenth of a millimetre and a ten-thousandth of a
/// content scale.
bool
same_panel_size(const QarGuiPanelSize & a, const QarGuiPanelSize & b)
{
  return std::fabs(a.width_meters - b.width_meters) < 1e-4f &&
         std::fabs(a.height_meters - b.height_meters) < 1e-4f &&
         std::fabs(a.content_scale - b.content_scale) < 1e-4f;
}

/// Handles of the first panel list query, and the largest list read at all.
constexpr std::size_t kPanelListFirst = 64;
constexpr std::size_t kPanelListMax = 1024;

/// Reads the session's panel list with one `qar_query_gui_panels` call into a buffer that grows
/// while it comes back full (or while the runtime reports more panels than the buffer holds).
/// The handles are owned by the caller. False when the query fails.
bool
query_panel_list(QarSession * session, std::vector<QarGuiPanel *> & out)
{
  out.clear();
  std::size_t capacity = kPanelListFirst;
  for (;;) {
    std::vector<QarGuiPanel *> handles(capacity, nullptr);
    std::size_t written = 0;
    if (qar_result_is_error(
        QAR_CALL(qar_query_gui_panels, session, handles.data(), handles.size(), &written)))
    {
      return false;
    }
    const bool more = (written >= capacity) && capacity < kPanelListMax;
    if (!more) {
      handles.resize(std::min(written, handles.size()));
      out = std::move(handles);
      return true;
    }
    for (QarGuiPanel * handle : handles) {
      if (handle != nullptr) {
        QAR_CALL(qar_gui_panel_handle_destroy, handle);
      }
    }
    capacity = std::min(std::max(capacity * 4, written), kPanelListMax);
  }
}

/// What the probe read about the panel `get_or_create` returned.
struct PanelProbeReading
{
  qar_ar_core::PanelProbe probe;
};

/// Reads the panel @p id out of the session's panel list: its state, its content type, its
/// content stream and its size. The handles of the list are owned here and destroyed. A value
/// the runtime does not answer takes the safe default: HIDDEN (the panel is shown), a content
/// that differs (it is replaced) and a size that differs (it is set).
PanelProbeReading
probe_panel(
  QarSession * session, const QarGuiPanelId & id, const QarStreamId & ours,
  const QarGuiPanelSize & wanted)
{
  PanelProbeReading reading;
  std::vector<QarGuiPanel *> handles;
  if (!query_panel_list(session, handles)) {
    return reading;
  }
  for (QarGuiPanel * handle : handles) {
    if (handle == nullptr) {
      continue;
    }
    QarGuiPanelId listed{};
    if (!reading.probe.found &&
      !qar_result_is_error(QAR_CALL(qar_gui_panel_get_id, handle, &listed)) &&
      std::memcmp(listed.data, id.data, sizeof(id.data)) == 0)
    {
      reading.probe.found = true;
      QarGuiPanelState state = QAR_GUI_PANEL_STATE_HIDDEN;
      if (qar_result_is_error(QAR_CALL(qar_gui_panel_get_state, handle, &state))) {
        state = QAR_GUI_PANEL_STATE_HIDDEN;
      }
      reading.probe.state = state;
      QarGuiPanelContentType type = QAR_GUI_PANEL_CONTENT_TYPE_STREAM_ID;
      if (qar_result_is_error(QAR_CALL(qar_gui_panel_get_content_type, handle, &type))) {
        type = QAR_GUI_PANEL_CONTENT_TYPE_STREAM_ID;
      }
      reading.probe.content_mismatch = type == QAR_GUI_PANEL_CONTENT_TYPE_WEBSITE_URI;
      reading.probe.content_differs = true;
      if (!reading.probe.content_mismatch) {
        QarStreamId stream = qar_stream_id_default();
        if (!qar_result_is_error(QAR_CALL(qar_gui_panel_get_content_stream_id, handle, &stream))) {
          reading.probe.content_differs =
            std::memcmp(stream.data, ours.data, sizeof(ours.data)) != 0;
        }
      }
      QarGuiPanelSize size{};
      reading.probe.size_differs =
        qar_result_is_error(QAR_CALL(qar_gui_panel_get_size, handle, &size)) ||
        !same_panel_size(size, wanted);
    }
    QAR_CALL(qar_gui_panel_handle_destroy, handle);
  }
  return reading;
}

/// Build the single-texture, single-view CPU layout one GUI panel needs.
///
/// A panel is one plain 2D image: one texture, one full-extent COLOR view with
/// no eye assignment — deliberately unlike the stereo render path's two-eye
/// separated-texture layout.
QarVideoFrameLayout
make_panel_layout(const seam::PanelDesc & desc)
{
  QarVideoFrameLayout layout = qar_video_frame_layout_default();

  layout.textures_count = 1;
  layout.textures[0] = qar_texture_size_default();
  layout.textures[0].format = to_c(desc.pixel_format);
  layout.textures[0].width = desc.buffer_width;
  layout.textures[0].height = desc.buffer_height;
  layout.textures[0].array_layers = 1;

  layout.views_count = 1;
  layout.views[0] = qar_video_frame_view_default();
  layout.views[0].start_x = 0;
  layout.views[0].start_y = 0;
  layout.views[0].end_x = desc.buffer_width;   // end values are exclusive
  layout.views[0].end_y = desc.buffer_height;
  layout.views[0].texture_index = 0;
  layout.views[0].array_layer_index = 0;
  layout.views[0].texture_format = to_c(desc.pixel_format);
  layout.views[0].data_type = QAR_VIDEO_FRAME_VIEW_TYPE_COLOR;
  layout.views[0].eye = QAR_VIDEO_FRAME_VIEW_EYE_NONE;

  return layout;
}

/// The value of the environment variable @p name, trimmed and in lower case; empty when unset.
std::string
lower_environment(const char * name)
{
  std::string value;
#ifdef _WIN32
  char * raw = nullptr;
  std::size_t length = 0;
  if (_dupenv_s(&raw, &length, name) == 0 && raw != nullptr) {
    value = raw;
  }
  std::free(raw);
#else
  const char * raw = std::getenv(name);
  if (raw != nullptr) {
    value = raw;
  }
#endif
  const char * const kSpace = " \t\r\n";
  const std::size_t first = value.find_first_not_of(kSpace);
  if (first == std::string::npos) {
    return std::string();
  }
  value = value.substr(first, value.find_last_not_of(kSpace) - first + 1);
  for (char & c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

/// Checks the destination of a frame against the captured pixels and copies them row by row.
/// @return empty when the copy was made, otherwise why it was not.
std::string
write_panel_pixels(
  const seam::PanelDesc & desc, const QarVideoTextureCpu & dst, const seam::PanelPixels & pixels)
{
  // The sender owns the destination and its layout is fixed at creation, so a
  // mismatch means our geometry and the sender have drifted apart — copying
  // anyway would write past the destination.
  if (dst.size.width != desc.buffer_width || dst.size.height != desc.buffer_height) {
    return "panel frame size mismatch: sender has " +
           std::to_string(dst.size.width) + "x" + std::to_string(dst.size.height) +
           ", we captured " + std::to_string(desc.buffer_width) + "x" +
           std::to_string(desc.buffer_height);
  }

  const std::uint32_t row_bytes = desc.buffer_width * 4u;
  const std::uint32_t dst_pitch = dst.pitch != 0 ? dst.pitch : row_bytes;
  if (dst_pitch < row_bytes || pixels.stride_bytes < row_bytes) {
    return "panel frame pitch is smaller than a row of pixels";
  }
  const std::size_t needed = static_cast<std::size_t>(dst_pitch) * (desc.buffer_height - 1) +
    row_bytes;
  if (dst.texture_data_size < needed) {
    return "panel destination buffer is too small for the captured frame";
  }

  // Row-by-row: Qt's scanline stride and QAR's pitch are independently chosen,
  // so a single flat memcpy would shear the image whenever they differ.
  for (std::uint32_t y = 0; y < desc.buffer_height; ++y) {
    std::memcpy(
      dst.texture_data + static_cast<std::size_t>(y) * dst_pitch,
      pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes,
      row_bytes);
  }
  return std::string();
}

}  // namespace

bool
default_panel_frame_calls_on_worker()
{
  static const bool on_worker = [] {
      const std::string value = lower_environment("QAR_FRAME_CALLS");
      if (value.empty() || value == "worker") {
        return true;
      }
      if (value == "gui") {
        return false;
      }
      std::fprintf(
        stderr, "[qar-panel-adapter] %s QAR_FRAME_CALLS='%s' is neither worker nor gui; the frame "
        "calls run on one worker thread per panel\n", local_time_hms_ms().c_str(), value.c_str());
      std::fflush(stderr);
      return true;
    }();
  return on_worker;
}

// What the panel's tasks and the GUI thread share. The tasks hold it by shared_ptr, so it outlives
// an adapter that was released while its tasks are queued. The video sender and the panel are
// made and destroyed on the thread that owns the session; the GUI thread uses the sender for the
// frame calls only between the moment `frames_open` is set (the panel exists and shows the
// stream) or `phase` reads Ready, and the moment it closes the adapter. After that moment the
// thread that owns the session touches the sender in the teardown only, which a close of the
// adapter queues behind the create task. With a frame worker the worker makes the frame calls in
// the GUI thread's place, until it has left; it is also the other user of the sender when the
// teardown runs, and of the two the last one destroys it (see `sender_users`).
struct QarCApiPanelSenderAdapter::Core
{
  enum class Phase { kCreating, kReady, kFailed, kClosed };

  std::atomic<Phase> phase{Phase::kCreating};
  /// Set by the create task as soon as the panel exists and shows the stream of the sender: from
  /// then on the GUI thread may send frames, although the attach probe, a reattach and the input
  /// subscription are still to run. The receiver shows the panel from this moment, so a frame
  /// that waits for the end of the create task is a black panel.
  std::atomic<bool> frames_open{false};
  seam::PanelDesc desc;
  /// Null when the host wants a stream-only panel (input off).
  std::shared_ptr<QarCApiPanelInputSource> input;

  /// Guards everything below.
  mutable std::mutex mutex;
  QarVideoSender * sender = nullptr;
  QarGuiPanelId panel_id{};
  /// Set once by the create task, never cleared: see stream_id_text().
  std::string stream_id_text;
  /// The common name of the panel; see QarCApiPanelSenderAdapter::common_name.
  std::string common_name;
  std::string error;
  bool has_panel = false;
  bool reattached = false;
  /// A "panel-place" task is queued; the newest placement asked for is in `wanted`.
  bool place_queued = false;
  bool has_wanted = false;
  seam::PanelPlacement wanted;
  std::vector<seam::PanelOpResult> results;
  /// close() is idempotent and its task can be repeated by nobody; this keeps the retirement line
  /// to one per panel.
  bool retired_logged = false;
  /// Keeps the panel in the state the adapter asked for; see panel_state_guard.hpp. Never null.
  std::shared_ptr<qar_qar_adapter::PanelStateKeeper> keeper;

  /// The breaker of the frame calls: two slow ones within a window pause them, and after the
  /// pause one probe frame decides. Its state is touched by whichever thread makes the frame
  /// calls, and its lock is never held across a QarOS call.
  struct Breaker
  {
    std::mutex mutex;
    FrameBreakerTimings timings;
    /// The frame calls are paused (read by any thread).
    std::atomic<bool> tripped{false};
    bool have_slow_call = false;
    std::chrono::steady_clock::time_point last_slow_call;
    /// The pause after the next slow probe, and when the current pause ends.
    std::chrono::milliseconds pause{5000};
    std::chrono::steady_clock::time_point resume_at;
    /// A probe frame is let through and no call has judged it yet.
    bool probing = false;
    /// When the frame calls were paused first, for the line about the resume.
    std::chrono::steady_clock::time_point first_trip_at;

    void set_timings(const FrameBreakerTimings & wanted)
    {
      std::lock_guard<std::mutex> lock(mutex);
      timings = wanted;
      pause = wanted.pause_first;
    }

    /// True while the frame calls must not be made. When the pause is over it lets one probe
    /// frame through.
    bool blocked()
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!tripped.load() || probing) {
        return false;
      }
      if (std::chrono::steady_clock::now() < resume_at) {
        return true;
      }
      // The pause is over: the next frame call is the probe, note() judges it.
      probing = true;
      return false;
    }

    /// True when the pause of the frame calls has ended: the next frame call is the probe. Read
    /// by the GUI thread, which stages a frame for the probe only then.
    bool pause_over()
    {
      std::lock_guard<std::mutex> lock(mutex);
      return std::chrono::steady_clock::now() >= resume_at;
    }

    /// Times one frame call that started at @p started; two slow ones pause the frame calls,
    /// and the call that follows a pause is judged as the probe.
    void note(
      const std::string & key, const char * call, std::chrono::steady_clock::time_point started)
    {
      std::lock_guard<std::mutex> lock(mutex);
      const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
      const auto as_ms = [](std::chrono::steady_clock::duration d) {
          return std::chrono::duration_cast<std::chrono::milliseconds>(d);
        };
      const std::chrono::milliseconds took = as_ms(now - started);
      const bool slow = took >= timings.slow_call;
      if (probing) {
        probing = false;
        if (slow) {
          // The Hub still stalls the frame calls: pause longer.
          last_slow_call = now;
          pause = std::min(pause * 2, timings.pause_cap);
          resume_at = now + pause;
          return;
        }
        // The slow calls before the pause still count: a slow call soon after the resume pauses
        // the frame calls again. The line comes first, so that a reader who sees the panel
        // running has the line.
        std::fprintf(
          stderr, "[qar-panel-adapter] %s panel '%s' frame calls resumed after %.1f s\n",
          local_time_hms_ms().c_str(), key.c_str(),
          std::chrono::duration<double>(now - first_trip_at).count());
        std::fflush(stderr);
        tripped.store(false);
        return;
      }
      if (tripped.load()) {
        return;
      }
      if (pause != timings.pause_first && now - last_slow_call >= timings.calm_reset) {
        pause = timings.pause_first;
      }
      if (!slow) {
        return;
      }
      const bool second = have_slow_call && as_ms(now - last_slow_call) <= timings.window;
      last_slow_call = now;
      have_slow_call = true;
      if (!second) {
        return;
      }
      first_trip_at = now;
      resume_at = now + pause;
      std::fprintf(
        stderr, "[qar-panel-adapter] %s panel '%s' frame calls stopped: %s took %lld ms twice\n",
        local_time_hms_ms().c_str(), key.c_str(), call, static_cast<long long>(took.count()));
      std::fflush(stderr);
      tripped.store(true);
    }
  };
  Breaker breaker;

  /// The hand-off between the GUI thread and the frame worker: one staged frame. The lock is held
  /// for copying a few values or the rows of one frame, never across a QarOS call.
  struct Frame
  {
    std::mutex mutex;
    /// The newest frame the GUI thread handed over: rows of buffer_width * 4 bytes without padding,
    /// valid while `has_staged`. Its storage is reused.
    std::vector<std::uint8_t> staged;
    bool has_staged = false;
    /// The frame the worker is sending. Only the worker touches it, outside the lock.
    std::vector<std::uint8_t> working;
    /// Why the staged frame has no destination yet: the answer of the worker's last attempt.
    seam::PanelNotReadyCause cause = seam::PanelNotReadyCause::NoDestination;
    /// A failure of the worker's frame calls, reported once by the next try_begin_frame.
    std::string error;

    /// The worker found no destination: the frame it holds stays staged for the next attempt,
    /// unless a newer one was staged meanwhile, which replaces it.
    void park(seam::PanelNotReadyCause why)
    {
      std::lock_guard<std::mutex> lock(mutex);
      cause = why;
      if (!has_staged) {
        staged.swap(working);
        has_staged = true;
      }
    }
    /// The frame was shown.
    void sent()
    {
      std::lock_guard<std::mutex> lock(mutex);
      cause = seam::PanelNotReadyCause::NoDestination;
    }
    /// The frame is dropped; the failure is reported to the GUI thread.
    void set_error(const std::string & text)
    {
      std::lock_guard<std::mutex> lock(mutex);
      cause = seam::PanelNotReadyCause::NoDestination;
      error = text;
    }
  };
  Frame frame;

  /// Of the teardown and the frame worker the last to call arrive() destroys the video sender.
  /// 1 when no worker makes the frame calls.
  std::atomic<int> sender_users{1};
  bool arrive() { return sender_users.fetch_sub(1) == 1; }

  /// Frames this sender has shown (a show_frame that succeeded).
  std::atomic<std::uint64_t> frames_shown{0};
  /// Lets the first frame shown print its line, and the first 603 after a frame print its line.
  qar_qar_adapter::LogOnce first_show_noted;
  qar_qar_adapter::LogOnce reconnect_noted;
  qar_qar_adapter::LogOnce paused_noted;

  enum class Begin { kReady, kNotReady, kError };

  /// Sorts the answer of qar_video_sender_try_begin_frame. A not-ready answer is no failure:
  /// the caller polls again. @p cause says which one; @p failure is set for a real failure.
  Begin classify_begin(
    const QarResult & r, bool sender_ready, seam::PanelNotReadyCause & cause,
    std::string & failure)
  {
    cause = seam::PanelNotReadyCause::NoDestination;
    if (!qar_result_is_error(r)) {
      return sender_ready ? Begin::kReady : Begin::kNotReady;
    }
    // NOT a failure — a NOT-READY state, and the difference matters because the
    // caller marks the panel broken on a failure and simply polls again on a
    // not-ready.
    //
    // The header documents this call as "non-blocking attempt to begin
    // producing a new frame. Sets out_ready to true when a send cycle was
    // reserved. When false, no frame cycle exists and the caller should poll
    // again later." A panel sender legitimately exists before anything in the
    // headset has attached to its stream, and that is exactly what
    // MEDIA_STREAMING_NO_ACTIVE_TRANSFERS (604) says: there is no transfer to
    // reserve a destination on. STREAM_IS_RECONNECTING (603) is the same shape
    // — transient, retried by polling, nothing the caller can fix. Both are
    // therefore reported the documented way: success, out_ready == false.
    const bool no_transfers =
      qar_result_has_code(r, QAR_STATUS_MEDIA_STREAMING_NO_ACTIVE_TRANSFERS);
    const bool reconnecting =
      qar_result_has_code(r, QAR_STATUS_MEDIA_STREAMING_STREAM_IS_RECONNECTING);
    // STREAM_IS_PAUSED (602): the receiver paused the stream. Nothing is wanted until it
    // resumes, which is a state of the receiver and no fault of this sender.
    const bool paused = qar_result_has_code(r, QAR_STATUS_MEDIA_STREAMING_STREAM_IS_PAUSED);
    if (!(no_transfers || reconnecting || paused)) {
      failure = panel_result_error("qar_video_sender_try_begin_frame", r);
      return Begin::kError;
    }
    // All three are reported as not ready, and the caller counts them apart: nothing attached
    // to the stream yet is a different fault from a receiver that lost a stream it had, and
    // from one that paused it.
    cause = paused ? seam::PanelNotReadyCause::Paused :
      reconnecting ? seam::PanelNotReadyCause::Reconnecting :
      seam::PanelNotReadyCause::NoActiveTransfers;
    std::string stream_text;
    {
      std::lock_guard<std::mutex> lock(mutex);
      stream_text = stream_id_text;
    }
    if (paused && paused_noted.first()) {
      std::fprintf(
        stderr, "[qar-panel-adapter] %s panel '%s' stream %s: paused by the receiver\n",
        local_time_hms_ms().c_str(), desc.key.c_str(), stream_text.c_str());
      std::fflush(stderr);
    }
    const std::uint64_t shown = frames_shown.load();
    if (reconnecting && shown > 0 && reconnect_noted.first()) {
      std::fprintf(
        stderr, "%s\n",
        reconnecting_line(local_time_hms_ms(), desc.key, stream_text, shown).c_str());
      std::fflush(stderr);
    }
    return Begin::kNotReady;
  }

  /// Counts a frame shown and prints the line of the first one, with the thread that showed it.
  void note_shown()
  {
    frames_shown.fetch_add(1);
    if (first_show_noted.first()) {
      // The thread that shows frames, to be read next to the threads of the 3D stream's lines.
      std::fprintf(
        stderr, "%s\n",
        first_panel_frame_line(local_time_hms_ms(), desc.key, thread_number()).c_str());
      std::fflush(stderr);
    }
  }
};

QarCApiPanelSenderAdapter::QarCApiPanelSenderAdapter(
  qar_qar_adapter::IQarSessionOwner * owner, const seam::PanelDesc & desc,
  std::shared_ptr<QarCApiPanelInputSource> input, const FrameCallOptions & frame_calls)
: owner_(owner),
  desc_(desc),
  input_(input),
  core_(std::make_shared<Core>())
{
  core_->desc = desc;
  core_->input = std::move(input);
  core_->common_name = desc.common_name;
  core_->keeper = std::make_shared<qar_qar_adapter::PanelStateKeeper>(
    owner, "qar-panel-adapter", desc.key);
  if (owner != nullptr) {
    release_lane_ = owner->release_lane();
  }

  // What can be checked without the runtime is refused at once.
  if (owner_ == nullptr) {
    refuse("no QarSession (is QAR streaming started?)");
    return;
  }
  if (desc_.buffer_width == 0 || desc_.buffer_height == 0) {
    refuse("panel buffer size must be non-zero");
    return;
  }
  if (desc_.common_name.empty()) {
    refuse("no common name for panel '" + desc_.key + "'");
    return;
  }
  const std::shared_ptr<Core> core = core_;
  const bool posted = owner_->post_session_task(
    ("panel-create:" + desc_.key).c_str(),
    [core](QarSession * session) { run_create(session, core); },
    [core] {
      seam::PanelOpResult result;
      result.op = seam::PanelOpResult::Op::Create;
      result.ok = false;
      result.error = "the QarOS session ended before the panel was made";
      std::lock_guard<std::mutex> lock(core->mutex);
      core->error = result.error;
      core->results.push_back(result);
      Core::Phase expected = Core::Phase::kCreating;
      core->phase.compare_exchange_strong(expected, Core::Phase::kFailed);
    });
  if (!posted) {
    refuse("QAR session is not accepting calls");
    return;
  }

  // The frame worker needs the lane: it posts the release of the video sender there when it
  // leaves, and the owner waits for it before it destroys the session.
  if (frame_calls.on_worker) {
    if (release_lane_ != nullptr) {
      core_->sender_users.store(2);
      qar_qar_adapter::FrameWorker::Jobs jobs;
      jobs.poll = [core] { run_send(core); };
      jobs.show = [core] { run_send(core); };
      jobs.leave = [core, lane = release_lane_] { release_sender_on_lane(core, lane); };
      qar_qar_adapter::FrameWorkerLog log;
      log.prefix = "[qar-panel-adapter]";
      log.handle = "video sender";
      worker_ = qar_qar_adapter::FrameWorker::start(
        "panel '" + desc_.key + "'", release_lane_, std::move(jobs), frame_calls.worker_timings,
        std::move(log));
      if (worker_ == nullptr) {
        core_->sender_users.store(1);
      }
    }
    if (worker_ == nullptr) {
      std::fprintf(
        stderr, "[qar-panel-adapter] %s panel '%s' frame calls run on the GUI thread "
        "(no frame worker could start)\n", local_time_hms_ms().c_str(), desc_.key.c_str());
      std::fflush(stderr);
    }
  }
}

QarCApiPanelSenderAdapter::~QarCApiPanelSenderAdapter()
{
  close();
}

void
QarCApiPanelSenderAdapter::refuse(const std::string & reason)
{
  refused_ = true;
  last_error_ = reason;
  std::lock_guard<std::mutex> lock(core_->mutex);
  core_->error = reason;
  core_->phase.store(Core::Phase::kFailed, std::memory_order_release);
}

bool
QarCApiPanelSenderAdapter::ready() const
{
  return core_->phase.load(std::memory_order_acquire) == Core::Phase::kReady;
}

bool
QarCApiPanelSenderAdapter::pending() const
{
  return core_->phase.load(std::memory_order_acquire) == Core::Phase::kCreating &&
         !core_->frames_open.load(std::memory_order_acquire);
}

QarGuiPanelId
QarCApiPanelSenderAdapter::panel_id() const
{
  std::lock_guard<std::mutex> lock(core_->mutex);
  return core_->panel_id;
}

bool
QarCApiPanelSenderAdapter::reattached() const
{
  std::lock_guard<std::mutex> lock(core_->mutex);
  return core_->reattached;
}

std::string
QarCApiPanelSenderAdapter::common_name() const
{
  std::lock_guard<std::mutex> lock(core_->mutex);
  return core_->common_name;
}

std::string
QarCApiPanelSenderAdapter::stream_id_text() const
{
  std::lock_guard<std::mutex> lock(core_->mutex);
  return core_->stream_id_text;
}

void
QarCApiPanelSenderAdapter::drain_results(std::vector<seam::PanelOpResult> & out)
{
  // The consumer calls this on every tick, which is when a panel that QarOS hid again inside the
  // hold of an ask and then left alone is asked about once more.
  core_->keeper->poll();
  retry_staged_frame();
  std::vector<seam::PanelOpResult> taken;
  {
    std::lock_guard<std::mutex> lock(core_->mutex);
    taken.swap(core_->results);
  }
  for (seam::PanelOpResult & result : taken) {
    if (!result.ok && !result.error.empty()) {
      last_error_ = result.error;
    }
    out.push_back(std::move(result));
  }
}

// The create chain: video_sender_create -> stream_id -> get_or_create -> probe, and the reattach
// steps when the panel is an earlier one. Runs on the thread that owns the session. The failing
// step is recorded in Core::error and the phase becomes Failed. The frames open right after the
// first get_or_create, so the probe and the reattach do not hold them.
void
QarCApiPanelSenderAdapter::run_create(QarSession * session, const std::shared_ptr<Core> & core)
{
  // An adapter closed or detached before its turn never made a panel: nothing is created, so the
  // hide and the detach queued behind this task find nothing to change and cost no call. One that
  // is closed while this task runs leaves the panel it made for those tasks (hide, detach).
  if (core->phase.load(std::memory_order_acquire) == Core::Phase::kClosed) {
    return;
  }
  const seam::PanelDesc desc = core->desc;
  const auto fail = [&core](const std::string & error) {
      seam::PanelOpResult result;
      result.op = seam::PanelOpResult::Op::Create;
      result.ok = false;
      result.error = error;
      std::lock_guard<std::mutex> lock(core->mutex);
      core->error = error;
      core->results.push_back(result);
      Core::Phase expected = Core::Phase::kCreating;
      core->phase.compare_exchange_strong(expected, Core::Phase::kFailed);
    };

  // The pose and the size of the panel in the C types, for the calls below.
  const QarPose wanted_pose = to_c(desc.placement.pose);
  const QarGuiPanelSize wanted_size = to_c(desc.placement.size);

  QarVideoSenderInit init = qar_video_sender_init_default();
  init.graphics_api = QAR_GRAPHICS_API_CPU;
  init.layout = make_panel_layout(desc);

  QarVideoSender * sender = nullptr;
  QarResult r = QAR_CALL(qar_video_sender_create, session, &init, &sender);
  if (qar_result_is_error(r) || sender == nullptr) {
    fail(panel_result_error("qar_video_sender_create", r));
    return;
  }
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    core->sender = sender;
  }

  QarStreamId stream_id = qar_stream_id_default();
  r = QAR_CALL(qar_video_sender_stream_id, sender, &stream_id);
  if (qar_result_is_error(r)) {
    fail(panel_result_error("qar_video_sender_stream_id", r));
    return;
  }
  // Kept for the LIFETIME of this adapter, close() included: everything the
  // runtime ever says about this panel's content — the streamCatalog entry, the
  // streamRequest queries a receiver retries, the reconnect it enters when the
  // producer goes away — is keyed by this id and nothing else.
  const std::string stream_id_text = qar_id_uuid(stream_id.data);
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    core->stream_id_text = stream_id_text;
  }

  // The panel's content IS the stream: chain QarGuiPanelStreamIdExt onto the
  // init. WITHOUT this extension the runtime creates a website panel instead
  // (documented in qar_streaming.h), which would silently show a browser rather
  // than the widget.
  QarGuiPanelStreamIdExt stream_ext = qar_gui_panel_stream_id_ext_default();
  stream_ext.stream_id = stream_id;

  // The panel of this common name: created, or found again when an earlier session left it
  // behind. A name that is taken by a panel that cannot be reused is tried again with a suffix
  // appended to the base name: "-v2" for a website panel, "-r1" .. "-r8" for closed panels.
  std::string common_name = desc.common_name;
  QarGuiPanelId panel_id{};
  qar_ar_core::PanelAttachDecision decision;
  PanelProbeReading reading;
  for (unsigned retries = 0;; ++retries) {
    QarGuiPanelInit panel_init = qar_gui_panel_init_default();
    panel_init.header.next = &stream_ext;
    panel_init.common_name = common_name.c_str();
    panel_init.display_name = desc.display_name.empty() ? nullptr : desc.display_name.c_str();
    panel_init.pose = wanted_pose;
    panel_init.size = wanted_size;

    r = QAR_CALL(qar_gui_panels_get_or_create, session, &panel_init, &panel_id);
    if (qar_result_is_error(r)) {
      fail(panel_result_error("qar_gui_panels_get_or_create", r));
      return;
    }
    // The panel exists, and the receiver may attach to the stream at once. Frames sent before the
    // probe ends are harmless: a stream nobody shows has no transfer and the sender answers "no
    // active transfers". The sender stays the same through a retry with a suffix and through the
    // reattach (which names the stream, not the sender); a failure below sets Failed and the GUI
    // thread stops sending at its next tick. Nothing on this thread touches the sender again
    // before the teardown, which runs only after the adapter was closed.
    core->frames_open.store(true, std::memory_order_release);

    reading = probe_panel(session, panel_id, stream_id, wanted_size);
    decision = qar_ar_core::decide_panel_attach(reading.probe, retries);
    if (decision.kind != qar_ar_core::PanelAttachKind::RetryWithSuffix) {
      break;
    }
    std::fprintf(
      stderr, "[qar-panel-adapter] %s panel '%s' name=%s is taken by %s panel, trying %s%s\n",
      local_time_hms_ms().c_str(), desc.key.c_str(), common_name.c_str(),
      decision.reason == "closed" ? "a closed" : "a web", desc.common_name.c_str(),
      decision.retry_suffix.c_str());
    std::fflush(stderr);
    common_name = desc.common_name + decision.retry_suffix;
  }
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    core->common_name = common_name;
    core->panel_id = panel_id;
  }
  if (decision.kind == qar_ar_core::PanelAttachKind::Refused) {
    const std::string refusal = decision.reason == "closed" ?
      "closed panels of the names " + desc.common_name + " to " + common_name +
      " exist; no free name is left" :
      "a web panel of the name " + common_name + " exists";
    std::fprintf(
      stderr, "[qar-panel-adapter] %s panel '%s' refused: %s\n", local_time_hms_ms().c_str(),
      desc.key.c_str(), refusal.c_str());
    std::fflush(stderr);
    fail(refusal);
    return;
  }

  const bool reattached = decision.kind == qar_ar_core::PanelAttachKind::Reattached;
  if (reattached) {
    // The panel of an earlier session shows the stream of a sender that is gone. Our
    // stream goes on it, then size and pose, then it is shown again.
    r = QAR_CALL(qar_gui_panels_set_stream, session, &panel_id, &stream_id);
    if (qar_result_is_error(r)) {
      fail(panel_result_error("qar_gui_panels_set_stream", r));
      return;
    }
    if (decision.change_size) {
      r = QAR_CALL(qar_gui_panels_change_size, session, &panel_id, &wanted_size);
      if (qar_result_is_error(r)) {
        fail(panel_result_error("qar_gui_panels_change_size", r));
        return;
      }
    }
    r = QAR_CALL(qar_gui_panels_update_pose, session, &panel_id, &wanted_pose);
    if (qar_result_is_error(r)) {
      fail(panel_result_error("qar_gui_panels_update_pose", r));
      return;
    }
    if (decision.show) {
      r = QAR_CALL(qar_gui_panels_set_state, session, &panel_id, QAR_GUI_PANEL_STATE_VISIBLE);
      if (qar_result_is_error(r)) {
        fail(panel_result_error("qar_gui_panels_set_state", r));
        return;
      }
    }
  }
  // Marked only now: a step of the reattach that failed leaves the panel of the earlier session
  // alone, because the consumer drops a failed adapter and its teardown closes the panel it owns.
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    core->has_panel = true;
    core->reattached = reattached;
  }

  // How the panel came to be, in the one format the log checks read.
  qar_ar_core::PanelAttachLogFields fields;
  fields.time = local_time_hms_ms();
  fields.key = desc.key;
  fields.reattached = reattached;
  fields.common_name = common_name;
  fields.panel_id = panel_id_text(panel_id);
  if (reading.probe.found) {
    fields.state_was = qar_ar_core::panel_state_name(reading.probe.state);
  }
  fields.stream_swapped = reattached && decision.swap_content;
  fields.size_changed = reattached && decision.change_size;
  std::fprintf(stderr, "%s\n", qar_ar_core::panel_attach_log_line(fields).c_str());

  // The creation half of the pair the retirement line below completes. Printed
  // unconditionally, because it is the ONLY place the runtime's stream UUID and
  // the widget key are written next to each other: without it, a
  // `streamRequest/<uuid>` in the runtime trace names nothing we know.
  std::fprintf(
    stderr, "[qar-panel-adapter] %s panel '%s' %s: id=%s stream=%s buffer=%ux%u px\n",
    local_time_hms_ms().c_str(), desc.key.c_str(), reattached ? "reattached" : "created",
    panel_id_text(panel_id).c_str(), stream_id_text.c_str(),
    static_cast<unsigned>(desc.buffer_width), static_cast<unsigned>(desc.buffer_height));
  std::fflush(stderr);

  // The panel exists, so it can now be pointed at. Subscribing HERE rather
  // than in the RViz panel is what makes the subscription's lifetime exactly the
  // panel's - the teardown is the only place it is ended, and it runs
  // whether the panel went away, the widget resized, or the process is
  // shutting down. The content frame is the buffer we stream, which is what the
  // renderer decodes and therefore the frame the event's pixels are in.
  //
  // A failure here is NOT fatal: a panel that streams but takes no input is
  // strictly better than no panel, and it is the only outcome available on a
  // runtime without the gesture subscription API.
  if (core->input) {
    // The content extent, NOT the buffer: the buffer is aligned up for the
    // encoder and the slack is transparent padding the widget never paints.
    const std::uint32_t content_w =
      desc.content_width != 0 ? desc.content_width : desc.buffer_width;
    const std::uint32_t content_h =
      desc.content_height != 0 ? desc.content_height : desc.buffer_height;
    if (!core->input->subscribe_panel(desc.key, panel_id, content_w, content_h, stream_id_text)) {
      std::fprintf(stderr,
        "[qar-panel-adapter] %s panel '%s' streams but takes no AR input: %s\n",
        local_time_hms_ms().c_str(), desc.key.c_str(), core->input->last_error().c_str());
      std::fflush(stderr);
    } else {
      // The POSITIVE half, printed once per panel creation. Without it, silence
      // is ambiguous: neither line in a log means we never reached here at all,
      // whereas this line means the runtime accepted the subscription and owes us
      // click/hover events for THIS id at THIS extent -- which is also the
      // extent every incoming pixel coordinate will be normalised against, so a
      // mismatch with the panel you are poking is visible right here.
      std::fprintf(stderr,
        "[qar-panel-adapter] %s panel '%s' subscribed for AR click+hover: id='%s' content=%ux%u px\n",
        local_time_hms_ms().c_str(), desc.key.c_str(), panel_id_text(panel_id).c_str(),
        static_cast<unsigned>(content_w), static_cast<unsigned>(content_h));
      std::fflush(stderr);
    }
  }

  // The panel shows the stream, so VISIBLE is the state to keep. A runtime that reports the panel
  // hidden right after the adapter showed it is asked again. A watch that cannot start is
  // reported and the panel works without it.
  core->keeper->start(session, panel_id);

  seam::PanelOpResult done;
  done.op = seam::PanelOpResult::Op::Create;
  done.ok = true;
  done.reattached = reattached;
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    core->error.clear();
    core->results.push_back(done);
  }
  // Published last: the GUI thread reads the sender after it saw Ready. A close that came in
  // meanwhile keeps the phase Closed, and its task cleans up behind this one.
  Core::Phase expected = Core::Phase::kCreating;
  core->phase.compare_exchange_strong(
    expected, Core::Phase::kReady, std::memory_order_release, std::memory_order_relaxed);
}

void
QarCApiPanelSenderAdapter::run_place(QarSession * session, const std::shared_ptr<Core> & core)
{
  seam::PanelPlacement placement;
  QarGuiPanelId panel_id{};
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    core->place_queued = false;
    if (!core->has_wanted) {
      return;
    }
    core->has_wanted = false;
    placement = core->wanted;
    panel_id = core->panel_id;
    if (!core->has_panel || core->phase.load() == Core::Phase::kFailed) {
      return;
    }
  }
  seam::PanelOpResult result;
  result.op = seam::PanelOpResult::Op::Placement;
  const QarPose pose = to_c(placement.pose);
  const QarGuiPanelSize size = to_c(placement.size);
  const QarResult posed = QAR_CALL(qar_gui_panels_update_pose, session, &panel_id, &pose);
  if (qar_result_is_error(posed)) {
    result.ok = false;
    result.error = panel_result_error("qar_gui_panels_update_pose", posed);
  } else {
    const QarResult sized = QAR_CALL(
        qar_gui_panels_change_size, session, &panel_id, &size);
    if (qar_result_is_error(sized)) {
      result.ok = false;
      result.error = panel_result_error("qar_gui_panels_change_size", sized);
    }
  }
  std::lock_guard<std::mutex> lock(core->mutex);
  if (result.ok) {
    core->desc.placement = placement;
  }
  core->results.push_back(std::move(result));
}

void
QarCApiPanelSenderAdapter::resubscribe_if_revived(
  const std::shared_ptr<Core> & core, const QarGuiPanelId & panel_id)
{
  if (!core->input || core->phase.load() == Core::Phase::kClosed) {
    return;
  }
  std::string key;
  std::string stream_id_text;
  std::uint32_t content_w = 0;
  std::uint32_t content_h = 0;
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    key = core->desc.key;
    stream_id_text = core->stream_id_text;
    content_w = core->desc.content_width != 0 ? core->desc.content_width : core->desc.buffer_width;
    content_h =
      core->desc.content_height != 0 ? core->desc.content_height : core->desc.buffer_height;
  }
  if (key.empty()) {
    return;
  }
  // The key keeps ONE subscription, on the panel that was made or reattached last. A kept panel
  // of the resize ring that is shown again is another id: taps on it would be dropped.
  const std::optional<QarGuiPanelId> now = core->input->subscribed_id(key);
  if (now.has_value() && std::memcmp(now->data, panel_id.data, sizeof(panel_id.data)) == 0) {
    return;
  }
  if (!core->input->subscribe_panel(key, panel_id, content_w, content_h, stream_id_text)) {
    std::fprintf(stderr,
      "[qar-panel-adapter] %s panel '%s' shown again but takes no AR input: %s\n",
      local_time_hms_ms().c_str(), key.c_str(), core->input->last_error().c_str());
    std::fflush(stderr);
    return;
  }
  std::fprintf(stderr,
    "[qar-panel-adapter] %s panel '%s' re-subscribed for AR click+hover: id='%s' (revived)\n",
    local_time_hms_ms().c_str(), key.c_str(), panel_id_text(panel_id).c_str());
  std::fflush(stderr);
}

void
QarCApiPanelSenderAdapter::run_input(QarSession *, const std::shared_ptr<Core> & core)
{
  QarGuiPanelId panel_id{};
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    if (!core->has_panel || core->phase.load() == Core::Phase::kFailed) {
      return;  // the panel was never made, or its creation failed: nothing to subscribe
    }
    panel_id = core->panel_id;
  }
  resubscribe_if_revived(core, panel_id);
}

void
QarCApiPanelSenderAdapter::ensure_input()
{
  const Core::Phase phase = core_->phase.load(std::memory_order_acquire);
  if (closed_ || (phase != Core::Phase::kCreating && phase != Core::Phase::kReady)) {
    return;
  }
  const std::shared_ptr<Core> core = core_;
  owner_->post_session_task(
    ("panel-input:" + desc_.key).c_str(),
    [core](QarSession * session) { run_input(session, core); });
}

void
QarCApiPanelSenderAdapter::run_state(
  QarSession * session, const std::shared_ptr<Core> & core, QarGuiPanelState state)
{
  // Whatever this task finds to do, the keeper learns that the request has been served.
  struct Served
  {
    qar_qar_adapter::PanelStateKeeper & keeper;
    ~Served() { keeper.request_done(); }
  } served{*core->keeper};
  QarGuiPanelId panel_id{};
  std::string key;
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    if (!core->has_panel || core->phase.load() == Core::Phase::kFailed) {
      return;  // the panel was never made, or its creation failed: nothing to change
    }
    panel_id = core->panel_id;
    key = core->desc.key;
  }
  seam::PanelOpResult result;
  result.op = seam::PanelOpResult::Op::State;
  result.state = to_seam(state);
  if (state == QAR_GUI_PANEL_STATE_VISIBLE) {
    resubscribe_if_revived(core, panel_id);
  }
  const QarResult r = QAR_CALL(qar_gui_panels_set_state, session, &panel_id, state);
  if (qar_result_is_error(r)) {
    result.ok = false;
    result.error = panel_result_error("qar_gui_panels_set_state", r);
  } else {
    // The same shape as the creation line, so the change can be lined up against the host's own
    // log and against the runtime's trace. The input source reads a panel update as an AR-side
    // close only for the state CLOSED, so the HIDDEN or VISIBLE set here is not taken for one.
    std::fprintf(
      stderr, "[qar-panel-adapter] %s panel '%s' state -> %s\n", local_time_hms_ms().c_str(),
      key.c_str(), panel_state_text(state).c_str());
    std::fflush(stderr);
  }
  std::lock_guard<std::mutex> lock(core->mutex);
  core->results.push_back(std::move(result));
}

bool
QarCApiPanelSenderAdapter::try_begin_frame(bool & out_ready)
{
  out_ready = false;
  not_ready_cause_ = seam::PanelNotReadyCause::NoDestination;
  const Core::Phase phase = core_->phase.load(std::memory_order_acquire);
  if (phase == Core::Phase::kCreating && !core_->frames_open.load(std::memory_order_acquire)) {
    not_ready_cause_ = seam::PanelNotReadyCause::Creating;
    return true;
  }
  if ((phase != Core::Phase::kReady && phase != Core::Phase::kCreating) || closed_) {
    if (phase == Core::Phase::kFailed) {
      std::lock_guard<std::mutex> lock(core_->mutex);
      last_error_ = core_->error;
    }
    if (last_error_.empty()) { last_error_ = "panel sender is not ready"; }
    return false;
  }
  if (sender_ == nullptr) {
    std::lock_guard<std::mutex> lock(core_->mutex);
    sender_ = core_->sender;
  }
  if (sender_ == nullptr) {
    if (last_error_.empty()) { last_error_ = "panel sender is not ready"; }
    return false;
  }

  if (worker_ != nullptr) {
    return try_begin_on_worker(out_ready);
  }

  if (core_->breaker.blocked()) {
    // The frame calls are paused: nothing is asked of the sender meanwhile.
    frame_reserved_ = false;
    return true;
  }

  bool sender_ready = false;
  const auto begin_started = std::chrono::steady_clock::now();
  const QarResult r = QAR_CALL(qar_video_sender_try_begin_frame, sender_, &sender_ready);
  core_->breaker.note(desc_.key, "try_begin_frame", begin_started);
  if (core_->breaker.tripped.load()) {
    frame_reserved_ = false;
    return true;
  }
  std::string error;
  switch (core_->classify_begin(r, sender_ready, not_ready_cause_, error)) {
    case Core::Begin::kError:
      last_error_ = error;
      return false;
    case Core::Begin::kNotReady:
      frame_reserved_ = false;
      return true;
    case Core::Begin::kReady:
      break;
  }
  frame_reserved_ = true;
  out_ready = true;
  return true;
}

bool
QarCApiPanelSenderAdapter::try_begin_on_worker(bool & out_ready)
{
  Core::Frame & frame = core_->frame;
  frame_reserved_ = false;
  std::string error;
  bool staged = false;
  seam::PanelNotReadyCause cause = seam::PanelNotReadyCause::NoDestination;
  {
    std::lock_guard<std::mutex> lock(frame.mutex);
    if (!frame.error.empty()) {
      error.swap(frame.error);
    } else {
      staged = frame.has_staged;
      cause = frame.cause;
    }
  }
  if (!error.empty()) {
    last_error_ = error;
    return false;
  }
  // The frame calls are paused, or the worker is inside a call that does not return: no frame is
  // staged for them. After the pause a frame is let through for the worker to probe with.
  const bool paused = core_->breaker.tripped.load() && !core_->breaker.pause_over();
  if (worker_->waiting() || paused) {
    return true;
  }
  if (staged) {
    // A frame waits for a destination: that is what the consumer is told, and the worker tries
    // the frame again. A worker inside a call that blocks does not delay this call.
    not_ready_cause_ = cause;
    last_kick_ = std::chrono::steady_clock::now();
    worker_->kick();
    return true;
  }
  // No cycle is reserved before there is a frame to send: the answer says that a staging slot is
  // free, and the worker reserves the cycle when submit_frame hands the frame over.
  frame_reserved_ = true;
  out_ready = true;
  return true;
}

void
QarCApiPanelSenderAdapter::retry_staged_frame()
{
  if (worker_ == nullptr || closed_) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now - last_kick_ < std::chrono::milliseconds(20)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(core_->frame.mutex);
    if (!core_->frame.has_staged) {
      return;
    }
  }
  if (core_->breaker.tripped.load() && !core_->breaker.pause_over()) {
    return;
  }
  last_kick_ = now;
  worker_->kick();
}

bool
QarCApiPanelSenderAdapter::submit_frame(const seam::PanelPixels & pixels)
{
  if (!frame_reserved_) {
    last_error_ = "submit_frame without a reserved cycle";
    return false;
  }
  // Whatever happens below, the reservation is consumed: leaving it set would
  // make the next tick believe a cycle is still open.
  frame_reserved_ = false;

  if (worker_ != nullptr) {
    return submit_on_worker(pixels);
  }

  if (pixels.data == nullptr) {
    last_error_ = "submit_frame got a null pixel buffer";
    return false;
  }

  if (core_->breaker.blocked()) {
    last_error_ = "frame calls stopped: a frame call blocked";
    return false;
  }

  QarVideoFrameCpu frame = qar_video_frame_cpu_default();
  const auto frame_started = std::chrono::steady_clock::now();
  const QarResult r = QAR_CALL(qar_video_sender_frame_cpu, sender_, &frame);
  core_->breaker.note(desc_.key, "frame_cpu", frame_started);
  if (qar_result_is_error(r)) {
    last_error_ = panel_result_error("qar_video_sender_frame_cpu", r);
    return false;
  }
  if (core_->breaker.tripped.load()) {
    last_error_ = "frame calls stopped: a frame call blocked";
    return false;
  }
  if (frame.textures_count == 0 || frame.textures[0].texture_data == nullptr) {
    last_error_ = "qar_video_sender_frame_cpu returned no writable texture";
    return false;
  }

  const std::string copy_error = write_panel_pixels(desc_, frame.textures[0], pixels);
  if (!copy_error.empty()) {
    last_error_ = copy_error;
    return false;
  }

  const auto show_started = std::chrono::steady_clock::now();
  const QarResult shown = QAR_CALL(qar_video_sender_show_frame, sender_);
  core_->breaker.note(desc_.key, "show_frame", show_started);
  if (qar_result_is_error(shown)) {
    last_error_ = panel_result_error("qar_video_sender_show_frame", shown);
    return false;
  }
  core_->note_shown();
  last_error_.clear();
  return true;
}

bool
QarCApiPanelSenderAdapter::submit_on_worker(const seam::PanelPixels & pixels)
{
  Core::Frame & frame = core_->frame;
  if (pixels.data == nullptr) {
    last_error_ = "submit_frame got a null pixel buffer";
    return false;
  }
  const std::uint32_t row_bytes = desc_.buffer_width * 4u;
  if (pixels.stride_bytes < row_bytes) {
    last_error_ = "panel frame pitch is smaller than a row of pixels";
    return false;
  }
  {
    // The frame is copied into the staging buffer, which replaces a frame that is still staged.
    // The worker takes the buffer under the same lock and never holds it across a QarOS call.
    std::lock_guard<std::mutex> lock(frame.mutex);
    frame.staged.resize(static_cast<std::size_t>(row_bytes) * desc_.buffer_height);
    for (std::uint32_t y = 0; y < desc_.buffer_height; ++y) {
      std::memcpy(
        frame.staged.data() + static_cast<std::size_t>(y) * row_bytes,
        pixels.data + static_cast<std::size_t>(y) * pixels.stride_bytes,
        row_bytes);
    }
    frame.has_staged = true;
  }
  worker_->hand_over();
  last_error_.clear();
  return true;
}

// The job of the frame worker: sends the staged frame in one go (reserves the cycle, gets the
// destination, copies the rows into it, shows the frame), so that no cycle outlives the job. A
// frame that finds no destination stays staged. Every QarOS call is made here, none with a lock
// held.
void
QarCApiPanelSenderAdapter::run_send(const std::shared_ptr<Core> & core)
{
  Core::Frame & frame = core->frame;
  {
    std::lock_guard<std::mutex> lock(frame.mutex);
    if (!frame.has_staged) {
      return;
    }
    frame.staged.swap(frame.working);
    frame.has_staged = false;
  }
  const Core::Phase phase = core->phase.load(std::memory_order_acquire);
  QarVideoSender * sender = nullptr;
  if (phase == Core::Phase::kReady ||
    (phase == Core::Phase::kCreating && core->frames_open.load(std::memory_order_acquire)))
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    sender = core->sender;
  }
  if (sender == nullptr || core->breaker.blocked()) {
    frame.park(seam::PanelNotReadyCause::NoDestination);
    return;
  }

  bool sender_ready = false;
  const auto begin_started = std::chrono::steady_clock::now();
  const QarResult r = QAR_CALL(qar_video_sender_try_begin_frame, sender, &sender_ready);
  core->breaker.note(core->desc.key, "try_begin_frame", begin_started);
  if (core->breaker.tripped.load()) {
    frame.park(seam::PanelNotReadyCause::NoDestination);
    return;
  }
  seam::PanelNotReadyCause cause = seam::PanelNotReadyCause::NoDestination;
  std::string error;
  switch (core->classify_begin(r, sender_ready, cause, error)) {
    case Core::Begin::kError:
      frame.set_error(error);
      return;
    case Core::Begin::kNotReady:
      frame.park(cause);
      return;
    case Core::Begin::kReady:
      break;
  }

  QarVideoFrameCpu cpu = qar_video_frame_cpu_default();
  const auto cpu_started = std::chrono::steady_clock::now();
  const QarResult got = QAR_CALL(qar_video_sender_frame_cpu, sender, &cpu);
  core->breaker.note(core->desc.key, "frame_cpu", cpu_started);
  if (qar_result_is_error(got)) {
    frame.set_error(panel_result_error("qar_video_sender_frame_cpu", got));
    return;
  }
  if (core->breaker.tripped.load()) {
    frame.set_error("frame calls stopped: a frame call blocked");
    return;
  }
  if (cpu.textures_count == 0 || cpu.textures[0].texture_data == nullptr) {
    frame.set_error("qar_video_sender_frame_cpu returned no writable texture");
    return;
  }
  // The destination belongs to the cycle this job reserved; it is written here and nowhere else.
  seam::PanelPixels pixels;
  pixels.data = frame.working.data();
  pixels.width = core->desc.buffer_width;
  pixels.height = core->desc.buffer_height;
  pixels.stride_bytes = core->desc.buffer_width * 4u;
  const std::string copy_error = write_panel_pixels(core->desc, cpu.textures[0], pixels);
  if (!copy_error.empty()) {
    frame.set_error(copy_error);
    return;
  }
  const auto show_started = std::chrono::steady_clock::now();
  const QarResult shown = QAR_CALL(qar_video_sender_show_frame, sender);
  core->breaker.note(core->desc.key, "show_frame", show_started);
  if (qar_result_is_error(shown)) {
    frame.set_error(panel_result_error("qar_video_sender_show_frame", shown));
    return;
  }
  core->note_shown();
  frame.sent();
}

bool
QarCApiPanelSenderAdapter::set_placement(const seam::PanelPlacement & placement)
{
  const Core::Phase phase = core_->phase.load(std::memory_order_acquire);
  if (closed_ || (phase != Core::Phase::kCreating && phase != Core::Phase::kReady)) {
    last_error_ = "no live panel to place";
    return false;
  }
  bool queue = false;
  {
    // The newest placement wins: one task is queued at most and applies what was asked last.
    std::lock_guard<std::mutex> lock(core_->mutex);
    core_->wanted = placement;
    core_->has_wanted = true;
    queue = !core_->place_queued;
    core_->place_queued = true;
  }
  if (queue) {
    const std::shared_ptr<Core> core = core_;
    const bool posted = owner_->post_session_task(
      ("panel-place:" + desc_.key).c_str(),
      [core](QarSession * session) { run_place(session, core); },
      [core] {
        std::lock_guard<std::mutex> lock(core->mutex);
        core->place_queued = false;
        core->has_wanted = false;
      });
    if (!posted) {
      std::lock_guard<std::mutex> lock(core_->mutex);
      core_->place_queued = false;
      core_->has_wanted = false;
      last_error_ = "QAR session is not accepting calls";
      return false;
    }
  }
  last_error_.clear();
  return true;
}

bool
QarCApiPanelSenderAdapter::set_state(seam::PanelState wanted)
{
  const QarGuiPanelState state = to_c(wanted);
  const Core::Phase phase = core_->phase.load(std::memory_order_acquire);
  if (closed_ || (phase != Core::Phase::kCreating && phase != Core::Phase::kReady)) {
    last_error_ = "no live panel to change";
    return false;
  }
  const std::shared_ptr<Core> core = core_;
  core->keeper->request(state);
  const bool posted = owner_->post_session_task(
    ("panel-state:" + desc_.key).c_str(),
    [core, state](QarSession * session) { run_state(session, core, state); },
    [core] { core->keeper->request_dropped(); });
  if (!posted) {
    core->keeper->request_dropped();
    last_error_ = "QAR session is not accepting calls";
    return false;
  }
  last_error_.clear();
  return true;
}

void
QarCApiPanelSenderAdapter::set_state_guard_clock(qar_qar_adapter::PanelStateKeeper::ClockFn now)
{
  core_->keeper->set_clock(std::move(now));
}

seam::PanelCloseReport
QarCApiPanelSenderAdapter::close(bool panel_closed_in_ar)
{
  return teardown(panel_closed_in_ar, /*keep_panel=*/false);
}

void
QarCApiPanelSenderAdapter::detach()
{
  teardown(/*panel_closed_in_ar=*/false, /*keep_panel=*/true);
}

seam::PanelCloseReport
QarCApiPanelSenderAdapter::teardown(bool panel_closed_in_ar, bool keep_panel)
{
  seam::PanelCloseReport report;
  // The second close — the destructor after the stream's own — has nothing left
  // to do, and must not do the first step again: unsubscribe_panel() is keyed by
  // PANEL KEY, so a late one would cancel the subscriptions of a newer adapter
  // that has since taken the same key (a dialog opened again, a dock restreamed).
  if (closed_) {
    return report;
  }
  closed_ = true;
  frame_reserved_ = false;
  sender_ = nullptr;
  // From here no frame call is made on the sender, so the thread that owns the session may
  // destroy it.
  core_->phase.store(Core::Phase::kClosed, std::memory_order_release);
  core_->keeper->retire();
  if (worker_ != nullptr) {
    // The worker makes no further frame call after the one it is in; it then posts the release
    // of the video sender, which the teardown task below leaves to it when it is the last one.
    worker_->stop();
  }

  if (input_ && !desc_.key.empty()) {
    report.input_unsubscribed = input_->has_panel(desc_.key);
  }
  if (owner_ == nullptr) {
    return report;
  }
  const std::shared_ptr<Core> core = core_;
  const std::shared_ptr<qar_qar_adapter::IReleaseLane> lane = release_lane_;
  const bool posted = owner_->post_session_task(
    ((keep_panel ? "panel-detach:" : "panel-close:") + desc_.key).c_str(),
    [core, panel_closed_in_ar, keep_panel](QarSession * session) {
      run_teardown(session, core, panel_closed_in_ar, keep_panel);
    },
    [core, lane] {
      // The task will not run: the session it was for is gone or going.
      core->keeper->release_on(lane);
      release_sender_on_lane(core, lane);
    });
  if (posted) {
    report.panel = seam::PanelCloseOutcome::Queued;
    return report;
  }
  // No task can be queued (the session is lost or ending). The video sender and the input tokens
  // belong to the session, so they go to the thread that owns it, the watch of the panel's state
  // with them. The panel in the Hub is left as it is; the next session finds it again under its
  // common name.
  core->keeper->release_on(lane);
  if (input_ && !desc_.key.empty() && !refused_) {
    input_->unsubscribe_panel_on_lane(desc_.key);
  }
  release_sender_on_lane(core, lane);
  return report;
}

void
QarCApiPanelSenderAdapter::release_sender_on_lane(
  const std::shared_ptr<Core> & core, const std::shared_ptr<qar_qar_adapter::IReleaseLane> & lane)
{
  if (!core->arrive() || lane == nullptr) {
    return;
  }
  const bool posted = lane->post_release(
    ("panel-video-sender-release:" + core->desc.key).c_str(), [core] {
      QarVideoSender * sender = nullptr;
      {
        std::lock_guard<std::mutex> lock(core->mutex);
        sender = core->sender;
        core->sender = nullptr;
      }
      if (sender != nullptr) {
        QAR_CALL(qar_video_sender_handle_destroy, sender);
      }
    });
  if (!posted) {
    std::fprintf(
      stderr, "[qar-panel-adapter] %s panel '%s' video sender left unreleased (the owner thread "
      "is gone)\n", local_time_hms_ms().c_str(), core->desc.key.c_str());
    std::fflush(stderr);
  }
}

bool
QarCApiPanelSenderAdapter::frame_calls_stopped() const
{
  return core_->breaker.tripped.load() || (worker_ != nullptr && worker_->waiting());
}

void
QarCApiPanelSenderAdapter::set_frame_breaker_timings(const FrameBreakerTimings & timings)
{
  core_->breaker.set_timings(timings);
}

// The one teardown, in the one safe order, on the thread that owns the session. Idempotent per
// Core.
void
QarCApiPanelSenderAdapter::run_teardown(
  QarSession * session, const std::shared_ptr<Core> & core, bool panel_closed_in_ar,
  bool keep_panel)
{
  QarVideoSender * sender = nullptr;
  QarGuiPanelId panel_id{};
  std::string stream_id_text;
  bool has_panel = false;
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    sender = core->sender;
    panel_id = core->panel_id;
    stream_id_text = core->stream_id_text;
    has_panel = core->has_panel;
    core->has_panel = false;
  }
  const std::string & key = core->desc.key;
  const bool something_to_close = has_panel || sender != nullptr;
  const auto started = std::chrono::steady_clock::now();
  const auto step_line = [&key](const char * what) {
    // Every line below carries the local wall-clock time, because the only way to
    // line a close up against a QarOS crash dump in %TEMP%\quaternar is by the
    // clock. Printed and flushed BEFORE each runtime call that follows it, so if
    // the process dies inside that call the last line in the log names it.
    std::fprintf(
      stderr, "[qar-panel-adapter] %s panel '%s' %s\n", local_time_hms_ms().c_str(),
      key.c_str(), what);
    std::fflush(stderr);
  };
  if (something_to_close) {
    char head[256] = {0};
    if (keep_panel) {
      std::snprintf(
        head, sizeof(head) - 1, "detaching: id=%s stream=%s (the GUI panel is kept)",
        panel_id_text(panel_id).c_str(), stream_id_text.c_str());
    } else {
      std::snprintf(
        head, sizeof(head) - 1,
        "closing: id=%s stream=%s (operator already closed it in AR: %s)",
        panel_id_text(panel_id).c_str(), stream_id_text.c_str(),
        panel_closed_in_ar ? "yes" : "no");
    }
    step_line(head);
  }

  // The watch of the panel's state ends with the input subscriptions: from here on the runtime's
  // republishing of the panel, our own close included, asks for nothing.
  core->keeper->stop();

  // STEP 1 — input FIRST, before the panel it is about. unsubscribe_panel does
  // not return until no click/hover callback for this panel is running, so from
  // here on nothing can arrive naming a panel that is being destroyed. It also
  // retires the panel-UPDATE subscription, which is what keeps the CLOSED state
  // the runtime republishes for OUR close_panel below from coming back to us as
  // "the operator closed this panel in AR".
  //
  // Only the subscription that names THIS panel ends. The key keeps one subscription, on the
  // panel made or shown last: when that is another panel (a newer adapter took the key), it is
  // not ours to cancel.
  if (core->input && !key.empty()) {
    const std::optional<QarGuiPanelId> subscribed = core->input->subscribed_id(key);
    const bool had_input =
      has_panel && subscribed.has_value() &&
      std::memcmp(subscribed->data, panel_id.data, sizeof(panel_id.data)) == 0;
    if (had_input) {
      core->input->unsubscribe_panel(key);
      step_line("step 1/3: AR input + panel-update subscriptions cancelled");
    }
  }

  // STEP 2 — the panel, then (step 3) the stream it points at: closing the sender
  // first would leave a live panel referencing a retired stream. The runtime
  // confirms the order, also for dialog panels: the panel's State reaches 3
  // (closed) about a millisecond before the sender teardown begins.
  seam::PanelCloseReport report;
  if (has_panel && keep_panel) {
    // Detach: the QarOS panel stays as it is (the caller hid it), and the next session finds it
    // again under its common name.
    step_line("step 2/3: GUI panel kept for the next session");
  } else if (has_panel) {
    if (panel_closed_in_ar) {
      // The operator already closed it: the runtime has removed the panel, and a
      // second close of the same id is a call we have no reason to make and no
      // documented behaviour for. Skipped, and said so.
      report.panel = seam::PanelCloseOutcome::AlreadyClosedInAr;
      step_line(
        "step 2/3: qar_gui_panels_close_panel SKIPPED -- the operator already closed this "
        "panel in AR");
    } else {
      // The result is CHECKED, not discarded. A close that failed would leave a
      // panel in the session pointing at the sender destroyed immediately below —
      // an orphan, and an invisible one if this call's QarResult were thrown
      // away. panel_result_error() also echoes it to stderr.
      const QarResult closed = QAR_CALL(qar_gui_panels_close_panel, session, &panel_id);
      if (qar_result_is_error(closed)) {
        const std::string error = panel_result_error("qar_gui_panels_close_panel", closed);
        report.panel = seam::PanelCloseOutcome::Failed;
        report.panel_message = error;
        step_line(
          ("step 2/3: " + error +
          " -- the video sender is destroyed regardless; the AR panel may still be on screen")
          .c_str());
      } else {
        report.panel = seam::PanelCloseOutcome::Closed;
        step_line("step 2/3: qar_gui_panels_close_panel ran, ok");
      }
    }
  }

  // STEP 3 — the video sender, unconditionally. Announced BEFORE the call (the
  // riskiest of the three: it is the one a crash dump has been about). A frame worker that is
  // still inside a call on the sender destroys it, through the lane, when it has left: the last
  // of this step and the worker's exit is the one that destroys it.
  bool left_to_worker = false;
  if (core->arrive()) {
    QarVideoSender * doomed = nullptr;
    {
      std::lock_guard<std::mutex> lock(core->mutex);
      doomed = core->sender;
      core->sender = nullptr;
    }
    if (doomed != nullptr) {
      step_line("step 3/3: destroying the video sender");
      QAR_CALL(qar_video_sender_handle_destroy, doomed);
      report.sender_destroyed = true;
    }
  } else if (sender != nullptr) {
    step_line("step 3/3: the video sender is destroyed when the frame worker has left");
    left_to_worker = true;
  }

  // The retirement line, and the counterpart to the creation line above: what this
  // close actually did, as the outcome of the steps just logged, so one grep on
  // "retired:" answers "was the AR panel closed, and is the sender gone" for a stream id.
  //
  // The line states the outcome and nothing else. The C API has no call to withdraw a
  // stream (qar_video_sender_handle_destroy returns void, qar_gui_panels_close_panel
  // closes the PANEL, and neither tells a receiver already attached to the stream that
  // it has gone: the receiver logs "connection lost for stream ..., entering reconnecting
  // state" and retries every ten seconds, see
  // docs/SDK_COMPATIBILITY.md#known-sdk-limitations). The line is printed on EVERY
  // close and the sender IS destroyed on every one, so a standing warning about a stream
  // "kept" would say the opposite of what happened.
  bool print_retired = false;
  {
    std::lock_guard<std::mutex> lock(core->mutex);
    print_retired = !stream_id_text.empty() && !core->retired_logged;
    core->retired_logged = true;
  }
  if (print_retired) {
    const long long took_ms = static_cast<long long>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    const char * gui_panel = keep_panel ? "kept" : "none was open";
    switch (report.panel) {
      case seam::PanelCloseOutcome::NoPanel:
      case seam::PanelCloseOutcome::Queued:
        break;
      case seam::PanelCloseOutcome::Closed:
        gui_panel = "closed";
        break;
      case seam::PanelCloseOutcome::AlreadyClosedInAr:
        gui_panel = "close skipped (the operator had already closed it in AR)";
        break;
      case seam::PanelCloseOutcome::Failed:
        gui_panel = "close FAILED (see step 2/3 above)";
        break;
    }
    std::fprintf(
      stderr,
      "[qar-panel-adapter] %s panel '%s' retired: id=%s stream=%s took=%lldms -- GUI panel %s, "
      "video sender %s\n",
      local_time_hms_ms().c_str(), key.c_str(), panel_id_text(panel_id).c_str(),
      stream_id_text.c_str(), took_ms, gui_panel,
      report.sender_destroyed ? "destroyed" :
      left_to_worker ? "released by the frame worker" : "none to destroy");
    std::fflush(stderr);
  }
}

QarCApiPanelSenderFactory::QarCApiPanelSenderFactory(
  qar_qar_adapter::IQarSessionOwner * session_owner, std::shared_ptr<QarCApiPanelInputSource> input)
: session_owner_(session_owner),
  input_(std::move(input))
{
}

std::shared_ptr<seam::IPanelSender>
QarCApiPanelSenderFactory::create_panel_sender(const seam::PanelDesc & desc)
{
  if (session_owner_ == nullptr) {
    last_error_ = "no QAR session owner";
    return nullptr;
  }
  if (!session_owner_->ready()) {
    last_error_ = "QAR session is not ready yet";
    return nullptr;
  }

  // The threads of frame workers that have ended are joined here.
  qar_qar_adapter::FrameWorker::reap();
  if (frame_calls_noted_.first()) {
    std::fprintf(
      stderr, "[qar-panel-adapter] %s frame calls run on %s (QAR_FRAME_CALLS=%s)\n",
      local_time_hms_ms().c_str(),
      frame_calls_.on_worker ? "one worker thread per panel" : "the GUI thread",
      frame_calls_.on_worker ? "worker" : "gui");
    std::fflush(stderr);
  }

  // The panel and its video sender are made on the thread that owns the session; the adapter
  // is handed back at once, still "creating".
  auto adapter = std::make_shared<QarCApiPanelSenderAdapter>(
    session_owner_, desc, input_, frame_calls_);
  if (adapter->refused()) {
    last_error_ = adapter->last_error();
    return nullptr;
  }
  last_error_.clear();
  return adapter;
}

}  // namespace qar_capi_panels
