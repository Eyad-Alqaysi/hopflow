/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class AudioBufferTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void audioSpecificConfig();
  void waitsUntilStartDepth();
  void playsInOrderThenSilence();
  void dropsOldestWhenTooFull();
};
