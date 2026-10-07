/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QStringList>

//! Encode a list of strings as one IPC argument
/*!
IPC messages are newline framed and split on '=', so arbitrary text such as
file paths is sent as base64url (no padding) of a JSON array.
*/
inline QString encodeIpcList(const QStringList &items)
{
  const auto json = QJsonDocument(QJsonArray::fromStringList(items)).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(json.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

//! Decode an argument made by encodeIpcList(); returns an empty list if it is malformed
inline QStringList decodeIpcList(const QString &argument)
{
  const auto json = QByteArray::fromBase64(argument.toLatin1(), QByteArray::Base64UrlEncoding);
  QStringList items;
  for (const auto &value : QJsonDocument::fromJson(json).array()) {
    items.append(value.toString());
  }
  return items;
}
