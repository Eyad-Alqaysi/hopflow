/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioBufferTests.h"

#include "stream/AudioBuffer.h"

#include <QTest>

#include <vector>

using namespace hopflow::stream;

namespace {

//! Stereo frames numbered 0, 1, 2... in both channels
std::vector<float> ramp(int start, int frames)
{
  std::vector<float> samples;
  for (int i = 0; i < frames; ++i) {
    samples.push_back(float(start + i));
    samples.push_back(float(start + i));
  }
  return samples;
}

} // namespace

void AudioBufferTests::audioSpecificConfig()
{
  // AAC-LC, 48 kHz, stereo is the well known 0x1190
  QCOMPARE(aacAudioSpecificConfig(48000, 2), QByteArray::fromHex("1190"));
  QCOMPARE(aacAudioSpecificConfig(44100, 2), QByteArray::fromHex("1210"));
  QCOMPARE(aacAudioSpecificConfig(48000, 1), QByteArray::fromHex("1188"));
}

void AudioBufferTests::waitsUntilStartDepth()
{
  PcmJitterBuffer buffer(2, 100, 1000);
  std::vector<float> out(2 * 10, -1.0f);

  const auto some = ramp(0, 50);
  buffer.push(some.data(), 50);
  // not enough yet: silence, and nothing consumed
  QCOMPARE(buffer.pull(out.data(), 10), 0u);
  QCOMPARE(out[0], 0.0f);
  QCOMPARE(buffer.bufferedFrames(), 50u);

  const auto more = ramp(50, 50);
  buffer.push(more.data(), 50);
  QCOMPARE(buffer.pull(out.data(), 10), 10u);
  QCOMPARE(out[0], 0.0f);
  QCOMPARE(out[19], 9.0f);
}

void AudioBufferTests::playsInOrderThenSilence()
{
  PcmJitterBuffer buffer(2, 10, 1000);
  const auto samples = ramp(0, 15);
  buffer.push(samples.data(), 15);

  std::vector<float> out(2 * 20, -1.0f);
  QCOMPARE(buffer.pull(out.data(), 20), 15u);
  QCOMPARE(out[28], 14.0f);
  QCOMPARE(out[30], 0.0f); // ran dry
  QCOMPARE(out[39], 0.0f);

  // after running dry it waits for the start depth again
  const auto next = ramp(100, 5);
  buffer.push(next.data(), 5);
  QCOMPARE(buffer.pull(out.data(), 5), 0u);
}

void AudioBufferTests::dropsOldestWhenTooFull()
{
  PcmJitterBuffer buffer(2, 100, 300);
  const auto samples = ramp(0, 400);
  buffer.push(samples.data(), 400);

  // back down to the start depth, keeping the newest audio
  QCOMPARE(buffer.bufferedFrames(), 100u);
  std::vector<float> out(2);
  QCOMPARE(buffer.pull(out.data(), 1), 1u);
  QCOMPARE(out[0], 300.0f);
}

QTEST_MAIN(AudioBufferTests)
