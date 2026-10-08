/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/StreamPreset.h"
#include "stream/StreamProtocol.h"

#include <QImage>

#include <functional>
#include <memory>

//! What each platform implements for screen sharing
/*!
A source captures and encodes; a sink decodes and plays. Callbacks may come
from any thread; the stream classes move the data to their own thread.
*/
namespace hopflow::stream {

struct EncodedVideo
{
  int64_t timestampUs = 0;
  bool keyframe = false;
  QByteArray data;
};

struct EncodedAudio
{
  int64_t timestampUs = 0;
  QByteArray data;
};

class IVideoSource
{
public:
  using ConfigReady = std::function<void(const VideoConfig &)>;
  using FrameReady = std::function<void(const EncodedVideo &)>;
  using Failed = std::function<void(const QString &message)>;

  virtual ~IVideoSource() = default;

  //! Start capturing; \p onConfig comes before the first frame and again if the size changes
  virtual void start(const Preset &preset, ConfigReady onConfig, FrameReady onFrame, Failed onFailed) = 0;
  virtual void stop() = 0;
  virtual void setBitrate(uint32_t bitsPerSecond) = 0;
  virtual void requestKeyframe() = 0;
};

class IAudioSource
{
public:
  using ConfigReady = std::function<void(const AudioConfig &)>;
  using PacketReady = std::function<void(const EncodedAudio &)>;
  using Failed = std::function<void(const QString &message)>;

  virtual ~IAudioSource() = default;
  virtual void start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed) = 0;
  virtual void stop() = 0;
};

class IVideoDecoder
{
public:
  virtual ~IVideoDecoder() = default;
  //! Returns false if this decoder cannot handle \p config
  virtual bool configure(const VideoConfig &config) = 0;
  //! Decode one frame; returns a null image if more data is needed or it failed
  virtual QImage decode(const EncodedVideo &frame) = 0;
};

class IAudioSink
{
public:
  virtual ~IAudioSink() = default;
  virtual bool configure(const AudioConfig &config) = 0;
  virtual void play(const EncodedAudio &packet) = 0;
  //! Playback position in stream time, or -1 before playback starts
  virtual int64_t positionUs() const = 0;
};

//! @name Factories, implemented per platform
//@{
std::unique_ptr<IVideoSource> createScreenVideoSource();
std::unique_ptr<IAudioSource> createSystemAudioSource();
std::unique_ptr<IVideoDecoder> createVideoDecoder(VideoCodec codec);
std::unique_ptr<IAudioSink> createAudioSink(AudioCodec codec);
//@}

} // namespace hopflow::stream
