// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/rviz_render_thread_executor.hpp"

#include <utility>

#include <QCoreApplication>
#include <QThread>

namespace qar_rviz_streaming
{

RVizRenderThreadExecutor::RVizRenderThreadExecutor()
: render_thread_(
    QCoreApplication::instance() ? QCoreApplication::instance()->thread()
                                 : QThread::currentThread())
{
}

void RVizRenderThreadExecutor::post_to_render_thread(std::function<void()> work)
{
  if (!work) {
    return;
  }

  QCoreApplication * app = QCoreApplication::instance();
  if (app != nullptr) {
    // Queue onto the GUI/render thread; executed when its event loop next runs.
    // qApp lives in the GUI thread, so it is a valid context object even when
    // this call originates from a Qar callback/worker thread.
    QMetaObject::invokeMethod(app, std::move(work), Qt::QueuedConnection);
    return;
  }

  // No application/event loop (e.g. very early startup or a unit test): only run
  // inline if we are already on the captured thread; otherwise there is nothing
  // to queue onto and we must not touch Ogre off-thread.
  if (QThread::currentThread() == render_thread_) {
    work();
  }
}

bool RVizRenderThreadExecutor::is_render_thread() const
{
  return QThread::currentThread() == render_thread_;
}

}  // namespace qar_rviz_streaming
