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
class IVirtualDisplay;
class StreamSender;
} // namespace hopflow::stream

//! Extending this desktop onto another computer's monitor
/*!
The other computer says what its monitor is like, this one adds a virtual
display matching it and streams that display, which the other computer
shows full screen.

The virtual display is part of this desktop, so mouse and keyboard sharing
would also leave through its edges: it is paused while the display is
extended.
*/
class ExtendDisplayController : public QObject
{
  Q_OBJECT

public:
  /*!
  \p targets lists computers to offer. \p pauseSharing stops mouse and
  keyboard sharing and returns whether it was running; \p resumeSharing
  starts it again.
  */
  ExtendDisplayController(
      QWidget *window, std::function<QStringList()> targets, std::function<bool()> pauseSharing,
      std::function<void()> resumeSharing, QObject *parent = nullptr
  );
  ~ExtendDisplayController() override;

  QAction *extendAction() const
  {
    return m_extendAction;
  }

  QAction *stopAction() const
  {
    return m_stopAction;
  }

  //! True while mouse and keyboard sharing is paused for an extended display
  bool hasPausedSharing() const
  {
    return m_pausedSharing;
  }

Q_SIGNALS:
  //! Something the user should know, for a tray notification
  void notify(const QString &message);

private:
  void chooseAndExtend();
  void extend(const QString &target);
  void tryNextHost();
  void stopExtending();
  void onDisplayInfo(const hopflow::stream::DisplayInfo &info);
  void createInBackground(const hopflow::stream::DisplayInfo &info);
  void onDisplayCreated(bool created);
  void onStopped(const QString &reason);
  void endSession();
  void showError(const QString &message);
  void updateActions();

  QWidget *m_window;
  std::function<QStringList()> m_targets;
  std::function<bool()> m_pauseSharing;
  std::function<void()> m_resumeSharing;
  QAction *m_extendAction;
  QAction *m_stopAction;
  // declared before the sender, so capture has stopped by the time the display goes; shared with
  // a worker creating it, which then destroys it if the session ended meanwhile
  std::shared_ptr<hopflow::stream::IVirtualDisplay> m_display;
  std::unique_ptr<hopflow::stream::StreamSender> m_sender;
  QStringList m_hostsToTry;
  bool m_streaming = false;
  //! A worker is creating a display, which only it may touch until it finishes
  bool m_creating = false;
  bool m_pausedSharing = false;
};
