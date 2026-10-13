// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_EXPOSURE_REGISTRY_HPP_
#define QAR_RVIZ_STREAMING__AR_EXPOSURE_REGISTRY_HPP_

/// @file ar_exposure_registry.hpp
/// @brief What is currently exposed to AR, and what state each exposure is in.
///
/// The operator's list: one row per topic / service / action marked "expose as
/// a QAR AR tool", carrying the panel id, the served page's token and URL, and
/// whether the QarOS panel is open yet. Pure C++17 so the identity and
/// duplicate rules — the ones that decide whether "Add" is idempotent, whether
/// a topic and a service of the same name collide, and what "re-open" reopens —
/// are unit-tested rather than only clicked.
///
/// It holds no session and no adapter: a session lives in `qar_web_panels` and
/// an AR panel in `qar_qar_adapter`, and both are looked up by token/panel id
/// from the panel that owns them.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "qar_rviz_streaming/ar_ros_entity.hpp"

namespace qar_rviz_streaming
{

/// @brief One exposed entity.
struct ExposedEntity
{
  RosEntityRef entity;
  bool writable = false;       ///< Topic: the panel offers a publish form.
  std::string panel_id;        ///< UiModel root id; see make_panel_id.
  std::string template_name;   ///< Which IPanelTemplate laid it out.

  // --- Filled in as the exposure comes up ---------------------------------
  std::string token;           ///< WebPanelServer session token; empty before add_panel.
  std::string page_url;        ///< What the QarOS URL panel loads.
  bool ar_panel_open = false;  ///< A QarOS GUI panel exists for it.
  bool subscribed = false;     ///< Topic: a TopicSubscription is live.
  std::string last_error;

  /// How often the panel is refreshed from ROS. The wire is not the limit —
  /// a human reading a number in mid-air is; see "AR tools pipeline" in
  /// docs/ARCHITECTURE.md.
  double refresh_hz = 5.0;

  /// "topic /scan (sensor_msgs/msg/LaserScan)" — the list row.
  std::string describe() const;
};

/// @brief The exposures, keyed by (kind, name).
class ExposureRegistry
{
public:
  /// Fills @c panel_id when empty. Refuses an entity with no name, a duplicate
  /// (kind, name), and a panel id already taken — a collision there would make
  /// two panels answer to one wire id.
  bool add(ExposedEntity entry, std::string * out_error = nullptr);

  bool remove(RosEntityKind kind, std::string_view name);
  bool remove_by_panel_id(std::string_view panel_id);

  bool contains(RosEntityKind kind, std::string_view name) const;
  const ExposedEntity * find(RosEntityKind kind, std::string_view name) const;
  ExposedEntity * find(RosEntityKind kind, std::string_view name);
  const ExposedEntity * find_by_panel_id(std::string_view panel_id) const;
  ExposedEntity * find_by_panel_id(std::string_view panel_id);
  const ExposedEntity * find_by_token(std::string_view token) const;
  ExposedEntity * find_by_token(std::string_view token);

  const std::vector<ExposedEntity> & entries() const { return entries_; }
  std::size_t size() const { return entries_.size(); }
  bool empty() const { return entries_.empty(); }
  void clear() { entries_.clear(); }

private:
  /// Insertion order, which is the order the operator added them: a list that
  /// reshuffles under the cursor is unusable (the same rule the topic tree obeys).
  std::vector<ExposedEntity> entries_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_EXPOSURE_REGISTRY_HPP_
