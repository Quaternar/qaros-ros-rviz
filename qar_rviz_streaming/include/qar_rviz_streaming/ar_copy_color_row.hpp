// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT

#ifndef QAR_RVIZ_STREAMING__AR_COPY_COLOR_ROW_HPP_
#define QAR_RVIZ_STREAMING__AR_COPY_COLOR_ROW_HPP_

/// @file ar_copy_color_row.hpp
/// @brief The text of a colour row in the property tree of an enlarged AR copy. Qt GUI only: no
/// RViz.
///
/// RViz draws the value of a colour property as a colour box of the row's height and, to its
/// right, the text with the alignment of a plain `drawText`, which is the top of the row. In a
/// row that the enlarged-controls style made taller than the text the text then sits at the top
/// of the row while the text of every other row is centred. The copy's property trees draw the
/// text of a colour row with this function instead.

#include <QPainter>
#include <QRect>
#include <QString>

namespace qar_rviz_streaming
{

/// The space between the colour box and the text, in pixels.
constexpr int kColorRowTextGapPx = 4;

/// @brief The rectangle the text of a colour row gets: the row without the colour box (as wide
/// as the row is high) and the gap, over the whole height of the row.
QRect color_row_text_rect(const QRect & row);

/// @brief Draws @p text left-aligned and vertically centred in @ref color_row_text_rect of
/// @p row with the pen and the font that @p painter has.
void paint_color_row_text(QPainter * painter, const QRect & row, const QString & text);

}  // namespace qar_rviz_streaming

#endif  // QAR_RVIZ_STREAMING__AR_COPY_COLOR_ROW_HPP_
