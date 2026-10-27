// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/hub_connection_widget.hpp"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "qar_qt_streaming/scroll_census.hpp"
#include "qar_rviz_streaming/stable_panel_layout.hpp"

namespace qar_rviz_streaming
{
namespace
{

constexpr int kDefaultForgetConfirmationMs = 5000;
constexpr int kTargetRows = 3;
constexpr int kHostMaxLength = 253;
constexpr int kPortMaxLength = 5;
constexpr int kCodeMaxLength = 16;

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

  // Which Hub: the one of this PC, the one of another PC, or whichever fits.
  auto * form = new QFormLayout();
  mode_combo_ = new QComboBox(this);
  mode_combo_->addItem("Automatic", "auto");
  mode_combo_->addItem("This PC (approve once in the QAROS Hub)", "local");
  mode_combo_->addItem("Another PC (pairing code)", "remote");
  mode_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  mode_combo_->setMinimumContentsLength(14);
  mode_combo_->setToolTip(
    "Which QAROS Hub this program joins, when you connect. Automatic: the Hub of another PC "
    "when a pairing with it is stored, else the Hub of this PC when QAROS is installed here. "
    "This PC: the Hub asks once whether this program may join; no code. Another PC: type the "
    "host, the port and the code that Hub shows.");
  guard_wheel(mode_combo_);
  form->addRow("Hub:", mode_combo_);
  layout->addLayout(form);

  // Host and port of the Hub of another PC. The row keeps its room while it is hidden.
  auto * remote_fields = new QWidget(this);
  auto * remote_row = new QHBoxLayout(remote_fields);
  remote_row->setContentsMargins(0, 0, 0, 0);
  host_label_ = new QLabel("Host:", remote_fields);
  host_edit_ = new QLineEdit(remote_fields);
  host_edit_->setMaxLength(kHostMaxLength);
  host_edit_->setPlaceholderText("host name or address");
  host_edit_->setToolTip("The host name or address of the Hub of the other PC.");
  port_label_ = new QLabel("Port:", remote_fields);
  port_edit_ = new QLineEdit(remote_fields);
  port_edit_->setMaxLength(kPortMaxLength);
  port_edit_->setPlaceholderText("shown with the code");
  port_edit_->setToolTip(
    "The onboarding port of that Hub, 1 to 65535. The Hub shows it next to the code.");
  port_edit_->setMaximumWidth(120);
  remote_row->addWidget(host_label_);
  remote_row->addWidget(host_edit_, 1);
  remote_row->addWidget(port_label_);
  remote_row->addWidget(port_edit_);
  retain_size_when_hidden(remote_fields);
  remote_fields_ = remote_fields;
  layout->addWidget(remote_fields);

  // The pairing code. It lives in this line edit only.
  auto * code_row = new QHBoxLayout();
  auto * code_label = new QLabel("Code:", this);
  code_edit_ = new QLineEdit(this);
  code_edit_->setMaxLength(kCodeMaxLength);
  code_edit_->setPlaceholderText("pairing code");
  code_edit_->setInputMethodHints(Qt::ImhDigitsOnly | Qt::ImhNoPredictiveText);
  code_edit_->setToolTip(
    "The code that the other PC's Hub shows (Connection panel, Hubs, Pair another Hub). It "
    "lives about ten seconds and works once: type it and press Enter at once. It is not "
    "saved or logged.");
  code_edit_->setEnabled(false);
  pair_button_ = new QPushButton("Pair", this);
  pair_button_->setToolTip("Pair with the Hub at the host and port above, using the code.");
  pair_button_->setEnabled(false);
  cancel_button_ = new QPushButton("Cancel", this);
  cancel_button_->setToolTip("Stop waiting for approval, for the rejoin or for the pairing.");
  cancel_button_->setEnabled(false);
  code_row->addWidget(code_label);
  code_row->addWidget(code_edit_, 1);
  code_row->addWidget(pair_button_);
  code_row->addWidget(cancel_button_);
  layout->addLayout(code_row);

  forget_button_ = new QPushButton("Forget pairing", this);
  forget_button_->setEnabled(false);
  layout->addWidget(forget_button_);

  auto * status_form = new QFormLayout();
  identity_label_ = new QLabel("Not connected yet", this);
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

  forget_timer_ = new QTimer(this);
  forget_timer_->setSingleShot(true);
  forget_timer_->setInterval(kDefaultForgetConfirmationMs);
  connect(forget_timer_, &QTimer::timeout, this, [this]() { reset_forget_confirmation(); });

  connect(
    mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
    [this](int) { Q_EMIT modeEdited(mode_combo_->currentData().toString()); });
  connect(
    host_edit_, &QLineEdit::editingFinished, this,
    [this]() { Q_EMIT hostEdited(host_edit_->text().trimmed()); });
  connect(
    port_edit_, &QLineEdit::editingFinished, this,
    [this]() { Q_EMIT portEdited(port_edit_->text().trimmed()); });
  connect(code_edit_, &QLineEdit::returnPressed, this, [this]() { submit_code(); });
  connect(pair_button_, &QPushButton::clicked, this, [this]() { submit_code(); });
  connect(cancel_button_, &QPushButton::clicked, this, [this]() { Q_EMIT cancelRequested(); });
  connect(forget_button_, &QPushButton::clicked, this, [this]() { on_forget_clicked(); });
}

void HubConnectionWidget::apply(const ConnectionView & view)
{
  set_label_text(state_label_, QString::fromStdString(view.state_text));
  set_label_text(identity_label_, QString::fromStdString(view.identity_text));
  remote_fields_->setVisible(view.remote_fields_visible);
  code_edit_->setEnabled(view.code_enabled);
  pair_button_->setEnabled(view.pair_enabled);
  cancel_button_->setEnabled(view.cancel_enabled);

  forget_text_ = QString::fromStdString(view.forget_text);
  forget_tool_tip_ = QString::fromStdString(view.forget_tooltip);
  forget_enabled_ = view.forget_enabled;
  forget_button_->setEnabled(forget_enabled_);
  if (!forget_enabled_) {
    reset_forget_confirmation();
  } else if (!forget_pending_) {
    forget_button_->setText(forget_text_);
    forget_button_->setToolTip(forget_tool_tip_);
  }
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

void HubConnectionWidget::set_mode_word(const QString & word)
{
  const QSignalBlocker blocker(mode_combo_);
  const int index = mode_combo_->findData(word);
  if (index >= 0) {
    mode_combo_->setCurrentIndex(index);
  }
}

QString HubConnectionWidget::mode_word() const
{
  return mode_combo_->currentData().toString();
}

void HubConnectionWidget::set_host_text(const QString & text)
{
  const QSignalBlocker blocker(host_edit_);
  host_edit_->setText(text);
}

QString HubConnectionWidget::host_text() const
{
  return host_edit_->text().trimmed();
}

void HubConnectionWidget::set_port_text(const QString & text)
{
  const QSignalBlocker blocker(port_edit_);
  port_edit_->setText(text);
}

QString HubConnectionWidget::port_text() const
{
  return port_edit_->text().trimmed();
}

void HubConnectionWidget::clear_code()
{
  // setText, not clear(): the text goes without a trace in the undo history of the field.
  code_edit_->setText(QString());
}

void HubConnectionWidget::reset_forget_confirmation()
{
  forget_timer_->stop();
  forget_pending_ = false;
  forget_button_->setText(forget_text_.isEmpty() ? QStringLiteral("Forget pairing") : forget_text_);
  forget_button_->setToolTip(forget_tool_tip_);
}

bool HubConnectionWidget::forget_confirmation_pending() const
{
  return forget_pending_;
}

void HubConnectionWidget::set_forget_confirmation_ms(int milliseconds)
{
  forget_timer_->setInterval(milliseconds);
}

void HubConnectionWidget::submit_code()
{
  const QString host = host_edit_->text().trimmed();
  int port = 0;
  if (code_edit_->text().trimmed().isEmpty()) {
    Q_EMIT pairRefused(QStringLiteral("Type the pairing code that the Hub shows."));
    return;
  }
  if (host.isEmpty()) {
    Q_EMIT pairRefused(QStringLiteral("Type the host name or address of the Hub."));
    return;
  }
  if (!parse_hub_port(port_edit_->text().trimmed().toStdString(), port)) {
    Q_EMIT pairRefused(
      QStringLiteral("Type the port of the Hub, 1 to 65535; the Hub shows it with the code."));
    return;
  }
  const QString code = code_edit_->text().trimmed();
  clear_code();
  Q_EMIT pairRequested(code, host, port);
}

void HubConnectionWidget::on_forget_clicked()
{
  if (!forget_enabled_) {
    return;
  }
  if (!forget_pending_) {
    forget_pending_ = true;
    forget_button_->setText(QStringLiteral("Click again to forget"));
    forget_timer_->start();
    return;
  }
  reset_forget_confirmation();
  Q_EMIT forgetConfirmed();
}

}  // namespace qar_rviz_streaming
