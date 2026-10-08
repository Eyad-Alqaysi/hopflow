/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// built with ARC (see CMakeLists.txt)

#include "stream/MacAudio.h"

#include "stream/AudioBuffer.h"

#import <AudioToolbox/AudioToolbox.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <mutex>

using namespace hopflow::stream;

namespace {

// returned by input callbacks when they have nothing more to give right now
constexpr OSStatus kNoMoreInput = 'hfnd';

AudioStreamBasicDescription pcmFormat()
{
  AudioStreamBasicDescription format{};
  format.mSampleRate = kAudioSampleRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  format.mChannelsPerFrame = kAudioChannels;
  format.mBitsPerChannel = 32;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = kAudioChannels * sizeof(float);
  format.mBytesPerPacket = format.mBytesPerFrame;
  return format;
}

AudioStreamBasicDescription aacFormat()
{
  AudioStreamBasicDescription format{};
  format.mSampleRate = kAudioSampleRate;
  format.mFormatID = kAudioFormatMPEG4AAC;
  format.mFormatFlags = kMPEG4Object_AAC_LC;
  format.mChannelsPerFrame = kAudioChannels;
  format.mFramesPerPacket = kAacFramesPerPacket;
  return format;
}

} // namespace

namespace hopflow::stream {

//
// MacAacEncoder
//

struct MacAacEncoder::Impl
{
  AudioConverterRef converter = nullptr;
  std::vector<float> pending;
  size_t offered = 0; // frames handed to the converter in the current call
  bool gaveInput = false;
  UInt32 maxPacketSize = 0;
  int64_t packets = 0;

  ~Impl()
  {
    if (converter != nullptr) {
      AudioConverterDispose(converter);
    }
  }

  static OSStatus
  provide(AudioConverterRef, UInt32 *packets, AudioBufferList *data, AudioStreamPacketDescription **, void *user)
  {
    auto *self = static_cast<Impl *>(user);
    if (self->gaveInput) {
      *packets = 0;
      return kNoMoreInput;
    }

    // one packet's worth at a time
    const auto frames = std::min<UInt32>(kAacFramesPerPacket, UInt32(self->pending.size() / kAudioChannels));
    data->mBuffers[0].mData = self->pending.data();
    data->mBuffers[0].mDataByteSize = frames * kAudioChannels * sizeof(float);
    data->mBuffers[0].mNumberChannels = kAudioChannels;
    *packets = frames;
    self->offered = frames;
    self->gaveInput = true;
    return noErr;
  }
};

MacAacEncoder::MacAacEncoder() : m_impl(std::make_unique<Impl>())
{
}

MacAacEncoder::~MacAacEncoder() = default;

bool MacAacEncoder::open()
{
  const auto in = pcmFormat();
  const auto out = aacFormat();
  if (AudioConverterNew(&in, &out, &m_impl->converter) != noErr) {
    return false;
  }
  UInt32 bitrate = kAacBitrate;
  AudioConverterSetProperty(m_impl->converter, kAudioConverterEncodeBitRate, sizeof(bitrate), &bitrate);
  UInt32 size = sizeof(m_impl->maxPacketSize);
  AudioConverterGetProperty(
      m_impl->converter, kAudioConverterPropertyMaximumOutputPacketSize, &size, &m_impl->maxPacketSize
  );
  return m_impl->maxPacketSize > 0;
}

std::vector<EncodedAudio> MacAacEncoder::encode(const float *samples, uint32_t frames)
{
  std::vector<EncodedAudio> out;
  if (m_impl->converter == nullptr) {
    return out;
  }

  m_impl->pending.insert(m_impl->pending.end(), samples, samples + size_t(frames) * kAudioChannels);
  QByteArray packet(qsizetype(m_impl->maxPacketSize), Qt::Uninitialized);

  while (m_impl->pending.size() >= size_t(kAacFramesPerPacket) * kAudioChannels) {
    AudioBufferList list{};
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = kAudioChannels;
    list.mBuffers[0].mDataByteSize = m_impl->maxPacketSize;
    list.mBuffers[0].mData = packet.data();
    UInt32 count = 1;
    AudioStreamPacketDescription description{};

    m_impl->gaveInput = false;
    m_impl->offered = 0;
    const auto status =
        AudioConverterFillComplexBuffer(m_impl->converter, Impl::provide, m_impl.get(), &count, &list, &description);
    m_impl->pending.erase(
        m_impl->pending.begin(), m_impl->pending.begin() + std::ptrdiff_t(m_impl->offered * kAudioChannels)
    );
    if (status != noErr && status != kNoMoreInput) {
      break;
    }

    // the encoder's first packets only prime it and come out later
    if (count > 0 && list.mBuffers[0].mDataByteSize > 0) {
      const auto timestamp = m_impl->packets * kAacFramesPerPacket * 1'000'000 / kAudioSampleRate;
      out.push_back(EncodedAudio{timestamp, packet.left(qsizetype(list.mBuffers[0].mDataByteSize))});
      ++m_impl->packets;
    }
  }
  return out;
}

//
// MacAacDecoder
//

struct MacAacDecoder::Impl
{
  AudioConverterRef converter = nullptr;
  const QByteArray *input = nullptr;
  AudioStreamPacketDescription description{};

  ~Impl()
  {
    if (converter != nullptr) {
      AudioConverterDispose(converter);
    }
  }

  static OSStatus provide(
      AudioConverterRef, UInt32 *packets, AudioBufferList *data, AudioStreamPacketDescription **descriptions, void *user
  )
  {
    auto *self = static_cast<Impl *>(user);
    if (self->input == nullptr) {
      *packets = 0;
      return kNoMoreInput;
    }

    self->description = AudioStreamPacketDescription{0, 0, UInt32(self->input->size())};
    data->mBuffers[0].mData = const_cast<char *>(self->input->constData());
    data->mBuffers[0].mDataByteSize = UInt32(self->input->size());
    data->mBuffers[0].mNumberChannels = kAudioChannels;
    if (descriptions != nullptr) {
      *descriptions = &self->description;
    }
    *packets = 1;
    self->input = nullptr;
    return noErr;
  }
};

MacAacDecoder::MacAacDecoder() : m_impl(std::make_unique<Impl>())
{
}

MacAacDecoder::~MacAacDecoder() = default;

bool MacAacDecoder::open()
{
  const auto in = aacFormat();
  const auto out = pcmFormat();
  return AudioConverterNew(&in, &out, &m_impl->converter) == noErr;
}

std::vector<float> MacAacDecoder::decode(const QByteArray &packet)
{
  std::vector<float> samples(size_t(kAacFramesPerPacket) * kAudioChannels);
  if (m_impl->converter == nullptr || packet.isEmpty()) {
    return {};
  }

  AudioBufferList list{};
  list.mNumberBuffers = 1;
  list.mBuffers[0].mNumberChannels = kAudioChannels;
  list.mBuffers[0].mDataByteSize = UInt32(samples.size() * sizeof(float));
  list.mBuffers[0].mData = samples.data();
  UInt32 frames = kAacFramesPerPacket;

  m_impl->input = &packet;
  const auto status =
      AudioConverterFillComplexBuffer(m_impl->converter, Impl::provide, m_impl.get(), &frames, &list, nullptr);
  m_impl->input = nullptr;
  if (status != noErr && status != kNoMoreInput) {
    return {};
  }
  samples.resize(size_t(frames) * kAudioChannels);
  return samples;
}

} // namespace hopflow::stream

//
// MacSystemAudioSource
//

namespace hopflow::stream {

struct MacSystemAudioSource::Impl
{
  std::mutex mutex;
  bool running = false;
  MacAacEncoder encoder;
  IAudioSource::PacketReady onPacket;
  IAudioSource::Failed onFailed;
  dispatch_queue_t queue = dispatch_queue_create("io.github.eyad_alqaysi.hopflow.audio", DISPATCH_QUEUE_SERIAL);
  id stream = nil;
  id output = nil;
  std::vector<float> interleaved;

  void fail(const QString &message)
  {
    std::scoped_lock lock(mutex);
    if (running && onFailed) {
      running = false;
      onFailed(message);
    }
  }

  //! Runs on the audio queue
  void captured(CMSampleBufferRef sample)
  {
    const auto *format = CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sample));
    if (format == nullptr || format->mFormatID != kAudioFormatLinearPCM ||
        (format->mFormatFlags & kAudioFormatFlagIsFloat) == 0) {
      return;
    }

    AudioBufferList list{};
    CMBlockBufferRef retained = nullptr;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(
            sample, nullptr, &list, sizeof(list), nullptr, nullptr, 0, &retained
        ) != noErr) {
      return;
    }

    // ScreenCaptureKit delivers one buffer per channel; the encoder wants them interleaved
    const auto frames = UInt32(CMSampleBufferGetNumSamples(sample));
    interleaved.assign(size_t(frames) * kAudioChannels, 0.0f);
    const bool planar = (format->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
    for (UInt32 channel = 0; channel < kAudioChannels; ++channel) {
      const UInt32 source = std::min<UInt32>(channel, format->mChannelsPerFrame - 1);
      for (UInt32 frame = 0; frame < frames; ++frame) {
        const float *data =
            planar ? static_cast<const float *>(list.mBuffers[std::min(source, list.mNumberBuffers - 1)].mData)
                   : static_cast<const float *>(list.mBuffers[0].mData);
        interleaved[size_t(frame) * kAudioChannels + channel] =
            planar ? data[frame] : data[size_t(frame) * format->mChannelsPerFrame + source];
      }
    }
    CFRelease(retained);

    const auto packets = encoder.encode(interleaved.data(), frames);
    std::scoped_lock lock(mutex);
    if (running) {
      for (const auto &packet : packets) {
        onPacket(packet);
      }
    }
  }
};

} // namespace hopflow::stream

API_AVAILABLE(macos(13.0))
@interface HopflowAudioOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
  std::shared_ptr<MacSystemAudioSource::Impl> impl;
}
@end

@implementation HopflowAudioOutput

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type
{
  if (type == SCStreamOutputTypeAudio && CMSampleBufferDataIsReady(sample)) {
    impl->captured(sample);
  }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
  impl->fail(QStringLiteral("sound capture stopped"));
}

@end

namespace hopflow::stream {

MacSystemAudioSource::MacSystemAudioSource() : m_impl(std::make_shared<Impl>())
{
}

MacSystemAudioSource::~MacSystemAudioSource()
{
  stop();
}

bool MacSystemAudioSource::isAvailable()
{
  if (@available(macOS 13.0, *)) {
    return true;
  }
  return false;
}

void MacSystemAudioSource::start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed)
{
  if (@available(macOS 13.0, *)) {
    auto impl = m_impl;
    if (!impl->encoder.open()) {
      onFailed(QStringLiteral("cannot start the sound encoder"));
      return;
    }
    {
      std::scoped_lock lock(impl->mutex);
      impl->onPacket = std::move(onPacket);
      impl->onFailed = std::move(onFailed);
      impl->running = true;
    }
    onConfig(
        AudioConfig{
            AudioCodec::Aac, kAudioSampleRate, kAudioChannels, aacAudioSpecificConfig(kAudioSampleRate, kAudioChannels)
        }
    );

    [SCShareableContent
        getShareableContentExcludingDesktopWindows:NO
                               onScreenWindowsOnly:YES
                                 completionHandler:^(SCShareableContent *content, NSError *error) {
                                   if (error != nil || content.displays.count == 0) {
                                     impl->fail(QStringLiteral("sound capture needs Screen Recording permission"));
                                     return;
                                   }
                                   dispatch_async(impl->queue, ^{
                                     {
                                       std::scoped_lock lock(impl->mutex);
                                       if (!impl->running) {
                                         return;
                                       }
                                     }
                                     SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
                                     config.capturesAudio = YES;
                                     // not the sound of screens this Mac is showing from elsewhere
                                     config.excludesCurrentProcessAudio = YES;
                                     config.sampleRate = kAudioSampleRate;
                                     config.channelCount = kAudioChannels;
                                     // the picture comes from the video stream; keep this one tiny
                                     config.width = 2;
                                     config.height = 2;
                                     config.minimumFrameInterval = CMTimeMake(1, 1);

                                     SCContentFilter *filter =
                                         [[SCContentFilter alloc] initWithDisplay:content.displays.firstObject
                                                                 excludingWindows:@[]];
                                     HopflowAudioOutput *output = [[HopflowAudioOutput alloc] init];
                                     output->impl = impl;
                                     SCStream *stream = [[SCStream alloc] initWithFilter:filter
                                                                           configuration:config
                                                                                delegate:output];
                                     NSError *addError = nil;
                                     if (![stream addStreamOutput:output
                                                             type:SCStreamOutputTypeAudio
                                               sampleHandlerQueue:impl->queue
                                                            error:&addError]) {
                                       impl->fail(QStringLiteral("cannot capture sound"));
                                       return;
                                     }
                                     impl->stream = stream;
                                     impl->output = output;
                                     [stream startCaptureWithCompletionHandler:^(NSError *startError) {
                                       if (startError != nil) {
                                         impl->fail(QStringLiteral("cannot capture sound"));
                                       }
                                     }];
                                   });
                                 }];
  } else {
    onFailed(QStringLiteral("sharing sound needs macOS 13 or later"));
  }
}

void MacSystemAudioSource::stop()
{
  auto impl = m_impl;
  {
    std::scoped_lock lock(impl->mutex);
    impl->running = false;
  }
  dispatch_async(impl->queue, ^{
    if (@available(macOS 13.0, *)) {
      if (SCStream *stream = impl->stream) {
        [stream stopCaptureWithCompletionHandler:^(NSError *) {
          (void)impl;
        }];
      }
    }
    impl->stream = nil;
    impl->output = nil;
  });
}

//
// MacAudioSink
//

struct MacAudioSink::Impl
{
  // start once 50 ms are buffered, never let more than 250 ms build up
  PcmJitterBuffer buffer{kAudioChannels, kAudioSampleRate / 20, kAudioSampleRate / 4};
  MacAacDecoder decoder;
  AudioQueueRef queue = nullptr;
  bool pcm = false;

  static constexpr UInt32 kBufferFrames = kAudioSampleRate / 100; // 10 ms
  static constexpr int kBufferCount = 3;

  ~Impl()
  {
    if (queue != nullptr) {
      AudioQueueStop(queue, true);
      AudioQueueDispose(queue, true);
    }
  }

  static void refill(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer)
  {
    auto *self = static_cast<Impl *>(user);
    self->buffer.pull(static_cast<float *>(buffer->mAudioData), kBufferFrames);
    buffer->mAudioDataByteSize = kBufferFrames * kAudioChannels * sizeof(float);
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
  }
};

MacAudioSink::MacAudioSink() : m_impl(std::make_unique<Impl>())
{
}

MacAudioSink::~MacAudioSink() = default;

bool MacAudioSink::configure(const AudioConfig &config)
{
  if (config.sampleRate != kAudioSampleRate || config.channels != kAudioChannels) {
    return false;
  }
  m_impl->pcm = config.codec == AudioCodec::Pcm16;
  if (!m_impl->pcm && !m_impl->decoder.open()) {
    return false;
  }

  const auto format = pcmFormat();
  if (AudioQueueNewOutput(&format, Impl::refill, m_impl.get(), nullptr, kCFRunLoopCommonModes, 0, &m_impl->queue) !=
      noErr) {
    return false;
  }
  for (int i = 0; i < Impl::kBufferCount; ++i) {
    AudioQueueBufferRef buffer = nullptr;
    AudioQueueAllocateBuffer(m_impl->queue, Impl::kBufferFrames * kAudioChannels * sizeof(float), &buffer);
    Impl::refill(m_impl.get(), m_impl->queue, buffer);
  }
  return AudioQueueStart(m_impl->queue, nullptr) == noErr;
}

void MacAudioSink::play(const EncodedAudio &packet)
{
  if (m_impl->pcm) {
    const auto frames = UInt32(packet.data.size() / qsizetype(sizeof(int16_t) * kAudioChannels));
    const auto *pcm = reinterpret_cast<const int16_t *>(packet.data.constData());
    std::vector<float> samples(size_t(frames) * kAudioChannels);
    for (size_t i = 0; i < samples.size(); ++i) {
      samples[i] = float(pcm[i]) / 32768.0f;
    }
    m_impl->buffer.push(samples.data(), frames);
    return;
  }

  const auto samples = m_impl->decoder.decode(packet.data);
  if (!samples.empty()) {
    m_impl->buffer.push(samples.data(), UInt32(samples.size() / kAudioChannels));
  }
}

int64_t MacAudioSink::positionUs() const
{
  return -1;
}

} // namespace hopflow::stream
