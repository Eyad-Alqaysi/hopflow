/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

//! Captures what the Mac is playing with a Core Audio process tap (macOS 14.2 and later)
/*!
Unlike ScreenCaptureKit, the tap can silence the Mac's own output while it
captures, so the sound only plays on the other computer. Hopflow itself is
left out of the tap, so sound it plays from elsewhere is neither sent back
nor muted. It needs the audio capture permission (NSAudioCaptureUsageDescription).
*/
class MacAudioTap : public IAudioSource
{
public:
  MacAudioTap(AudioCodec codec, bool muteLocal);
  ~MacAudioTap() override;

  void start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed) override;
  void stop() override;

  static bool isAvailable();

  struct Impl;

private:
  std::shared_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
