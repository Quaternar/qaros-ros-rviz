// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/qt_stall_watchdog.hpp"

#include <algorithm>
#include <cwchar>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <time.h>
#include <unistd.h>
#endif

namespace qar_rviz_streaming
{

namespace
{

constexpr std::int64_t kMsPerDay = 24LL * 60 * 60 * 1000;
constexpr std::int64_t kNsPerMs = 1'000'000;

/// A line in a buffer on the stack. Nothing here allocates, locks or calls the C library.
class LineBuilder
{
public:
  void text(const char * s)
  {
    if (s == nullptr) {
      return;
    }
    while (*s != '\0' && length_ < kCapacity) {
      buffer_[length_++] = *s++;
    }
  }

  void unsigned_number(std::uint64_t value)
  {
    char digits[24];
    int count = 0;
    do {
      digits[count++] = static_cast<char>('0' + value % 10);
      value /= 10;
    } while (value != 0);
    while (count > 0 && length_ < kCapacity) {
      buffer_[length_++] = digits[--count];
    }
  }

  /// Seconds with one decimal, cut (not rounded) from a length in nanoseconds.
  void seconds(std::int64_t ns)
  {
    const std::uint64_t tenths = ns > 0 ? static_cast<std::uint64_t>(ns / 100'000'000) : 0;
    unsigned_number(tenths / 10);
    text(".");
    unsigned_number(tenths % 10);
  }

  void clock(std::int64_t ms_of_day)
  {
    char stamp[16];
    format_stall_clock(ms_of_day, stamp);
    text(stamp);
  }

  /// Ends the line with a line feed and gives its length.
  std::size_t finish()
  {
    if (length_ >= kCapacity) {
      length_ = kCapacity - 1;
    }
    buffer_[length_++] = '\n';
    return length_;
  }

  const char * data() const { return buffer_; }

private:
  static constexpr std::size_t kCapacity = 400;
  char buffer_[kCapacity + 1];
  std::size_t length_ = 0;
};

}  // namespace

void format_stall_clock(std::int64_t ms_of_day, char * out)
{
  std::int64_t ms = ms_of_day % kMsPerDay;
  if (ms < 0) {
    ms += kMsPerDay;
  }
  const int hours = static_cast<int>(ms / 3'600'000);
  const int minutes = static_cast<int>(ms / 60'000 % 60);
  const int seconds = static_cast<int>(ms / 1000 % 60);
  const int millis = static_cast<int>(ms % 1000);
  out[0] = static_cast<char>('0' + hours / 10);
  out[1] = static_cast<char>('0' + hours % 10);
  out[2] = ':';
  out[3] = static_cast<char>('0' + minutes / 10);
  out[4] = static_cast<char>('0' + minutes % 10);
  out[5] = ':';
  out[6] = static_cast<char>('0' + seconds / 10);
  out[7] = static_cast<char>('0' + seconds % 10);
  out[8] = '.';
  out[9] = static_cast<char>('0' + millis / 100);
  out[10] = static_cast<char>('0' + millis / 10 % 10);
  out[11] = static_cast<char>('0' + millis % 10);
  out[12] = '\0';
}

QtStallWatchdog::QtStallWatchdog(
  const StallWatchdogConfig & config, const StallClock * clock, StallSink * sink,
  StallDumpWriter * dump)
: config_(config), clock_(clock), sink_(sink), dump_(dump),
  step_(kStallStepOutsideTick), stall_limit_ns_(config.stall_after_ns), report_step_("")
{
}

QtStallWatchdog::~QtStallWatchdog()
{
  stop();
}

void QtStallWatchdog::tick()
{
  const std::int64_t now = clock_->steady_ns();
  const std::int64_t previous = last_tick_ns_.load(std::memory_order_relaxed);
  if (previous != 0 && !paused_.load(std::memory_order_relaxed) &&
    now - previous >= stall_limit_ns_.load(std::memory_order_relaxed))
  {
    long_gap_ns_.store(now - previous, std::memory_order_relaxed);
    long_gap_began_ns_.store(previous, std::memory_order_relaxed);
    long_gaps_.fetch_add(1, std::memory_order_release);
  }
  step_.store(kStallStepOutsideTick, std::memory_order_relaxed);
  step_since_ns_.store(now, std::memory_order_relaxed);
  last_tick_ns_.store(now, std::memory_order_relaxed);
  ticks_.fetch_add(1, std::memory_order_release);
}

void QtStallWatchdog::set_step(const char * step)
{
  step_since_ns_.store(clock_->steady_ns(), std::memory_order_relaxed);
  step_.store(step != nullptr ? step : "", std::memory_order_release);
}

void QtStallWatchdog::set_paused(bool paused)
{
  if (!paused) {
    // The time of the pause is not a gap: the next tick measures from here.
    last_tick_ns_.store(clock_->steady_ns(), std::memory_order_relaxed);
  }
  paused_.store(paused, std::memory_order_release);
}

void QtStallWatchdog::set_tick_period_ms(double period_ms)
{
  const double period_ns = period_ms > 0.0 ? period_ms * 1.0e6 : 0.0;
  const std::int64_t four_periods = static_cast<std::int64_t>(4.0 * period_ns);
  stall_limit_ns_.store(std::max(config_.stall_after_ns, four_periods), std::memory_order_relaxed);
}

bool QtStallWatchdog::take_report(StallReport * report)
{
  const std::uint64_t seq = report_seq_.load(std::memory_order_acquire);
  if (seq == report_taken_) {
    return false;
  }
  report_taken_ = seq;
  if (report != nullptr) {
    report->gap_s = static_cast<double>(report_gap_ns_.load(std::memory_order_relaxed)) / 1.0e9;
    report->step = report_step_.load(std::memory_order_relaxed);
    report->began_ms_of_day = report_began_ms_.load(std::memory_order_relaxed);
    report->step_since_ms_of_day = report_step_since_ms_.load(std::memory_order_relaxed);
  }
  return true;
}

void QtStallWatchdog::start()
{
  std::lock_guard<std::mutex> lock(wake_mutex_);
  if (thread_.joinable()) {
    return;
  }
  stop_requested_ = false;
  thread_done_ = false;
  abandoned_ = std::make_shared<std::atomic<bool>>(false);
  thread_ = std::thread([this, abandoned = abandoned_] {run(abandoned);});
}

void QtStallWatchdog::stop()
{
  constexpr std::chrono::seconds kJoinLimit(3);
  std::unique_lock<std::mutex> lock(wake_mutex_);
  stop_requested_ = true;
  wake_.notify_all();
  if (!thread_.joinable()) {
    return;
  }
  if (wake_.wait_for(lock, kJoinLimit, [this] {return thread_done_;})) {
    lock.unlock();
    thread_.join();
    return;
  }
  // The thread is inside the dump writer (or a sink write) and does not come back. It is left
  // behind: from here on it touches nothing of this object, see poll_thread().
  abandoned_->store(true, std::memory_order_release);
  const bool in_dump = dump_active_.load(std::memory_order_relaxed);
  lock.unlock();
  LineBuilder line;
  line.text("[qar-stall] ");
  line.clock(clock_->local_ms_of_day(clock_->steady_ns()));
  line.text(" the watchdog thread did not end within 3 s");
  line.text(
    in_dump ? " (a stack dump is still being written); it is left running" :
    "; it is left running");
  write_text(line.data(), line.finish());
  thread_.detach();
}

void QtStallWatchdog::run(std::shared_ptr<std::atomic<bool>> abandoned)
{
  std::unique_lock<std::mutex> lock(wake_mutex_);
  while (!stop_requested_) {
    lock.unlock();
    if (poll_thread(*abandoned)) {
      return;  // stop() has given up on this thread: nothing of the object is touched
    }
    lock.lock();
    if (stop_requested_) {
      break;
    }
    wake_.wait_for(
      lock, std::chrono::nanoseconds(config_.poll_every_ns), [this] {return stop_requested_;});
  }
  thread_done_ = true;
  wake_.notify_all();
}

void QtStallWatchdog::write_text(const char * text, std::size_t length)
{
  if (sink_ != nullptr) {
    sink_->write(text, length);
  }
}

void QtStallWatchdog::poll()
{
  const std::atomic<bool> never_abandoned{false};
  poll_thread(never_abandoned);
}

bool QtStallWatchdog::poll_thread(const std::atomic<bool> & abandoned)
{
  const std::int64_t now = clock_->steady_ns();
  if (paused_.load(std::memory_order_acquire)) {
    in_stall_ = false;
    return false;
  }
  const std::uint64_t ticks = ticks_.load(std::memory_order_acquire);
  if (ticks == 0) {
    return false;  // not armed before the first tick
  }
  const std::int64_t last_tick = last_tick_ns_.load(std::memory_order_relaxed);
  const std::int64_t limit = stall_limit_ns_.load(std::memory_order_relaxed);

  if (in_stall_) {
    if (ticks != stall_ticks_) {
      // The ticks are back. The gap is the one the Qt thread measured at its first tick.
      std::int64_t gap_ns = last_tick - stall_began_ns_;
      std::int64_t began_ns = stall_began_ns_;
      if (long_gaps_.load(std::memory_order_acquire) != stall_long_gaps_) {
        gap_ns = long_gap_ns_.load(std::memory_order_relaxed);
        began_ns = long_gap_began_ns_.load(std::memory_order_relaxed);
      }
      const std::int64_t began_ms = clock_->local_ms_of_day(began_ns);
      const std::int64_t since_ms = clock_->local_ms_of_day(stall_step_since_ns_);
      LineBuilder line;
      line.text("[qar-stall] ");
      line.clock(clock_->local_ms_of_day(now));
      line.text(" Qt thread ticks again after ");
      line.seconds(gap_ns);
      line.text(" s (last step: ");
      line.text(stall_step_);
      line.text(")");
      // The report is published before the line is written: whoever sees the line finds it.
      report_gap_ns_.store(gap_ns, std::memory_order_relaxed);
      report_step_.store(stall_step_, std::memory_order_relaxed);
      report_began_ms_.store(began_ms, std::memory_order_relaxed);
      report_step_since_ms_.store(since_ms, std::memory_order_relaxed);
      report_seq_.fetch_add(1, std::memory_order_release);
      resume_lines_.fetch_add(1, std::memory_order_relaxed);
      write_text(line.data(), line.finish());
      in_stall_ = false;
      return false;
    }
    // Still silent: the step can move inside one tick, so the last one seen is kept.
    stall_step_ = step_.load(std::memory_order_acquire);
    stall_step_since_ns_ = step_since_ns_.load(std::memory_order_relaxed);
  } else if (now - last_tick >= limit) {
    in_stall_ = true;
    stall_ticks_ = ticks;
    stall_long_gaps_ = long_gaps_.load(std::memory_order_acquire);
    stall_began_ns_ = last_tick;
    stall_step_ = step_.load(std::memory_order_acquire);
    stall_step_since_ns_ = step_since_ns_.load(std::memory_order_relaxed);
    LineBuilder line;
    line.text("[qar-stall] ");
    line.clock(clock_->local_ms_of_day(now));
    line.text(" Qt thread has not ticked for ");
    line.seconds(now - last_tick);
    line.text(" s; last step: ");
    line.text(stall_step_);
    line.text(" (since ");
    line.clock(clock_->local_ms_of_day(stall_step_since_ns_));
    line.text(")");
    write_text(line.data(), line.finish());
    stall_lines_.fetch_add(1, std::memory_order_relaxed);
  }

  if (in_stall_ && dump_ != nullptr && !dumped_ &&
    now - last_tick >= std::max(config_.dump_after_ns, limit))
  {
    dumped_ = true;  // once per run, whatever the outcome
    char detail[300];
    detail[0] = '\0';
    dump_active_.store(true, std::memory_order_relaxed);
    const bool written = dump_->write_dump(detail, sizeof(detail));
    if (abandoned.load(std::memory_order_acquire)) {
      return true;
    }
    dump_active_.store(false, std::memory_order_relaxed);
    detail[sizeof(detail) - 1] = '\0';
    LineBuilder line;
    line.text("[qar-stall] ");
    line.clock(clock_->local_ms_of_day(clock_->steady_ns()));
    line.text(written ? " stack dump written: " : " stack dump not written: ");
    line.text(detail);
    write_text(line.data(), line.finish());
    if (written) {
      dumps_.fetch_add(1, std::memory_order_relaxed);
    }
  }
  return false;
}

// ------------------------------------------------------------------------------------------
// The platform: clock, sink, dump writer
// ------------------------------------------------------------------------------------------

namespace
{

std::int64_t steady_now_ns()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// The local time of day now, in milliseconds since midnight.
std::int64_t local_ms_of_day_now()
{
#ifdef _WIN32
  SYSTEMTIME t;
  GetLocalTime(&t);
  return ((static_cast<std::int64_t>(t.wHour) * 60 + t.wMinute) * 60 + t.wSecond) * 1000 +
         t.wMilliseconds;
#else
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  tm local;
  localtime_r(&ts.tv_sec, &local);
  return ((static_cast<std::int64_t>(local.tm_hour) * 60 + local.tm_min) * 60 + local.tm_sec) *
         1000 + ts.tv_nsec / kNsPerMs;
#endif
}

std::int64_t local_ms_of_day_at(std::int64_t at_ns)
{
  const std::int64_t age_ms = (steady_now_ns() - at_ns) / kNsPerMs;
  std::int64_t ms = (local_ms_of_day_now() - age_ms) % kMsPerDay;
  if (ms < 0) {
    ms += kMsPerDay;
  }
  return ms;
}

}  // namespace

#ifdef _WIN32

std::int64_t SystemStallClock::steady_ns() const
{
  return steady_now_ns();
}

std::int64_t SystemStallClock::local_ms_of_day(std::int64_t at_ns) const
{
  return local_ms_of_day_at(at_ns);
}

FileHandleStallSink::FileHandleStallSink(void * handle)
: handle_(handle)
{
}

void FileHandleStallSink::write(const char * text, std::size_t length)
{
  HANDLE const handle = static_cast<HANDLE>(handle_);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
}

namespace
{

// MiniDumpWriteDump of dbghelp.dll, found at run time: the package includes no dbghelp header, so
// the signature is spelled with plain types (the three trailing pointers are passed as null) and
// the dump type with the values of MINIDUMP_TYPE for "normal" (thread stacks, no heap) and "with
// thread info".
using MiniDumpWriteDumpFn = BOOL (WINAPI *)(HANDLE, DWORD, HANDLE, DWORD, void *, void *, void *);
constexpr DWORD kMiniDumpNormal = 0x00000000;
constexpr DWORD kMiniDumpWithThreadInfo = 0x00001000;

/// Copies @p text as UTF-8 into @p out (NUL-ended, cut to @p capacity).
void wide_to_utf8(const std::wstring & text, char * out, std::size_t capacity)
{
  if (capacity == 0) {
    return;
  }
  out[0] = '\0';
  if (text.empty()) {
    return;
  }
  const int n = WideCharToMultiByte(
    CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out,
    static_cast<int>(capacity) - 1, nullptr, nullptr);
  out[n > 0 ? n : 0] = '\0';
}

void put_reason(char * detail, std::size_t capacity, const char * reason)
{
  std::size_t n = 0;
  while (reason[n] != '\0' && n + 1 < capacity) {
    detail[n] = reason[n];
    ++n;
  }
  detail[n] = '\0';
}

}  // namespace

MiniDumpStallWriter::MiniDumpStallWriter(const std::wstring & directory, std::uint64_t min_free)
: directory_(directory), min_free_bytes_(min_free)
{
  HMODULE const module = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  dbghelp_ = module;
  if (module != nullptr) {
    write_fn_ = reinterpret_cast<void *>(GetProcAddress(module, "MiniDumpWriteDump"));
  }
  CreateDirectoryW(directory_.c_str(), nullptr);
}

MiniDumpStallWriter::~MiniDumpStallWriter()
{
  // dbghelp stays loaded: a dump that had not returned when the watchdog was stopped may still
  // be running inside it, on a thread that was left behind.
}

bool MiniDumpStallWriter::write_dump(char * detail, std::size_t capacity)
{
  if (write_fn_ == nullptr) {
    put_reason(detail, capacity, "dbghelp.dll has no MiniDumpWriteDump");
    return false;
  }
  ULARGE_INTEGER free_bytes;
  free_bytes.QuadPart = 0;
  if (GetDiskFreeSpaceExW(directory_.c_str(), &free_bytes, nullptr, nullptr) &&
    free_bytes.QuadPart < min_free_bytes_)
  {
    put_reason(detail, capacity, "the volume is nearly full");
    return false;
  }
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t name[64];
  swprintf(
    name, sizeof(name) / sizeof(name[0]), L"\\rviz_stall_%04d%02d%02d_%02d%02d%02d.dmp",
    t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
  const std::wstring path = directory_ + name;
  HANDLE const file = CreateFileW(
    path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    put_reason(detail, capacity, "the file could not be created");
    return false;
  }
  const BOOL ok = reinterpret_cast<MiniDumpWriteDumpFn>(write_fn_)(
    GetCurrentProcess(), GetCurrentProcessId(), file,
    kMiniDumpNormal | kMiniDumpWithThreadInfo, nullptr, nullptr, nullptr);
  CloseHandle(file);
  if (!ok) {
    put_reason(detail, capacity, "MiniDumpWriteDump failed");
    return false;
  }
  wide_to_utf8(path, detail, capacity);
  return true;
}

#endif  // _WIN32

namespace
{

#ifndef _WIN32
class StderrFdSink : public StallSink
{
public:
  void write(const char * text, std::size_t length) override
  {
    const ssize_t ignored = ::write(2, text, length);
    (void)ignored;
  }
};

class SystemStallClock : public StallClock
{
public:
  std::int64_t steady_ns() const override {return steady_now_ns();}
  std::int64_t local_ms_of_day(std::int64_t at_ns) const override
  {
    return local_ms_of_day_at(at_ns);
  }
};
#endif

/// The objects that the system watchdog reads from. They are made first and die last.
struct SystemStallParts
{
  explicit SystemStallParts(bool with_dump)
  {
#ifdef _WIN32
    if (with_dump) {
      wchar_t temp[MAX_PATH + 1];
      const DWORD n = GetTempPathW(MAX_PATH, temp);
      if (n > 0 && n < MAX_PATH) {
        dumper = std::make_unique<MiniDumpStallWriter>(
          std::wstring(temp) + L"qar_rviz_logs", 2ULL * 1024 * 1024 * 1024);
      }
    }
#else
    (void)with_dump;
#endif
  }

  SystemStallClock clock;
#ifdef _WIN32
  FileHandleStallSink sink{GetStdHandle(STD_ERROR_HANDLE)};
  std::unique_ptr<MiniDumpStallWriter> dumper;
#else
  StderrFdSink sink;
  std::unique_ptr<StallDumpWriter> dumper;
#endif
};

class SystemStallWatchdog : private SystemStallParts, public QtStallWatchdog
{
public:
  explicit SystemStallWatchdog(bool with_dump)
  : SystemStallParts(with_dump),
    QtStallWatchdog(StallWatchdogConfig{}, &clock, &sink, dumper.get())
  {
  }
};

}  // namespace

std::unique_ptr<QtStallWatchdog> make_system_stall_watchdog(bool with_dump)
{
  return std::make_unique<SystemStallWatchdog>(with_dump);
}

}  // namespace qar_rviz_streaming
