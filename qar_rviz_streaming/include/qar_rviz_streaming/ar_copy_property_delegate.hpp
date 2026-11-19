// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_COPY_PROPERTY_DELEGATE_HPP_
#define QAR_RVIZ_STREAMING__AR_COPY_PROPERTY_DELEGATE_HPP_

/// @file ar_copy_property_delegate.hpp
/// @brief The item delegate that the property trees of an enlarged AR copy of an RViz panel
/// (Displays, Views, Tool Properties, Selection) use. Needs rviz_common.
///
/// It is RViz's own delegate for everything, the editors and every property but one, and draws
/// the value of a colour property with the text centred in the row (ar_copy_color_row.hpp)
/// where RViz draws it at the top. The trees of the panels on the desktop are not touched.

#include <QWidget>

#include "rviz_common/properties/property_tree_delegate.hpp"

namespace qar_rviz_streaming
{

/// @brief RViz's property tree delegate with a colour row whose text is centred.
class ArCopyPropertyDelegate : public rviz_common::properties::PropertyTreeDelegate
{
public:
  explicit ArCopyPropertyDelegate(QObject * parent_object = nullptr);

  void paint(
    QPainter * painter, const QStyleOptionViewItem & option,
    const QModelIndex & index) const override;
};

/// @brief Gives the value column of every property tree below @p root the
/// @ref ArCopyPropertyDelegate; the delegate of the tree is kept and comes back with
/// @ref remove_ar_copy_property_delegate. A tree that has one already is left alone.
/// @return the number of trees changed. Qt GUI thread only.
int install_ar_copy_property_delegate(QWidget & root);

/// @brief Puts the delegate of every property tree below @p root back as it was before
/// @ref install_ar_copy_property_delegate. @return the number of trees changed.
int remove_ar_copy_property_delegate(QWidget & root);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_COPY_PROPERTY_DELEGATE_HPP_
