/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxyHopflow.h"

#include "base/Log.h"
#include "client/Client.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/Computer.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/FileTransfer.h"
#include "deskflow/FileTransferLocations.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/ipc/CoreIpc.h"

#include <QString>

#include <cstring>

using namespace deskflow::filetransfer;

namespace {

FileTransferPurpose toPurpose(uint8_t value)
{
  if (value > static_cast<uint8_t>(FileTransferPurpose::Clipboard)) {
    throw BadClientException("invalid file transfer purpose");
  }
  return static_cast<FileTransferPurpose>(value);
}

FileTransferStatus toStatus(uint8_t value)
{
  if (value > static_cast<uint8_t>(FileTransferStatus::Error)) {
    throw BadClientException("invalid file transfer status");
  }
  return static_cast<FileTransferStatus>(value);
}

} // namespace

ServerProxyHopflow::ServerProxyHopflow(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events),
      m_files(
          FileTransferManager::Callbacks{
              .destination = transferFolder,
              .received = [this](
                              uint32_t id, FileTransferPurpose purpose, const std::vector<std::string> &paths
                          ) { received(id, purpose, paths); },
              .failed = [](const std::string &message
                        ) { ipcSendToClient(QStringLiteral("fileTransferFailed"), QString::fromStdString(message)); }
          }
      )
{
}

ServerProxyHopflow::~ServerProxyHopflow() = default;

void ServerProxyHopflow::queryInfo()
{
  // the platform must reach the server before the info, which is when it sends options
  const auto platform = static_cast<uint8_t>(localPlatform());
  LOG_DEBUG("sending platform %d to server", platform);
  ProtocolUtil::writef(getStream(), kMsgHPlatform, platform);
  ServerProxy1_8::queryInfo();
}

bool ServerProxyHopflow::onGrabClipboard(ClipboardID id)
{
  const auto result = ServerProxy1_8::onGrabClipboard(id);
  if (id != kClipboardClipboard) {
    return result;
  }

  auto files = getClient()->getComputer()->getClipboardFiles();
  if (!files.empty() && files == m_clipboardReceived) {
    // our own paste of received files, not a new copy
    return result;
  }
  m_clipboardReceived.clear();

  // the server only needs to hear about files, or that files are gone
  if (!files.empty() || m_offeringClipboard) {
    offer(FileTransferPurpose::Clipboard, files);
    m_offeringClipboard = !files.empty();
  }
  return result;
}

void ServerProxyHopflow::offer(FileTransferPurpose purpose, const std::vector<std::string> &paths)
{
  LOG_DEBUG("offering %zu files to the server", paths.size());
  const auto joined = joinPaths(paths);
  ProtocolUtil::writef(getStream(), kMsgHFileOffer, static_cast<uint8_t>(purpose), &joined);
}

ServerProxy::ConnectionResult ServerProxyHopflow::parseMessage(const uint8_t *code)
{
  if (parseFileMessage(code)) {
    return ConnectionResult::Okay;
  }
  return ServerProxy1_8::parseMessage(code);
}

bool ServerProxyHopflow::parseFileMessage(const uint8_t *code)
{
  uint32_t id = 0;
  uint8_t value = 0;
  std::string text;

  if (memcmp(code, kMsgHFileRequest, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileRequest + 4, &id, &value, &text);
    m_files.send(id, toPurpose(value), splitPaths(text), transport());
  } else if (memcmp(code, kMsgHFileStart, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileStart + 4, &id, &value, &text);
    m_files.onStart(id, toPurpose(value), text, transport());
  } else if (memcmp(code, kMsgHFileChunk, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileChunk + 4, &id, &text);
    m_files.onChunk(id, text);
  } else if (memcmp(code, kMsgHFileAck, 4) == 0) {
    uint32_t chunks = 0;
    ProtocolUtil::readf(getStream(), kMsgHFileAck + 4, &id, &chunks);
    m_files.onAck(id, chunks);
  } else if (memcmp(code, kMsgHFileEnd, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileEnd + 4, &id, &value);
    m_files.onEnd(id, toStatus(value));
  } else {
    return false;
  }
  return true;
}

Transport ServerProxyHopflow::transport()
{
  auto *stream = getStream();
  return Transport{
      .start = [stream](
                   uint32_t id, FileTransferPurpose purpose, const std::string &manifest
               ) { ProtocolUtil::writef(stream, kMsgHFileStart, id, static_cast<uint8_t>(purpose), &manifest); },
      .chunk =
          [stream](uint32_t id, const std::string &data) { ProtocolUtil::writef(stream, kMsgHFileChunk, id, &data); },
      .ack = [stream](uint32_t id, uint32_t chunks) { ProtocolUtil::writef(stream, kMsgHFileAck, id, chunks); },
      .end = [stream](
                 uint32_t id, FileTransferStatus status
             ) { ProtocolUtil::writef(stream, kMsgHFileEnd, id, static_cast<uint8_t>(status)); }
  };
}

void ServerProxyHopflow::received(uint32_t id, FileTransferPurpose purpose, const std::vector<std::string> &paths)
{
  if (purpose == FileTransferPurpose::Clipboard) {
    m_clipboardReceived = paths;
    if (!getClient()->getComputer()->setClipboardFiles(paths)) {
      LOG_WARN("this computer cannot put files on the clipboard");
    }
    removeOldClipboardTransfers(id);
  }

  const auto kind = purpose == FileTransferPurpose::Clipboard ? QStringLiteral("clipboard") : QStringLiteral("drop");
  ipcSendToClient(QStringLiteral("filesReceived"), kind + QLatin1Char('\n') + QString::fromStdString(joinPaths(paths)));
}
