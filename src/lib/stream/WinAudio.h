/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

//! Captures what the PC is playing (WASAPI loopback) and encodes it as AAC
class WinSystemAudioSource : public IAudioSource
{
public:
  WinSystemAudioSource();
  ~WinSystemAudioSource() override;

  void start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed) override;
  void stop() override;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! Decodes shared AAC sound and plays it on the default output (WASAPI)
class WinAudioSink : public IAudioSink
{
public:
  WinAudioSink();
  ~WinAudioSink() override;

  bool configure(const AudioConfig &config) override;
  void play(const EncodedAudio &packet) override;
  int64_t positionUs() const override;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
