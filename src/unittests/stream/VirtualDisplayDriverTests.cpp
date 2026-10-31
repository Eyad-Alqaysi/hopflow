/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "VirtualDisplayDriverTests.h"

#include "stream/VirtualDisplayDriver.h"

#include <QTest>

using namespace hopflow::stream::vdd;

namespace {

// the parts of the vdd_settings.xml the driver ships with that matter here
const QString kShipped = QStringLiteral(R"(<?xml version='1.0' encoding='utf-8'?>
<!-- Virtual Display Driver (HDR) - Minimal Configuration File -->
<vdd_settings>
    <monitors>
        <count>1</count>
    </monitors>
    <gpu>
        <friendlyname>default</friendlyname>
    </gpu>
    <global>
        <g_refresh_rate>60</g_refresh_rate>
        <g_refresh_rate>120</g_refresh_rate>
    </global>
    <resolutions>
        <resolution>
            <width>1920</width>
            <height>1080</height>
            <refresh_rate>60</refresh_rate>
        </resolution>
        <resolution>
            <width>2560</width>
            <height>1440</height>
            <refresh_rate>59.94</refresh_rate>
        </resolution>
    </resolutions>
    <logging>
        <logging>false</logging>
    </logging>
    <auto_resolutions>
        <edid_mode_filtering>
            <min_refresh_rate>24</min_refresh_rate>
        </edid_mode_filtering>
    </auto_resolutions>
</vdd_settings>
)");

const QString kVddMonitor =
    QStringLiteral("\\\\?\\DISPLAY#MTT1337#5&2b5a8c3&0&UID256#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}");
const QString kRealMonitor =
    QStringLiteral("\\\\?\\DISPLAY#DELA0F4#5&1a2b3c&0&UID4352#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}");

DisplaySource source(const QString &name, const QString &adapter, const QString &monitor, bool attached)
{
  DisplaySource s;
  s.deviceName = name;
  s.adapterId = adapter;
  s.monitorId = monitor;
  s.attached = attached;
  return s;
}

} // namespace

void VirtualDisplayDriverTests::readsShippedSettings()
{
  const auto settings = parseSettings(kShipped);
  QVERIFY(settings);
  QCOMPARE(settings->monitorCount, 1);
  const QList<Mode> expected{{1920, 1080, 60}, {2560, 1440, 60},  {1920, 1080, 60},
                             {2560, 1440, 60}, {1920, 1080, 120}, {2560, 1440, 120}};
  QCOMPARE(settings->modes, expected);
}

void VirtualDisplayDriverTests::offeredModeLeavesSettingsAlone()
{
  const Mode offered{2560, 1440, 120};
  QCOMPARE(settingsOffering(kShipped, offered), std::optional<QString>(kShipped));
}

void VirtualDisplayDriverTests::addsMissingMode()
{
  const Mode mode{3024, 1964, 120};
  const auto updated = settingsOffering(kShipped, mode);
  QVERIFY(updated);
  QVERIFY(updated->contains(QStringLiteral(
      "        <resolution>\n"
      "            <width>3024</width>\n"
      "            <height>1964</height>\n"
      "            <refresh_rate>120</refresh_rate>\n"
      "        </resolution>\n"
      "    </resolutions>"
  )));

  const auto settings = parseSettings(*updated);
  QVERIFY(settings);
  QVERIFY(settings->modes.contains(mode));
  // everything the user had is still offered
  const auto before = parseSettings(kShipped);
  for (const auto &old : before->modes) {
    QVERIFY(settings->modes.contains(old));
  }
}

void VirtualDisplayDriverTests::addsModeToEmptyResolutions_data()
{
  QTest::addColumn<QString>("xml");
  QTest::newRow("self closing") << QStringLiteral("<vdd_settings><resolutions/></vdd_settings>");
  QTest::newRow("one line") << QStringLiteral("<vdd_settings><resolutions></resolutions></vdd_settings>");
  QTest::newRow("absent") << QStringLiteral(
      "<vdd_settings>\n    <monitors><count>2</count></monitors>\n</vdd_settings>\n"
  );
}

void VirtualDisplayDriverTests::addsModeToEmptyResolutions()
{
  QFETCH(QString, xml);
  const Mode mode{1512, 982, 60};
  const auto updated = settingsOffering(xml, mode);
  QVERIFY(updated);
  const auto settings = parseSettings(*updated);
  QVERIFY(settings);
  QCOMPARE(settings->modes, QList<Mode>{mode});
}

void VirtualDisplayDriverTests::writesSettingsWhenMissing()
{
  const Mode mode{1512, 982, 120};
  const auto created = settingsOffering(QString(), mode);
  QVERIFY(created);
  const auto settings = parseSettings(*created);
  QVERIFY(settings);
  QCOMPARE(settings->monitorCount, 1);
  QCOMPARE(settings->modes, QList<Mode>{mode});
}

void VirtualDisplayDriverTests::rejectsBrokenSettings()
{
  QVERIFY(!parseSettings(QStringLiteral("<vdd_settings><resolutions>")));
  QVERIFY(!settingsOffering(QStringLiteral("<vdd_settings><count>1</vdd_settings>"), Mode{1920, 1080, 60}));
  // well formed, but nowhere to put a resolution
  QVERIFY(!settingsOffering(QStringLiteral("<other/>"), Mode{1920, 1080, 60}));
}

void VirtualDisplayDriverTests::recognisesHardwareIds()
{
  QVERIFY(isDriverHardwareId(QStringLiteral("Root\\MttVDD")));
  QVERIFY(isDriverHardwareId(QStringLiteral("ROOT\\MTTVDD")));
  QVERIFY(isDriverHardwareId(QStringLiteral("MttVDD")));
  QVERIFY(!isDriverHardwareId(QStringLiteral("Root\\MttVDD2")));
  QVERIFY(!isDriverHardwareId(QStringLiteral("PCI\\VEN_10DE&DEV_2684")));
}

void VirtualDisplayDriverTests::findsDriverSource()
{
  const QList<DisplaySource> sources{
      source(QStringLiteral("\\\\.\\DISPLAY1"), QStringLiteral("PCI\\VEN_10DE&DEV_2684"), kRealMonitor, true),
      source(QStringLiteral("\\\\.\\DISPLAY2"), QStringLiteral("Root\\MttVDD"), kVddMonitor, true),
      source(QStringLiteral("\\\\.\\DISPLAY3"), QStringLiteral("Root\\MttVDD"), kVddMonitor, false),
  };
  QCOMPARE(findDriverSource(sources), std::optional<qsizetype>(2));
  QCOMPARE(findDriverSource(sources.mid(0, 2)), std::optional<qsizetype>(1));

  // known by its monitor alone, whatever the adapter reports
  const QList<DisplaySource> byMonitor{source(QStringLiteral("\\\\.\\DISPLAY5"), QString(), kVddMonitor, false)};
  QCOMPARE(findDriverSource(byMonitor), std::optional<qsizetype>(0));
}

void VirtualDisplayDriverTests::ignoresDriverWithoutMonitor()
{
  const QList<DisplaySource> sources{
      source(QStringLiteral("\\\\.\\DISPLAY1"), QStringLiteral("PCI\\VEN_8086&DEV_A7A0"), kRealMonitor, true),
      source(QStringLiteral("\\\\.\\DISPLAY2"), QStringLiteral("Root\\MttVDD"), QString(), false),
  };
  QCOMPARE(findDriverSource(sources), std::nullopt);
}

void VirtualDisplayDriverTests::placesBesideDesktop()
{
  auto primary = source(QStringLiteral("\\\\.\\DISPLAY1"), QString(), kRealMonitor, true);
  primary.primary = true;
  primary.bounds = QRect(0, 0, 2560, 1440);
  auto left = source(QStringLiteral("\\\\.\\DISPLAY2"), QString(), kRealMonitor, true);
  left.bounds = QRect(-1920, 200, 1920, 1080);
  auto detached = source(QStringLiteral("\\\\.\\DISPLAY4"), QString(), kRealMonitor, false);
  detached.bounds = QRect(9000, 0, 800, 600);
  auto virtualDisplay = source(QStringLiteral("\\\\.\\DISPLAY3"), QStringLiteral("Root\\MttVDD"), kVddMonitor, true);
  virtualDisplay.bounds = QRect(2560, 0, 1920, 1080);

  const QList<DisplaySource> sources{primary, left, detached, virtualDisplay};
  QCOMPARE(besideDesktop(sources, QStringLiteral("\\\\.\\display3")), QPoint(2560, 0));

  auto right = source(QStringLiteral("\\\\.\\DISPLAY5"), QString(), kRealMonitor, true);
  right.bounds = QRect(2560, -300, 1080, 1920);
  QCOMPARE(besideDesktop(sources + QList<DisplaySource>{right}, QStringLiteral("\\\\.\\DISPLAY3")), QPoint(3640, 0));
}

QTEST_MAIN(VirtualDisplayDriverTests)
