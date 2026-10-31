/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/Microphone.h"

#include <algorithm>
#include <cstdint>

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

QByteArray floatToPcm16(const float *samples, size_t count)
{
  QByteArray pcm(qsizetype(count * sizeof(int16_t)), Qt::Uninitialized);
  auto *out = reinterpret_cast<int16_t *>(pcm.data());
  for (size_t i = 0; i < count; ++i) {
    out[i] = int16_t(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f);
  }
  return pcm;
}

std::vector<float> pcm16ToFloat(const QByteArray &pcm)
{
  const auto *in = reinterpret_cast<const int16_t *>(pcm.constData());
  std::vector<float> samples(size_t(pcm.size()) / sizeof(int16_t));
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = float(in[i]) / 32768.0f;
  }
  return samples;
}

} // namespace hopflow::stream
