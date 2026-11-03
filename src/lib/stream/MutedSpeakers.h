/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QSettings>
#include <QStringList>

namespace hopflow::stream {

//! Output devices Hopflow muted while their sound played elsewhere, kept on disk
/*!
A device is added before Hopflow mutes it and removed once Hopflow has
unmuted it, so whatever is left was muted by a run that crashed. Each thread
uses its own instance; they all read and write the same file.
*/
class MutedSpeakers
{
public:
  explicit MutedSpeakers(const QString &fileName = defaultFileName());

  //! False when the record could not be saved, so the device must not be muted
  bool add(const QString &deviceId);
  void remove(const QString &deviceId);
  QStringList devices() const;

  //! Beside Hopflow's other state, which is not copied between computers
  static QString defaultFileName();

private:
  QSettings m_settings;
};

} // namespace hopflow::stream
