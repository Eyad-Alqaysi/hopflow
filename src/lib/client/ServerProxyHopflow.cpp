/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerProxyHopflow.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

ServerProxyHopflow::ServerProxyHopflow(Client *client, deskflow::IStream *stream, IEventQueue *events)
    : ServerProxy1_8(client, stream, events)
{
}

void ServerProxyHopflow::queryInfo()
{
  // the platform must reach the server before the info, which is when it sends options
  const auto platform = static_cast<uint8_t>(localPlatform());
  LOG_DEBUG("sending platform %d to server", platform);
  ProtocolUtil::writef(getStream(), kMsgHPlatform, platform);
  ServerProxy1_8::queryInfo();
}
