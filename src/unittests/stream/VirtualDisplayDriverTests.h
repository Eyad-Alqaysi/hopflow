/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class VirtualDisplayDriverTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void readsShippedSettings();
  void offeredModeLeavesSettingsAlone();
  void addsMissingMode();
  void addsModeToEmptyResolutions_data();
  void addsModeToEmptyResolutions();
  void writesSettingsWhenMissing();
  void rejectsBrokenSettings();
  void recognisesHardwareIds();
  void findsDriverSource();
  void ignoresDriverWithoutMonitor();
  void findsDriverSourceByPath();
  void keepsShownDriverSourceByPath();
  void ignoresPathsWithoutDriver();
  void placesBesideDesktop();
};
