/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/FileTransfer.h"
#include "deskflow/ProtocolTypes.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace deskflow::filetransfer {

//! Where a transfer's messages go: the peer computer, or the server relaying to it
struct Transport
{
  std::function<void(uint32_t id, FileTransferPurpose, const std::string &manifest)> start;
  std::function<void(uint32_t id, const std::string &data)> chunk;
  std::function<void(uint32_t id, uint32_t chunks)> ack;
  std::function<void(uint32_t id, FileTransferStatus)> end;
};

//! Runs the file transfers this computer sends and receives
/*!
Sending keeps at most kFileTransferWindow chunks unacknowledged and sends more
as acknowledgements arrive, so a large transfer never piles up in memory.
Receiving writes through a Receiver and reports the finished files.
*/
class FileTransferManager
{
public:
  struct Callbacks
  {
    //! Folder to receive a transfer into
    std::function<std::filesystem::path(FileTransferPurpose, uint32_t id)> destination;
    //! Files of a transfer arrived and were moved into place
    std::function<void(uint32_t id, FileTransferPurpose, const std::vector<std::string> &paths)> received;
    //! A transfer failed or was refused; \p message is for the user
    std::function<void(const std::string &message)> failed;
  };

  explicit FileTransferManager(Callbacks callbacks, uint64_t maxTransferBytes = kDefaultMaxTransferBytes);
  ~FileTransferManager();

  //! Start sending \p paths as transfer \p id; returns false if they cannot be read
  bool send(uint32_t id, FileTransferPurpose purpose, const std::vector<std::string> &paths, Transport transport);

  void onStart(uint32_t id, FileTransferPurpose purpose, const std::string &manifest, Transport transport);
  void onChunk(uint32_t id, const std::string &data);
  void onAck(uint32_t id, uint32_t chunks);
  void onEnd(uint32_t id, FileTransferStatus status);

  //! Cancel every transfer, telling the peers
  void cancelAll();

  bool isActive(uint32_t id) const;

  void setMaxTransferBytes(uint64_t bytes)
  {
    m_maxTransferBytes = bytes;
  }

  static constexpr uint64_t kDefaultMaxTransferBytes = 2ull * 1024 * 1024 * 1024;

private:
  struct Outgoing
  {
    Sender sender;
    Transport transport;
    uint32_t sent = 0;
    uint32_t acked = 0;
  };

  struct Incoming
  {
    std::unique_ptr<Receiver> receiver;
    FileTransferPurpose purpose;
    Transport transport;
    uint32_t chunks = 0;
  };

  void pump(uint32_t id);
  void pumpOne(uint32_t id);
  void failIncoming(uint32_t id, const std::string &message);

  Callbacks m_callbacks;
  uint64_t m_maxTransferBytes;
  std::map<uint32_t, Outgoing> m_outgoing;
  std::map<uint32_t, Incoming> m_incoming;
  std::set<uint32_t> m_pumpQueue;
  bool m_pumping = false;
};

} // namespace deskflow::filetransfer
