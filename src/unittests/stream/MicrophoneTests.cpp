/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MicrophoneTests.h"

#include "stream/Microphone.h"

#include <QTest>

#include <cmath>
#include <vector>

using namespace hopflow::stream;

void MicrophoneTests::findsVirtualCableInput_data()
{
  QTest::addColumn<QString>("name");
  QTest::addColumn<bool>("matches");

  QTest::newRow("cable") << "CABLE Input (VB-Audio Virtual Cable)" << true;
  QTest::newRow("other case") << "cable input (VB-Audio Virtual Cable)" << true;
  // the recording side is what apps use as a microphone; Hopflow plays into the other side
  QTest::newRow("cable output") << "CABLE Output (VB-Audio Virtual Cable)" << false;
  QTest::newRow("speakers") << "Speakers (Realtek(R) Audio)" << false;
  QTest::newRow("empty") << "" << false;
}

void MicrophoneTests::findsVirtualCableInput()
{
  QFETCH(QString, name);
  QFETCH(bool, matches);
  QCOMPARE(isVirtualCableInput(name), matches);
}

void MicrophoneTests::pcm16RoundTrip()
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

void MicrophoneTests::pcm16Clamps()
{
  const std::vector<float> loud = {2.0f, -2.0f};
  const auto pcm = floatToPcm16(loud.data(), loud.size());
  const auto *values = reinterpret_cast<const int16_t *>(pcm.constData());
  QCOMPARE(values[0], int16_t(32767));
  QCOMPARE(values[1], int16_t(-32767));
}

void MicrophoneTests::configIsPcm()
{
  const auto config = microphoneConfig();
  QCOMPARE(config.codec, AudioCodec::Pcm16);
  QCOMPARE(config.sampleRate, kAudioSampleRate);
  QCOMPARE(uint32_t(config.channels), kAudioChannels);
  QCOMPARE(kMicPacketFrames, 480u);
}

QTEST_MAIN(MicrophoneTests)
