// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The page of the AR control panel: that it is a valid model, that its widgets are the ones the
// router accepts, that its slots are bound to the paths the state fills, and that the pick
// block comes first. Pure C++17: no session, no ROS graph, no Qt.

#include "qar_rviz_streaming/ar_control_panel_model.hpp"

#include <cstdint>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "qar_rviz_streaming/ar_control_panel_router.hpp"
#include "qar_ui_model/html_renderer.hpp"
#include "qar_ui_model/page_metrics.hpp"
#include "qar_ui_model/type_description.hpp"
#include "qar_ui_model/viewport_fit.hpp"

namespace
{

using qar_rviz_streaming::ControlPanelCommandKind;
using qar_rviz_streaming::make_ar_control_initial_document;
using qar_rviz_streaming::make_ar_control_page_model;
using qar_rviz_streaming::route_control_panel_event;
using qar_ui_model::UiModel;
using qar_ui_model::UiValue;
using qar_ui_model::Widget;
using qar_ui_model::WidgetType;
namespace ac = qar_rviz_streaming::ar_control;

const UiValue * document_value(const UiValue & document, const std::string & path)
{
  qar_ui_model::FieldPath parsed;
  std::string error;
  EXPECT_TRUE(qar_ui_model::FieldPath::parse(path, parsed, error)) << path << ": " << error;
  return qar_ui_model::get_path(document, parsed);
}

/// The dotted paths of the values that are not objects.
void collect_leaf_paths(
  const UiValue & value, const std::string & prefix, std::set<std::string> & out)
{
  if (!value.is_object()) {
    out.insert(prefix);
    return;
  }
  for (const qar_ui_model::UiObjectEntry & entry : value.entries()) {
    collect_leaf_paths(entry.value, prefix.empty() ? entry.key : prefix + "." + entry.key, out);
  }
}

/// The widgets of the page in document order, the root excluded.
std::vector<const Widget *> page_widgets(const UiModel & model)
{
  std::vector<const Widget *> widgets;
  for (const Widget & child : model.root.children) {
    widgets.push_back(&child);
  }
  return widgets;
}

std::size_t index_of(const UiModel & model, const std::string & id)
{
  const std::vector<const Widget *> widgets = page_widgets(model);
  for (std::size_t i = 0; i < widgets.size(); ++i) {
    if (widgets[i]->id == id) {
      return i;
    }
  }
  ADD_FAILURE() << "no widget " << id;
  return widgets.size();
}

}  // namespace

TEST(ArControlPageModel, Validates)
{
  const UiModel model = make_ar_control_page_model();
  const std::vector<qar_ui_model::UiProblem> problems = qar_ui_model::validate(model);
  for (const qar_ui_model::UiProblem & problem : problems) {
    ADD_FAILURE() << qar_ui_model::to_string(problem.severity) << " " << problem.widget_id << ": "
                  << problem.message;
  }
  EXPECT_TRUE(problems.empty()) << "neither an error nor a warning";
  EXPECT_TRUE(qar_ui_model::is_valid(problems));

  // The root is the page, with its title and nothing else.
  EXPECT_EQ(model.root.type, WidgetType::Panel);
  EXPECT_EQ(model.root.id, ac::kPanelId);
  EXPECT_EQ(model.root.id, "qar_control");
  EXPECT_EQ(model.root.label, ac::kPageTitle);
  EXPECT_EQ(model.root.label, "QAR control");
  EXPECT_TRUE(model.root.description.empty()) << "no subtitle";

  // A single column of leaves: no section and no row (a row asks for 260 px per child, and the
  // viewport is 500 px wide).
  for (const Widget & child : model.root.children) {
    EXPECT_TRUE(child.children.empty()) << child.id;
    EXPECT_NE(child.type, WidgetType::Section) << child.id;
    EXPECT_NE(child.type, WidgetType::Row) << child.id;
    EXPECT_NE(child.type, WidgetType::Column) << child.id;
  }
  // 3 pick widgets, the topic line, its switches and the "more topics" line, the connection,
  // 2 headings, 16 + 8 slots, 2 "more" lines, the Qt panel size choice, peers and error.
  EXPECT_EQ(
    model.root.children.size(),
    3u + 1u + ac::kPickTopicSlots + 1u + 1u + 2u + ac::kPanelSlots + ac::kToolSlots + 2u + 1u +
    2u);

  // The model survives its own wire form.
  UiModel parsed;
  std::string error;
  ASSERT_TRUE(qar_ui_model::from_value(qar_ui_model::to_value(model), parsed, error)) << error;
  EXPECT_EQ(parsed.root.children.size(), model.root.children.size());
  EXPECT_TRUE(qar_ui_model::is_valid(qar_ui_model::validate(parsed)));
}

TEST(ArControlPageModel, RendersAsAPage)
{
  const UiModel model = make_ar_control_page_model();
  std::string html;
  std::vector<qar_ui_model::UiProblem> problems;
  ASSERT_TRUE(qar_ui_model::render_html_page(
      model, qar_ui_model::HtmlRenderOptions{}, html, problems));
  EXPECT_TRUE(problems.empty());
  EXPECT_NE(html.find("QAR control"), std::string::npos) << "the title";
  for (const Widget & child : model.root.children) {
    EXPECT_NE(html.find("data-qar-id=\"" + child.id + "\""), std::string::npos) << child.id;
  }
  // The page is served with its data, so that it is not blank before the first frame.
  EXPECT_NE(html.find("offline"), std::string::npos);

  // A widget with a visibility binding starts hidden in the markup, and one without is shown.
  for (const Widget & child : model.root.children) {
    const std::string fragment = qar_ui_model::render_widget_fragment(child);
    EXPECT_EQ(fragment.find("style=\"display:none\"") != std::string::npos,
      !child.visible_binding.empty()) << child.id;
  }
}

TEST(ArControlPageModel, IdsAndActionsMatchTheRouter)
{
  const UiModel model = make_ar_control_page_model();
  std::set<std::string> accepted_ids;
  for (const Widget * widget : page_widgets(model)) {
    if (widget->action.empty()) {
      // Something that only shows: the router knows it and has nothing to run.
      const auto routing =
        route_control_panel_event(qar_ui_model::ControlBody{widget->id, "", "", {}});
      EXPECT_FALSE(routing.ok) << widget->id;
      EXPECT_EQ(routing.error, "control '" + widget->id + "' has no action") << widget->id;
      continue;
    }
    // The values that this widget sends: the options of a choice, on and off for a switch, the
    // payload of a button.
    std::vector<UiValue> values;
    if (widget->type == WidgetType::Choice) {
      for (const qar_ui_model::ChoiceOption & option : widget->choices) {
        values.push_back(option.value);
      }
    } else if (widget->type == WidgetType::Toggle) {
      values = {UiValue(true), UiValue(false)};
    } else {
      EXPECT_EQ(widget->type, WidgetType::Button) << widget->id;
      values = {widget->payload};
    }
    ASSERT_FALSE(values.empty()) << widget->id;
    for (const UiValue & value : values) {
      const auto routing = route_control_panel_event(
        qar_ui_model::ControlBody{widget->id, widget->action, widget->binding, value});
      EXPECT_TRUE(routing.ok) << widget->id << ": " << routing.error;
      EXPECT_NE(routing.command.kind, ControlPanelCommandKind::None) << widget->id;
    }
    // ... and nothing else is accepted with another action.
    const auto foreign = route_control_panel_event(
      qar_ui_model::ControlBody{widget->id, "ros.publish", widget->binding, values.front()});
    EXPECT_FALSE(foreign.ok) << widget->id;
    accepted_ids.insert(widget->id);
  }

  // The widgets that send something are exactly the ones the router has a command for.
  std::set<std::string> expected = {
    ac::kIdPickMode, ac::kIdPickConfirm, ac::kIdPickCancel, ac::kIdArCopySize};
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    expected.insert(ac::panel_toggle_id(slot));
  }
  for (std::size_t slot = 0; slot < ac::kToolSlots; ++slot) {
    expected.insert(ac::tool_toggle_id(slot));
  }
  for (std::size_t slot = 0; slot < ac::kPickTopicSlots; ++slot) {
    expected.insert(ac::pick_topic_toggle_id(slot));
  }
  EXPECT_EQ(accepted_ids, expected);

  // The command each one means.
  const auto kind_of = [&](const std::string & id) {
      const Widget * widget = model.find(id);
      EXPECT_NE(widget, nullptr) << id;
      if (widget == nullptr) {
        return ControlPanelCommandKind::None;
      }
      const UiValue value = widget->type == WidgetType::Toggle ? UiValue(true) :
        widget->type == WidgetType::Choice ? widget->choices.front().value : widget->payload;
      return route_control_panel_event(
        qar_ui_model::ControlBody{id, widget->action, widget->binding, value}).command.kind;
    };
  EXPECT_EQ(kind_of(ac::kIdPickMode), ControlPanelCommandKind::SetPickMode);
  EXPECT_EQ(kind_of(ac::kIdPickConfirm), ControlPanelCommandKind::ConfirmPick);
  EXPECT_EQ(kind_of(ac::kIdPickCancel), ControlPanelCommandKind::CancelPick);
  EXPECT_EQ(kind_of("cp_panel_3"), ControlPanelCommandKind::SetPanelOn);
  EXPECT_EQ(kind_of("cp_tool_1"), ControlPanelCommandKind::SetToolOn);
  EXPECT_EQ(kind_of("cp_pick_topic_2"), ControlPanelCommandKind::SetPickTopic);
  EXPECT_EQ(kind_of("cp_ar_copy_size"), ControlPanelCommandKind::SetArCopySize);

  // The page offers exactly the three pick modes of the router, in the words it expects.
  const Widget * mode = model.find(ac::kIdPickMode);
  ASSERT_NE(mode, nullptr);
  ASSERT_EQ(mode->choices.size(), 3u);
  EXPECT_EQ(mode->choices[0].label, "Off");
  EXPECT_EQ(mode->choices[0].value, UiValue("off"));
  EXPECT_EQ(mode->choices[1].label, "Point");
  EXPECT_EQ(mode->choices[1].value, UiValue("point"));
  EXPECT_EQ(mode->choices[2].label, "Nav goal");
  EXPECT_EQ(mode->choices[2].value, UiValue("nav_goal"));
  EXPECT_TRUE(mode->editable);
  EXPECT_EQ(mode->label, "Pick in the scene");
  EXPECT_EQ(model.find(ac::kIdPickConfirm)->label, "Confirm pick");
  EXPECT_EQ(model.find(ac::kIdPickCancel)->label, "Cancel pick");
}

TEST(ArControlPageModel, HasSixteenPanelSwitchesAndEightToolSwitches)
{
  EXPECT_EQ(ac::kPanelSlots, 16u);
  EXPECT_EQ(ac::kToolSlots, 8u);
  const UiModel model = make_ar_control_page_model();
  std::size_t panel_switches = 0;
  std::size_t tool_switches = 0;
  for (const Widget & child : model.root.children) {
    if (child.type != WidgetType::Toggle) {
      continue;
    }
    if (child.id.rfind(ac::kPanelIdPrefix, 0) == 0) {
      ++panel_switches;
    } else if (child.id.rfind(ac::kToolIdPrefix, 0) == 0) {
      ++tool_switches;
    }
  }
  EXPECT_EQ(panel_switches, 16u);
  EXPECT_EQ(tool_switches, 8u);
  EXPECT_NE(model.find("cp_panel_15"), nullptr);
  EXPECT_EQ(model.find("cp_panel_16"), nullptr);
  EXPECT_NE(model.find("cp_tool_7"), nullptr);
  EXPECT_EQ(model.find("cp_tool_8"), nullptr);
  // Every switch is a plain toggle: the page keeps the touch target of the theme for each.
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    EXPECT_EQ(model.find(ac::panel_toggle_id(slot))->type, WidgetType::Toggle) << slot;
  }
}

TEST(ArControlPageModel, SlotsCarryLabelAndVisibleBindings)
{
  const UiModel model = make_ar_control_page_model();

  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    const Widget * toggle = model.find(ac::panel_toggle_id(slot));
    ASSERT_NE(toggle, nullptr) << slot;
    const std::string n = std::to_string(slot);
    EXPECT_EQ(toggle->type, WidgetType::Toggle);
    EXPECT_TRUE(toggle->editable);
    EXPECT_EQ(toggle->label, "Panel " + std::to_string(slot + 1));
    EXPECT_EQ(toggle->action, "cp.panel.toggle");
    EXPECT_EQ(toggle->binding, "panels.s" + n + ".on");
    EXPECT_EQ(toggle->label_binding, "panels.s" + n + ".title");
    EXPECT_EQ(toggle->visible_binding, "panels.s" + n + ".show");
  }
  // A tool row is a switch of the same kind and with the same bindings as a panel row.
  for (std::size_t slot = 0; slot < ac::kToolSlots; ++slot) {
    const Widget * toggle = model.find(ac::tool_toggle_id(slot));
    const Widget * panel_toggle = model.find(ac::panel_toggle_id(slot));
    ASSERT_NE(toggle, nullptr) << slot;
    ASSERT_NE(panel_toggle, nullptr) << slot;
    const std::string n = std::to_string(slot);
    EXPECT_EQ(toggle->type, WidgetType::Toggle);
    EXPECT_EQ(toggle->type, panel_toggle->type);
    EXPECT_TRUE(toggle->editable);
    EXPECT_EQ(toggle->label, "Tool " + std::to_string(slot + 1));
    EXPECT_EQ(toggle->action, "cp.tool.toggle");
    EXPECT_STREQ(ac::kActionToolToggle, "cp.tool.toggle");
    EXPECT_EQ(toggle->binding, "tools.s" + n + ".on");
    EXPECT_EQ(toggle->label_binding, "tools.s" + n + ".title");
    EXPECT_EQ(toggle->visible_binding, "tools.s" + n + ".show");
    EXPECT_EQ(toggle->editable, panel_toggle->editable);
    EXPECT_NE(toggle->action, panel_toggle->action) << "a tool switch is not a panel switch";
    EXPECT_TRUE(toggle->payload.is_null()) << "the value says on or off; no slot number is sent";
  }

  // The lines that count what did not fit.
  const Widget * more_panels = model.find(ac::kIdMorePanels);
  const Widget * more_tools = model.find(ac::kIdMoreTools);
  ASSERT_NE(more_panels, nullptr);
  ASSERT_NE(more_tools, nullptr);
  EXPECT_EQ(more_panels->type, WidgetType::ValueDisplay);
  EXPECT_EQ(more_panels->label, "More panels");
  EXPECT_EQ(more_panels->binding, "more.panels");
  EXPECT_EQ(more_panels->visible_binding, "more.panels_show");
  EXPECT_EQ(more_tools->label, "More tools");
  EXPECT_EQ(more_tools->binding, "more.tools");
  EXPECT_EQ(more_tools->visible_binding, "more.tools_show");

  // The pick mode takes its caption from the document, and the always-shown widgets have no
  // visibility binding.
  EXPECT_EQ(model.find(ac::kIdPickMode)->binding, "pick.mode");
  EXPECT_EQ(model.find(ac::kIdPickMode)->label_binding, "pick.caption");
  for (const char * id : {ac::kIdPickMode, ac::kIdPickConfirm, ac::kIdPickCancel,
      ac::kIdConnection, ac::kIdPanelsHeader, ac::kIdToolsHeader, ac::kIdPeers, ac::kIdError})
  {
    EXPECT_TRUE(model.find(id)->visible_binding.empty()) << id;
  }

  // The state widgets read the paths the document has.
  const Widget * light = model.find(ac::kIdConnection);
  ASSERT_NE(light, nullptr);
  EXPECT_EQ(light->type, WidgetType::StatusLight);
  EXPECT_EQ(light->label, "QarOS");
  EXPECT_EQ(light->binding, "status.connection");
  ASSERT_EQ(light->rules.size(), 5u);
  EXPECT_EQ(light->rules[0].equals, UiValue("live"));
  EXPECT_EQ(light->rules[0].level, qar_ui_model::StatusLevel::Ok);
  EXPECT_EQ(light->rules[0].text, "Connected");
  EXPECT_EQ(light->rules[1].equals, UiValue("connecting"));
  EXPECT_EQ(light->rules[1].level, qar_ui_model::StatusLevel::Warning);
  EXPECT_EQ(light->rules[1].text, "Connecting");
  EXPECT_EQ(light->rules[2].equals, UiValue("paused"));
  EXPECT_EQ(light->rules[2].level, qar_ui_model::StatusLevel::Warning);
  EXPECT_EQ(light->rules[2].text, "Paused on the desktop");
  EXPECT_EQ(light->rules[3].equals, UiValue("offline"));
  EXPECT_EQ(light->rules[3].level, qar_ui_model::StatusLevel::Stale);
  EXPECT_EQ(light->rules[3].text, "Not connected");
  EXPECT_EQ(light->rules[4].equals, UiValue("preview"));
  EXPECT_EQ(light->rules[4].level, qar_ui_model::StatusLevel::Neutral);
  EXPECT_EQ(light->rules[4].text, "Offline preview");
  EXPECT_EQ(model.find(ac::kIdPeers)->binding, "status.peers");
  EXPECT_EQ(model.find(ac::kIdPeers)->label, "Peers");
  EXPECT_EQ(model.find(ac::kIdError)->binding, "status.error");
  EXPECT_EQ(model.find(ac::kIdError)->label, "Last error");
  EXPECT_EQ(model.find(ac::kIdPanelsHeader)->label, "Panels in AR");
  EXPECT_EQ(model.find(ac::kIdToolsHeader)->label, "AR tools");
}

TEST(ArControlPageModel, InitialDocumentHidesEverySlot)
{
  const UiModel model = make_ar_control_page_model();
  const UiValue document = make_ar_control_initial_document();
  EXPECT_EQ(model.initial_data, document) << "the page starts from this document";

  EXPECT_EQ(*document_value(document, "status.connection"), UiValue("offline"));
  EXPECT_EQ(*document_value(document, "status.peers"), UiValue(0));
  EXPECT_EQ(*document_value(document, "status.error"), UiValue("none"));
  EXPECT_EQ(*document_value(document, "pick.mode"), UiValue("off"));
  EXPECT_EQ(*document_value(document, "pick.caption"), UiValue(ac::kCaptionPickOff));
  for (std::size_t slot = 0; slot < ac::kPanelSlots; ++slot) {
    EXPECT_EQ(*document_value(document, ac::panel_path(slot, "show")), UiValue(false)) << slot;
    EXPECT_TRUE(document_value(document, ac::panel_path(slot, "title"))->is_null()) << slot;
    EXPECT_EQ(*document_value(document, ac::panel_path(slot, "on")), UiValue(false)) << slot;
  }
  for (std::size_t slot = 0; slot < ac::kToolSlots; ++slot) {
    EXPECT_EQ(*document_value(document, ac::tool_path(slot, "show")), UiValue(false)) << slot;
    EXPECT_TRUE(document_value(document, ac::tool_path(slot, "title"))->is_null()) << slot;
    EXPECT_EQ(*document_value(document, ac::tool_path(slot, "on")), UiValue(false)) << slot;
  }
  // The page starts offline, so it starts with the line that says why it has no switches.
  EXPECT_EQ(*document_value(document, "more.panels"), UiValue(ac::kLineNoLiveSession));
  EXPECT_EQ(
    *document_value(document, "more.panels"),
    UiValue("AR panels and tools are switched in a live QarOS session"));
  EXPECT_EQ(*document_value(document, "more.panels_show"), UiValue(true));
  EXPECT_EQ(*document_value(document, "more.tools"), UiValue(""));
  EXPECT_EQ(*document_value(document, "more.tools_show"), UiValue(false));

  // Every path that a widget is bound to is in the document, so that no widget starts without
  // a value; and every path of the document is bound by exactly one widget binding, so that
  // nothing is published that the page does not read.
  std::multiset<std::string> bound;
  for (const Widget * widget : page_widgets(model)) {
    for (const std::string * path :
      {&widget->binding, &widget->label_binding, &widget->visible_binding})
    {
      if (path->empty()) {
        continue;
      }
      EXPECT_NE(document_value(document, *path), nullptr) << widget->id << " " << *path;
      bound.insert(*path);
    }
  }
  std::set<std::string> leaves;
  collect_leaf_paths(document, "", leaves);
  // 3 status + 2 pick + 2 topic + 4 x 3 topic switch + 2 more topics + the Qt panel size
  // + 16 x 3 panel + 8 x 3 tool + 4 more paths.
  EXPECT_EQ(leaves.size(), 98u);
  EXPECT_EQ(bound.size(), leaves.size());
  EXPECT_EQ(std::set<std::string>(bound.begin(), bound.end()), leaves);

  // Every slot starts hidden, the line about the missing session starts shown, and the line
  // about the tools starts hidden.
  std::size_t hidden = 0;
  for (const Widget * widget : page_widgets(model)) {
    if (widget->visible_binding.empty()) {
      continue;
    }
    const UiValue * show = document_value(document, widget->visible_binding);
    ASSERT_NE(show, nullptr) << widget->id;
    EXPECT_EQ(*show, UiValue(widget->id == ac::kIdMorePanels)) << widget->id;
    if (widget->id != ac::kIdMorePanels) {
      ++hidden;
    }
  }
  // The slots, the line about the tools, the topic line, its switches and the "more topics" line.
  EXPECT_EQ(hidden, ac::kPanelSlots + ac::kToolSlots + 1u + 1u + ac::kPickTopicSlots + 1u);
  EXPECT_EQ(*document_value(document, "more.topics"), UiValue(""));
  EXPECT_EQ(*document_value(document, "settings.ar_copy_size"), UiValue("enlarged"));
  EXPECT_EQ(*document_value(document, "pick.topic"), UiValue(""));
  for (std::size_t slot = 0; slot < ac::kPickTopicSlots; ++slot) {
    EXPECT_EQ(*document_value(document, ac::pick_topic_path(slot, "show")), UiValue(false));
    EXPECT_TRUE(document_value(document, ac::pick_topic_path(slot, "title"))->is_null());
    EXPECT_EQ(*document_value(document, ac::pick_topic_path(slot, "on")), UiValue(false));
  }
}

TEST(ArControlPageModel, TheTopicBlockFollowsCancel)
{
  const UiModel model = make_ar_control_page_model();
  // Right after Cancel and before the connection light, so that it stays on the first screen.
  const std::size_t cancel = index_of(model, ac::kIdPickCancel);
  EXPECT_EQ(index_of(model, ac::kIdPickTopic), cancel + 1);
  for (std::size_t slot = 0; slot < ac::kPickTopicSlots; ++slot) {
    EXPECT_EQ(index_of(model, ac::pick_topic_toggle_id(slot)), cancel + 2 + slot);
  }
  EXPECT_LT(index_of(model, ac::pick_topic_toggle_id(ac::kPickTopicSlots - 1)),
    index_of(model, ac::kIdConnection));

  const Widget * line = model.find(ac::kIdPickTopic);
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->type, WidgetType::ValueDisplay);
  EXPECT_EQ(line->label, "Publishes to");
  EXPECT_EQ(line->binding, "pick.topic");
  EXPECT_EQ(line->visible_binding, "pick.topic_show");
  EXPECT_TRUE(line->action.empty());

  for (std::size_t slot = 0; slot < ac::kPickTopicSlots; ++slot) {
    const Widget * toggle = model.find(ac::pick_topic_toggle_id(slot));
    ASSERT_NE(toggle, nullptr) << slot;
    EXPECT_EQ(toggle->type, WidgetType::Toggle) << slot;
    EXPECT_EQ(toggle->action, "cp.pick.topic") << slot;
    EXPECT_EQ(toggle->binding, "pick.topics.s" + std::to_string(slot) + ".on") << slot;
    EXPECT_EQ(toggle->label_binding, "pick.topics.s" + std::to_string(slot) + ".title") << slot;
    EXPECT_EQ(toggle->visible_binding, "pick.topics.s" + std::to_string(slot) + ".show") << slot;
  }
  EXPECT_EQ(model.find(ac::pick_topic_toggle_id(ac::kPickTopicSlots)), nullptr);
}

TEST(ArControlPageModel, PickBlockComesFirst)
{
  const UiModel model = make_ar_control_page_model();
  ASSERT_GE(model.root.children.size(), 3u);
  EXPECT_EQ(model.root.children[0].id, ac::kIdPickMode);
  EXPECT_EQ(model.root.children[1].id, ac::kIdPickConfirm);
  EXPECT_EQ(model.root.children[2].id, ac::kIdPickCancel);
  // Then the state of the connection, and only then the lists.
  EXPECT_LT(index_of(model, ac::kIdPickCancel), index_of(model, ac::kIdConnection));
  EXPECT_LT(index_of(model, ac::kIdConnection), index_of(model, ac::kIdPanelsHeader));
  EXPECT_LT(index_of(model, ac::kIdPanelsHeader), index_of(model, ac::panel_toggle_id(0)));
  EXPECT_LT(
    index_of(model, ac::panel_toggle_id(ac::kPanelSlots - 1)), index_of(model, ac::kIdMorePanels));
  EXPECT_LT(index_of(model, ac::kIdMorePanels), index_of(model, ac::kIdToolsHeader));
  EXPECT_LT(index_of(model, ac::kIdToolsHeader), index_of(model, ac::tool_toggle_id(0)));
  EXPECT_LT(
    index_of(model, ac::tool_toggle_id(ac::kToolSlots - 1)), index_of(model, ac::kIdMoreTools));
  EXPECT_LT(index_of(model, ac::kIdMoreTools), index_of(model, ac::kIdPeers));
  EXPECT_LT(index_of(model, ac::kIdPeers), index_of(model, ac::kIdError));
  EXPECT_EQ(model.root.children.back().id, ac::kIdError);

  // What the page asks for, as the layout estimate of qar_ui_model puts it (CSS pixels). The
  // estimate counts every slot as shown, which is the tallest the page gets. The numbers are
  // recorded in the output; the checks only keep them sane.
  const qar_ui_model::ArTheme theme = qar_ui_model::ArTheme::dark();
  const qar_ui_model::PageMetrics all = qar_ui_model::measure_page(model, theme);
  const qar_ui_model::PageMetrics single = qar_ui_model::measure_page(model, theme, 1);
  double pick_block = 0.0;
  for (std::size_t i = 0; i < 3; ++i) {
    pick_block += qar_ui_model::measure_widget_height(model.root.children[i], theme, 1);
  }
  // The page that holds the pick block alone: the block plus the chrome of the page.
  UiModel block_page = model;
  block_page.root.children.resize(3);
  const qar_ui_model::PageMetrics block = qar_ui_model::measure_page(block_page, theme, 1);
  std::cout << "[ INFO ] measure_page: " << all.width_px << " x " << all.height_px
            << " px (up to three columns), " << single.width_px << " x " << single.height_px
            << " px (one column, " << single.widget_count << " widgets); the pick block alone "
            << pick_block << " px of widgets, " << block.height_px << " px as a page\n";
  RecordProperty("page_height_px", static_cast<int>(single.height_px));
  RecordProperty("pick_block_widgets_px", static_cast<int>(pick_block));
  RecordProperty("pick_block_page_px", static_cast<int>(block.height_px));
  EXPECT_GT(pick_block, 0.0);
  EXPECT_GT(block.height_px, pick_block);
  EXPECT_LT(block.height_px, single.height_px);
  EXPECT_EQ(single.columns, 1u);
  EXPECT_EQ(single.widget_count, model.root.children.size());
  EXPECT_EQ(single.widest_row, 0u) << "no row in the page";
}

/// The page reduced to the armed pick block: the mode, Confirm, Cancel, optionally the line that
/// names the topic, the four topic switches and the "more topics" hint (every one of them shown).
UiModel armed_block_page(const UiModel & model, bool with_topic_line)
{
  UiModel block = model;
  block.root.children.clear();
  std::vector<std::string> ids = {ac::kIdPickMode, ac::kIdPickConfirm, ac::kIdPickCancel};
  if (with_topic_line) {
    ids.push_back(ac::kIdPickTopic);
  }
  for (std::size_t slot = 0; slot < ac::kPickTopicSlots; ++slot) {
    ids.push_back(ac::pick_topic_toggle_id(slot));
  }
  ids.push_back(ac::kIdMoreTopics);
  for (const std::string & id : ids) {
    block.root.children.push_back(*model.find(id));
  }
  return block;
}

/// Where the armed block ends, in CSS pixels from the top of the page, for a viewport @p width
/// wide, fitted as the page's script fits it. @p caption_line receives the height of one line of
/// caption text at that fit.
double armed_block_bottom_px(
  const UiModel & model, bool with_topic_line, double width, double * caption_line = nullptr)
{
  const qar_ui_model::ViewportFit fit =
    qar_ui_model::fit_page_to_viewport(model, qar_ui_model::ArTheme::dark(), width);
  const UiModel block = armed_block_page(model, with_topic_line);
  // measure_page counts the panel padding on both sides; the bottom edge has only the top one.
  const double bottom =
    qar_ui_model::measure_page(block, fit.theme, 1).height_px - fit.theme.padding_px;
  if (caption_line != nullptr) {
    *caption_line = fit.theme.small_font_px * qar_ui_model::kPageLineHeight;
  }
  return bottom;
}

TEST(ArControlPageModel, TheArmedPickBlockFitsTheFirstScreen)
{
  const UiModel model = make_ar_control_page_model();
  struct Viewport
  {
    double width;
    double height;
  };
  // The design viewport, and the one that the logs show once the panel has been resized.
  for (const Viewport viewport : {Viewport{500.0, 500.0}, Viewport{296.0, 282.0}}) {
    double caption_line = 0.0;
    const double bottom = armed_block_bottom_px(model, false, viewport.width, &caption_line);
    std::cout << "[ INFO ] armed pick block " << viewport.width << " x " << viewport.height
              << ": ends at " << bottom << " px, one wrapped caption line is " << caption_line
              << " px\n";
    // For the record: the same block when the line that names a topic outside the switches is
    // shown as well (a typed topic); it is the rarer case and not pinned.
    std::cout << "[ INFO ]   with the topic line: "
              << armed_block_bottom_px(model, true, viewport.width) << " px\n";
    RecordProperty(
      "block_bottom_px_" + std::to_string(static_cast<int>(viewport.width)),
      static_cast<int>(bottom));
    EXPECT_GT(bottom, 0.0);
    EXPECT_LE(bottom, viewport.height - caption_line)
      << "mode, Confirm, Cancel, four topic switches and the hint end inside the first screen, "
      << "with room for one wrapped caption line, at " << viewport.width << " px";
  }
}

TEST(ArControlPageModel, TheTopicSwitchesAndTheHintAreCompact)
{
  const UiModel model = make_ar_control_page_model();
  for (std::size_t slot = 0; slot < ac::kPickTopicSlots; ++slot) {
    const Widget * toggle = model.find(ac::pick_topic_toggle_id(slot));
    ASSERT_NE(toggle, nullptr) << slot;
    EXPECT_TRUE(toggle->compact) << "the topic name stands inside the switch: " << slot;
  }
  const Widget * hint = model.find(ac::kIdMoreTopics);
  ASSERT_NE(hint, nullptr);
  EXPECT_TRUE(hint->compact) << "one line, not a readout of its own";
  EXPECT_EQ(hint->label, "More topics on the desktop:");
  // The line that names a topic outside the switches and every other toggle keep their layout.
  EXPECT_FALSE(model.find(ac::kIdPickTopic)->compact);
  EXPECT_FALSE(model.find(ac::panel_toggle_id(0))->compact);
  EXPECT_FALSE(model.find(ac::tool_toggle_id(0))->compact);

  // A switch is one touch target high, a plain one a caption row more.
  const qar_ui_model::ArTheme theme = qar_ui_model::ArTheme::dark();
  EXPECT_NEAR(
    qar_ui_model::measure_widget_height(*model.find(ac::pick_topic_toggle_id(0)), theme, 1),
    theme.touch_target_px, 1e-9);
  EXPECT_GE(theme.touch_target_px, 64);
  EXPECT_GT(
    qar_ui_model::measure_widget_height(*model.find(ac::panel_toggle_id(0)), theme, 1),
    theme.touch_target_px);
}

TEST(ArControlPageModel, TheMoreTopicsLineFollowsTheTopicSwitches)
{
  const UiModel model = make_ar_control_page_model();
  // After the last topic switch and before the connection light: the pick block stays together on
  // the first screen.
  const std::size_t last = index_of(model, ac::pick_topic_toggle_id(ac::kPickTopicSlots - 1));
  EXPECT_EQ(index_of(model, ac::kIdMoreTopics), last + 1);
  EXPECT_LT(index_of(model, ac::kIdMoreTopics), index_of(model, ac::kIdConnection));
  EXPECT_STREQ(ac::kIdMoreTopics, "cp_more_topics");

  const Widget * line = model.find(ac::kIdMoreTopics);
  ASSERT_NE(line, nullptr);
  EXPECT_EQ(line->type, WidgetType::ValueDisplay);
  EXPECT_EQ(line->label, "More topics on the desktop:");
  EXPECT_EQ(line->binding, "more.topics");
  EXPECT_EQ(line->visible_binding, "more.topics_show");
  EXPECT_TRUE(line->action.empty()) << "it only shows";
}

TEST(ArControlPageModel, TheQtPanelSizeChoiceStandsBelowThePanelSwitches)
{
  const UiModel model = make_ar_control_page_model();
  const Widget * choice = model.find(ac::kIdArCopySize);
  ASSERT_NE(choice, nullptr);
  EXPECT_STREQ(ac::kIdArCopySize, "cp_ar_copy_size");
  EXPECT_EQ(choice->type, WidgetType::Choice);
  EXPECT_TRUE(choice->editable);
  EXPECT_EQ(choice->label, "Qt panels");
  EXPECT_EQ(choice->binding, "settings.ar_copy_size");
  EXPECT_EQ(choice->action, "cp.ar_copy.size");
  EXPECT_STREQ(ac::kActionArCopySize, "cp.ar_copy.size");
  EXPECT_TRUE(choice->visible_binding.empty()) << "the setting can be chosen at any time";
  ASSERT_EQ(choice->choices.size(), 2u);
  EXPECT_EQ(choice->choices[0].label, "Enlarged");
  EXPECT_EQ(choice->choices[0].value, UiValue("enlarged"));
  EXPECT_EQ(choice->choices[1].label, "Native");
  EXPECT_EQ(choice->choices[1].value, UiValue("native"));

  // Below the panel switches and their "more" line, above the AR tools.
  EXPECT_LT(
    index_of(model, ac::panel_toggle_id(ac::kPanelSlots - 1)), index_of(model, ac::kIdArCopySize));
  EXPECT_LT(index_of(model, ac::kIdMorePanels), index_of(model, ac::kIdArCopySize));
  EXPECT_LT(index_of(model, ac::kIdArCopySize), index_of(model, ac::kIdToolsHeader));
  // The pick block stays first.
  EXPECT_LT(index_of(model, ac::kIdMoreTopics), index_of(model, ac::kIdConnection));
  EXPECT_LT(index_of(model, ac::kIdConnection), index_of(model, ac::kIdArCopySize));
}
