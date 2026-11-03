/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MicrophoneTests.h"

#include "stream/Microphone.h"

#include <QTest>

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

void MicrophoneTests::configIsPcm()
{
  const auto config = microphoneConfig();
  QCOMPARE(config.codec, AudioCodec::Pcm16);
  QCOMPARE(config.sampleRate, kAudioSampleRate);
  QCOMPARE(uint32_t(config.channels), kAudioChannels);
  QCOMPARE(kMicPacketFrames, 480u);
}

QTEST_MAIN(MicrophoneTests)
