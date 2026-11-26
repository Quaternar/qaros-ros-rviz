// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__RENDER_TICK_TIMER_HPP_
#define QAR_RVIZ_STREAMING__RENDER_TICK_TIMER_HPP_

/// @file render_tick_timer.hpp
/// @brief Puts a TickTimerPlan on the QTimer of the render tick (QtCore only).

#include "qar_rviz_streaming/render_tick_pacing.hpp"

class QTimer;

namespace qar_rviz_streaming
{

/// @brief Sets @p timer to @p plan; the timer is always a Qt::PreciseTimer, because the coarse
/// timer of Windows is served off the 15.6 ms system tick.
///
/// @param restart true: stop the timer, set it and start it (the first setting, and every
/// setting that must leave the timer running). false: change the interval of a timer that is
/// active, which Qt restarts with the interval of the plan, and leave a timer that is stopped
/// alone, so that a scripted run that stopped the tick keeps it stopped.
void apply_tick_plan(QTimer & timer, const TickTimerPlan & plan, bool restart);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__RENDER_TICK_TIMER_HPP_
