// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT

#include "qar_ros_ui/qos_selection.hpp"

#include "rmw/qos_profiles.h"

namespace qar_ros_ui
{

namespace
{

rmw_qos_profile_t keep_last(std::size_t depth)
{
  rmw_qos_profile_t profile = rmw_qos_profile_default;
  profile.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  profile.depth = depth == 0 ? 1 : depth;
  return profile;
}

bool is_best_effort(const rmw_qos_profile_t & profile)
{
  return profile.reliability == RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
}

bool is_transient_local(const rmw_qos_profile_t & profile)
{
  return profile.durability == RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
}

bool preset_profile(QosPreset preset, std::size_t depth, rmw_qos_profile_t & out)
{
  switch (preset) {
    case QosPreset::SystemDefault:
      out = rmw_qos_profile_system_default;
      return true;
    case QosPreset::Reliable:
      out = keep_last(depth);
      return true;
    case QosPreset::SensorData:
      out = keep_last(depth);
      out.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
      return true;
    case QosPreset::TransientLocal:
      out = keep_last(depth);
      out.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
      return true;
    case QosPreset::Adaptive:
      return false;
  }
  return false;
}

const char * reliability_name(rmw_qos_reliability_policy_t policy)
{
  switch (policy) {
    case RMW_QOS_POLICY_RELIABILITY_RELIABLE: return "reliable";
    case RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT: return "best_effort";
    case RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT: return "system_default";
    default: return "unknown";
  }
}

const char * durability_name(rmw_qos_durability_policy_t policy)
{
  switch (policy) {
    case RMW_QOS_POLICY_DURABILITY_VOLATILE: return "volatile";
    case RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL: return "transient_local";
    case RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT: return "system_default";
    default: return "unknown";
  }
}

}  // namespace

const char * to_string(QosPreset preset)
{
  switch (preset) {
    case QosPreset::Adaptive: return "adaptive";
    case QosPreset::SystemDefault: return "system_default";
    case QosPreset::Reliable: return "reliable";
    case QosPreset::SensorData: return "sensor_data";
    case QosPreset::TransientLocal: return "transient_local";
  }
  return "unknown";
}

rmw_qos_profile_t choose_subscription_profile(
  const std::vector<rmw_qos_profile_t> & publishers, QosPreset preset, std::size_t depth)
{
  rmw_qos_profile_t profile;
  if (preset_profile(preset, depth, profile)) {
    return profile;
  }
  profile = keep_last(depth);
  if (publishers.empty()) {
    // Nothing to match yet: request the least, which matches everything that
    // may come; reconcile() tightens it once a publisher shows its profile.
    profile.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
    profile.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
    return profile;
  }
  bool any_best_effort = false;
  bool all_transient_local = true;
  for (const rmw_qos_profile_t & publisher : publishers) {
    any_best_effort = any_best_effort || is_best_effort(publisher);
    all_transient_local = all_transient_local && is_transient_local(publisher);
  }
  profile.reliability = any_best_effort ?
    RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT : RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  profile.durability = all_transient_local ?
    RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL : RMW_QOS_POLICY_DURABILITY_VOLATILE;
  return profile;
}

rmw_qos_profile_t choose_publisher_profile(
  const std::vector<rmw_qos_profile_t> & subscriptions,
  const std::vector<rmw_qos_profile_t> & other_publishers,
  QosPreset preset,
  std::size_t depth)
{
  rmw_qos_profile_t profile;
  if (preset_profile(preset, depth, profile)) {
    return profile;
  }
  profile = keep_last(depth);
  profile.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  bool transient_local = false;
  for (const rmw_qos_profile_t & endpoint : subscriptions) {
    transient_local = transient_local || is_transient_local(endpoint);
  }
  for (const rmw_qos_profile_t & endpoint : other_publishers) {
    transient_local = transient_local || is_transient_local(endpoint);
  }
  profile.durability = transient_local ?
    RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL : RMW_QOS_POLICY_DURABILITY_VOLATILE;
  return profile;
}

bool same_matching_policies(const rmw_qos_profile_t & a, const rmw_qos_profile_t & b)
{
  return a.reliability == b.reliability && a.durability == b.durability &&
         a.history == b.history && a.depth == b.depth;
}

std::string describe_profile(const rmw_qos_profile_t & profile)
{
  std::string text = reliability_name(profile.reliability);
  text += "/";
  text += durability_name(profile.durability);
  text += "/";
  switch (profile.history) {
    case RMW_QOS_POLICY_HISTORY_KEEP_LAST:
      text += "keep_last(" + std::to_string(profile.depth) + ")";
      break;
    case RMW_QOS_POLICY_HISTORY_KEEP_ALL:
      text += "keep_all";
      break;
    default:
      text += "system_default";
      break;
  }
  return text;
}

}  // namespace qar_ros_ui
