// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Which topic a pick is published to: the options of a mode, the check of a name, the setting
// text. Pure C++17: no ROS graph, no session, no Qt.

#include "qar_rviz_streaming/pick_topic_choice.hpp"

#include <map>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace
{

using qar_rviz_streaming::GraphTopic;
using qar_rviz_streaming::PickTopicOption;
using qar_rviz_streaming::PickTopicSource;
using qar_rviz_streaming::SpatialMode;
namespace q = qar_rviz_streaming;

constexpr const char * kPointStamped = "geometry_msgs/msg/PointStamped";
constexpr const char * kPoseStamped = "geometry_msgs/msg/PoseStamped";
constexpr const char * kPoseCov = "geometry_msgs/msg/PoseWithCovarianceStamped";
constexpr const char * kString = "std_msgs/msg/String";

std::vector<std::string> topics_of(const std::vector<PickTopicOption> & options)
{
  std::vector<std::string> names;
  for (const PickTopicOption & option : options) {
    names.push_back(option.topic);
  }
  return names;
}

}  // namespace

static_assert(q::kPickTopicSlots == q::ar_control::kPickTopicSlots, "one count for page and unit");

TEST(PickTopicChoice, TheDefaultComesFirst)
{
  const struct
  {
    SpatialMode mode;
    const char * topic;
    const char * type;
  } cases[] = {
    {SpatialMode::Point, "/clicked_point", kPointStamped},
    {SpatialMode::NavGoal, "/goal_pose", kPoseStamped},
    {SpatialMode::InitialPose, "/initialpose", kPoseCov},
    {SpatialMode::Area, "/qar/selected_area", "geometry_msgs/msg/PolygonStamped"}};
  for (const auto & c : cases) {
    // The default leads even when the graph and the setting list other topics first.
    const std::vector<GraphTopic> graph = {{"/aaa", {c.type}}};
    const auto options = q::pick_topic_options(c.mode, {"/bbb"}, graph);
    ASSERT_GE(options.size(), 3u) << q::pick_mode_word(c.mode);
    EXPECT_EQ(options[0].topic, c.topic);
    EXPECT_EQ(options[0].type_name, c.type);
    EXPECT_EQ(options[0].source, PickTopicSource::Default);
  }
  EXPECT_TRUE(q::pick_topic_options(SpatialMode::Off, {"/x"}, {}).empty());
}

TEST(PickTopicChoice, ConfiguredThenGraphSortedWithoutDuplicates)
{
  const std::vector<GraphTopic> graph = {
    {"/zeta", {kPointStamped}}, {"/clicked_point", {kPointStamped}}, {"/alpha", {kPointStamped}},
    {"/b", {kPointStamped}}, {"/my_point", {kPointStamped}}};
  const auto options =
    q::pick_topic_options(SpatialMode::Point, {"/my_point", "/b", "/my_point"}, graph);
  EXPECT_EQ(
    topics_of(options),
    (std::vector<std::string>{"/clicked_point", "/my_point", "/b", "/alpha", "/zeta"}));
  EXPECT_EQ(options[0].source, PickTopicSource::Default);
  EXPECT_EQ(options[1].source, PickTopicSource::Setting);
  EXPECT_EQ(options[2].source, PickTopicSource::Setting);
  EXPECT_EQ(options[3].source, PickTopicSource::Graph);
  EXPECT_EQ(options[4].source, PickTopicSource::Graph);
}

TEST(PickTopicChoice, OnlyTypesTheModeCanPublish)
{
  const std::vector<GraphTopic> graph = {
    {"/chat", {kString}}, {"/pose", {kPoseStamped}}, {"/point", {kPointStamped}},
    {"/cov", {kPoseCov}}, {"/pose_abs", {"geometry_msgs/msg/Pose"}}};
  EXPECT_EQ(
    topics_of(q::pick_topic_options(SpatialMode::Point, {}, graph)),
    (std::vector<std::string>{"/clicked_point", "/point"}));
  EXPECT_EQ(
    topics_of(q::pick_topic_options(SpatialMode::NavGoal, {}, graph)),
    (std::vector<std::string>{"/goal_pose", "/cov", "/pose", "/pose_abs"}));
  EXPECT_EQ(
    topics_of(q::pick_topic_options(SpatialMode::InitialPose, {}, graph)),
    (std::vector<std::string>{"/initialpose", "/cov", "/pose"}));
  // A name that is no legal topic name is never offered, even when the graph lists it.
  EXPECT_EQ(
    topics_of(q::pick_topic_options(SpatialMode::Point, {"relative"}, {{"/a b", {kPointStamped}}})),
    (std::vector<std::string>{"/clicked_point"}));
}

TEST(PickTopicChoice, ATopicOfAnotherTypeIsRefused)
{
  const std::vector<GraphTopic> graph = {{"/chat", {kString}}, {"/point", {kPointStamped}}};
  std::string reason;
  EXPECT_FALSE(q::pick_topic_acceptable(SpatialMode::Point, "/chat", kPointStamped, graph, reason));
  EXPECT_NE(reason.find("String"), std::string::npos) << reason;
  EXPECT_NE(reason.find("PointStamped"), std::string::npos) << reason;
  EXPECT_TRUE(q::pick_topic_acceptable(SpatialMode::Point, "/point", kPointStamped, graph, reason));
  // A topic nobody has advertised yet is a typed name: it takes the type of the mode.
  EXPECT_TRUE(q::pick_topic_acceptable(SpatialMode::Point, "/new", kPointStamped, graph, reason));
  // A type the mode cannot publish is refused whatever the topic.
  EXPECT_FALSE(q::pick_topic_acceptable(SpatialMode::Point, "/point", kPoseStamped, graph, reason));
  EXPECT_FALSE(q::pick_topic_acceptable(SpatialMode::Off, "/point", kPointStamped, graph, reason));
  EXPECT_EQ(reason, "no pick mode is armed");
  // A graph entry without types says nothing against the name.
  EXPECT_TRUE(
    q::pick_topic_acceptable(SpatialMode::Point, "/x", kPointStamped, {{"/x", {}}}, reason));
}

TEST(PickTopicChoice, BadNamesAreRefused)
{
  const std::string too_long = "/" + std::string(255, 'a');
  const std::string longest = "/" + std::string(254, 'a');
  const std::string names[] = {
    "", "relative", "no_slash/x", "/a//b", "/a/", "/", "/with space", "/caf\xc3\xa9", "/a-b",
    "/1abc", "/a/2b", too_long};
  for (const std::string & name : names) {
    std::string reason;
    EXPECT_FALSE(q::pick_topic_name_ok(name, reason)) << name;
    EXPECT_FALSE(reason.empty()) << name;
    std::string why;
    EXPECT_FALSE(
      q::pick_topic_acceptable(SpatialMode::Point, name, kPointStamped, {}, why)) << name;
    EXPECT_FALSE(why.empty()) << name;
  }
  for (const std::string & name : {std::string("/a"), std::string("/a/b_2/C"), std::string("/_x"),
      longest})
  {
    std::string reason;
    EXPECT_TRUE(q::pick_topic_name_ok(name, reason)) << name << ": " << reason;
  }
}

TEST(PickTopicChoice, ParseAndFormatRoundTrip)
{
  std::vector<std::string> warnings;
  const auto parsed = q::parse_pick_topics(
    "point=/a,/b;nav_goal=/c;initial_pose=/d;area=/e", warnings);
  EXPECT_TRUE(warnings.empty());
  ASSERT_EQ(parsed.size(), 4u);
  EXPECT_EQ(parsed.at(SpatialMode::Point), (std::vector<std::string>{"/a", "/b"}));
  EXPECT_EQ(parsed.at(SpatialMode::NavGoal), (std::vector<std::string>{"/c"}));
  EXPECT_EQ(parsed.at(SpatialMode::InitialPose), (std::vector<std::string>{"/d"}));
  EXPECT_EQ(parsed.at(SpatialMode::Area), (std::vector<std::string>{"/e"}));

  // The chosen topics alone: one per mode, in the order of the modes.
  const std::map<SpatialMode, std::string> chosen = {
    {SpatialMode::Area, "/e"}, {SpatialMode::Point, "/a"}, {SpatialMode::NavGoal, ""}};
  EXPECT_EQ(q::format_pick_topics(chosen), "point=/a;area=/e");
  EXPECT_EQ(q::format_pick_topics({}), "");

  // With the list of the setting: the chosen topic first, the others after it, no duplicates.
  const std::map<SpatialMode, std::string> pick = {{SpatialMode::Point, "/b"}};
  EXPECT_EQ(q::format_pick_topics(pick, parsed), "point=/b,/a;nav_goal=/c;initial_pose=/d;area=/e");

  // What is written reads back as the same lists.
  std::vector<std::string> again;
  const auto reread = q::parse_pick_topics(q::format_pick_topics(pick, parsed), again);
  EXPECT_TRUE(again.empty());
  EXPECT_EQ(reread.at(SpatialMode::Point), (std::vector<std::string>{"/b", "/a"}));

  // The text never exceeds the limit of the setting.
  std::vector<std::string> many;
  for (int i = 0; i < 40; ++i) {
    many.push_back("/" + std::string(40, 'a') + std::to_string(i));
  }
  const std::string text = q::format_pick_topics({{SpatialMode::Point, "/first"}},
      {{SpatialMode::Point, many}});
  EXPECT_LE(text.size(), q::kMaxPickTopicsSettingBytes);
  EXPECT_EQ(text.rfind("point=/first,", 0), 0u);
}

TEST(PickTopicChoice, BadEntriesAreWarnings)
{
  std::vector<std::string> warnings;
  const auto parsed = q::parse_pick_topics(
    "point=/a,relative,/a,/b c;;bogus=/x;nav_goal;nav_goal=/ok", warnings);
  EXPECT_EQ(parsed.at(SpatialMode::Point), (std::vector<std::string>{"/a"}));
  EXPECT_EQ(parsed.at(SpatialMode::NavGoal), (std::vector<std::string>{"/ok"}));
  EXPECT_EQ(parsed.size(), 2u);
  // relative, the repeat of /a, "/b c", the unknown mode, the part without '='.
  EXPECT_EQ(warnings.size(), 5u);
  for (const std::string & warning : warnings) {
    EXPECT_EQ(warning.rfind("pick topics:", 0), 0u) << warning;
  }
  std::vector<std::string> none;
  EXPECT_TRUE(q::parse_pick_topics("", none).empty());
  EXPECT_TRUE(none.empty());
}

TEST(PickTopicChoice, TheGraphTypeChoosesTheShape)
{
  const std::vector<GraphTopic> graph = {
    {"/amcl_goal", {kPoseCov}}, {"/plain", {kPoseStamped}}, {"/both", {kPoseCov, kPoseStamped}},
    {"/chat", {kString}}};
  EXPECT_EQ(q::pick_type_for_topic(SpatialMode::NavGoal, "/amcl_goal", graph), kPoseCov);
  EXPECT_EQ(q::pick_type_for_topic(SpatialMode::NavGoal, "/plain", graph), kPoseStamped);
  // Both types: the first of the mode's list wins.
  EXPECT_EQ(q::pick_type_for_topic(SpatialMode::NavGoal, "/both", graph), kPoseStamped);
  EXPECT_EQ(q::pick_type_for_topic(SpatialMode::InitialPose, "/both", graph), kPoseCov);
  // No such topic, or a type the mode cannot publish: the type of the mode.
  EXPECT_EQ(q::pick_type_for_topic(SpatialMode::NavGoal, "/nobody", graph), kPoseStamped);
  EXPECT_EQ(q::pick_type_for_topic(SpatialMode::NavGoal, "/chat", graph), kPoseStamped);
  EXPECT_EQ(q::pick_type_for_topic(SpatialMode::Off, "/plain", graph), "");

  const auto options = q::pick_topic_options(SpatialMode::NavGoal, {}, graph);
  ASSERT_EQ(options.size(), 4u);  // /goal_pose, /amcl_goal, /both, /plain
  EXPECT_EQ(options[1].topic, "/amcl_goal");
  EXPECT_EQ(options[1].type_name, kPoseCov);
  EXPECT_EQ(q::short_type(options[1].type_name), "PoseWithCovarianceStamped");
  EXPECT_EQ(q::short_type("Plain"), "Plain");
}

TEST(PickTopicChoice, AtMostSixteenOptions)
{
  std::vector<GraphTopic> graph;
  for (int i = 0; i < 40; ++i) {
    graph.push_back({"/t" + std::string(i < 10 ? "0" : "") + std::to_string(i), {kPointStamped}});
  }
  const auto options = q::pick_topic_options(SpatialMode::Point, {"/s1", "/s2"}, graph);
  ASSERT_EQ(options.size(), q::kMaxPickTopicOptions);
  EXPECT_EQ(options[0].topic, "/clicked_point");
  EXPECT_EQ(options[1].topic, "/s1");
  EXPECT_EQ(options[2].topic, "/s2");
  EXPECT_EQ(options[3].topic, "/t00");
  EXPECT_EQ(options.back().topic, "/t12");
}

TEST(PickTopicChoice, ModeWordsRoundTrip)
{
  for (const SpatialMode mode : {SpatialMode::Point, SpatialMode::NavGoal,
      SpatialMode::InitialPose, SpatialMode::Area})
  {
    SpatialMode back = SpatialMode::Off;
    ASSERT_TRUE(q::pick_mode_from_word(q::pick_mode_word(mode), back));
    EXPECT_EQ(back, mode);
  }
  SpatialMode unchanged = SpatialMode::Area;
  EXPECT_FALSE(q::pick_mode_from_word("off", unchanged));
  EXPECT_FALSE(q::pick_mode_from_word("", unchanged));
  EXPECT_EQ(unchanged, SpatialMode::Area);
  EXPECT_STREQ(q::to_string(PickTopicSource::Graph), "graph");
  EXPECT_STREQ(q::to_string(PickTopicSource::Typed), "typed");
}

TEST(PickTopicChoice, TheChosenTopicIsAlwaysWithinTheFirstSlots)
{
  std::vector<GraphTopic> graph;
  for (int i = 0; i < 8; ++i) {
    graph.push_back({"/t" + std::to_string(i), {kPointStamped}});
  }
  // A topic of the first four stays where it is.
  auto options = q::pick_topic_options(SpatialMode::Point, {}, graph, "/t1");
  EXPECT_EQ(topics_of(options)[2], "/t1");
  // A topic beyond them moves to the place after the conventional one.
  options = q::pick_topic_options(SpatialMode::Point, {}, graph, "/t6");
  ASSERT_EQ(options.size(), 9u);
  EXPECT_EQ(options[0].topic, "/clicked_point");
  EXPECT_EQ(options[1].topic, "/t6");
  EXPECT_EQ(options[1].source, PickTopicSource::Graph);
  EXPECT_EQ(options[2].topic, "/t0");
  EXPECT_EQ(options.back().topic, "/t7");
  // A typed name that no option holds is listed as typed, with the type of the mode.
  options = q::pick_topic_options(SpatialMode::NavGoal, {}, graph, "/my_goal");
  ASSERT_EQ(options.size(), 2u);
  EXPECT_EQ(options[1].topic, "/my_goal");
  EXPECT_EQ(options[1].source, PickTopicSource::Typed);
  EXPECT_EQ(options[1].type_name, kPoseStamped);
  // The default and an empty or illegal name change nothing.
  EXPECT_EQ(q::pick_topic_options(SpatialMode::Point, {}, graph, "/clicked_point").size(), 9u);
  EXPECT_EQ(q::pick_topic_options(SpatialMode::Point, {}, graph, "").size(), 9u);
  EXPECT_EQ(q::pick_topic_options(SpatialMode::Point, {}, graph, "bad name").size(), 9u);
  // Never more than sixteen.
  for (int i = 8; i < 40; ++i) {
    graph.push_back({"/t" + std::to_string(i), {kPointStamped}});
  }
  EXPECT_EQ(q::pick_topic_options(SpatialMode::Point, {}, graph, "/typed").size(),
    q::kMaxPickTopicOptions);
  EXPECT_EQ(q::pick_topic_options(SpatialMode::Point, {}, graph, "/typed")[1].topic, "/typed");
  EXPECT_EQ(graph[0], (GraphTopic{"/t0", {kPointStamped}}));
}
