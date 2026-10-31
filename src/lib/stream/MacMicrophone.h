/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

//! Captures the default microphone as 16 bit PCM, asking for permission first
class MacMicrophoneSource : public IAudioSource
{
public:
  MacMicrophoneSource();
  ~MacMicrophoneSource() override;

  void start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed) override;
  void stop() override;

  struct Impl;

private:
  std::shared_ptr<Impl> m_impl;
};

//! Plays into "Hopflow Mic Feed", which HopflowMic.driver loops into the "Hopflow Microphone" input
class MacVirtualMicSink : public IAudioSink
{
public:
  MacVirtualMicSink();
  ~MacVirtualMicSink() override;

  bool configure(const AudioConfig &config) override;
  void play(const EncodedAudio &packet) override;
  int64_t positionUs() const override;

  //! Whether HopflowMic.driver is installed and loaded
  static bool isAvailable();

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
