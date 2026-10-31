/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/StreamReceiver.h"

namespace hopflow::stream {

namespace {

// don't flood the sender with requests while waiting for a keyframe
const int kKeyframeRequestIntervalMs = 1000;

} // namespace

StreamReceiver::StreamReceiver(StreamConnection *connection, QObject *parent)
    : QObject(parent),
      m_connection(connection)
{
  connection->setParent(this);
  connect(connection, &StreamConnection::frameReceived, this, &StreamReceiver::onFrame);
  connect(connection, &StreamConnection::closed, this, [this](const QString &reason) {
    m_statsTimer.stop();
    if (!m_stopped) {
      m_stopped = true;
      Q_EMIT stopped(reason);
    }
  });

  m_statsTimer.setInterval(1000);
  connect(&m_statsTimer, &QTimer::timeout, this, &StreamReceiver::sendStats);
  m_statsTimer.start();
}

StreamReceiver::~StreamReceiver() = default;

void StreamReceiver::stop()
{
  if (m_connection) {
    m_connection->close();
  }
}

void StreamReceiver::onFrame(const Frame &frame)
{
  switch (frame.type) {
  case FrameType::Hello:
    if (const auto hello = decodeHello(frame.payload)) {
      m_computerName = hello->computerName;
      m_kind = hello->kind;
      Q_EMIT started(m_computerName);
    }
    break;

  case FrameType::VideoConfig:
    if (const auto config = decodeVideoConfig(frame.payload)) {
      onVideoConfig(*config);
    }
    break;

  case FrameType::VideoFrame:
    onVideoFrame(frame);
    break;

  case FrameType::AudioConfig:
    if (const auto config = decodeAudioConfig(frame.payload)) {
      onAudioConfig(*config);
    }
    break;

  case FrameType::AudioPacket:
    if (m_audio) {
      m_audio->play(EncodedAudio{frame.timestampUs, frame.payload});
    }
    break;

  default:
    break;
  }
}

void StreamReceiver::onVideoConfig(const VideoConfig &config)
{
  m_decoder = createVideoDecoder(config.codec);
  if (!m_decoder || !m_decoder->configure(config)) {
    m_decoder.reset();
    stop();
  }
}

void StreamReceiver::onVideoFrame(const Frame &frame)
{
  const auto decoded = decodeVideoFrame(frame.payload);
  if (!decoded || !m_decoder) {
    return;
  }

  const auto image = m_decoder->decode(EncodedVideo{frame.timestampUs, decoded->keyframe, decoded->data});
  if (image.isNull()) {
    requestKeyframe();
    return;
  }

  ++m_framesThisSecond;
  Q_EMIT frameReady(image);
}

void StreamReceiver::sendDisplayInfo(const DisplayInfo &info)
{
  if (m_connection) {
    m_connection->send(Frame{FrameType::DisplayInfo, 0, encode(info)});
  }
}

void StreamReceiver::onAudioConfig(const AudioConfig &config)
{
  switch (m_kind) {
  case SessionKind::Microphone:
    m_audio = createVirtualMicSink(config.codec);
    if (!m_audio) {
      Q_EMIT audioUnavailable(virtualMicUnavailableReason());
      return;
    }
    break;
  case SessionKind::Sound:
    m_audio = createAudioSink(config.codec, AudioLatency::Low);
    break;
  case SessionKind::ScreenShare:
  case SessionKind::ExtendDisplay:
    m_audio = createAudioSink(config.codec, AudioLatency::Smooth);
    break;
  }

  if (m_audio && !m_audio->configure(config)) {
    m_audio.reset();
    Q_EMIT audioUnavailable(QStringLiteral("this computer cannot play the sound it was sent"));
  }
}

void StreamReceiver::requestKeyframe()
{
  if (!m_connection) {
    return;
  }
  if (m_lastKeyframeRequest.isValid() && m_lastKeyframeRequest.elapsed() < kKeyframeRequestIntervalMs) {
    return;
  }
  m_lastKeyframeRequest.start();
  m_connection->send(Frame{FrameType::KeyframeRequest, 0, {}});
}

void StreamReceiver::sendStats()
{
  m_fps = m_framesThisSecond;
  m_framesThisSecond = 0;
  if (m_connection) {
    m_connection->send(Frame{FrameType::Stats, 0, encode(Stats{static_cast<uint16_t>(m_fps), 0})});
  }
}

} // namespace hopflow::stream
