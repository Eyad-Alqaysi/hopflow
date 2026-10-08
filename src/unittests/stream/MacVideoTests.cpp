/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MacVideoTests.h"

#include "stream/H264.h"
#include "stream/MacVideo.h"
#include "stream/TestPattern.h"

#include <QMutex>
#include <QTest>

using namespace hopflow::stream;

namespace {

struct Encoded
{
  QMutex mutex;
  QList<VideoConfig> configs;
  QList<EncodedVideo> frames;
};

//! Average of each channel over a block, to compare pictures despite compression
QColor averageColor(const QImage &image, QRect area)
{
  qint64 r = 0;
  qint64 g = 0;
  qint64 b = 0;
  for (int y = area.top(); y <= area.bottom(); ++y) {
    for (int x = area.left(); x <= area.right(); ++x) {
      const auto pixel = image.pixel(x, y);
      r += qRed(pixel);
      g += qGreen(pixel);
      b += qBlue(pixel);
    }
  }
  const auto count = qint64(area.width()) * area.height();
  return QColor(int(r / count), int(g / count), int(b / count));
}

int distance(QColor a, QColor b)
{
  return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}

} // namespace

void MacVideoTests::encodeAndDecode()
{
  const QSize size(640, 360);
  Encoded out;
  MacH264Encoder encoder;
  QVERIFY(encoder.open(
      size, 30,
      [&](const VideoConfig &config) {
        QMutexLocker lock(&out.mutex);
        out.configs.append(config);
      },
      [&](const EncodedVideo &frame) {
        QMutexLocker lock(&out.mutex);
        out.frames.append(frame);
      }
  ));

  QList<QImage> sources;
  for (int i = 0; i < 30; ++i) {
    sources.append(TestPatternSource::render(size, i * 33));
    encoder.encode(sources.last(), i * 33333);
  }
  encoder.finish();

  QMutexLocker lock(&out.mutex);
  QCOMPARE(out.frames.size(), 30);
  QCOMPARE(out.configs.size(), 1);
  QVERIFY(out.frames.first().keyframe);
  QCOMPARE(out.configs.first().width, uint16_t(640));
  QCOMPARE(out.configs.first().height, uint16_t(360));
  QVERIFY(h264::parseAvcC(out.configs.first().codecData).has_value());
  // frames are length prefixed NAL units
  QVERIFY(h264::splitAvcc(out.frames.first().data).has_value());
  // timestamps start at zero and increase
  QCOMPARE(out.frames.first().timestampUs, int64_t(0));
  QVERIFY(out.frames.last().timestampUs > out.frames.first().timestampUs);

  MacVideoDecoder decoder;
  QVERIFY(decoder.configure(out.configs.first()));
  for (int i = 0; i < out.frames.size(); ++i) {
    const auto image = decoder.decode(out.frames[i]);
    QVERIFY2(!image.isNull(), qPrintable(QStringLiteral("frame %1 did not decode").arg(i)));
    QCOMPARE(image.size(), size);

    // the colour bars survive compression
    const QRect whiteBar(10, 10, 60, 80);
    const QRect redBar(5 * size.width() / 7 + 10, 10, 60, 80);
    QVERIFY(distance(averageColor(image, whiteBar), averageColor(sources[i], whiteBar)) < 40);
    const auto got = averageColor(image, redBar);
    const auto want = averageColor(sources[i], redBar);
    QVERIFY2(
        distance(got, want) < 40, qPrintable(QStringLiteral("red bar is %1, expected %2").arg(got.name(), want.name()))
    );
  }
}

void MacVideoTests::keyframeOnRequest()
{
  const QSize size(320, 180);
  Encoded out;
  MacH264Encoder encoder;
  QVERIFY(encoder.open(
      size, 30, [&](const VideoConfig &) {},
      [&](const EncodedVideo &frame) {
        QMutexLocker lock(&out.mutex);
        out.frames.append(frame);
      }
  ));

  for (int i = 0; i < 10; ++i) {
    if (i == 6) {
      encoder.requestKeyframe();
    }
    encoder.encode(TestPatternSource::render(size, i * 33), i * 33333);
  }
  encoder.finish();

  QMutexLocker lock(&out.mutex);
  QCOMPARE(out.frames.size(), 10);
  QVERIFY(out.frames[0].keyframe);
  QVERIFY(!out.frames[3].keyframe);
  QVERIFY(out.frames[6].keyframe);
}

void MacVideoTests::decoderRejectsBadConfig()
{
  MacVideoDecoder decoder;
  QVERIFY(!decoder.configure(VideoConfig{VideoCodec::H264, 640, 360, 30, QByteArray("garbage")}));
  QVERIFY(!decoder.configure(VideoConfig{VideoCodec::Jpeg, 640, 360, 30, {}}));
  QVERIFY(decoder.decode(EncodedVideo{0, true, QByteArray("x")}).isNull());
}

QTEST_MAIN(MacVideoTests)
