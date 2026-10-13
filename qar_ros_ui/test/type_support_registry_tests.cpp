// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <string>

#include "qar_ros_ui/type_support_registry.hpp"

using qar_ros_ui::ActionTypeSupport;
using qar_ros_ui::kIntrospectionIdentifier;
using qar_ros_ui::kRmwIdentifier;
using qar_ros_ui::MessageTypeSupport;
using qar_ros_ui::Result;
using qar_ros_ui::ServiceTypeSupport;
using qar_ros_ui::Status;
using qar_ros_ui::TypeSupportRegistry;

TEST(TypeSupportRegistry, NamingRulesMatchTheGeneratedSymbols)
{
  EXPECT_EQ(
    TypeSupportRegistry::library_basename("geometry_msgs", kRmwIdentifier),
    "geometry_msgs__rosidl_typesupport_cpp");
  EXPECT_EQ(
    TypeSupportRegistry::message_symbol(kIntrospectionIdentifier, "geometry_msgs", "msg", "PoseStamped"),
    "rosidl_typesupport_introspection_cpp__get_message_type_support_handle__geometry_msgs__msg__PoseStamped");
  EXPECT_EQ(
    TypeSupportRegistry::service_symbol(kRmwIdentifier, "std_srvs", "srv", "SetBool"),
    "rosidl_typesupport_cpp__get_service_type_support_handle__std_srvs__srv__SetBool");
  EXPECT_EQ(
    TypeSupportRegistry::action_symbol(kRmwIdentifier, "test_msgs", "Fibonacci"),
    "rosidl_typesupport_cpp__get_action_type_support_handle__test_msgs__action__Fibonacci");
}

TEST(TypeSupportRegistry, LoadsBothHandlesOfAMessage)
{
  MessageTypeSupport ts;
  const Result result = TypeSupportRegistry::global().message("geometry_msgs/msg/PoseStamped", ts);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(ts.valid());
  EXPECT_EQ(ts.type_name, "geometry_msgs/msg/PoseStamped");
  EXPECT_STREQ(ts.introspection->typesupport_identifier, kIntrospectionIdentifier);
  EXPECT_STREQ(ts.rmw->typesupport_identifier, kRmwIdentifier);
  ASSERT_NE(ts.members(), nullptr);
  EXPECT_STREQ(ts.members()->message_namespace_, "geometry_msgs::msg");
  EXPECT_STREQ(ts.members()->message_name_, "PoseStamped");
  EXPECT_EQ(ts.members()->member_count_, 2u);
  EXPECT_GT(ts.members()->size_of_, 0u);
  EXPECT_NE(ts.members()->init_function, nullptr);
  EXPECT_NE(ts.members()->fini_function, nullptr);
  EXPECT_TRUE(ts.introspection_library != nullptr && ts.rmw_library != nullptr);
}

TEST(TypeSupportRegistry, LegacyTwoPartNameIsNormalized)
{
  MessageTypeSupport ts;
  ASSERT_TRUE(TypeSupportRegistry::global().message("geometry_msgs/Pose", ts).ok());
  EXPECT_EQ(ts.type_name, "geometry_msgs/msg/Pose");
}

TEST(TypeSupportRegistry, HandlesAndLibrariesAreCached)
{
  TypeSupportRegistry & registry = TypeSupportRegistry::global();
  MessageTypeSupport first;
  MessageTypeSupport second;
  ASSERT_TRUE(registry.message("geometry_msgs/msg/Point", first).ok());
  const std::size_t libraries = registry.loaded_library_count();
  ASSERT_TRUE(registry.message("geometry_msgs/msg/Point", second).ok());
  EXPECT_EQ(first.introspection, second.introspection);
  EXPECT_EQ(first.rmw, second.rmw);
  EXPECT_EQ(first.introspection_library, second.introspection_library);
  EXPECT_EQ(registry.loaded_library_count(), libraries);

  // Another type of the same package reuses the two libraries.
  MessageTypeSupport third;
  ASSERT_TRUE(registry.message("geometry_msgs/msg/Quaternion", third).ok());
  EXPECT_EQ(registry.loaded_library_count(), libraries);
}

TEST(TypeSupportRegistry, UnknownPackageIsReported)
{
  MessageTypeSupport ts;
  const Result result = TypeSupportRegistry::global().message("no_such_pkg_qar/msg/Nope", ts);
  EXPECT_EQ(result.status, Status::TypeSupportUnavailable);
  EXPECT_NE(result.message.find("no_such_pkg_qar__rosidl_typesupport_introspection_cpp"), std::string::npos)
    << result.message;
  EXPECT_FALSE(ts.valid());
}

TEST(TypeSupportRegistry, UnknownTypeInKnownPackageIsReported)
{
  MessageTypeSupport ts;
  const Result result = TypeSupportRegistry::global().message("geometry_msgs/msg/NoSuchType", ts);
  EXPECT_EQ(result.status, Status::TypeSupportUnavailable);
  EXPECT_NE(result.message.find("NoSuchType"), std::string::npos) << result.message;
}

TEST(TypeSupportRegistry, MalformedNamesAreRejectedBeforeLoading)
{
  TypeSupportRegistry registry;
  MessageTypeSupport ts;
  for (const char * bad : {"", "geometry_msgs", "a/b/c/d", "geometry_msgs/foo/Pose",
      "geometry msgs/msg/Pose", "/msg/Pose", "geometry_msgs//Pose", "1pkg/msg/Pose"})
  {
    const Result result = registry.message(bad, ts);
    EXPECT_EQ(result.status, Status::InvalidArgument) << "'" << bad << "': " << result.message;
  }
  EXPECT_EQ(registry.loaded_library_count(), 0u);
}

TEST(TypeSupportRegistry, LoadsAServiceWithItsHalves)
{
  ServiceTypeSupport ts;
  const Result result = TypeSupportRegistry::global().service("std_srvs/srv/SetBool", ts);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(ts.valid());
  ASSERT_NE(ts.members(), nullptr);
  EXPECT_STREQ(ts.members()->service_name_, "SetBool");
  EXPECT_EQ(ts.request.type_name, "std_srvs/srv/SetBool_Request");
  EXPECT_EQ(ts.response.type_name, "std_srvs/srv/SetBool_Response");
  EXPECT_EQ(ts.request.members()->member_count_, 1u);
  EXPECT_EQ(ts.response.members()->member_count_, 2u);
  // The service table and the message tables describe the same structs.
  EXPECT_EQ(ts.members()->request_members_, ts.request.members());
  EXPECT_EQ(ts.members()->response_members_, ts.response.members());
}

TEST(TypeSupportRegistry, LoadsAnActionWithItsEightMessages)
{
  ActionTypeSupport ts;
  const Result result = TypeSupportRegistry::global().action("test_msgs/action/Fibonacci", ts);
  ASSERT_TRUE(result.ok()) << result.message;
  ASSERT_TRUE(ts.valid());
  EXPECT_NE(ts.rmw->goal_service_type_support, nullptr);
  EXPECT_NE(ts.rmw->result_service_type_support, nullptr);
  EXPECT_NE(ts.rmw->cancel_service_type_support, nullptr);
  EXPECT_NE(ts.rmw->feedback_message_type_support, nullptr);
  EXPECT_NE(ts.rmw->status_message_type_support, nullptr);
  EXPECT_EQ(ts.goal.type_name, "test_msgs/action/Fibonacci_Goal");
  EXPECT_EQ(ts.goal.members()->member_count_, 1u);
  EXPECT_EQ(ts.feedback_message.type_name, "test_msgs/action/Fibonacci_FeedbackMessage");
  EXPECT_EQ(ts.feedback_message.members()->member_count_, 2u);  // goal_id + feedback
  EXPECT_EQ(ts.send_goal_request.members()->member_count_, 2u);  // goal_id + goal
  EXPECT_EQ(ts.send_goal_response.members()->member_count_, 2u);  // accepted + stamp
  EXPECT_EQ(ts.get_result_response.members()->member_count_, 2u);  // status + result
}

TEST(TypeSupportRegistry, ActionLookupRequiresAnActionType)
{
  ActionTypeSupport ts;
  const Result result = TypeSupportRegistry::global().action("geometry_msgs/msg/Pose", ts);
  EXPECT_EQ(result.status, Status::InvalidArgument);
}

TEST(TypeSupportRegistry, CandidatePathsPreferExplicitPrefixesAndEndWithTheBareName)
{
  TypeSupportRegistry registry;
  registry.add_search_prefix("Z:/definitely/not/here");
  registry.add_search_prefix(QAR_ROS_UI_TEST_ROS_ROOT);
  registry.add_search_prefix(QAR_ROS_UI_TEST_ROS_ROOT);  // duplicates are folded
  EXPECT_EQ(registry.search_prefixes().size(), 2u);

  const std::vector<std::string> paths =
    registry.candidate_paths("geometry_msgs__rosidl_typesupport_cpp");
  ASSERT_GE(paths.size(), 2u);
  // The non-existent prefix produces no candidate; the real one comes first.
  EXPECT_EQ(paths.front().rfind(QAR_ROS_UI_TEST_ROS_ROOT, 0), 0u) << paths.front();
  EXPECT_NE(paths.front().find("geometry_msgs__rosidl_typesupport_cpp"), std::string::npos);
#ifdef _WIN32
  EXPECT_EQ(paths.back(), "geometry_msgs__rosidl_typesupport_cpp.dll");
#else
  EXPECT_EQ(paths.back(), "libgeometry_msgs__rosidl_typesupport_cpp.so");
#endif

  MessageTypeSupport ts;
  ASSERT_TRUE(registry.message("geometry_msgs/msg/Pose", ts).ok());
  EXPECT_EQ(registry.loaded_library_count(), 2u);
}
