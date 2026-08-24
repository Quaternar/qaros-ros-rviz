// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__RVIZ_PEER_PANEL_MODEL_HPP_
#define QAR_RVIZ_STREAMING__RVIZ_PEER_PANEL_MODEL_HPP_

#include <cstdint>
#include <string>
#include <vector>

#include "qar_ogre_streaming/types.hpp"  // brings the real QarPeerId + peer-id helpers

namespace qar_rviz_streaming
{

/// @brief One mock peer the panel tracks (minimum UI).
///
/// Per contractor answer C6 there is NO authorization state here — the peer id
/// exists only to route/identify a stream, never to approve/deny access.
struct RvizPeerEntry
{
  QarPeerId id{};
  std::string label;
  bool streaming = false;
  std::uint32_t width = 1280;
  std::uint32_t height = 720;
  std::uint32_t fps = 60;
};

/// @brief Pure (Qt-free) model of the panel's mock peer list.
///
/// Kept deliberately independent of Qt widgets so the add/remove logic is
/// unit-testable and the panel just mirrors it into a QListWidget. It generates
/// deterministic, non-zero peer ids so the same peer can be looked up in the
/// reusable library's stream map.
class RvizPeerPanelModel
{
public:
  /// @brief Add a new mock peer with a generated id/label and the given render
  /// settings. Returns a reference valid until the next mutating call.
  const RvizPeerEntry & addMockPeer(std::uint32_t width, std::uint32_t height, std::uint32_t fps);

  /// @brief Remove the peer with @p id. @return true if it existed.
  bool removePeer(const QarPeerId & id);

  /// @brief Non-owning lookup (nullptr if absent).
  RvizPeerEntry * find(const QarPeerId & id);
  const RvizPeerEntry * find(const QarPeerId & id) const;

  const std::vector<RvizPeerEntry> & peers() const { return peers_; }
  std::size_t size() const { return peers_.size(); }
  bool empty() const { return peers_.empty(); }

private:
  std::vector<RvizPeerEntry> peers_;
  std::uint32_t next_index_ = 1;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__RVIZ_PEER_PANEL_MODEL_HPP_
