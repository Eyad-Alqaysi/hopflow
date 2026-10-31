/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/StreamPreset.h"
#include "stream/StreamProtocol.h"

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <functional>
#include <map>
#include <memory>
#include <vector>

class QAction;
class QWidget;
class ScreenViewerWindow;

namespace hopflow::stream {
class StreamConnection;
class StreamReceiver;
class StreamSender;
class StreamServer;
} // namespace hopflow::stream

//! Screen sharing in the GUI: showing this screen elsewhere, and receiving every kind of stream here
/*!
It owns the listening server, so it also receives extended displays, sound
and microphones; other controllers only send.
*/
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
  void tryNextHost();
  void stopSharing();
  void updateActions();
  void onViewerConnected(hopflow::stream::StreamConnection *connection);
  void onSessionStarted(hopflow::stream::StreamReceiver *receiver, const QString &name);
  void showViewer(hopflow::stream::StreamReceiver *receiver, const QString &name);
  ScreenViewerWindow *viewerFor(hopflow::stream::SessionKind kind) const;
  static QString soundName(hopflow::stream::SessionKind kind);

  QWidget *m_window;
  std::function<QStringList()> m_targets;
  QAction *m_shareAction;
  QAction *m_stopAction;
  std::unique_ptr<hopflow::stream::StreamServer> m_server;
  std::unique_ptr<hopflow::stream::StreamSender> m_sender;
  //! Connected, but the hello saying what they carry has not arrived
  std::vector<std::unique_ptr<hopflow::stream::StreamReceiver>> m_pending;
  std::map<hopflow::stream::SessionKind, std::unique_ptr<hopflow::stream::StreamReceiver>> m_receivers;
  QPointer<ScreenViewerWindow> m_viewer;
  QPointer<ScreenViewerWindow> m_extendViewer;
  QStringList m_hostsToTry;
  hopflow::stream::Preset m_preset;
  bool m_streaming = false;
};
