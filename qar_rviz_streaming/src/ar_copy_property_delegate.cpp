// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_copy_property_delegate.hpp"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QPointer>
#include <QVariant>

#include "qar_rviz_streaming/ar_copy_color_row.hpp"
#include "rviz_common/properties/color_editor.hpp"
#include "rviz_common/properties/color_property.hpp"
#include "rviz_common/properties/property_tree_widget.hpp"

namespace qar_rviz_streaming
{

namespace
{

/// The value column of RViz's property tree, which the delegate is set for.
constexpr int kValueColumn = 1;
/// The dynamic property that keeps the delegate the tree had before.
constexpr const char * kKeptDelegateProperty = "qar_ar_copy_kept_delegate";

}  // namespace

ArCopyPropertyDelegate::ArCopyPropertyDelegate(QObject * parent_object)
: rviz_common::properties::PropertyTreeDelegate(parent_object)
{
}

void ArCopyPropertyDelegate::paint(
  QPainter * painter, const QStyleOptionViewItem & option, const QModelIndex & index) const
{
  using rviz_common::properties::ColorProperty;
  using rviz_common::properties::Property;
  // The same lookup as RViz's delegate: the model hands the property out as the index pointer.
  const auto * property = qobject_cast<const ColorProperty *>(
    static_cast<const Property *>(index.internalPointer()));
  if (property == nullptr) {
    rviz_common::properties::PropertyTreeDelegate::paint(painter, option, index);
    return;
  }
  painter->save();
  QColor color = property->getColor();
  if (!(property->getViewFlags(0) & Qt::ItemIsEnabled)) {
    color = QColor(200, 200, 200);
    painter->setPen(QColor(Qt::lightGray));
  }
  rviz_common::properties::ColorEditor::paintColorBox(painter, option.rect, color);
  paint_color_row_text(painter, option.rect, property->getValue().toString());
  painter->restore();
}

int install_ar_copy_property_delegate(QWidget & root)
{
  int changed = 0;
  const auto trees = root.findChildren<rviz_common::properties::PropertyTreeWidget *>();
  for (rviz_common::properties::PropertyTreeWidget * tree : trees) {
    if (tree->property(kKeptDelegateProperty).isValid()) {
      continue;
    }
    QAbstractItemDelegate * const kept = tree->itemDelegateForColumn(kValueColumn);
    tree->setProperty(kKeptDelegateProperty, QVariant::fromValue(static_cast<QObject *>(kept)));
    tree->setItemDelegateForColumn(kValueColumn, new ArCopyPropertyDelegate(tree));
    ++changed;
  }
  return changed;
}

int remove_ar_copy_property_delegate(QWidget & root)
{
  int changed = 0;
  const auto trees = root.findChildren<rviz_common::properties::PropertyTreeWidget *>();
  for (rviz_common::properties::PropertyTreeWidget * tree : trees) {
    const QVariant kept = tree->property(kKeptDelegateProperty);
    if (!kept.isValid()) {
      continue;
    }
    QAbstractItemDelegate * const own = tree->itemDelegateForColumn(kValueColumn);
    tree->setItemDelegateForColumn(
      kValueColumn, qobject_cast<QAbstractItemDelegate *>(kept.value<QObject *>()));
    if (dynamic_cast<ArCopyPropertyDelegate *>(own) != nullptr) {
      own->deleteLater();
    }
    tree->setProperty(kKeptDelegateProperty, QVariant());
    ++changed;
  }
  return changed;
}

}  // namespace qar_rviz_streaming
