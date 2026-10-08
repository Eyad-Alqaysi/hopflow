/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StreamProtocolTests.h"

#include "stream/RateController.h"
#include "stream/StreamPreset.h"
#include "stream/StreamProtocol.h"

#include <QTest>

using namespace hopflow::stream;

void StreamProtocolTests::frameRoundTrip()
{
  const Frame frame{FrameType::VideoFrame, 1234567890123, QByteArray("payload")};
  const auto bytes = encodeFrame(frame);
  QCOMPARE(bytes.size(), qsizetype(kHeaderSize + 7));

  FrameParser parser;
  parser.feed(bytes);
  const auto parsed = parser.next();
  QVERIFY(parsed.has_value());
  QCOMPARE(parsed->type, FrameType::VideoFrame);
  QCOMPARE(parsed->timestampUs, frame.timestampUs);
  QCOMPARE(parsed->payload, frame.payload);
  QVERIFY(!parser.next().has_value());
}

void StreamProtocolTests::parserHandlesSplitAndJoinedFrames()
{
  const auto first = encodeFrame(Frame{FrameType::Hello, 0, QByteArray(1000, 'a')});
  const auto second = encodeFrame(Frame{FrameType::Bye, 5, {}});
  const auto stream = first + second;

  // one byte at a time, as a slow network delivers it
  FrameParser parser;
  QList<Frame> frames;
  for (const char byte : stream) {
    parser.feed(QByteArray(1, byte));
    while (auto frame = parser.next()) {
      frames.append(*frame);
    }
  }
  QCOMPARE(frames.size(), 2);
  QCOMPARE(frames[0].payload, QByteArray(1000, 'a'));
  QCOMPARE(frames[1].type, FrameType::Bye);
  QCOMPARE(frames[1].timestampUs, int64_t(5));
}

void StreamProtocolTests::parserRejectsUnknownType()
{
  auto bytes = encodeFrame(Frame{FrameType::Hello, 0, {}});
  bytes[0] = char(99);
  FrameParser parser;
  parser.feed(bytes);
  QVERIFY(!parser.next().has_value());
  QVERIFY(parser.hasError());
}

void StreamProtocolTests::parserRejectsOversizedFrame()
{
  // a header claiming a huge payload must fail at once, not buffer forever
  auto bytes = encodeFrame(Frame{FrameType::VideoFrame, 0, {}});
  bytes[1] = char(0x7f);
  FrameParser parser;
  parser.feed(bytes);
  QVERIFY(!parser.next().has_value());
  QVERIFY(parser.hasError());
}

void StreamProtocolTests::payloadRoundTrips()
{
  const auto hello = decodeHello(encode(Hello{1, QStringLiteral("Eyad’s Mac")}));
  QVERIFY(hello);
  QCOMPARE(hello->computerName, QStringLiteral("Eyad’s Mac"));

  const VideoConfig config{VideoCodec::H264, 1920, 1080, 60, QByteArray("\x01\x64\x00\x28", 4)};
  const auto video = decodeVideoConfig(encode(config));
  QVERIFY(video);
  QCOMPARE(video->codec, VideoCodec::H264);
  QCOMPARE(video->width, uint16_t(1920));
  QCOMPARE(video->height, uint16_t(1080));
  QCOMPARE(video->fps, uint16_t(60));
  QCOMPARE(video->codecData, config.codecData);

  const auto frame = decodeVideoFrame(encode(VideoFrame{true, QByteArray("nal")}));
  QVERIFY(frame);
  QVERIFY(frame->keyframe);
  QCOMPARE(frame->data, QByteArray("nal"));

  const auto audio = decodeAudioConfig(encode(AudioConfig{AudioCodec::Aac, 48000, 2, QByteArray("\x11\x90", 2)}));
  QVERIFY(audio);
  QCOMPARE(audio->sampleRate, uint32_t(48000));
  QCOMPARE(audio->channels, uint8_t(2));

  const auto stats = decodeStats(encode(Stats{58, 3}));
  QVERIFY(stats);
  QCOMPARE(stats->receivedFps, uint16_t(58));
}

void StreamProtocolTests::payloadRejectsMalformed_data()
{
  QTest::addColumn<QByteArray>("payload");

  QTest::newRow("empty") << QByteArray();
  QTest::newRow("truncated") << encode(VideoConfig{VideoCodec::H264, 1920, 1080, 30, {}}).left(4);
  QTest::newRow("trailing bytes") << encode(VideoConfig{VideoCodec::H264, 1920, 1080, 30, {}}) + "x";
  QTest::newRow("unknown codec") << QByteArray("\x09\x07\x80\x04\x38\x00\x1e\x00\x00\x00\x00", 11);
  QTest::newRow("zero size") << encode(VideoConfig{VideoCodec::H264, 0, 1080, 30, {}});
}

void StreamProtocolTests::payloadRejectsMalformed()
{
  QFETCH(QByteArray, payload);
  QVERIFY(!decodeVideoConfig(payload).has_value());
}

void StreamProtocolTests::outputSize_data()
{
  QTest::addColumn<int>("resolution");
  QTest::addColumn<QSize>("screen");
  QTest::addColumn<QSize>("expected");

  QTest::newRow("retina to 1080p") << int(Resolution::P1080) << QSize(3024, 1964) << QSize(1662, 1080);
  QTest::newRow("1080p to 720p") << int(Resolution::P720) << QSize(1920, 1080) << QSize(1280, 720);
  QTest::newRow("never upscales") << int(Resolution::P1080) << QSize(1280, 800) << QSize(1280, 800);
  QTest::newRow("native kept even") << int(Resolution::Native) << QSize(1513, 983) << QSize(1512, 982);
}

void StreamProtocolTests::outputSize()
{
  QFETCH(int, resolution);
  QFETCH(QSize, screen);
  QFETCH(QSize, expected);
  QCOMPARE(hopflow::stream::outputSize(static_cast<Resolution>(resolution), screen), expected);
}

void StreamProtocolTests::outputFps()
{
  QCOMPARE(hopflow::stream::outputFps(FrameRate::Fps30, 120), 30);
  QCOMPARE(hopflow::stream::outputFps(FrameRate::Fps60, 120), 60);
  QCOMPARE(hopflow::stream::outputFps(FrameRate::Native, 120), 120);
  QCOMPARE(hopflow::stream::outputFps(FrameRate::Native, 0), 60);
}

void StreamProtocolTests::rateDropsOnBacklogAndStepsDown()
{
  RateController rate(8'000'000);
  const uint64_t congested = 1'000'000;
  QVERIFY(rate.shouldSend(0, 0));

  // the first congested frame is dropped but the bitrate holds
  QVERIFY(!rate.shouldSend(congested, 100));
  QCOMPARE(rate.bitrate(), 8'000'000u);
  QVERIFY(!rate.takeBitrateChange());

  // congestion that lasts lowers the bitrate
  QVERIFY(!rate.shouldSend(congested, 100 + RateController::kStepDownAfterMs));
  QCOMPARE(rate.bitrate(), 6'000'000u);
  QVERIFY(rate.takeBitrateChange());
  QVERIFY(!rate.takeBitrateChange());
}

void StreamProtocolTests::rateStepsBackUp()
{
  RateController rate(8'000'000);
  rate.shouldSend(10'000'000, 0);
  rate.shouldSend(10'000'000, RateController::kStepDownAfterMs);
  QCOMPARE(rate.bitrate(), 6'000'000u);
  rate.takeBitrateChange();

  // a quiet spell brings it back toward the target, never past it
  int64_t now = 2000;
  for (int i = 0; i < 20; ++i) {
    QVERIFY(rate.shouldSend(0, now));
    now += RateController::kStepUpAfterMs;
  }
  QCOMPARE(rate.bitrate(), 8'000'000u);
}

void StreamProtocolTests::rateNeverGoesBelowFloor()
{
  RateController rate(10'000'000);
  int64_t now = 0;
  for (int i = 0; i < 50; ++i) {
    rate.shouldSend(100'000'000, now);
    now += RateController::kStepDownAfterMs;
  }
  QCOMPARE(rate.bitrate(), static_cast<uint32_t>(10'000'000 * RateController::kFloor));
}

QTEST_MAIN(StreamProtocolTests)
