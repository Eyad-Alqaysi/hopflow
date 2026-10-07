/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CtrlCmdSwapTests.h"

#include "common/KeyModifierID.h"
#include "server/CtrlCmdSwap.h"

#include <QTest>

namespace {

//! Final mapping for \p option once the client applied \p options in order
uint32_t effectiveMapping(const OptionsList &options, OptionID option, uint32_t identity)
{
  uint32_t value = identity;
  for (size_t i = 0; i + 1 < options.size(); i += 2) {
    if (options[i] == option) {
      value = options[i + 1];
    }
  }
  return value;
}

OptionsList identityModifiers()
{
  return {kOptionModifierMapForControl, kKeyModifierIDControl,    kOptionModifierMapForSuper,
          kKeyModifierIDSuper,          kOptionModifierMapForAlt, kKeyModifierIDAlt};
}

} // namespace

void CtrlCmdSwapTests::platformPairs_data()
{
  QTest::addColumn<int>("server");
  QTest::addColumn<int>("client");
  QTest::addColumn<bool>("swapped");

  using enum PeerPlatform;
  QTest::newRow("windows server, mac client") << int(Windows) << int(MacOS) << true;
  QTest::newRow("mac server, windows client") << int(MacOS) << int(Windows) << true;
  QTest::newRow("linux server, mac client") << int(Linux) << int(MacOS) << true;
  QTest::newRow("mac server, linux client") << int(MacOS) << int(Linux) << true;
  QTest::newRow("windows server, windows client") << int(Windows) << int(Windows) << false;
  QTest::newRow("mac server, mac client") << int(MacOS) << int(MacOS) << false;
  QTest::newRow("windows server, linux client") << int(Windows) << int(Linux) << false;
  QTest::newRow("mac server, deskflow client") << int(MacOS) << int(Unknown) << false;
}

void CtrlCmdSwapTests::platformPairs()
{
  QFETCH(int, server);
  QFETCH(int, client);
  QFETCH(bool, swapped);

  auto options = identityModifiers();
  QCOMPARE(
      applyAutoCtrlCmdSwap(options, static_cast<PeerPlatform>(server), static_cast<PeerPlatform>(client)), swapped
  );

  const auto ctrl = effectiveMapping(options, kOptionModifierMapForControl, kKeyModifierIDControl);
  const auto super = effectiveMapping(options, kOptionModifierMapForSuper, kKeyModifierIDSuper);
  QCOMPARE(ctrl, swapped ? kKeyModifierIDSuper : kKeyModifierIDControl);
  QCOMPARE(super, swapped ? kKeyModifierIDControl : kKeyModifierIDSuper);
  QCOMPARE(effectiveMapping(options, kOptionModifierMapForAlt, kKeyModifierIDAlt), kKeyModifierIDAlt);
}

void CtrlCmdSwapTests::explicitMappingWins_data()
{
  QTest::addColumn<uint32_t>("option");
  QTest::addColumn<uint32_t>("value");

  QTest::newRow("ctrl mapped to alt") << kOptionModifierMapForControl << kKeyModifierIDAlt;
  QTest::newRow("super mapped to meta") << kOptionModifierMapForSuper << kKeyModifierIDMeta;
  QTest::newRow("user already swapped") << kOptionModifierMapForControl << kKeyModifierIDSuper;
}

void CtrlCmdSwapTests::explicitMappingWins()
{
  QFETCH(uint32_t, option);
  QFETCH(uint32_t, value);

  auto options = identityModifiers();
  for (size_t i = 0; i < options.size(); i += 2) {
    if (options[i] == option) {
      options[i + 1] = value;
    }
  }
  const auto before = options;

  QVERIFY(!applyAutoCtrlCmdSwap(options, PeerPlatform::Windows, PeerPlatform::MacOS));
  QCOMPARE(options, before);
}

void CtrlCmdSwapTests::swapOverridesIdentityMapping()
{
  // no per-computer mappings at all, e.g. a computer missing from the config
  OptionsList options;
  QVERIFY(applyAutoCtrlCmdSwap(options, PeerPlatform::MacOS, PeerPlatform::Windows));
  QCOMPARE(effectiveMapping(options, kOptionModifierMapForControl, kKeyModifierIDControl), kKeyModifierIDSuper);
  QCOMPARE(effectiveMapping(options, kOptionModifierMapForSuper, kKeyModifierIDSuper), kKeyModifierIDControl);
}

QTEST_MAIN(CtrlCmdSwapTests)
