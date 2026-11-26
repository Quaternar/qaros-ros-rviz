// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_PANEL_LIST_WIDGET_HPP_
#define QAR_RVIZ_STREAMING__AR_PANEL_LIST_WIDGET_HPP_

/// @file ar_panel_list_widget.hpp
/// @brief The list of AR panels of the "AR panels" tab: a table of eight rows, a pager row and the
/// buttons that act on the selected row. Qt Widgets only: no RViz, no ROS, no QAROS.
///
/// The panel streams itself to AR, so nothing the list shows may change its size. The list is
/// exactly eight rows high whatever it holds, has no scroll bars (more rows are reached with the
/// pager), takes no width of its own, and every button keeps the width of its longest text. The
/// host gives the widget the rows of the list and is told about the buttons through signals;
/// the rows are updated in place, a cell is written only when its text changed.

#include <vector>

#include <QString>
#include <QWidget>

#include "qar_rviz_streaming/ar_panel_list.hpp"

class QEvent;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace qar_rviz_streaming
{

class ArPanelListWidget : public QWidget
{
  Q_OBJECT

public:
  explicit ArPanelListWidget(QWidget * parent = nullptr);

  /// @brief Shows @p rows. The selection stays on its key; when its row is gone the first row is
  /// selected. The page stays when it still exists.
  void set_rows(const std::vector<ArPanelRow> & rows);

  /// @brief The key of the selected row; empty while the list is empty.
  QString selected_key() const;

  /// @brief Selects the row with @p key (on its page); nothing happens when there is none.
  void select_key(const QString & key);

  /// @brief Whether a panel may be streamed or shown now (it needs a QarOS session). Hiding and
  /// stopping stay available.
  void set_stream_allowed(bool allowed);

  /// @brief The rows as given to @ref set_rows.
  const std::vector<ArPanelRow> & rows() const { return rows_; }

  /// @brief Gives the list the height of its eight rows again, measured under the style the
  /// widget uses now. The host calls it after it changed the style of the widget's tree.
  void remeasure_rows();

  // The controls, for the host's tool tips and for tests.
  QTreeWidget * tree() const { return tree_; }
  QPushButton * primary_button() const { return primary_button_; }
  QPushButton * size_button() const { return size_button_; }
  QPushButton * reopen_button() const { return reopen_button_; }
  QPushButton * previous_button() const { return previous_button_; }
  QPushButton * next_button() const { return next_button_; }
  QPushButton * refresh_button() const { return refresh_button_; }
  QLabel * page_label() const { return page_label_; }

Q_SIGNALS:
  void primaryRequested(QString key);
  void sizeRequested(QString key);
  void reopenRequested(QString key);
  void refreshRequested();

protected:
  bool eventFilter(QObject * watched, QEvent * event) override;

private:
  /// The row of the selected key, or null.
  const ArPanelRow * selected_row() const;
  void refresh_tree();
  void refresh_pager();
  void refresh_buttons();
  void fit_columns();
  void on_selection_changed();
  void go_to_page(int page);

  QTreeWidget * tree_ = nullptr;
  QPushButton * previous_button_ = nullptr;
  QPushButton * next_button_ = nullptr;
  QLabel * page_label_ = nullptr;
  QPushButton * refresh_button_ = nullptr;
  QPushButton * primary_button_ = nullptr;
  QPushButton * size_button_ = nullptr;
  QPushButton * reopen_button_ = nullptr;

  std::vector<ArPanelRow> rows_;
  QString selected_;
  int page_ = 0;
  bool stream_allowed_ = false;
};

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_PANEL_LIST_WIDGET_HPP_
