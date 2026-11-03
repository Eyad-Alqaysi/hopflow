/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StreamSupport.h"

#include "TlsUtility.h"
#include "common/Settings.h"
#include "net/SecureUtils.h"
#include "stream/StreamPeers.h"
#include "stream/StreamSender.h"

#include <QAbstractButton>
#include <QGuiApplication>
#include <QMessageBox>

namespace hopflow::gui {

std::optional<QSslConfiguration> localTls()
{
  using namespace deskflow::gui;
  if (!TlsUtility::isCertValid() && !TlsUtility::generateCertificate()) {
    return std::nullopt;
  }
  return hopflow::stream::tlsConfiguration(Settings::value(Settings::Security::Certificate).toString());
}

quint16 streamPort()
{
  return static_cast<quint16>(Settings::value(Settings::Stream::Port).toUInt());
}

std::unique_ptr<hopflow::stream::StreamSender> makeSender()
{
  return std::make_unique<hopflow::stream::StreamSender>(
      Settings::value(Settings::Core::ComputerName).toString(), hopflow::stream::trustedDatabases(),
      Settings::tlsTrustedScreensDb()
  );
}

bool askToTrust(QWidget *window, const QByteArray &fingerprint, const QString &question)
{
  QMessageBox box(window);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(QObject::tr("Screen Sharing"));
  box.setText(question);
  box.setInformativeText(
      QObject::tr("Only allow it if this fingerprint matches the one shown in %1 on that computer:\n\n%2")
          .arg(QGuiApplication::applicationDisplayName(), deskflow::formatSSLFingerprintColumns(fingerprint))
  );
  box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  box.setDefaultButton(QMessageBox::No);
  box.button(QMessageBox::Yes)->setText(QObject::tr("Allow"));
  box.button(QMessageBox::No)->setText(QObject::tr("Don't Allow"));
  return box.exec() == QMessageBox::Yes;
}

QStringList splitHosts(const QString &target)
{
  QStringList hosts;
  for (const auto &host : target.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    if (const auto trimmed = host.trimmed(); !trimmed.isEmpty()) {
      hosts.append(trimmed);
    }
  }
  return hosts;
}

} // namespace hopflow::gui
