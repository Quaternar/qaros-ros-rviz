// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__ADD_TOPIC_DIALOG_HPP_
#define QAR_RVIZ_STREAMING__ADD_TOPIC_DIALOG_HPP_

/// @file add_topic_dialog.hpp
/// @brief The graph browser: a live ROS 2 tree of topics, services and actions
/// with a filter, and the choice of what to do with the one you picked.
///
/// The listing covers the whole graph (`qar_ros_ui::GraphDiscovery` fills it)
/// and the dialog carries the "expose as a QAR AR tool" decision. It is pure Qt
/// Widgets — handed a listing and a DisplayClassCatalog, it hands back the
/// operator's choice. It performs no ROS call, creates no RViz Display and
/// generates no panel, so the ROS-facing work stays in QarStreamingPanel where
/// the node and DisplayContext live.

#include <QDialog>
#include <QString>

#include <vector>

#include "qar_rviz_streaming/ar_ros_entity.hpp"
#include "qar_rviz_streaming/display_class_catalog.hpp"
#include "qar_rviz_streaming/ros_graph_tree.hpp"

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace qar_rviz_streaming
{

/// @brief What the operator picked and what they asked for.
struct AddTopicSelection
{
  RosEntityKind kind = RosEntityKind::Topic;
  QString name;          ///< Fully-qualified graph name.
  QString message_type;  ///< Topic message / service / action type.
  /// RViz Display class id to instantiate (topics only); empty when no display
  /// handles the type, in which case the topic can still be pinned or exposed.
  QString display_class;

  /// Generate an AR panel for it and open it in the headset. Always true for a
  /// service or an action — RViz has nothing else to do with those.
  bool expose_as_ar_tool = false;
  /// Topics only: the AR panel also offers a publish form. Off by default —
  /// a `publish:/cmd_vel` control is a robot command, so the operator opts in.
  bool allow_publish = false;

  bool empty() const { return name.isEmpty(); }
};

/// @brief Modal picker over the live ROS graph.
class AddTopicDialog : public QDialog
{
  Q_OBJECT

public:
  AddTopicDialog(
    std::vector<RosGraphEntry> entries,
    const DisplayClassCatalog & catalog,
    QWidget * parent = nullptr);

  /// @brief The operator's choice; only meaningful after @c exec() returned Accepted.
  const AddTopicSelection & selection() const { return selection_; }

  /// @brief Replace the listing (the Refresh button re-queries the graph).
  /// Keeps the current filter and re-selects the previous entity when it is
  /// still there, so a refresh does not lose the operator's place.
  void setEntries(std::vector<RosGraphEntry> entries);

Q_SIGNALS:
  /// @brief The operator asked for a fresh listing. The panel re-queries ROS and
  /// calls @ref setEntries; the dialog never touches ROS itself.
  void refreshRequested();

private Q_SLOTS:
  void onFilterChanged(const QString & text);
  void onSelectionChanged();
  void onItemActivated(QTreeWidgetItem * item, int column);
  void onExposeToggled(bool checked);

private:
  void rebuildTree();
  /// One top-level group ("Topics") plus its namespace tree.
  void addKindGroup(RosEntityKind kind, const std::vector<RosGraphEntry> & kept);
  /// Recursively turn a RosTopicNode into QTreeWidgetItems.
  void addNode(RosEntityKind kind, const RosTopicNode & node, QTreeWidgetItem * parent_item);
  /// Refresh the "what Add will do" line, the checkboxes and the OK button.
  void updateSelectionState();
  /// "topic\n/scan" — the reselect key, since a topic and a service may share a name.
  static QString selectionKey(RosEntityKind kind, const QString & name);

  std::vector<RosGraphEntry> entries_;
  const DisplayClassCatalog & catalog_;

  QLineEdit * filter_edit_ = nullptr;
  QTreeWidget * tree_ = nullptr;
  QCheckBox * expose_check_ = nullptr;
  QCheckBox * publish_check_ = nullptr;
  QLabel * detail_label_ = nullptr;
  QDialogButtonBox * buttons_ = nullptr;

  AddTopicSelection selection_;
  /// Entity to re-select after a rebuild, so filtering/refreshing keeps the place.
  QString pending_reselect_;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__ADD_TOPIC_DIALOG_HPP_
