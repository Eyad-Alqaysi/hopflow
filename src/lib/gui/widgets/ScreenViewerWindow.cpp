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
#include <QTimer>

namespace {

//! Longest gap between the two Esc presses that close an extended display
constexpr int kDoubleEscapeMs = 1500;

} // namespace

ScreenViewerWindow::ScreenViewerWindow(const QString &computerName, Mode mode, QWidget *parent)
    : QWidget(parent, Qt::Window),
      m_mode(mode)
{
  setAttribute(Qt::WA_OpaquePaintEvent);

  if (m_mode == Mode::ExtendedDisplay) {
    setWindowTitle(tr("%1 – %2 extended display").arg(kAppName, computerName));
    setWindowFlag(Qt::FramelessWindowHint);
    setCursor(Qt::BlankCursor);
    return;
  }

  // Discord lists windows by title, so make it easy to find
  setWindowTitle(tr("%1 – %2 screen").arg(kAppName, computerName));
  setMinimumSize(320, 180);
  m_message = tr("Waiting for %1…").arg(computerName);

  if (const auto *screen = QGuiApplication::primaryScreen()) {
    resize(screen->availableSize() * 0.6);
  }
}

void ScreenViewerWindow::showFrame(const QImage &image)
{
  if (m_mode == Mode::ExtendedDisplay && !m_message.isEmpty()) {
    setCursor(Qt::BlankCursor); // a new session after one stopped
  }
  m_frame = image;
  m_message.clear();
  update();
}

void ScreenViewerWindow::showMessage(const QString &message)
{
  if (m_mode == Mode::ExtendedDisplay) {
    unsetCursor(); // the other computer no longer draws one
  }
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

  if (isEscapeHintShown()) {
    const auto text = tr("Press Esc again to stop showing the extended display");
    const auto box = painter.fontMetrics().boundingRect(text).adjusted(-16, -8, 16, 8);
    const auto hint = box.translated(rect().center().x() - box.center().x(), 40 - box.top());
    painter.fillRect(hint, QColor(0, 0, 0, 200));
    painter.setPen(Qt::white);
    painter.drawText(hint, Qt::AlignCenter, text);
  }
}

void ScreenViewerWindow::mouseDoubleClickEvent(QMouseEvent *)
{
  if (m_mode == Mode::SharedScreen) {
    setWindowState(windowState() ^ Qt::WindowFullScreen);
  }
}

void ScreenViewerWindow::keyPressEvent(QKeyEvent *event)
{
  if (event->key() == Qt::Key_Escape && m_mode == Mode::SharedScreen && isFullScreen()) {
    showNormal();
    return;
  }
  if (event->key() != Qt::Key_Escape || m_mode == Mode::SharedScreen) {
    QWidget::keyPressEvent(event);
    return;
  }

  // once the picture has stopped there is nothing to lose
  if (!m_message.isEmpty() || isEscapeHintShown()) {
    close();
    return;
  }
  m_escape.start();
  update();
  QTimer::singleShot(kDoubleEscapeMs, this, qOverload<>(&QWidget::update));
}

bool ScreenViewerWindow::isEscapeHintShown() const
{
  return m_escape.isValid() && m_escape.elapsed() < kDoubleEscapeMs;
}

void ScreenViewerWindow::closeEvent(QCloseEvent *event)
{
  Q_EMIT closed();
  event->accept();
}
