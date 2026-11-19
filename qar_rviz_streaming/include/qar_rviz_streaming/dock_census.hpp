// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__DOCK_CENSUS_HPP_
#define QAR_RVIZ_STREAMING__DOCK_CENSUS_HPP_

/// @file dock_census.hpp
/// @brief The docks of RViz's main window as a plain list. Qt Widgets only: no RViz, no ROS.
///
/// RViz offers no list of its panels and no signal for a panel that is added or removed. Every
/// panel lives in a QDockWidget that is a direct child of the main window, whether the dock is
/// docked, tabbed behind another or floating, so the list is a walk over those children. The walk
/// is a few dozen objects, cheap enough for a few calls a second. The host compares
/// @ref census_signature with the signature of its last walk to learn that the list changed.

#include <map>
#include <vector>

#include <QPointer>
#include <QString>
#include <QWidget>

class QMainWindow;

namespace qar_rviz_streaming
{

/// One dock of the main window.
struct DockEntry
{
  QString key;              ///< "dock:<title>", "dock:<title>#2" for the second dock of that title
  QString title;            ///< the title of the dock, else its object name
  QPointer<QWidget> content;
  bool open = false;        ///< not closed in RViz (a dock tabbed behind another stays open)
};

/// The main window of @p any: the first QMainWindow among @p any and its parentWidget() chain (the
/// parent of a floating dock is still the main window); null when there is none.
QMainWindow * host_main_window(QWidget * any);

/// The docks of @p main, its direct children only, in child order. A dock whose content is
/// @p self_content is skipped, and so is a dock that has neither a title nor an object name. The
/// keys are unique: the second dock of one title gets the suffix "#2", the third "#3", in child
/// order.
///
/// @p kept names the key a content widget is already known under (the key it streams under). A
/// dock whose content is in @p kept keeps that key even when a dock of the same title before it
/// is deleted, and the other docks take the free keys around it, so a deleted dock never moves
/// the key of one that streams. A kept key that no dock holds is simply not used.
std::vector<DockEntry> census_docks(
  QMainWindow & main, const QWidget * self_content,
  const std::map<const QWidget *, QString> & kept = {});

/// A text that changes when a key, a title or the open state of a dock changes, and that stays
/// the same otherwise.
QString census_signature(const std::vector<DockEntry> & docks);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__DOCK_CENSUS_HPP_
