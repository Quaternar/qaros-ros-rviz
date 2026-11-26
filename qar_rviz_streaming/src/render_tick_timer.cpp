// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/render_tick_timer.hpp"

#include <QTimer>

namespace qar_rviz_streaming
{

void apply_tick_plan(QTimer & timer, const TickTimerPlan & plan, bool restart)
{
  if (restart) {
    // Never leave the timer running with a stale interval while it is reconfigured: a queued
    // timeout of the old period could otherwise still land after the start.
    timer.stop();
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(plan.interval_ms);
    timer.start();
    return;
  }
  if (!timer.isActive()) {
    return;
  }
  if (timer.timerType() != Qt::PreciseTimer) {
    timer.setTimerType(Qt::PreciseTimer);
  }
  if (timer.interval() != plan.interval_ms) {
    timer.setInterval(plan.interval_ms);
  }
}

}  // namespace qar_rviz_streaming
