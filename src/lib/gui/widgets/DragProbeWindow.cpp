/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DragProbeWindow.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {

// big enough to be under the cursor at the very edge of the screen
const int kProbeSize = 24;

// a probe nobody stops (the core went away) hides itself
const int kProbeTimeoutMs = 10000;

QStringList localFiles(const QMimeData *data)
{
  QStringList paths;
  for (const auto &url : data->urls()) {
    if (url.isLocalFile()) {
      paths.append(url.toLocalFile());
    }
  }
  return paths;
}

} // namespace

DragProbeWindow::DragProbeWindow(QWidget *parent)
    : QWidget(
          parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus |
                      Qt::NoDropShadowWindowHint
      )
{
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAcceptDrops(true);
  // fully transparent windows are not hit tested, so keep a trace of opacity
  setWindowOpacity(0.01);
  resize(kProbeSize, kProbeSize);

  m_timeout.setSingleShot(true);
  m_timeout.setInterval(kProbeTimeoutMs);
  connect(&m_timeout, &QTimer::timeout, this, &DragProbeWindow::stop);
}

void DragProbeWindow::probeAt(int x, int y)
{
  show();
  m_timeout.start();

#if defined(Q_OS_WIN)
  // the core reports physical pixels, so place the window natively instead of
  // through Qt's scaled coordinates
  const auto window = reinterpret_cast<HWND>(winId());
  SetWindowPos(
      window, HWND_TOPMOST, x - kProbeSize / 2, y - kProbeSize / 2, kProbeSize, kProbeSize,
      SWP_NOACTIVATE | SWP_SHOWWINDOW
  );

  // the drag only notices the window when the mouse moves over it
  SetCursorPos(x + 1, y + 1);
  SetCursorPos(x, y);
#else
  move(x - kProbeSize / 2, y - kProbeSize / 2);
#endif
}

void DragProbeWindow::stop()
{
  m_timeout.stop();
  hide();
}

void DragProbeWindow::dragEnterEvent(QDragEnterEvent *event)
{
  const auto paths = localFiles(event->mimeData());
  if (paths.isEmpty()) {
    event->ignore();
    return;
  }

  Q_EMIT filesDragged(paths);
  event->setDropAction(Qt::CopyAction);
  event->accept();
}

void DragProbeWindow::dragMoveEvent(QDragMoveEvent *event)
{
  event->setDropAction(Qt::CopyAction);
  event->accept();
}

void DragProbeWindow::dropEvent(QDropEvent *event)
{
  // the files are on their way to the other computer; dropping here does nothing
  event->setDropAction(Qt::IgnoreAction);
  event->ignore();
  stop();
}
