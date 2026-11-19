// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Tests of the list of the docks of a main window (dock_census.hpp), offscreen. A QMainWindow with
// real QDockWidgets stands in for RViz's window: the census must find the docks wherever they
// are (docked, tabbed, floating) and say which of them are open.

#include "qar_rviz_streaming/dock_census.hpp"

#include <map>
#include <vector>

#include <gtest/gtest.h>

#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QString>

namespace
{

using qar_rviz_streaming::census_docks;
using qar_rviz_streaming::census_signature;
using qar_rviz_streaming::DockEntry;
using qar_rviz_streaming::host_main_window;

/// A main window with a central widget, shown offscreen so that the docks have a visible state.
class DockCensusWindow : public QMainWindow
{
public:
  DockCensusWindow()
  {
    setCentralWidget(new QLabel("scene", this));
    resize(900, 600);
  }

  /// A dock with a label as its content, docked on the left.
  QDockWidget * add_dock(const QString & title, const QString & object_name = QString())
  {
    auto * dock = new QDockWidget(title, this);
    dock->setObjectName(object_name);
    dock->setWidget(new QLabel(title + " content", dock));
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    return dock;
  }

  void settle()
  {
    show();
    QApplication::processEvents();
  }
};

std::vector<QString> keys_of(const std::vector<DockEntry> & docks)
{
  std::vector<QString> keys;
  for (const DockEntry & dock : docks) {
    keys.push_back(dock.key);
  }
  return keys;
}

const DockEntry * find_key(const std::vector<DockEntry> & docks, const QString & key)
{
  for (const DockEntry & dock : docks) {
    if (dock.key == key) {
      return &dock;
    }
  }
  return nullptr;
}

}  // namespace

TEST(DockCensus, ListsEveryDockOfTheMainWindowInOrder)
{
  DockCensusWindow window;
  QDockWidget * displays = window.add_dock("Displays");
  window.add_dock("Views");
  window.add_dock("Time");
  window.settle();

  const std::vector<DockEntry> docks = census_docks(window, nullptr);
  ASSERT_EQ(docks.size(), 3u);
  EXPECT_EQ(docks[0].key, "dock:Displays");
  EXPECT_EQ(docks[1].key, "dock:Views");
  EXPECT_EQ(docks[2].key, "dock:Time");
  EXPECT_EQ(docks[0].title, "Displays");
  EXPECT_EQ(docks[0].content.data(), displays->widget());
  for (const DockEntry & dock : docks) {
    EXPECT_TRUE(dock.open) << dock.key.toStdString();
  }

  // A dock whose title is empty is named by its object name; one without either is not listed.
  window.add_dock(QString(), "by_object_name");
  window.add_dock(QString());
  const std::vector<DockEntry> more = census_docks(window, nullptr);
  ASSERT_EQ(more.size(), 4u);
  EXPECT_EQ(more[3].key, "dock:by_object_name");
  EXPECT_EQ(more[3].title, "by_object_name");
}

TEST(DockCensus, FindsTheDocksWhileThisPanelFloats)
{
  DockCensusWindow window;
  QDockWidget * self_dock = window.add_dock("Qar Streaming");
  window.add_dock("Views");
  window.add_dock("Time");
  window.settle();
  QWidget * self = self_dock->widget();

  const auto docked = census_docks(*host_main_window(self), self);
  EXPECT_EQ(keys_of(docked), (std::vector<QString>{"dock:Views", "dock:Time"}));

  // A floating dock is a window of its own: the window of its content is the dock, not the main
  // window, so the main window has to be found through the parent chain.
  self_dock->setFloating(true);
  QApplication::processEvents();
  ASSERT_TRUE(self_dock->isFloating());
  EXPECT_NE(self->window(), &window) << "the premise: window() does not reach the main window";
  QMainWindow * main = host_main_window(self);
  ASSERT_EQ(main, &window);
  const auto floating = census_docks(*main, self);
  EXPECT_EQ(keys_of(floating), (std::vector<QString>{"dock:Views", "dock:Time"}));

  // The same for the content of a docked dock, and for the main window itself.
  EXPECT_EQ(host_main_window(window.centralWidget()), &window);
  EXPECT_EQ(host_main_window(&window), &window);

  // A widget outside every main window has none.
  QLabel loose("loose");
  EXPECT_EQ(host_main_window(&loose), nullptr);
  EXPECT_EQ(host_main_window(nullptr), nullptr);
}

TEST(DockCensus, AClosedDockIsNotOpen)
{
  DockCensusWindow window;
  window.add_dock("Displays");
  QDockWidget * views = window.add_dock("Views");
  window.settle();
  ASSERT_TRUE(find_key(census_docks(window, nullptr), "dock:Views")->open);

  views->close();
  QApplication::processEvents();
  const auto closed = census_docks(window, nullptr);
  ASSERT_EQ(closed.size(), 2u) << "a closed dock is still a child of the window";
  EXPECT_FALSE(find_key(closed, "dock:Views")->open);
  EXPECT_TRUE(find_key(closed, "dock:Displays")->open);

  views->show();
  QApplication::processEvents();
  EXPECT_TRUE(find_key(census_docks(window, nullptr), "dock:Views")->open);
}

TEST(DockCensus, ADockTabbedBehindAnotherStaysOpen)
{
  DockCensusWindow window;
  QDockWidget * first = window.add_dock("First");
  QDockWidget * second = window.add_dock("Second");
  QDockWidget * third = window.add_dock("Third");
  window.tabifyDockWidget(first, second);
  window.tabifyDockWidget(second, third);
  window.settle();

  const auto docks = census_docks(window, nullptr);
  ASSERT_EQ(docks.size(), 3u);
  for (const DockEntry & dock : docks) {
    EXPECT_TRUE(dock.open) << dock.key.toStdString();
  }

  // Closing one of the tabbed docks is seen as a close; the others stay open.
  second->close();
  QApplication::processEvents();
  const auto after = census_docks(window, nullptr);
  EXPECT_FALSE(find_key(after, "dock:Second")->open);
  EXPECT_TRUE(find_key(after, "dock:First")->open);
  EXPECT_TRUE(find_key(after, "dock:Third")->open);
}

TEST(DockCensus, DuplicateTitlesGetDistinctKeys)
{
  DockCensusWindow window;
  window.add_dock("Image");
  window.add_dock("Views");
  window.add_dock("Image");
  window.add_dock("Image");
  window.settle();

  const auto docks = census_docks(window, nullptr);
  EXPECT_EQ(
    keys_of(docks),
    (std::vector<QString>{"dock:Image", "dock:Views", "dock:Image#2", "dock:Image#3"}));
  for (const DockEntry & dock : docks) {
    EXPECT_EQ(dock.title, dock.key.startsWith("dock:Image") ? "Image" : "Views");
  }
}

TEST(DockCensus, ADockThatStreamsKeepsItsKeyWhenAnEarlierDockOfTheTitleIsDeleted)
{
  DockCensusWindow window;
  QDockWidget * first = window.add_dock("Tool Properties");
  QDockWidget * second = window.add_dock("Tool Properties");
  window.settle();

  // The second dock streams under its "#2" key; the host remembers it by the content widget.
  const auto before = census_docks(window, nullptr);
  ASSERT_EQ(
    keys_of(before), (std::vector<QString>{"dock:Tool Properties", "dock:Tool Properties#2"}));
  const std::map<const QWidget *, QString> kept = {
    {second->widget(), QStringLiteral("dock:Tool Properties#2")}};

  // Without the memory the survivor moves to the first key, as the keys follow the child order.
  delete first;
  QApplication::processEvents();
  EXPECT_EQ(keys_of(census_docks(window, nullptr)), (std::vector<QString>{"dock:Tool Properties"}));
  // With it the key stays, and a dock added later takes the free key around it.
  EXPECT_EQ(
    keys_of(census_docks(window, nullptr, kept)),
    (std::vector<QString>{"dock:Tool Properties#2"}));
  window.add_dock("Tool Properties");
  window.settle();
  const auto after = census_docks(window, nullptr, kept);
  EXPECT_EQ(
    keys_of(after), (std::vector<QString>{"dock:Tool Properties#2", "dock:Tool Properties"}));
  EXPECT_EQ(after[0].content, second->widget());

  // A kept key that no dock holds any more is not used, and every key stays unique.
  const std::map<const QWidget *, QString> stale = {{nullptr, QStringLiteral("dock:Gone")}};
  EXPECT_EQ(
    keys_of(census_docks(window, nullptr, stale)),
    (std::vector<QString>{"dock:Tool Properties", "dock:Tool Properties#2"}));
}

TEST(DockCensus, TheSignatureChangesOnAddRemoveRenameClose)
{
  DockCensusWindow window;
  window.add_dock("Displays");
  QDockWidget * views = window.add_dock("Views");
  window.settle();

  const QString base = census_signature(census_docks(window, nullptr));
  EXPECT_FALSE(base.isEmpty());
  EXPECT_EQ(census_signature(census_docks(window, nullptr)), base) << "a walk changes nothing";

  // Add.
  QDockWidget * time = window.add_dock("Time");
  QApplication::processEvents();
  const QString added = census_signature(census_docks(window, nullptr));
  EXPECT_NE(added, base);

  // Rename.
  time->setWindowTitle("Clock");
  const QString renamed = census_signature(census_docks(window, nullptr));
  EXPECT_NE(renamed, added);

  // Close (the dock stays in the list, not open).
  views->close();
  QApplication::processEvents();
  const QString closed = census_signature(census_docks(window, nullptr));
  EXPECT_NE(closed, renamed);

  // Open again: back to the signature before the close.
  views->show();
  QApplication::processEvents();
  EXPECT_EQ(census_signature(census_docks(window, nullptr)), renamed);

  // Remove.
  delete time;
  const QString removed = census_signature(census_docks(window, nullptr));
  EXPECT_NE(removed, renamed);
  EXPECT_EQ(removed, base);

  EXPECT_TRUE(census_signature({}).isEmpty());
}

TEST(DockCensus, ThisPanelIsSkipped)
{
  DockCensusWindow window;
  QDockWidget * self_dock = window.add_dock("Qar Streaming");
  window.add_dock("Displays");
  window.add_dock("Qar Streaming");  // another dock with the same title is not this panel
  window.settle();

  const auto docks = census_docks(window, self_dock->widget());
  EXPECT_EQ(keys_of(docks), (std::vector<QString>{"dock:Displays", "dock:Qar Streaming"}))
    << "the skipped dock does not take the key of the next dock of its title";
  const auto everything = census_docks(window, nullptr);
  EXPECT_EQ(everything.size(), 3u);
}
