/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/MediaInterfaces.h"
#include "stream/TestPattern.h"

#if defined(__APPLE__)
#include "stream/MacAudio.h"
#include "stream/MacMicrophone.h"
#include "stream/MacVideo.h"
#elif defined(_WIN32)
#include "stream/WinAudio.h"
#include "stream/WinMicrophone.h"
#include "stream/WinVideo.h"
#endif

namespace hopflow::stream {

std::unique_ptr<IVideoSource> createScreenVideoSource(const QString &displayId)
{
  if (!displayId.isEmpty()) {
    // capture of a chosen display is added per platform with the virtual display
    return nullptr;
  }
#if defined(__APPLE__)
  if (MacScreenSource::isAvailable()) {
    return std::make_unique<MacScreenSource>();
  }
#elif defined(_WIN32)
  return std::make_unique<WinScreenSource>();
#endif
  return nullptr;
}

std::unique_ptr<IAudioSource> createSystemAudioSource(bool)
{
#if defined(__APPLE__)
  if (MacSystemAudioSource::isAvailable()) {
    return std::make_unique<MacSystemAudioSource>();
  }
#elif defined(_WIN32)
  return std::make_unique<WinSystemAudioSource>();
#endif
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

std::unique_ptr<IAudioSource> createMicrophoneSource()
{
#if defined(__APPLE__)
  return std::make_unique<MacMicrophoneSource>();
#elif defined(_WIN32)
  return std::make_unique<WinMicrophoneSource>();
#else
  return nullptr;
#endif
}

std::unique_ptr<IVirtualDisplay> createVirtualDisplay()
{
  return nullptr;
}

std::unique_ptr<IAudioSink> createVirtualMicSink(AudioCodec)
{
#if defined(__APPLE__)
  if (MacVirtualMicSink::isAvailable()) {
    return std::make_unique<MacVirtualMicSink>();
  }
#elif defined(_WIN32)
  if (auto device = WinVirtualMicSink::findDevice(); !device.isEmpty()) {
    return std::make_unique<WinVirtualMicSink>(device);
  }
#endif
  return nullptr;
}

QString virtualMicUnavailableReason()
{
#if defined(__APPLE__)
  return QStringLiteral("the virtual microphone driver HopflowMic.driver is not installed: run "
                        "/Applications/Hopflow.app/Contents/Resources/install-hopflow-mic.sh in Terminal, "
                        "then choose \"Hopflow Microphone\" in your apps");
#elif defined(_WIN32)
  return QStringLiteral("no virtual microphone found: install VB-Audio Virtual Cable from https://vb-audio.com/Cable/, "
                        "then choose \"CABLE Output\" as the microphone in your apps");
#else
  return QStringLiteral("a virtual microphone is not available on this computer");
#endif
}

std::unique_ptr<IAudioSink> createAudioSink(AudioCodec, AudioLatency)
{
#if defined(__APPLE__)
  return std::make_unique<MacAudioSink>();
#elif defined(_WIN32)
  return std::make_unique<WinAudioSink>();
#else
  return std::make_unique<SilentAudioSink>();
#endif
}

} // namespace hopflow::stream
