/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QSslConfiguration>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class QWidget;

namespace hopflow::stream {
class StreamSender;
}

//! What every stream feature in the GUI needs: screen sharing, extended displays, sound and microphone
namespace hopflow::gui {

//! TLS using this computer's certificate, creating one if needed
std::optional<QSslConfiguration> localTls();

quint16 streamPort();

//! A sender named after this computer, trusting the paired computers
std::unique_ptr<hopflow::stream::StreamSender> makeSender();

//! Ask whether to trust a computer with \p fingerprint; true if the user allows it
bool askToTrust(QWidget *window, const QByteArray &fingerprint, const QString &question);

//! The addresses in a computer's comma separated address list, in the order to try them
QStringList splitHosts(const QString &target);

} // namespace hopflow::gui
