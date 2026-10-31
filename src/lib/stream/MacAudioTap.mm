/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// built with ARC (see CMakeLists.txt)

#include "stream/MacAudioTap.h"

#include "stream/MacAudio.h"

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>

#include <unistd.h>

#include <mutex>
#include <vector>

using namespace hopflow::stream;

namespace {

// returned by the converter's input callback when it has nothing more to give right now
constexpr OSStatus kNoMoreInput = 'hfnd';

AudioObjectPropertyAddress globalAddress(AudioObjectPropertySelector selector)
{
  return AudioObjectPropertyAddress{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

AudioStreamBasicDescription stereoFloat(Float64 sampleRate)
{
  AudioStreamBasicDescription format{};
  format.mSampleRate = sampleRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mChannelsPerFrame = kAudioChannels;
  format.mBitsPerChannel = 32;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = kAudioChannels * sizeof(float);
  format.mBytesPerPacket = format.mBytesPerFrame;
  return format;
}

//! Hopflow's own audio process, or kAudioObjectUnknown if it has none
API_AVAILABLE(macos(14.2)) AudioObjectID ownProcessObject()
{
  const auto address = globalAddress(kAudioHardwarePropertyTranslatePIDToProcessObject);
  pid_t pid = getpid();
  AudioObjectID process = kAudioObjectUnknown;
  UInt32 size = sizeof(process);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(pid), &pid, &size, &process) != noErr) {
    return kAudioObjectUnknown;
  }
  return process;
}

NSString *defaultOutputUid()
{
  auto address = globalAddress(kAudioHardwarePropertyDefaultOutputDevice);
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr, &size, &device) != noErr ||
      device == kAudioObjectUnknown) {
    return nil;
  }

  address = globalAddress(kAudioDevicePropertyDeviceUID);
  CFStringRef uid = nullptr;
  size = sizeof(uid);
  if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &uid) != noErr || uid == nullptr) {
    return nil;
  }
  return (__bridge_transfer NSString *)uid;
}

//! Stereo interleaved floats at the output device's rate in, kAudioSampleRate out
class RateConverter
{
public:
  RateConverter() = default;
  RateConverter(const RateConverter &) = delete;
  RateConverter &operator=(const RateConverter &) = delete;

  ~RateConverter()
  {
    close();
  }

  bool open(Float64 sampleRate)
  {
    close();
    m_ratio = kAudioSampleRate / sampleRate;
    if (sampleRate == kAudioSampleRate) {
      return true;
    }
    const auto in = stereoFloat(sampleRate);
    const auto out = stereoFloat(kAudioSampleRate);
    return AudioConverterNew(&in, &out, &m_converter) == noErr;
  }

  //! \p samples converted to kAudioSampleRate; the converter keeps a little back between calls
  std::vector<float> convert(std::vector<float> samples)
  {
    if (m_converter == nullptr) {
      return samples;
    }

    const auto inputFrames = UInt32(samples.size() / kAudioChannels);
    // room for everything the input can produce, so the converter always asks for more before stopping
    auto outputFrames = UInt32(inputFrames * m_ratio) + 64;
    std::vector<float> out(size_t(outputFrames) * kAudioChannels);
    AudioBufferList list{};
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = kAudioChannels;
    list.mBuffers[0].mDataByteSize = UInt32(out.size() * sizeof(float));
    list.mBuffers[0].mData = out.data();

    Input input{&samples, inputFrames};
    const auto status = AudioConverterFillComplexBuffer(m_converter, provide, &input, &outputFrames, &list, nullptr);
    if (status != noErr && status != kNoMoreInput) {
      return {};
    }
    out.resize(size_t(outputFrames) * kAudioChannels);
    return out;
  }

private:
  struct Input
  {
    std::vector<float> *samples;
    UInt32 frames;
  };

  static OSStatus
  provide(AudioConverterRef, UInt32 *packets, AudioBufferList *data, AudioStreamPacketDescription **, void *user)
  {
    auto *input = static_cast<Input *>(user);
    if (input->frames == 0) {
      *packets = 0;
      return kNoMoreInput;
    }
    data->mBuffers[0].mData = input->samples->data();
    data->mBuffers[0].mDataByteSize = input->frames * kAudioChannels * sizeof(float);
    data->mBuffers[0].mNumberChannels = kAudioChannels;
    *packets = input->frames;
    input->frames = 0;
    return noErr;
  }

  void close()
  {
    if (m_converter != nullptr) {
      AudioConverterDispose(m_converter);
      m_converter = nullptr;
    }
  }

  AudioConverterRef m_converter = nullptr;
  Float64 m_ratio = 1.0;
};

} // namespace

namespace hopflow::stream {

struct MacAudioTap::Impl
{
  Impl(AudioCodec codec, bool muteLocal) : encoder(codec), muteLocal(muteLocal)
  {
  }

  std::mutex mutex;
  bool running = false;
  MacSoundEncoder encoder;
  const bool muteLocal;
  IAudioSource::PacketReady onPacket;
  IAudioSource::Failed onFailed;
  dispatch_queue_t queue = dispatch_queue_create("io.github.eyad_alqaysi.hopflow.audiotap", DISPATCH_QUEUE_SERIAL);
  AudioObjectID tap = kAudioObjectUnknown;
  AudioObjectID aggregate = kAudioObjectUnknown;
  AudioDeviceIOProcID ioProc = nullptr;
  AudioStreamBasicDescription format{};
  RateConverter rate;
  std::vector<float> interleaved;

  //! Create the tap and the private device it is read through; returns why it failed, or empty
  API_AVAILABLE(macos(14.2)) QString createTap()
  {
    const auto process = ownProcessObject();
    NSArray<NSNumber *> *excluded = process == kAudioObjectUnknown ? @[] : @[ @(process) ];
    CATapDescription *description = [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:excluded];
    description.name = @"Hopflow";
    description.privateTap = YES;
    description.muteBehavior = muteLocal ? CATapMutedWhenTapped : CATapUnmuted;
    if (const auto status = AudioHardwareCreateProcessTap(description, &tap); status != noErr) {
      tap = kAudioObjectUnknown;
      return QStringLiteral("cannot capture the sound playing on this Mac (error %1)").arg(status);
    }

    const auto formatAddress = globalAddress(kAudioTapPropertyFormat);
    UInt32 size = sizeof(format);
    if (AudioObjectGetPropertyData(tap, &formatAddress, 0, nullptr, &size, &format) != noErr ||
        format.mFormatID != kAudioFormatLinearPCM || (format.mFormatFlags & kAudioFormatFlagIsFloat) == 0 ||
        format.mChannelsPerFrame == 0 || !rate.open(format.mSampleRate)) {
      return QStringLiteral("the sound playing on this Mac is in a format Hopflow cannot send");
    }

    // the output device clocks the private device the tap is read through
    NSString *output = defaultOutputUid();
    if (output == nil) {
      return QStringLiteral("this Mac has no sound output to capture");
    }
    NSDictionary *device = @{
      @kAudioAggregateDeviceNameKey : @"Hopflow Sound",
      @kAudioAggregateDeviceUIDKey : [NSUUID UUID].UUIDString,
      @kAudioAggregateDeviceMainSubDeviceKey : output,
      @kAudioAggregateDeviceIsPrivateKey : @YES,
      @kAudioAggregateDeviceIsStackedKey : @NO,
      @kAudioAggregateDeviceTapAutoStartKey : @YES,
      @kAudioAggregateDeviceSubDeviceListKey : @[
        @{@kAudioSubDeviceUIDKey : output}
      ],
      @kAudioAggregateDeviceTapListKey : @[
        @{@kAudioSubTapDriftCompensationKey : @YES,
          @kAudioSubTapUIDKey : description.UUID.UUIDString}
      ],
    };
    if (const auto status = AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)device, &aggregate);
        status != noErr) {
      aggregate = kAudioObjectUnknown;
      return QStringLiteral("cannot capture the sound playing on this Mac (error %1)").arg(status);
    }
    return {};
  }

  void destroyTap()
  {
    if (ioProc != nullptr) {
      AudioDeviceStop(aggregate, ioProc);
      AudioDeviceDestroyIOProcID(aggregate, ioProc);
      ioProc = nullptr;
    }
    if (aggregate != kAudioObjectUnknown) {
      AudioHardwareDestroyAggregateDevice(aggregate);
      aggregate = kAudioObjectUnknown;
    }
    if (@available(macOS 14.2, *)) {
      if (tap != kAudioObjectUnknown) {
        AudioHardwareDestroyProcessTap(tap);
        tap = kAudioObjectUnknown;
      }
    }
  }

  void fail(const QString &message)
  {
    std::scoped_lock lock(mutex);
    if (running && onFailed) {
      running = false;
      onFailed(message);
    }
  }

  //! Runs on the queue
  void captured(const AudioBufferList *input)
  {
    if (input == nullptr || input->mNumberBuffers == 0) {
      return;
    }

    // a tap delivers floats, one buffer per channel or interleaved; the encoder wants stereo interleaved
    const bool planar = (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    const UInt32 channels = format.mChannelsPerFrame;
    const UInt32 frames =
        input->mBuffers[0].mDataByteSize / UInt32(sizeof(float) * (planar ? 1 : input->mBuffers[0].mNumberChannels));
    interleaved.assign(size_t(frames) * kAudioChannels, 0.0f);
    for (UInt32 channel = 0; channel < kAudioChannels; ++channel) {
      const UInt32 source = std::min(channel, channels - 1);
      const auto &buffer = input->mBuffers[planar ? std::min(source, input->mNumberBuffers - 1) : 0];
      const auto *data = static_cast<const float *>(buffer.mData);
      if (data == nullptr) {
        continue;
      }
      for (UInt32 frame = 0; frame < frames; ++frame) {
        interleaved[size_t(frame) * kAudioChannels + channel] =
            planar ? data[frame] : data[size_t(frame) * buffer.mNumberChannels + source];
      }
    }

    const auto samples = rate.convert(std::move(interleaved));
    const auto packets = encoder.encode(samples.data(), UInt32(samples.size() / kAudioChannels));
    std::scoped_lock lock(mutex);
    if (running) {
      for (const auto &packet : packets) {
        onPacket(packet);
      }
    }
  }
};

MacAudioTap::MacAudioTap(AudioCodec codec, bool muteLocal) : m_impl(std::make_shared<Impl>(codec, muteLocal))
{
}

MacAudioTap::~MacAudioTap()
{
  stop();
}

bool MacAudioTap::isAvailable()
{
  if (@available(macOS 14.2, *)) {
    return true;
  }
  return false;
}

void MacAudioTap::start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed)
{
  stop();
  if (@available(macOS 14.2, *)) {
    auto impl = m_impl;
    if (!impl->encoder.open()) {
      onFailed(QStringLiteral("cannot start the sound encoder"));
      return;
    }
    if (const auto error = impl->createTap(); !error.isEmpty()) {
      impl->destroyTap();
      onFailed(error);
      return;
    }
    {
      std::scoped_lock lock(impl->mutex);
      impl->onPacket = std::move(onPacket);
      impl->onFailed = std::move(onFailed);
      impl->running = true;
    }
    onConfig(impl->encoder.config());

    // the block keeps impl alive until stop() destroys the IOProc
    const auto created = AudioDeviceCreateIOProcIDWithBlock(
        &impl->ioProc, impl->aggregate, impl->queue,
        ^(const AudioTimeStamp *, const AudioBufferList *input, const AudioTimeStamp *, AudioBufferList *,
          const AudioTimeStamp *) {
          impl->captured(input);
        }
    );
    if (created != noErr) {
      impl->ioProc = nullptr;
      impl->fail(QStringLiteral("cannot capture the sound playing on this Mac (error %1)").arg(created));
      impl->destroyTap();
      return;
    }
    if (const auto started = AudioDeviceStart(impl->aggregate, impl->ioProc); started != noErr) {
      impl->fail(QStringLiteral("cannot capture the sound playing on this Mac (error %1)").arg(started));
      impl->destroyTap();
    }
  } else {
    onFailed(QStringLiteral("muting this Mac while sending its sound needs macOS 14.2 or later"));
  }
}

void MacAudioTap::stop()
{
  {
    std::scoped_lock lock(m_impl->mutex);
    m_impl->running = false;
  }
  // AudioDeviceStop waits for a running IOProc, so nothing touches the tap afterwards
  m_impl->destroyTap();
}

} // namespace hopflow::stream
