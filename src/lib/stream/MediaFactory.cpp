/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/MediaInterfaces.h"
#include "stream/TestPattern.h"

namespace hopflow::stream {

// platform capture and codecs are added per platform; until then only the
// JPEG test path is available

std::unique_ptr<IVideoSource> createScreenVideoSource()
{
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
  return nullptr;
}

std::unique_ptr<IAudioSink> createAudioSink(AudioCodec)
{
  return std::make_unique<SilentAudioSink>();
}

} // namespace hopflow::stream
