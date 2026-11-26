// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/qar_panel_ar_copy.hpp"

#include <algorithm>
#include <cmath>

#include <QByteArray>
#include <QFont>
#include <QVariant>

#include "qar_qt_streaming/enlarged_controls_style.hpp"
#include "qar_rviz_streaming/ar_panel_list_widget.hpp"
#include "qar_rviz_streaming/hub_connection_widget.hpp"

namespace qar_rviz_streaming
{

namespace
{

/// The dynamic properties that keep the font of a copy root as it was before its text was scaled.
constexpr const char * kBaseFontProperty = "qar_ar_copy_base_font";
constexpr const char * kBaseFontSetProperty = "qar_ar_copy_base_font_set";

}  // namespace

void apply_ar_copy_text(QWidget & root, double scale, ArCopyHinting hinting)
{
  if (!root.property(kBaseFontProperty).isValid()) {
    root.setProperty(kBaseFontProperty, root.font());
    root.setProperty(kBaseFontSetProperty, root.testAttribute(Qt::WA_SetFont));
  }
  QFont font = root.property(kBaseFontProperty).value<QFont>();
  if (font.pointSizeF() > 0.0) {
    font.setPointSizeF(font.pointSizeF() * scale);
  } else if (font.pixelSize() > 0) {
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(font.pixelSize() * scale))));
  }
  font.setHintingPreference(
    hinting == ArCopyHinting::None ? QFont::PreferNoHinting : QFont::PreferDefaultHinting);
  root.setFont(font);
}

void remove_ar_copy_text(QWidget & root)
{
  const QVariant base = root.property(kBaseFontProperty);
  if (!base.isValid()) {
    return;
  }
  if (root.property(kBaseFontSetProperty).toBool()) {
    root.setFont(base.value<QFont>());
  } else {
    root.setFont(QFont());
  }
  root.setProperty(kBaseFontProperty, QVariant());
  root.setProperty(kBaseFontSetProperty, QVariant());
}

ArCopyText ar_copy_text_from_environment(bool enlarged)
{
  ArCopyText text;
  if (enlarged) {
    text.scale = ar_copy_text_scale(qgetenv(kArCopyTextScaleEnv).constData());
    text.hinting = ar_copy_font_hinting(qgetenv(kArCopyFontHintingEnv).constData());
  }
  return text;
}

ArCopyText set_ar_copy_text(QWidget & root, bool enlarged)
{
  const ArCopyText text = ar_copy_text_from_environment(enlarged);
  if (enlarged) {
    apply_ar_copy_text(root, text.scale, text.hinting);
  } else {
    remove_ar_copy_text(root);
  }
  return text;
}

bool ArDialogCopyText::give(QWidget & dialog)
{
  if (qar_qt_streaming::enlarged_controls_style_of(&dialog) == nullptr) {
    return false;
  }
  for (const Entry & entry : entries_) {
    if (entry.dialog.data() == &dialog) {
      return false;  // offered already; sync() follows the style
    }
  }
  entries_.erase(
    std::remove_if(
      entries_.begin(), entries_.end(), [](const Entry & entry) {return entry.dialog.isNull();}),
    entries_.end());
  Entry entry;
  entry.dialog = &dialog;
  entry.scale = set_ar_copy_text(dialog, true).scale;
  entry.applied = true;
  entries_.push_back(entry);
  return true;
}

int ArDialogCopyText::sync()
{
  entries_.erase(
    std::remove_if(
      entries_.begin(), entries_.end(), [](const Entry & entry) {return entry.dialog.isNull();}),
    entries_.end());
  int changed = 0;
  for (Entry & entry : entries_) {
    const bool styled =
      qar_qt_streaming::enlarged_controls_style_of(entry.dialog.data()) != nullptr;
    if (styled && !entry.applied) {
      entry.scale = set_ar_copy_text(*entry.dialog, true).scale;
      entry.applied = true;
      ++changed;
    } else if (!styled && entry.applied) {
      set_ar_copy_text(*entry.dialog, false);
      entry.applied = false;
      ++changed;
    }
  }
  return changed;
}

double ArDialogCopyText::text_scale_of(const QWidget & dialog) const
{
  for (const Entry & entry : entries_) {
    if (entry.dialog.data() == &dialog) {
      return entry.applied ? entry.scale : 0.0;
    }
  }
  return 0.0;
}

QarPanelArCopy::QarPanelArCopy(
  const QarPanelView & original, const QarPanelViewInputs & inputs, QStyle * style)
: QWidget(nullptr)
{
  setObjectName(QStringLiteral("qar_panel_ar_copy"));
  setWindowTitle(QStringLiteral("Qar Streaming (AR copy)"));
  set_ar_copy_text(*this, style != nullptr);
  if (style != nullptr) {
    qar_qt_streaming::apply_style_to_tree(this, style);
  }
  QarPanelViewInputs copy_inputs = inputs;
  copy_inputs.rows_by_style = true;
  view_ = build_qar_panel_view(*this, copy_inputs);
  bound_ = bind_qar_panel_views(binder_, original, view_);
}

QarPanelArCopy::~QarPanelArCopy()
{
  binder_.release();
}

bool QarPanelArCopy::complete() const
{
  return bound_ == qar_panel_view_bound_count();
}

int QarPanelArCopy::sync()
{
  return binder_.sync();
}

QSize QarPanelArCopy::stable_size() const
{
  if (!stable_size_.isValid()) {
    stable_size_ = compute_stable_hints(
      *view_.tabs, view_.page_contents, *view_.status_strip, kPanelArCopyMaxHeightPx, 0).hint;
  }
  return stable_size_;
}

PageShortfall QarPanelArCopy::shortfall() const
{
  return page_shortfall(*view_.tabs, view_.page_contents);
}

void QarPanelArCopy::set_style(QStyle * style)
{
  // The text first: the style centres a form label against the field's size hint, which depends
  // on the font the field has when the style polishes it.
  set_ar_copy_text(*this, style != nullptr);
  if (style != nullptr) {
    qar_qt_streaming::apply_style_to_tree(this, style);
  } else {
    qar_qt_streaming::remove_style_from_tree(this);
  }
  remeasure_minimum_rows(*this);
  // The two composite lists measure their rows themselves.
  if (view_.ar_panel_list != nullptr) {
    view_.ar_panel_list->remeasure_rows();
  }
  if (view_.hub_widget != nullptr) {
    view_.hub_widget->remeasure_rows();
  }
  stable_size_ = QSize();
}

}  // namespace qar_rviz_streaming
