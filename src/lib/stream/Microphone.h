/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/AudioBuffer.h"
#include "stream/StreamProtocol.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <vector>

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

//! Interleaved floats in [-1, 1] to 16 bit PCM, clamping what is out of range
QByteArray floatToPcm16(const float *samples, size_t count);

//! 16 bit PCM to interleaved floats
std::vector<float> pcm16ToFloat(const QByteArray &pcm);

} // namespace hopflow::stream
