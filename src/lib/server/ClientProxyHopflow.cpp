/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxyHopflow.h"

#include "base/Log.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "server/FileTransferRouter.h"
#include "server/Server.h"

#include <cstring>

ClientProxyHopflow::ClientProxyHopflow(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, adoptedStream, server, events)
{
}

bool ClientProxyHopflow::parseHandshakeMessage(const uint8_t *code)
{
  return parseHopflowMessage(code) || ClientProxy1_8::parseHandshakeMessage(code);
}

bool ClientProxyHopflow::parseMessage(const uint8_t *code)
{
  return parseHopflowMessage(code) || parseFileMessage(code) || ClientProxy1_8::parseMessage(code);
}

bool ClientProxyHopflow::parseHopflowMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgHPlatform, 4) == 0) {
    recvPlatform();
    return true;
  }
  return false;
}

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

bool ClientProxyHopflow::parseFileMessage(const uint8_t *code)
{
  auto &router = getServer()->fileTransfers();
  uint32_t id = 0;
  uint8_t value = 0;
  std::string text;

  if (memcmp(code, kMsgHFileOffer, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileOffer + 4, &value, &text);
    router.onOffer(this, toPurpose(value), text);
  } else if (memcmp(code, kMsgHFileStart, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileStart + 4, &id, &value, &text);
    router.onStart(this, id, toPurpose(value), text);
  } else if (memcmp(code, kMsgHFileChunk, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileChunk + 4, &id, &text);
    router.onChunk(this, id, text);
  } else if (memcmp(code, kMsgHFileAck, 4) == 0) {
    uint32_t chunks = 0;
    ProtocolUtil::readf(getStream(), kMsgHFileAck + 4, &id, &chunks);
    router.onAck(this, id, chunks);
  } else if (memcmp(code, kMsgHFileEnd, 4) == 0) {
    ProtocolUtil::readf(getStream(), kMsgHFileEnd + 4, &id, &value);
    router.onEnd(this, id, toStatus(value));
  } else {
    return false;
  }
  return true;
}

void ClientProxyHopflow::fileRequest(uint32_t id, FileTransferPurpose purpose, const std::string &paths)
{
  LOG_DEBUG("file transfer %u: asking \"%s\" to send files", id, getName().c_str());
  ProtocolUtil::writef(getStream(), kMsgHFileRequest, id, static_cast<uint8_t>(purpose), &paths);
}

void ClientProxyHopflow::fileStart(uint32_t id, FileTransferPurpose purpose, const std::string &manifest)
{
  ProtocolUtil::writef(getStream(), kMsgHFileStart, id, static_cast<uint8_t>(purpose), &manifest);
}

void ClientProxyHopflow::fileChunk(uint32_t id, const std::string &data)
{
  ProtocolUtil::writef(getStream(), kMsgHFileChunk, id, &data);
}

void ClientProxyHopflow::fileAck(uint32_t id, uint32_t chunks)
{
  ProtocolUtil::writef(getStream(), kMsgHFileAck, id, chunks);
}

void ClientProxyHopflow::fileEnd(uint32_t id, FileTransferStatus status)
{
  ProtocolUtil::writef(getStream(), kMsgHFileEnd, id, static_cast<uint8_t>(status));
}

void ClientProxyHopflow::recvPlatform()
{
  uint8_t platform = 0;
  ProtocolUtil::readf(getStream(), kMsgHPlatform + 4, &platform);
  if (platform > static_cast<uint8_t>(PeerPlatform::Other)) {
    LOG_WARN("client \"%s\" sent unknown platform %d", getName().c_str(), platform);
    platform = static_cast<uint8_t>(PeerPlatform::Unknown);
  }
  LOG_DEBUG("client \"%s\" platform is %d", getName().c_str(), platform);
  setPlatform(static_cast<PeerPlatform>(platform));
}
