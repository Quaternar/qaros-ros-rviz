// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/panel_settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

#include "qar_rviz_streaming/spatial_interaction.hpp"

namespace qar_rviz_streaming
{
namespace
{

bool is_space(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

bool is_digit(char c)
{
  return c >= '0' && c <= '9';
}

/// Letters and digits of ASCII only; the C locale never decides.
bool is_ascii_alnum(char c)
{
  return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string trimmed(const std::string & text)
{
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && is_space(text[begin])) {
    ++begin;
  }
  while (end > begin && is_space(text[end - 1])) {
    --end;
  }
  return text.substr(begin, end - begin);
}

std::string lowered(std::string text)
{
  for (char & c : text) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return text;
}

std::string join(const std::vector<std::string> & words, const char * separator)
{
  std::string out;
  for (std::size_t i = 0; i < words.size(); ++i) {
    if (i > 0) {
      out += separator;
    }
    out += words[i];
  }
  return out;
}

/// A text that is safe in a log line: printable ASCII, at most 80 bytes.
std::string printable(const std::string & text)
{
  constexpr std::size_t kMaxBytes = 80;
  std::string out;
  for (std::size_t i = 0; i < text.size() && i < kMaxBytes; ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    out.push_back((c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?');
  }
  if (text.size() > kMaxBytes) {
    out += "...";
  }
  return out;
}

long long power_of_ten(int exponent)
{
  long long value = 1;
  for (int i = 0; i < exponent; ++i) {
    value *= 10;
  }
  return value;
}

/// Digits only, at most nine of them.
bool parse_unsigned(const std::string & text, int & out_value)
{
  if (text.empty() || text.size() > 9) {
    return false;
  }
  int value = 0;
  for (const char c : text) {
    if (!is_digit(c)) {
      return false;
    }
    value = value * 10 + (c - '0');
  }
  out_value = value;
  return true;
}

/// Reads a plain decimal text ("1", "1.5", ".5", "1.") as a count of 10^-decimals, rounding
/// half up on the digits of the text itself, so that the result does not depend on binary
/// floating point or on the C locale.
bool parse_decimal_scaled(const std::string & text, int decimals, long long & out_scaled)
{
  if (text.empty() || text.size() > 32) {
    return false;
  }
  std::size_t i = 0;
  std::string whole;
  std::string fraction;
  while (i < text.size() && is_digit(text[i])) {
    whole.push_back(text[i++]);
  }
  if (i < text.size() && text[i] == '.') {
    ++i;
    while (i < text.size() && is_digit(text[i])) {
      fraction.push_back(text[i++]);
    }
  }
  if (i != text.size() || (whole.empty() && fraction.empty()) || whole.size() > 9) {
    return false;
  }
  long long scaled = 0;
  for (const char c : whole) {
    scaled = scaled * 10 + (c - '0');
  }
  scaled *= power_of_ten(decimals);
  for (int d = 0; d < decimals; ++d) {
    const std::size_t at = static_cast<std::size_t>(d);
    const long long digit = at < fraction.size() ? fraction[at] - '0' : 0;
    scaled += digit * power_of_ten(decimals - 1 - d);
  }
  const std::size_t next = static_cast<std::size_t>(decimals);
  if (next < fraction.size() && fraction[next] >= '5') {
    ++scaled;
  }
  out_scaled = scaled;
  return true;
}

/// The inverse of parse_decimal_scaled for a count that is not negative.
std::string format_scaled(long long scaled, int decimals)
{
  const long long scale = power_of_ten(decimals);
  std::string out = std::to_string(scaled / scale);
  if (decimals > 0) {
    const std::string fraction = std::to_string(scaled % scale);
    out += '.';
    out.append(static_cast<std::size_t>(decimals) - fraction.size(), '0');
    out += fraction;
  }
  return out;
}

const std::string & default_text(const char * key)
{
  static const SettingsText defaults = [] {
      SettingsText table;
      for (const SettingSpec & spec : panel_setting_specs()) {
        table[spec.key] = spec.default_value;
      }
      return table;
    }();
  static const std::string kEmpty;
  const auto found = defaults.find(key);
  return found != defaults.end() ? found->second : kEmpty;
}

const std::string & resolved_or_default_text(const ResolvedSettings & settings, const char * key)
{
  const auto found = settings.find(key);
  return found != settings.end() ? found->second.value : default_text(key);
}

std::string rejected_saved_sentence(
  const SettingSpec & spec, const std::string & text, const std::string & error)
{
  return std::string(spec.key) + " '" + printable(text) + "' is " + error +
         " - using the default '" + spec.default_value + "' instead";
}

SettingSpec choice_spec(
  const char * key, const char * env, const char * default_value,
  std::vector<std::string> choices, std::vector<std::string> env_choices = {})
{
  SettingSpec spec;
  spec.key = key;
  spec.env = env;
  spec.kind = SettingKind::Choice;
  spec.default_value = default_value;
  spec.choices = std::move(choices);
  spec.env_choices = std::move(env_choices);
  return spec;
}

SettingSpec bool_spec(const char * key, const char * env, const char * default_value)
{
  SettingSpec spec;
  spec.key = key;
  spec.env = env;
  spec.kind = SettingKind::Bool;
  spec.default_value = default_value;
  return spec;
}

std::vector<SettingSpec> build_specs()
{
  std::vector<SettingSpec> specs;

  specs.push_back(choice_spec(
      settings_key::kResolution, nullptr, "1280x720",
      {"640x480", "1024x1024", "1280x720", "1600x900", "1920x1080", "2048x2048"}));

  SettingSpec fps;
  fps.key = settings_key::kFpsCap;
  fps.kind = SettingKind::Int;
  fps.default_value = "60";
  fps.choices = {"15", "30", "60", "72", "90"};
  specs.push_back(fps);

  specs.push_back(choice_spec(
      settings_key::kPeerAdmission, "QAR_PANEL_PEER_ADMISSION", "invited",
      {"invited", "new"}, {"invited", "new", "named", "any"}));
  specs.push_back(bool_spec(
      settings_key::kHiddenDisplaysOnConnect, "QAR_PANEL_HIDDEN_DISPLAYS", "true"));
  specs.push_back(bool_spec(settings_key::kArControlPanel, "QAR_PANEL_AR_CONTROL", "true"));
  specs.push_back(choice_spec(
      settings_key::kArPanelCloseMode, "QAR_PANEL_CLOSE_MODE", "destroy",
      {"destroy", "notice"}));
  specs.push_back(choice_spec(
      settings_key::kArVisibility, "QAR_PANEL_AR_VISIBILITY", "hide", {"hide", "close"}));

  SettingSpec scale;
  scale.key = settings_key::kWebPanelScale;
  scale.env = "QAR_PANEL_WEB_SCALE";
  scale.kind = SettingKind::Double;
  scale.default_value = "1.00";
  scale.min = 0.25;
  scale.max = 4.0;
  scale.decimals = 2;
  specs.push_back(scale);

  specs.push_back(choice_spec(
      settings_key::kPickTarget, nullptr, "action_point",
      {"action_point", "ground_plane", "fixed_depth"}));

  SettingSpec frame;
  frame.key = settings_key::kPickFrame;
  frame.kind = SettingKind::Text;
  frame.default_value = "map";
  frame.min_length = 1;
  frame.max_length = 128;
  frame.allowed_chars = "_/.-";
  specs.push_back(frame);

  specs.push_back(choice_spec(
      settings_key::kInviteDevice, nullptr, "visualizer",
      {"visualizer", "hololens", "quest", "zed"}));

  SettingSpec hostname;
  hostname.key = settings_key::kInviteHostname;
  hostname.kind = SettingKind::Text;
  hostname.default_value = "";
  hostname.min_length = 0;
  hostname.max_length = 64;
  hostname.allowed_chars = ".-";
  specs.push_back(hostname);

  return specs;
}

struct TargetWord
{
  const char * word;
  int id;
};

const TargetWord kTargetWords[] = {
  {"action_point", kActionPointTargetId},
  {"ground_plane", kGroundPlaneTargetId},
  {"fixed_depth", kFixedDepthTargetId},
};

}  // namespace

const std::vector<SettingSpec> & panel_setting_specs()
{
  static const std::vector<SettingSpec> specs = build_specs();
  return specs;
}

const SettingSpec * find_setting_spec(const std::string & key)
{
  for (const SettingSpec & spec : panel_setting_specs()) {
    if (key == spec.key) {
      return &spec;
    }
  }
  return nullptr;
}

const char * to_string(SettingSource source)
{
  switch (source) {
    case SettingSource::Default:
      return "default";
    case SettingSource::Saved:
      return "saved";
    case SettingSource::Environment:
      return "environment";
  }
  return "default";
}

bool parse_bool_word(const std::string & text, bool & out_value)
{
  const std::string word = lowered(trimmed(text));
  if (word == "1" || word == "true" || word == "on" || word == "yes") {
    out_value = true;
    return true;
  }
  if (word == "0" || word == "false" || word == "off" || word == "no") {
    out_value = false;
    return true;
  }
  return false;
}

bool canonicalize_setting(
  const SettingSpec & spec, const std::string & text, bool from_environment,
  std::string & out_value, std::string & out_error)
{
  out_value.clear();
  out_error.clear();
  const std::string word = trimmed(text);

  switch (spec.kind) {
    case SettingKind::Choice: {
        const std::vector<std::string> & allowed =
          (from_environment && !spec.env_choices.empty()) ? spec.env_choices : spec.choices;
        const std::string wanted = lowered(word);
        for (const std::string & choice : allowed) {
          if (lowered(choice) == wanted) {
            out_value = choice;
            return true;
          }
        }
        out_error = "not one of " + join(allowed, "|");
        return false;
      }
    case SettingKind::Bool: {
        bool value = false;
        if (!parse_bool_word(word, value)) {
          out_error = "not one of on|off|1|0|true|false|yes|no";
          return false;
        }
        out_value = value ? "true" : "false";
        return true;
      }
    case SettingKind::Int: {
        int value = 0;
        const bool parsed = parse_unsigned(word, value);
        const std::string canonical = std::to_string(value);
        const bool listed = spec.choices.empty() ||
          std::find(spec.choices.begin(), spec.choices.end(), canonical) != spec.choices.end();
        if (!parsed || !listed) {
          out_error = spec.choices.empty() ? std::string("not a whole number")
                                           : "not one of " + join(spec.choices, "|");
          return false;
        }
        out_value = canonical;
        return true;
      }
    case SettingKind::Double: {
        const long long scale = power_of_ten(spec.decimals);
        const long long low = std::llround(spec.min * static_cast<double>(scale));
        const long long high = std::llround(spec.max * static_cast<double>(scale));
        long long scaled = 0;
        if (!parse_decimal_scaled(word, spec.decimals, scaled) || scaled < low || scaled > high) {
          out_error = "not a number from " + format_scaled(low, spec.decimals) + " to " +
            format_scaled(high, spec.decimals);
          return false;
        }
        out_value = format_scaled(scaled, spec.decimals);
        return true;
      }
    case SettingKind::Text: {
        if (word.size() < spec.min_length) {
          out_error = spec.min_length <= 1 ? std::string("empty")
                                           : "shorter than " + std::to_string(spec.min_length) +
            " characters";
          return false;
        }
        if (spec.max_length > 0 && word.size() > spec.max_length) {
          out_error = "longer than " + std::to_string(spec.max_length) + " characters";
          return false;
        }
        const char * extra = spec.allowed_chars != nullptr ? spec.allowed_chars : "";
        for (const char c : word) {
          if (!is_ascii_alnum(c) && (c == '\0' || std::strchr(extra, c) == nullptr)) {
            out_error = std::string("not made of letters, digits") +
              (*extra != '\0' ? std::string(" and ") + extra : std::string());
            return false;
          }
        }
        out_value = word;
        return true;
      }
  }
  out_error = "not a known kind of setting";
  return false;
}

SettingsText parse_saved_settings(const SettingsText & config, SettingsLoadReport & report)
{
  report = SettingsLoadReport();
  SettingsText result;

  bool any_known = false;
  for (const SettingSpec & spec : panel_setting_specs()) {
    any_known = any_known || config.find(spec.key) != config.end();
  }

  const auto version = config.find(kSettingsVersionKey);
  if (version != config.end()) {
    int value = 0;
    if (parse_unsigned(trimmed(version->second), value) && value >= 1) {
      report.version = value;
      if (value > kPanelSettingsVersion) {
        report.warnings.push_back(
          "settings were written by a newer plugin (version " + std::to_string(value) +
          "); unknown keys are ignored");
      }
    } else {
      report.version = 1;
      report.warnings.push_back(
        std::string(kSettingsVersionKey) + " '" + printable(version->second) +
        "' is not a version number - reading the values as version 1");
    }
  } else if (any_known) {
    report.version = 1;
  }

  for (const SettingSpec & spec : panel_setting_specs()) {
    const auto found = config.find(spec.key);
    if (found == config.end()) {
      continue;
    }
    std::string value;
    std::string error;
    if (canonicalize_setting(spec, found->second, false, value, error)) {
      result[spec.key] = value;
    } else {
      report.warnings.push_back(rejected_saved_sentence(spec, found->second, error));
    }
  }

  for (const auto & entry : config) {
    if (entry.first == "Class" || entry.first == "Name" || entry.first == kSettingsVersionKey ||
      find_setting_spec(entry.first) != nullptr)
    {
      continue;
    }
    report.unknown_keys.push_back(entry.first);
  }
  return result;
}

bool wants_default_dock_size(const SettingsLoadReport & report)
{
  // Version 0 is a configuration with no key of ours, version 1 one with keys and no usable
  // version; both are below kDefaultDockSizeSettingsVersion, the lowest version that does not
  // ask for the default dock size.
  return report.version < kDefaultDockSizeSettingsVersion;
}

ResolvedSettings resolve_settings(
  const SettingsText & saved, const EnvLookup & env, std::vector<std::string> & warnings)
{
  ResolvedSettings result;
  for (const SettingSpec & spec : panel_setting_specs()) {
    ResolvedSetting resolved;
    resolved.value = spec.default_value;

    const auto found = saved.find(spec.key);
    if (found != saved.end()) {
      std::string value;
      std::string error;
      if (canonicalize_setting(spec, found->second, false, value, error)) {
        resolved.value = value;
        resolved.source = SettingSource::Saved;
      } else {
        warnings.push_back(rejected_saved_sentence(spec, found->second, error));
      }
    }

    if (spec.env != nullptr && env) {
      const std::optional<std::string> raw = env(spec.env);
      if (raw.has_value() && !trimmed(*raw).empty()) {
        std::string value;
        std::string error;
        if (canonicalize_setting(spec, *raw, true, value, error)) {
          resolved.value = value;
          resolved.source = SettingSource::Environment;
          resolved.env_name = spec.env;
          resolved.env_raw = *raw;
        } else {
          warnings.push_back(
            std::string(spec.env) + "='" + printable(*raw) + "' is " + error + " - using the " +
            (resolved.source == SettingSource::Saved ? "saved value" : "default") + " '" +
            resolved.value + "'");
        }
      }
    }
    result[spec.key] = std::move(resolved);
  }
  return result;
}

SettingsText settings_to_save(const SettingsText & user, const ResolvedSettings & resolved)
{
  SettingsText out;
  out[kSettingsVersionKey] = std::to_string(kPanelSettingsVersion);
  for (const SettingSpec & spec : panel_setting_specs()) {
    std::string value;
    std::string error;
    const auto own = user.find(spec.key);
    if (own != user.end() && canonicalize_setting(spec, own->second, false, value, error)) {
      out[spec.key] = value;
      continue;
    }
    const auto in_force = resolved.find(spec.key);
    if (in_force == resolved.end()) {
      out[spec.key] = spec.default_value;
    } else if (in_force->second.source != SettingSource::Environment) {
      out[spec.key] = in_force->second.value;
    }
  }
  return out;
}

bool setting_bool(const ResolvedSettings & settings, const char * key)
{
  return resolved_or_default_text(settings, key) == "true";
}

int setting_int(const ResolvedSettings & settings, const char * key)
{
  int value = 0;
  return parse_unsigned(resolved_or_default_text(settings, key), value) ? value : 0;
}

double setting_double(const ResolvedSettings & settings, const char * key)
{
  const SettingSpec * spec = find_setting_spec(key);
  long long scaled = 0;
  if (spec == nullptr ||
    !parse_decimal_scaled(resolved_or_default_text(settings, key), spec->decimals, scaled))
  {
    return 0.0;
  }
  return static_cast<double>(scaled) / static_cast<double>(power_of_ten(spec->decimals));
}

const std::string & setting_text(const ResolvedSettings & settings, const char * key)
{
  return resolved_or_default_text(settings, key);
}

bool pick_target_id_from_word(const std::string & word, int & out_id)
{
  for (const TargetWord & target : kTargetWords) {
    if (word == target.word) {
      out_id = target.id;
      return true;
    }
  }
  return false;
}

const char * pick_target_word(int id)
{
  for (const TargetWord & target : kTargetWords) {
    if (id == target.id) {
      return target.word;
    }
  }
  const int fallback = default_spatial_target_id();
  for (const TargetWord & target : kTargetWords) {
    if (fallback == target.id) {
      return target.word;
    }
  }
  return kTargetWords[0].word;
}

}  // namespace qar_rviz_streaming
