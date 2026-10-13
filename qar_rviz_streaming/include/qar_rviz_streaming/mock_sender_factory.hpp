// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__MOCK_SENDER_FACTORY_HPP_
#define QAR_RVIZ_STREAMING__MOCK_SENDER_FACTORY_HPP_

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "qar_ogre_streaming/mock_qar_frame_info_source.hpp"
#include "qar_ogre_streaming/qar_render_sender_adapter.hpp"
#include "qar_ogre_streaming/types.hpp"

namespace qar_rviz_streaming
{

/// @brief Simulated Qar sender backed by the deterministic animated frame source.
///
/// The panel's offline preview wires the reusable library with this stand-in so
/// the full stereo pipeline (per-eye pose/FOV -> render both eyes -> submit
/// separated-texture frame) runs without a Qar runtime. begin_frame() returns
/// the source's per-eye pose/FOV for an advancing frame index; submit is a no-op
/// (live mode submits through the QarOS C API instead).
class AnimatedMockSender : public qar_ogre_streaming::IQarRenderSenderAdapter
{
public:
  explicit AnimatedMockSender(
    std::shared_ptr<const qar_ogre_streaming::MockQarFrameInfoSource> source)
  : source_(std::move(source))
  {
  }

  bool begin_frame(qar_ogre_streaming::QarFrameInfo & out_info) override
  {
    if (source_) {
      out_info = source_->frame_at(frame_index_++);
    } else {
      out_info = qar_ogre_streaming::QarFrameInfo{};
      out_info.should_render = false;
    }
    return out_info.should_render;
  }

  bool submit_opengl_stereo_frame(
    const qar_ogre_streaming::QarOpenGlStereoFrame & /*frame*/) override
  {
    return true;
  }

private:
  std::shared_ptr<const qar_ogre_streaming::MockQarFrameInfoSource> source_;
  std::uint64_t frame_index_ = 0;
};

/// @brief Factory handing out AnimatedMockSenders, one deterministic frame source
/// per peer.
///
/// With several peers, each must get a VISIBLY INDEPENDENT view of the same
/// RViz scene. The factory derives a per-peer @ref MockQarFrameInfoSource::Config
/// from a shared base config by offsetting the orbit phase (and a little pitch) by
/// the peer's creation index, so N peers frame the scene from N distinct
/// viewpoints. Peer index 0 gets ZERO offset, so a single-peer run uses the base
/// config unchanged.
class AnimatedMockSenderFactory : public qar_ogre_streaming::IQarRenderSenderFactory
{
public:
  struct PeerSource
  {
    QarPeerId peer_id{};
    std::shared_ptr<const qar_ogre_streaming::MockQarFrameInfoSource> source;
  };

  AnimatedMockSenderFactory() = default;

  /// @brief Set the base config (from the panel's render settings + IPD). Applied
  /// to every subsequently created peer, with a per-peer phase offset.
  void set_base_config(const qar_ogre_streaming::MockQarFrameInfoSource::Config & base)
  {
    base_ = base;
  }

  std::shared_ptr<qar_ogre_streaming::IQarRenderSenderAdapter> create_sender(
    const QarPeerId & peer_id) override
  {
    const std::uint32_t index = static_cast<std::uint32_t>(sources_.size());
    qar_ogre_streaming::MockQarFrameInfoSource::Config cfg = base_;
    apply_peer_offset(cfg, index);
    auto source = std::make_shared<qar_ogre_streaming::MockQarFrameInfoSource>(cfg);
    sources_.push_back(PeerSource{peer_id, source});
    return std::make_shared<AnimatedMockSender>(source);
  }

  /// @brief Every per-peer source created so far (creation order).
  const std::vector<PeerSource> & sources() const { return sources_; }

  /// @brief The source created for @p peer_id, or nullptr if none.
  std::shared_ptr<const qar_ogre_streaming::MockQarFrameInfoSource> source_for(
    const QarPeerId & peer_id) const
  {
    for (const PeerSource & ps : sources_) {
      if (qar_ogre_streaming::peer_id_equal(ps.peer_id, peer_id)) {
        return ps.source;
      }
    }
    return nullptr;
  }

private:
  /// @brief Spread up to 8 peers around the orbit so their views clearly differ.
  static void apply_peer_offset(
    qar_ogre_streaming::MockQarFrameInfoSource::Config & cfg, std::uint32_t index)
  {
    if (index == 0) {
      return;  // peer 0 == baseline
    }
    constexpr float kTwoPi = 6.2831853071795864769f;
    cfg.start_yaw_rad += static_cast<float>(index) * (kTwoPi / 8.0f);
    // Small per-peer elevation variety (kept within a comfortable range).
    const float pitch_delta = 0.12f * static_cast<float>((index % 3)) - 0.12f;
    cfg.pitch_rad += pitch_delta;
  }

  qar_ogre_streaming::MockQarFrameInfoSource::Config base_{};
  std::vector<PeerSource> sources_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__MOCK_SENDER_FACTORY_HPP_
