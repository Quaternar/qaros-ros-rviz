// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/add_topic_dialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <utility>

#include "qar_rviz_streaming/ar_tool_writability.hpp"

namespace qar_rviz_streaming
{
namespace
{

/// Item data roles: the fully-qualified name, its type and its kind ride along
/// on the item so a selection does not have to be mapped back through the tree.
constexpr int kNameRole = Qt::UserRole + 1;
constexpr int kTypeRole = Qt::UserRole + 2;
constexpr int kKindRole = Qt::UserRole + 3;

const RosEntityKind kKinds[] = {
  RosEntityKind::Topic, RosEntityKind::Service, RosEntityKind::Action};

/// Depth-first search for the item carrying @p key (kind + name).
QTreeWidgetItem *
find_entity_item(QTreeWidgetItem * item, const QString & key)
{
  const QString name = item->data(0, kNameRole).toString();
  if (!name.isEmpty()) {
    const auto kind = static_cast<RosEntityKind>(item->data(0, kKindRole).toInt());
    if (QString::fromLatin1(to_string(kind)) + "\n" + name == key) {
      return item;
    }
  }
  for (int i = 0; i < item->childCount(); ++i) {
    if (auto * hit = find_entity_item(item->child(i), key)) {
      return hit;
    }
  }
  return nullptr;
}

}  // namespace

QString
AddTopicDialog::selectionKey(RosEntityKind kind, const QString & name)
{
  return name.isEmpty() ? QString() : QString::fromLatin1(to_string(kind)) + "\n" + name;
}

AddTopicDialog::AddTopicDialog(
  std::vector<RosGraphEntry> entries, const DisplayClassCatalog & catalog, QWidget * parent)
: QDialog(parent),
  entries_(std::move(entries)),
  catalog_(catalog)
{
  setWindowTitle(tr("Add from the ROS graph"));
  setModal(true);
  resize(620, 520);

  auto * layout = new QVBoxLayout(this);

  auto * filter_row = new QHBoxLayout();
  filter_row->addWidget(new QLabel(tr("Filter:"), this));
  filter_edit_ = new QLineEdit(this);
  filter_edit_->setPlaceholderText(tr("name, type, or \"topic\" / \"service\" / \"action\""));
  filter_row->addWidget(filter_edit_, 1);
  auto * refresh_button = new QPushButton(tr("Refresh"), this);
  filter_row->addWidget(refresh_button);
  layout->addLayout(filter_row);

  tree_ = new QTreeWidget(this);
  tree_->setColumnCount(2);
  tree_->setHeaderLabels({tr("Name"), tr("Type")});
  tree_->header()->setStretchLastSection(true);
  tree_->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(tree_, 1);

  publish_check_ = new QCheckBox(tr("Allow publishing from AR"), this);
  publish_check_->setToolTip(
    tr("Ticked: the AR tool has a form and a Publish button that asks for a second press. "
    "The default comes from the type: a switch or velocity sliders can be used "
    "without a keyboard, anything else is read-only. Publishing to a topic is a command "
    "to a robot."));
  layout->addWidget(publish_check_);

  detail_label_ = new QLabel(this);
  detail_label_->setObjectName("add_topic_detail");
  detail_label_->setWordWrap(true);
  layout->addWidget(detail_label_);

  buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons_->button(QDialogButtonBox::Ok)->setText(tr("Add"));
  buttons_->button(QDialogButtonBox::Ok)->setToolTip(
    tr("Create the display (for a topic) and generate the AR tool."));
  display_only_button_ = buttons_->addButton(tr("Add display only"), QDialogButtonBox::ActionRole);
  display_only_button_->setToolTip(
    tr("Create the display and pin the topic to the watch list; no AR tool is generated."));
  layout->addWidget(buttons_);

  connect(filter_edit_, &QLineEdit::textChanged, this, &AddTopicDialog::onFilterChanged);
  connect(refresh_button, &QPushButton::clicked, this, &AddTopicDialog::refreshRequested);
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, &AddTopicDialog::onSelectionChanged);
  connect(tree_, &QTreeWidget::itemActivated, this, &AddTopicDialog::onItemActivated);
  connect(display_only_button_, &QPushButton::clicked, this, &AddTopicDialog::onAddDisplayOnly);
  connect(publish_check_, &QCheckBox::toggled, this, [this](bool) { updateSelectionState(); });
  connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);

  rebuildTree();
  updateSelectionState();
}

void
AddTopicDialog::setEntries(std::vector<RosGraphEntry> entries)
{
  // Remember where the operator was: a refresh that silently drops the selection is
  // worse than no refresh at all.
  pending_reselect_ = selectionKey(selection_.kind, selection_.name);
  entries_ = std::move(entries);
  rebuildTree();
  updateSelectionState();
}

void
AddTopicDialog::onFilterChanged(const QString &)
{
  pending_reselect_ = selectionKey(selection_.kind, selection_.name);
  rebuildTree();
  updateSelectionState();
}

void
AddTopicDialog::rebuildTree()
{
  tree_->clear();

  const auto kept = filter_graph_entries(entries_, filter_edit_ ? filter_edit_->text() : QString());
  for (const RosEntityKind kind : kKinds) {
    addKindGroup(kind, kept);
  }
  tree_->expandAll();
  tree_->resizeColumnToContents(0);

  if (pending_reselect_.isEmpty()) {
    return;
  }
  // Re-select by kind+name rather than by row: filtering reshuffles the rows,
  // and a topic and a service may share a name.
  for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
    if (auto * hit = find_entity_item(tree_->topLevelItem(i), pending_reselect_)) {
      tree_->setCurrentItem(hit);
      break;
    }
  }
  pending_reselect_.clear();
}

void
AddTopicDialog::addKindGroup(RosEntityKind kind, const std::vector<RosGraphEntry> & kept)
{
  const RosTopicNode root = build_graph_tree(kept, kind);
  if (root.children.empty()) {
    return;  // a group with nothing under it is noise
  }
  auto * group = new QTreeWidgetItem(tree_);
  group->setText(0, ros_entity_kind_label(kind));
  group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
  QFont bold = group->font(0);
  bold.setBold(true);
  group->setFont(0, bold);

  for (const auto & child : root.children) {
    addNode(kind, child, group);
  }
}

void
AddTopicDialog::addNode(
  RosEntityKind kind, const RosTopicNode & node, QTreeWidgetItem * parent_item)
{
  auto * item = new QTreeWidgetItem(parent_item);
  item->setText(0, node.segment);

  if (node.is_topic()) {
    // A node may be BOTH an entity and a namespace, so this is not an
    // else-branch of "has children".
    const QString type = node.types.front();
    item->setText(1, node.types.join(", "));
    item->setData(0, kNameRole, node.full_name);
    item->setData(0, kTypeRole, type);
    item->setData(0, kKindRole, static_cast<int>(kind));
    item->setToolTip(0, node.full_name);
  } else {
    // Namespace-only levels are not selectable targets.
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
  }

  for (const auto & child : node.children) {
    addNode(kind, child, item);
  }
}

void
AddTopicDialog::onSelectionChanged()
{
  updateSelectionState();
}

void
AddTopicDialog::onItemActivated(QTreeWidgetItem * item, int)
{
  // Double-click / Enter on a real entity is the same as pressing Add.
  if (item != nullptr && !item->data(0, kNameRole).toString().isEmpty()) {
    updateSelectionState();
    accept();
  }
}

void
AddTopicDialog::onAddDisplayOnly()
{
  display_only_ = true;
  updateSelectionState();
  if (selection_.empty()) {
    display_only_ = false;
    return;
  }
  accept();
}

void
AddTopicDialog::updateSelectionState()
{
  const QTreeWidgetItem * current = tree_->currentItem();
  const QString name = current == nullptr ? QString() : current->data(0, kNameRole).toString();

  selection_ = AddTopicSelection{};
  if (name.isEmpty()) {
    // box_key_ is kept: a rebuild of the tree (filter, refresh) clears the selection for a
    // moment and then selects the same entity again, and the box must survive that.
    display_only_ = false;
    detail_label_->setText(tr("Select a topic, service or action."));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    display_only_button_->setEnabled(false);
    publish_check_->setEnabled(false);
    return;
  }

  selection_.kind = static_cast<RosEntityKind>(current->data(0, kKindRole).toInt());
  selection_.name = name;
  selection_.message_type = current->data(0, kTypeRole).toString();
  const bool is_topic = selection_.kind == RosEntityKind::Topic;
  if (is_topic) {
    selection_.display_class = catalog_.best_class_for_message_type(selection_.message_type);
  }

  // "Add" always generates the AR tool; only a topic can be added as a display alone.
  const bool display_only = is_topic && display_only_;
  selection_.expose_as_ar_tool = !display_only;
  display_only_button_->setVisible(is_topic);
  // An image topic has no other way into AR than its tool, but its display is the choice of
  // "Add display only": it needs a display class that handles the type.
  display_only_button_->setEnabled(
    is_topic &&
    !(is_image_message_type(selection_.message_type.toStdString()) &&
    selection_.display_class.isEmpty()));

  // The publish box starts in the state the rule of the type gives, whenever the SELECTION
  // changes; a rebuild of the same selection keeps what the operator made of it. A service or
  // an action has its own request or goal form, so the box is off and disabled for it.
  const ArToolWriteDefault rule =
    default_write_for(selection_.kind, selection_.message_type.toStdString());
  const QString key = selectionKey(selection_.kind, name);
  const bool selection_changed = key != box_key_;
  box_key_ = key;
  const bool publishable = is_topic && selection_.expose_as_ar_tool;
  {
    QSignalBlocker block_publish(publish_check_);
    publish_check_->setEnabled(is_topic);
    if (!is_topic) {
      publish_check_->setChecked(false);
    } else if (selection_changed) {
      publish_check_->setChecked(rule.writable);
    }
  }
  selection_.allow_publish = publishable && publish_check_->isChecked();
  selection_.write_chosen = publishable && publish_check_->isChecked() != rule.writable;

  QStringList lines;
  lines << QString("%1 %2  [%3]")
    .arg(QString::fromLatin1(to_string(selection_.kind)), name, selection_.message_type);
  if (is_topic) {
    // The same pure decision onAddTopic acts on, so the summary never promises a
    // display the Add button will then not create.
    if (should_skip_display_for_exposed_topic(
        selection_.expose_as_ar_tool, selection_.message_type.toStdString(),
        selection_.display_class.toStdString()))
    {
      lines << tr("No RViz display will be created — the AR panel replaces it.");
    } else {
      lines << (selection_.display_class.isEmpty()
        ? tr("No RViz display handles %1 — it will be pinned to the watch list only.")
          .arg(selection_.message_type)
        : tr("Will create display: %1").arg(selection_.display_class));
    }
  }
  if (selection_.expose_as_ar_tool) {
    const QString reason = QString::fromStdString(rule.reason);
    if (!is_topic) {
      lines << tr("Will generate an AR tool with %1.").arg(reason);
    } else if (selection_.allow_publish) {
      lines << tr("Will generate an AR tool that can publish (Publish asks for a second press): %1")
        .arg(selection_.write_chosen ? tr("%1 (changed from the default)").arg(reason) : reason);
    } else {
      lines << tr("Will generate a read-only AR tool: %1")
        .arg(selection_.write_chosen ? tr("%1 (changed from the default)").arg(reason) : reason);
    }
  }
  detail_label_->setText(lines.join('\n'));
  buttons_->button(QDialogButtonBox::Ok)->setEnabled(true);
}

}  // namespace qar_rviz_streaming
