// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__QT_STALL_WATCHDOG_HPP_
#define QAR_RVIZ_STREAMING__QT_STALL_WATCHDOG_HPP_

/// @file qt_stall_watchdog.hpp
/// @brief Names the step the Qt thread was in when it stops serving the render tick.
///
/// ## The problem this exists for
///
/// A call that never returns on the Qt thread (a graphics driver call while the GPU is faulted,
/// a lock, a wait) freezes RViz. The heartbeat of the panel reports the length of the gap only
/// after the thread runs again, and it cannot say where the thread waited.
///
/// ## What it does
///
///  * The Qt thread calls @ref QtStallWatchdog::tick once per tick of the panel and
///    @ref QtStallWatchdog::set_step before each step of the tick. Both are a few atomic stores
///    and one clock read; they take no lock and allocate nothing.
///  * A watchdog thread wakes four times per second. When the tick counter has not moved for
///    the stall limit it writes one `[qar-stall]` line that names the last step, and when the
///    ticks come back it writes one more. It never touches Qt, Ogre or QAROS.
///  * The line is formatted into a buffer on the thread's stack and handed to the sink as raw
///    bytes. The production sink is a `WriteFile` on a handle taken at start: no CRT stdio, no
///    lock that the Qt thread may hold, no allocation on the way to the file.
///  * Optionally (a dump writer is given) one minidump of the process is written, once, after a
///    longer stall.
///
/// The clock, the sink and the dump writer are interfaces so that the unit tests need none of
/// the platform: they drive @ref QtStallWatchdog::poll with a fake clock.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace qar_rviz_streaming
{

/// @brief Time source of the watchdog.
class StallClock
{
public:
  virtual ~StallClock() = default;

  /// @brief A monotonic time in nanoseconds.
  virtual std::int64_t steady_ns() const = 0;

  /// @brief The local time of day, in milliseconds since midnight, of the steady instant
  /// @p at_ns (an instant not later than steady_ns()).
  virtual std::int64_t local_ms_of_day(std::int64_t at_ns) const = 0;
};

/// @brief Where the lines go. Called from the watchdog thread only.
class StallSink
{
public:
  virtual ~StallSink() = default;

  /// @brief Write @p length bytes (a complete line, ending in a line feed). Must not allocate
  /// and must not take a lock that the Qt thread can hold.
  virtual void write(const char * text, std::size_t length) = 0;
};

/// @brief Writes a dump of the process. Called from the watchdog thread only, once per run.
class StallDumpWriter
{
public:
  virtual ~StallDumpWriter() = default;

  /// @brief Write the dump. On success returns true and puts the path of the file into
  /// @p detail; on failure returns false and puts the reason there. Both are NUL-terminated
  /// text of at most @p capacity bytes.
  virtual bool write_dump(char * detail, std::size_t capacity) = 0;
};

/// @brief The limits of the watchdog.
struct StallWatchdogConfig
{
  /// No tick for this long is a stall.
  std::int64_t stall_after_ns = 3'000'000'000;
  /// The watchdog thread wakes this often.
  std::int64_t poll_every_ns = 250'000'000;
  /// With a dump writer: no tick for this long writes the dump (once per run).
  std::int64_t dump_after_ns = 5'000'000'000;
};

/// @brief What the watchdog saw of the stall that ended last (see
/// @ref QtStallWatchdog::take_report).
struct StallReport
{
  /// The length of the gap between the last tick before it and the first one after it.
  double gap_s = 0.0;
  /// The step the Qt thread was in; a string literal. Never null.
  const char * step = "";
  /// Local time of day (ms since midnight) at which the gap began.
  std::int64_t began_ms_of_day = 0;
  /// Local time of day (ms since midnight) at which that step began.
  std::int64_t step_since_ms_of_day = 0;
};

/// @brief Formats a time of day as `HH:MM:SS.mmm` into @p out (at least 13 bytes, NUL-ended).
void format_stall_clock(std::int64_t ms_of_day, char * out);

/// @brief The step the Qt thread reports between two ticks.
inline constexpr const char * kStallStepOutsideTick = "outside the plugin tick";

/// @brief The stall watchdog.
class QtStallWatchdog
{
public:
  /// @param clock Not owned; outlives the watchdog.
  /// @param sink Not owned; outlives the watchdog.
  /// @param dump Not owned, may be null (no dump).
  QtStallWatchdog(
    const StallWatchdogConfig & config, const StallClock * clock, StallSink * sink,
    StallDumpWriter * dump = nullptr);
  virtual ~QtStallWatchdog();

  QtStallWatchdog(const QtStallWatchdog &) = delete;
  QtStallWatchdog & operator=(const QtStallWatchdog &) = delete;

  // --- Qt thread ------------------------------------------------------------------------

  /// @brief One tick of the panel. The first call arms the watchdog.
  void tick();

  /// @brief The step that follows. @p step must be a string literal (or any text that lives
  /// as long as the watchdog); it is stored as a pointer.
  void set_step(const char * step);

  /// @brief The step that is current.
  const char * step() const { return step_.load(std::memory_order_relaxed); }

  /// @brief Stop (true) or resume (false) the watching for a reason known to the caller, such
  /// as a stopped tick timer. A paused watchdog writes nothing, and the time of the pause is
  /// not counted when it resumes.
  void set_paused(bool paused);

  /// @brief The period of the tick timer. The stall limit is never shorter than four periods.
  void set_tick_period_ms(double period_ms);

  /// @brief The report of a stall that has ended, once per stall. False when there is none
  /// since the last call. The Qt thread uses it to mirror the stall into the panel log.
  bool take_report(StallReport * report);

  // --- Owner -----------------------------------------------------------------------------

  /// @brief Start the watchdog thread. Idempotent.
  void start();

  /// @brief Stop the thread and join it. The thread never calls Qt, Ogre or QAROS, so the state
  /// of the Qt thread does not delay it: it ends within about one poll step, unless it is
  /// inside the dump writer (or in a sink write that does not return). The wait for the thread
  /// is three seconds at most: then one line is written, the thread is left running detached
  /// (it reads nothing of this object once the writer returns) and stop() returns.
  /// Idempotent; the destructor calls it.
  void stop();

  // --- Watchdog thread (public so a test can drive it with a fake clock) ---------------

  /// @brief One look at the counters at the clock's current time.
  void poll();

  /// @name Counters (any thread)
  /// @{
  std::uint64_t stall_lines() const { return stall_lines_.load(std::memory_order_relaxed); }
  std::uint64_t resume_lines() const { return resume_lines_.load(std::memory_order_relaxed); }
  std::uint64_t dumps_written() const { return dumps_.load(std::memory_order_relaxed); }
  /// @}

private:
  void run(std::shared_ptr<std::atomic<bool>> abandoned);
  /// poll() for the watchdog thread: true when the thread must end at once, because stop() left
  /// it behind while it was inside the dump writer (nothing of this object may be touched then).
  bool poll_thread(const std::atomic<bool> & abandoned);
  void write_text(const char * text, std::size_t length);
  std::int64_t effective_stall_ns() const;

  StallWatchdogConfig config_;
  const StallClock * clock_;
  StallSink * sink_;
  StallDumpWriter * dump_;

  // The Qt thread writes these; the watchdog thread reads them.
  std::atomic<std::uint64_t> ticks_{0};
  std::atomic<std::int64_t> last_tick_ns_{0};
  std::atomic<const char *> step_;
  std::atomic<std::int64_t> step_since_ns_{0};
  std::atomic<bool> paused_{false};
  std::atomic<std::int64_t> stall_limit_ns_;
  // The last gap that reached the stall limit, kept by tick() so that the resume line can give
  // the gap exactly (the watchdog thread sees the tick only at its next look).
  std::atomic<std::uint64_t> long_gaps_{0};
  std::atomic<std::int64_t> long_gap_ns_{0};
  std::atomic<std::int64_t> long_gap_began_ns_{0};

  // The watchdog thread writes these; the Qt thread reads them in take_report().
  std::atomic<std::uint64_t> report_seq_{0};
  std::atomic<std::int64_t> report_gap_ns_{0};
  std::atomic<const char *> report_step_;
  std::atomic<std::int64_t> report_began_ms_{0};
  std::atomic<std::int64_t> report_step_since_ms_{0};
  std::uint64_t report_taken_ = 0;  // Qt thread only

  // Watchdog thread only.
  bool in_stall_ = false;
  bool dumped_ = false;
  std::uint64_t stall_ticks_ = 0;
  std::uint64_t stall_long_gaps_ = 0;
  const char * stall_step_ = "";
  std::int64_t stall_step_since_ns_ = 0;
  std::int64_t stall_began_ns_ = 0;

  std::atomic<std::uint64_t> stall_lines_{0};
  std::atomic<std::uint64_t> resume_lines_{0};
  std::atomic<std::uint64_t> dumps_{0};

  std::atomic<bool> dump_active_{false};

  std::thread thread_;
  std::mutex wake_mutex_;
  std::condition_variable wake_;
  bool stop_requested_ = false;
  bool thread_done_ = false;
  // Shared with the thread: set by stop() when it leaves the thread behind.
  std::shared_ptr<std::atomic<bool>> abandoned_;
};

#ifdef _WIN32
/// @brief The clock of this machine: `steady_clock` and the local time of day.
class SystemStallClock : public StallClock
{
public:
  std::int64_t steady_ns() const override;
  std::int64_t local_ms_of_day(std::int64_t at_ns) const override;
};

/// @brief Writes each line with one `WriteFile` call on a handle that the caller keeps open.
class FileHandleStallSink : public StallSink
{
public:
  /// @param handle A file or console `HANDLE`; null or invalid makes every write a no-op.
  explicit FileHandleStallSink(void * handle);
  void write(const char * text, std::size_t length) override;

private:
  void * handle_;
};

/// @brief Writes `rviz_stall_<stamp>.dmp` (thread stacks only) with `MiniDumpWriteDump`.
///
/// The dump is written from the calling thread, inside the process. That is not what Microsoft
/// recommends (a process should be dumped from another process): a thread that is suspended
/// by the dump while it holds the heap or loader lock can deadlock the dump, dbghelp is not
/// thread safe, and a crash handler in the same process may use it at the same time.
/// `MiniDumpWriteDump` suspends the other threads of the process while it reads them. A dump
/// that hangs may therefore keep the Qt thread suspended and turn a stall that would have ended
/// into one that does not (expected from the documented behaviour of the call, not measured
/// here). The dump is off unless it is asked for. A dump that has not returned when the
/// watchdog is stopped is left behind after three seconds (see @ref QtStallWatchdog::stop), and
/// dbghelp then stays loaded for the rest of the process. The writer loads dbghelp and creates
/// the folder when it is made; the file is created when the dump is written.
class MiniDumpStallWriter : public StallDumpWriter
{
public:
  /// @param directory Where the file goes (created if missing).
  /// @param min_free_bytes The dump is refused when the volume has less free space.
  MiniDumpStallWriter(const std::wstring & directory, std::uint64_t min_free_bytes);
  ~MiniDumpStallWriter() override;
  bool write_dump(char * detail, std::size_t capacity) override;

private:
  std::wstring directory_;
  std::uint64_t min_free_bytes_;
  void * dbghelp_ = nullptr;
  void * write_fn_ = nullptr;
};
#endif  // _WIN32

/// @brief A watchdog with the clock, the sink and (optionally) the dump writer of this platform.
///
/// The sink writes to the standard error handle of the process with one `WriteFile` call per
/// line. The dump writer (@p with_dump) writes `rviz_stall_<stamp>.dmp` into
/// `%TEMP%\qar_rviz_logs` and refuses when the volume has less than 2 GB free.
std::unique_ptr<QtStallWatchdog> make_system_stall_watchdog(bool with_dump);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__QT_STALL_WATCHDOG_HPP_
