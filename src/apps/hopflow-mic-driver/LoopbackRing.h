/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace hopflow::mic {

//! Carries audio from one real-time thread to another without locks
/*!
One thread writes, another reads; neither ever waits. Positions count frames
since the start and only grow, so the buffer wraps by taking them modulo the
capacity. The reader starts playing once \p startFrames more than it asks
for are buffered, which absorbs the two threads running their cycles out of
phase. It plays silence when the writer falls behind and, when more than
\p maxFrames build up, drops the oldest audio so that playing resumes at the
starting depth.
*/
template <uint32_t Channels, uint32_t Capacity> class LoopbackRing
{
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
  LoopbackRing(uint32_t startFrames, uint32_t maxFrames)
      : m_startFrames(std::min(startFrames, Capacity)),
        m_maxFrames(std::clamp(maxFrames, m_startFrames, Capacity))
  {
  }

  //! Writer thread only
  void write(const float *in, uint32_t frames)
  {
    auto position = m_write.load(std::memory_order_relaxed);
    for (uint32_t frame = 0; frame < frames; ++frame, ++position) {
      std::copy_n(in + size_t(frame) * Channels, Channels, slot(position));
    }
    m_write.store(position, std::memory_order_release);
  }

  //! Reader thread only: fills \p frames frames and returns how many were real audio
  uint32_t read(float *out, uint32_t frames)
  {
    const auto written = m_write.load(std::memory_order_acquire);
    auto position = m_read.load(std::memory_order_relaxed);

    if (written - position > m_maxFrames) {
      position = written - std::min<uint64_t>(m_maxFrames, uint64_t(m_startFrames) + frames);
    }
    if (!m_playing && written - position >= uint64_t(m_startFrames) + frames) {
      m_playing = true;
    }

    uint32_t available = 0;
    if (m_playing) {
      available = uint32_t(std::min<uint64_t>(frames, written - position));
      for (uint32_t frame = 0; frame < available; ++frame) {
        std::copy_n(slot(position + frame), Channels, out + size_t(frame) * Channels);
      }

      // a writer that lapped us while we copied left garbage in the oldest frames
      const auto after = m_write.load(std::memory_order_acquire);
      if (after - position > Capacity) {
        const auto lost = uint32_t(std::min<uint64_t>(after - position - Capacity, available));
        std::fill_n(out, size_t(lost) * Channels, 0.0f);
      }

      position += available;
      if (available < frames) {
        m_playing = false; // ran dry: wait to build up again
      }
    }
    std::fill(out + size_t(available) * Channels, out + size_t(frames) * Channels, 0.0f);
    m_read.store(position, std::memory_order_relaxed);
    return available;
  }

  //! Approximate from any thread
  uint32_t bufferedFrames() const
  {
    const auto written = m_write.load(std::memory_order_acquire);
    const auto read = m_read.load(std::memory_order_relaxed);
    return uint32_t(std::min<uint64_t>(written - read, Capacity));
  }

private:
  float *slot(uint64_t position)
  {
    return m_samples.data() + size_t(position & (Capacity - 1)) * Channels;
  }

  const uint32_t m_startFrames;
  const uint32_t m_maxFrames;
  std::array<float, size_t(Capacity) * Channels> m_samples{};
  std::atomic<uint64_t> m_write{0};
  std::atomic<uint64_t> m_read{0};
  bool m_playing = false;
};

} // namespace hopflow::mic
