// Copyright 2026 QuaternAR
// SPDX-License-Identifier: MIT
//
// Custom gtest entry point of the widget tests: a QApplication must exist before any QWidget is
// constructed. The offscreen platform plugin keeps the run headless: no window is ever created
// on the desktop, so running the suite cannot disturb a session. The plugin rasterizes no glyphs,
// so the tests assert on sizes that must not change, never on absolute metrics or pixels.

#include <gtest/gtest.h>

#include <QApplication>

int main(int argc, char ** argv)
{
  // Set before QApplication is constructed, and only if the caller has not chosen a platform.
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }

  QApplication app(argc, argv);
  (void)app;

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
