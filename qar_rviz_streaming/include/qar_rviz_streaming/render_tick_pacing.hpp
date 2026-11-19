// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__RENDER_TICK_PACING_HPP_
#define QAR_RVIZ_STREAMING__RENDER_TICK_PACING_HPP_

/// @file render_tick_pacing.hpp
/// @brief How often the render tick of the panel runs: the decisions, without Qt.
///
/// The render tick is the one periodic timer of the panel that is not a single shot. While a
/// stream exists it runs at the configured period (0.1 ms unless QAR_PANEL_TICK_MS says
/// otherwise): a period below 1 ms is a zero-interval timer, which fires whenever the event loop
/// has nothing else to do, plus a throttle that admits one body per period. While nothing is
/// streamed the same tick runs on an ordinary timer of 50 ms, so an idle RViz does not keep the
/// event loop of its GUI thread busy.

#include <chrono>
#include <cstddef>

namespace qar_rviz_streaming
{

/// @brief The two speeds of the render tick.
enum class TickTier
{
  kIdle,       ///< Nothing is streamed: a slow ordinary timer.
  kStreaming,  ///< A stream exists: the configured period.
};

/// @brief The period of the idle tier, in ms.
inline constexpr double kIdleTickMs = 50.0;

/// @brief What the timer of the render tick is set to.
struct TickTimerPlan
{
  /// The argument of QTimer::setInterval (0 for a zero-interval timer).
  int interval_ms = 0;
  /// True when the timer fires whenever the event loop is idle and onRenderTick throttles.
  bool zero_interval = false;
  /// The throttle of the zero-interval timer in microseconds (0 when there is none).
  double throttle_us = 0.0;
  /// The nominal period of the tier in ms: what the stall watchdog and the gap threshold use.
  double period_ms = 0.0;
};

/// @brief The timer setting of a tier.
///
/// kStreaming: a period below 1 ms is a zero-interval timer with a throttle of the period in
/// microseconds; any other period is an ordinary interval rounded to whole ms.
/// kIdle: the larger of @p idle_ms and @p configured_ms, never below 1 ms, never zero-interval,
/// so a configured tick that is slower than the idle period is not made faster by being idle.
TickTimerPlan tick_timer_plan(TickTier tier, double configured_ms, double idle_ms = kIdleTickMs);

/// @brief The facts that decide the tier.
struct TickDemand
{
  /// The panel talks to QAROS (QAR_PANEL_LIVE_STREAM) and not to the offline preview.
  bool live_mode = false;
  /// Live mode: the session to the Hub is up.
  bool live_connected = false;
  /// Preview mode: the manager streams.
  bool preview_streaming = false;
  /// A scripted run (QAR_PANEL_AUTODUMP, AUTOCAPTURE, AUTOSTEREO, AUTOMULTI) drives the panel.
  bool scripted_driver = false;
  /// 3D streams of the Ogre manager.
  std::size_t streams_3d = 0;
  /// Qt panel streams of the AR panel manager that are not parked.
  std::size_t ar_panel_streams = 0;
};

/// @brief The tier for these facts: streaming when a scripted run drives the panel, or in live
/// mode when connected with a 3D stream or an AR panel stream, or in preview mode when the
/// manager streams or an AR panel stream exists; idle otherwise.
TickTier tick_tier_for(const TickDemand & demand);

/// @brief The admit rule of the zero-interval timer: at most one tick body per throttle period.
///
/// A call that is refused costs one clock comparison. A plan without a zero-interval timer
/// admits everything, because Qt paces such a timer itself.
class TickThrottle
{
public:
  using Clock = std::chrono::steady_clock;

  /// Takes the rule of @p plan and forgets the last admitted tick, so that the next one runs.
  void configure(const TickTimerPlan & plan);

  /// Forgets the last admitted tick.
  void reset() {has_last_ = false;}

  /// True when the tick body may run at the given time; the time is then the last admitted one.
  bool admit(Clock::time_point now);

  bool active() const {return active_;}
  double interval_us() const {return interval_us_;}

private:
  bool active_ = false;
  double interval_us_ = 0.0;
  bool has_last_ = false;
  Clock::time_point last_{};
};

/// @brief The inter-tick gap in ms from which a gap is written to the timing log: 25 ms, or 2.5
/// periods when the tick is slower than 10 ms, so that a tick of the idle tier is not a gap.
double gui_gap_record_ms(double timer_period_ms);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__RENDER_TICK_PACING_HPP_
