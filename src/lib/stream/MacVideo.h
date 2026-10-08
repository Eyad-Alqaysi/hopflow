/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

namespace detail {
struct EncoderState;
}

//! H.264 encoder on VideoToolbox: hardware, real time, no B frames
/*!
The screen source feeds it captured frames; tests and software sources can
feed it images.
*/
class MacH264Encoder
{
public:
  MacH264Encoder();
  ~MacH264Encoder();

  bool open(QSize size, int fps, IVideoSource::ConfigReady onConfig, IVideoSource::FrameReady onFrame);
  void encode(const QImage &image, int64_t timestampUs);
  void requestKeyframe();
  void setBitrate(uint32_t bitsPerSecond);

  //! Wait until every frame passed to encode() has come out
  void finish();

private:
  std::shared_ptr<detail::EncoderState> m_state;
};

//! Captures the main display with ScreenCaptureKit and encodes it with VideoToolbox
class MacScreenSource : public IVideoSource
{
public:
  MacScreenSource();
  ~MacScreenSource() override;

  void start(const Preset &preset, ConfigReady onConfig, FrameReady onFrame, Failed onFailed) override;
  void stop() override;
  void setBitrate(uint32_t bitsPerSecond) override;
  void requestKeyframe() override;

  //! True if this macOS has ScreenCaptureKit (12.3 or later)
  static bool isAvailable();

  struct Impl;

private:
  std::shared_ptr<Impl> m_impl;
};

//! Decodes H.264 with VideoToolbox into BGRA images
class MacVideoDecoder : public IVideoDecoder
{
public:
  MacVideoDecoder();
  ~MacVideoDecoder() override;

  bool configure(const VideoConfig &config) override;
  QImage decode(const EncodedVideo &frame) override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
