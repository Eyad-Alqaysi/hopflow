/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_8.h"

//! Proxy for a server implementing the Hopflow protocol (1.100)
/*!
Adds the Hopflow messages on top of upstream protocol 1.8.
*/
class ServerProxyHopflow : public ServerProxy1_8
{
public:
  ServerProxyHopflow(Client *client, deskflow::IStream *stream, IEventQueue *events);
  ~ServerProxyHopflow() override = default;

protected:
  void queryInfo() override;
};
