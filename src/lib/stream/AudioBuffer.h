/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>

#include <cstdint>
#include <deque>
#include <mutex>

namespace hopflow::stream {

//! Audio format used end to end: 48 kHz stereo
inline constexpr uint32_t kAudioSampleRate = 48000;
inline constexpr uint32_t kAudioChannels = 2;
inline constexpr uint32_t kAacFramesPerPacket = 1024;
inline constexpr uint32_t kAacBitrate = 128000;

//! AudioSpecificConfig for AAC-LC at \p sampleRate with \p channels
QByteArray aacAudioSpecificConfig(uint32_t sampleRate, uint32_t channels);

//! Holds decoded audio between the network and the sound card
/*!
Playback waits until a little audio has built up, so network jitter does
not cause gaps. If audio piles up, because the sender's clock runs faster or
a burst arrived, the oldest audio is dropped to keep latency low. Samples
are interleaved floats; push and pull may run on different threads.
*/
class PcmJitterBuffer
{
public:
  PcmJitterBuffer(uint32_t channels, uint32_t startFrames, uint32_t maxFrames);

  void push(const float *samples, uint32_t frames);

  //! Fill \p frames frames; silence where there is not enough audio. Returns the frames of real audio.
  uint32_t pull(float *out, uint32_t frames);

  uint32_t bufferedFrames() const;

private:
  const uint32_t m_channels;
  const uint32_t m_startFrames;
  const uint32_t m_maxFrames;
  mutable std::mutex m_mutex;
  std::deque<float> m_samples;
  bool m_playing = false;
};

} // namespace hopflow::stream
