// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__PANEL_SETTINGS_HPP_
#define QAR_RVIZ_STREAMING__PANEL_SETTINGS_HPP_

/// @file panel_settings.hpp
/// @brief The settings the Qar Streaming panel keeps in the RViz configuration: which keys
/// exist, which values each key accepts, and which value is in force. Standard library only.
///
/// ## The one precedence rule
///
/// For every setting the value in force is the first of
///
///  1. the environment variable of the setting, when it is set, not empty (after trimming) and
///     acceptable for the setting;
///  2. the value saved in the RViz configuration, when it is acceptable;
///  3. the built-in default.
///
/// A variable whose text is not acceptable is reported once in the warnings of
/// @ref resolve_settings and ignored; the saved value or the default applies. A variable may
/// hold values that the panel's controls do not offer; a saved configuration may not.
///
/// ## What is saved
///
/// @ref settings_to_save returns the operator's own values and never a value that only the
/// environment supplied, so removing the variable brings the operator's choice back.
///
/// ## Texts
///
/// Every value travels as text (`SettingsText`): the panel copies between the RViz `Config`
/// and this text, so nothing here depends on a type of the configuration library. Canonical
/// text is what the panel stores and compares: a choice word in lower case, `true` or `false`
/// for a bool, a decimal integer, a decimal with a fixed number of decimals, a trimmed text.
/// The canonical text of a number never depends on the C locale.
///
/// Warning texts are sentences without a log prefix; the host adds its own.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qar_rviz_streaming
{

/// The value of the version key that @ref settings_to_save writes.
constexpr int kPanelSettingsVersion = 3;
/// The lowest settings version whose configuration does not ask for the default dock size; see
/// @ref wants_default_dock_size.
constexpr int kDefaultDockSizeSettingsVersion = 2;
/// The key that carries the version. It is not a setting: it is not in
/// @ref panel_setting_specs.
constexpr const char * kSettingsVersionKey = "Settings Version";

/// The keys of the settings, in the spelling of the RViz configuration.
namespace settings_key
{
constexpr const char * kResolution = "Resolution";
constexpr const char * kFpsCap = "FPS Cap";
constexpr const char * kHubMode = "Hub Mode";
constexpr const char * kHubHost = "Hub Host";
constexpr const char * kHubPort = "Hub Port";
constexpr const char * kHiddenDisplaysOnConnect = "Hidden Displays On Connect";
constexpr const char * kArControlPanel = "AR Control Panel";
constexpr const char * kArPanelCloseMode = "AR Panel Close Mode";
constexpr const char * kArVisibility = "AR Visibility";
constexpr const char * kWebPanelScale = "Web Panel Scale";
constexpr const char * kPickTarget = "Pick Target";
constexpr const char * kPickFrame = "Pick Frame";
constexpr const char * kInviteDevice = "Invite Device";
constexpr const char * kInviteHostname = "Invite Hostname";
}  // namespace settings_key

enum class SettingKind : std::uint8_t
{
  Choice = 0,  ///< One word of a list.
  Bool,        ///< `true` or `false`.
  Int,         ///< A whole number: one of a list, or within a range when max is above 0.
  Double,      ///< A decimal number within a range, with a fixed number of decimals.
  Text         ///< A short text of limited characters.
};

/// @brief Everything that is known about one setting.
struct SettingSpec
{
  const char * key = "";
  const char * env = nullptr;            ///< overriding variable, nullptr = none
  SettingKind kind = SettingKind::Text;
  const char * default_value = "";       ///< canonical text
  std::vector<std::string> choices;      ///< Choice/Int: what the controls and a saved value hold
  std::vector<std::string> env_choices;  ///< Choice: what the variable may hold; empty = choices
  double min = 0.0;                      ///< Double, Int: inclusive range (Int: when max > 0)
  double max = 0.0;
  int decimals = 0;                      ///< Double: kept decimals
  std::size_t min_length = 0;            ///< Text: shortest accepted text, in bytes
  std::size_t max_length = 0;            ///< Text: longest accepted text, in bytes
  const char * allowed_chars = "";       ///< Text: every byte is one of these or a letter/digit
};

/// Every persisted setting, in the order of the Settings tab.
const std::vector<SettingSpec> & panel_setting_specs();
/// The spec of @p key, or nullptr for a key that is not a setting.
const SettingSpec * find_setting_spec(const std::string & key);

enum class SettingSource : std::uint8_t { Default = 0, Saved, Environment };
const char * to_string(SettingSource source);  ///< "default", "saved", "environment"

/// @brief The value in force for one setting and where it came from.
struct ResolvedSetting
{
  std::string value;          ///< canonical text
  SettingSource source = SettingSource::Default;
  std::string env_name;       ///< set when source == Environment
  std::string env_raw;        ///< the variable's text as set, for a tooltip
};

using SettingsText = std::map<std::string, std::string>;
using ResolvedSettings = std::map<std::string, ResolvedSetting>;
/// Looks up one environment variable; an empty optional means "not set".
using EnvLookup = std::function<std::optional<std::string>(const char * name)>;

/// @brief What reading a saved configuration found besides the values.
struct SettingsLoadReport
{
  /// The saved version: the value of the version key; 1 when the key is missing but known keys
  /// are present; 0 when nothing of ours was saved.
  int version = 0;
  std::vector<std::string> warnings;      ///< one sentence per rejected value or oddity
  std::vector<std::string> migrated;      ///< one sentence per value changed or dropped on read
  std::vector<std::string> unknown_keys;  ///< not ours, not Class, not Name, not the version
};

/// @brief Reads a bool word: `on`, `1`, `true`, `yes` or `off`, `0`, `false`, `no`, trimmed and
/// case-insensitive. This is the vocabulary of the other boolean variables of the plugin.
/// @return false (and @p out_value unchanged) for any other text.
bool parse_bool_word(const std::string & text, bool & out_value);

/// @brief Brings @p text to the canonical text of @p spec.
///
/// Surrounding white space is dropped. A Choice and a Bool are compared without regard to case;
/// a Text keeps its case. A Choice accepts @ref SettingSpec::env_choices when
/// @p from_environment is set and the list is not empty, @ref SettingSpec::choices otherwise.
/// A Double is a plain decimal number (digits with an optional point; no sign, no exponent),
/// rounded half up to @ref SettingSpec::decimals and then checked against the range.
/// @param out_error On failure the predicate of a sentence: "not one of a|b|c".
/// @return false on failure; @p out_value is then empty.
bool canonicalize_setting(
  const SettingSpec & spec, const std::string & text, bool from_environment,
  std::string & out_value, std::string & out_error);

/// @brief Reads the settings of a saved configuration.
///
/// @p config holds every key of the panel's configuration map as text (Class, Name and the
/// version key included). The result holds the canonical value of every key of
/// @ref panel_setting_specs whose saved text is acceptable; a rejected value is left out and
/// named in the warnings. A version above @ref kPanelSettingsVersion still has its known keys
/// read, with a warning.
///
/// A configuration below version 3 is read as this plugin's current settings: "Peer Admission"
/// is dropped (QAROS serves every target, so the key has no meaning) and the invite kinds that
/// QAROS cannot start are mapped (`quest` to `headset`, `visualizer` and `zed` to `hololens`).
/// Each change is a sentence in @ref SettingsLoadReport::migrated.
SettingsText parse_saved_settings(const SettingsText & config, SettingsLoadReport & report);

/// @brief Whether a loaded configuration wants the default dock size of the panel once.
///
/// True when the configuration has no version key (also when it holds none of the panel's keys)
/// or a version below @ref kDefaultDockSizeSettingsVersion: the dock that such a configuration
/// restores has the size of an older panel. The host applies the default size once, after RViz
/// has restored its window state. The next save writes @ref kPanelSettingsVersion, and a
/// configuration with that version never asks again.
/// @param report The report that @ref parse_saved_settings filled for that configuration.
bool wants_default_dock_size(const SettingsLoadReport & report);

/// @brief The value in force for every setting: the precedence rule of this file.
/// @param saved The operator's own values, canonical (the result of @ref parse_saved_settings
/// plus the edits made since).
/// @param warnings Receives one sentence per environment variable that was set but rejected.
ResolvedSettings resolve_settings(
  const SettingsText & saved, const EnvLookup & env, std::vector<std::string> & warnings);

/// @brief What to write into the configuration: the version key, the operator's own values and
/// the value in force of every other key. A key that the environment overrides and that the
/// operator never chose is left out, and an environment value is never written.
SettingsText settings_to_save(const SettingsText & user, const ResolvedSettings & resolved);

/// Typed reads of a resolved value. A key that is not in @p settings reads as the default of its
/// spec (false, 0, 0.0 and the empty text for a key that has no spec).
bool setting_bool(const ResolvedSettings & settings, const char * key);
int setting_int(const ResolvedSettings & settings, const char * key);
double setting_double(const ResolvedSettings & settings, const char * key);
const std::string & setting_text(const ResolvedSettings & settings, const char * key);

/// @brief The environment variables that have no effect with this QAROS.
const std::vector<std::string> & removed_environment_variables();

/// @brief One sentence for each variable of @ref removed_environment_variables that @p env says
/// is set (even to an empty text): "ignored: <name> has no effect with this QAROS".
std::vector<std::string> removed_environment_sentences(const EnvLookup & env);

/// "action_point" | "ground_plane" | "fixed_depth" <-> the target ids of spatial_interaction.hpp.
/// @return false (and @p out_id unchanged) for any other word.
bool pick_target_id_from_word(const std::string & word, int & out_id);
/// The word of a target id. An unknown id gives the word of the default target, as
/// spatial_target_from_id gives the default target.
const char * pick_target_word(int id);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__PANEL_SETTINGS_HPP_
