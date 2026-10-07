/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferNotify.h"

#include "common/IpcEncoding.h"
#include "deskflow/ipc/CoreIpc.h"

namespace deskflow::filetransfer {

void notifyReceived(FileTransferPurpose purpose, const std::vector<std::string> &paths)
{
  // first item is how the files were sent, the rest are their paths
  QStringList items = {
      purpose == FileTransferPurpose::Clipboard ? QStringLiteral("clipboard") : QStringLiteral("drop")
  };
  for (const auto &path : paths) {
    items.append(QString::fromStdString(path));
  }
  ipcSendToClient(QStringLiteral("filesReceived"), encodeIpcList(items));
}

void notifyFailed(const std::string &message)
{
  ipcSendToClient(QStringLiteral("fileTransferFailed"), encodeIpcList({QString::fromStdString(message)}));
}

} // namespace deskflow::filetransfer
