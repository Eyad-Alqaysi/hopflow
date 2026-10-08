/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/StreamSender.h"

namespace hopflow::stream {

StreamSender::StreamSender(
    QString computerName, QStringList trustedDatabases, QString approvedDatabase, QObject *parent
)
    : QObject(parent),
      m_computerName(std::move(computerName)),
      m_client(std::move(trustedDatabases), std::move(approvedDatabase))
{
  connect(&m_client, &StreamClient::connected, this, &StreamSender::onConnected);
  connect(&m_client, &StreamClient::approvalNeeded, this, &StreamSender::approvalNeeded);
  connect(&m_client, &StreamClient::failed, this, [this](const QString &message) { finish(message); });
}

StreamSender::~StreamSender()
{
  if (m_video) {
    m_video->stop();
  }
  if (m_audio) {
    m_audio->stop();
  }
}

void StreamSender::start(
    const QString &host, quint16 port, const QSslConfiguration &tls, const Preset &preset,
    std::unique_ptr<IVideoSource> video, std::unique_ptr<IAudioSource> audio
)
{
  stop();
  m_preset = preset;
  m_video = std::move(video);
  m_audio = preset.audio ? std::move(audio) : nullptr;
  m_needKeyframe = true;
  m_active = true;
  m_client.connectTo(host, port, tls);
}

void StreamSender::stop()
{
  finish(tr("stopped"));
}

bool StreamSender::isActive() const
{
  return m_active;
}

void StreamSender::resolveApproval(bool accept)
{
  m_client.gate()->resolve(accept);
}

uint32_t StreamSender::bitrate() const
{
  return m_rate ? m_rate->bitrate() : 0;
}

void StreamSender::onConnected(StreamConnection *connection)
{
  if (!m_active) {
    connection->close();
    connection->deleteLater();
    return;
  }

  m_connection = connection;
  connection->setParent(this);
  connect(connection, &StreamConnection::frameReceived, this, &StreamSender::onFrame);
  connect(connection, &StreamConnection::closed, this, [this](const QString &reason) { finish(reason); });
  connection->send(Frame{FrameType::Hello, 0, encode(Hello{kProtocolVersion, m_computerName})});
  m_clock.start();

  // sources call back from their own threads; hop to ours before touching the connection
  QPointer<StreamSender> self(this);
  m_video->start(
      m_preset,
      [self](const VideoConfig &config) {
        QMetaObject::invokeMethod(self, [self, config] {
          if (self) {
            self->onVideoConfig(config);
          }
        });
      },
      [self](const EncodedVideo &frame) {
        QMetaObject::invokeMethod(self, [self, frame] {
          if (self) {
            self->onVideoFrame(frame);
          }
        });
      },
      [self](const QString &message) {
        QMetaObject::invokeMethod(self, [self, message] {
          if (self) {
            self->finish(message);
          }
        });
      }
  );

  if (m_audio) {
    m_audio->start(
        [self](const AudioConfig &config) {
          QMetaObject::invokeMethod(self, [self, config] {
            if (self) {
              self->onAudioConfig(config);
            }
          });
        },
        [self](const EncodedAudio &packet) {
          QMetaObject::invokeMethod(self, [self, packet] {
            if (self) {
              self->onAudioPacket(packet);
            }
          });
        },
        [self](const QString &) {
          // sharing goes on without sound
          QMetaObject::invokeMethod(self, [self] {
            if (self && self->m_audio) {
              self->m_audio->stop();
            }
          });
        }
    );
  }

  Q_EMIT streaming(connection->peerAddress());
}

void StreamSender::onVideoConfig(const VideoConfig &config)
{
  if (!m_connection) {
    return;
  }

  // a new size or codec setup starts the rate control from the preset again
  m_rate.emplace(startBitrate(QSize(config.width, config.height), config.fps));
  m_video->setBitrate(m_rate->bitrate());
  m_needKeyframe = true;
  m_connection->send(Frame{FrameType::VideoConfig, 0, encode(config)});
}

void StreamSender::onVideoFrame(const EncodedVideo &frame)
{
  if (!m_connection || !m_rate) {
    return;
  }

  // after a drop, later frames refer to the missing one, so wait for a keyframe
  if (m_needKeyframe && !frame.keyframe) {
    return;
  }

  if (!m_rate->shouldSend(m_connection->backlog(), m_clock.elapsed())) {
    m_needKeyframe = true;
    m_video->requestKeyframe();
    if (m_rate->takeBitrateChange()) {
      m_video->setBitrate(m_rate->bitrate());
    }
    return;
  }

  if (m_rate->takeBitrateChange()) {
    m_video->setBitrate(m_rate->bitrate());
  }
  m_needKeyframe = false;
  m_connection->send(Frame{FrameType::VideoFrame, frame.timestampUs, encode(VideoFrame{frame.keyframe, frame.data})});
}

void StreamSender::onAudioConfig(const AudioConfig &config)
{
  if (m_connection) {
    m_connection->send(Frame{FrameType::AudioConfig, 0, encode(config)});
  }
}

void StreamSender::onAudioPacket(const EncodedAudio &packet)
{
  if (m_connection) {
    m_connection->send(Frame{FrameType::AudioPacket, packet.timestampUs, packet.data});
  }
}

void StreamSender::onFrame(const Frame &frame)
{
  if (frame.type == FrameType::KeyframeRequest && m_video) {
    m_needKeyframe = true;
    m_video->requestKeyframe();
  }
}

void StreamSender::finish(const QString &reason)
{
  if (!m_active) {
    return;
  }
  m_active = false;

  if (m_video) {
    m_video->stop();
  }
  if (m_audio) {
    m_audio->stop();
  }
  if (m_connection) {
    // tell the viewer, without hearing our own close back
    m_connection->disconnect(this);
    m_connection->close();
    m_connection->deleteLater();
    m_connection = nullptr;
  }
  m_rate.reset();
  Q_EMIT stopped(reason);
}

} // namespace hopflow::stream
