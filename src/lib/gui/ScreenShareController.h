/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/StreamPreset.h"

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <memory>

class QAction;
class QWidget;
class ScreenViewerWindow;

namespace hopflow::stream {
class StreamConnection;
class StreamReceiver;
class StreamSender;
class StreamServer;
} // namespace hopflow::stream

//! Screen sharing in the GUI: showing this screen elsewhere, and viewing others here
class ScreenShareController : public QObject
{
  Q_OBJECT

public:
  //! \p targets lists computers to offer in the share dialog
  ScreenShareController(QWidget *window, std::function<QStringList()> targets, QObject *parent = nullptr);
  ~ScreenShareController() override;

  QAction *shareAction() const
  {
    return m_shareAction;
  }

  QAction *stopAction() const
  {
    return m_stopAction;
  }

  //! Start or stop accepting other screens, following the settings
  void applySettings();

Q_SIGNALS:
  //! Something the user should know, for a tray notification
  void notify(const QString &message);

private:
  void chooseAndShare();
  void share(const QString &target, const hopflow::stream::Preset &preset);
  void stopSharing();
  void updateActions();
  void onViewerConnected(hopflow::stream::StreamConnection *connection);
  bool askToTrust(const QByteArray &fingerprint, const QString &question);

  QWidget *m_window;
  std::function<QStringList()> m_targets;
  QAction *m_shareAction;
  QAction *m_stopAction;
  std::unique_ptr<hopflow::stream::StreamServer> m_server;
  std::unique_ptr<hopflow::stream::StreamSender> m_sender;
  std::unique_ptr<hopflow::stream::StreamReceiver> m_receiver;
  QPointer<ScreenViewerWindow> m_viewer;
};
