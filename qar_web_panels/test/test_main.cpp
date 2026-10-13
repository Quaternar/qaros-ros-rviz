// Copyright 2026 Quaternar
// SPDX-License-Identifier: MIT
//
// Custom gtest entry point: the server and the test clients are Qt socket
// objects, so an event loop must exist. QCoreApplication is enough — nothing
// here touches a window or a widget, so no platform plugin is needed.

#include <gtest/gtest.h>

#include <QCoreApplication>

int
main(int argc, char ** argv)
{
  QCoreApplication app(argc, argv);
  (void)app;

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
