/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/Microphone.h"

namespace hopflow::stream {

AudioConfig microphoneConfig()
{
  return AudioConfig{AudioCodec::Pcm16, kAudioSampleRate, kAudioChannels, {}};
}

bool isVirtualCableInput(const QString &friendlyName)
{
  // Windows calls it "CABLE Input (VB-Audio Virtual Cable)"
  return friendlyName.contains(QStringLiteral("CABLE Input"), Qt::CaseInsensitive);
}

} // namespace hopflow::stream
