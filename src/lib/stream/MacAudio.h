/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/AudioBuffer.h"
#include "stream/MediaInterfaces.h"

#include <CoreAudio/AudioHardwareBase.h>
#include <CoreAudio/CoreAudioTypes.h>

#include <functional>
#include <memory>
#include <vector>

namespace hopflow::stream {

//! Interleaved stereo linear PCM: floats, or 16 bit integers
AudioStreamBasicDescription stereoPcmFormat(bool floats = true, Float64 sampleRate = kAudioSampleRate);

//! \p frames of captured floats in \p format, one buffer per channel or interleaved, as interleaved stereo in \p out
void interleaveStereo(
    const AudioBufferList &list, const AudioStreamBasicDescription &format, UInt32 frames, std::vector<float> &out
);

//! Calls \p changed on a queue of its own each time the default output, or input, device changes
/*!
A call already under way can finish after the watch is destroyed, so
\p changed must hold only weak references to what it restarts.
*/
class MacDefaultDeviceWatch
{
public:
  enum class Direction
  {
    Output,
    Input
  };

  MacDefaultDeviceWatch(Direction direction, std::function<void()> changed);
  ~MacDefaultDeviceWatch();
  MacDefaultDeviceWatch(const MacDefaultDeviceWatch &) = delete;
  MacDefaultDeviceWatch &operator=(const MacDefaultDeviceWatch &) = delete;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! AAC-LC encoder on AudioToolbox: 48 kHz stereo floats in, 1024 frame packets out
class MacAacEncoder
{
public:
  MacAacEncoder();
  ~MacAacEncoder();
  MacAacEncoder(const MacAacEncoder &) = delete;
  MacAacEncoder &operator=(const MacAacEncoder &) = delete;

  bool open();

  //! Add interleaved samples; returns the packets that are now complete
  std::vector<EncodedAudio> encode(const float *samples, uint32_t frames);

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! AAC-LC decoder on AudioToolbox: packets in, interleaved 48 kHz stereo floats out
class MacAacDecoder
{
public:
  MacAacDecoder();
  ~MacAacDecoder();
  MacAacDecoder(const MacAacDecoder &) = delete;
  MacAacDecoder &operator=(const MacAacDecoder &) = delete;

  bool open();
  std::vector<float> decode(const QByteArray &packet);

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! Encodes captured 48 kHz stereo floats as \p codec: AAC, or Pcm16 for low latency
class MacSoundEncoder
{
public:
  explicit MacSoundEncoder(AudioCodec codec);

  bool open();
  AudioConfig config() const;
  std::vector<EncodedAudio> encode(const float *samples, uint32_t frames);

private:
  const AudioCodec m_codec;
  MacAacEncoder m_aac;
  Pcm16Packetizer m_pcm;
};

//! Plays what a PcmJitterBuffer holds through an output audio unit
class MacPcmOutput
{
public:
  explicit MacPcmOutput(PcmJitterBuffer &buffer);
  ~MacPcmOutput();
  MacPcmOutput(const MacPcmOutput &) = delete;
  MacPcmOutput &operator=(const MacPcmOutput &) = delete;

  //! Play on \p device, pulling about \p bufferFrames at a time
  /*!
  With kAudioObjectUnknown it plays on the default output and moves along
  when the user picks another one, for example by plugging in headphones.
  */
  bool start(AudioObjectID device, UInt32 bufferFrames);

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! Captures what the Mac is playing with ScreenCaptureKit (macOS 13 and later)
class MacSystemAudioSource : public IAudioSource
{
public:
  explicit MacSystemAudioSource(AudioCodec codec = AudioCodec::Aac);
  ~MacSystemAudioSource() override;

  void start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed) override;
  void stop() override;

  static bool isAvailable();

  struct Impl;

private:
  std::shared_ptr<Impl> m_impl;
};

//! Plays shared audio through the default output, following it when it changes
class MacAudioSink : public IAudioSink
{
public:
  explicit MacAudioSink(AudioLatency latency = AudioLatency::Smooth);
  ~MacAudioSink() override;

  bool configure(const AudioConfig &config) override;
  void play(const EncodedAudio &packet) override;
  int64_t positionUs() const override;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
