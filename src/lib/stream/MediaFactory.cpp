/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/MediaInterfaces.h"
#include "stream/TestPattern.h"

#if defined(__APPLE__)
#include "stream/MacAudio.h"
#include "stream/MacAudioTap.h"
#include "stream/MacMicrophone.h"
#include "stream/MacVideo.h"
#include "stream/MacVirtualDisplay.h"
#elif defined(_WIN32)
#include "stream/WinAudio.h"
#include "stream/WinMicrophone.h"
#include "stream/WinVideo.h"
#include "stream/WinVirtualDisplay.h"
#endif

namespace hopflow::stream {

std::unique_ptr<IVideoSource> createScreenVideoSource(const QString &displayId)
{
#if defined(__APPLE__)
  bool isDisplayId = true;
  const uint32_t display = displayId.isEmpty() ? 0 : displayId.toUInt(&isDisplayId);
  if (isDisplayId && MacScreenSource::isAvailable()) {
    return std::make_unique<MacScreenSource>(display);
  }
#elif defined(_WIN32)
  return std::make_unique<WinScreenSource>(displayId);
#endif
  return nullptr;
}

std::unique_ptr<IAudioSource> createSystemAudioSource([[maybe_unused]] bool muteLocal, AudioLatency latency)
{
  [[maybe_unused]] const auto codec = latency == AudioLatency::Low ? AudioCodec::Pcm16 : AudioCodec::Aac;
#if defined(__APPLE__)
  // only a process tap can mute the Mac; before macOS 14.2 the sound also keeps playing here
  if (muteLocal && MacAudioTap::isAvailable()) {
    return std::make_unique<MacAudioTap>(codec, muteLocal);
  }
  if (MacSystemAudioSource::isAvailable()) {
    return std::make_unique<MacSystemAudioSource>(codec);
  }
#elif defined(_WIN32)
  return std::make_unique<WinSystemAudioSource>(codec, muteLocal);
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
#if defined(__APPLE__)
  return std::make_unique<MacVirtualDisplay>();
#elif defined(_WIN32)
  return std::make_unique<WinVirtualDisplay>();
#else
  return nullptr;
#endif
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
  return QStringLiteral(
      "the virtual microphone driver HopflowMic.driver is not installed: run "
      "/Applications/Hopflow.app/Contents/Resources/install-hopflow-mic.sh in Terminal, "
      "then choose \"Hopflow Microphone\" in your apps"
  );
#elif defined(_WIN32)
  return QStringLiteral(
      "no virtual microphone found: install VB-Audio Virtual Cable from https://vb-audio.com/Cable/, "
      "then choose \"CABLE Output\" as the microphone in your apps"
  );
#else
  return QStringLiteral("a virtual microphone is not available on this computer");
#endif
}

std::unique_ptr<IAudioSink> createAudioSink(AudioCodec, [[maybe_unused]] AudioLatency latency)
{
#if defined(__APPLE__)
  return std::make_unique<MacAudioSink>(latency);
#elif defined(_WIN32)
  return std::make_unique<WinAudioSink>(latency);
#else
  return std::make_unique<SilentAudioSink>();
#endif
}

} // namespace hopflow::stream
