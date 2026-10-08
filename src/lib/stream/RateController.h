/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

namespace hopflow::stream {

//! Keeps a stream within what the network delivers
/*!
The sender reports how many bytes are still waiting to be sent each time it
has a frame ready. A growing backlog means the network is slower than the
encoder: frames are dropped right away so latency stays low, and if the
congestion lasts the bitrate steps down. After a quiet spell it steps back up
toward the preset.
*/
class RateController
{
public:
  explicit RateController(uint32_t targetBitrate);

  //! Decide about the frame that is ready now; returns false to drop it
  bool shouldSend(uint64_t backlogBytes, int64_t nowMs);

  //! Bitrate the encoder should use
  uint32_t bitrate() const
  {
    return m_bitrate;
  }

  //! True once after the bitrate changed, so the encoder is told only then
  bool takeBitrateChange();

  //! The lowest bitrate it will step down to, as a fraction of the target
  static constexpr double kFloor = 0.2;
  //! Backlog, in seconds of video at the current bitrate, that counts as congestion
  static constexpr double kCongestedSeconds = 0.15;
  static constexpr int64_t kStepDownAfterMs = 1000;
  static constexpr int64_t kStepUpAfterMs = 5000;

private:
  uint32_t m_target;
  uint32_t m_bitrate;
  bool m_changed = false;
  int64_t m_congestedSinceMs = -1;
  int64_t m_clearSinceMs = -1;
};

} // namespace hopflow::stream
