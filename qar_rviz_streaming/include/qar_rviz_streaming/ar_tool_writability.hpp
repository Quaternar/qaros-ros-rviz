// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_TOOL_WRITABILITY_HPP_
#define QAR_RVIZ_STREAMING__AR_TOOL_WRITABILITY_HPP_

/// @file ar_tool_writability.hpp
/// @brief Which AR tools can publish by default: the ones a person can use without a keyboard.
///
/// A writable topic page stages a message and publishes it with a Publish button that asks
/// for a second press. Every field the person must set has to be reachable by a switch, a
/// slider or a button, and the message has to be a command whose meaning does not depend on
/// a typed value. The rule is one table of exact type names, so a new row is a one-line
/// change and nothing is guessed from the shape of a message. Standard library only.

#include <string>
#include <vector>

#include "qar_rviz_streaming/ar_ros_entity.hpp"

namespace qar_rviz_streaming
{

/// @brief One topic type that is writable by default, and why.
struct KeyboardFreeTopicType
{
  const char * type_name;  ///< Exact ROS 2 type name, e.g. `std_msgs/msg/Bool`.
  const char * reason;     ///< Short wording for the dialog and the log.
};

/// @brief The writable rows of the table (Bool, Twist, TwistStamped).
///
/// `std_msgs/msg/Empty` is not a row: ROS 2 gives a message without fields one placeholder
/// member, and the generated page shows it as a number field next to the Publish button.
const std::vector<KeyboardFreeTopicType> & keyboard_free_topic_types();

/// @brief Whether an AR tool publishes by default, and the reason in words.
struct ArToolWriteDefault
{
  bool writable = false;
  std::string reason;
};

/// @brief The default for an entity. A topic follows the table; a service and an action are
/// not governed by it (their page always has the request or goal form): writable = true with
/// the reason "its request form" / "its goal form". An unknown or empty type is read-only.
ArToolWriteDefault default_write_for(RosEntityKind kind, const std::string & type_name);

/// @brief The reason a topic type is read-only, chosen by the package and name of the type.
std::string read_only_reason(const std::string & type_name);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_TOOL_WRITABILITY_HPP_
