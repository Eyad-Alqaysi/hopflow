/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenViewerWindow.h"

#include "common/Constants.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QScreen>

ScreenViewerWindow::ScreenViewerWindow(const QString &computerName, QWidget *parent) : QWidget(parent, Qt::Window)
{
  // Discord lists windows by title, so make it easy to find
  setWindowTitle(tr("%1 – %2 screen").arg(kAppName, computerName));
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMinimumSize(320, 180);
  m_message = tr("Waiting for %1…").arg(computerName);

  if (const auto *screen = QGuiApplication::primaryScreen()) {
    resize(screen->availableSize() * 0.6);
  }
}

void ScreenViewerWindow::showFrame(const QImage &image)
{
  m_frame = image;
  m_message.clear();
  update();
}

void ScreenViewerWindow::showMessage(const QString &message)
{
  m_message = message;
  update();
}

void ScreenViewerWindow::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);

  if (!m_frame.isNull()) {
    const auto target = QRect(QPoint(), m_frame.size().scaled(size(), Qt::KeepAspectRatio));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, target.size() != m_frame.size());
    painter.drawImage(target.translated(rect().center() - target.center()), m_frame);
  }

  if (!m_message.isEmpty()) {
    painter.setPen(Qt::white);
    painter.drawText(rect(), Qt::AlignCenter, m_message);
  }
}

void ScreenViewerWindow::mouseDoubleClickEvent(QMouseEvent *)
{
  setWindowState(windowState() ^ Qt::WindowFullScreen);
}

void ScreenViewerWindow::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape && isFullScreen()) {
    showNormal();
    return;
  }
  QWidget::keyPressEvent(event);
}

void ScreenViewerWindow::closeEvent(QCloseEvent *event)
{
  Q_EMIT closed();
  event->accept();
}
