/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/MutedSpeakers.h"

#include "common/Constants.h"

#include <QStandardPaths>

namespace hopflow::stream {

namespace {

const auto kDevicesKey = QStringLiteral("mutedSpeakers");

} // namespace

MutedSpeakers::MutedSpeakers(const QString &fileName) : m_settings(fileName, QSettings::IniFormat)
{
}

bool MutedSpeakers::add(const QString &deviceId)
{
  if (auto devices = this->devices(); !devices.contains(deviceId)) {
    devices.append(deviceId);
    m_settings.setValue(kDevicesKey, devices);
  }
  m_settings.sync();
  return m_settings.status() == QSettings::NoError;
}

void MutedSpeakers::remove(const QString &deviceId)
{
  auto devices = this->devices();
  if (devices.removeAll(deviceId) == 0) {
    return;
  }
  if (devices.isEmpty()) {
    m_settings.remove(kDevicesKey);
  } else {
    m_settings.setValue(kDevicesKey, devices);
  }
  // should this fail, the next start unmutes the device again, leaving it as it is now
  m_settings.sync();
}

QStringList MutedSpeakers::devices() const
{
  return m_settings.value(kDevicesKey).toStringList();
}

QString MutedSpeakers::defaultFileName()
{
  return QStringLiteral("%1/%2-muted-speakers.ini")
      .arg(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation), kAppName);
}

} // namespace hopflow::stream
