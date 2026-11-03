/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

namespace hopflow::stream {

//! Keeps received video close to live when this computer falls behind
/*!
A frame's delay is when it arrived minus when it was captured, in two clocks
that differ by an unknown offset, so only the growth over the smallest delay
seen means anything. When decoding or drawing is slower than the sender, that
growth builds up without bound in the network buffers. Past kMaxLagUs, frames
are dropped until a keyframe arrives in time, which decodes on its own, so the
picture jumps back to live instead of trailing further behind.
*/
class VideoLatencyGuard
{
public:
  //! Returns false to drop the frame captured at \p captureUs that arrived at \p arrivalUs
  bool shouldShow(int64_t captureUs, int64_t arrivalUs, bool keyframe);

  //! Forget the baseline, such as when a new stream starts
  void reset();

  //! How far behind live a frame may be before frames are dropped
  static constexpr int64_t kMaxLagUs = 250'000;
  //! Clocks on two computers drift apart; allow this many microseconds of it per second
  static constexpr int64_t kDriftUsPerSecond = 100;

private:
  bool m_hasBaseline = false;
  int64_t m_baselineUs = 0;
  int64_t m_baselineArrivalUs = 0;
  bool m_skipping = false;
};

} // namespace hopflow::stream
