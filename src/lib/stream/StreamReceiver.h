/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"
#include "stream/StreamConnection.h"

#include <QElapsedTimer>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace hopflow::stream {

//! Plays the screen another computer is sharing
class StreamReceiver : public QObject
{
  Q_OBJECT

public:
  //! Takes ownership of \p connection
  explicit StreamReceiver(StreamConnection *connection, QObject *parent = nullptr);
  ~StreamReceiver() override;

  void stop();

  QString computerName() const
  {
    return m_computerName;
  }

  //! Frames shown in the last second
  int fps() const
  {
    return m_fps;
  }

Q_SIGNALS:
  void started(const QString &computerName);
  void frameReady(const QImage &image);
  void stopped(const QString &reason);

private:
  void onFrame(const Frame &frame);
  void onVideoConfig(const VideoConfig &config);
  void onVideoFrame(const Frame &frame);
  void onAudioConfig(const AudioConfig &config);
  void requestKeyframe();
  void sendStats();

  QPointer<StreamConnection> m_connection;
  std::unique_ptr<IVideoDecoder> m_decoder;
  std::unique_ptr<IAudioSink> m_audio;
  QString m_computerName;
  QTimer m_statsTimer;
  QElapsedTimer m_lastKeyframeRequest;
  int m_framesThisSecond = 0;
  int m_fps = 0;
  bool m_stopped = false;
};

} // namespace hopflow::stream
