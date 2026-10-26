// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_PANEL_GENERATOR_HPP_
#define QAR_RVIZ_STREAMING__AR_PANEL_GENERATOR_HPP_

/// @file ar_panel_generator.hpp
/// @brief Schema -> `UiModel`: the seam between an introspected ROS type and the
/// AR panel that shows and drives it.
///
/// **This is where the semantic templates plug in.** The panel never builds a
/// widget tree itself: it fills a @ref PanelGenerationRequest and hands it to a
/// @ref PanelTemplateRegistry, which picks the best-scoring @ref IPanelTemplate
/// and falls back to @ref GenericFormTemplate — the straightforward
/// field-by-field form. A semantic template ("this is a Twist, give it
/// a joystick"; "this is a BatteryState, give it a gauge") is an
/// `IPanelTemplate` registered ahead of the fallback (ar_semantic_templates.hpp),
/// and nothing else changes: the action ids, the document layout and the
/// control routing are all fixed here, so a template only decides LAYOUT.
///
/// Pure C++17 + `qar_ui_model`. No Qt, no rclcpp — the generation rules are the
/// part with the edge cases, so they are unit-tested without a graph.

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/ui_model.hpp"

#include "qar_rviz_streaming/ar_ros_entity.hpp"

namespace qar_rviz_streaming
{

/// @brief The action ids every template emits and @ref ArControlRouter maps.
///
/// A control event carries an opaque action string; making that string a small
/// closed vocabulary (rather than "whatever the widget id was") is what lets the
/// router be a pure function of (model, schema, event) and lets a semantic
/// template stay layout-only.
namespace ar_actions
{
constexpr const char * kSetField = "ros.set";        ///< Stage one field of the outbound message.
constexpr const char * kPublish = "ros.publish";     ///< Publish the staged message to the topic.
constexpr const char * kCall = "ros.call";           ///< Call the service with the staged request.
constexpr const char * kSendGoal = "ros.send_goal";  ///< Send the staged goal.
constexpr const char * kCancelGoal = "ros.cancel_goal";  ///< Cancel the goal in flight.

/// @brief Open this Image/CompressedImage topic as its own streamed AR video
/// panel. The first HOST-side action: it performs no ROS operation (no
/// publish, no call, no goal), carries no payload and lives in the `ar.`
/// namespace rather than `ros.` precisely to flag that difference at a
/// glance. `ArControlRouter::route` accepts it only for a topic entity whose
/// type is `sensor_msgs/msg/Image` or `sensor_msgs/msg/CompressedImage`; the
/// host executes it in `QarStreamingPanel::openVideoPanel`. See
/// docs/ARCHITECTURE.md, "Video pipeline".
constexpr const char * kOpenVideo = "ar.open_video";
}  // namespace ar_actions

/// @brief Top-level keys of a panel's single data document.
///
/// A panel binds against ONE document (docs/ARCHITECTURE.md,
/// "Data and control flow"), so a service's request and response — and an
/// action's goal, feedback, result and status — are sections of one synthetic
/// root. `draft`/`request`/`goal` are the only writable ones; everything else
/// is what ROS told us.
namespace ar_doc
{
constexpr const char * kMessage = "msg";     ///< Topic: the latest received message.
constexpr const char * kDraft = "draft";     ///< Topic: the message being composed.
constexpr const char * kRequest = "request";
constexpr const char * kResponse = "response";
constexpr const char * kGoal = "goal";
constexpr const char * kFeedback = "feedback";
constexpr const char * kResult = "result";
constexpr const char * kStatus = "status";   ///< Action: goal status as text.
}  // namespace ar_doc

/// @brief True for the document sections a control event may write.
bool is_writable_document_section(std::string_view key);

/// @brief Everything a template needs to lay out one entity.
struct PanelGenerationRequest
{
  RosEntityRef entity;

  /// Filled per kind by the caller from `qar_ros_ui::introspect_*`.
  qar_ui_model::MessageDescriptionPtr message;  ///< Topic.
  qar_ui_model::ServiceDescription service;     ///< Service.
  qar_ui_model::ActionDescription action;       ///< Action.

  /// Topic only: also generate the publish form. Off by default — a
  /// `publish:/cmd_vel` control is a robot command, and the operator opts in.
  bool writable = false;

  /// Caps on the generated tree. A 900-field message must not become a 900-widget
  /// page in a headset; what does not fit is reachable through a JSON inspector.
  std::size_t max_fields = 48;
  std::size_t max_depth = 4;

  /// The description the generated widgets are laid out from, per kind.
  const qar_ui_model::MessageDescription * primary() const;
  bool valid(std::string & out_error) const;
};

struct PanelGenerationResult
{
  bool ok = false;
  std::string error;
  std::string template_name;
  qar_ui_model::UiModel model;
  /// Synthetic description of the whole data document — one Message field per
  /// section. `validate(model, schema)` and @ref ArControlRouter both use it, so
  /// a binding that does not resolve fails here rather than in a headset.
  qar_ui_model::MessageDescriptionPtr document_schema;
  /// Problems from `qar_ui_model::validate`; Errors make @ref ok false.
  std::vector<qar_ui_model::UiProblem> problems;
};

/// @brief The synthetic root description of a request's data document.
/// Public because tests and the router need exactly what the generator used.
qar_ui_model::MessageDescriptionPtr make_document_schema(const PanelGenerationRequest & request);

/// @brief A layout strategy. The semantic templates implement it.
class IPanelTemplate
{
public:
  virtual ~IPanelTemplate() = default;

  /// Stable identifier recorded in the model's metadata and the exposure list.
  virtual std::string name() const = 0;

  /// How well this template fits @p request. <= 0 means "does not apply"; the
  /// registry picks the highest score and, on a tie, the earliest registered.
  /// The generic fallback scores 1, so any specific template beats it.
  virtual int match(const PanelGenerationRequest & request) const = 0;

  /// Build the widget tree. @p schema is @ref make_document_schema's output for
  /// the same request, so a template never has to re-derive it.
  virtual bool build(
    const PanelGenerationRequest & request,
    const qar_ui_model::MessageDescription & schema,
    qar_ui_model::UiModel & out_model,
    std::string & out_error) const = 0;
};

/// @brief The generic template: one widget per field, read-only unless the
/// section is writable. Arrays become a JSON inspector rather than a guessed
/// table — deciding that `ranges[]` is a chart and `poses[]` is a table is
/// exactly the semantic judgement the semantic templates own.
class GenericFormTemplate : public IPanelTemplate
{
public:
  std::string name() const override { return "generic-form"; }
  int match(const PanelGenerationRequest & request) const override;
  bool build(
    const PanelGenerationRequest & request,
    const qar_ui_model::MessageDescription & schema,
    qar_ui_model::UiModel & out_model,
    std::string & out_error) const override;
};

/// @brief Picks a template and validates what it produced.
class PanelTemplateRegistry
{
public:
  /// Registers @ref GenericFormTemplate as the fallback.
  PanelTemplateRegistry();

  /// Later registrations do NOT displace earlier ones on a score tie, so the
  /// fallback (score 1) can never win against a real template.
  void add(std::shared_ptr<IPanelTemplate> tmpl);

  const IPanelTemplate * select(const PanelGenerationRequest & request) const;
  std::vector<std::string> template_names() const;

  /// Build + validate. A model with an Error-severity problem is refused: the
  /// panel would fail at `WebPanelServer::add_panel` anyway, and failing here
  /// names the widget.
  PanelGenerationResult generate(const PanelGenerationRequest & request) const;

private:
  std::vector<std::shared_ptr<IPanelTemplate>> templates_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_PANEL_GENERATOR_HPP_
