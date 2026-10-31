/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/AudioBuffer.h"

#include <algorithm>
#include <array>

namespace hopflow::stream {

QByteArray aacAudioSpecificConfig(uint32_t sampleRate, uint32_t channels)
{
  static constexpr std::array<uint32_t, 13> rates = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                                     22050, 16000, 12000, 11025, 8000,  7350};
  const auto it = std::find(rates.begin(), rates.end(), sampleRate);
  const auto index = static_cast<uint32_t>(it == rates.end() ? 3 : it - rates.begin());

  // 5 bits object type (2, AAC-LC), 4 bits rate index, 4 bits channels, 3 bits zero
  const uint32_t bits = (2u << 11) | (index << 7) | ((channels & 0xf) << 3);
  QByteArray config;
  config.append(static_cast<char>(bits >> 8));
  config.append(static_cast<char>(bits & 0xff));
  return config;
}

AudioConfig audioConfigFor(AudioCodec codec)
{
  if (codec == AudioCodec::Pcm16) {
    return AudioConfig{AudioCodec::Pcm16, kAudioSampleRate, kAudioChannels, {}};
  }
  return AudioConfig{
      AudioCodec::Aac, kAudioSampleRate, kAudioChannels, aacAudioSpecificConfig(kAudioSampleRate, kAudioChannels)
  };
}

JitterDepth jitterDepth(AudioLatency latency)
{
  if (latency == AudioLatency::Low) {
    return JitterDepth{kAudioSampleRate / 50, kAudioSampleRate * 2 / 25};
  }
  return JitterDepth{kAudioSampleRate / 20, kAudioSampleRate / 4};
}

std::vector<EncodedAudio> Pcm16Packetizer::pack(const float *samples, uint32_t frames)
{
  for (size_t i = 0; i < size_t(frames) * kAudioChannels; ++i) {
    m_pending.push_back(static_cast<int16_t>(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f));
  }

  std::vector<EncodedAudio> packets;
  constexpr size_t packetSamples = size_t(kPcmFramesPerPacket) * kAudioChannels;
  size_t offset = 0;
  for (; m_pending.size() - offset >= packetSamples; offset += packetSamples) {
    const auto timestamp = m_packets * kPcmFramesPerPacket * 1'000'000 / kAudioSampleRate;
    packets.push_back(
        EncodedAudio{
            timestamp,
            QByteArray(
                reinterpret_cast<const char *>(m_pending.data() + offset), qsizetype(packetSamples * sizeof(int16_t))
            )
        }
    );
    ++m_packets;
  }
  m_pending.erase(m_pending.begin(), m_pending.begin() + std::ptrdiff_t(offset));
  return packets;
}

PcmJitterBuffer::PcmJitterBuffer(uint32_t channels, JitterDepth depth)
    : PcmJitterBuffer(channels, depth.startFrames, depth.maxFrames)
{
}

PcmJitterBuffer::PcmJitterBuffer(uint32_t channels, uint32_t startFrames, uint32_t maxFrames)
    : m_channels(channels),
      m_startFrames(startFrames),
      m_maxFrames(std::max(maxFrames, startFrames))
{
}

void PcmJitterBuffer::push(const float *samples, uint32_t frames)
{
  std::scoped_lock lock(m_mutex);
  m_samples.insert(m_samples.end(), samples, samples + size_t(frames) * m_channels);

  // too far behind: drop the oldest audio, back to the starting depth
  if (m_samples.size() > size_t(m_maxFrames) * m_channels) {
    m_samples.erase(m_samples.begin(), m_samples.end() - std::ptrdiff_t(m_startFrames) * m_channels);
  }
}

uint32_t PcmJitterBuffer::pull(float *out, uint32_t frames)
{
  std::scoped_lock lock(m_mutex);
  const auto wanted = size_t(frames) * m_channels;

  if (!m_playing && m_samples.size() >= size_t(m_startFrames) * m_channels) {
    m_playing = true;
  }

  size_t available = 0;
  if (m_playing) {
    available = std::min(wanted, m_samples.size());
    std::copy_n(m_samples.begin(), available, out);
    m_samples.erase(m_samples.begin(), m_samples.begin() + std::ptrdiff_t(available));
    if (available < wanted) {
      m_playing = false; // ran dry: wait to build up again
    }
  }
  std::fill(out + available, out + wanted, 0.0f);
  return static_cast<uint32_t>(available / m_channels);
}

uint32_t PcmJitterBuffer::bufferedFrames() const
{
  std::scoped_lock lock(m_mutex);
  return static_cast<uint32_t>(m_samples.size() / m_channels);
}

} // namespace hopflow::stream
