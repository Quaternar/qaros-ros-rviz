// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the list of AR panels (ar_panel_list_widget.hpp), offscreen: eight rows and no scroll
// bars, a size that no text or number of rows changes, the selection kept by key, the pages and
// the fixed widths of the buttons.

#include "qar_rviz_streaming/ar_panel_list_widget.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace
{

using qar_rviz_streaming::ArPanelAction;
using qar_rviz_streaming::ArPanelListInput;
using qar_rviz_streaming::ArPanelListWidget;
using qar_rviz_streaming::ArPanelRow;
using qar_rviz_streaming::kArPanelListRows;
using qar_rviz_streaming::make_ar_panel_rows;

/// @p count streamed RViz panels called "<prefix> <n>".
std::vector<ArPanelRow> make_rows(int count, const std::string & prefix = "Panel")
{
  std::vector<ArPanelListInput> inputs;
  for (int i = 1; i <= count; ++i) {
    ArPanelListInput input;
    input.key = "dock:" + prefix + " " + std::to_string(i);
    input.title = prefix + " " + std::to_string(i);
    input.kind = qar_rviz_streaming::ArPanelKind::RvizPanel;
    input.in_manager = i % 2 == 1;
    input.frames_sent = true;
    input.scaled_kind = true;
    input.scale = 2.0;
    inputs.push_back(input);
  }
  return make_ar_panel_rows(inputs, true);
}

void show(ArPanelListWidget & list)
{
  list.resize(480, 400);
  list.show();
  QApplication::processEvents();
}

}  // namespace

TEST(ArPanelListWidget, HasEightRowsAndNoScrollBars)
{
  ArPanelListWidget list;
  show(list);
  QTreeWidget * tree = list.tree();
  EXPECT_EQ(tree->minimumHeight(), tree->maximumHeight());
  EXPECT_EQ(tree->verticalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  EXPECT_EQ(tree->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  const int height = tree->height();

  list.set_rows(make_rows(20));
  QApplication::processEvents();
  EXPECT_EQ(tree->topLevelItemCount(), kArPanelListRows);
  EXPECT_EQ(tree->height(), height);
  EXPECT_EQ(tree->verticalScrollBar()->maximum(), 0);
  EXPECT_EQ(tree->horizontalScrollBar()->maximum(), 0);
  EXPECT_FALSE(tree->verticalScrollBar()->isVisible());
  EXPECT_FALSE(tree->horizontalScrollBar()->isVisible());

  // Eight rows and the header fit the list: the last row of the page is fully inside the viewport.
  const QRect last = tree->visualItemRect(tree->topLevelItem(kArPanelListRows - 1));
  EXPECT_GE(last.top(), 0);
  EXPECT_LE(last.bottom(), tree->viewport()->height());

  EXPECT_EQ(tree->headerItem()->text(0), "Panel");
  EXPECT_EQ(tree->headerItem()->text(1), "Kind");
  EXPECT_EQ(tree->headerItem()->text(2), "Seen by");
  EXPECT_EQ(tree->headerItem()->text(3), "In AR");
  EXPECT_EQ(tree->headerItem()->text(4), "Size");
  EXPECT_FALSE(tree->rootIsDecorated());
}

TEST(ArPanelListWidget, TheSizeHintDoesNotChangeWithTheTexts)
{
  ArPanelListWidget list;
  const QSize empty_hint = list.sizeHint();
  const QSize empty_minimum = list.minimumSizeHint();

  const std::string long_prefix(120, 'x');
  list.set_rows(make_rows(12, long_prefix));
  EXPECT_EQ(list.sizeHint(), empty_hint);
  EXPECT_EQ(list.minimumSizeHint(), empty_minimum);
  show(list);
  const QSize shown_hint = list.sizeHint();
  const QSize shown_minimum = list.minimumSizeHint();
  EXPECT_EQ(shown_hint, empty_hint);
  EXPECT_EQ(shown_minimum, empty_minimum);

  list.set_rows(make_rows(1));
  QApplication::processEvents();
  EXPECT_EQ(list.sizeHint(), shown_hint);
  EXPECT_EQ(list.minimumSizeHint(), shown_minimum);
  EXPECT_EQ(list.height(), 400);

  list.set_rows({});
  QApplication::processEvents();
  EXPECT_EQ(list.sizeHint(), shown_hint);
  EXPECT_EQ(list.minimumSizeHint(), shown_minimum);
  EXPECT_EQ(list.selected_key(), QString());
}

TEST(ArPanelListWidget, TheSelectionIsKeptByKey)
{
  ArPanelListWidget list;
  show(list);
  std::vector<ArPanelRow> rows = make_rows(3);
  list.set_rows(rows);
  // The first row is selected until the person chooses one.
  EXPECT_EQ(list.selected_key(), "dock:Panel 1");

  list.select_key("dock:Panel 2");
  EXPECT_EQ(list.selected_key(), "dock:Panel 2");

  // The rows come in another order: the same entry stays selected.
  std::vector<ArPanelRow> shuffled = {rows[2], rows[1], rows[0]};
  list.set_rows(shuffled);
  EXPECT_EQ(list.selected_key(), "dock:Panel 2");
  ASSERT_EQ(list.tree()->selectedItems().size(), 1);
  EXPECT_EQ(list.tree()->selectedItems().front()->data(0, Qt::UserRole).toString(),
    "dock:Panel 2");
  EXPECT_EQ(list.tree()->selectedItems().front(), list.tree()->topLevelItem(1));

  // The person's click selects.
  list.tree()->setCurrentItem(list.tree()->topLevelItem(2));
  EXPECT_EQ(list.selected_key(), "dock:Panel 1");

  // The selected entry is gone: the first row is selected.
  list.set_rows({rows[1], rows[2]});
  EXPECT_EQ(list.selected_key(), "dock:Panel 2");

  // A key that is not listed changes nothing.
  list.select_key("dock:nothing");
  EXPECT_EQ(list.selected_key(), "dock:Panel 2");
}

TEST(ArPanelListWidget, RowsAreUpdatedInPlace)
{
  ArPanelListWidget list;
  show(list);
  std::vector<ArPanelRow> rows = make_rows(4);
  list.set_rows(rows);
  QTreeWidgetItem * first = list.tree()->topLevelItem(0);

  int changed = 0;
  QObject::connect(
    list.tree()->model(), &QAbstractItemModel::dataChanged, &list, [&changed]() {++changed;});
  list.set_rows(rows);
  EXPECT_EQ(changed, 0) << "an unchanged row is not written";
  EXPECT_EQ(list.tree()->topLevelItem(0), first);

  rows[0].cells[3] = "hidden";
  list.set_rows(rows);
  EXPECT_EQ(list.tree()->topLevelItem(0), first);
  EXPECT_EQ(first->text(3), "hidden");
  EXPECT_GT(changed, 0);
}

TEST(ArPanelListWidget, PagingShowsTheRightRows)
{
  ArPanelListWidget list;
  show(list);

  // Up to eight rows: no pager buttons, no label, but the row is kept (the room stays).
  list.set_rows(make_rows(5));
  EXPECT_FALSE(list.previous_button()->isVisibleTo(&list));
  EXPECT_FALSE(list.next_button()->isVisibleTo(&list));
  EXPECT_TRUE(list.refresh_button()->isVisibleTo(&list));
  EXPECT_TRUE(list.page_label()->text().isEmpty());
  EXPECT_TRUE(list.previous_button()->sizePolicy().retainSizeWhenHidden());
  EXPECT_TRUE(list.next_button()->sizePolicy().retainSizeWhenHidden());

  list.set_rows(make_rows(12));
  QApplication::processEvents();
  EXPECT_TRUE(list.previous_button()->isVisibleTo(&list));
  EXPECT_TRUE(list.next_button()->isVisibleTo(&list));
  EXPECT_FALSE(list.previous_button()->isEnabled());
  EXPECT_TRUE(list.next_button()->isEnabled());
  EXPECT_EQ(list.page_label()->text(), "Rows 1-8 of 12");
  EXPECT_EQ(list.tree()->topLevelItemCount(), 8);
  EXPECT_EQ(list.tree()->topLevelItem(0)->text(0), "Panel 1");
  EXPECT_EQ(list.tree()->topLevelItem(7)->text(0), "Panel 8");

  list.next_button()->click();
  EXPECT_EQ(list.page_label()->text(), "Rows 9-12 of 12");
  EXPECT_EQ(list.tree()->topLevelItemCount(), 4);
  EXPECT_EQ(list.tree()->topLevelItem(0)->text(0), "Panel 9");
  EXPECT_EQ(list.tree()->topLevelItem(3)->text(0), "Panel 12");
  EXPECT_TRUE(list.previous_button()->isEnabled());
  EXPECT_FALSE(list.next_button()->isEnabled());
  // The buttons act on a row that is on the page.
  EXPECT_EQ(list.selected_key(), "dock:Panel 9");

  // The page stays while rows come and go, and falls back to the last one that exists.
  list.set_rows(make_rows(14));
  EXPECT_EQ(list.page_label()->text(), "Rows 9-14 of 14");
  list.set_rows(make_rows(6));
  EXPECT_TRUE(list.page_label()->text().isEmpty());
  EXPECT_EQ(list.tree()->topLevelItemCount(), 6);
  EXPECT_EQ(list.tree()->topLevelItem(0)->text(0), "Panel 1");

  list.set_rows(make_rows(12));
  list.next_button()->click();
  list.previous_button()->click();
  EXPECT_EQ(list.page_label()->text(), "Rows 1-8 of 12");

  // select_key moves to the page of the row.
  list.select_key("dock:Panel 11");
  EXPECT_EQ(list.page_label()->text(), "Rows 9-12 of 12");
  EXPECT_EQ(list.selected_key(), "dock:Panel 11");
}

TEST(ArPanelListWidget, ButtonsKeepTheirWidth)
{
  ArPanelListWidget list;
  show(list);
  QPushButton * buttons[] = {
    list.primary_button(), list.size_button(), list.reopen_button(), list.previous_button(),
    list.next_button(), list.refresh_button()};
  int widths[6];
  for (int i = 0; i < 6; ++i) {
    widths[i] = buttons[i]->width();
    EXPECT_EQ(buttons[i]->minimumWidth(), buttons[i]->maximumWidth()) << i;
  }

  // Every state of the primary and the size button.
  std::vector<ArPanelListInput> inputs(4);
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    inputs[i].key = "dock:P" + std::to_string(i);
    inputs[i].title = inputs[i].key;
    inputs[i].scaled_kind = true;
    inputs[i].scale = 2.0;
  }
  inputs[1].in_manager = true;
  inputs[1].frames_sent = true;
  inputs[2].in_manager = true;
  inputs[2].ar_hidden = true;
  inputs[3].in_manager = true;
  inputs[3].size_native = true;
  inputs[3].scale = 1.0;
  for (const bool hide_mode : {true, false}) {
    list.set_rows(make_ar_panel_rows(inputs, hide_mode));
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      list.select_key(QString::fromStdString(inputs[i].key));
      QApplication::processEvents();
      for (int b = 0; b < 6; ++b) {
        EXPECT_EQ(buttons[b]->width(), widths[b]) << i << " " << b;
      }
      const QFontMetrics metrics(list.primary_button()->font());
      EXPECT_LE(metrics.horizontalAdvance(list.primary_button()->text()),
        list.primary_button()->width());
      EXPECT_LE(metrics.horizontalAdvance(list.size_button()->text()),
        list.size_button()->width());
    }
  }

  // The texts follow the row.
  list.set_rows(make_ar_panel_rows(inputs, true));
  list.select_key("dock:P0");
  EXPECT_EQ(list.primary_button()->text(), "Stream to AR");
  EXPECT_EQ(list.size_button()->text(), "Native size in AR");
  list.select_key("dock:P1");
  EXPECT_EQ(list.primary_button()->text(), "Hide in AR");
  list.select_key("dock:P2");
  EXPECT_EQ(list.primary_button()->text(), "Show in AR");
  list.select_key("dock:P3");
  EXPECT_EQ(list.size_button()->text(), "Enlarged in AR");
  list.set_rows(make_ar_panel_rows(inputs, false));
  list.select_key("dock:P1");
  EXPECT_EQ(list.primary_button()->text(), "Stop streaming");
}

TEST(ArPanelListWidget, TheButtonsRequestTheirActions)
{
  ArPanelListWidget list;
  show(list);
  std::vector<ArPanelListInput> inputs(3);
  inputs[0].key = "dock:Off";
  inputs[1].key = "dock:On";
  inputs[1].in_manager = true;
  inputs[1].frames_sent = true;
  inputs[1].scaled_kind = true;
  inputs[1].scale = 2.0;
  inputs[2].key = "control:everybody";
  inputs[2].kind = qar_rviz_streaming::ArPanelKind::ArControl;
  inputs[2].in_manager = true;
  inputs[2].frames_sent = true;
  for (auto & input : inputs) {
    input.title = input.key;
  }
  list.set_rows(make_ar_panel_rows(inputs, true));

  QStringList primary;
  QStringList size;
  QStringList reopen;
  int refresh = 0;
  QObject::connect(
    &list, &ArPanelListWidget::primaryRequested, &list, [&](const QString & key) {primary << key;});
  QObject::connect(
    &list, &ArPanelListWidget::sizeRequested, &list, [&](const QString & key) {size << key;});
  QObject::connect(
    &list, &ArPanelListWidget::reopenRequested, &list, [&](const QString & key) {reopen << key;});
  QObject::connect(
    &list, &ArPanelListWidget::refreshRequested, &list, [&]() {++refresh;});

  // Streaming needs a session; hiding does not.
  list.select_key("dock:Off");
  EXPECT_FALSE(list.primary_button()->isEnabled());
  list.set_stream_allowed(true);
  EXPECT_TRUE(list.primary_button()->isEnabled());
  list.primary_button()->click();
  ASSERT_EQ(primary.size(), 1);
  EXPECT_EQ(primary.at(0), "dock:Off");
  EXPECT_FALSE(list.size_button()->isEnabled());
  EXPECT_FALSE(list.reopen_button()->isEnabled());

  list.set_stream_allowed(false);
  list.select_key("dock:On");
  EXPECT_TRUE(list.primary_button()->isEnabled());
  EXPECT_TRUE(list.size_button()->isEnabled());
  list.size_button()->click();
  ASSERT_EQ(size.size(), 1);
  EXPECT_EQ(size.at(0), "dock:On");

  // The AR control row is re-opened, not streamed.
  list.select_key("control:everybody");
  EXPECT_FALSE(list.primary_button()->isEnabled());
  EXPECT_TRUE(list.reopen_button()->isEnabled());
  list.reopen_button()->click();
  ASSERT_EQ(reopen.size(), 1);
  EXPECT_EQ(reopen.at(0), "control:everybody");

  list.refresh_button()->click();
  EXPECT_EQ(refresh, 1);
  EXPECT_EQ(primary.size(), 1);
}

namespace
{

/// An AR tool row of the kind the host builds: its QarOS panel is made when @p made.
ArPanelListInput tool_input(const std::string & name, bool made, bool hidden = false)
{
  ArPanelListInput input;
  input.key = "tool:" + name;
  input.title = name;
  input.kind = qar_rviz_streaming::ArPanelKind::ArTool;
  input.in_manager = true;
  input.frames_sent = made;
  input.ar_hidden = made && hidden;
  return input;
}

}  // namespace

TEST(ArPanelListWidget, ToolRowsDoNotChangeTheSizeHint)
{
  ArPanelListWidget list;
  const QSize empty_hint = list.sizeHint();
  const QSize empty_minimum = list.minimumSizeHint();
  show(list);
  const int tree_height = list.tree()->height();
  const int width = list.width();

  // Panels, then one to ten tools with long names: the hints, the height of the list and the
  // widths of the buttons stay.
  QPushButton * buttons[] = {
    list.primary_button(), list.size_button(), list.reopen_button(), list.previous_button(),
    list.next_button(), list.refresh_button()};
  int widths[6];
  for (int i = 0; i < 6; ++i) {
    widths[i] = buttons[i]->width();
  }
  for (const int tools : {1, 4, 10}) {
    std::vector<ArPanelListInput> inputs;
    for (int i = 0; i < 3; ++i) {
      ArPanelListInput panel;
      panel.key = "dock:P" + std::to_string(i);
      panel.title = panel.key;
      inputs.push_back(panel);
    }
    for (int i = 0; i < tools; ++i) {
      inputs.push_back(tool_input("/" + std::string(80, 'x') + std::to_string(i), i % 2 == 0));
    }
    list.set_rows(make_ar_panel_rows(inputs, true));
    QApplication::processEvents();
    EXPECT_EQ(list.sizeHint(), empty_hint) << tools;
    EXPECT_EQ(list.minimumSizeHint(), empty_minimum) << tools;
    EXPECT_EQ(list.tree()->height(), tree_height) << tools;
    EXPECT_EQ(list.width(), width) << tools;
    EXPECT_EQ(list.tree()->topLevelItemCount(), std::min(3 + tools, kArPanelListRows));
    for (int b = 0; b < 6; ++b) {
      EXPECT_EQ(buttons[b]->width(), widths[b]) << tools << " " << b;
    }
    // Selecting every tool row never moves the buttons either.
    for (int i = 0; i < tools; ++i) {
      list.select_key(QString::fromStdString(inputs[static_cast<std::size_t>(3 + i)].key));
      QApplication::processEvents();
      for (int b = 0; b < 6; ++b) {
        EXPECT_EQ(buttons[b]->width(), widths[b]) << tools << " " << i << " " << b;
      }
      EXPECT_EQ(list.sizeHint(), empty_hint);
    }
  }
}

TEST(ArPanelListWidget, ToolRowButtonsRequestTheirActions)
{
  ArPanelListWidget list;
  show(list);
  list.set_stream_allowed(true);
  list.set_rows(
    make_ar_panel_rows(
      {tool_input("/shown", true), tool_input("/hidden", true, true),
        tool_input("/pending", false)}, true));

  QStringList primary;
  QStringList reopen;
  QObject::connect(
    &list, &ArPanelListWidget::primaryRequested, &list, [&](const QString & key) {primary << key;});
  QObject::connect(
    &list, &ArPanelListWidget::reopenRequested, &list, [&](const QString & key) {reopen << key;});

  list.select_key("tool:/shown");
  EXPECT_EQ(list.primary_button()->text(), "Hide in AR");
  EXPECT_TRUE(list.primary_button()->isEnabled());
  EXPECT_TRUE(list.reopen_button()->isEnabled());
  EXPECT_FALSE(list.size_button()->isEnabled());
  list.primary_button()->click();
  list.reopen_button()->click();

  list.select_key("tool:/hidden");
  EXPECT_EQ(list.primary_button()->text(), "Show in AR");
  list.primary_button()->click();

  // A tool whose QarOS panel is not made yet has nothing to hide or show; it can be re-opened.
  list.select_key("tool:/pending");
  EXPECT_FALSE(list.primary_button()->isEnabled());
  EXPECT_TRUE(list.reopen_button()->isEnabled());
  list.primary_button()->click();

  EXPECT_EQ(primary, QStringList({"tool:/shown", "tool:/hidden"}));
  EXPECT_EQ(reopen, QStringList({"tool:/shown"}));
}
