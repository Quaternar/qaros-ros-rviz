// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_panel_list_widget.hpp"

#include <algorithm>

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "qar_rviz_streaming/ar_copy_scale.hpp"
#include "qar_rviz_streaming/stable_panel_layout.hpp"

namespace qar_rviz_streaming
{
namespace
{

constexpr const char * kColumnTitles[kArPanelListColumns] = {
  "Panel", "Kind", "Seen by", "In AR", "Size"};
/// The share of the width each column gets (the columns always fill the list exactly, so the
/// list has no sideways scroll bar and the widths do not depend on the texts).
constexpr int kColumnWeights[kArPanelListColumns] = {30, 17, 14, 25, 14};
constexpr int kMinimumColumnWidth = 24;

/// Every text a button can carry; the button keeps the width of the longest.
const QStringList kPrimaryTexts = {
  QStringLiteral("Stream to AR"), QStringLiteral("Hide in AR"), QStringLiteral("Stop streaming"),
  QStringLiteral("Show in AR")};
const QStringList kSizeTexts = {
  QString::fromLatin1(kArCopyButtonToNativeText), QString::fromLatin1(kArCopyButtonToEnlargedText)};

constexpr const char * kListToolTip =
  "The panels that can be shown in AR, the AR control panel and the AR tools. Select a row, then "
  "use the buttons below. Nothing here is ever closed while a session runs: a panel is shown, "
  "hidden or stopped.";
constexpr const char * kPrimaryToolTip =
  "Stream the selected panel to AR, hide it there or show it again. With \"Hide panels in AR "
  "instead of closing them\" off, the button stops the stream instead of hiding the panel. "
  "For an AR tool the button hides or shows the tool's panel. Streaming and showing need a "
  "running QarOS session.";
constexpr const char * kSizeToolTip =
  "Switch the AR copy of the selected panel between enlarged controls and native size. The "
  "panel on the desktop does not change.";
constexpr const char * kReopenToolTip =
  "Ask QarOS to show the selected AR panel again if it was closed or hidden in AR.";
constexpr const char * kRefreshToolTip = "Re-scan RViz for dock panels (they come and go).";
constexpr const char * kPreviousToolTip = "Show the rows before these.";
constexpr const char * kNextToolTip = "Show the rows after these.";

/// A widget whose content changes at run time never asks its layout for more width.
void make_width_neutral(QWidget * widget)
{
  QSizePolicy policy = widget->sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Ignored);
  widget->setSizePolicy(policy);
}

/// Gives @p button the width of the longest of @p texts and leaves @p first on it.
void fix_width_to(QPushButton * button, const QStringList & texts, const QString & first)
{
  int widest = 0;
  for (const QString & text : texts) {
    button->setText(text);
    widest = std::max(widest, button->sizeHint().width());
  }
  button->setText(first);
  button->setFixedWidth(widest);
}

void set_text_if_changed(QPushButton * button, const QString & text)
{
  if (button->text() != text) {
    button->setText(text);
  }
}

}  // namespace

void ArPanelListWidget::remeasure_rows()
{
  // A list that holds rows is measured by its first row; an empty one by a probe row.
  int row_height = 0;
  if (tree_->topLevelItemCount() > 0) {
    row_height = tree_->sizeHintForRow(0);
  } else {
    auto * probe = new QTreeWidgetItem(tree_);
    probe->setText(0, QStringLiteral("Mg"));
    row_height = tree_->sizeHintForRow(0);
    delete probe;
  }
  row_height = std::max(row_height, tree_->fontMetrics().height());
  tree_->setFixedHeight(
    tree_->header()->sizeHint().height() + kArPanelListRows * row_height +
    2 * tree_->frameWidth());
}

ArPanelListWidget::ArPanelListWidget(QWidget * parent)
: QWidget(parent)
{
  auto * layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(6);

  // The list: a flat table without scroll bars. Its height is eight rows, taken from the style
  // with one row in it, so that no text and no number of rows can move the geometry.
  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(kArPanelListColumns);
  QStringList titles;
  for (const char * title : kColumnTitles) {
    titles.append(QString::fromLatin1(title));
  }
  tree_->setHeaderLabels(titles);
  tree_->setRootIsDecorated(false);
  tree_->setItemsExpandable(false);
  tree_->setUniformRowHeights(true);
  tree_->setAllColumnsShowFocus(true);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tree_->setTextElideMode(Qt::ElideRight);
  tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tree_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tree_->setToolTip(QString::fromLatin1(kListToolTip));
  tree_->header()->setStretchLastSection(false);
  tree_->header()->setSectionsMovable(false);
  tree_->header()->setSectionsClickable(false);
  tree_->header()->setSectionResizeMode(QHeaderView::Fixed);
  tree_->header()->setMinimumSectionSize(kMinimumColumnWidth);
  make_width_neutral(tree_);
  tree_->setMinimumWidth(0);
  tree_->viewport()->installEventFilter(this);

  remeasure_rows();
  layout->addWidget(tree_);

  // The pager row is always there; its buttons are only hidden (they keep their room) when all
  // rows fit on one page. Refresh stays at the right.
  auto * pager = new QHBoxLayout();
  previous_button_ = new QPushButton("Previous", this);
  previous_button_->setToolTip(QString::fromLatin1(kPreviousToolTip));
  next_button_ = new QPushButton("Next", this);
  next_button_->setToolTip(QString::fromLatin1(kNextToolTip));
  page_label_ = new QLabel(this);
  page_label_->setAlignment(Qt::AlignCenter);
  make_width_neutral(page_label_);
  refresh_button_ = new QPushButton("Refresh", this);
  refresh_button_->setToolTip(QString::fromLatin1(kRefreshToolTip));
  for (QPushButton * button : {previous_button_, next_button_, refresh_button_}) {
    button->setFixedWidth(button->sizeHint().width());
  }
  retain_size_when_hidden(previous_button_);
  retain_size_when_hidden(next_button_);
  retain_size_when_hidden(page_label_);
  pager->addWidget(previous_button_);
  pager->addWidget(next_button_);
  pager->addWidget(page_label_, 1);
  pager->addWidget(refresh_button_);
  layout->addLayout(pager);

  // The buttons of the selected row. Their texts follow the row and their widths do not.
  auto * actions = new QHBoxLayout();
  primary_button_ = new QPushButton(kPrimaryTexts.first(), this);
  primary_button_->setToolTip(QString::fromLatin1(kPrimaryToolTip));
  fix_width_to(primary_button_, kPrimaryTexts, kPrimaryTexts.first());
  size_button_ = new QPushButton(kSizeTexts.first(), this);
  size_button_->setToolTip(QString::fromLatin1(kSizeToolTip));
  fix_width_to(size_button_, kSizeTexts, kSizeTexts.first());
  reopen_button_ = new QPushButton("Re-open in AR", this);
  reopen_button_->setToolTip(QString::fromLatin1(kReopenToolTip));
  reopen_button_->setFixedWidth(reopen_button_->sizeHint().width());
  actions->addWidget(primary_button_);
  actions->addWidget(size_button_);
  actions->addWidget(reopen_button_);
  actions->addStretch(1);
  layout->addLayout(actions);

  primary_button_->setEnabled(false);
  size_button_->setEnabled(false);
  reopen_button_->setEnabled(false);
  previous_button_->setVisible(false);
  next_button_->setVisible(false);
  page_label_->setVisible(false);

  connect(
    tree_, &QTreeWidget::itemSelectionChanged, this, &ArPanelListWidget::on_selection_changed);
  connect(previous_button_, &QPushButton::clicked, this, [this]() {go_to_page(page_ - 1);});
  connect(next_button_, &QPushButton::clicked, this, [this]() {go_to_page(page_ + 1);});
  connect(refresh_button_, &QPushButton::clicked, this, &ArPanelListWidget::refreshRequested);
  connect(
    primary_button_, &QPushButton::clicked, this, [this]() {
      const ArPanelRow * row = selected_row();
      if (row != nullptr && row->primary != ArPanelAction::None) {
        Q_EMIT primaryRequested(selected_);
      }
    });
  connect(
    size_button_, &QPushButton::clicked, this, [this]() {
      const ArPanelRow * row = selected_row();
      if (row != nullptr && row->size_enabled) {
        Q_EMIT sizeRequested(selected_);
      }
    });
  connect(
    reopen_button_, &QPushButton::clicked, this, [this]() {
      const ArPanelRow * row = selected_row();
      if (row != nullptr && row->reopen) {
        Q_EMIT reopenRequested(selected_);
      }
    });
  fit_columns();
}

bool ArPanelListWidget::eventFilter(QObject * watched, QEvent * event)
{
  if (tree_ != nullptr && watched == tree_->viewport() && event->type() == QEvent::Resize) {
    fit_columns();
  }
  return QWidget::eventFilter(watched, event);
}

void ArPanelListWidget::fit_columns()
{
  const int width = tree_->viewport()->width();
  if (width <= 0) {
    return;
  }
  int weights = 0;
  for (const int weight : kColumnWeights) {
    weights += weight;
  }
  int used = 0;
  for (int column = 0; column < kArPanelListColumns; ++column) {
    // The last column takes what the others left, so the columns fill the list exactly.
    const int wanted = column + 1 == kArPanelListColumns ?
      width - used : width * kColumnWeights[column] / weights;
    const int section = std::max(wanted, kMinimumColumnWidth);
    if (tree_->columnWidth(column) != section) {
      tree_->setColumnWidth(column, section);
    }
    used += section;
  }
}

const ArPanelRow * ArPanelListWidget::selected_row() const
{
  for (const ArPanelRow & row : rows_) {
    if (QString::fromStdString(row.key) == selected_) {
      return &row;
    }
  }
  return nullptr;
}

QString ArPanelListWidget::selected_key() const
{
  return selected_row() != nullptr ? selected_ : QString();
}

void ArPanelListWidget::set_rows(const std::vector<ArPanelRow> & rows)
{
  rows_ = rows;
  if (selected_row() == nullptr) {
    selected_ = rows_.empty() ? QString() : QString::fromStdString(rows_.front().key);
  }
  refresh_tree();
  refresh_pager();
  refresh_buttons();
}

void ArPanelListWidget::select_key(const QString & key)
{
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    if (QString::fromStdString(rows_[i].key) == key) {
      selected_ = key;
      page_ = static_cast<int>(i) / kArPanelListRows;
      refresh_tree();
      refresh_pager();
      refresh_buttons();
      return;
    }
  }
}

void ArPanelListWidget::set_stream_allowed(bool allowed)
{
  if (stream_allowed_ != allowed) {
    stream_allowed_ = allowed;
    refresh_buttons();
  }
}

void ArPanelListWidget::go_to_page(int page)
{
  const ArPanelPage now = ar_panel_page(static_cast<int>(rows_.size()), page);
  const int wanted = now.first / kArPanelListRows;
  if (wanted == page_) {
    return;
  }
  page_ = wanted;
  // The row the buttons act on is on the page: the first one.
  if (now.last > now.first) {
    selected_ = QString::fromStdString(rows_[static_cast<std::size_t>(now.first)].key);
  }
  refresh_tree();
  refresh_pager();
  refresh_buttons();
}

void ArPanelListWidget::on_selection_changed()
{
  const QList<QTreeWidgetItem *> items = tree_->selectedItems();
  if (items.isEmpty()) {
    return;
  }
  const QString key = items.front()->data(0, Qt::UserRole).toString();
  if (key != selected_) {
    selected_ = key;
    refresh_buttons();
  }
}

void ArPanelListWidget::refresh_tree()
{
  const QSignalBlocker blocker(tree_);
  ArPanelPage page = ar_panel_page(static_cast<int>(rows_.size()), page_);
  page_ = page.first / kArPanelListRows;

  const int count = page.last - page.first;
  while (tree_->topLevelItemCount() < count) {
    new QTreeWidgetItem(tree_);
  }
  while (tree_->topLevelItemCount() > count) {
    delete tree_->takeTopLevelItem(tree_->topLevelItemCount() - 1);
  }

  QTreeWidgetItem * selected_item = nullptr;
  for (int i = 0; i < count; ++i) {
    const ArPanelRow & row = rows_[static_cast<std::size_t>(page.first + i)];
    QTreeWidgetItem * item = tree_->topLevelItem(i);
    const QString key = QString::fromStdString(row.key);
    if (item->data(0, Qt::UserRole).toString() != key) {
      item->setData(0, Qt::UserRole, key);
    }
    const QString tip = QString::fromStdString(row.tool_tip);
    for (int column = 0; column < kArPanelListColumns; ++column) {
      const QString text = QString::fromStdString(row.cells[column]);
      if (item->text(column) != text) {
        item->setText(column, text);
      }
      if (item->toolTip(column) != tip) {
        item->setToolTip(column, tip);
      }
    }
    if (key == selected_) {
      selected_item = item;
    }
  }
  if (selected_item != nullptr) {
    if (tree_->currentItem() != selected_item) {
      tree_->setCurrentItem(selected_item);
    }
    if (!selected_item->isSelected()) {
      selected_item->setSelected(true);
    }
  } else if (!tree_->selectedItems().isEmpty()) {
    tree_->clearSelection();
  }
}

void ArPanelListWidget::refresh_pager()
{
  const ArPanelPage page = ar_panel_page(static_cast<int>(rows_.size()), page_);
  const bool paged = !page.label.empty();
  previous_button_->setVisible(paged);
  next_button_->setVisible(paged);
  page_label_->setVisible(paged);
  previous_button_->setEnabled(page.prev);
  next_button_->setEnabled(page.next);
  const QString label = QString::fromStdString(page.label);
  if (page_label_->text() != label) {
    page_label_->setText(label);
  }
}

void ArPanelListWidget::refresh_buttons()
{
  const ArPanelRow * row = selected_row();
  if (row == nullptr) {
    primary_button_->setEnabled(false);
    size_button_->setEnabled(false);
    reopen_button_->setEnabled(false);
    return;
  }
  if (row->primary != ArPanelAction::None) {
    set_text_if_changed(primary_button_, QString::fromStdString(row->primary_text));
  }
  // Streaming and showing need a QarOS session; hiding and stopping do not.
  const bool needs_session =
    row->primary == ArPanelAction::Stream || row->primary == ArPanelAction::Show;
  primary_button_->setEnabled(
    row->primary != ArPanelAction::None && (!needs_session || stream_allowed_));
  if (row->size_enabled) {
    set_text_if_changed(size_button_, QString::fromStdString(row->size_text));
  }
  size_button_->setEnabled(row->size_enabled);
  reopen_button_->setEnabled(row->reopen);
}

}  // namespace qar_rviz_streaming
