// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__WINDOWS_SCHEDULING_HPP_
#define QAR_RVIZ_STREAMING__WINDOWS_SCHEDULING_HPP_

/// @file windows_scheduling.hpp
/// @brief Keep Windows from throttling RViz while another window covers it.
///
/// ## The problem this exists for
///
/// While the full-screen QarOS Visualizer covers RViz, Windows 11 stops
/// honouring a window-owning process's high timer-resolution request
/// (`timeBeginPeriod`), and may additionally power-throttle it (EcoQoS). Every
/// timer-driven cadence in RViz then snaps back to the ~15.6 ms scheduler tick:
/// the AR video panels went from a steady 30 fps to 21 fps (one capture per
/// 3 x 15.625 ms) and worse. The zero-interval render tick is unaffected; the
/// timers are not.
///
/// ## What is done about it
///
///  * `SetProcessInformation(ProcessPowerThrottling)` with ControlMask =
///    EXECUTION_SPEED | IGNORE_TIMER_RESOLUTION and StateMask = 0: "never
///    throttle this process's speed, and always honour its timer-resolution
///    requests, occluded or not". Resolved from kernel32 at run time, so a
///    Windows that lacks it (before 10 1709 / 11) just skips it.
///  * `timeBeginPeriod(1)`, paired with `timeEndPeriod(1)` when the guard
///    releases.
///
/// Everything here is a no-op on other platforms.

#include <cstdint>
#include <string>

namespace qar_rviz_streaming
{

/// @brief What @ref WindowsSchedulingGuard::apply managed to do.
struct SchedulingOptOutResult
{
  /// False off Windows; every other field is then meaningless.
  bool windows = false;
  /// `SetProcessInformation` was found in kernel32.
  bool process_information_api_found = false;
  /// The power-throttling opt-out was accepted by the OS.
  bool power_throttling_disabled = false;
  /// `GetLastError()` of the failed call, 0 when it did not fail.
  std::uint32_t power_throttling_error = 0;
  /// `timeBeginPeriod(1)` succeeded, so a matching `timeEndPeriod(1)` is owed.
  bool timer_period_raised = false;

  /// One line for the panel log, always starting "SCHED:".
  std::string summary() const;
};

/// @brief Applies the opt-out once and undoes the timer-period request on
/// release (destruction at the latest). One guard per panel: Windows counts
/// `timeBeginPeriod` calls, so the pairing must be exact.
class WindowsSchedulingGuard
{
public:
  WindowsSchedulingGuard() = default;
  ~WindowsSchedulingGuard() { release(); }

  WindowsSchedulingGuard(const WindowsSchedulingGuard &) = delete;
  WindowsSchedulingGuard & operator=(const WindowsSchedulingGuard &) = delete;

  /// @brief Apply the opt-out. Idempotent: a second call returns the first
  /// call's result without touching the OS again.
  const SchedulingOptOutResult & apply();

  /// @brief Undo what @ref apply raised (`timeEndPeriod(1)`). Idempotent.
  void release();

  const SchedulingOptOutResult & result() const { return result_; }
  bool applied() const { return applied_; }

private:
  SchedulingOptOutResult result_;
  bool applied_ = false;
};

/// @brief Whether a window belongs to the application in the foreground.
enum class ForegroundState
{
  kUnknown = -1,    ///< Not Windows, or no window handle to compare.
  kBackground = 0,  ///< Something else is the foreground window.
  kForeground = 1   ///< The foreground window is @p window_handle, one of its
                    ///< owner chain, or any window of this process.
};

/// @brief Is the window @p window_handle (an `HWND`, e.g. from
/// `QWidget::window()->windowHandle()->winId()`; 0 = none) in the foreground?
///
/// Same-process counts: a floating dock or a dialog of RViz being the foreground
/// window still means RViz is not occluded by anything.
ForegroundState foreground_state(std::uintptr_t window_handle);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__WINDOWS_SCHEDULING_HPP_
