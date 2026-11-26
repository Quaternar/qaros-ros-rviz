// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/exit_guard.hpp"

#include <algorithm>
#include <memory>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace qar_rviz_streaming
{

namespace
{

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

  /// A length in seconds: whole when it is whole, else with one decimal (cut).
  void seconds(std::int64_t ns)
  {
    const std::uint64_t tenths = ns > 0 ? static_cast<std::uint64_t>(ns / 100'000'000) : 0;
    unsigned_number(tenths / 10);
    if (tenths % 10 != 0) {
      text(".");
      unsigned_number(tenths % 10);
    }
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

ExitGuard::ExitGuard(
  const ExitGuardConfig & config, const StallClock * clock, StallSink * sink,
  ExitTerminator * terminator)
: config_(config), clock_(clock), sink_(sink), terminator_(terminator), step_("(none)")
{
}

ExitGuard::~ExitGuard()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  if (thread_.joinable()) {
    thread_.join();
  }
}

void ExitGuard::start()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!thread_.joinable()) {
    thread_ = std::thread([this] { run(); });
  }
}

void ExitGuard::arm()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (armed_ || fired_) {
      return;
    }
    armed_ = true;
    deadline_ns_ = clock_->steady_ns() + config_.deadline_ns;
  }
  wake_.notify_all();
}

void ExitGuard::disarm()
{
  std::lock_guard<std::mutex> lock(mutex_);
  armed_ = false;
}

void ExitGuard::set_step(const char * step)
{
  step_.store(step, std::memory_order_relaxed);
}

bool ExitGuard::armed() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return armed_;
}

bool ExitGuard::fired() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return fired_;
}

bool ExitGuard::poll()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!armed_ || fired_ || clock_->steady_ns() < deadline_ns_) {
      return false;
    }
    fired_ = true;
    armed_ = false;
  }
  LineBuilder line;
  line.text("[qar-exit] ");
  line.clock(clock_->local_ms_of_day(clock_->steady_ns()));
  line.text(" RViz did not end within ");
  line.seconds(config_.deadline_ns);
  line.text(" s after quitting; last step: ");
  line.text(step_.load(std::memory_order_relaxed));
  line.text("; the process ends now");
  const std::size_t length = line.finish();
  sink_->write(line.data(), length);
  terminator_->terminate(config_.exit_code);
  return true;
}

void ExitGuard::run()
{
  std::unique_lock<std::mutex> lock(mutex_);
  while (!stop_ && !fired_) {
    if (!armed_) {
      wake_.wait(lock, [this] { return stop_ || armed_; });
      continue;
    }
    const std::int64_t left = deadline_ns_ - clock_->steady_ns();
    if (left <= 0) {
      lock.unlock();
      poll();
      lock.lock();
      continue;
    }
    wake_.wait_for(lock, std::chrono::nanoseconds(std::min(left, config_.slice_ns)));
  }
}

namespace
{

#ifdef _WIN32

class SystemExitTerminator : public ExitTerminator
{
public:
  // ExitProcess takes the loader lock first, which a stuck library call may hold; this does not.
  void terminate(int code) override
  {
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
  }
};

#else

class SystemExitTerminator : public ExitTerminator
{
public:
  void terminate(int code) override { _exit(code); }
};

class StderrExitSink : public StallSink
{
public:
  void write(const char * text, std::size_t length) override
  {
    const ssize_t written = ::write(2, text, length);
    (void)written;
  }
};

#endif

/// What the process-wide guard reads from. Made first, never destroyed.
struct SystemExitParts
{
  SystemStallClock clock;
#ifdef _WIN32
  // The handle is taken before any library is loaded and written with one WriteFile call.
  FileHandleStallSink sink{GetStdHandle(STD_ERROR_HANDLE)};
#else
  StderrExitSink sink;
#endif
  SystemExitTerminator terminator;
  ExitGuard guard{ExitGuardConfig{}, &clock, &sink, &terminator};
};

std::atomic<ExitGuard *> g_process_guard{nullptr};
std::once_flag g_process_guard_once;

}  // namespace

void start_process_exit_guard(bool enabled)
{
  std::call_once(g_process_guard_once, [enabled] {
      if (!enabled) {
        return;
      }
#ifdef _WIN32
      // The thread must be able to return into this code for as long as the process lives, also
      // when RViz unloads the plugin. The module is pinned now, while nothing holds the loader
      // lock; a call that changes a module's reference count needs it.
      HMODULE module = nullptr;
      GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&start_process_exit_guard), &module);
#endif
      // Never freed: the thread outlives main.
      SystemExitParts * const parts = new SystemExitParts();
      parts->guard.start();
      g_process_guard.store(&parts->guard, std::memory_order_release);
    });
}

void arm_process_exit_guard()
{
  if (ExitGuard * const guard = g_process_guard.load(std::memory_order_acquire)) {
    guard->arm();
  }
}

void set_process_exit_step(const char * step)
{
  if (ExitGuard * const guard = g_process_guard.load(std::memory_order_acquire)) {
    guard->set_step(step);
  }
}

bool process_exit_guard_running()
{
  return g_process_guard.load(std::memory_order_acquire) != nullptr;
}

}  // namespace qar_rviz_streaming
