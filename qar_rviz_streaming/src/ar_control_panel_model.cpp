// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_control_panel_model.hpp"

#include <string>
#include <utility>

#include "qar_ui_model/type_description.hpp"

namespace qar_rviz_streaming
{
namespace
{

namespace widgets = qar_ui_model::widgets;
using qar_ui_model::ChoiceOption;
using qar_ui_model::StatusLevel;
using qar_ui_model::StatusRule;
using qar_ui_model::UiValue;
using qar_ui_model::Widget;

StatusRule connection_rule(const char * word, StatusLevel level, const char * text)
{
  StatusRule rule;
  rule.equals = UiValue(word);
  rule.level = level;
  rule.text = text;
  return rule;
}

/// Writes @p value at @p path of @p document; the paths are the constants of ar_control, which
/// always parse.
void put(UiValue & document, const std::string & path, UiValue value)
{
  qar_ui_model::FieldPath parsed;
  std::string error;
  if (qar_ui_model::FieldPath::parse(path, parsed, error)) {
    qar_ui_model::set_path(document, parsed, std::move(value), error);
  }
}

Widget make_pick_mode_widget()
{
  Widget mode = widgets::choice(
    ar_control::kIdPickMode, ar_control::kPathPickMode, "Pick in the scene",
    {ChoiceOption{"Off", UiValue("off")}, ChoiceOption{"Point", UiValue("point")},
      ChoiceOption{"Nav goal", UiValue("nav_goal")}});
  // The caption of the choice is the sentence that says what the operator can do now.
  mode.with_label_binding(ar_control::kPathPickCaption).with_action(ar_control::kActionPickMode);
  return mode;
}

Widget make_connection_widget()
{
  Widget light = widgets::status_light(
    ar_control::kIdConnection, ar_control::kPathConnection, "QarOS");
  light.with_rule(connection_rule("live", StatusLevel::Ok, "Connected"))
  .with_rule(connection_rule("connecting", StatusLevel::Warning, "Connecting"))
  .with_rule(connection_rule("paused", StatusLevel::Warning, "Paused on the desktop"))
  .with_rule(connection_rule("offline", StatusLevel::Stale, "Not connected"))
  .with_rule(connection_rule("preview", StatusLevel::Neutral, "Offline preview"));
  return light;
}

Widget make_panel_slot_widget(std::size_t slot)
{
  Widget toggle = widgets::toggle(
    ar_control::panel_toggle_id(slot), ar_control::panel_path(slot, "on"),
    "Panel " + std::to_string(slot + 1));
  toggle.with_label_binding(ar_control::panel_path(slot, "title"))
  .with_visible_binding(ar_control::panel_path(slot, "show"))
  .with_action(ar_control::kActionPanelToggle);
  return toggle;
}

Widget make_tool_slot_widget(std::size_t slot)
{
  Widget toggle = widgets::toggle(
    ar_control::tool_toggle_id(slot), ar_control::tool_path(slot, "on"),
    "Tool " + std::to_string(slot + 1));
  toggle.with_label_binding(ar_control::tool_path(slot, "title"))
  .with_visible_binding(ar_control::tool_path(slot, "show"))
  .with_action(ar_control::kActionToolToggle);
  return toggle;
}

}  // namespace

qar_ui_model::UiModel make_ar_control_page_model()
{
  qar_ui_model::UiModel model;
  model.root = widgets::panel(ar_control::kPanelId, ar_control::kPageTitle);

  // The pick block comes first: it is what an operator in AR reaches for, and it has to be on
  // the first screen of the page.
  model.root.add(make_pick_mode_widget());
  model.root.add(widgets::button(
      ar_control::kIdPickConfirm, "Confirm pick", ar_control::kActionPickConfirm));
  model.root.add(widgets::button(
      ar_control::kIdPickCancel, "Cancel pick", ar_control::kActionPickCancel));
  model.root.add(make_connection_widget());

  model.root.add(widgets::label(ar_control::kIdPanelsHeader, "Panels in AR"));
  for (std::size_t slot = 0; slot < ar_control::kPanelSlots; ++slot) {
    model.root.add(make_panel_slot_widget(slot));
  }
  model.root.add(
    widgets::value(ar_control::kIdMorePanels, ar_control::kPathMorePanels, "More panels")
    .with_visible_binding(ar_control::kPathMorePanelsShow));

  model.root.add(widgets::label(ar_control::kIdToolsHeader, "AR tools"));
  for (std::size_t slot = 0; slot < ar_control::kToolSlots; ++slot) {
    model.root.add(make_tool_slot_widget(slot));
  }
  model.root.add(
    widgets::value(ar_control::kIdMoreTools, ar_control::kPathMoreTools, "More tools")
    .with_visible_binding(ar_control::kPathMoreToolsShow));

  model.root.add(
    widgets::value(ar_control::kIdPeers, ar_control::kPathPeers, "Peers").with_precision(0));
  model.root.add(widgets::value(ar_control::kIdError, ar_control::kPathError, "Last error"));

  model.initial_data = make_ar_control_initial_document();
  return model;
}

qar_ui_model::UiValue make_ar_control_initial_document()
{
  UiValue document = UiValue::object();
  put(document, ar_control::kPathConnection, UiValue("offline"));
  put(document, ar_control::kPathPeers, UiValue(0));
  put(document, ar_control::kPathError, UiValue("none"));
  put(document, ar_control::kPathPickMode, UiValue("off"));
  put(document, ar_control::kPathPickCaption, UiValue(ar_control::kCaptionPickOff));
  for (std::size_t slot = 0; slot < ar_control::kPanelSlots; ++slot) {
    put(document, ar_control::panel_path(slot, "show"), UiValue(false));
    put(document, ar_control::panel_path(slot, "title"), UiValue());
    put(document, ar_control::panel_path(slot, "on"), UiValue(false));
  }
  for (std::size_t slot = 0; slot < ar_control::kToolSlots; ++slot) {
    put(document, ar_control::tool_path(slot, "show"), UiValue(false));
    put(document, ar_control::tool_path(slot, "title"), UiValue());
    put(document, ar_control::tool_path(slot, "on"), UiValue(false));
  }
  // The connection reads "offline", so the line that says why there are no switches is shown.
  put(document, ar_control::kPathMorePanels, UiValue(ar_control::kLineNoLiveSession));
  put(document, ar_control::kPathMorePanelsShow, UiValue(true));
  put(document, ar_control::kPathMoreTools, UiValue(""));
  put(document, ar_control::kPathMoreToolsShow, UiValue(false));
  return document;
}

}  // namespace qar_rviz_streaming
