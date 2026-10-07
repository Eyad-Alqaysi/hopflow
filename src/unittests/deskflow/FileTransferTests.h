/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class FileTransferTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void safePaths_data();
  void safePaths();
  void manifestRoundTrip();
  void manifestRejected_data();
  void manifestRejected();
  void splitAndJoinPaths();
  void samePathsIgnoresNormalization();
  void uniqueDestination();
  void senderReceiverRoundTrip();
  void receiverRejectsExtraData();
  void receiverAbortCleansUp();
  void managerTransfer();
  void managerRefusesOverLimit();
  void managerReceiverCancels();

private:
  Log m_log;
};
