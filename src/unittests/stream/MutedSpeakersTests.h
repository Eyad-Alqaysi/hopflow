/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QTemporaryDir>

class MutedSpeakersTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void startsEmpty();
  void survivesRestart();
  void addsOnce();
  void removesOnlyThatDevice();
  void reportsUnsavedRecord();

private:
  QString fileName(const QString &name) const;

  QTemporaryDir m_dir;
};
