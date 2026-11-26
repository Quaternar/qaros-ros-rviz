// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/dock_census.hpp"

#include <map>
#include <set>
#include <utility>

#include <QAction>
#include <QDockWidget>
#include <QMainWindow>

namespace qar_rviz_streaming
{

QMainWindow * host_main_window(QWidget * any)
{
  for (QWidget * widget = any; widget != nullptr; widget = widget->parentWidget()) {
    if (auto * main = qobject_cast<QMainWindow *>(widget)) {
      return main;
    }
  }
  return nullptr;
}

std::vector<DockEntry> census_docks(
  QMainWindow & main, const QWidget * self_content,
  const std::map<const QWidget *, QString> & kept)
{
  std::vector<DockEntry> docks;
  const QList<QDockWidget *> children =
    main.findChildren<QDockWidget *>(QString(), Qt::FindDirectChildrenOnly);
  // The keys of the docks that keep theirs are taken first, so no other dock is given one.
  std::set<QString> taken;
  for (QDockWidget * dock : children) {
    const auto found = dock->widget() != nullptr ? kept.find(dock->widget()) : kept.end();
    if (found != kept.end() && !found->second.isEmpty()) {
      taken.insert(found->second);
    }
  }
  for (QDockWidget * dock : children) {
    if (self_content != nullptr && dock->widget() == self_content) {
      continue;
    }
    QString title = dock->windowTitle();
    if (title.isEmpty()) {
      title = dock->objectName();
    }
    if (title.isEmpty()) {
      continue;  // nothing to name it with
    }
    DockEntry entry;
    const auto found = dock->widget() != nullptr ? kept.find(dock->widget()) : kept.end();
    if (found != kept.end() && !found->second.isEmpty()) {
      entry.key = found->second;
    } else {
      // The first free key of the title: "dock:<title>", then "#2", "#3", in child order.
      const QString base = QStringLiteral("dock:") + title;
      entry.key = base;
      for (int number = 2; taken.count(entry.key) != 0; ++number) {
        entry.key = base + QStringLiteral("#") + QString::number(number);
      }
      taken.insert(entry.key);
    }
    entry.title = title;
    entry.content = dock->widget();
    entry.open = dock->toggleViewAction()->isChecked();
    docks.push_back(std::move(entry));
  }
  return docks;
}

QString census_signature(const std::vector<DockEntry> & docks)
{
  QString text;
  for (const DockEntry & dock : docks) {
    text += dock.key;
    text += QLatin1Char('\t');
    text += dock.title;
    text += QLatin1Char('\t');
    text += dock.open ? QLatin1Char('1') : QLatin1Char('0');
    text += QLatin1Char('\n');
  }
  return text;
}

}  // namespace qar_rviz_streaming
