/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferLocations.h"

#include "common/Constants.h"
#include "deskflow/FileTransfer.h"

#include <QDir>
#include <QStandardPaths>

#include <optional>
#include <string>

#if defined(_WIN32)
#include <windows.h>

#include <KnownFolders.h>
#include <ShlObj.h>
#include <WtsApi32.h>
#endif

namespace fs = std::filesystem;

namespace deskflow::filetransfer {

namespace {

const auto kClipboardFolder = ".clipboard";

#if defined(_WIN32)
//! Downloads of the user at the console; only succeeds when running as SYSTEM
std::optional<fs::path> consoleUserDownloads()
{
  HANDLE token = nullptr;
  if (!WTSQueryUserToken(WTSGetActiveConsoleSessionId(), &token)) {
    return std::nullopt;
  }

  PWSTR folder = nullptr;
  const auto result = SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT, token, &folder);
  CloseHandle(token);
  if (FAILED(result)) {
    CoTaskMemFree(folder);
    return std::nullopt;
  }

  fs::path path(folder);
  CoTaskMemFree(folder);
  return path;
}
#endif

fs::path downloadsFolder()
{
#if defined(_WIN32)
  if (auto path = consoleUserDownloads()) {
    return *path;
  }
#endif

  auto location = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  if (location.isEmpty()) {
    location = QDir::homePath();
  }
  return pathFromUtf8(location.toStdString());
}

} // namespace

fs::path receivedFilesFolder()
{
  if (const auto folder = qEnvironmentVariable("HOPFLOW_DOWNLOAD_DIR"); !folder.isEmpty()) {
    return pathFromUtf8(folder.toStdString());
  }
  return downloadsFolder() / kAppName;
}

fs::path transferFolder(FileTransferPurpose purpose, uint32_t id)
{
  if (purpose == FileTransferPurpose::Clipboard) {
    return receivedFilesFolder() / kClipboardFolder / std::to_string(id);
  }
  return receivedFilesFolder();
}

void removeOldClipboardTransfers(uint32_t keepId)
{
  const auto folder = receivedFilesFolder() / kClipboardFolder;
  std::error_code ec;
  for (const auto &entry : fs::directory_iterator(folder, ec)) {
    if (entry.path().filename() != std::to_string(keepId)) {
      fs::remove_all(entry.path(), ec);
    }
  }
}

} // namespace deskflow::filetransfer
