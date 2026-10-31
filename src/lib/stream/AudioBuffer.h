/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <QByteArray>

#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace hopflow::stream {

//! Audio format used end to end: 48 kHz stereo
inline constexpr uint32_t kAudioSampleRate = 48000;
inline constexpr uint32_t kAudioChannels = 2;
inline constexpr uint32_t kAacFramesPerPacket = 1024;
inline constexpr uint32_t kAacBitrate = 128000;
//! 10 ms: short packets keep routed sound close to live
inline constexpr uint32_t kPcmFramesPerPacket = kAudioSampleRate / 100;

//! AudioSpecificConfig for AAC-LC at \p sampleRate with \p channels
QByteArray aacAudioSpecificConfig(uint32_t sampleRate, uint32_t channels);

//! The AudioConfig a source sends for \p codec in the end to end format
AudioConfig audioConfigFor(AudioCodec codec);

//! How much audio a sink holds back, in frames
struct JitterDepth
{
  //! Playback starts once this much has arrived
  uint32_t startFrames;
  //! More than this and the oldest audio is dropped
  uint32_t maxFrames;
};

//! 50 ms and 250 ms for Smooth; 20 ms and 80 ms for Low
JitterDepth jitterDepth(AudioLatency latency);

//! Turns interleaved float samples into Pcm16 packets of kPcmFramesPerPacket frames
class Pcm16Packetizer
{
public:
  //! Add samples; returns the packets that are now complete
  std::vector<EncodedAudio> pack(const float *samples, uint32_t frames);

private:
  std::vector<int16_t> m_pending;
  int64_t m_packets = 0;
};

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
  PcmJitterBuffer(uint32_t channels, JitterDepth depth);

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
