/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "VideoLatencyGuardTests.h"

#include "stream/VideoLatencyGuard.h"

#include <QTest>

using hopflow::stream::VideoLatencyGuard;

namespace {

constexpr int64_t kFrameUs = 16'667;
constexpr int64_t kMaxLag = VideoLatencyGuard::kMaxLagUs;
//! Clearly past the limit, beyond the few microseconds the drift allowance adds
constexpr int64_t kLate = kMaxLag + 1'000;

} // namespace

void VideoLatencyGuardTests::showsFramesThatKeepUp()
{
  VideoLatencyGuard guard;
  for (int i = 0; i < 600; ++i) {
    QVERIFY(guard.shouldShow(i * kFrameUs, i * kFrameUs + 5'000, i % 300 == 0));
  }
}

void VideoLatencyGuardTests::ignoresTheClockOffset()
{
  // the two computers' clocks start hours apart
  VideoLatencyGuard guard;
  const int64_t offset = -3'600'000'000;
  for (int i = 0; i < 100; ++i) {
    QVERIFY(guard.shouldShow(i * kFrameUs, offset + i * kFrameUs, false));
  }
}

void VideoLatencyGuardTests::toleratesJitterUnderTheLimit()
{
  VideoLatencyGuard guard;
  QVERIFY(guard.shouldShow(0, 0, true));
  QVERIFY(guard.shouldShow(kFrameUs, kFrameUs + kMaxLag, false));
  QVERIFY(guard.shouldShow(2 * kFrameUs, 2 * kFrameUs, false));
}

void VideoLatencyGuardTests::skipsWhenFallingBehind()
{
  // each frame takes twice its interval to arrive, so the lag keeps growing
  VideoLatencyGuard guard;
  int firstSkipped = -1;
  for (int i = 0; i < 100 && firstSkipped < 0; ++i) {
    if (!guard.shouldShow(i * kFrameUs, i * 2 * kFrameUs, false)) {
      firstSkipped = i;
    }
  }
  // the first frame more than the limit behind, give or take the drift allowance
  QVERIFY(int64_t(firstSkipped) * kFrameUs > kMaxLag);
  QVERIFY(int64_t(firstSkipped - 2) * kFrameUs <= kMaxLag);
}

void VideoLatencyGuardTests::resumesOnlyOnATimelyKeyframe()
{
  VideoLatencyGuard guard;
  QVERIFY(guard.shouldShow(0, 0, true));
  QVERIFY(!guard.shouldShow(kFrameUs, kFrameUs + kLate, false));

  // caught up, but a delta frame needs the frames that were dropped
  QVERIFY(!guard.shouldShow(2 * kFrameUs, 2 * kFrameUs, false));
  // a keyframe that is still late is dropped too
  QVERIFY(!guard.shouldShow(3 * kFrameUs, 3 * kFrameUs + kLate, true));
  // a keyframe in time decodes on its own
  QVERIFY(guard.shouldShow(4 * kFrameUs, 4 * kFrameUs, true));
  QVERIFY(guard.shouldShow(5 * kFrameUs, 5 * kFrameUs, false));
}

void VideoLatencyGuardTests::allowsClockDrift()
{
  // the sender's clock runs 50 ppm slow: after an hour its frames look 180 ms later
  VideoLatencyGuard guard;
  const int64_t hourUs = 3'600'000'000;
  for (int64_t arrival = 0; arrival <= hourUs; arrival += 1'000'000) {
    const auto capture = arrival - arrival * 50 / 1'000'000;
    QVERIFY(guard.shouldShow(capture, arrival, false));
  }
}

void VideoLatencyGuardTests::resetForgetsTheBaseline()
{
  VideoLatencyGuard guard;
  QVERIFY(guard.shouldShow(0, 0, true));
  QVERIFY(!guard.shouldShow(kFrameUs, kFrameUs + kLate, false));

  // a new stream with a different offset starts fresh
  guard.reset();
  QVERIFY(guard.shouldShow(0, kMaxLag * 10, false));
  QVERIFY(guard.shouldShow(kFrameUs, kMaxLag * 10 + kFrameUs, false));
}

QTEST_MAIN(VideoLatencyGuardTests)
