/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "VirtualDisplayModeTests.h"

#include "stream/VirtualDisplayMode.h"

#include <QTest>

using namespace hopflow::stream;

void VirtualDisplayModeTests::fpsIsCappedAt60_data()
{
  QTest::addColumn<int>("refreshHz");
  QTest::addColumn<int>("fps");

  QTest::newRow("unknown") << 0 << 60;
  QTest::newRow("30 Hz") << 30 << 30;
  QTest::newRow("50 Hz") << 50 << 50;
  QTest::newRow("60 Hz") << 60 << 60;
  QTest::newRow("144 Hz") << 144 << 60;
}

void VirtualDisplayModeTests::fpsIsCappedAt60()
{
  QFETCH(int, refreshHz);
  QFETCH(int, fps);
  QCOMPARE(extendFps(refreshHz), fps);
  QCOMPARE(virtualDisplayMode(DisplayInfo{1920, 1080, uint16_t(refreshHz), 100}).refreshHz, fps);
}

void VirtualDisplayModeTests::standardDensityIsOneToOne()
{
  for (const uint16_t scale : {uint16_t(100), uint16_t(150), uint16_t(199)}) {
    const auto mode = virtualDisplayMode(DisplayInfo{2560, 1440, 60, scale});
    QVERIFY(!mode.hiDpi);
    QCOMPARE(mode.pixels, QSize(2560, 1440));
    QCOMPARE(mode.logical, QSize(2560, 1440));
  }
}

void VirtualDisplayModeTests::doubleDensityIsHiDpi()
{
  const auto mode = virtualDisplayMode(DisplayInfo{3840, 2160, 60, 200});
  QVERIFY(mode.hiDpi);
  QCOMPARE(mode.pixels, QSize(3840, 2160));
  QCOMPARE(mode.logical, QSize(1920, 1080));

  QVERIFY(virtualDisplayMode(DisplayInfo{3840, 2160, 60, 250}).hiDpi);
}

void VirtualDisplayModeTests::sizeInMillimetersFollowsScaling()
{
  // 96 dpi at 100%: 1920 pixels are 20 inches
  const auto standard = virtualDisplayMode(DisplayInfo{1920, 1080, 60, 100});
  QCOMPARE(standard.millimeters, QSizeF(508.0, 285.75));

  // twice the density: the same pixels on half the width
  const auto dense = virtualDisplayMode(DisplayInfo{1920, 1080, 60, 200});
  QCOMPARE(dense.millimeters, QSizeF(254.0, 142.875));
}

void VirtualDisplayModeTests::oddSizesBecomeEven()
{
  const auto mode = virtualDisplayMode(DisplayInfo{1367, 769, 60, 200});
  QCOMPARE(mode.pixels, QSize(1366, 768));
  QCOMPARE(mode.logical, QSize(683, 384));
}

QTEST_MAIN(VirtualDisplayModeTests)
