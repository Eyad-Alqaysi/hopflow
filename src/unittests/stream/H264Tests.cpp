/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "H264Tests.h"

#include "stream/H264.h"

#include <QTest>

using namespace hopflow::stream::h264;

namespace {

// a real High profile level 4.0 SPS and its PPS
const QByteArray kSps = QByteArray::fromHex("6764002838a64c3c0144fbf0110000030010000003032e2c5a4c3c");
const QByteArray kPps = QByteArray::fromHex("68ee3cb0");

} // namespace

void H264Tests::avcCRoundTrip()
{
  const auto avcC = makeAvcC(ParameterSets{{kSps}, {kPps}});
  QCOMPARE(avcC[0], char(1));
  QCOMPARE(avcC[1], kSps[1]); // profile
  QCOMPARE(avcC[3], kSps[3]); // level

  const auto parsed = parseAvcC(avcC);
  QVERIFY(parsed);
  QCOMPARE(parsed->sps, QList<QByteArray>{kSps});
  QCOMPARE(parsed->pps, QList<QByteArray>{kPps});
}

void H264Tests::avcCRejectsGarbage_data()
{
  QTest::addColumn<QByteArray>("data");
  const auto good = makeAvcC(ParameterSets{{kSps}, {kPps}});
  QTest::newRow("empty") << QByteArray();
  QTest::newRow("wrong version") << QByteArray(1, char(2)) + good.mid(1);
  QTest::newRow("truncated") << good.left(good.size() - 2);
  QTest::newRow("no pps") << good.left(8 + kSps.size()) + QByteArray(1, char(0));
}

void H264Tests::avcCRejectsGarbage()
{
  QFETCH(QByteArray, data);
  QVERIFY(!parseAvcC(data).has_value());
}

void H264Tests::avccRoundTrip()
{
  const QList<QByteArray> nals = {QByteArray("\x65\x88\x84", 3), QByteArray("\x41\x9a", 2)};
  const auto joined = joinAvcc(nals);
  QCOMPARE(joined.size(), qsizetype(4 + 3 + 4 + 2));
  QCOMPARE(*splitAvcc(joined), nals);
}

void H264Tests::avccRejectsTruncated()
{
  auto joined = joinAvcc({QByteArray("\x65\x88\x84", 3)});
  QVERIFY(!splitAvcc(joined.left(joined.size() - 1)).has_value());
  QVERIFY(!splitAvcc(QByteArray("\0\0", 2)).has_value());
}

void H264Tests::annexBMixedStartCodes()
{
  // Media Foundation mixes 4 and 3 byte start codes
  const QByteArray stream = QByteArray("\0\0\0\1", 4) + kSps + QByteArray("\0\0\1", 3) + kPps +
                            QByteArray("\0\0\0\1", 4) + QByteArray("\x65\x88\x00\x84", 4);
  const auto nals = splitAnnexB(stream);
  QCOMPARE(nals.size(), 3);
  QCOMPARE(nals[0], kSps);
  QCOMPARE(nals[1], kPps);
  // a zero inside a NAL unit is not a start code
  QCOMPARE(nals[2], QByteArray("\x65\x88\x00\x84", 4));
  QCOMPARE(nalType(nals[0]), kNalSps);
  QCOMPARE(nalType(nals[1]), kNalPps);
  QCOMPARE(nalType(nals[2]), kNalIdr);
}

void H264Tests::convertsBothWays()
{
  const QList<QByteArray> nals = {kSps, kPps, QByteArray("\x65\x11\x22", 3)};
  QCOMPARE(splitAnnexB(joinAnnexB(nals)), nals);
  QCOMPARE(*splitAvcc(joinAvcc(splitAnnexB(joinAnnexB(nals)))), nals);
}

QTEST_MAIN(H264Tests)
