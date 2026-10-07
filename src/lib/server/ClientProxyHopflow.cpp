/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxyHopflow.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

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
  return parseHopflowMessage(code) || ClientProxy1_8::parseMessage(code);
}

bool ClientProxyHopflow::parseHopflowMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgHPlatform, 4) == 0) {
    recvPlatform();
    return true;
  }
  return false;
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
