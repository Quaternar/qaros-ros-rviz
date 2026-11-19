// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/hub_connection_widget.hpp"

#include <algorithm>

#include <QAbstractItemView>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

#include "qar_qt_streaming/scroll_census.hpp"

namespace qar_rviz_streaming
{
namespace
{

constexpr int kTargetRows = 8;
constexpr const char * kDefaultCancelToolTip =
  "Stop loading QAROS, waiting for the approval in the Hub or for the rejoin.";
constexpr const char * kGroupToolTip =
  "The plugin joins the QAROS Hub of this PC. The Hub asks once whether rviz2 may join (click "
  "Allow in the QAROS window); the approval is revoked in the Visualizer. To reach the Hub of "
  "another PC, make the Hub of this PC a member of it.";
/// The words the cancel button can carry (ConnectionView::cancel_text).
constexpr const char * kCancelTexts[] = {"Cancel", "Join again", "Stop rejoining"};

/// A widget whose content changes at run time never asks its layout for more width.
void make_width_neutral(QWidget * widget)
{
  QSizePolicy policy = widget->sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Ignored);
  widget->setSizePolicy(policy);
}

void set_label_text(QLabel * label, const QString & text)
{
  if (label->text() != text) {
    label->setText(text);
  }
}

}  // namespace

std::string hub_target_row_text(const HubTargetRow & row)
{
  std::string text = row.name.empty() ? std::string("(unnamed device)") : row.name;
  if (!row.category.empty()) {
    text += " - " + row.category;
  }
  if (!row.presence.empty()) {
    text += ", " + row.presence;
  }
  text += row.served ? " - served" : " - not served";
  if (!row.served && !row.not_served_reason.empty()) {
    text += ": " + row.not_served_reason;
  }
  return text;
}

HubConnectionWidget::HubConnectionWidget(QWidget * parent)
: QGroupBox("QAROS Hub", parent)
{
  setToolTip(QString::fromUtf8(kGroupToolTip));
  auto * layout = new QVBoxLayout(this);

  auto * status_form = new QFormLayout();
  identity_label_ = new QLabel("This PC (not joined yet)", this);
  identity_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  identity_label_->setWordWrap(true);
  make_width_neutral(identity_label_);
  state_label_ = new QLabel("QarOS: not connected", this);
  state_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  state_label_->setWordWrap(true);
  make_width_neutral(state_label_);
  status_form->addRow("Identity:", identity_label_);
  status_form->addRow("State:", state_label_);
  layout->addLayout(status_form);

  // Cancel ends the wait for approval and the automatic rejoin. It sits at the left.
  auto * cancel_row = new QHBoxLayout();
  cancel_button_ = new QPushButton("Cancel", this);
  cancel_button_->setToolTip(QString::fromUtf8(kDefaultCancelToolTip));
  cancel_button_->setEnabled(false);
  // The button takes the room of its longest words from the start, so that a change of its text
  // never changes the width of the group.
  int cancel_width = 0;
  for (const char * text : kCancelTexts) {
    cancel_button_->setText(QString::fromUtf8(text));
    cancel_width = std::max(cancel_width, cancel_button_->sizeHint().width());
  }
  cancel_button_->setText("Cancel");
  cancel_button_->setMinimumWidth(cancel_width);
  cancel_row->addWidget(cancel_button_);
  cancel_row->addStretch(1);
  layout->addLayout(cancel_row);

  // The targets: the devices the Hub lists that this program renders for.
  target_list_ = new QListWidget(this);
  target_list_->setToolTip(
    "The devices in the Hub that this program streams to: name, kind, whether they are online, "
    "and whether they get a stream.");
  // The list has a height of its own and never takes spare room: it scrolls when the Hub holds
  // more targets than the rows it shows. A row is as high as the style makes an item (more than
  // the line spacing in some styles), so the height is measured with one probe item.
  remeasure_rows();
  target_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  target_list_->setSelectionMode(QAbstractItemView::NoSelection);
  target_list_->setTextElideMode(Qt::ElideRight);
  make_width_neutral(target_list_);
  target_list_->setSizePolicy(target_list_->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
  // A list grows with what the Hub holds: the census of scroll areas leaves it out.
  target_list_->setProperty(qar_qt_streaming::kScrollsByDesignProperty, true);
  layout->addWidget(new QLabel("Targets:", this));
  layout->addWidget(target_list_);

  connect(cancel_button_, &QPushButton::clicked, this, [this]() { Q_EMIT cancelRequested(); });
}

void HubConnectionWidget::remeasure_rows()
{
  // A list that holds rows is measured by its first row; an empty one by a probe item.
  const bool probed = target_list_->count() == 0;
  if (probed) {
    target_list_->addItem("Device");
  }
  int row_height = target_list_->sizeHintForRow(0);
  if (probed) {
    target_list_->clear();
  }
  if (row_height <= 0) {
    row_height = target_list_->fontMetrics().lineSpacing();
  }
  target_list_->setFixedHeight(row_height * kTargetRows + 2 * target_list_->frameWidth());
}

void HubConnectionWidget::apply(const ConnectionView & view)
{
  set_label_text(state_label_, QString::fromStdString(view.state_text));
  set_label_text(identity_label_, QString::fromStdString(view.identity_text));
  cancel_button_->setEnabled(view.cancel_enabled);
  cancel_button_->setText(QString::fromStdString(view.cancel_text));
  cancel_button_->setToolTip(
    view.cancel_tooltip.empty() ? QString::fromUtf8(kDefaultCancelToolTip) :
    QString::fromStdString(view.cancel_tooltip));
}

void HubConnectionWidget::set_targets(const std::vector<HubTargetRow> & rows)
{
  QStringList lines;
  for (const HubTargetRow & row : rows) {
    lines.append(QString::fromStdString(hub_target_row_text(row)));
  }
  bool same = target_list_->count() == lines.size();
  for (int i = 0; same && i < lines.size(); ++i) {
    same = target_list_->item(i)->text() == lines[i];
  }
  if (same) {
    return;
  }
  target_list_->clear();
  target_list_->addItems(lines);
}

}  // namespace qar_rviz_streaming
