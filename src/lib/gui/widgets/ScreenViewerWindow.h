/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QImage>
#include <QWidget>

//! Shows another computer's shared screen
/*!
This is the window to pick in Discord's "share a window" list. The picture
keeps its aspect ratio with black bars; double-click toggles full screen.
*/
class ScreenViewerWindow : public QWidget
{
  Q_OBJECT

public:
  explicit ScreenViewerWindow(const QString &computerName, QWidget *parent = nullptr);

  void showFrame(const QImage &image);

  //! Show why the picture stopped, over the last frame
  void showMessage(const QString &message);

Q_SIGNALS:
  void closed();

protected:
  void paintEvent(QPaintEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void keyPressEvent(QKeyEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  QImage m_frame;
  QString m_message;
};
