// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_panel_generator.hpp"

#include <algorithm>
#include <string_view>
#include <utility>

namespace qar_rviz_streaming
{
namespace
{
using qar_ui_model::FieldDescription;
using qar_ui_model::FieldKind;
using qar_ui_model::MessageDescription;
using qar_ui_model::MessageDescriptionBuilder;
using qar_ui_model::UiModel;
using qar_ui_model::UiValue;
using qar_ui_model::Widget;

/// A nested message whose members are exactly these numeric scalars gets the
/// purpose-built widget. This is a STRUCTURAL test (the field shape), not a
/// semantic one: "geometry_msgs/Point is a position you can drag" is the
/// semantic templates' call.
bool has_numeric_members(const MessageDescription & message, const std::vector<const char *> & names)
{
  for (const char * name : names) {
    const FieldDescription * member = message.find_field(name);
    if (member == nullptr || member->is_array() || !qar_ui_model::is_numeric(member->kind)) {
      return false;
    }
  }
  return true;
}

bool looks_like_quaternion(const MessageDescription & message)
{
  return message.fields.size() == 4 && has_numeric_members(message, {"x", "y", "z", "w"});
}

bool looks_like_vector3(const MessageDescription & message)
{
  return message.fields.size() == 3 && has_numeric_members(message, {"x", "y", "z"});
}

std::string join_path(const std::string & prefix, const std::string & name)
{
  return prefix.empty() ? name : prefix + "." + name;
}

/// One writable/read-only section of the document, laid out field by field.
struct SectionBuilder
{
  const PanelGenerationRequest * request = nullptr;
  std::size_t budget = 0;
  bool truncated = false;

  Widget make_leaf(
    const FieldDescription & field, const std::string & id, const std::string & path,
    bool writable) const
  {
    const std::string label = field.display_label();
    Widget widget = [&]() {
        if (!writable) {
          return qar_ui_model::widgets::value(id, path, label);
        }
        if (qar_ui_model::is_boolean(field.kind)) {
          return qar_ui_model::widgets::toggle(id, path, label);
        }
        if (qar_ui_model::is_string(field.kind)) {
          Widget text = qar_ui_model::widgets::text_input(id, path, label);
          if (field.string_upper_bound != 0) {
            text.max_length = field.string_upper_bound;
          }
          return text;
        }
        Widget number = qar_ui_model::widgets::number(id, path, label);
        number.integer = qar_ui_model::is_integer(field.kind);
        return number;
      }();

    if (!field.unit.empty()) {
      widget.with_unit(field.unit);
    }
    if (!field.description.empty()) {
      widget.with_description(field.description);
    }
    // Only the field's OWN documented range is applied: the kind's numeric
    // bounds (int64) do not survive a double, and the validator warns about a
    // range a field cannot hold.
    if (field.min && field.max) {
      widget.with_range(*field.min, *field.max);
    }
    if (field.step) {
      widget.with_step(*field.step);
    }
    if (writable) {
      widget.with_editable(true).with_action(ar_actions::kSetField);
    }
    return widget;
  }

  void append(
    Widget & parent, const std::string & id_prefix, const std::string & bind_prefix,
    const MessageDescription & message, bool writable, std::size_t depth)
  {
    for (const FieldDescription & field : message.fields) {
      if (budget == 0) {
        truncated = true;
        return;
      }
      const std::string path = join_path(bind_prefix, field.name);
      const std::string id = id_prefix + "." + field.name;

      // An array is one thing, and which array deserves a table, a chart or a
      // point cloud viewer is a semantic judgement for the semantic templates.
      // Here it is shown whole and is never editable — an absent field takes the
      // type's default, so a publish still produces a valid message.
      if (field.is_array()) {
        --budget;
        parent.add(
          qar_ui_model::widgets::json_inspector(id, path).with_label(field.display_label()));
        continue;
      }

      if (field.is_message()) {
        if (!field.message) {
          continue;  // validate_description would have flagged it; skip quietly
        }
        if (looks_like_quaternion(*field.message)) {
          --budget;
          Widget quaternion =
            qar_ui_model::widgets::quaternion(id, path, field.display_label());
          if (writable) {
            quaternion.with_editable(true).with_action(ar_actions::kSetField);
          } else {
            quaternion.with_editable(false);
          }
          parent.add(std::move(quaternion));
          continue;
        }
        if (looks_like_vector3(*field.message)) {
          --budget;
          Widget vector = qar_ui_model::widgets::vector3(id, path, field.display_label());
          if (writable) {
            vector.with_editable(true).with_action(ar_actions::kSetField);
          } else {
            vector.with_editable(false);
          }
          parent.add(std::move(vector));
          continue;
        }
        if (depth + 1 >= request->max_depth) {
          --budget;
          parent.add(
            qar_ui_model::widgets::json_inspector(id, path).with_label(field.display_label()));
          continue;
        }
        Widget group = qar_ui_model::widgets::section(id, field.display_label());
        append(group, id, path, *field.message, writable, depth + 1);
        if (!group.children.empty()) {
          parent.add(std::move(group));
        }
        continue;
      }

      --budget;
      parent.add(make_leaf(field, id, path, writable));
    }
  }
};

/// Section title + whether it is composed here or reported by ROS.
struct SectionSpec
{
  const char * key;
  const MessageDescription * description;
  bool writable;
  const char * title;
};

}  // namespace

bool is_writable_document_section(std::string_view key)
{
  return key == ar_doc::kDraft || key == ar_doc::kRequest || key == ar_doc::kGoal;
}

const qar_ui_model::MessageDescription * PanelGenerationRequest::primary() const
{
  switch (entity.kind) {
    case RosEntityKind::Topic:
      return message.get();
    case RosEntityKind::Service:
      return service.request.get();
    case RosEntityKind::Action:
      return action.goal.get();
  }
  return nullptr;
}

bool PanelGenerationRequest::valid(std::string & out_error) const
{
  if (entity.name.empty()) {
    out_error = "entity has no name";
    return false;
  }
  switch (entity.kind) {
    case RosEntityKind::Topic:
      if (!message) {
        out_error = "no message description for topic " + entity.name;
        return false;
      }
      break;
    case RosEntityKind::Service:
      if (!service.request || !service.response) {
        out_error = "service " + entity.name + " has no request/response description";
        return false;
      }
      break;
    case RosEntityKind::Action:
      if (!action.goal) {
        out_error = "action " + entity.name + " has no goal description";
        return false;
      }
      break;
  }
  if (max_depth == 0 || max_fields == 0) {
    out_error = "max_depth and max_fields must be positive";
    return false;
  }
  return true;
}

qar_ui_model::MessageDescriptionPtr make_document_schema(const PanelGenerationRequest & request)
{
  MessageDescriptionBuilder builder(
    std::string("qar_ar_panel/") + to_string(request.entity.kind) + "/" +
    request.entity.type_name);
  builder.description(request.entity.name);

  switch (request.entity.kind) {
    case RosEntityKind::Topic:
      if (request.message) {
        builder.field(FieldDescription::nested(ar_doc::kMessage, request.message));
        if (request.writable) {
          builder.field(FieldDescription::nested(ar_doc::kDraft, request.message));
        }
      }
      break;
    case RosEntityKind::Service:
      if (request.service.request) {
        builder.field(FieldDescription::nested(ar_doc::kRequest, request.service.request));
      }
      if (request.service.response) {
        builder.field(FieldDescription::nested(ar_doc::kResponse, request.service.response));
      }
      break;
    case RosEntityKind::Action:
      if (request.action.goal) {
        builder.field(FieldDescription::nested(ar_doc::kGoal, request.action.goal));
      }
      if (request.action.feedback) {
        builder.field(FieldDescription::nested(ar_doc::kFeedback, request.action.feedback));
      }
      if (request.action.result) {
        builder.field(FieldDescription::nested(ar_doc::kResult, request.action.result));
      }
      // Not a ROS message: the goal's lifecycle as one readable line.
      builder.field(
        FieldDescription::primitive(ar_doc::kStatus, FieldKind::String).with_label("Goal status"));
      break;
  }
  return builder.build();
}

int GenericFormTemplate::match(const PanelGenerationRequest &) const
{
  return 1;  // the floor: any real template outscores it
}

bool GenericFormTemplate::build(
  const PanelGenerationRequest & request,
  const MessageDescription & schema,
  UiModel & out_model,
  std::string & out_error) const
{
  const std::string panel_id = make_panel_id(request.entity.kind, request.entity.name);
  Widget root = qar_ui_model::widgets::panel(panel_id, request.entity.name);
  root.with_description(request.entity.type_name);

  std::vector<SectionSpec> sections;
  switch (request.entity.kind) {
    case RosEntityKind::Topic:
      sections.push_back({ar_doc::kMessage, request.message.get(), false, "Latest message"});
      if (request.writable) {
        sections.push_back({ar_doc::kDraft, request.message.get(), true, "Publish"});
      }
      break;
    case RosEntityKind::Service:
      sections.push_back({ar_doc::kRequest, request.service.request.get(), true, "Request"});
      sections.push_back({ar_doc::kResponse, request.service.response.get(), false, "Response"});
      break;
    case RosEntityKind::Action:
      sections.push_back({ar_doc::kGoal, request.action.goal.get(), true, "Goal"});
      sections.push_back({ar_doc::kFeedback, request.action.feedback.get(), false, "Feedback"});
      sections.push_back({ar_doc::kResult, request.action.result.get(), false, "Result"});
      break;
  }

  if (request.entity.kind == RosEntityKind::Action) {
    root.add(
      qar_ui_model::widgets::value(panel_id + ".status", ar_doc::kStatus, "Goal status"));
  }

  SectionBuilder fields;
  fields.request = &request;
  for (const SectionSpec & spec : sections) {
    if (spec.description == nullptr) {
      continue;
    }
    // Each section gets its own field budget so a fat response cannot starve
    // the request form the operator actually has to fill in.
    fields.budget = request.max_fields;
    fields.truncated = false;

    const std::string section_id = panel_id + "." + spec.key;
    Widget section = qar_ui_model::widgets::section(section_id, spec.title);
    fields.append(section, section_id, spec.key, *spec.description, spec.writable, 0);
    if (fields.truncated) {
      section.add(
        qar_ui_model::widgets::json_inspector(section_id + ".raw", spec.key)
        .with_label("All fields (raw)"));
    }

    // The button that turns the staged section into a ROS operation. `confirm`
    // is on the two that command the robot; cancel is the safe direction and
    // must fire on the first press.
    if (spec.writable) {
      switch (request.entity.kind) {
        case RosEntityKind::Topic:
          section.add(
            qar_ui_model::widgets::button(panel_id + ".publish", "Publish", ar_actions::kPublish)
            .with_confirm(true));
          break;
        case RosEntityKind::Service:
          section.add(
            qar_ui_model::widgets::button(panel_id + ".call", "Call service", ar_actions::kCall));
          break;
        case RosEntityKind::Action:
          section.add(
            qar_ui_model::widgets::button(panel_id + ".send", "Send goal", ar_actions::kSendGoal)
            .with_confirm(true));
          section.add(
            qar_ui_model::widgets::button(
              panel_id + ".cancel", "Cancel goal", ar_actions::kCancelGoal));
          break;
      }
    }
    if (section.children.empty()) {
      continue;
    }
    root.add(std::move(section));
  }

  if (root.children.empty()) {
    out_error = "nothing to show for " + request.entity.name;
    return false;
  }

  out_model = UiModel{};
  out_model.root = std::move(root);
  out_model.initial_data = qar_ui_model::make_default_value(schema);

  UiValue metadata = UiValue::object();
  metadata.set("entity_kind", UiValue(std::string(to_string(request.entity.kind))));
  metadata.set("entity_name", UiValue(request.entity.name));
  metadata.set("type_name", UiValue(request.entity.type_name));
  metadata.set("writable", UiValue(request.writable));
  metadata.set("template", UiValue(name()));
  out_model.metadata = std::move(metadata);
  return true;
}

PanelTemplateRegistry::PanelTemplateRegistry()
{
  templates_.push_back(std::make_shared<GenericFormTemplate>());
}

void PanelTemplateRegistry::add(std::shared_ptr<IPanelTemplate> tmpl)
{
  if (tmpl) {
    templates_.push_back(std::move(tmpl));
  }
}

const IPanelTemplate * PanelTemplateRegistry::select(const PanelGenerationRequest & request) const
{
  const IPanelTemplate * best = nullptr;
  int best_score = 0;
  for (const auto & tmpl : templates_) {
    const int score = tmpl->match(request);
    if (score > best_score) {
      best_score = score;
      best = tmpl.get();
    }
  }
  return best;
}

std::vector<std::string> PanelTemplateRegistry::template_names() const
{
  std::vector<std::string> names;
  names.reserve(templates_.size());
  for (const auto & tmpl : templates_) {
    names.push_back(tmpl->name());
  }
  return names;
}

PanelGenerationResult PanelTemplateRegistry::generate(
  const PanelGenerationRequest & request) const
{
  PanelGenerationResult result;
  if (!request.valid(result.error)) {
    return result;
  }
  const IPanelTemplate * tmpl = select(request);
  if (tmpl == nullptr) {
    result.error = "no panel template applies to " + request.entity.name;
    return result;
  }
  result.template_name = tmpl->name();
  result.document_schema = make_document_schema(request);
  if (!result.document_schema) {
    result.error = "could not build the document schema for " + request.entity.name;
    return result;
  }
  if (!tmpl->build(request, *result.document_schema, result.model, result.error)) {
    if (result.error.empty()) {
      result.error = "template '" + result.template_name + "' produced no model";
    }
    return result;
  }

  // Validated against the schema here rather than at add_panel: a binding that
  // does not resolve is a generator bug, and this names the widget.
  result.problems = qar_ui_model::validate(result.model, result.document_schema.get());
  result.ok = qar_ui_model::is_valid(result.problems);
  if (!result.ok) {
    for (const qar_ui_model::UiProblem & problem : result.problems) {
      if (problem.severity == qar_ui_model::ProblemSeverity::Error) {
        result.error = problem.widget_id.empty()
          ? problem.message
          : "widget '" + problem.widget_id + "': " + problem.message;
        break;
      }
    }
  }
  return result;
}

}  // namespace qar_rviz_streaming
