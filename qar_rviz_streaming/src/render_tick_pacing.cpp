// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/render_tick_pacing.hpp"

#include <algorithm>

namespace qar_rviz_streaming
{

TickTimerPlan tick_timer_plan(TickTier tier, double configured_ms, double idle_ms)
{
  TickTimerPlan plan;
  if (tier == TickTier::kStreaming) {
    // QTimer::setInterval takes whole ms, so a period below 1 ms runs as a zero-interval timer
    // that the throttle holds to the period.
    plan.zero_interval = configured_ms < 1.0;
    plan.throttle_us = configured_ms * 1000.0;
    plan.interval_ms = plan.zero_interval ? 0 : static_cast<int>(configured_ms + 0.5);
    plan.period_ms = configured_ms;
    return plan;
  }
  const double period_ms = std::max({idle_ms, configured_ms, 1.0});
  plan.zero_interval = false;
  plan.throttle_us = 0.0;
  plan.interval_ms = static_cast<int>(period_ms + 0.5);
  plan.period_ms = period_ms;
  return plan;
}

TickTier tick_tier_for(const TickDemand & demand)
{
  if (demand.scripted_driver) {
    return TickTier::kStreaming;
  }
  const bool streams = demand.live_mode ?
    (demand.live_connected && (demand.streams_3d > 0 || demand.ar_panel_streams > 0)) :
    (demand.preview_streaming || demand.ar_panel_streams > 0);
  return streams ? TickTier::kStreaming : TickTier::kIdle;
}

void TickThrottle::configure(const TickTimerPlan & plan)
{
  active_ = plan.zero_interval;
  interval_us_ = plan.throttle_us;
  has_last_ = false;
}

bool TickThrottle::admit(Clock::time_point now)
{
  if (!active_) {
    return true;
  }
  if (has_last_ &&
    std::chrono::duration<double, std::micro>(now - last_).count() < interval_us_)
  {
    return false;
  }
  last_ = now;
  has_last_ = true;
  return true;
}

double gui_gap_record_ms(double timer_period_ms)
{
  return std::max(25.0, 2.5 * timer_period_ms);
}

}  // namespace qar_rviz_streaming
