// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// The text of a colour row of an enlarged AR copy's property tree (ar_copy_color_row.hpp). The
// offscreen platform has no fonts, so no glyph is ever drawn there; the tests record where the
// painter is asked to put the text (a paint engine that keeps the baseline of every text line)
// and judge that against the metrics of the font: the line of text sits in the middle of a row
// that is taller than the text, where a plain drawText puts it at the top.

#include "qar_rviz_streaming/ar_copy_color_row.hpp"

#include <gtest/gtest.h>

#include <QFont>
#include <QFontMetrics>
#include <QList>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPointF>
#include <QRect>
#include <QString>

namespace
{

/// A paint engine that draws nothing and keeps the baseline origin of every text line.
class TextRecorder : public QPaintEngine
{
public:
  TextRecorder()
  : QPaintEngine(QPaintEngine::AllFeatures) {}
  bool begin(QPaintDevice *) override {return true;}
  bool end() override {return true;}
  void updateState(const QPaintEngineState &) override {}
  void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override {}
  void drawTextItem(const QPointF & origin, const QTextItem &) override
  {
    origins.append(origin);
  }
  Type type() const override {return QPaintEngine::User;}

  QList<QPointF> origins;
};

class RecordingDevice : public QPaintDevice
{
public:
  QPaintEngine * paintEngine() const override {return &engine;}

  const QList<QPointF> & origins() const {return engine.origins;}

protected:
  int metric(PaintDeviceMetric what) const override
  {
    switch (what) {
      case PdmWidth:
      case PdmHeight:
        return 2000;
      case PdmWidthMM:
      case PdmHeightMM:
        return 500;
      case PdmDpiX:
      case PdmDpiY:
      case PdmPhysicalDpiX:
      case PdmPhysicalDpiY:
        return 96;
      case PdmDepth:
        return 32;
      case PdmDevicePixelRatio:
        return 1;
      case PdmDevicePixelRatioScaled:
        return 65536;
      default:
        return 0;
    }
  }

private:
  mutable TextRecorder engine;
};

/// The baseline origin of the text that the painter was asked to draw in @p row.
QPointF drawn_origin(RecordingDevice & device, const QFont & font, const QRect & row, bool plain)
{
  QPainter painter(&device);
  painter.setFont(font);
  if (plain) {
    // What RViz's own colour row does: the text rectangle without an alignment.
    painter.drawText(qar_rviz_streaming::color_row_text_rect(row), QStringLiteral("0123456789"));
  } else {
    qar_rviz_streaming::paint_color_row_text(&painter, row, QStringLiteral("0123456789"));
  }
  painter.end();
  return device.origins().isEmpty() ? QPointF(-1, -1) : device.origins().last();
}

TEST(ArCopyColorRow, TheTextRectIsTheRowWithoutTheColourBoxAndTheGap)
{
  const QRect row(10, 20, 300, 60);
  const QRect text = qar_rviz_streaming::color_row_text_rect(row);
  EXPECT_EQ(text.left(), 10 + 60 + qar_rviz_streaming::kColorRowTextGapPx);
  EXPECT_EQ(text.right(), row.right());
  EXPECT_EQ(text.top(), row.top());
  EXPECT_EQ(text.height(), row.height());
}

TEST(ArCopyColorRow, TheLineOfTextIsCentredInATallRowAndStartsRightOfTheColourBox)
{
  QFont font;
  font.setPixelSize(16);
  const QFontMetrics metrics(font);
  for (const int height : {24, 60, 90}) {
    RecordingDevice device;
    const QRect row(5, 7, 400, height);
    const QPointF origin = drawn_origin(device, font, row, false);
    ASSERT_GE(origin.y(), 0.0) << "nothing was drawn, row height " << height;
    // The line box runs from the ascent above the baseline to the descent below it.
    const double top = origin.y() - metrics.ascent();
    const double bottom = origin.y() + metrics.descent();
    const double row_centre = row.top() + height / 2.0;
    EXPECT_NEAR((top + bottom) / 2.0, row_centre, 1.5) << "row height " << height;
    EXPECT_GE(origin.x(), row.left() + height + qar_rviz_streaming::kColorRowTextGapPx)
      << "row height " << height;
  }
}

TEST(ArCopyColorRow, APlainDrawTextPutsTheLineAtTheTopOfATallRow)
{
  // What RViz's own colour row does, so that the test above can fail.
  QFont font;
  font.setPixelSize(16);
  const QFontMetrics metrics(font);
  RecordingDevice device;
  const QRect row(0, 0, 400, 90);
  const QPointF origin = drawn_origin(device, font, row, true);
  ASSERT_GE(origin.y(), 0.0);
  const double centre = origin.y() - metrics.ascent() + metrics.height() / 2.0;
  EXPECT_LT(centre, row.height() / 2.0 - 20.0);
}

}  // namespace
