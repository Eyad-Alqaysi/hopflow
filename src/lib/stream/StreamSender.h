/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"
#include "stream/RateController.h"
#include "stream/StreamConnection.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>

namespace hopflow::stream {

//! Shows this computer's screen in a window on another computer
class StreamSender : public QObject
{
  Q_OBJECT

public:
  StreamSender(QString computerName, QStringList trustedDatabases, QString approvedDatabase, QObject *parent = nullptr);
  ~StreamSender() override;

  //! Connect to \p host and stream; audio is optional
  void start(
      const QString &host, quint16 port, const QSslConfiguration &tls, const Preset &preset,
      std::unique_ptr<IVideoSource> video, std::unique_ptr<IAudioSource> audio
  );
  void stop();
  bool isActive() const;

  //! Answer an approvalNeeded() question about the viewer
  void resolveApproval(bool accept);

  //! Current bitrate, for display
  uint32_t bitrate() const;

Q_SIGNALS:
  void streaming(const QString &viewer);
  void stopped(const QString &reason);
  void approvalNeeded(const QByteArray &fingerprint, const QString &address);

private:
  void onConnected(StreamConnection *connection);
  void onVideoConfig(const VideoConfig &config);
  void onVideoFrame(const EncodedVideo &frame);
  void onAudioConfig(const AudioConfig &config);
  void onAudioPacket(const EncodedAudio &packet);
  void onFrame(const Frame &frame);
  void finish(const QString &reason);

  QString m_computerName;
  StreamClient m_client;
  QPointer<StreamConnection> m_connection;
  std::unique_ptr<IVideoSource> m_video;
  std::unique_ptr<IAudioSource> m_audio;
  std::optional<RateController> m_rate;
  Preset m_preset;
  QElapsedTimer m_clock;
  bool m_needKeyframe = true;
  bool m_active = false;
};

} // namespace hopflow::stream
