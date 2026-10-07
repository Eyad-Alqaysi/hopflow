/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransferManager.h"

#include "base/Log.h"

namespace fs = std::filesystem;

namespace deskflow::filetransfer {

namespace {

// acknowledge often enough that the sender never stalls on a full window
constexpr uint32_t kAckEvery = kFileTransferWindow / 4;

std::string megabytes(uint64_t bytes)
{
  return std::to_string((bytes + 1024 * 1024 - 1) / (1024 * 1024)) + " MB";
}

} // namespace

FileTransferManager::FileTransferManager(Callbacks callbacks, uint64_t maxTransferBytes)
    : m_callbacks(std::move(callbacks)),
      m_maxTransferBytes(maxTransferBytes)
{
}

FileTransferManager::~FileTransferManager() = default;

bool FileTransferManager::send(
    uint32_t id, FileTransferPurpose purpose, const std::vector<std::string> &paths, Transport transport
)
{
  std::string error;
  auto sender = Sender::create(paths, error);
  if (!sender) {
    LOG_WARN("file transfer %u: %s", id, error.c_str());
    transport.end(id, FileTransferStatus::Error);
    if (m_callbacks.failed) {
      m_callbacks.failed(error);
    }
    return false;
  }

  const auto bytes = totalSize(sender->manifest());
  LOG_INFO("file transfer %u: sending %zu items, %s", id, sender->manifest().size(), megabytes(bytes).c_str());
  transport.start(id, purpose, serializeManifest(sender->manifest()));
  m_outgoing.insert_or_assign(id, Outgoing{std::move(*sender), std::move(transport)});
  pump(id);
  return true;
}

void FileTransferManager::pump(uint32_t id)
{
  // sending can re-enter (an ack or cancel delivered synchronously), so only the
  // outermost call sends and it picks up any transfer that asked to pump meanwhile
  m_pumpQueue.insert(id);
  if (m_pumping) {
    return;
  }

  m_pumping = true;
  while (!m_pumpQueue.empty()) {
    const auto next = *m_pumpQueue.begin();
    m_pumpQueue.erase(m_pumpQueue.begin());
    pumpOne(next);
  }
  m_pumping = false;
}

void FileTransferManager::pumpOne(uint32_t id)
{
  std::string chunk;
  std::string error;
  while (true) {
    auto it = m_outgoing.find(id);
    if (it == m_outgoing.end()) {
      return;
    }

    auto &outgoing = it->second;
    if (outgoing.sent - outgoing.acked >= kFileTransferWindow) {
      return;
    }

    // copy the transport: a callback may end the transfer and destroy the original
    const auto transport = outgoing.transport;
    if (!outgoing.sender.readChunk(chunk, error)) {
      LOG_WARN("file transfer %u: %s", id, error.c_str());
      m_outgoing.erase(it);
      transport.end(id, FileTransferStatus::Error);
      if (m_callbacks.failed) {
        m_callbacks.failed(error);
      }
      return;
    }

    if (chunk.empty()) {
      LOG_INFO("file transfer %u: all data sent", id);
      m_outgoing.erase(it);
      transport.end(id, FileTransferStatus::Ok);
      return;
    }

    ++outgoing.sent;
    transport.chunk(id, chunk);
  }
}

void FileTransferManager::onAck(uint32_t id, uint32_t chunks)
{
  auto it = m_outgoing.find(id);
  if (it == m_outgoing.end()) {
    return;
  }

  if (chunks > it->second.sent) {
    LOG_WARN("file transfer %u: peer acknowledged chunks that were not sent", id);
    return;
  }
  it->second.acked = std::max(it->second.acked, chunks);
  pump(id);
}

void FileTransferManager::onStart(
    uint32_t id, FileTransferPurpose purpose, const std::string &manifest, Transport transport
)
{
  if (m_incoming.contains(id)) {
    LOG_WARN("file transfer %u: already receiving", id);
    transport.end(id, FileTransferStatus::Error);
    return;
  }

  auto refuse = [&](const std::string &message) {
    LOG_WARN("file transfer %u: refused, %s", id, message.c_str());
    transport.end(id, FileTransferStatus::Cancelled);
    if (m_callbacks.failed) {
      m_callbacks.failed(message);
    }
  };

  std::string error;
  auto parsed = parseManifest(manifest, error);
  if (!parsed) {
    refuse(error);
    return;
  }

  const auto bytes = totalSize(*parsed);
  if (bytes > m_maxTransferBytes) {
    refuse("files are " + megabytes(bytes) + ", more than the " + megabytes(m_maxTransferBytes) + " limit");
    return;
  }

  const auto destination = m_callbacks.destination(purpose, id);
  std::error_code ec;
  fs::create_directories(destination, ec);
  if (const auto space = fs::space(destination, ec); !ec && space.available < bytes) {
    refuse("not enough disk space for " + megabytes(bytes));
    return;
  }

  auto receiver = std::make_unique<Receiver>(destination, std::move(*parsed));
  if (!receiver->begin(error)) {
    refuse(error);
    return;
  }

  LOG_INFO("file transfer %u: receiving %s into %s", id, megabytes(bytes).c_str(), pathToUtf8(destination).c_str());
  m_incoming.insert_or_assign(id, Incoming{std::move(receiver), purpose, std::move(transport)});
}

void FileTransferManager::onChunk(uint32_t id, const std::string &data)
{
  auto it = m_incoming.find(id);
  if (it == m_incoming.end()) {
    return;
  }

  auto &incoming = it->second;
  std::string error;
  if (!incoming.receiver->write(data, error)) {
    failIncoming(id, error);
    return;
  }

  ++incoming.chunks;
  if (incoming.chunks % kAckEvery == 0) {
    const auto ack = incoming.transport.ack;
    ack(id, incoming.chunks);
  }
}

void FileTransferManager::onEnd(uint32_t id, FileTransferStatus status)
{
  if (auto out = m_outgoing.find(id); out != m_outgoing.end()) {
    LOG_INFO("file transfer %u: cancelled by the receiver", id);
    m_outgoing.erase(out);
    return;
  }

  auto it = m_incoming.find(id);
  if (it == m_incoming.end()) {
    return;
  }

  if (status != FileTransferStatus::Ok) {
    LOG_WARN("file transfer %u: sender stopped the transfer", id);
    it->second.receiver->abort();
    m_incoming.erase(it);
    if (m_callbacks.failed) {
      m_callbacks.failed("the other computer stopped sending the files");
    }
    return;
  }

  std::string error;
  auto paths = it->second.receiver->finish(error);
  if (!paths) {
    failIncoming(id, error);
    return;
  }

  const auto purpose = it->second.purpose;
  m_incoming.erase(it);
  LOG_INFO("file transfer %u: received %zu items", id, paths->size());
  if (m_callbacks.received) {
    m_callbacks.received(id, purpose, *paths);
  }
}

void FileTransferManager::failIncoming(uint32_t id, const std::string &message)
{
  auto it = m_incoming.find(id);
  if (it == m_incoming.end()) {
    return;
  }

  LOG_WARN("file transfer %u: %s", id, message.c_str());
  it->second.receiver->abort();
  const auto end = it->second.transport.end;
  m_incoming.erase(it);
  end(id, FileTransferStatus::Error);
  if (m_callbacks.failed) {
    m_callbacks.failed(message);
  }
}

void FileTransferManager::cancelAll()
{
  auto outgoing = std::move(m_outgoing);
  auto incoming = std::move(m_incoming);
  m_outgoing.clear();
  m_incoming.clear();

  for (auto &[id, transfer] : outgoing) {
    transfer.transport.end(id, FileTransferStatus::Cancelled);
  }
  for (auto &[id, transfer] : incoming) {
    transfer.receiver->abort();
    transfer.transport.end(id, FileTransferStatus::Cancelled);
  }
}

bool FileTransferManager::isActive(uint32_t id) const
{
  return m_outgoing.contains(id) || m_incoming.contains(id);
}

} // namespace deskflow::filetransfer
