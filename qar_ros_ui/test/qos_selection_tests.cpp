// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <vector>

#include "rmw/qos_profiles.h"

#include "qar_ros_ui/qos_selection.hpp"

using qar_ros_ui::choose_publisher_profile;
using qar_ros_ui::choose_subscription_profile;
using qar_ros_ui::describe_profile;
using qar_ros_ui::QosPreset;
using qar_ros_ui::same_matching_policies;

namespace
{

rmw_qos_profile_t profile(
  rmw_qos_reliability_policy_t reliability, rmw_qos_durability_policy_t durability)
{
  rmw_qos_profile_t out = rmw_qos_profile_default;
  out.reliability = reliability;
  out.durability = durability;
  return out;
}

const rmw_qos_profile_t kReliableVolatile =
  profile(RMW_QOS_POLICY_RELIABILITY_RELIABLE, RMW_QOS_POLICY_DURABILITY_VOLATILE);
const rmw_qos_profile_t kBestEffortVolatile =
  profile(RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT, RMW_QOS_POLICY_DURABILITY_VOLATILE);
const rmw_qos_profile_t kReliableTransientLocal =
  profile(RMW_QOS_POLICY_RELIABILITY_RELIABLE, RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);

}  // namespace

TEST(QosSelection, NoPublishersMeansMatchWhateverComes)
{
  const rmw_qos_profile_t chosen = choose_subscription_profile({}, QosPreset::Adaptive, 7);
  EXPECT_EQ(chosen.reliability, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
  EXPECT_EQ(chosen.durability, RMW_QOS_POLICY_DURABILITY_VOLATILE);
  EXPECT_EQ(chosen.history, RMW_QOS_POLICY_HISTORY_KEEP_LAST);
  EXPECT_EQ(chosen.depth, 7u);
}

TEST(QosSelection, AnyBestEffortPublisherForcesBestEffort)
{
  const rmw_qos_profile_t chosen =
    choose_subscription_profile({kReliableVolatile, kBestEffortVolatile}, QosPreset::Adaptive);
  EXPECT_EQ(chosen.reliability, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
  EXPECT_EQ(chosen.durability, RMW_QOS_POLICY_DURABILITY_VOLATILE);
  EXPECT_EQ(chosen.depth, 10u);
}

TEST(QosSelection, AllReliablePublishersGetReliable)
{
  const rmw_qos_profile_t chosen =
    choose_subscription_profile({kReliableVolatile, kReliableVolatile}, QosPreset::Adaptive);
  EXPECT_EQ(chosen.reliability, RMW_QOS_POLICY_RELIABILITY_RELIABLE);
}

TEST(QosSelection, TransientLocalOnlyWhenEveryPublisherOffersIt)
{
  EXPECT_EQ(
    choose_subscription_profile({kReliableTransientLocal}, QosPreset::Adaptive).durability,
    RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
  EXPECT_EQ(
    choose_subscription_profile({kReliableTransientLocal, kReliableVolatile}, QosPreset::Adaptive).durability,
    RMW_QOS_POLICY_DURABILITY_VOLATILE);
}

TEST(QosSelection, PresetsIgnoreTheGraph)
{
  const std::vector<rmw_qos_profile_t> publishers = {kBestEffortVolatile};
  const rmw_qos_profile_t reliable = choose_subscription_profile(publishers, QosPreset::Reliable, 3);
  EXPECT_EQ(reliable.reliability, RMW_QOS_POLICY_RELIABILITY_RELIABLE);
  EXPECT_EQ(reliable.depth, 3u);
  const rmw_qos_profile_t sensor = choose_subscription_profile({}, QosPreset::SensorData, 5);
  EXPECT_EQ(sensor.reliability, RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT);
  EXPECT_EQ(sensor.depth, 5u);
  const rmw_qos_profile_t latched = choose_subscription_profile({}, QosPreset::TransientLocal, 1);
  EXPECT_EQ(latched.durability, RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
  EXPECT_EQ(latched.reliability, RMW_QOS_POLICY_RELIABILITY_RELIABLE);
  const rmw_qos_profile_t system = choose_subscription_profile({}, QosPreset::SystemDefault);
  EXPECT_EQ(system.reliability, RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT);
  EXPECT_EQ(system.history, RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT);
  EXPECT_EQ(choose_subscription_profile({}, QosPreset::Reliable, 0).depth, 1u);
}

TEST(QosSelection, PublisherIsReliableAndFollowsTransientLocalPeers)
{
  const rmw_qos_profile_t plain = choose_publisher_profile({}, {}, QosPreset::Adaptive);
  EXPECT_EQ(plain.reliability, RMW_QOS_POLICY_RELIABILITY_RELIABLE);
  EXPECT_EQ(plain.durability, RMW_QOS_POLICY_DURABILITY_VOLATILE);
  EXPECT_EQ(
    choose_publisher_profile({kReliableTransientLocal}, {}, QosPreset::Adaptive).durability,
    RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
  EXPECT_EQ(
    choose_publisher_profile({}, {kReliableTransientLocal}, QosPreset::Adaptive).durability,
    RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
  EXPECT_EQ(
    choose_publisher_profile({kBestEffortVolatile}, {}, QosPreset::Adaptive).reliability,
    RMW_QOS_POLICY_RELIABILITY_RELIABLE);
  EXPECT_EQ(
    choose_publisher_profile({kReliableTransientLocal}, {}, QosPreset::SensorData).durability,
    RMW_QOS_POLICY_DURABILITY_VOLATILE);
}

TEST(QosSelection, MatchingPoliciesComparison)
{
  EXPECT_TRUE(same_matching_policies(kReliableVolatile, kReliableVolatile));
  EXPECT_FALSE(same_matching_policies(kReliableVolatile, kBestEffortVolatile));
  EXPECT_FALSE(same_matching_policies(kReliableVolatile, kReliableTransientLocal));
  rmw_qos_profile_t deeper = kReliableVolatile;
  deeper.depth = 11;
  EXPECT_FALSE(same_matching_policies(kReliableVolatile, deeper));
  rmw_qos_profile_t with_deadline = kReliableVolatile;
  with_deadline.deadline = {1, 0};
  EXPECT_TRUE(same_matching_policies(kReliableVolatile, with_deadline));
}

TEST(QosSelection, DescribeProfile)
{
  EXPECT_EQ(describe_profile(kReliableVolatile), "reliable/volatile/keep_last(10)");
  EXPECT_EQ(describe_profile(kBestEffortVolatile), "best_effort/volatile/keep_last(10)");
  EXPECT_EQ(describe_profile(kReliableTransientLocal), "reliable/transient_local/keep_last(10)");
  rmw_qos_profile_t keep_all = kReliableVolatile;
  keep_all.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
  EXPECT_EQ(describe_profile(keep_all), "reliable/volatile/keep_all");
  EXPECT_STREQ(qar_ros_ui::to_string(QosPreset::Adaptive), "adaptive");
}
