// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/windows_scheduling.hpp"

#include <string>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>  // timeBeginPeriod / timeEndPeriod (winmm)

// Older SDKs lack some of these (IGNORE_TIMER_RESOLUTION arrived with Windows 11's
// SDK); the values are the documented ABI, so supply them rather than require a
// newer SDK to build.
#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#endif
#ifndef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif
#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

namespace qar_rviz_streaming
{

namespace
{

/// PROCESS_POWER_THROTTLING_STATE, declared here so an SDK without it still builds.
struct PowerThrottlingState
{
  ULONG Version;
  ULONG ControlMask;
  ULONG StateMask;
};

/// PROCESS_INFORMATION_CLASS::ProcessPowerThrottling.
constexpr int kProcessPowerThrottling = 4;

using SetProcessInformationFn = BOOL (WINAPI *)(HANDLE, int, LPVOID, DWORD);

}  // namespace

const SchedulingOptOutResult &
WindowsSchedulingGuard::apply()
{
  if (applied_) {
    return result_;
  }
  applied_ = true;
  result_.windows = true;

  const HMODULE kernel32 = ::GetModuleHandleW(L"kernel32.dll");
  const auto set_process_information = kernel32 != nullptr
    ? reinterpret_cast<SetProcessInformationFn>(
      ::GetProcAddress(kernel32, "SetProcessInformation"))
    : nullptr;
  if (set_process_information != nullptr) {
    result_.process_information_api_found = true;
    PowerThrottlingState state{};
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    // ControlMask names the bits we are taking over; StateMask = 0 turns every one
    // of them OFF, i.e. "do not throttle execution speed, do not ignore this
    // process's timer-resolution requests".
    state.ControlMask =
      PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;
    if (set_process_information(
        ::GetCurrentProcess(), kProcessPowerThrottling, &state, sizeof(state)))
    {
      result_.power_throttling_disabled = true;
    } else {
      result_.power_throttling_error = static_cast<std::uint32_t>(::GetLastError());
    }
  }

  result_.timer_period_raised = ::timeBeginPeriod(1) == TIMERR_NOERROR;
  return result_;
}

void
WindowsSchedulingGuard::release()
{
  if (result_.timer_period_raised) {
    ::timeEndPeriod(1);
    result_.timer_period_raised = false;
  }
}

ForegroundState
foreground_state(std::uintptr_t window_handle)
{
  if (window_handle == 0) {
    return ForegroundState::kUnknown;
  }
  const HWND foreground = ::GetForegroundWindow();
  if (foreground == nullptr) {
    return ForegroundState::kBackground;  // e.g. the desktop is switching; not us
  }
  const HWND mine = reinterpret_cast<HWND>(window_handle);
  if (foreground == mine ||
    ::GetAncestor(foreground, GA_ROOTOWNER) == ::GetAncestor(mine, GA_ROOTOWNER))
  {
    return ForegroundState::kForeground;
  }
  DWORD foreground_process = 0;
  ::GetWindowThreadProcessId(foreground, &foreground_process);
  return foreground_process == ::GetCurrentProcessId() ? ForegroundState::kForeground
                                                       : ForegroundState::kBackground;
}

}  // namespace qar_rviz_streaming

#else  // !_WIN32

namespace qar_rviz_streaming
{

const SchedulingOptOutResult &
WindowsSchedulingGuard::apply()
{
  applied_ = true;  // windows stays false: nothing to do
  return result_;
}

void
WindowsSchedulingGuard::release()
{
}

ForegroundState
foreground_state(std::uintptr_t)
{
  return ForegroundState::kUnknown;
}

}  // namespace qar_rviz_streaming

#endif  // _WIN32

namespace qar_rviz_streaming
{

std::string
SchedulingOptOutResult::summary() const
{
  if (!windows) {
    return "SCHED: not Windows - no scheduling opt-out needed.";
  }
  std::string line = "SCHED: ";
  if (power_throttling_disabled) {
    line += "process power throttling OFF (full speed, timer resolution always honoured "
      "even when occluded)";
  } else if (!process_information_api_found) {
    line += "SetProcessInformation not available (older Windows) - power-throttling "
      "opt-out skipped";
  } else {
    line += "SetProcessInformation(ProcessPowerThrottling) FAILED, error " +
      std::to_string(power_throttling_error);
  }
  line += timer_period_raised ? "; timeBeginPeriod(1) OK." : "; timeBeginPeriod(1) FAILED.";
  return line;
}

}  // namespace qar_rviz_streaming
