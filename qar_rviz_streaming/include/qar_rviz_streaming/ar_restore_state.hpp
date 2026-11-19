// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_RESTORE_STATE_HPP_
#define QAR_RVIZ_STREAMING__AR_RESTORE_STATE_HPP_

/// @file ar_restore_state.hpp
/// @brief What the panel keeps in the RViz configuration about the AR side of its work, and what
/// it restores from it: which AR panels are on, the size choices, the AR tools and the watched
/// topics. Standard library and the plugin's pure headers only.
///
/// ## The record
///
/// The configuration of the panel has a map "AR State":
///
/// @code
/// AR State:
///   Version: 1
///   Panels:
///     - Key: dock:Views
///       On: true
///       Size: native
///   Tools:
///     - Kind: topic
///       Name: /cmd_vel
///       Type: geometry_msgs/msg/Twist
///       Writable: true
///       Shown: true
///   Watched Topics:
///     - Name: /scan
///       Type: sensor_msgs/msg/LaserScan
/// @endcode
///
/// A record that cannot be read is skipped with one warning and the rest is used. Nothing of the
/// pick (its mode, its confirmation, a waiting pick) and nothing of the selected tab is part of
/// the record: a field of that name is not known and is ignored.
///
/// ## The tree
///
/// The configuration tree of RViz is not linked into the tests. @ref ConfigNode is a plain copy
/// of the part of it that the record needs; the plugin converts between the two and this unit
/// reads and writes the copy.

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "qar_rviz_streaming/ar_copy_scale.hpp"
#include "qar_rviz_streaming/ar_ros_entity.hpp"

namespace qar_rviz_streaming
{

/// The version of the record that this plugin writes.
constexpr int kArStateVersion = 1;
/// The key of the record in the configuration of the panel.
constexpr const char * kArStateKey = "AR State";
/// The key of the copy of RViz's Displays panel (the key the stream manager uses for it).
constexpr const char * kArStateDisplaysKey = "hidden:Displays";
/// The most records one list is read from; further records are ignored with one warning.
constexpr std::size_t kArStateMaxRecords = 256;

/// One element of a list, as text.
using ConfigRecord = std::map<std::string, std::string>;

/// The record as text, the way it stands in the configuration tree.
struct ArStateText
{
  std::string version;                ///< empty when the record has no Version
  std::vector<ConfigRecord> panels;   ///< "Panels"
  std::vector<ConfigRecord> tools;    ///< "Tools"
  std::vector<ConfigRecord> watched;  ///< "Watched Topics"
};

/// One AR panel of the record.
struct ArPanelState
{
  std::string key;                    ///< the key of the panel in the stream manager
  bool on = false;                    ///< it was streamed and shown in AR
  std::optional<ArCopySize> size;     ///< its own size choice; none = the global choice
};

/// One AR tool of the record.
struct ArToolState
{
  RosEntityKind kind = RosEntityKind::Topic;
  std::string name;
  std::string type;
  bool writable = false;  ///< a topic tool offers a publish form
  bool shown = true;      ///< its AR panel is shown (false: hidden in AR)
};

/// One pinned topic of the record.
struct WatchedTopicState
{
  std::string name;
  std::string type;
};

/// What is restored.
struct ArRestorePlan
{
  std::vector<ArPanelState> panels;
  std::vector<ArToolState> tools;
  std::vector<WatchedTopicState> watched;

  bool empty() const { return panels.empty() && tools.empty() && watched.empty(); }
};

/// @brief Whether @p key is the key of a panel that the record keeps: this panel
/// ("qar_streaming_panel"), a dock of the RViz window ("dock:<title>") or the copy of Displays.
/// The other AR panels (video, dialogs, the AR control panel, the tools) are made by their own
/// sources and are not part of the record.
bool ar_state_panel_key(const std::string & key);

/// @brief Reads @p text into a plan. A record with a missing or bad field is skipped; each skip,
/// each field that is not known and a Version that is not a number or is newer than
/// @ref kArStateVersion adds one sentence to @p warnings (without a prefix).
ArRestorePlan parse_ar_state(const ArStateText & text, std::vector<std::string> & warnings);

/// @brief The text of @p plan: the Version, and a record for each panel, tool and watched topic
/// in the order of the plan.
ArStateText make_ar_state(const ArRestorePlan & plan);

/// One panel that the stream manager knows, as far as the record cares.
struct ArPanelLive
{
  std::string key;
  bool on = false;  ///< streamed and shown in AR
};

/// @brief The panels the record keeps.
///
/// @param base the panels the record had (the plan of the configuration, or the last session),
/// and none while a session streams and the plan is applied: then @p streams are the whole truth
/// @param streams the panels the stream manager knows; only the keys of @ref ar_state_panel_key
/// count
/// @param queued the keys that wait to be restored: they are on
/// @param choices the size choices of single panels; the map is the truth for every size
/// @param displays_on_by_setting "Hidden Displays On Connect" in force
///
/// A panel is listed when it is on or has a size choice. The copy of Displays is listed when it
/// is on or off against the setting (an entry that agrees with the setting would keep a later
/// change of the setting from taking effect); it is the one panel whose "off" is kept.
std::vector<ArPanelState> merge_ar_panels(
  const std::vector<ArPanelState> & base, const std::vector<ArPanelLive> & streams,
  const std::vector<std::string> & queued, const std::map<std::string, ArCopySize> & choices,
  bool displays_on_by_setting);

/// @brief The panel of @p plan with @p key, or null.
const ArPanelState * find_ar_panel_state(const ArRestorePlan & plan, const std::string & key);

/// @brief The panels to start after the first connect: the plan's "on" panels in the saved
/// order, without the copy of Displays (its start follows @ref hidden_displays_starts) and
/// without a key of @p already (queued or streaming).
std::vector<std::string> panels_to_start(
  const ArRestorePlan & plan, const std::vector<std::string> & already);

/// @brief Whether the copy of Displays starts when the session connects: the plan's entry for it
/// when there is one, else @p setting_on.
bool hidden_displays_starts(const ArRestorePlan & plan, bool setting_on);

/// @brief A plain copy of a configuration tree.
struct ConfigNode
{
  enum class Kind { Empty, Value, Map, List };

  Kind kind = Kind::Empty;
  std::string value;                    ///< Kind::Value
  std::vector<std::string> map_keys;    ///< Kind::Map: the keys in order ...
  std::vector<ConfigNode> map_values;   ///< ... and the children under them
  std::vector<ConfigNode> list;         ///< Kind::List

  /// The child of a map under @p key, or null (also for a node that is not a map).
  const ConfigNode * find(const std::string & key) const;
  /// Makes the node a map when it is not one, and sets @p key to a copy of @p child.
  void set(const std::string & key, ConfigNode child);

  static ConfigNode make_value(std::string text);
};

/// @brief Where the record is read from and written to; the plugin fills the two functions from
/// its RViz configuration, a test from a tree of its own.
struct ArStateConfigIo
{
  /// The map of the panel's configuration.
  std::function<ConfigNode()> read;
  /// Takes the map of the panel's configuration with the record set in it.
  std::function<void(const ConfigNode &)> write;
};

/// @brief The record in the map of the panel's configuration: none when there is no key
/// "AR State" (or it is empty, or not a map: the latter with a sentence in @p warnings). The
/// children that are not plain values (a field that holds a map or a list) are skipped, with
/// one sentence each.
std::optional<ArStateText> read_ar_state_text(
  const ConfigNode & panel_map, std::vector<std::string> & warnings);

/// @brief Sets the key "AR State" of @p panel_map to @p text, in the order of the example of the
/// file comment. A list with no record is left out.
void write_ar_state_text(ConfigNode & panel_map, const ArStateText & text);

/// @brief @ref read_ar_state_text through @p io.
std::optional<ArStateText> load_ar_state_text(
  const ArStateConfigIo & io, std::vector<std::string> & warnings);

/// @brief @ref write_ar_state_text through @p io.
void store_ar_state_text(const ArStateConfigIo & io, const ArStateText & text);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_RESTORE_STATE_HPP_
