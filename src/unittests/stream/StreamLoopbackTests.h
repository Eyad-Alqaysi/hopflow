/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QTemporaryDir>

class StreamLoopbackTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void pairedComputersStream();
  void unknownSenderNeedsApproval_data();
  void unknownSenderNeedsApproval();
  void unknownViewerNeedsApproval();
  void stoppingEndsBothSides();

private:
  QTemporaryDir m_dir;
  QString m_viewerPem;
  QString m_senderPem;
  QByteArray m_viewerFingerprint;
  QByteArray m_senderFingerprint;
};
