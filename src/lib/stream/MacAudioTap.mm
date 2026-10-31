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

#include <QDebug>

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <vector>

using namespace hopflow::stream;

namespace {

// returned by the converter's input callback when it has nothing more to give right now
constexpr OSStatus kNoMoreInput = 'hfnd';

// this long without a single non zero sample is worth a hint about the permission
constexpr int64_t kSilentFramesBeforeHint = int64_t(kAudioSampleRate) * 10;

const QString kAllowAudioCapture = QStringLiteral(
    "allow Hopflow in System Settings, Privacy & Security, Screen & System Audio Recording, "
    "under System Audio Recording Only"
);

AudioObjectPropertyAddress globalAddress(AudioObjectPropertySelector selector)
{
  return AudioObjectPropertyAddress{selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
}

//! Whether the user has turned audio capture off for Hopflow
/*!
A tap that may not record still opens and delivers silence, and macOS has
no public call that tells, so this asks TCC's own preflight. When that
private call is missing it answers false and capture goes ahead.
*/
bool audioCaptureDenied()
{
  using Preflight = int (*)(CFStringRef, CFDictionaryRef);
  static const auto preflight = [] {
    void *tcc = dlopen("/System/Library/PrivateFrameworks/TCC.framework/Versions/A/TCC", RTLD_LAZY);
    return tcc == nullptr ? nullptr : reinterpret_cast<Preflight>(dlsym(tcc, "TCCAccessPreflight"));
  }();
  // 0 allowed, 1 denied, 2 not asked yet
  return preflight != nullptr && preflight(CFSTR("kTCCServiceAudioCapture"), nullptr) == 1;
}

QString captureError(OSStatus status)
{
  return QStringLiteral("cannot capture the sound playing on this Mac (error %1)").arg(status);
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
    const auto in = stereoPcmFormat(true, sampleRate);
    const auto out = stereoPcmFormat();
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
  //! Held while the tap, its private device and the IOProc are made or taken down
  std::mutex lifecycle;
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
  std::unique_ptr<MacDefaultDeviceWatch> watch;
  //! Frames of pure digital silence so far, or -1 once sound was heard or the hint was given
  int64_t silentFrames = 0;

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
      return captureError(status);
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
      return captureError(status);
    }
    return {};
  }

  //! Read the tap on the queue; returns why it failed, or empty
  static QString startIo(const std::shared_ptr<Impl> &impl)
  {
    // the block keeps impl alive until destroyTap() destroys the IOProc
    const auto created = AudioDeviceCreateIOProcIDWithBlock(
        &impl->ioProc, impl->aggregate, impl->queue,
        ^(const AudioTimeStamp *, const AudioBufferList *input, const AudioTimeStamp *, AudioBufferList *,
          const AudioTimeStamp *) {
          impl->captured(input);
        }
    );
    if (created != noErr) {
      impl->ioProc = nullptr;
      return captureError(created);
    }
    if (const auto started = AudioDeviceStart(impl->aggregate, impl->ioProc); started != noErr) {
      return captureError(started);
    }
    return {};
  }

  //! Tap again through the new default output; runs on the watch's queue
  /*!
  The global tap hears every process wherever it plays, but the private
  device it is read through is clocked by the output it was made with,
  which may have just been unplugged.
  */
  API_AVAILABLE(macos(14.2)) static void retap(const std::shared_ptr<Impl> &impl)
  {
    std::scoped_lock lock(impl->lifecycle);
    {
      std::scoped_lock runningLock(impl->mutex);
      if (!impl->running) {
        return;
      }
    }
    impl->destroyTap();
    auto error = impl->createTap();
    if (error.isEmpty()) {
      error = startIo(impl);
    }
    if (!error.isEmpty()) {
      impl->destroyTap();
      impl->fail(error);
    }
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

  //! A denied tap delivers zeros, as does a Mac playing nothing, so this only hints, once; runs on the queue
  void noticeSilence(const std::vector<float> &samples)
  {
    if (silentFrames < 0) {
      return;
    }
    if (std::any_of(samples.begin(), samples.end(), [](float sample) { return sample != 0.0f; })) {
      silentFrames = -1;
      return;
    }
    silentFrames += int64_t(samples.size() / kAudioChannels);
    if (silentFrames >= kSilentFramesBeforeHint) {
      silentFrames = -1;
      qInfo().noquote() << "no sound captured for 10 seconds; if this Mac is playing something," << kAllowAudioCapture;
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
    const UInt32 frames =
        input->mBuffers[0].mDataByteSize / UInt32(sizeof(float) * (planar ? 1 : input->mBuffers[0].mNumberChannels));
    interleaveStereo(*input, format, frames, interleaved);

    const auto samples = rate.convert(std::move(interleaved));
    noticeSilence(samples);
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
    if (audioCaptureDenied()) {
      onFailed(QStringLiteral("Hopflow may not record the sound playing on this Mac: %1").arg(kAllowAudioCapture));
      return;
    }
    std::scoped_lock lock(impl->lifecycle);
    impl->silentFrames = 0;
    if (const auto error = impl->createTap(); !error.isEmpty()) {
      impl->destroyTap();
      onFailed(error);
      return;
    }
    {
      std::scoped_lock runningLock(impl->mutex);
      impl->onPacket = std::move(onPacket);
      impl->onFailed = std::move(onFailed);
      impl->running = true;
    }
    onConfig(impl->encoder.config());

    if (const auto error = Impl::startIo(impl); !error.isEmpty()) {
      impl->fail(error);
      impl->destroyTap();
      return;
    }
    impl->watch =
        std::make_unique<MacDefaultDeviceWatch>(MacDefaultDeviceWatch::Direction::Output, [weak = std::weak_ptr(impl)] {
          if (const auto impl = weak.lock()) {
            Impl::retap(impl);
          }
        });
  } else {
    onFailed(QStringLiteral("muting this Mac while sending its sound needs macOS 14.2 or later"));
  }
}

void MacAudioTap::stop()
{
  m_impl->watch.reset();
  {
    std::scoped_lock lock(m_impl->mutex);
    m_impl->running = false;
  }
  // AudioDeviceStop waits for a running IOProc, so nothing touches the tap afterwards
  std::scoped_lock lock(m_impl->lifecycle);
  m_impl->destroyTap();
}

} // namespace hopflow::stream
