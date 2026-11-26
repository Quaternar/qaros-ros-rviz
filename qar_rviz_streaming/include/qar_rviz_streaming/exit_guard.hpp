// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The process exit guard: when RViz is quitting and the process does not end within a fixed time,
// the guard writes one line that names the last step and ends the process itself.

#ifndef QAR_RVIZ_STREAMING__EXIT_GUARD_HPP_
#define QAR_RVIZ_STREAMING__EXIT_GUARD_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include "qar_rviz_streaming/qt_stall_watchdog.hpp"

namespace qar_rviz_streaming
{

/// @brief Ends the process. Called from the guard thread only, once.
class ExitTerminator
{
public:
  virtual ~ExitTerminator() = default;

  /// @brief Ends the process with @p code. The system implementation never returns.
  virtual void terminate(int code) = 0;
};

/// @brief The limits of the guard, in one place so that a test scales them down.
struct ExitGuardConfig
{
  /// The longest time between arm() and the end of the process.
  std::int64_t deadline_ns = 5'000'000'000;
  /// The longest wait of the guard thread between two looks at the clock.
  std::int64_t slice_ns = 250'000'000;
  /// The exit code of the process that the guard ends.
  int exit_code = 3;
};

/// @brief A guard that ends the process when it is armed and the deadline passes.
///
/// ## What it does
///
///  * start() makes one thread. It is made before the first QAROS library is loaded: a thread made
///    later could wait for a loader lock that a stuck library call holds.
///  * arm() starts the deadline. The panel calls it when the application is about to quit; a panel
///    that is removed while RViz keeps running never arms it. A second arm() keeps the earlier
///    deadline.
///  * When the deadline passes while the guard is armed, the thread writes one line to the sink
///    (`[qar-exit] <time> RViz did not end within <n> s after quitting; last step: <step>; the
///    process ends now`) and calls the terminator.
///  * The thread never touches Qt, loads no library, takes no lock but its own and joins nothing.
///    The line is built in a buffer on the stack.
///
/// set_step() mirrors the step names of the Qt-thread watchdog: a pointer store, @p step must be a
/// string literal (or any text that lives as long as the guard).
class ExitGuard
{
public:
  /// @param clock, sink, terminator Must outlive the guard.
  ExitGuard(
    const ExitGuardConfig & config, const StallClock * clock, StallSink * sink,
    ExitTerminator * terminator);
  ~ExitGuard();

  ExitGuard(const ExitGuard &) = delete;
  ExitGuard & operator=(const ExitGuard &) = delete;

  /// @brief Makes the guard thread. A second call does nothing.
  void start();

  /// @brief Starts the deadline at the clock's now; keeps the earlier deadline when armed.
  void arm();

  /// @brief Cancels the deadline.
  void disarm();

  /// @brief The step that follows (see the class text).
  void set_step(const char * step);

  bool armed() const;

  /// @brief True once the line was written and the terminator was called.
  bool fired() const;

  /// @brief Looks at the clock once, without the thread: when armed and the deadline has passed,
  /// writes the line and calls the terminator. @return true when it did.
  bool poll();

private:
  void run();

  const ExitGuardConfig config_;
  const StallClock * clock_;
  StallSink * sink_;
  ExitTerminator * terminator_;
  std::atomic<const char *> step_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool armed_ = false;
  bool fired_ = false;
  bool stop_ = false;
  std::int64_t deadline_ns_ = 0;
  std::thread thread_;
};

/// @brief Makes the process-wide guard with the clock of this machine, the standard error handle
/// as the sink and the system terminator (`TerminateProcess`; `_exit` off Windows), starts its
/// thread, and pins the module that holds this code so that the thread can never outlive it. The
/// guard is leaked on purpose. Only the first call does anything. With @p enabled false nothing
/// is made and the three functions below do nothing.
void start_process_exit_guard(bool enabled);

/// @brief Arms the process-wide guard (the application is about to quit).
void arm_process_exit_guard();

/// @brief The step for the process-wide guard; a no-op when it is not running.
void set_process_exit_step(const char * step);

/// @brief True when the process-wide guard runs.
bool process_exit_guard_running();

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__EXIT_GUARD_HPP_
