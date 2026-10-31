/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LoopbackRingTests.h"

#include "LoopbackRing.h"

#include <QTest>

#include <vector>

using hopflow::mic::LoopbackRing;

namespace {

//! Stereo frames numbered from \p start in both channels
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

void LoopbackRingTests::waitsForStartDepthBeyondRead()
{
  LoopbackRing<2, 16> ring(4, 12);
  std::vector<float> out(2 * 2, -1.0f);

  // a read of 2 needs 4 more than that buffered first
  ring.write(ramp(0, 5).data(), 5);
  QCOMPARE(ring.read(out.data(), 2), 0u);
  QCOMPARE(out, std::vector<float>(4, 0.0f));
  QCOMPARE(ring.bufferedFrames(), 5u);

  ring.write(ramp(5, 1).data(), 1);
  QCOMPARE(ring.read(out.data(), 2), 2u);
  QCOMPARE(out, ramp(0, 2));
}

void LoopbackRingTests::wrapsAround()
{
  LoopbackRing<2, 16> ring(0, 16);
  std::vector<float> out(2 * 5);

  // 50 frames through a 16 frame buffer, in steps that do not divide it
  for (int start = 0; start < 50; start += 5) {
    ring.write(ramp(start, 5).data(), 5);
    QCOMPARE(ring.read(out.data(), 5), 5u);
    QCOMPARE(out, ramp(start, 5));
  }
  QCOMPARE(ring.bufferedFrames(), 0u);
}

void LoopbackRingTests::underrunGivesSilence()
{
  LoopbackRing<2, 16> ring(0, 16);
  std::vector<float> out(2 * 5, -1.0f);

  ring.write(ramp(0, 5).data(), 5);
  QCOMPARE(ring.read(out.data(), 5), 5u);

  // the writer falls behind while playing: what there is, then silence
  ring.write(ramp(5, 3).data(), 3);
  QCOMPARE(ring.read(out.data(), 5), 3u);
  QCOMPARE(out[4], 7.0f);
  QCOMPARE(out[6], 0.0f);
  QCOMPARE(out[9], 0.0f);

  // nothing more written: all silence
  QCOMPARE(ring.read(out.data(), 5), 0u);
  QCOMPARE(out, std::vector<float>(10, 0.0f));
}

void LoopbackRingTests::overrunDropsOldest()
{
  LoopbackRing<2, 16> ring(4, 12);
  std::vector<float> out(2 * 2);

  // 20 frames into 16 overwrites the oldest; more than 12 queued drops back to 4 past the read
  ring.write(ramp(0, 20).data(), 20);
  QCOMPARE(ring.read(out.data(), 2), 2u);
  QCOMPARE(out, ramp(14, 2));

  // the newest frames sit where the oldest were, after the wrap
  std::vector<float> rest(2 * 4);
  QCOMPARE(ring.read(rest.data(), 4), 4u);
  QCOMPARE(rest, ramp(16, 4));
}

QTEST_MAIN(LoopbackRingTests)
