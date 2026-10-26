// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The persisted settings of the panel: the table of keys, the precedence rule (acceptable
// non-empty environment > acceptable saved > default), reading and writing the saved text.
// Standard library only: the panel copies between its RViz configuration and SettingsText.

#include "qar_rviz_streaming/panel_settings.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "qar_rviz_streaming/spatial_interaction.hpp"

namespace
{

using qar_rviz_streaming::EnvLookup;
using qar_rviz_streaming::ResolvedSettings;
using qar_rviz_streaming::SettingKind;
using qar_rviz_streaming::SettingsLoadReport;
using qar_rviz_streaming::SettingsText;
using qar_rviz_streaming::SettingSource;
using qar_rviz_streaming::SettingSpec;
namespace key = qar_rviz_streaming::settings_key;

EnvLookup env_of(std::map<std::string, std::string> variables)
{
  return [variables](const char * name) -> std::optional<std::string> {
           const auto found = variables.find(name);
           if (found == variables.end()) {
             return std::nullopt;
           }
           return found->second;
         };
}

ResolvedSettings resolve(
  const SettingsText & saved, const EnvLookup & env, std::vector<std::string> * warnings = nullptr)
{
  std::vector<std::string> local;
  return qar_rviz_streaming::resolve_settings(saved, env, warnings != nullptr ? *warnings : local);
}

const SettingSpec & spec_of(const char * setting)
{
  const SettingSpec * spec = qar_rviz_streaming::find_setting_spec(setting);
  EXPECT_NE(spec, nullptr) << setting;
  static const SettingSpec kEmpty;
  return spec != nullptr ? *spec : kEmpty;
}

/// The canonical text of @p text for @p setting, or "!" followed by the reason.
std::string canonical(const char * setting, const std::string & text, bool from_environment = false)
{
  std::string value;
  std::string error;
  if (!qar_rviz_streaming::canonicalize_setting(
      spec_of(setting), text, from_environment, value, error))
  {
    EXPECT_TRUE(value.empty()) << "a rejected value leaves no value behind";
    return "!" + error;
  }
  EXPECT_TRUE(error.empty());
  return value;
}

bool contains(const std::vector<std::string> & words, const std::string & word)
{
  return std::find(words.begin(), words.end(), word) != words.end();
}

}  // namespace

TEST(PanelSettings, SpecTableKeysAreUniqueAndOrdered)
{
  const std::vector<std::string> expected = {
    "Resolution", "FPS Cap", "Peer Admission", "Hidden Displays On Connect", "AR Control Panel",
    "AR Panel Close Mode", "AR Visibility", "Web Panel Scale", "Pick Target", "Pick Frame",
    "Invite Device", "Invite Hostname"};
  std::vector<std::string> keys;
  for (const SettingSpec & spec : qar_rviz_streaming::panel_setting_specs()) {
    keys.push_back(spec.key);
  }
  EXPECT_EQ(keys, expected);
  EXPECT_EQ(std::set<std::string>(keys.begin(), keys.end()).size(), keys.size());

  // The constants are the table's spelling.
  EXPECT_STREQ(key::kResolution, "Resolution");
  EXPECT_STREQ(key::kFpsCap, "FPS Cap");
  EXPECT_STREQ(key::kPeerAdmission, "Peer Admission");
  EXPECT_STREQ(key::kHiddenDisplaysOnConnect, "Hidden Displays On Connect");
  EXPECT_STREQ(key::kArControlPanel, "AR Control Panel");
  EXPECT_STREQ(key::kArPanelCloseMode, "AR Panel Close Mode");
  EXPECT_STREQ(key::kArVisibility, "AR Visibility");
  EXPECT_STREQ(key::kWebPanelScale, "Web Panel Scale");
  EXPECT_STREQ(key::kPickTarget, "Pick Target");
  EXPECT_STREQ(key::kPickFrame, "Pick Frame");
  EXPECT_STREQ(key::kInviteDevice, "Invite Device");
  EXPECT_STREQ(key::kInviteHostname, "Invite Hostname");

  // The variable of each setting that has one.
  const std::map<std::string, std::string> variables = {
    {"Peer Admission", "QAR_PANEL_PEER_ADMISSION"},
    {"Hidden Displays On Connect", "QAR_PANEL_HIDDEN_DISPLAYS"},
    {"AR Control Panel", "QAR_PANEL_AR_CONTROL"},
    {"AR Panel Close Mode", "QAR_PANEL_CLOSE_MODE"},
    {"AR Visibility", "QAR_PANEL_AR_VISIBILITY"},
    {"Web Panel Scale", "QAR_PANEL_WEB_SCALE"}};
  for (const SettingSpec & spec : qar_rviz_streaming::panel_setting_specs()) {
    const auto variable = variables.find(spec.key);
    if (variable == variables.end()) {
      EXPECT_EQ(spec.env, nullptr) << spec.key;
    } else {
      ASSERT_NE(spec.env, nullptr) << spec.key;
      EXPECT_EQ(std::string(spec.env), variable->second) << spec.key;
    }
    // Every default is acceptable and already canonical, and lookups find the same spec.
    EXPECT_EQ(canonical(spec.key, spec.default_value), spec.default_value) << spec.key;
    EXPECT_EQ(qar_rviz_streaming::find_setting_spec(spec.key), &spec);
  }

  // The version key is not a setting.
  EXPECT_EQ(
    qar_rviz_streaming::find_setting_spec(qar_rviz_streaming::kSettingsVersionKey), nullptr);
  EXPECT_EQ(qar_rviz_streaming::find_setting_spec("Nonsense"), nullptr);
  EXPECT_EQ(spec_of("Peer Admission").kind, SettingKind::Choice);
  EXPECT_EQ(spec_of("FPS Cap").kind, SettingKind::Int);
  EXPECT_EQ(spec_of("Web Panel Scale").kind, SettingKind::Double);
}

TEST(PanelSettings, DefaultsWhenNothingIsSavedOrSet)
{
  std::vector<std::string> warnings;
  const ResolvedSettings settings = resolve({}, env_of({}), &warnings);
  EXPECT_TRUE(warnings.empty());
  ASSERT_EQ(settings.size(), qar_rviz_streaming::panel_setting_specs().size());
  for (const auto & entry : settings) {
    EXPECT_EQ(entry.second.source, SettingSource::Default) << entry.first;
    EXPECT_TRUE(entry.second.env_name.empty()) << entry.first;
  }
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kResolution), "1280x720");
  EXPECT_EQ(qar_rviz_streaming::setting_int(settings, key::kFpsCap), 60);
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kPeerAdmission), "invited");
  EXPECT_TRUE(qar_rviz_streaming::setting_bool(settings, key::kHiddenDisplaysOnConnect));
  EXPECT_TRUE(qar_rviz_streaming::setting_bool(settings, key::kArControlPanel));
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kArPanelCloseMode), "destroy");
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kArVisibility), "hide");
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::setting_double(settings, key::kWebPanelScale), 1.0);
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kPickTarget), "action_point");
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kPickFrame), "map");
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kInviteDevice), "visualizer");
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kInviteHostname), "");

  // A lookup that is not there is no environment.
  const ResolvedSettings without_lookup = resolve({}, EnvLookup{});
  ASSERT_EQ(without_lookup.size(), settings.size());
  for (const auto & entry : without_lookup) {
    EXPECT_EQ(entry.second.source, SettingSource::Default) << entry.first;
    EXPECT_EQ(entry.second.value, settings.at(entry.first).value) << entry.first;
  }

  // A key that is not in the resolved settings reads as the default of its spec.
  const ResolvedSettings none;
  EXPECT_EQ(qar_rviz_streaming::setting_int(none, key::kFpsCap), 60);
  EXPECT_EQ(qar_rviz_streaming::setting_text(none, key::kPickFrame), "map");
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::setting_double(none, key::kWebPanelScale), 1.0);
  EXPECT_EQ(qar_rviz_streaming::setting_text(none, "Nonsense"), "");
  EXPECT_FALSE(qar_rviz_streaming::setting_bool(none, "Nonsense"));
  EXPECT_EQ(qar_rviz_streaming::setting_int(none, "Nonsense"), 0);
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::setting_double(none, "Nonsense"), 0.0);
}

TEST(PanelSettings, SavedValueWinsOverDefault)
{
  const SettingsText saved = {
    {"Resolution", "1920x1080"}, {"FPS Cap", "90"}, {"Peer Admission", "new"},
    {"Hidden Displays On Connect", "false"}, {"AR Control Panel", "false"},
    {"AR Panel Close Mode", "notice"}, {"AR Visibility", "close"}, {"Web Panel Scale", "1.50"},
    {"Pick Target", "ground_plane"}, {"Pick Frame", "odom"}, {"Invite Device", "quest"},
    {"Invite Hostname", "HOLO-1"}};
  std::vector<std::string> warnings;
  const ResolvedSettings settings = resolve(saved, env_of({}), &warnings);
  EXPECT_TRUE(warnings.empty());
  for (const auto & entry : settings) {
    EXPECT_EQ(entry.second.source, SettingSource::Saved) << entry.first;
    EXPECT_EQ(entry.second.value, saved.at(entry.first)) << entry.first;
  }
  EXPECT_EQ(qar_rviz_streaming::setting_int(settings, key::kFpsCap), 90);
  EXPECT_FALSE(qar_rviz_streaming::setting_bool(settings, key::kHiddenDisplaysOnConnect));
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::setting_double(settings, key::kWebPanelScale), 1.5);
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kInviteHostname), "HOLO-1");
}

TEST(PanelSettings, EnvironmentWinsOverSaved)
{
  const SettingsText saved = {
    {"Peer Admission", "new"}, {"Hidden Displays On Connect", "true"},
    {"AR Panel Close Mode", "notice"}, {"Web Panel Scale", "2.00"}, {"Resolution", "640x480"}};
  const EnvLookup env = env_of({
    {"QAR_PANEL_PEER_ADMISSION", "  Invited "}, {"QAR_PANEL_HIDDEN_DISPLAYS", "OFF"},
    {"QAR_PANEL_CLOSE_MODE", "destroy"}, {"QAR_PANEL_WEB_SCALE", " 1.25 "},
    {"QAR_PANEL_AR_VISIBILITY", "Close"}});
  std::vector<std::string> warnings;
  const ResolvedSettings settings = resolve(saved, env, &warnings);
  EXPECT_TRUE(warnings.empty());

  const auto & admission = settings.at(key::kPeerAdmission);
  EXPECT_EQ(admission.value, "invited");
  EXPECT_EQ(admission.source, SettingSource::Environment);
  EXPECT_EQ(admission.env_name, "QAR_PANEL_PEER_ADMISSION");
  EXPECT_EQ(admission.env_raw, "  Invited ");
  EXPECT_FALSE(qar_rviz_streaming::setting_bool(settings, key::kHiddenDisplaysOnConnect));
  EXPECT_EQ(qar_rviz_streaming::setting_text(settings, key::kArPanelCloseMode), "destroy");
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::setting_double(settings, key::kWebPanelScale), 1.25);
  // No saved value, so the variable wins over the default.
  EXPECT_EQ(settings.at(key::kArVisibility).source, SettingSource::Environment);
  EXPECT_EQ(settings.at(key::kArVisibility).value, "close");
  // A setting without a variable is not touched by the others.
  EXPECT_EQ(settings.at(key::kResolution).source, SettingSource::Saved);
  EXPECT_EQ(settings.at(key::kFpsCap).source, SettingSource::Default);
  EXPECT_STREQ(qar_rviz_streaming::to_string(SettingSource::Default), "default");
  EXPECT_STREQ(qar_rviz_streaming::to_string(SettingSource::Saved), "saved");
  EXPECT_STREQ(qar_rviz_streaming::to_string(SettingSource::Environment), "environment");
}

TEST(PanelSettings, EmptyEnvironmentValueDoesNotWin)
{
  const SettingsText saved = {{"Peer Admission", "new"}};
  for (const char * empty : {"", " ", "\t \r\n"}) {
    std::vector<std::string> warnings;
    const ResolvedSettings settings = resolve(
      saved, env_of({{"QAR_PANEL_PEER_ADMISSION", empty}, {"QAR_PANEL_CLOSE_MODE", empty}}),
      &warnings);
    EXPECT_TRUE(warnings.empty()) << "an empty variable is no variable";
    EXPECT_EQ(settings.at(key::kPeerAdmission).value, "new");
    EXPECT_EQ(settings.at(key::kPeerAdmission).source, SettingSource::Saved);
    EXPECT_EQ(settings.at(key::kArPanelCloseMode).source, SettingSource::Default);
    EXPECT_TRUE(settings.at(key::kPeerAdmission).env_name.empty());
  }
}

TEST(PanelSettings, InvalidEnvironmentValueIsReportedAndIgnored)
{
  // With a saved value: that value stays in force.
  {
    std::vector<std::string> warnings;
    const ResolvedSettings settings = resolve(
      {{"Peer Admission", "new"}}, env_of({{"QAR_PANEL_PEER_ADMISSION", "xyz"}}), &warnings);
    EXPECT_EQ(settings.at(key::kPeerAdmission).value, "new");
    EXPECT_EQ(settings.at(key::kPeerAdmission).source, SettingSource::Saved);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(
      warnings[0],
      "QAR_PANEL_PEER_ADMISSION='xyz' is not one of invited|new|named|any - using the saved "
      "value 'new'");
  }
  // Without one: the default.
  {
    std::vector<std::string> warnings;
    const ResolvedSettings settings = resolve({}, env_of({{"QAR_PANEL_PEER_ADMISSION", "xyz"}}),
        &warnings);
    EXPECT_EQ(settings.at(key::kPeerAdmission).value, "invited");
    EXPECT_EQ(settings.at(key::kPeerAdmission).source, SettingSource::Default);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_EQ(
      warnings[0],
      "QAR_PANEL_PEER_ADMISSION='xyz' is not one of invited|new|named|any - using the default "
      "'invited'");
  }
  // Every kind of variable is judged by its own rule.
  {
    std::vector<std::string> warnings;
    const ResolvedSettings settings = resolve(
      {}, env_of({
        {"QAR_PANEL_HIDDEN_DISPLAYS", "maybe"}, {"QAR_PANEL_AR_CONTROL", "2"},
        {"QAR_PANEL_CLOSE_MODE", "keep"}, {"QAR_PANEL_AR_VISIBILITY", "minimise"},
        {"QAR_PANEL_WEB_SCALE", "9"}}),
      &warnings);
    EXPECT_EQ(warnings.size(), 5u);
    for (const auto & entry : settings) {
      EXPECT_EQ(entry.second.source, SettingSource::Default) << entry.first;
    }
    EXPECT_TRUE(contains(
        warnings,
        "QAR_PANEL_WEB_SCALE='9' is not a number from 0.25 to 4.00 - using the default '1.00'"));
    EXPECT_TRUE(contains(
        warnings,
        "QAR_PANEL_CLOSE_MODE='keep' is not one of destroy|notice - using the default 'destroy'"));
  }
  // A variable that is not printable text is made printable and cut in the warning.
  {
    std::vector<std::string> warnings;
    const std::string odd = std::string("x\x01\xff") + std::string(200, 'y');
    resolve({}, env_of({{"QAR_PANEL_PEER_ADMISSION", odd}}), &warnings);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].find("='x??yyy"), std::string::npos);
    EXPECT_LT(warnings[0].size(), 200u);
  }
}

TEST(PanelSettings, EnvironmentMayHoldValuesTheUiDoesNotOffer)
{
  for (const char * word : {"named", "any", "NAMED", " Any "}) {
    const ResolvedSettings settings =
      resolve({{"Peer Admission", "new"}}, env_of({{"QAR_PANEL_PEER_ADMISSION", word}}));
    const auto & admission = settings.at(key::kPeerAdmission);
    EXPECT_EQ(admission.source, SettingSource::Environment) << word;
    EXPECT_TRUE(admission.value == "named" || admission.value == "any") << word;
  }
  // The same words are not acceptable from a saved configuration or from the controls.
  EXPECT_EQ(canonical("Peer Admission", "any", true), "any");
  EXPECT_EQ(canonical("Peer Admission", "named", true), "named");
  EXPECT_EQ(canonical("Peer Admission", "any", false), "!not one of invited|new");
  EXPECT_EQ(canonical("Peer Admission", "named", false), "!not one of invited|new");
  SettingsLoadReport report;
  const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(
    {{"Peer Admission", "any"}}, report);
  EXPECT_TRUE(parsed.empty());
  EXPECT_EQ(report.warnings.size(), 1u);
}

TEST(PanelSettings, InvalidSavedValueFallsBackWithWarning)
{
  const SettingsText config = {
    {"Resolution", "800x600"}, {"FPS Cap", "50"}, {"Peer Admission", "any"},
    {"Hidden Displays On Connect", "maybe"}, {"Web Panel Scale", "0.1"},
    {"Pick Frame", "bad frame"}, {"Invite Device", "phone"}, {"Pick Target", "sky"},
    {"AR Visibility", "minimise"}, {"AR Panel Close Mode", "keep"}, {"AR Control Panel", "2"},
    {"Invite Hostname", "bad_host"}};
  SettingsLoadReport report;
  const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(config, report);
  EXPECT_TRUE(parsed.empty());
  ASSERT_EQ(report.warnings.size(), config.size());
  EXPECT_TRUE(contains(
      report.warnings,
      "Resolution '800x600' is not one of 640x480|1024x1024|1280x720|1600x900|1920x1080|2048x2048"
      " - using the default '1280x720' instead"));
  EXPECT_TRUE(contains(
      report.warnings,
      "FPS Cap '50' is not one of 15|30|60|72|90 - using the default '60' instead"));
  EXPECT_TRUE(contains(
      report.warnings,
      "Web Panel Scale '0.1' is not a number from 0.25 to 4.00 - using the default '1.00' "
      "instead"));

  // What is left in force is the default; the other values are not affected.
  SettingsText mixed = config;
  mixed["Resolution"] = "1600x900";
  const SettingsText kept = qar_rviz_streaming::parse_saved_settings(mixed, report);
  ASSERT_EQ(kept.size(), 1u);
  EXPECT_EQ(kept.at("Resolution"), "1600x900");
  const ResolvedSettings settings = resolve(kept, env_of({}));
  EXPECT_EQ(settings.at(key::kResolution).value, "1600x900");
  EXPECT_EQ(settings.at(key::kFpsCap).value, "60");
  EXPECT_EQ(settings.at(key::kFpsCap).source, SettingSource::Default);
}

TEST(PanelSettings, UnknownKeysAreReported)
{
  const SettingsText config = {
    {"Class", "qar_rviz_streaming/QarStreamingPanel"}, {"Name", "Qar Streaming"},
    {"Settings Version", "1"}, {"Resolution", "1920x1080"}, {"Zoom", "3"}, {"Bogus Key", "x"}};
  SettingsLoadReport report;
  const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(config, report);
  EXPECT_EQ(report.unknown_keys, (std::vector<std::string>{"Bogus Key", "Zoom"}));
  EXPECT_EQ(parsed, (SettingsText{{"Resolution", "1920x1080"}}));
  EXPECT_TRUE(report.warnings.empty()) << "an unknown key is listed, not warned about";
  EXPECT_EQ(report.version, 1);

  // An unknown key is not kept and not saved again.
  const ResolvedSettings settings = resolve(parsed, env_of({}));
  const SettingsText written = qar_rviz_streaming::settings_to_save(parsed, settings);
  EXPECT_EQ(written.count("Zoom"), 0u);
  EXPECT_EQ(written.count("Bogus Key"), 0u);
  EXPECT_EQ(written.count("Class"), 0u);
  EXPECT_EQ(written.count("Name"), 0u);
}

TEST(PanelSettings, NewerVersionReadsKnownKeys)
{
  // Version 2 is the version the plugin writes, so the first newer one is 3.
  const SettingsText config = {
    {"Settings Version", "3"}, {"Resolution", "1920x1080"}, {"Future Key", "x"}};
  SettingsLoadReport report;
  const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(config, report);
  EXPECT_EQ(report.version, 3);
  EXPECT_EQ(parsed, (SettingsText{{"Resolution", "1920x1080"}}));
  ASSERT_EQ(report.warnings.size(), 1u);
  EXPECT_EQ(
    report.warnings[0],
    "settings were written by a newer plugin (version 3); unknown keys are ignored");
  EXPECT_EQ(report.unknown_keys, (std::vector<std::string>{"Future Key"}));
}

TEST(PanelSettings, CurrentVersionIsNotNewer)
{
  // A configuration that this plugin saved reads without a word of warning.
  const SettingsText config = {
    {"Settings Version", "2"}, {"Resolution", "1920x1080"}, {"Future Key", "x"}};
  SettingsLoadReport report;
  const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(config, report);
  EXPECT_EQ(report.version, 2);
  EXPECT_EQ(parsed, (SettingsText{{"Resolution", "1920x1080"}}));
  EXPECT_TRUE(report.warnings.empty());
  EXPECT_EQ(report.unknown_keys, (std::vector<std::string>{"Future Key"}));
}

TEST(PanelSettings, MissingVersionReadsAsVersionOne)
{
  {
    SettingsLoadReport report;
    const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(
      {{"Class", "x/y"}, {"Name", "n"}, {"FPS Cap", "30"}}, report);
    EXPECT_EQ(report.version, 1);
    EXPECT_EQ(parsed, (SettingsText{{"FPS Cap", "30"}}));
    EXPECT_TRUE(report.warnings.empty());
  }
  // Nothing of ours: version 0, no values, and nothing to log.
  {
    SettingsLoadReport report;
    report.version = 7;
    report.warnings.push_back("stale");
    const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(
      {{"Class", "x/y"}, {"Name", "n"}}, report);
    EXPECT_EQ(report.version, 0);
    EXPECT_TRUE(parsed.empty());
    EXPECT_TRUE(report.warnings.empty());
    EXPECT_TRUE(report.unknown_keys.empty());
  }
  // A version that is not a number is read as version one, with a warning.
  for (const char * text : {"abc", "", "0", "-1", "1.5"}) {
    SettingsLoadReport report;
    const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(
      {{"Settings Version", text}, {"FPS Cap", "30"}}, report);
    EXPECT_EQ(report.version, 1) << text;
    EXPECT_EQ(parsed.size(), 1u) << text;
    ASSERT_EQ(report.warnings.size(), 1u) << text;
    EXPECT_NE(report.warnings[0].find("is not a version number"), std::string::npos) << text;
  }
}

TEST(PanelSettings, SaveNeverWritesEnvironmentValues)
{
  const EnvLookup env = env_of({
    {"QAR_PANEL_PEER_ADMISSION", "any"}, {"QAR_PANEL_HIDDEN_DISPLAYS", "off"},
    {"QAR_PANEL_WEB_SCALE", "3.5"}});

  // The operator chose an admission before: that choice is written, not the variable's.
  {
    const SettingsText user = {{"Peer Admission", "new"}, {"Resolution", "1920x1080"}};
    const ResolvedSettings settings = resolve(user, env);
    ASSERT_EQ(settings.at(key::kPeerAdmission).value, "any");
    const SettingsText written = qar_rviz_streaming::settings_to_save(user, settings);
    EXPECT_EQ(written.at("Peer Admission"), "new");
    EXPECT_EQ(written.at("Resolution"), "1920x1080");
    // The operator never chose these two: they are left out, so the variable's value never
    // reaches the file.
    EXPECT_EQ(written.count("Hidden Displays On Connect"), 0u);
    EXPECT_EQ(written.count("Web Panel Scale"), 0u);
  }
  // The operator chose nothing: no key that a variable overrides is written.
  {
    const ResolvedSettings settings = resolve({}, env);
    const SettingsText written = qar_rviz_streaming::settings_to_save({}, settings);
    EXPECT_EQ(written.count("Peer Admission"), 0u);
    EXPECT_EQ(written.count("Hidden Displays On Connect"), 0u);
    EXPECT_EQ(written.count("Web Panel Scale"), 0u);
    for (const auto & entry : written) {
      EXPECT_NE(entry.second, "any") << entry.first;
      EXPECT_NE(entry.second, "3.50") << entry.first;
    }
  }
  // Without the variables every key is written, and the defaults are the operator's too.
  {
    const SettingsText written = qar_rviz_streaming::settings_to_save({}, resolve({}, env_of({})));
    EXPECT_EQ(written.at("Peer Admission"), "invited");
    EXPECT_EQ(written.at("Hidden Displays On Connect"), "true");
    EXPECT_EQ(written.at("Web Panel Scale"), "1.00");
  }
}

TEST(PanelSettings, SaveWritesTheVersionKey)
{
  const SettingsText written = qar_rviz_streaming::settings_to_save({}, resolve({}, env_of({})));
  ASSERT_EQ(written.count(qar_rviz_streaming::kSettingsVersionKey), 1u);
  EXPECT_EQ(written.at(qar_rviz_streaming::kSettingsVersionKey), "2");
  EXPECT_EQ(qar_rviz_streaming::kPanelSettingsVersion, 2);
  EXPECT_STREQ(qar_rviz_streaming::kSettingsVersionKey, "Settings Version");
  EXPECT_EQ(written.size(), qar_rviz_streaming::panel_setting_specs().size() + 1);

  // What is written reads back as the same values with the same version, and in force again.
  SettingsLoadReport report;
  const SettingsText user = {{"Resolution", "2048x2048"}, {"Pick Frame", "base_link"}};
  const SettingsText saved = qar_rviz_streaming::settings_to_save(user, resolve(user, env_of({})));
  const SettingsText read = qar_rviz_streaming::parse_saved_settings(saved, report);
  EXPECT_EQ(report.version, qar_rviz_streaming::kPanelSettingsVersion);
  EXPECT_TRUE(report.warnings.empty());
  EXPECT_TRUE(report.unknown_keys.empty());
  EXPECT_EQ(read.size(), qar_rviz_streaming::panel_setting_specs().size());
  EXPECT_EQ(read.at("Resolution"), "2048x2048");
  EXPECT_EQ(read.at("Pick Frame"), "base_link");
}

TEST(PanelSettings, SaveKeepsTheOperatorsValueWhileTheEnvironmentWins)
{
  // The operator's value is written whatever is in force, so that removing the variable
  // brings the choice back.
  const SettingsText user = {{"AR Visibility", "close"}};
  const ResolvedSettings with_variable =
    resolve(user, env_of({{"QAR_PANEL_AR_VISIBILITY", "hide"}}));
  EXPECT_EQ(with_variable.at(key::kArVisibility).value, "hide");
  EXPECT_EQ(
    qar_rviz_streaming::settings_to_save(user, with_variable).at("AR Visibility"), "close");
  const ResolvedSettings without_variable = resolve(user, env_of({}));
  EXPECT_EQ(without_variable.at(key::kArVisibility).value, "close");
}

TEST(PanelSettings, NeverPersistedKeysAreAbsent)
{
  const std::vector<std::string> never = {
    "Mode", "Pick Mode", "Spatial Mode", "Publish Without Confirming", "Auto Confirm",
    "Allow Publishing From AR", "Allow Publishing", "Prime Next Frame", "Prime",
    "Show Advanced Options", "Selected Tab", "Tab", "Simulated Peers", "Watched Topics",
    "AR Tools", "Render Tick"};
  std::set<std::string> table;
  for (const SettingSpec & spec : qar_rviz_streaming::panel_setting_specs()) {
    table.insert(spec.key);
  }
  const SettingsText written = qar_rviz_streaming::settings_to_save({}, resolve({}, env_of({})));
  for (const std::string & name : never) {
    EXPECT_EQ(table.count(name), 0u) << name;
    EXPECT_EQ(qar_rviz_streaming::find_setting_spec(name), nullptr) << name;
    EXPECT_EQ(written.count(name), 0u) << name;
  }

  // Found in a saved configuration they are unknown keys: reported, not read, not written.
  SettingsText config;
  for (const std::string & name : never) {
    config[name] = "true";
  }
  SettingsLoadReport report;
  const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(config, report);
  EXPECT_TRUE(parsed.empty());
  EXPECT_EQ(report.unknown_keys.size(), never.size());
  EXPECT_EQ(report.version, 0);
}

TEST(PanelSettings, BoolWordsMatchTheEnvironmentVocabulary)
{
  // The words of the other boolean variables of the plugin: on/1/true/yes and off/0/false/no.
  for (const char * word : {"1", "true", "on", "yes", "TRUE", "On", "YES", " yes ", "\ttrue\n"}) {
    bool value = false;
    EXPECT_TRUE(qar_rviz_streaming::parse_bool_word(word, value)) << word;
    EXPECT_TRUE(value) << word;
    EXPECT_EQ(canonical("AR Control Panel", word, true), "true") << word;
    EXPECT_EQ(canonical("AR Control Panel", word, false), "true") << word;
  }
  for (const char * word : {"0", "false", "off", "no", "FALSE", "Off", "NO", " off ", "\tno\n"}) {
    bool value = true;
    EXPECT_TRUE(qar_rviz_streaming::parse_bool_word(word, value)) << word;
    EXPECT_FALSE(value) << word;
    EXPECT_EQ(canonical("AR Control Panel", word, true), "false") << word;
  }
  for (const char * word : {"", " ", "2", "y", "n", "t", "f", "enabled", "disable", "true!",
      "onn"})
  {
    bool value = true;
    EXPECT_FALSE(qar_rviz_streaming::parse_bool_word(word, value)) << word;
    EXPECT_TRUE(value) << "a refused word leaves the value alone: " << word;
    EXPECT_EQ(
      canonical("AR Control Panel", word, true), "!not one of on|off|1|0|true|false|yes|no")
      << word;
  }
}

TEST(PanelSettings, PickTargetWordsRoundTrip)
{
  using qar_rviz_streaming::kActionPointTargetId;
  using qar_rviz_streaming::kFixedDepthTargetId;
  using qar_rviz_streaming::kGroundPlaneTargetId;
  struct Case
  {
    const char * word;
    int id;
  };
  for (const Case & c : {Case{"action_point", kActionPointTargetId},
      Case{"ground_plane", kGroundPlaneTargetId}, Case{"fixed_depth", kFixedDepthTargetId}})
  {
    int id = -1;
    ASSERT_TRUE(qar_rviz_streaming::pick_target_id_from_word(c.word, id)) << c.word;
    EXPECT_EQ(id, c.id) << c.word;
    EXPECT_STREQ(qar_rviz_streaming::pick_target_word(c.id), c.word);
    // Every word is a value the table accepts.
    EXPECT_EQ(canonical("Pick Target", c.word), c.word);
  }
  // The ids are the ones the target list is stored under, and they are never renumbered.
  EXPECT_EQ(kActionPointTargetId, 1);
  EXPECT_EQ(kGroundPlaneTargetId, 0);
  EXPECT_EQ(kFixedDepthTargetId, 2);
  EXPECT_EQ(
    qar_rviz_streaming::spatial_target_options().front().id,
    qar_rviz_streaming::default_spatial_target_id());

  int id = 77;
  EXPECT_FALSE(qar_rviz_streaming::pick_target_id_from_word("", id));
  EXPECT_FALSE(qar_rviz_streaming::pick_target_id_from_word("sky", id));
  EXPECT_FALSE(qar_rviz_streaming::pick_target_id_from_word("Action_Point", id));
  EXPECT_EQ(id, 77) << "a refused word leaves the id alone";
  // An unknown id gives the word of the default target.
  EXPECT_STREQ(qar_rviz_streaming::pick_target_word(99), "action_point");
  EXPECT_STREQ(qar_rviz_streaming::pick_target_word(-1), "action_point");
  EXPECT_EQ(
    std::string(
      qar_rviz_streaming::pick_target_word(qar_rviz_streaming::default_spatial_target_id())),
    "action_point");
}

TEST(PanelSettings, PickFrameValidation)
{
  // 1..128 bytes of letters, digits and _/.- ; a leading slash is allowed, not required.
  for (const char * frame : {"map", "odom", "base_link", "/odom", "a.b-c/d_e", "A", "frame_1",
      "camera_link/optical"})
  {
    EXPECT_EQ(canonical("Pick Frame", frame), frame) << frame;
  }
  EXPECT_EQ(canonical("Pick Frame", std::string(128, 'f')), std::string(128, 'f'));
  EXPECT_EQ(canonical("Pick Frame", "  map \t"), "map") << "surrounding white space goes";
  EXPECT_EQ(canonical("Pick Frame", "Base_Link"), "Base_Link") << "the case stays";

  EXPECT_EQ(canonical("Pick Frame", ""), "!empty");
  EXPECT_EQ(canonical("Pick Frame", "   "), "!empty");
  EXPECT_EQ(canonical("Pick Frame", std::string(129, 'f')), "!longer than 128 characters");
  const std::string expected_reason = "!not made of letters, digits and _/.-";
  EXPECT_EQ(canonical("Pick Frame", "has space"), expected_reason);
  EXPECT_EQ(canonical("Pick Frame", "bad!"), expected_reason);
  EXPECT_EQ(canonical("Pick Frame", "a:b"), expected_reason);
  EXPECT_EQ(canonical("Pick Frame", "tab\tinside"), expected_reason);
  EXPECT_EQ(canonical("Pick Frame", std::string("nul\0inside", 10)), expected_reason);
  EXPECT_EQ(canonical("Pick Frame", "caf\xc3\xa9"), expected_reason);

  // The hostname of the device to invite: 0..64 bytes of letters, digits, '.' and '-'.
  EXPECT_EQ(canonical("Invite Hostname", ""), "");
  EXPECT_EQ(canonical("Invite Hostname", "  "), "");
  EXPECT_EQ(canonical("Invite Hostname", "HOLOLENS-A2B3"), "HOLOLENS-A2B3");
  EXPECT_EQ(canonical("Invite Hostname", "host.local"), "host.local");
  EXPECT_EQ(canonical("Invite Hostname", std::string(64, 'h')), std::string(64, 'h'));
  EXPECT_EQ(canonical("Invite Hostname", std::string(65, 'h')), "!longer than 64 characters");
  EXPECT_EQ(canonical("Invite Hostname", "a_b"), "!not made of letters, digits and .-");
  EXPECT_EQ(canonical("Invite Hostname", "a/b"), "!not made of letters, digits and .-");

  // The device kind and the Int setting are lists of words and numbers.
  EXPECT_EQ(canonical("Invite Device", "HoloLens"), "hololens");
  EXPECT_EQ(canonical("Invite Device", "phone"), "!not one of visualizer|hololens|quest|zed");
  EXPECT_EQ(canonical("FPS Cap", "072"), "72");
  EXPECT_EQ(canonical("FPS Cap", " 90 "), "90");
  EXPECT_EQ(canonical("FPS Cap", "61"), "!not one of 15|30|60|72|90");
  EXPECT_EQ(canonical("FPS Cap", "-30"), "!not one of 15|30|60|72|90");
  EXPECT_EQ(canonical("FPS Cap", "30.0"), "!not one of 15|30|60|72|90");
  EXPECT_EQ(canonical("FPS Cap", "99999999999"), "!not one of 15|30|60|72|90");
  EXPECT_EQ(canonical("FPS Cap", ""), "!not one of 15|30|60|72|90");
}

TEST(PanelSettings, WebPanelScaleRangeAndRounding)
{
  // 0.25..4.00 with two decimals, rounded half up on the digits of the text.
  EXPECT_EQ(canonical("Web Panel Scale", "1"), "1.00");
  EXPECT_EQ(canonical("Web Panel Scale", "1.5"), "1.50");
  EXPECT_EQ(canonical("Web Panel Scale", "1."), "1.00");
  EXPECT_EQ(canonical("Web Panel Scale", ".5"), "0.50");
  EXPECT_EQ(canonical("Web Panel Scale", "0.25"), "0.25");
  EXPECT_EQ(canonical("Web Panel Scale", "4"), "4.00");
  EXPECT_EQ(canonical("Web Panel Scale", "4.00"), "4.00");
  EXPECT_EQ(canonical("Web Panel Scale", " 1.25 "), "1.25");
  EXPECT_EQ(canonical("Web Panel Scale", "01.50"), "1.50");
  EXPECT_EQ(canonical("Web Panel Scale", "1.234"), "1.23");
  EXPECT_EQ(canonical("Web Panel Scale", "1.235"), "1.24");
  EXPECT_EQ(canonical("Web Panel Scale", "1.005"), "1.01") << "decimal rounding, not binary";
  EXPECT_EQ(canonical("Web Panel Scale", "1.004999"), "1.00");
  EXPECT_EQ(canonical("Web Panel Scale", "0.249"), "0.25") << "the range is checked after rounding";
  EXPECT_EQ(canonical("Web Panel Scale", "3.999"), "4.00");

  const std::string range = "!not a number from 0.25 to 4.00";
  EXPECT_EQ(canonical("Web Panel Scale", "0.24"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "0.244"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "0"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "4.01"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "4.005"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "9"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "007.5"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "-1"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "+1"), range);
  EXPECT_EQ(canonical("Web Panel Scale", ""), range);
  EXPECT_EQ(canonical("Web Panel Scale", "."), range);
  EXPECT_EQ(canonical("Web Panel Scale", "abc"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "1e0"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "1,5"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "1.2.3"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "0x1"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "nan"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "inf"), range);
  EXPECT_EQ(canonical("Web Panel Scale", "1 2"), range);
  EXPECT_EQ(canonical("Web Panel Scale", std::string(40, '1')), range);

  // The typed read gives the number the canonical text names.
  ResolvedSettings settings = resolve({{"Web Panel Scale", "0.35"}}, env_of({}));
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::setting_double(settings, key::kWebPanelScale), 0.35);
  settings = resolve({{"Web Panel Scale", "3.07"}}, env_of({}));
  EXPECT_DOUBLE_EQ(qar_rviz_streaming::setting_double(settings, key::kWebPanelScale), 3.07);
}

TEST(PanelSettings, ResolvedSettingsRoundTripThroughTheSavedText)
{
  const SettingsText config = {
    {"Class", "qar_rviz_streaming/QarStreamingPanel"}, {"Name", "Qar Streaming"},
    {"Settings Version", "1"}, {"Resolution", "1600x900"}, {"FPS Cap", "30"},
    {"Peer Admission", "new"}, {"Web Panel Scale", "2.5"}, {"Pick Target", "fixed_depth"}};
  SettingsLoadReport report;
  const SettingsText user = qar_rviz_streaming::parse_saved_settings(config, report);
  EXPECT_EQ(report.version, 1);
  EXPECT_TRUE(report.warnings.empty());
  EXPECT_EQ(user.at("Web Panel Scale"), "2.50") << "the saved text is made canonical";

  const ResolvedSettings settings = resolve(user, env_of({}));
  const SettingsText saved = qar_rviz_streaming::settings_to_save(user, settings);
  EXPECT_EQ(saved.at("Resolution"), "1600x900");
  EXPECT_EQ(saved.at("FPS Cap"), "30");
  EXPECT_EQ(saved.at("Peer Admission"), "new");
  EXPECT_EQ(saved.at("Web Panel Scale"), "2.50");
  EXPECT_EQ(saved.at("Pick Target"), "fixed_depth");
  // The keys the operator never chose are written with the value in force.
  EXPECT_EQ(saved.at("Pick Frame"), "map");

  // Reading what was written and resolving again changes nothing.
  SettingsLoadReport again;
  const SettingsText reread = qar_rviz_streaming::parse_saved_settings(saved, again);
  EXPECT_TRUE(again.warnings.empty());
  const ResolvedSettings resolved_again = resolve(reread, env_of({}));
  for (const auto & entry : settings) {
    EXPECT_EQ(resolved_again.at(entry.first).value, entry.second.value) << entry.first;
  }
}

// --- Version 2 and the default dock size ------------------------------------------------------

TEST(PanelSettings, VersionOneConfigLoadsAllTwelveKeysAsBefore)
{
  // A configuration saved with version 1: every key of the table, each with a value that is not
  // the default, so that a key that is not read shows.
  const SettingsText config = {
    {"Class", "qar_rviz_streaming/QarStreamingPanel"}, {"Name", "Qar Streaming"},
    {"Settings Version", "1"}, {"Resolution", "1920x1080"}, {"FPS Cap", "30"},
    {"Peer Admission", "new"}, {"Hidden Displays On Connect", "false"},
    {"AR Control Panel", "false"}, {"AR Panel Close Mode", "notice"}, {"AR Visibility", "close"},
    {"Web Panel Scale", "1.5"}, {"Pick Target", "ground_plane"}, {"Pick Frame", "odom"},
    {"Invite Device", "quest"}, {"Invite Hostname", "headset-1.local"}};
  SettingsLoadReport report;
  const SettingsText parsed = qar_rviz_streaming::parse_saved_settings(config, report);
  EXPECT_EQ(report.version, 1);
  EXPECT_TRUE(report.warnings.empty());
  EXPECT_TRUE(report.unknown_keys.empty());
  EXPECT_EQ(
    parsed,
    (SettingsText{
      {"Resolution", "1920x1080"}, {"FPS Cap", "30"}, {"Peer Admission", "new"},
      {"Hidden Displays On Connect", "false"}, {"AR Control Panel", "false"},
      {"AR Panel Close Mode", "notice"}, {"AR Visibility", "close"},
      {"Web Panel Scale", "1.50"}, {"Pick Target", "ground_plane"}, {"Pick Frame", "odom"},
      {"Invite Device", "quest"}, {"Invite Hostname", "headset-1.local"}}));
  EXPECT_EQ(parsed.size(), 12u);
  EXPECT_EQ(parsed.size(), qar_rviz_streaming::panel_setting_specs().size());
  for (const SettingSpec & spec : qar_rviz_streaming::panel_setting_specs()) {
    EXPECT_NE(parsed.at(spec.key), spec.default_value) << spec.key << " was read, not defaulted";
  }

  // It is in force as it was saved, and it asks for the default dock size once.
  const ResolvedSettings settings = resolve(parsed, env_of({}));
  for (const auto & entry : parsed) {
    EXPECT_EQ(settings.at(entry.first).value, entry.second) << entry.first;
    EXPECT_EQ(settings.at(entry.first).source, SettingSource::Saved) << entry.first;
  }
  EXPECT_TRUE(qar_rviz_streaming::wants_default_dock_size(report));

  // Saving it writes version 2 and the same twelve values; loading that asks no more.
  const SettingsText saved = qar_rviz_streaming::settings_to_save(parsed, settings);
  EXPECT_EQ(saved.at("Settings Version"), "2");
  SettingsLoadReport again;
  EXPECT_EQ(qar_rviz_streaming::parse_saved_settings(saved, again), parsed);
  EXPECT_EQ(again.version, 2);
  EXPECT_TRUE(again.warnings.empty());
  EXPECT_FALSE(qar_rviz_streaming::wants_default_dock_size(again));
}

TEST(PanelSettings, DefaultDockSizeIsWantedBelowVersionTwo)
{
  EXPECT_EQ(qar_rviz_streaming::kDefaultDockSizeSettingsVersion, 2);
  EXPECT_GE(
    qar_rviz_streaming::kPanelSettingsVersion, qar_rviz_streaming::kDefaultDockSizeSettingsVersion);

  struct Case
  {
    const char * what;
    SettingsText config;
    int version;
    bool wanted;
  };
  const Case cases[] = {
    {"no key of ours at all", {{"Class", "x/y"}, {"Name", "n"}}, 0, true},
    {"an empty configuration", {}, 0, true},
    {"our keys and no version", {{"FPS Cap", "30"}}, 1, true},
    {"version 1", {{"Settings Version", "1"}, {"FPS Cap", "30"}}, 1, true},
    {"version 1, no other key", {{"Settings Version", "1"}}, 1, true},
    {"a version that is not a number", {{"Settings Version", "abc"}, {"FPS Cap", "30"}}, 1, true},
    {"version 0", {{"Settings Version", "0"}, {"FPS Cap", "30"}}, 1, true},
    {"a negative version", {{"Settings Version", "-1"}}, 1, true},
    {"version 2", {{"Settings Version", "2"}, {"FPS Cap", "30"}}, 2, false},
    {"version 2, no other key", {{"Settings Version", "2"}}, 2, false},
    {"version 3, newer", {{"Settings Version", "3"}, {"FPS Cap", "30"}}, 3, false},
    {"version 99", {{"Settings Version", "99"}}, 99, false},
    {"version with white space", {{"Settings Version", " 2 "}}, 2, false},
  };
  for (const Case & c : cases) {
    SettingsLoadReport report;
    qar_rviz_streaming::parse_saved_settings(c.config, report);
    EXPECT_EQ(report.version, c.version) << c.what;
    EXPECT_EQ(qar_rviz_streaming::wants_default_dock_size(report), c.wanted) << c.what;
  }

  // A report that was never filled is the report of a configuration with no key of ours.
  EXPECT_TRUE(qar_rviz_streaming::wants_default_dock_size(SettingsLoadReport()));
}

TEST(PanelSettings, WhatIsSavedNeverAsksForTheDefaultDockSizeAgain)
{
  // Whatever was loaded, a save writes the current version, and the next load asks nothing.
  for (const SettingsText & user : {SettingsText{}, SettingsText{{"Resolution", "2048x2048"}}}) {
    const SettingsText saved =
      qar_rviz_streaming::settings_to_save(user, resolve(user, env_of({})));
    SettingsLoadReport report;
    qar_rviz_streaming::parse_saved_settings(saved, report);
    EXPECT_EQ(report.version, qar_rviz_streaming::kPanelSettingsVersion);
    EXPECT_FALSE(qar_rviz_streaming::wants_default_dock_size(report));
  }
}
