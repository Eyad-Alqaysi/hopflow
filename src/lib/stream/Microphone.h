/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/AudioBuffer.h"
#include "stream/StreamProtocol.h"

#include <QString>

//! Platform independent pieces of microphone sharing
namespace hopflow::stream {

//! Microphones send raw PCM in 10 ms packets, so the far end hears them within a few packets
inline constexpr uint32_t kMicPacketFrames = kAudioSampleRate / 100;

//! The format every microphone source announces
AudioConfig microphoneConfig();

//! UID of the hidden output of HopflowMic.driver that loops into "Hopflow Microphone"
inline constexpr char kMacMicFeedUid[] = "HopflowMicFeed_UID";

//! Whether \p friendlyName is VB-Audio Virtual Cable's playback side; apps record its other side as a microphone
bool isVirtualCableInput(const QString &friendlyName);

} // namespace hopflow::stream
