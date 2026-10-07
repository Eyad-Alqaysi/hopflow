/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class HopflowFileTransferTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void transferOverTheWire();
  void receiverRefusesUnsafeManifest();

private:
  Log m_log;
};
