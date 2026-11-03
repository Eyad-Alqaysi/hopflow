/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/VideoLatencyGuard.h"

namespace hopflow::stream {

bool VideoLatencyGuard::shouldShow(int64_t captureUs, int64_t arrivalUs, bool keyframe)
{
  // the smallest delay seen, allowed to creep up as slowly as the clocks can drift
  const auto baseline = [this, arrivalUs] {
    return m_baselineUs + (arrivalUs - m_baselineArrivalUs) * kDriftUsPerSecond / 1'000'000;
  };

  const auto delay = arrivalUs - captureUs;
  if (!m_hasBaseline || delay < baseline()) {
    m_hasBaseline = true;
    m_baselineUs = delay;
    m_baselineArrivalUs = arrivalUs;
  }
  const auto lag = delay - baseline();

  if (lag > kMaxLagUs) {
    m_skipping = true;
  } else if (m_skipping && keyframe) {
    m_skipping = false;
  }
  return !m_skipping;
}

void VideoLatencyGuard::reset()
{
  m_hasBaseline = false;
  m_skipping = false;
}

} // namespace hopflow::stream
