/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_8.h"

//! Proxy for a client implementing the Hopflow protocol (1.100)
/*!
Adds the Hopflow messages on top of upstream protocol 1.8.
*/
class ClientProxyHopflow : public ClientProxy1_8
{
public:
  ClientProxyHopflow(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxyHopflow() override = default;

  bool isHopflow() const override
  {
    return true;
  }

  void fileRequest(uint32_t id, FileTransferPurpose purpose, const std::string &paths) override;
  void fileStart(uint32_t id, FileTransferPurpose purpose, const std::string &manifest) override;
  void fileChunk(uint32_t id, const std::string &data) override;
  void fileAck(uint32_t id, uint32_t chunks) override;
  void fileEnd(uint32_t id, FileTransferStatus status) override;

protected:
  bool parseHandshakeMessage(const uint8_t *code) override;
  bool parseMessage(const uint8_t *code) override;

private:
  bool parseHopflowMessage(const uint8_t *code);
  bool parseFileMessage(const uint8_t *code);
  void recvPlatform();
};
