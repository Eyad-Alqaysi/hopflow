/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

//! Captures what the PC is playing (WASAPI loopback) and sends it as AAC, or as Pcm16 for low latency
/*!
With \p muteLocal the PC's speakers are muted while it captures and set back
as they were afterwards; loopback still hears what is playing.
*/
class WinSystemAudioSource : public IAudioSource
{
public:
  explicit WinSystemAudioSource(AudioCodec codec = AudioCodec::Aac, bool muteLocal = false);
  ~WinSystemAudioSource() override;

  void start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed) override;
  void stop() override;

  //! Unmute speakers that a run which crashed while muting them left muted
  static void restoreMutedSpeakers();

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! Decodes shared AAC sound and plays it on the default output (WASAPI)
class WinAudioSink : public IAudioSink
{
public:
  explicit WinAudioSink(AudioLatency latency = AudioLatency::Smooth);
  ~WinAudioSink() override;

  bool configure(const AudioConfig &config) override;
  void play(const EncodedAudio &packet) override;
  int64_t positionUs() const override;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
