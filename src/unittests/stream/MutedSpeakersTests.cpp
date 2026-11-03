/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MutedSpeakersTests.h"

#include "stream/MutedSpeakers.h"

#include <QFile>
#include <QTest>

using namespace hopflow::stream;

namespace {

// Windows endpoint IDs, as IMMDevice::GetId gives them
const QString kSpeakers = QStringLiteral("{0.0.0.00000000}.{6b0e4f8a-2c1d-4e3f-9a8b-7c6d5e4f3a2b}");
const QString kHeadphones = QStringLiteral("{0.0.0.00000000}.{1f2e3d4c-5b6a-4978-8695-a4b3c2d1e0f9}");

} // namespace

QString MutedSpeakersTests::fileName(const QString &name) const
{
  return m_dir.filePath(name);
}

void MutedSpeakersTests::startsEmpty()
{
  QVERIFY(m_dir.isValid());
  QCOMPARE(MutedSpeakers(fileName(QStringLiteral("empty.ini"))).devices(), QStringList());
}

void MutedSpeakersTests::survivesRestart()
{
  const auto file = fileName(QStringLiteral("restart.ini"));
  QVERIFY(MutedSpeakers(file).add(kSpeakers));
  // a new instance stands for the next run, after a crash
  QCOMPARE(MutedSpeakers(file).devices(), QStringList{kSpeakers});
}

void MutedSpeakersTests::addsOnce()
{
  MutedSpeakers record(fileName(QStringLiteral("once.ini")));
  QVERIFY(record.add(kSpeakers));
  QVERIFY(record.add(kSpeakers));
  QCOMPARE(record.devices(), QStringList{kSpeakers});
}

void MutedSpeakersTests::removesOnlyThatDevice()
{
  const auto file = fileName(QStringLiteral("remove.ini"));
  MutedSpeakers record(file);
  QVERIFY(record.add(kSpeakers));
  QVERIFY(record.add(kHeadphones));

  record.remove(kSpeakers);
  QCOMPARE(MutedSpeakers(file).devices(), QStringList{kHeadphones});
  record.remove(kSpeakers);
  QCOMPARE(record.devices(), QStringList{kHeadphones});
  record.remove(kHeadphones);
  QCOMPARE(MutedSpeakers(file).devices(), QStringList());
}

void MutedSpeakersTests::reportsUnsavedRecord()
{
  // a file where the record's folder would have to be
  const auto blocker = fileName(QStringLiteral("blocker"));
  QFile file(blocker);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.close();

  QVERIFY(!MutedSpeakers(blocker + QStringLiteral("/state/muted.ini")).add(kSpeakers));
}

QTEST_MAIN(MutedSpeakersTests)
