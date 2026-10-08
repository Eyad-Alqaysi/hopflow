/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/StreamProtocol.h"

#include <QDataStream>
#include <QIODevice>

namespace hopflow::stream {

namespace {

bool isKnownType(uint8_t type)
{
  return type >= static_cast<uint8_t>(FrameType::Hello) && type <= static_cast<uint8_t>(FrameType::Bye);
}

//! Reads a payload completely, or fails if it is short or has bytes left over
template <typename Read> auto decodeWith(const QByteArray &payload, Read read)
{
  QDataStream in(payload);
  in.setByteOrder(QDataStream::BigEndian);
  auto value = read(in);
  if (in.status() != QDataStream::Ok || !in.atEnd()) {
    value.reset();
  }
  return value;
}

template <typename Write> QByteArray encodeWith(Write write)
{
  QByteArray payload;
  QDataStream out(&payload, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::BigEndian);
  write(out);
  return payload;
}

} // namespace

QByteArray encodeFrame(const Frame &frame)
{
  QByteArray bytes;
  bytes.reserve(kHeaderSize + frame.payload.size());
  QDataStream out(&bytes, QIODevice::WriteOnly);
  out.setByteOrder(QDataStream::BigEndian);
  out << static_cast<uint8_t>(frame.type) << static_cast<uint32_t>(frame.payload.size())
      << static_cast<qint64>(frame.timestampUs);
  out.writeRawData(frame.payload.constData(), static_cast<int>(frame.payload.size()));
  return bytes;
}

void FrameParser::feed(const QByteArray &data)
{
  if (!hasError()) {
    m_buffer.append(data);
  }
}

std::optional<Frame> FrameParser::next()
{
  if (hasError() || m_buffer.size() < kHeaderSize) {
    return std::nullopt;
  }

  QDataStream in(m_buffer);
  in.setByteOrder(QDataStream::BigEndian);
  uint8_t type = 0;
  uint32_t length = 0;
  qint64 timestamp = 0;
  in >> type >> length >> timestamp;

  if (!isKnownType(type)) {
    m_error = QStringLiteral("unknown frame type %1").arg(type);
    return std::nullopt;
  }
  if (length > kMaxPayload) {
    m_error = QStringLiteral("frame of %1 bytes is too large").arg(length);
    return std::nullopt;
  }
  if (m_buffer.size() < kHeaderSize + length) {
    return std::nullopt;
  }

  Frame frame{static_cast<FrameType>(type), timestamp, m_buffer.mid(kHeaderSize, length)};
  m_buffer.remove(0, kHeaderSize + length);
  return frame;
}

QByteArray encode(const Hello &hello)
{
  return encodeWith([&](QDataStream &out) { out << hello.version << hello.computerName.toUtf8(); });
}

QByteArray encode(const VideoConfig &config)
{
  return encodeWith([&](QDataStream &out) {
    out << static_cast<uint8_t>(config.codec) << config.width << config.height << config.fps << config.codecData;
  });
}

QByteArray encode(const VideoFrame &frame)
{
  return encodeWith([&](QDataStream &out) { out << static_cast<uint8_t>(frame.keyframe) << frame.data; });
}

QByteArray encode(const AudioConfig &config)
{
  return encodeWith([&](QDataStream &out) {
    out << static_cast<uint8_t>(config.codec) << config.sampleRate << config.channels << config.codecData;
  });
}

QByteArray encode(const Stats &stats)
{
  return encodeWith([&](QDataStream &out) { out << stats.receivedFps << stats.queuedFrames; });
}

std::optional<Hello> decodeHello(const QByteArray &payload)
{
  return decodeWith(payload, [](QDataStream &in) -> std::optional<Hello> {
    Hello hello;
    QByteArray name;
    in >> hello.version >> name;
    hello.computerName = QString::fromUtf8(name);
    return hello;
  });
}

std::optional<VideoConfig> decodeVideoConfig(const QByteArray &payload)
{
  auto config = decodeWith(payload, [](QDataStream &in) -> std::optional<VideoConfig> {
    VideoConfig config;
    uint8_t codec = 0;
    in >> codec >> config.width >> config.height >> config.fps >> config.codecData;
    if (codec > static_cast<uint8_t>(VideoCodec::Jpeg)) {
      return std::nullopt;
    }
    config.codec = static_cast<VideoCodec>(codec);
    return config;
  });
  if (config && (config->width == 0 || config->height == 0)) {
    config.reset();
  }
  return config;
}

std::optional<VideoFrame> decodeVideoFrame(const QByteArray &payload)
{
  return decodeWith(payload, [](QDataStream &in) -> std::optional<VideoFrame> {
    VideoFrame frame;
    uint8_t keyframe = 0;
    in >> keyframe >> frame.data;
    frame.keyframe = keyframe != 0;
    return frame;
  });
}

std::optional<AudioConfig> decodeAudioConfig(const QByteArray &payload)
{
  auto config = decodeWith(payload, [](QDataStream &in) -> std::optional<AudioConfig> {
    AudioConfig config;
    uint8_t codec = 0;
    in >> codec >> config.sampleRate >> config.channels >> config.codecData;
    if (codec > static_cast<uint8_t>(AudioCodec::Pcm16)) {
      return std::nullopt;
    }
    config.codec = static_cast<AudioCodec>(codec);
    return config;
  });
  if (config && (config->sampleRate == 0 || config->channels == 0 || config->channels > 8)) {
    config.reset();
  }
  return config;
}

std::optional<Stats> decodeStats(const QByteArray &payload)
{
  return decodeWith(payload, [](QDataStream &in) -> std::optional<Stats> {
    Stats stats;
    in >> stats.receivedFps >> stats.queuedFrames;
    return stats;
  });
}

} // namespace hopflow::stream
