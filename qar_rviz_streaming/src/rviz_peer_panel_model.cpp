// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/rviz_peer_panel_model.hpp"

#include <algorithm>
#include <cstdio>

namespace qar_rviz_streaming
{

const RvizPeerEntry & RvizPeerPanelModel::addMockPeer(
  std::uint32_t width, std::uint32_t height, std::uint32_t fps)
{
  const std::uint32_t index = next_index_++;

  RvizPeerEntry entry;
  // Deterministic, non-zero id: encode the index in the low bytes and mark the
  // rest so peer_id_to_hex() renders something readable and unique per peer.
  entry.id = QarPeerId{};
  entry.id.data[0] = static_cast<std::uint8_t>((index >> 0) & 0xFF);
  entry.id.data[1] = static_cast<std::uint8_t>((index >> 8) & 0xFF);
  entry.id.data[2] = static_cast<std::uint8_t>((index >> 16) & 0xFF);
  entry.id.data[3] = static_cast<std::uint8_t>((index >> 24) & 0xFF);
  entry.id.data[15] = 0xEE;  // marker byte for a simulated peer

  char buf[32];
  std::snprintf(buf, sizeof(buf), "mock-peer-%02u", index);
  entry.label = buf;

  entry.width = width;
  entry.height = height;
  entry.fps = fps;
  entry.streaming = false;

  peers_.push_back(entry);
  return peers_.back();
}

bool RvizPeerPanelModel::removePeer(const QarPeerId & id)
{
  auto it = std::find_if(
    peers_.begin(), peers_.end(),
    [&id](const RvizPeerEntry & e) { return qar_ogre_streaming::peer_id_equal(e.id, id); });
  if (it == peers_.end()) {
    return false;
  }
  peers_.erase(it);
  return true;
}

RvizPeerEntry * RvizPeerPanelModel::find(const QarPeerId & id)
{
  auto it = std::find_if(
    peers_.begin(), peers_.end(),
    [&id](const RvizPeerEntry & e) { return qar_ogre_streaming::peer_id_equal(e.id, id); });
  return it == peers_.end() ? nullptr : &(*it);
}

const RvizPeerEntry * RvizPeerPanelModel::find(const QarPeerId & id) const
{
  auto it = std::find_if(
    peers_.begin(), peers_.end(),
    [&id](const RvizPeerEntry & e) { return qar_ogre_streaming::peer_id_equal(e.id, id); });
  return it == peers_.end() ? nullptr : &(*it);
}

}  // namespace qar_rviz_streaming
