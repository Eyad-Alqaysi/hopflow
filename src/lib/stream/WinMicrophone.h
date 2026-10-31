/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

//! Captures the default communications microphone (WASAPI) as 16 bit PCM
class WinMicrophoneSource : public IAudioSource
{
public:
  WinMicrophoneSource();
  ~WinMicrophoneSource() override;

  void start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed) override;
  void stop() override;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! Plays 16 bit PCM into VB-Audio Virtual Cable, whose "CABLE Output" apps record as a microphone
class WinVirtualMicSink : public IAudioSink
{
public:
  //! \p deviceId comes from findDevice()
  explicit WinVirtualMicSink(const QString &deviceId);
  ~WinVirtualMicSink() override;

  bool configure(const AudioConfig &config) override;
  void play(const EncodedAudio &packet) override;
  int64_t positionUs() const override;

  //! Endpoint ID of the cable's playback side, or empty when it is not installed
  static QString findDevice();

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
