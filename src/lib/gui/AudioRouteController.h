/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/StreamProtocol.h"

#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

class QAction;
class QWidget;

namespace hopflow::stream {
class IAudioSource;
class StreamSender;
} // namespace hopflow::stream

//! Sends this computer's sound or microphone to play on another computer
/*!
There is one controller for each kind, SessionKind::Sound or
SessionKind::Microphone. The other computer plays it through its
ScreenShareController, which receives every kind of stream.
*/
class AudioRouteController : public QObject
{
  Q_OBJECT

public:
  //! \p targets lists computers to offer in the dialog
  AudioRouteController(
      hopflow::stream::SessionKind kind, QWidget *window, std::function<QStringList()> targets,
      QObject *parent = nullptr
  );
  ~AudioRouteController() override;

  QAction *sendAction() const
  {
    return m_sendAction;
  }

  QAction *stopAction() const
  {
    return m_stopAction;
  }

Q_SIGNALS:
  //! Something the user should know, for a tray notification
  void notify(const QString &message);

private:
  bool isSound() const;
  QString title() const;
  void chooseAndSend();
  void send(const QString &target);
  void tryNextHost();
  void stopSending();
  void updateActions();
  std::unique_ptr<hopflow::stream::IAudioSource> createSource() const;

  const hopflow::stream::SessionKind m_kind;
  QWidget *m_window;
  std::function<QStringList()> m_targets;
  QAction *m_sendAction;
  QAction *m_stopAction;
  std::unique_ptr<hopflow::stream::StreamSender> m_sender;
  QStringList m_hostsToTry;
  bool m_streaming = false;
  bool m_stopping = false;
};
