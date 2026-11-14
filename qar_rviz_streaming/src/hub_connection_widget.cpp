// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/hub_connection_widget.hpp"

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

constexpr int kTargetRows = 3;

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
  auto * layout = new QVBoxLayout(this);
  setToolTip(
    "This program joins the QAROS Hub of this PC as a local app; the Hub asks once whether it "
    "may join. To reach the Hub of another PC, make the Hub of this PC a member of it.");

  auto * cancel_row = new QHBoxLayout();
  cancel_button_ = new QPushButton("Cancel", this);
  cancel_button_->setToolTip("Stop loading QAROS or waiting for the approval in the Hub.");
  cancel_button_->setEnabled(false);
  cancel_row->addStretch(1);
  cancel_row->addWidget(cancel_button_);
  layout->addLayout(cancel_row);

  auto * status_form = new QFormLayout();
  identity_label_ = new QLabel("This PC: local app", this);
  identity_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  identity_label_->setWordWrap(true);
  identity_label_->setToolTip(kIdentityToolTip);
  make_width_neutral(identity_label_);
  state_label_ = new QLabel("QarOS: not connected", this);
  state_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  state_label_->setWordWrap(true);
  make_width_neutral(state_label_);
  status_form->addRow("Identity:", identity_label_);
  status_form->addRow("State:", state_label_);
  layout->addLayout(status_form);

  // The targets: the devices the Hub lists that this program renders for.
  target_list_ = new QListWidget(this);
  target_list_->setToolTip(
    "The devices in the Hub that this program streams to: name, kind, whether they are online, "
    "and whether they get a stream.");
  target_list_->setMinimumHeight(target_list_->fontMetrics().lineSpacing() * kTargetRows + 6);
  target_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  target_list_->setSelectionMode(QAbstractItemView::NoSelection);
  target_list_->setTextElideMode(Qt::ElideRight);
  make_width_neutral(target_list_);
  // A list grows with what the Hub holds: the census of scroll areas leaves it out.
  target_list_->setProperty(qar_qt_streaming::kScrollsByDesignProperty, true);
  layout->addWidget(new QLabel("Targets:", this));
  layout->addWidget(target_list_);

  connect(cancel_button_, &QPushButton::clicked, this, [this]() { Q_EMIT cancelRequested(); });
}

void HubConnectionWidget::apply(const ConnectionView & view)
{
  set_label_text(state_label_, QString::fromStdString(view.state_text));
  set_label_text(identity_label_, QString::fromStdString(view.identity_text));
  cancel_button_->setEnabled(view.cancel_enabled);
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
