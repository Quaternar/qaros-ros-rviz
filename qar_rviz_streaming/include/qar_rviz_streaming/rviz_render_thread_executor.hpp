// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__RVIZ_RENDER_THREAD_EXECUTOR_HPP_
#define QAR_RVIZ_STREAMING__RVIZ_RENDER_THREAD_EXECUTOR_HPP_

#include <functional>

#include "qar_ogre_streaming/render_thread_executor.hpp"

class QThread;

namespace qar_rviz_streaming
{

/// @brief RViz/Qt-backed implementation of qar_ogre_streaming::IRenderThreadExecutor.
///
/// In RViz2 all Ogre/OpenGL work must happen on the Qt main (GUI) thread, which
/// is also the thread that owns the OpenGL producer context and drives RViz's
/// render loop. This executor captures that
/// thread at construction (it is constructed by the panel on the GUI thread) and:
///
///  * post_to_render_thread(): marshals @p work onto the GUI thread via
///    QMetaObject::invokeMethod(qApp, ..., Qt::QueuedConnection), so a Qar
///    networking/callback thread can only *enqueue* render work, never render.
///  * is_render_thread(): true iff the caller is currently on that GUI thread.
///
/// The reusable library never sees Qt — it only sees the IRenderThreadExecutor
/// interface, so this class is the RViz-specific bridge.
class RVizRenderThreadExecutor : public qar_ogre_streaming::IRenderThreadExecutor
{
public:
  /// @brief Capture the Qt application (GUI/render) thread. Must be constructed
  /// on that thread (the panel does so in onInitialize()).
  RVizRenderThreadExecutor();
  ~RVizRenderThreadExecutor() override = default;

  /// @brief Queue @p work to run on the Qt GUI/render thread during the event
  /// loop. If no QCoreApplication exists yet, runs inline only when already on
  /// the captured thread (otherwise drops — there is no loop to queue onto).
  void post_to_render_thread(std::function<void()> work) override;

  /// @brief True iff the calling thread is the captured Qt GUI/render thread.
  bool is_render_thread() const override;

  /// @brief The captured render thread (for diagnostics/tests).
  QThread * render_thread() const { return render_thread_; }

private:
  QThread * render_thread_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__RVIZ_RENDER_THREAD_EXECUTOR_HPP_
