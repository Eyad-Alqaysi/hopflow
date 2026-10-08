/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace hopflow::stream {

//! A moving test picture sent as JPEG frames, for testing without capture
class TestPatternSource : public QObject, public IVideoSource
{
  Q_OBJECT

public:
  explicit TestPatternSource(QSize screen = QSize(1920, 1080), QObject *parent = nullptr);

  void start(const Preset &preset, ConfigReady onConfig, FrameReady onFrame, Failed onFailed) override;
  void stop() override;
  void setBitrate(uint32_t bitsPerSecond) override;
  void requestKeyframe() override;

  //! Draw the picture shown at \p elapsedMs, at \p size
  static QImage render(QSize size, int64_t elapsedMs);

private:
  void emitFrame();

  QSize m_screen;
  QSize m_size;
  int m_quality = 80;
  QTimer m_timer;
  QElapsedTimer m_clock;
  FrameReady m_onFrame;
};

//! Decodes JPEG frames, used for the test pattern and as a software fallback
class JpegDecoder : public IVideoDecoder
{
public:
  bool configure(const VideoConfig &config) override;
  QImage decode(const EncodedVideo &frame) override;
};

//! Plays nothing; used where a platform has no audio output yet
class SilentAudioSink : public IAudioSink
{
public:
  bool configure(const AudioConfig &) override
  {
    return true;
  }
  void play(const EncodedAudio &) override
  {
    // deliberately silent
  }
  int64_t positionUs() const override
  {
    return -1;
  }
};

} // namespace hopflow::stream
