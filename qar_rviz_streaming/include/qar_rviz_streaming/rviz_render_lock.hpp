// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__RVIZ_RENDER_LOCK_HPP_
#define QAR_RVIZ_STREAMING__RVIZ_RENDER_LOCK_HPP_

#include "qar_ogre_streaming/render_lock.hpp"

#include "qar_rviz_streaming/rviz_display_context_adapter.hpp"

namespace qar_rviz_streaming
{

/// @brief Implements the reusable library's IRenderLock (the batch lock) by
/// forwarding to the RViz DisplayContext render lock via
/// RVizDisplayContextAdapter.
///
/// The multi-peer scheduler wraps the WHOLE batch in a single
/// lock_render()/unlock_render() pair so every peer samples one coherent RViz
/// scene snapshot. This is the single RViz-specific seam that supplies that
/// lock; the library itself never includes an rviz header.
///
/// Non-owning: the wrapped adapter is owned by the panel and must outlive this
/// object (the panel destroys the manager/scheduler before the adapter).
class RVizRenderLock : public qar_ogre_streaming::IRenderLock
{
public:
  explicit RVizRenderLock(RVizDisplayContextAdapter * adapter) : adapter_(adapter) {}

  void lock_render() override
  {
    if (adapter_ != nullptr) {
      adapter_->lockRender();
    }
  }

  void unlock_render() override
  {
    if (adapter_ != nullptr) {
      adapter_->unlockRender();
    }
  }

private:
  RVizDisplayContextAdapter * adapter_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__RVIZ_RENDER_LOCK_HPP_
