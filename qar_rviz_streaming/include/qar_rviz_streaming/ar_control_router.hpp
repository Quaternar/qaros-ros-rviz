// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_CONTROL_ROUTER_HPP_
#define QAR_RVIZ_STREAMING__AR_CONTROL_ROUTER_HPP_

/// @file ar_control_router.hpp
/// @brief One inbound control event -> the ROS operation it means.
///
/// The half of the ROS graph -> AR panel path that carries authority: a
/// `qar-ui/1` control frame arrives from a browser in a headset and something
/// has to decide whether it publishes to `/cmd_vel`, calls a service, sends a
/// goal — or is refused. That decision is a pure function of (the panel's
/// model, the panel's document schema, the frame), so it lives here, with no
/// Qt, no rclcpp and no session: the panel executes the returned
/// @ref ControlIntent against `qar_ros_ui`'s bridges and acks with the outcome.
///
/// The router owns the panel's **document** — the same `UiValue` the session
/// serves — because a field edit and the publish that follows it are one
/// conversation: `ros.set` stages a value, `ros.publish` sends what was staged.
/// Staging is all-or-nothing and validated against the schema, so a malformed
/// edit changes nothing and is answered with a reason.

#include <cstdint>
#include <string>

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_model.hpp"
#include "qar_ui_model/ui_protocol.hpp"
#include "qar_ui_model/ui_value.hpp"

#include "qar_rviz_streaming/ar_ros_entity.hpp"

namespace qar_rviz_streaming
{

/// @brief What the host must do about a control event.
enum class RosActionKind : std::uint8_t
{
  None = 0,      ///< Nothing to do (the router already handled it).
  SetField,      ///< A value was staged; publish the echo so other viewers see it.
  PublishTopic,
  CallService,
  SendGoal,
  CancelGoal,
  /// HOST-side only: open a new AR panel streaming this Image/CompressedImage
  /// topic's video. No ROS operation, no payload — see `ar_actions::kOpenVideo`.
  OpenVideoPanel
};
const char * to_string(RosActionKind kind);

struct ControlIntent
{
  RosActionKind kind = RosActionKind::None;
  RosEntityRef entity;

  /// SetField: the full document path that was written ("draft.linear.x"), and
  /// the value written there. The host echoes them with `PanelSession::set_value`.
  std::string document_path;
  qar_ui_model::UiValue value;

  /// Publish / Call / SendGoal: the complete message to hand to the bridge.
  qar_ui_model::UiValue payload;
};

struct ControlRouting
{
  bool ok = false;
  /// Human-readable; goes verbatim into `PanelSession::ack(event, false, ...)`.
  std::string error;
  ControlIntent intent;
};

/// @brief Maps control frames for ONE exposed entity.
class ArControlRouter
{
public:
  /// @param schema @ref make_document_schema's output for the same model.
  ArControlRouter(
    RosEntityRef entity,
    qar_ui_model::UiModel model,
    qar_ui_model::MessageDescriptionPtr schema);

  const RosEntityRef & entity() const { return entity_; }
  const qar_ui_model::UiModel & model() const { return model_; }
  const qar_ui_model::MessageDescriptionPtr & schema() const { return schema_; }

  /// The router's copy of the panel's data document. The host seeds the
  /// read-only sections from ROS; the router writes only the writable ones.
  const qar_ui_model::UiValue & document() const { return document_; }
  /// Mirror an inbound ROS value into the document so a later publish stages
  /// against what is actually there. Fails on a path the schema rejects.
  bool set_document_value(
    const std::string & path, qar_ui_model::UiValue value, std::string * out_error = nullptr);

  /// @brief Map one frame, staging a field edit on success.
  ///
  /// Refusals, each with a reason a human can act on: an unknown widget id, a
  /// widget whose declared action contradicts the frame's, an unknown action
  /// id, an action that does not exist for this entity kind (publishing to a
  /// service), a write to a read-only section, a binding that does not resolve,
  /// and any value the schema will not accept.
  ControlRouting route(const qar_ui_model::ControlBody & control);

  /// @brief The document key a given action reads its payload from
  /// ("draft" / "request" / "goal"); nullptr when the action carries none.
  static const char * outbound_key(RosActionKind kind);

private:
  RosEntityRef entity_;
  qar_ui_model::UiModel model_;
  qar_ui_model::MessageDescriptionPtr schema_;
  qar_ui_model::UiValue document_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_CONTROL_ROUTER_HPP_
