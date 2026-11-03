/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

//! Wire format for screen sharing
/*!
Screen sharing runs on its own TLS connection, separate from the input
connection, so video never delays the mouse. Every message is a frame:

  type (1 byte) | payload length (4 bytes) | timestamp in us (8 bytes) | payload

All integers are big endian.
*/
namespace hopflow::stream {

inline constexpr uint8_t kProtocolVersion = 2;
inline constexpr uint16_t kDefaultPort = 24802;
inline constexpr uint32_t kHeaderSize = 13;
//! Largest payload accepted, enough for a keyframe at native resolution
inline constexpr uint32_t kMaxPayload = 16 * 1024 * 1024;

enum class FrameType : uint8_t
{
  Hello = 1,
  VideoConfig = 2,
  VideoFrame = 3,
  AudioConfig = 4,
  AudioPacket = 5,
  KeyframeRequest = 6,
  Stats = 7,
  Bye = 8,
  //! Viewer to sender: the monitor an extended display should match
  DisplayInfo = 9
};

struct Frame
{
  FrameType type = FrameType::Bye;
  int64_t timestampUs = 0;
  QByteArray payload;
};

QByteArray encodeFrame(const Frame &frame);

//! Splits a byte stream into frames
class FrameParser
{
public:
  void feed(const QByteArray &data);

  //! The next complete frame, if one has arrived
  std::optional<Frame> next();

  //! The stream was malformed; the connection must be dropped
  bool hasError() const
  {
    return !m_error.isEmpty();
  }

  QString error() const
  {
    return m_error;
  }

private:
  QByteArray m_buffer;
  QString m_error;
};

enum class VideoCodec : uint8_t
{
  H264 = 0,
  //! Software fallback and test pattern: each frame is a JPEG image
  Jpeg = 1
};

enum class AudioCodec : uint8_t
{
  Aac = 0,
  //! Interleaved signed 16-bit samples
  Pcm16 = 1
};

//! What a connection is for, sent in Hello; version 1 peers only share screens
enum class SessionKind : uint8_t
{
  //! A screen shown in a window
  ScreenShare = 0,
  //! A virtual display on the sender, shown full screen on the viewer's monitor
  ExtendDisplay = 1,
  //! The sender's sound, played on the viewer's speakers or headphones
  Sound = 2,
  //! The sender's microphone, fed into a virtual microphone on the viewer
  Microphone = 3
};

struct Hello
{
  uint8_t version = kProtocolVersion;
  QString computerName;
  SessionKind kind = SessionKind::ScreenShare;
};

struct VideoConfig
{
  VideoCodec codec = VideoCodec::H264;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t fps = 0;
  //! Codec setup data, for H.264 the avcC record
  QByteArray codecData;
};

struct VideoFrame
{
  bool keyframe = false;
  QByteArray data;
};

struct AudioConfig
{
  AudioCodec codec = AudioCodec::Aac;
  uint32_t sampleRate = 48000;
  uint8_t channels = 2;
  //! Codec setup data, for AAC the AudioSpecificConfig
  QByteArray codecData;
};

struct DisplayInfo
{
  //! Size in physical pixels
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t refreshHz = 0;
  //! Device pixels per logical pixel, times 100
  uint16_t scalePercent = 100;
};

struct Stats
{
  uint16_t receivedFps = 0;
  uint32_t queuedFrames = 0;
};

QByteArray encode(const Hello &hello);
QByteArray encode(const VideoConfig &config);
QByteArray encode(const VideoFrame &frame);
QByteArray encode(const AudioConfig &config);
QByteArray encode(const DisplayInfo &info);
QByteArray encode(const Stats &stats);

std::optional<Hello> decodeHello(const QByteArray &payload);
std::optional<VideoConfig> decodeVideoConfig(const QByteArray &payload);
std::optional<VideoFrame> decodeVideoFrame(const QByteArray &payload);
std::optional<AudioConfig> decodeAudioConfig(const QByteArray &payload);
std::optional<DisplayInfo> decodeDisplayInfo(const QByteArray &payload);
std::optional<Stats> decodeStats(const QByteArray &payload);

} // namespace hopflow::stream
