// Copyright 2026 Quaternar
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

  expose_check_ = new QCheckBox(tr("Expose as a QAR AR tool"), this);
  expose_check_->setToolTip(
    tr("Generate a panel from the type and open it in the headset. A service or "
    "an action can only be used this way — RViz has no display for either."));
  publish_check_ = new QCheckBox(tr("Allow publishing from AR"), this);
  publish_check_->setToolTip(
    tr("The AR panel also gets an editable form and a Publish button. Off by "
    "default: publishing to a topic is a robot command."));
  layout->addWidget(expose_check_);
  layout->addWidget(publish_check_);

  detail_label_ = new QLabel(this);
  detail_label_->setWordWrap(true);
  layout->addWidget(detail_label_);

  buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons_->button(QDialogButtonBox::Ok)->setText(tr("Add"));
  layout->addWidget(buttons_);

  connect(filter_edit_, &QLineEdit::textChanged, this, &AddTopicDialog::onFilterChanged);
  connect(refresh_button, &QPushButton::clicked, this, &AddTopicDialog::refreshRequested);
  connect(tree_, &QTreeWidget::itemSelectionChanged, this, &AddTopicDialog::onSelectionChanged);
  connect(tree_, &QTreeWidget::itemActivated, this, &AddTopicDialog::onItemActivated);
  connect(expose_check_, &QCheckBox::toggled, this, &AddTopicDialog::onExposeToggled);
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
AddTopicDialog::onExposeToggled(bool)
{
  updateSelectionState();
}

void
AddTopicDialog::updateSelectionState()
{
  const QTreeWidgetItem * current = tree_->currentItem();
  const QString name = current == nullptr ? QString() : current->data(0, kNameRole).toString();

  const AddTopicSelection previous = selection_;
  selection_ = AddTopicSelection{};
  if (name.isEmpty()) {
    detail_label_->setText(tr("Select a topic, service or action."));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    expose_check_->setEnabled(false);
    publish_check_->setEnabled(false);
    return;
  }

  selection_.kind = static_cast<RosEntityKind>(current->data(0, kKindRole).toInt());
  selection_.name = name;
  selection_.message_type = current->data(0, kTypeRole).toString();
  if (selection_.kind == RosEntityKind::Topic) {
    selection_.display_class = catalog_.best_class_for_message_type(selection_.message_type);
  }

  // A service or an action has no RViz display and nothing to pin, so exposing
  // it is the only thing "Add" can mean: forced on, and said so on the button.
  const bool forced = selection_.kind != RosEntityKind::Topic;
  QSignalBlocker block_expose(expose_check_);
  QSignalBlocker block_publish(publish_check_);
  expose_check_->setEnabled(!forced);
  if (forced) {
    expose_check_->setChecked(true);
  } else if (previous.kind != selection_.kind) {
    expose_check_->setChecked(false);  // back to a topic: a plain RViz display is the default
  }
  selection_.expose_as_ar_tool = expose_check_->isChecked();

  // Publishing only makes sense for an exposed topic; a service/action panel
  // always has its own request/goal form.
  const bool publishable =
    selection_.kind == RosEntityKind::Topic && selection_.expose_as_ar_tool;
  publish_check_->setEnabled(publishable);
  if (!publishable) {
    publish_check_->setChecked(false);
  }
  selection_.allow_publish = publishable && publish_check_->isChecked();

  QStringList lines;
  lines << QString("%1 %2  [%3]")
    .arg(QString::fromLatin1(to_string(selection_.kind)), name, selection_.message_type);
  if (selection_.kind == RosEntityKind::Topic) {
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
    lines << (selection_.allow_publish
      ? tr("Will generate an AR panel with a publish form.")
      : tr("Will generate a read-only AR panel."));
  }
  detail_label_->setText(lines.join('\n'));
  buttons_->button(QDialogButtonBox::Ok)->setEnabled(true);
}

}  // namespace qar_rviz_streaming
