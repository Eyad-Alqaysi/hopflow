/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/MediaInterfaces.h"
#include "stream/TestPattern.h"

#if defined(__APPLE__)
#include "stream/MacVideo.h"
#elif defined(_WIN32)
#include "stream/WinVideo.h"
#endif

namespace hopflow::stream {

std::unique_ptr<IVideoSource> createScreenVideoSource()
{
#if defined(__APPLE__)
  if (MacScreenSource::isAvailable()) {
    return std::make_unique<MacScreenSource>();
  }
#elif defined(_WIN32)
  return std::make_unique<WinScreenSource>();
#endif
  return nullptr;
}

std::unique_ptr<IAudioSource> createSystemAudioSource()
{
  return nullptr;
}

std::unique_ptr<IVideoDecoder> createVideoDecoder(VideoCodec codec)
{
  if (codec == VideoCodec::Jpeg) {
    return std::make_unique<JpegDecoder>();
  }
#if defined(__APPLE__)
  return std::make_unique<MacVideoDecoder>();
#elif defined(_WIN32)
  return std::make_unique<WinVideoDecoder>();
#else
  return nullptr;
#endif
}

std::unique_ptr<IAudioSink> createAudioSink(AudioCodec)
{
  return std::make_unique<SilentAudioSink>();
}

} // namespace hopflow::stream
