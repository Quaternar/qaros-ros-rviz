// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// What is currently exposed to AR: identity, duplicates, removal and lookup by
// the two keys the live path uses (panel id from a model, token from a session).

#include <gtest/gtest.h>

#include <string>

#include "qar_rviz_streaming/ar_exposure_registry.hpp"

using qar_rviz_streaming::ExposedEntity;
using qar_rviz_streaming::ExposureRegistry;
using qar_rviz_streaming::RosEntityKind;
using qar_rviz_streaming::RosEntityRef;

namespace
{

ExposedEntity
entry(RosEntityKind kind, const char * name, const char * type = "pkg/msg/Thing")
{
  ExposedEntity out;
  out.entity = RosEntityRef{kind, name, type};
  return out;
}

}  // namespace

TEST(ArExposureRegistryTest, AddDerivesThePanelId)
{
  ExposureRegistry registry;
  std::string error;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/scan"), &error)) << error;

  const ExposedEntity * found = registry.find(RosEntityKind::Topic, "/scan");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->panel_id, "topic._scan");
  EXPECT_FALSE(found->ar_panel_open);
  EXPECT_TRUE(found->token.empty());
}

TEST(ArExposureRegistryTest, AnExplicitPanelIdIsKept)
{
  ExposureRegistry registry;
  ExposedEntity e = entry(RosEntityKind::Topic, "/scan");
  e.panel_id = "custom.panel";
  ASSERT_TRUE(registry.add(e));
  EXPECT_EQ(registry.find(RosEntityKind::Topic, "/scan")->panel_id, "custom.panel");
}

TEST(ArExposureRegistryTest, TheSameEntityCannotBeExposedTwice)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/scan")));

  std::string error;
  EXPECT_FALSE(registry.add(entry(RosEntityKind::Topic, "/scan"), &error));
  EXPECT_NE(error.find("/scan"), std::string::npos) << error;
  EXPECT_EQ(registry.size(), 1u);
}

TEST(ArExposureRegistryTest, ATopicAndAServiceOfTheSameNameCoexist)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/thing")));
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Service, "/thing")));
  EXPECT_EQ(registry.size(), 2u);
  EXPECT_NE(
    registry.find(RosEntityKind::Topic, "/thing")->panel_id,
    registry.find(RosEntityKind::Service, "/thing")->panel_id);
}

TEST(ArExposureRegistryTest, ADuplicatePanelIdIsRefused)
{
  ExposureRegistry registry;
  ExposedEntity first = entry(RosEntityKind::Topic, "/a");
  first.panel_id = "shared";
  ExposedEntity second = entry(RosEntityKind::Topic, "/b");
  second.panel_id = "shared";
  ASSERT_TRUE(registry.add(first));

  std::string error;
  EXPECT_FALSE(registry.add(second, &error));
  EXPECT_NE(error.find("shared"), std::string::npos) << error;
}

TEST(ArExposureRegistryTest, AnUnnamedEntityIsRefused)
{
  ExposureRegistry registry;
  std::string error;
  EXPECT_FALSE(registry.add(entry(RosEntityKind::Topic, ""), &error));
  EXPECT_FALSE(error.empty());
  EXPECT_TRUE(registry.empty());
}

TEST(ArExposureRegistryTest, InsertionOrderIsPreserved)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/z")));
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/a")));
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Action, "/m")));

  ASSERT_EQ(registry.entries().size(), 3u);
  EXPECT_EQ(registry.entries()[0].entity.name, "/z");
  EXPECT_EQ(registry.entries()[1].entity.name, "/a");
  EXPECT_EQ(registry.entries()[2].entity.name, "/m");
}

TEST(ArExposureRegistryTest, RemoveTakesOnlyTheMatchingKind)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/thing")));
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Service, "/thing")));

  EXPECT_TRUE(registry.remove(RosEntityKind::Topic, "/thing"));
  EXPECT_FALSE(registry.contains(RosEntityKind::Topic, "/thing"));
  EXPECT_TRUE(registry.contains(RosEntityKind::Service, "/thing"));
  EXPECT_FALSE(registry.remove(RosEntityKind::Topic, "/thing"));
}

TEST(ArExposureRegistryTest, RemoveByPanelIdIsTheReverseLookupTheSessionUses)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Action, "/dock")));
  const std::string panel_id = registry.entries().front().panel_id;

  EXPECT_NE(registry.find_by_panel_id(panel_id), nullptr);
  EXPECT_TRUE(registry.remove_by_panel_id(panel_id));
  EXPECT_TRUE(registry.empty());
  EXPECT_FALSE(registry.remove_by_panel_id(panel_id));
}

TEST(ArExposureRegistryTest, TokenLookupFindsTheServedPanel)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/scan")));
  ExposedEntity * live = registry.find(RosEntityKind::Topic, "/scan");
  ASSERT_NE(live, nullptr);
  live->token = "abc123";
  live->page_url = "http://10.0.0.5:8080/p/abc123/";

  const ExposedEntity * found = registry.find_by_token("abc123");
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->entity.name, "/scan");
  EXPECT_EQ(found->page_url, "http://10.0.0.5:8080/p/abc123/");
}

TEST(ArExposureRegistryTest, AnEmptyTokenNeverMatchesAnExposureThatHasNoneYet)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/scan")));
  EXPECT_EQ(registry.find_by_token(""), nullptr);
  EXPECT_EQ(registry.find_by_panel_id(""), nullptr);
}

TEST(ArExposureRegistryTest, DescribeNamesTheKindTheEntityAndItsType)
{
  const ExposedEntity e = entry(RosEntityKind::Service, "/spawn", "turtlesim/srv/Spawn");
  EXPECT_EQ(e.describe(), "service /spawn (turtlesim/srv/Spawn)");
}

TEST(ArExposureRegistryTest, ClearEmptiesEverything)
{
  ExposureRegistry registry;
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/a")));
  ASSERT_TRUE(registry.add(entry(RosEntityKind::Topic, "/b")));
  registry.clear();
  EXPECT_TRUE(registry.empty());
  EXPECT_EQ(registry.size(), 0u);
}

TEST(ArRosEntityKindTest, KindNamesRoundTrip)
{
  for (const RosEntityKind kind :
    {RosEntityKind::Topic, RosEntityKind::Service, RosEntityKind::Action})
  {
    RosEntityKind parsed = RosEntityKind::Topic;
    ASSERT_TRUE(qar_rviz_streaming::ros_entity_kind_from_string(to_string(kind), parsed));
    EXPECT_EQ(parsed, kind);
  }
  RosEntityKind unused = RosEntityKind::Topic;
  EXPECT_FALSE(qar_rviz_streaming::ros_entity_kind_from_string("nonsense", unused));
}
