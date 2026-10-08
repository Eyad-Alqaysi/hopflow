/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/RateController.h"

#include <algorithm>
#include <utility>

namespace hopflow::stream {

RateController::RateController(uint32_t targetBitrate) : m_target(targetBitrate), m_bitrate(targetBitrate)
{
}

bool RateController::shouldSend(uint64_t backlogBytes, int64_t nowMs)
{
  const auto congestedBytes = static_cast<uint64_t>(m_bitrate / 8.0 * kCongestedSeconds);
  if (backlogBytes > congestedBytes) {
    m_clearSinceMs = -1;
    if (m_congestedSinceMs < 0) {
      m_congestedSinceMs = nowMs;
    } else if (nowMs - m_congestedSinceMs >= kStepDownAfterMs) {
      const auto floor = static_cast<uint32_t>(m_target * kFloor);
      const auto lower = std::max(floor, static_cast<uint32_t>(m_bitrate * 0.75));
      m_changed = m_changed || lower != m_bitrate;
      m_bitrate = lower;
      m_congestedSinceMs = nowMs;
    }
    return false;
  }

  m_congestedSinceMs = -1;
  if (m_clearSinceMs < 0) {
    m_clearSinceMs = nowMs;
  } else if (nowMs - m_clearSinceMs >= kStepUpAfterMs && m_bitrate < m_target) {
    m_bitrate = std::min(m_target, static_cast<uint32_t>(m_bitrate * 1.15));
    m_changed = true;
    m_clearSinceMs = nowMs;
  }
  return true;
}

bool RateController::takeBitrateChange()
{
  return std::exchange(m_changed, false);
}

} // namespace hopflow::stream
