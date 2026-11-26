// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#include "qar_rviz_streaming/ar_copy_color_row.hpp"

namespace qar_rviz_streaming
{

QRect color_row_text_rect(const QRect & row)
{
  QRect rect = row;
  rect.setLeft(row.left() + row.height() + kColorRowTextGapPx);
  return rect;
}

void paint_color_row_text(QPainter * painter, const QRect & row, const QString & text)
{
  painter->drawText(color_row_text_rect(row), Qt::AlignLeft | Qt::AlignVCenter, text);
}

}  // namespace qar_rviz_streaming
