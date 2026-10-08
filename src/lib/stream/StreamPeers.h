/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QSslConfiguration>
#include <QStringList>

#include <optional>

//! Who may share screens with this computer
/*!
Screen sharing reuses the computer's Hopflow certificate. A peer is trusted
if its certificate's SHA-256 fingerprint is in any of the given databases:
the ones the main connection already filled in (trusted servers and clients),
or the one screen sharing adds to when the user accepts a new peer.
*/
namespace hopflow::stream {

bool isPeerTrusted(const QByteArray &sha256, const QStringList &databases);

//! Add \p sha256 to \p database; returns false if it cannot be written
bool trustPeer(const QByteArray &sha256, const QString &database);

//! The databases checked by default, from the settings
QStringList trustedDatabases();

//! TLS settings using the certificate and key in \p pemPath
/*!
Peers are asked for their certificate but it is not checked against a
certificate authority: Hopflow certificates are self signed, so trust comes
from the fingerprint instead. Returns nothing if the file cannot be read.
*/
std::optional<QSslConfiguration> tlsConfiguration(const QString &pemPath);

} // namespace hopflow::stream
