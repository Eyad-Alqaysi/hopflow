/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

//! Captures the primary display with DXGI desktop duplication and encodes it with Media Foundation
/*!
Capture, scaling and colour conversion run on the GPU; the frame is then
encoded by the Media Foundation H.264 encoder, set for low latency.
Everything runs on one worker thread.
*/
class WinScreenSource : public IVideoSource
{
public:
  WinScreenSource();
  ~WinScreenSource() override;

  void start(const Preset &preset, ConfigReady onConfig, FrameReady onFrame, Failed onFailed) override;
  void stop() override;
  void setBitrate(uint32_t bitsPerSecond) override;
  void requestKeyframe() override;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

//! Decodes H.264 with the Media Foundation decoder into BGRA images
class WinVideoDecoder : public IVideoDecoder
{
public:
  WinVideoDecoder();
  ~WinVideoDecoder() override;

  bool configure(const VideoConfig &config) override;
  QImage decode(const EncodedVideo &frame) override;

  struct Impl;

private:
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
