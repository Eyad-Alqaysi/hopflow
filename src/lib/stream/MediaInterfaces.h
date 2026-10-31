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

//! A display that exists only in software, for extending the desktop onto another computer
/*!
It stays connected until destroy() or destruction. Its picture is captured
with createScreenVideoSource(displayId()).
*/
class IVirtualDisplay
{
public:
  virtual ~IVirtualDisplay() = default;
  //! Add the display to the desktop at \p size physical pixels; false with a reason in error()
  virtual bool create(const DisplayInfo &info) = 0;
  //! Identifier the screen source understands; empty until created
  virtual QString displayId() const = 0;
  virtual void destroy() = 0;
  virtual QString error() const = 0;
};

//! How long received audio may wait before it is played
enum class AudioLatency : uint8_t
{
  //! Enough buffer to ride out Wi-Fi jitter, kept in sync with video
  Smooth,
  //! As little as the network allows, for sound routing and microphones
  Low
};

//! @name Factories, implemented per platform; each returns nullptr where unsupported
//@{
//! Capture \p displayId, or the main display when empty
std::unique_ptr<IVideoSource> createScreenVideoSource(const QString &displayId = {});
//! What this computer plays; with \p muteLocal its own speakers go quiet while captured
std::unique_ptr<IAudioSource> createSystemAudioSource(bool muteLocal = false);
std::unique_ptr<IAudioSource> createMicrophoneSource();
std::unique_ptr<IVirtualDisplay> createVirtualDisplay();
std::unique_ptr<IVideoDecoder> createVideoDecoder(VideoCodec codec);
std::unique_ptr<IAudioSink> createAudioSink(AudioCodec codec, AudioLatency latency = AudioLatency::Smooth);
//! Plays into a virtual microphone other apps can record from
std::unique_ptr<IAudioSink> createVirtualMicSink(AudioCodec codec);
//! Why createVirtualMicSink() returned nullptr, such as a driver that is not installed
QString virtualMicUnavailableReason();
//@}

} // namespace hopflow::stream
