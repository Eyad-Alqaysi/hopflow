/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QStringList>
#include <QTimer>
#include <QWidget>

//! An almost invisible window that finds out which files are being dragged
/*!
The core asks for a probe when files may be dragged across the screen edge.
The window appears under the cursor, accepts the drag to read its file paths,
and refuses the drop, so nothing happens if the user releases over it.
*/
class DragProbeWindow : public QWidget
{
  Q_OBJECT

public:
  explicit DragProbeWindow(QWidget *parent = nullptr);

  //! Show under the cursor at \p x, \p y in physical screen pixels
  void probeAt(int x, int y);
  void stop();

Q_SIGNALS:
  void filesDragged(const QStringList &paths);

protected:
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;

private:
  QTimer m_timeout;
};
