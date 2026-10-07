/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "IpcEncodingTests.h"

#include "common/IpcEncoding.h"

#include <QTest>

void IpcEncodingTests::roundTrip_data()
{
  QTest::addColumn<QStringList>("items");

  QTest::newRow("empty") << QStringList{};
  QTest::newRow("paths") << QStringList{
      QStringLiteral("drop"), QStringLiteral("C:\\Users\\me\\Downloads\\Hopflow\\a.txt")
  };
  QTest::newRow("delimiters") << QStringList{
      QStringLiteral("a=b"), QStringLiteral("line\nbreak"), QStringLiteral("==")
  };
  QTest::newRow("unicode") << QStringList{QStringLiteral("/Users/me/写真 (1).jpg")};
}

void IpcEncodingTests::roundTrip()
{
  QFETCH(QStringList, items);

  const auto encoded = encodeIpcList(items);
  // the IPC layer frames on newlines and splits on '='
  QVERIFY(!encoded.contains(QLatin1Char('=')));
  QVERIFY(!encoded.contains(QLatin1Char('\n')));
  QCOMPARE(decodeIpcList(encoded), items);
}

void IpcEncodingTests::malformed()
{
  QVERIFY(decodeIpcList(QStringLiteral("not base64 !")).isEmpty());
  QVERIFY(decodeIpcList(QString()).isEmpty());
}

QTEST_MAIN(IpcEncodingTests)
