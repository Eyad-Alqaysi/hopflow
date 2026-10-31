/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// built with ARC (see CMakeLists.txt)

#include "stream/MacMicrophone.h"

#include "stream/AudioBuffer.h"
#include "stream/MacAudio.h"
#include "stream/Microphone.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>

#include <mutex>
#include <utility>

using namespace hopflow::stream;

namespace {

//! The hidden "Hopflow Mic Feed" device; it is left out of the device list, but its UID still finds it
AudioObjectID feedDevice()
{
  CFStringRef uid = (__bridge CFStringRef) @(kMacMicFeedUid);
  const AudioObjectPropertyAddress address{
      kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain
  };
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(uid), &uid, &size, &device) != noErr) {
    return kAudioObjectUnknown;
  }
  return device;
}

QString permissionDenied()
{
  return QStringLiteral(
      "Hopflow may not use the microphone: allow it in System Settings, Privacy & Security, Microphone"
  );
}

} // namespace

namespace hopflow::stream {

//
// MacMicrophoneSource
//

struct MacMicrophoneSource::Impl
{
  static constexpr int kBufferCount = 4;

  std::mutex mutex;
  bool running = false;
  AudioQueueRef queue = nullptr;
  IAudioSource::PacketReady onPacket;
  IAudioSource::Failed onFailed;
  int64_t capturedFrames = 0;

  void fail(const QString &message)
  {
    std::scoped_lock lock(mutex);
    if (running && onFailed) {
      running = false;
      onFailed(message);
    }
  }

  //! Start capturing once permission is granted; any thread
  void open()
  {
    std::scoped_lock lock(mutex);
    if (!running || queue != nullptr) {
      return;
    }

    // Audio Queue converts whatever the microphone gives, mono included, to 48 kHz stereo
    const auto format = stereoPcmFormat(false);
    const UInt32 bufferBytes = kMicPacketFrames * format.mBytesPerFrame;
    if (AudioQueueNewInput(&format, captured, this, nullptr, kCFRunLoopCommonModes, 0, &queue) != noErr) {
      queue = nullptr;
      running = false;
      onFailed(QStringLiteral("cannot open the microphone on this Mac"));
      return;
    }
    for (int i = 0; i < kBufferCount; ++i) {
      AudioQueueBufferRef buffer = nullptr;
      if (AudioQueueAllocateBuffer(queue, bufferBytes, &buffer) == noErr) {
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
      }
    }
    if (AudioQueueStart(queue, nullptr) != noErr) {
      running = false;
      onFailed(QStringLiteral("this Mac has no microphone to send"));
    }
  }

  //! Runs on the queue's own thread
  static void captured(
      void *user, AudioQueueRef queue, AudioQueueBufferRef buffer, const AudioTimeStamp *, UInt32,
      const AudioStreamPacketDescription *
  )
  {
    auto *self = static_cast<Impl *>(user);
    std::scoped_lock lock(self->mutex);
    if (!self->running) {
      return;
    }
    const auto bytes = qsizetype(buffer->mAudioDataByteSize);
    if (bytes > 0) {
      const auto timestamp = self->capturedFrames * 1'000'000 / kAudioSampleRate;
      self->capturedFrames += bytes / qsizetype(kAudioChannels * sizeof(int16_t));
      self->onPacket(EncodedAudio{timestamp, QByteArray(static_cast<const char *>(buffer->mAudioData), bytes)});
    }
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  }
};

MacMicrophoneSource::MacMicrophoneSource() : m_impl(std::make_shared<Impl>())
{
}

MacMicrophoneSource::~MacMicrophoneSource()
{
  stop();
}

void MacMicrophoneSource::start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed)
{
  stop();
  auto impl = m_impl;
  {
    std::scoped_lock lock(impl->mutex);
    impl->onPacket = std::move(onPacket);
    impl->onFailed = std::move(onFailed);
    impl->capturedFrames = 0;
    impl->running = true;
  }
  onConfig(microphoneConfig());

  const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
  if (status == AVAuthorizationStatusAuthorized) {
    impl->open();
  } else if (status == AVAuthorizationStatusNotDetermined) {
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                               if (granted) {
                                 impl->open();
                               } else {
                                 impl->fail(permissionDenied());
                               }
                             }];
  } else {
    impl->fail(permissionDenied());
  }
}

void MacMicrophoneSource::stop()
{
  AudioQueueRef queue = nullptr;
  {
    std::scoped_lock lock(m_impl->mutex);
    m_impl->running = false;
    queue = std::exchange(m_impl->queue, nullptr);
  }
  // outside the lock: disposing waits for a callback that may be waiting for it
  if (queue != nullptr) {
    AudioQueueStop(queue, true);
    AudioQueueDispose(queue, true);
  }
}

//
// MacVirtualMicSink
//

struct MacVirtualMicSink::Impl
{
  // a microphone has to keep up with the conversation: start at 20 ms, never more than 80 ms
  PcmJitterBuffer buffer{kAudioChannels, kAudioSampleRate / 50, kAudioSampleRate * 2 / 25};
  MacAacDecoder decoder;
  MacPcmOutput output{buffer};
  bool pcm = false;
};

MacVirtualMicSink::MacVirtualMicSink() : m_impl(std::make_unique<Impl>())
{
}

MacVirtualMicSink::~MacVirtualMicSink() = default;

bool MacVirtualMicSink::isAvailable()
{
  return feedDevice() != kAudioObjectUnknown;
}

bool MacVirtualMicSink::configure(const AudioConfig &config)
{
  if (config.sampleRate != kAudioSampleRate || config.channels != kAudioChannels) {
    return false;
  }
  m_impl->pcm = config.codec == AudioCodec::Pcm16;
  if (!m_impl->pcm && !m_impl->decoder.open()) {
    return false;
  }
  const auto device = feedDevice();
  // 5 ms at a time keeps the microphone close to live
  return device != kAudioObjectUnknown && m_impl->output.start(device, kAudioSampleRate / 200);
}

void MacVirtualMicSink::play(const EncodedAudio &packet)
{
  const auto samples = m_impl->pcm ? pcm16ToFloat(packet.data) : m_impl->decoder.decode(packet.data);
  if (!samples.empty()) {
    m_impl->buffer.push(samples.data(), UInt32(samples.size() / kAudioChannels));
  }
}

int64_t MacVirtualMicSink::positionUs() const
{
  return -1;
}

} // namespace hopflow::stream
