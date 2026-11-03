/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioBufferTests.h"

#include "stream/AudioBuffer.h"

#include <QTest>

#include <cmath>
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

void AudioBufferTests::lowLatencyStaysWithinBounds()
{
  const auto depth = jitterDepth(AudioLatency::Low);
  QCOMPARE(depth.startFrames, kAudioSampleRate / 50);   // 20 ms
  QCOMPARE(depth.maxFrames, kAudioSampleRate * 2 / 25); // 80 ms
  QVERIFY(jitterDepth(AudioLatency::Smooth).startFrames > depth.startFrames);

  PcmJitterBuffer buffer(kAudioChannels, depth);
  std::vector<float> out(size_t(kPcmFramesPerPacket) * kAudioChannels);
  const std::vector<float> packet(size_t(kPcmFramesPerPacket) * kAudioChannels, 0.5f);

  // one 10 ms packet is not enough to start, two are
  buffer.push(packet.data(), kPcmFramesPerPacket);
  QCOMPARE(buffer.pull(out.data(), kPcmFramesPerPacket), 0u);
  buffer.push(packet.data(), kPcmFramesPerPacket);
  QCOMPARE(buffer.pull(out.data(), kPcmFramesPerPacket), kPcmFramesPerPacket);

  // a burst after a stall never holds more than 80 ms
  for (int i = 0; i < 50; ++i) {
    buffer.push(packet.data(), kPcmFramesPerPacket);
    QVERIFY(buffer.bufferedFrames() <= depth.maxFrames);
  }
}

void AudioBufferTests::pcm16PacketsAreTenMilliseconds()
{
  Pcm16Packetizer packetizer;
  const auto some = ramp(0, 300);
  QVERIFY(packetizer.pack(some.data(), 300).empty());

  // 300 + 700 frames make two whole packets, 40 frames wait for the next
  const auto more = ramp(0, 700);
  const auto packets = packetizer.pack(more.data(), 700);
  QCOMPARE(packets.size(), size_t(2));
  QCOMPARE(packets[0].data.size(), qsizetype(kPcmFramesPerPacket * kAudioChannels * sizeof(int16_t)));
  QCOMPARE(packets[0].timestampUs, int64_t(0));
  QCOMPARE(packets[1].timestampUs, int64_t(10'000));

  const std::vector<float> loud = {2.0f, -2.0f};
  std::vector<float> rest(size_t(kPcmFramesPerPacket - 40 - 1) * kAudioChannels, 0.0f);
  rest.insert(rest.end(), loud.begin(), loud.end());
  const auto last = packetizer.pack(rest.data(), uint32_t(rest.size() / kAudioChannels));
  QCOMPARE(last.size(), size_t(1));
  const auto *samples = reinterpret_cast<const int16_t *>(last[0].data.constData());
  // out of range samples are clipped, not wrapped
  QCOMPARE(samples[kPcmFramesPerPacket * kAudioChannels - 2], int16_t(32767));
  QCOMPARE(samples[kPcmFramesPerPacket * kAudioChannels - 1], int16_t(-32767));
}

void AudioBufferTests::pcm16RoundTrip()
{
  const std::vector<float> samples = {0.0f, 0.5f, -0.5f, 0.25f};
  const auto pcm = floatToPcm16(samples.data(), samples.size());
  QCOMPARE(pcm.size(), qsizetype(samples.size() * 2));

  const auto back = pcm16ToFloat(pcm);
  QCOMPARE(back.size(), samples.size());
  for (size_t i = 0; i < samples.size(); ++i) {
    QVERIFY(std::abs(back[i] - samples[i]) < 1.0f / 16384);
  }
}

void AudioBufferTests::pcm16Clamps()
{
  const std::vector<float> loud = {2.0f, -2.0f};
  const auto pcm = floatToPcm16(loud.data(), loud.size());
  const auto *values = reinterpret_cast<const int16_t *>(pcm.constData());
  QCOMPARE(values[0], int16_t(32767));
  QCOMPARE(values[1], int16_t(-32767));
}

QTEST_MAIN(AudioBufferTests)
