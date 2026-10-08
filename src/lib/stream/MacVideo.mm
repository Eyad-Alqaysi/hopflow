/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// built with ARC (see CMakeLists.txt)

#include "stream/MacVideo.h"

#include "stream/H264.h"

#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <VideoToolbox/VideoToolbox.h>

#include <atomic>
#include <mutex>

using namespace hopflow::stream;

namespace {

int64_t toMicroseconds(CMTime time)
{
  return CMTIME_IS_VALID(time) ? static_cast<int64_t>(CMTimeGetSeconds(time) * 1e6) : 0;
}

QString describe(NSError *error)
{
  return error ? QString::fromNSString(error.localizedDescription) : QStringLiteral("unknown error");
}

} // namespace

namespace hopflow::stream::detail {

//! Everything the capture queue and the encoder callback share
/*!
Owned jointly by the source and the capture delegate, so callbacks that are
still in flight after stop() find it alive but no longer running.
*/
struct EncoderState
{
  std::mutex mutex;
  bool running = false;
  VTCompressionSessionRef session = nullptr;
  CVPixelBufferRef lastFrame = nullptr;
  std::atomic<bool> forceKeyframe{true};
  int64_t firstTimestampUs = -1;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t fps = 0;
  QByteArray avcC;
  IVideoSource::ConfigReady onConfig;
  IVideoSource::FrameReady onFrame;
  IVideoSource::Failed onFailed;

  ~EncoderState()
  {
    shutdown();
  }

  //! Encode on the capture queue only
  void encode(CVPixelBufferRef frame, CMTime time)
  {
    if (session == nullptr) {
      return;
    }

    if (frame != lastFrame) {
      CVPixelBufferRetain(frame);
      CVPixelBufferRelease(lastFrame);
      lastFrame = frame;
    }

    NSDictionary *options = nil;
    if (forceKeyframe.exchange(false)) {
      options = @{(__bridge NSString *)kVTEncodeFrameOptionKey_ForceKeyFrame : @YES};
    }
    VTCompressionSessionEncodeFrame(
        session, frame, time, kCMTimeInvalid, (__bridge CFDictionaryRef)options, nullptr, nullptr
    );
  }

  //! A still screen sends no new frames, so a keyframe is made from the last one
  void encodeLastAsKeyframe()
  {
    if (lastFrame != nullptr) {
      forceKeyframe = true;
      encode(lastFrame, CMClockGetTime(CMClockGetHostTimeClock()));
    }
  }

  void encoded(OSStatus status, CMSampleBufferRef sample)
  {
    if (status != noErr || sample == nullptr || !CMSampleBufferDataIsReady(sample)) {
      return;
    }

    bool keyframe = true;
    if (CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
        attachments != nullptr && CFArrayGetCount(attachments) > 0) {
      auto *first = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
      const void *notSync = CFDictionaryGetValue(first, kCMSampleAttachmentKey_NotSync);
      keyframe = notSync == nullptr || !CFBooleanGetValue(static_cast<CFBooleanRef>(notSync));
    }

    std::optional<VideoConfig> config;
    if (keyframe) {
      h264::ParameterSets sets;
      auto format = CMSampleBufferGetFormatDescription(sample);
      size_t count = 0;
      CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, nullptr);
      for (size_t i = 0; i < count; ++i) {
        const uint8_t *bytes = nullptr;
        size_t size = 0;
        if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &bytes, &size, nullptr, nullptr) == noErr) {
          const QByteArray set(reinterpret_cast<const char *>(bytes), static_cast<qsizetype>(size));
          (h264::nalType(set) == h264::kNalSps ? sets.sps : sets.pps).append(set);
        }
      }
      const auto avcCNow = h264::makeAvcC(sets);
      if (!avcCNow.isEmpty() && avcCNow != avcC) {
        avcC = avcCNow;
        config = VideoConfig{VideoCodec::H264, width, height, fps, avcC};
      }
    }

    CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
    QByteArray data(static_cast<qsizetype>(CMBlockBufferGetDataLength(block)), Qt::Uninitialized);
    if (CMBlockBufferCopyDataBytes(block, 0, data.size(), data.data()) != kCMBlockBufferNoErr) {
      return;
    }

    const auto time = toMicroseconds(CMSampleBufferGetPresentationTimeStamp(sample));
    std::scoped_lock lock(mutex);
    if (!running) {
      return;
    }
    if (firstTimestampUs < 0) {
      firstTimestampUs = time;
    }
    if (config) {
      onConfig(*config);
    }
    onFrame(EncodedVideo{time - firstTimestampUs, keyframe, data});
  }

  void fail(const QString &message)
  {
    std::scoped_lock lock(mutex);
    if (running && onFailed) {
      running = false;
      onFailed(message);
    }
  }

  void shutdown()
  {
    if (session != nullptr) {
      VTCompressionSessionInvalidate(session);
      CFRelease(session);
      session = nullptr;
    }
    CVPixelBufferRelease(lastFrame);
    lastFrame = nullptr;
  }
};

} // namespace hopflow::stream::detail

using hopflow::stream::detail::EncoderState;

namespace {

void compressedFrame(void *state, void *, OSStatus status, VTEncodeInfoFlags, CMSampleBufferRef sample)
{
  static_cast<EncoderState *>(state)->encoded(status, sample);
}

void setBitrateOn(VTCompressionSessionRef session, uint32_t bitsPerSecond)
{
  VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, (__bridge CFNumberRef) @(bitsPerSecond));
  // a hard ceiling too, so bursts of motion cannot flood the network
  NSArray *limits = @[ @(bitsPerSecond / 8 * 3 / 2), @1 ];
  VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, (__bridge CFArrayRef)limits);
}

} // namespace

API_AVAILABLE(macos(12.3))
@interface HopflowCaptureOutput : NSObject <SCStreamOutput, SCStreamDelegate> {
@public
  std::shared_ptr<EncoderState> state;
}
@end

@implementation HopflowCaptureOutput

- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type
{
  if (type != SCStreamOutputTypeScreen) {
    return;
  }

  // only complete frames carry a picture; idle ones mean nothing changed
  CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
  if (attachments == nullptr || CFArrayGetCount(attachments) == 0) {
    return;
  }
  NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
  if ([info[SCStreamFrameInfoStatus] integerValue] != SCFrameStatusComplete) {
    if (state->forceKeyframe) {
      state->encodeLastAsKeyframe();
    }
    return;
  }

  if (CVPixelBufferRef frame = CMSampleBufferGetImageBuffer(sample)) {
    state->encode(frame, CMSampleBufferGetPresentationTimeStamp(sample));
  }
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
  state->fail(QStringLiteral("screen capture stopped: %1").arg(describe(error)));
}

@end

namespace hopflow::stream {

struct MacScreenSource::Impl
{
  std::shared_ptr<EncoderState> state = std::make_shared<EncoderState>();
  dispatch_queue_t queue = dispatch_queue_create("io.github.eyad_alqaysi.hopflow.capture", DISPATCH_QUEUE_SERIAL);
  // only touched on the capture queue
  id stream = nil;
  id output = nil;
};

namespace {

bool isRunning(EncoderState &state)
{
  std::scoped_lock lock(state.mutex);
  return state.running;
}

VTCompressionSessionRef createEncoder(EncoderState &state, QSize size, int fps)
{
  // best first: low latency hardware, any hardware, then whatever the system has
  NSMutableArray<NSDictionary *> *specs = [NSMutableArray array];
  if (@available(macOS 11.3, *)) {
    [specs addObject:@{
      (__bridge NSString *)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder : @YES,
      (__bridge NSString *)kVTVideoEncoderSpecification_EnableLowLatencyRateControl : @YES
    }];
  }
  [specs addObject:@{(__bridge NSString *)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder : @YES}];
  [specs addObject:@{}];

  VTCompressionSessionRef session = nullptr;
  for (NSDictionary *spec in specs) {
    if (VTCompressionSessionCreate(
            kCFAllocatorDefault, size.width(), size.height(), kCMVideoCodecType_H264, (__bridge CFDictionaryRef)spec,
            nullptr, kCFAllocatorDefault, compressedFrame, &state, &session
        ) == noErr) {
      break;
    }
    session = nullptr;
  }
  if (session == nullptr) {
    return nullptr;
  }

  // real time, no reordering (no B frames) and a keyframe at least every 5 s
  VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
  VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
  VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
  VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFNumberRef) @(fps));
  VTSessionSetProperty(session, kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, (__bridge CFNumberRef) @5);
  // screens are Rec. 709; say so in the stream so every decoder converts colours back the same way
  VTSessionSetProperty(session, kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2);
  VTSessionSetProperty(session, kVTCompressionPropertyKey_TransferFunction, kCVImageBufferTransferFunction_ITU_R_709_2);
  VTSessionSetProperty(session, kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2);
  setBitrateOn(session, startBitrate(size, fps));
  VTCompressionSessionPrepareToEncodeFrames(session);
  return session;
}

//! Pixel size and refresh rate of \p display
API_AVAILABLE(macos(12.3))
std::pair<QSize, int> displayMode(SCDisplay *display)
{
  QSize pixels(int(display.width), int(display.height));
  int refresh = 0;
  if (CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display.displayID)) {
    pixels = QSize(int(CGDisplayModeGetPixelWidth(mode)), int(CGDisplayModeGetPixelHeight(mode)));
    refresh = int(CGDisplayModeGetRefreshRate(mode) + 0.5);
    CGDisplayModeRelease(mode);
  }
  // built-in displays report 0 here; ProMotion ones report their peak through NSScreen
  for (NSScreen *screen in NSScreen.screens) {
    NSNumber *number = screen.deviceDescription[@"NSScreenNumber"];
    if (number.unsignedIntValue == display.displayID) {
      refresh = std::max(refresh, int(screen.maximumFramesPerSecond));
    }
  }
  return {pixels, refresh > 0 ? refresh : 60};
}

//! Runs on the capture queue
API_AVAILABLE(macos(12.3))
void startCapture(const std::shared_ptr<MacScreenSource::Impl> &impl, SCDisplay *display, const Preset &preset)
{
  auto state = impl->state;
  if (!isRunning(*state)) {
    return; // stopped while looking for the display
  }

  const auto [pixels, refresh] = displayMode(display);
  const auto size = outputSize(preset.resolution, pixels);
  const auto fps = outputFps(preset.frameRate, refresh);
  state->width = uint16_t(size.width());
  state->height = uint16_t(size.height());
  state->fps = uint16_t(fps);

  state->session = createEncoder(*state, size, fps);
  if (state->session == nullptr) {
    state->fail(QStringLiteral("cannot start the video encoder"));
    return;
  }

  SCStreamConfiguration *config = [[SCStreamConfiguration alloc] init];
  config.width = size.width();
  config.height = size.height();
  config.minimumFrameInterval = CMTimeMake(1, fps);
  config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
  config.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_709_2;
  config.queueDepth = 5;
  config.showsCursor = YES;

  SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
  HopflowCaptureOutput *output = [[HopflowCaptureOutput alloc] init];
  output->state = state;
  SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:output];

  NSError *error = nil;
  if (![stream addStreamOutput:output type:SCStreamOutputTypeScreen sampleHandlerQueue:impl->queue error:&error]) {
    state->fail(describe(error));
    return;
  }
  impl->stream = stream;
  impl->output = output;
  [stream startCaptureWithCompletionHandler:^(NSError *startError) {
    if (startError != nil) {
      state->fail(QStringLiteral("cannot capture the screen: %1").arg(describe(startError)));
    }
  }];
}

} // namespace

MacScreenSource::MacScreenSource() : m_impl(std::make_shared<Impl>())
{
}

MacScreenSource::~MacScreenSource()
{
  stop();
}

bool MacScreenSource::isAvailable()
{
  if (@available(macOS 12.3, *)) {
    return true;
  }
  return false;
}

void MacScreenSource::start(const Preset &preset, ConfigReady onConfig, FrameReady onFrame, Failed onFailed)
{
  if (@available(macOS 12.3, *)) {
    auto impl = m_impl;
    {
      std::scoped_lock lock(impl->state->mutex);
      impl->state->onConfig = std::move(onConfig);
      impl->state->onFrame = std::move(onFrame);
      impl->state->onFailed = std::move(onFailed);
      impl->state->running = true;
    }

    // the first call asks the user for Screen Recording permission
    [SCShareableContent
        getShareableContentExcludingDesktopWindows:NO
                               onScreenWindowsOnly:YES
                                 completionHandler:^(SCShareableContent *content, NSError *error) {
                                   if (error != nil || content.displays.count == 0) {
                                     impl->state->fail(QStringLiteral(
                                         "allow Hopflow under System Settings > Privacy & Security > Screen "
                                         "Recording, then share again"
                                     ));
                                     return;
                                   }

                                   SCDisplay *display = content.displays.firstObject;
                                   for (SCDisplay *candidate in content.displays) {
                                     if (candidate.displayID == CGMainDisplayID()) {
                                       display = candidate;
                                     }
                                   }
                                   dispatch_async(impl->queue, ^{
                                     startCapture(impl, display, preset);
                                   });
                                 }];
  } else {
    onFailed(QStringLiteral("sharing this screen needs macOS 12.3 or later"));
  }
}

void MacScreenSource::stop()
{
  auto impl = m_impl;
  {
    std::scoped_lock lock(impl->state->mutex);
    impl->state->running = false;
  }

  // capture and encoding only happen on the capture queue, so stop them there
  dispatch_async(impl->queue, ^{
    if (@available(macOS 12.3, *)) {
      if (SCStream *stream = impl->stream) {
        [stream stopCaptureWithCompletionHandler:^(NSError *) {
          (void)impl; // keep everything alive until capture has stopped
        }];
      }
    }
    impl->stream = nil;
    impl->output = nil;
    impl->state->shutdown();
  });
}

void MacScreenSource::setBitrate(uint32_t bitsPerSecond)
{
  auto impl = m_impl;
  dispatch_async(impl->queue, ^{
    if (impl->state->session != nullptr) {
      setBitrateOn(impl->state->session, bitsPerSecond);
    }
  });
}

void MacScreenSource::requestKeyframe()
{
  auto impl = m_impl;
  impl->state->forceKeyframe = true;
  dispatch_async(impl->queue, ^{
    impl->state->encodeLastAsKeyframe();
  });
}

//
// MacH264Encoder
//

MacH264Encoder::MacH264Encoder() : m_state(std::make_shared<EncoderState>())
{
}

MacH264Encoder::~MacH264Encoder() = default;

bool MacH264Encoder::open(QSize size, int fps, IVideoSource::ConfigReady onConfig, IVideoSource::FrameReady onFrame)
{
  m_state->width = uint16_t(size.width());
  m_state->height = uint16_t(size.height());
  m_state->fps = uint16_t(fps);
  m_state->onConfig = std::move(onConfig);
  m_state->onFrame = std::move(onFrame);
  m_state->running = true;
  m_state->session = createEncoder(*m_state, size, fps);
  return m_state->session != nullptr;
}

void MacH264Encoder::encode(const QImage &image, int64_t timestampUs)
{
  const auto bgra = image.convertToFormat(QImage::Format_RGB32);
  CVPixelBufferRef buffer = nullptr;
  if (CVPixelBufferCreate(
          kCFAllocatorDefault, size_t(bgra.width()), size_t(bgra.height()), kCVPixelFormatType_32BGRA, nullptr, &buffer
      ) != kCVReturnSuccess) {
    return;
  }

  CVPixelBufferLockBaseAddress(buffer, 0);
  auto *base = static_cast<uchar *>(CVPixelBufferGetBaseAddress(buffer));
  const auto stride = CVPixelBufferGetBytesPerRow(buffer);
  for (int y = 0; y < bgra.height(); ++y) {
    memcpy(base + y * stride, bgra.constScanLine(y), size_t(bgra.width()) * 4);
  }
  CVPixelBufferUnlockBaseAddress(buffer, 0);

  m_state->encode(buffer, CMTimeMake(timestampUs, 1000000));
  CVPixelBufferRelease(buffer);
}

void MacH264Encoder::requestKeyframe()
{
  m_state->forceKeyframe = true;
}

void MacH264Encoder::setBitrate(uint32_t bitsPerSecond)
{
  if (m_state->session != nullptr) {
    setBitrateOn(m_state->session, bitsPerSecond);
  }
}

void MacH264Encoder::finish()
{
  if (m_state->session != nullptr) {
    VTCompressionSessionCompleteFrames(m_state->session, kCMTimeInvalid);
  }
}

//
// MacVideoDecoder
//

struct MacVideoDecoder::Impl
{
  CMVideoFormatDescriptionRef format = nullptr;
  VTDecompressionSessionRef session = nullptr;
  QImage image;

  ~Impl()
  {
    reset();
  }

  void reset()
  {
    if (session != nullptr) {
      VTDecompressionSessionInvalidate(session);
      CFRelease(session);
      session = nullptr;
    }
    if (format != nullptr) {
      CFRelease(format);
      format = nullptr;
    }
  }

  static void
  decompressed(void *refcon, void *, OSStatus status, VTDecodeInfoFlags, CVImageBufferRef buffer, CMTime, CMTime)
  {
    auto *self = static_cast<Impl *>(refcon);
    if (status != noErr || buffer == nullptr) {
      return;
    }

    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    const auto width = static_cast<int>(CVPixelBufferGetWidth(buffer));
    const auto height = static_cast<int>(CVPixelBufferGetHeight(buffer));
    const auto stride = CVPixelBufferGetBytesPerRow(buffer);
    const auto *base = static_cast<const uchar *>(CVPixelBufferGetBaseAddress(buffer));

    // BGRA in memory is what QImage calls RGB32 on little endian machines
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
      memcpy(image.scanLine(y), base + y * stride, static_cast<size_t>(width) * 4);
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    self->image = std::move(image);
  }

  bool createSession()
  {
    NSDictionary *attributes = @{(__bridge NSString *)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    VTDecompressionOutputCallbackRecord callback{decompressed, this};
    return VTDecompressionSessionCreate(
               kCFAllocatorDefault, format, nullptr, (__bridge CFDictionaryRef)attributes, &callback, &session
           ) == noErr;
  }
};

MacVideoDecoder::MacVideoDecoder() : m_impl(std::make_unique<Impl>())
{
}

MacVideoDecoder::~MacVideoDecoder() = default;

bool MacVideoDecoder::configure(const VideoConfig &config)
{
  m_impl->reset();
  if (config.codec != VideoCodec::H264) {
    return false;
  }

  const auto sets = h264::parseAvcC(config.codecData);
  if (!sets) {
    return false;
  }

  std::vector<const uint8_t *> pointers;
  std::vector<size_t> sizes;
  for (const auto *list : {&sets->sps, &sets->pps}) {
    for (const auto &set : *list) {
      pointers.push_back(reinterpret_cast<const uint8_t *>(set.constData()));
      sizes.push_back(static_cast<size_t>(set.size()));
    }
  }

  if (CMVideoFormatDescriptionCreateFromH264ParameterSets(
          kCFAllocatorDefault, pointers.size(), pointers.data(), sizes.data(), 4, &m_impl->format
      ) != noErr) {
    return false;
  }
  return m_impl->createSession();
}

QImage MacVideoDecoder::decode(const EncodedVideo &frame)
{
  if (m_impl->session == nullptr || frame.data.isEmpty()) {
    return {};
  }

  CMBlockBufferRef block = nullptr;
  if (CMBlockBufferCreateWithMemoryBlock(
          kCFAllocatorDefault, nullptr, static_cast<size_t>(frame.data.size()), kCFAllocatorDefault, nullptr, 0,
          static_cast<size_t>(frame.data.size()), 0, &block
      ) != kCMBlockBufferNoErr) {
    return {};
  }
  CMBlockBufferReplaceDataBytes(frame.data.constData(), block, 0, static_cast<size_t>(frame.data.size()));

  CMSampleBufferRef sample = nullptr;
  const size_t sampleSize = static_cast<size_t>(frame.data.size());
  const auto created =
      CMSampleBufferCreateReady(kCFAllocatorDefault, block, m_impl->format, 1, 0, nullptr, 1, &sampleSize, &sample);
  CFRelease(block);
  if (created != noErr) {
    return {};
  }

  m_impl->image = QImage();
  VTDecodeInfoFlags flags = 0;
  const auto status = VTDecompressionSessionDecodeFrame(m_impl->session, sample, 0, nullptr, &flags);
  CFRelease(sample);

  // the system may tear the session down, for example after sleep
  if (status == kVTInvalidSessionErr) {
    VTDecompressionSessionInvalidate(m_impl->session);
    CFRelease(m_impl->session);
    m_impl->session = nullptr;
    m_impl->createSession();
  }
  return m_impl->image;
}

} // namespace hopflow::stream
