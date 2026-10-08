/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/StreamPeers.h"

#include "common/Settings.h"
#include "net/FingerprintDatabase.h"

#include <QFile>
#include <QSslCertificate>
#include <QSslKey>

namespace hopflow::stream {

namespace {

Fingerprint sha256Fingerprint(const QByteArray &sha256)
{
  return Fingerprint{QCryptographicHash::Sha256, sha256};
}

} // namespace

bool isPeerTrusted(const QByteArray &sha256, const QStringList &databases)
{
  if (sha256.isEmpty()) {
    return false;
  }

  for (const auto &path : databases) {
    FingerprintDatabase db;
    db.read(path);
    if (db.isTrusted(sha256Fingerprint(sha256))) {
      return true;
    }
  }
  return false;
}

bool trustPeer(const QByteArray &sha256, const QString &database)
{
  FingerprintDatabase db;
  db.read(database);
  db.addTrusted(sha256Fingerprint(sha256));
  return db.write(database);
}

QStringList trustedDatabases()
{
  return {Settings::tlsTrustedServersDb(), Settings::tlsTrustedClientsDb(), Settings::tlsTrustedScreensDb()};
}

std::optional<QSslConfiguration> tlsConfiguration(const QString &pemPath)
{
  // the PEM file holds both the private key and the certificate
  const auto certificates = QSslCertificate::fromPath(pemPath, QSsl::Pem);
  QFile file(pemPath);
  if (certificates.isEmpty() || !file.open(QIODevice::ReadOnly)) {
    return std::nullopt;
  }

  const QSslKey key(&file, QSsl::Rsa, QSsl::Pem);
  if (key.isNull()) {
    return std::nullopt;
  }

  auto config = QSslConfiguration::defaultConfiguration();
  config.setLocalCertificate(certificates.first());
  config.setPrivateKey(key);
  config.setPeerVerifyMode(QSslSocket::QueryPeer);
  config.setProtocol(QSsl::TlsV1_2OrLater);
  return config;
}

} // namespace hopflow::stream
