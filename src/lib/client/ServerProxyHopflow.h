/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ServerProxy1_8.h"
#include "deskflow/FileTransferManager.h"

#include <string>
#include <vector>

//! Proxy for a server implementing the Hopflow protocol (1.100)
/*!
Adds the Hopflow messages on top of upstream protocol 1.8: it reports this
computer's platform, offers files copied or dragged here, and sends and
receives file transfers on the server's request.
*/
class ServerProxyHopflow : public ServerProxy1_8
{
public:
  ServerProxyHopflow(Client *client, deskflow::IStream *stream, IEventQueue *events);
  ~ServerProxyHopflow() override;

  bool onGrabClipboard(ClipboardID id) override;

protected:
  void queryInfo() override;
  ConnectionResult parseMessage(const uint8_t *code) override;
  void onMouseMoved(int32_t x, int32_t y) override;
  void onMouseButton(ButtonID id, bool pressed) override;
  void onLeft() override;

private:
  bool parseFileMessage(const uint8_t *code);
  void offer(FileTransferPurpose purpose, const std::vector<std::string> &paths);
  void received(uint32_t id, FileTransferPurpose purpose, const std::vector<std::string> &paths);
  bool isAtEdge(int32_t x, int32_t y) const;
  deskflow::filetransfer::Transport transport();

  deskflow::filetransfer::FileTransferManager m_files;
  //! Files received for the clipboard, so putting them there is not offered back
  std::vector<std::string> m_clipboardReceived;
  bool m_offeringClipboard = false;
  //! Files offered as being dragged here, while the drag lasts
  std::vector<std::string> m_dragFiles;
  int m_buttonsDown = 0;
};
