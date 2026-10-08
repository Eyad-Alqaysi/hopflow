/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MacAudioTests.h"

#include "stream/AudioBuffer.h"
#include "stream/MacAudio.h"

#include <QTest>

#include <cmath>

using namespace hopflow::stream;

void MacAudioTests::aacRoundTrip()
{
  // one second of a 1 kHz tone at half volume, in odd sized chunks as capture delivers it
  constexpr int kFrames = int(kAudioSampleRate);
  std::vector<float> tone(size_t(kFrames) * 2);
  for (int i = 0; i < kFrames; ++i) {
    tone[size_t(i) * 2] = tone[size_t(i) * 2 + 1] = 0.5f * std::sin(2.0f * float(M_PI) * 1000.0f * float(i) / 48000.0f);
  }

  MacAacEncoder encoder;
  QVERIFY(encoder.open());
  std::vector<EncodedAudio> packets;
  for (int offset = 0; offset < kFrames; offset += 960) {
    const auto frames = uint32_t(std::min(960, kFrames - offset));
    for (auto &packet : encoder.encode(tone.data() + size_t(offset) * 2, frames)) {
      packets.push_back(packet);
    }
  }
  QVERIFY(packets.size() > 40);
  QCOMPARE(packets[1].timestampUs - packets[0].timestampUs, int64_t(1024 * 1'000'000 / 48000));
  // no more than 128 kbps; a pure tone needs far less
  qint64 bytes = 0;
  for (const auto &packet : packets) {
    bytes += packet.data.size();
  }
  QVERIFY2(bytes > 0 && bytes <= 16000 * 12 / 10, qPrintable(QStringLiteral("%1 bytes in a second").arg(bytes)));

  MacAacDecoder decoder;
  QVERIFY(decoder.open());
  std::vector<float> decoded;
  for (const auto &packet : packets) {
    const auto samples = decoder.decode(packet.data);
    decoded.insert(decoded.end(), samples.begin(), samples.end());
  }
  QVERIFY(decoded.size() > size_t(kFrames));

  // past the codec's start up delay the tone is back: same loudness and pitch
  double energy = 0;
  int crossings = 0;
  const size_t start = 4096;
  const size_t end = std::min(decoded.size() / 2, size_t(40'000));
  for (size_t i = start; i < end; ++i) {
    const float left = decoded[i * 2];
    energy += double(left) * left;
    if (i > start && (decoded[(i - 1) * 2] < 0) != (left < 0)) {
      ++crossings;
    }
  }
  const double rms = std::sqrt(energy / double(end - start));
  QVERIFY2(std::abs(rms - 0.3536) < 0.05, qPrintable(QStringLiteral("rms %1").arg(rms)));
  const double hz = crossings / 2.0 / (double(end - start) / 48000.0);
  QVERIFY2(std::abs(hz - 1000.0) < 20.0, qPrintable(QStringLiteral("pitch %1 Hz").arg(hz)));
}

QTEST_MAIN(MacAudioTests)
