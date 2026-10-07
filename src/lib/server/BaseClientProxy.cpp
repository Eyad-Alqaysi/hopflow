/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2006 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/BaseClientProxy.h"

#include "base/Log.h"

//
// BaseClientProxy
//

BaseClientProxy::BaseClientProxy(const std::string &name) : m_name(name)
{
  // do nothing
}

void BaseClientProxy::setJumpCursorPos(int32_t x, int32_t y)
{
  m_x = x;
  m_y = y;
}

void BaseClientProxy::getJumpCursorPos(int32_t &x, int32_t &y) const
{
  x = m_x;
  y = m_y;
}

std::string BaseClientProxy::getName() const
{
  return m_name;
}

void BaseClientProxy::fileRequest(uint32_t id, FileTransferPurpose, const std::string &)
{
  LOG_WARN("file transfer %u: \"%s\" cannot send files", id, getName().c_str());
}

void BaseClientProxy::fileStart(uint32_t id, FileTransferPurpose, const std::string &)
{
  LOG_WARN("file transfer %u: \"%s\" cannot receive files", id, getName().c_str());
}

void BaseClientProxy::fileChunk(uint32_t, const std::string &)
{
  // the start was already refused
}

void BaseClientProxy::fileAck(uint32_t, uint32_t)
{
  // the request was already refused
}

void BaseClientProxy::fileEnd(uint32_t, FileTransferStatus)
{
  // the start or request was already refused
}
