/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileTransferManager.h"
#include "deskflow/ProtocolTypes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

class BaseClientProxy;

namespace deskflow {
class Computer;
}

//! Decides which files go where, and relays transfers between computers
/*!
Computers offer files when they copy them or drag them across an edge. The
router remembers the offer and, when the user moves to another computer that
runs Hopflow (copied files) or drops there (dragged files), asks the offering
computer to send them. Transfers between two clients pass through the server;
transfers to or from the server computer use its own FileTransferManager.
*/
class FileTransferRouter
{
public:
  FileTransferRouter(BaseClientProxy *primary, deskflow::Computer *computer);
  ~FileTransferRouter();

  void setEnabled(bool enabled);
  void setMaxTransferBytes(uint64_t bytes);

  //! @name events from the server
  //@{

  //! \p from has new files on its clipboard, or none when \p paths is empty
  void onClipboardFiles(BaseClientProxy *from, const std::vector<std::string> &paths);

  //! The server computer's clipboard changed
  void onPrimaryClipboardChanged();

  //! A client that does not run Hopflow took the clipboard
  void onClipboardTakenByOther(BaseClientProxy *by);

  //! \p from is dragging \p paths, or stopped dragging when it is empty
  void onDragFiles(BaseClientProxy *from, const std::vector<std::string> &paths);

  //! The cursor entered \p computer
  void onEnter(BaseClientProxy *computer);

  //! The mouse button was released on \p computer
  void onDrop(BaseClientProxy *computer);

  //! True while files are being dragged from \p computer
  bool isDragging(const BaseClientProxy *computer) const;

  void onClientRemoved(BaseClientProxy *client);

  //@}
  //! @name messages from clients
  //@{

  void onOffer(BaseClientProxy *from, FileTransferPurpose purpose, const std::string &paths);
  void onStart(BaseClientProxy *from, uint32_t id, FileTransferPurpose purpose, const std::string &manifest);
  void onChunk(BaseClientProxy *from, uint32_t id, const std::string &data);
  void onAck(BaseClientProxy *from, uint32_t id, uint32_t chunks);
  void onEnd(BaseClientProxy *from, uint32_t id, FileTransferStatus status);

  //@}

private:
  struct Offer
  {
    BaseClientProxy *owner = nullptr;
    std::vector<std::string> paths;
    std::set<BaseClientProxy *> deliveredTo;
  };

  struct Route
  {
    BaseClientProxy *source;
    BaseClientProxy *destination;
  };

  void startTransfer(
      BaseClientProxy *source, BaseClientProxy *destination, FileTransferPurpose purpose,
      const std::vector<std::string> &paths
  );
  deskflow::filetransfer::Transport transportTo(BaseClientProxy *client);
  void endRoute(uint32_t id);
  void localReceived(FileTransferPurpose purpose, uint32_t id, const std::vector<std::string> &paths);
  bool canReceive(const BaseClientProxy *computer) const;

  BaseClientProxy *m_primary;
  deskflow::Computer *m_computer;
  deskflow::filetransfer::FileTransferManager m_local;
  bool m_enabled = true;
  uint64_t m_maxTransferBytes = deskflow::filetransfer::FileTransferManager::kDefaultMaxTransferBytes;
  uint32_t m_nextId = 1;
  uint32_t m_lastLocalClipboardId = 0;
  std::map<uint32_t, Route> m_routes;
  Offer m_clipboard;
  Offer m_drag;
  //! Files the server computer received for its clipboard, so they are not offered back
  std::vector<std::string> m_localClipboardReceived;
};
