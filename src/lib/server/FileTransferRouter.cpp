/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/FileTransferRouter.h"

#include "base/Log.h"
#include "deskflow/Computer.h"
#include "deskflow/FileTransfer.h"
#include "deskflow/FileTransferLocations.h"
#include "deskflow/ipc/CoreIpc.h"
#include "server/BaseClientProxy.h"

#include <QString>

using namespace deskflow::filetransfer;

namespace {

QString describeReceived(FileTransferPurpose purpose, const std::vector<std::string> &paths)
{
  const auto kind = purpose == FileTransferPurpose::Clipboard ? QStringLiteral("clipboard") : QStringLiteral("drop");
  return kind + QLatin1Char('\n') + QString::fromStdString(joinPaths(paths));
}

} // namespace

FileTransferRouter::FileTransferRouter(BaseClientProxy *primary, deskflow::Computer *computer)
    : m_primary(primary),
      m_computer(computer),
      m_local(
          FileTransferManager::Callbacks{
              .destination = transferFolder,
              .received = [this](
                              uint32_t id, FileTransferPurpose purpose, const std::vector<std::string> &paths
                          ) { localReceived(purpose, id, paths); },
              .failed = [](const std::string &message
                        ) { ipcSendToClient(QStringLiteral("fileTransferFailed"), QString::fromStdString(message)); }
          }
      )
{
}

FileTransferRouter::~FileTransferRouter() = default;

void FileTransferRouter::setEnabled(bool enabled)
{
  if (m_enabled && !enabled) {
    LOG_INFO("file transfer is disabled");
    m_clipboard = {};
    m_drag = {};
  }
  m_enabled = enabled;
}

void FileTransferRouter::setMaxTransferBytes(uint64_t bytes)
{
  m_maxTransferBytes = bytes;
  m_local.setMaxTransferBytes(bytes);
}

bool FileTransferRouter::canReceive(const BaseClientProxy *computer) const
{
  return computer == m_primary || computer->isHopflow();
}

void FileTransferRouter::onClipboardFiles(BaseClientProxy *from, const std::vector<std::string> &paths)
{
  if (paths.empty()) {
    // whoever copied last decides what the clipboard holds
    if (m_clipboard.owner != nullptr) {
      LOG_DEBUG("clipboard no longer holds files");
    }
    m_clipboard = {};
    return;
  }

  LOG_DEBUG("\"%s\" copied %zu files", from->getName().c_str(), paths.size());
  m_clipboard = Offer{from, paths, {}};
}

void FileTransferRouter::onPrimaryClipboardChanged()
{
  const auto files = m_computer->getClipboardFiles();
  if (!files.empty() && files == m_localClipboardReceived) {
    // our own paste of received files, not a new copy
    return;
  }
  m_localClipboardReceived.clear();
  onClipboardFiles(m_primary, files);
}

void FileTransferRouter::onClipboardTakenByOther(BaseClientProxy *by)
{
  if (!by->isHopflow()) {
    m_clipboard = {};
  }
}

void FileTransferRouter::onDragFiles(BaseClientProxy *from, const std::vector<std::string> &paths)
{
  if (paths.empty()) {
    if (m_drag.owner == from) {
      m_drag = {};
    }
    return;
  }

  if (m_drag.owner != from) {
    LOG_INFO("\"%s\" is dragging %zu files", from->getName().c_str(), paths.size());
  }
  m_drag = Offer{from, paths, {}};
}

bool FileTransferRouter::isDragging(const BaseClientProxy *computer) const
{
  return m_enabled && m_drag.owner == computer;
}

void FileTransferRouter::onEnter(BaseClientProxy *computer)
{
  auto &offer = m_clipboard;
  if (!m_enabled || offer.owner == nullptr || offer.owner == computer || !canReceive(computer) ||
      offer.deliveredTo.contains(computer)) {
    return;
  }

  offer.deliveredTo.insert(computer);
  startTransfer(offer.owner, computer, FileTransferPurpose::Clipboard, offer.paths);
}

void FileTransferRouter::onDrop(BaseClientProxy *computer)
{
  if (m_drag.owner == nullptr) {
    return;
  }

  const auto drag = std::exchange(m_drag, {});
  if (!m_enabled || drag.owner == computer) {
    return;
  }

  if (!canReceive(computer)) {
    LOG_WARN("files dropped on \"%s\", which does not run Hopflow", computer->getName().c_str());
    return;
  }

  startTransfer(drag.owner, computer, FileTransferPurpose::Drop, drag.paths);
}

void FileTransferRouter::startTransfer(
    BaseClientProxy *source, BaseClientProxy *destination, FileTransferPurpose purpose,
    const std::vector<std::string> &paths
)
{
  const auto id = m_nextId++;
  LOG_INFO(
      "file transfer %u: %zu items from \"%s\" to \"%s\"", id, paths.size(), source->getName().c_str(),
      destination->getName().c_str()
  );

  m_routes.insert_or_assign(id, Route{source, destination});
  if (source == m_primary) {
    m_local.send(id, purpose, paths, transportTo(destination));
  } else {
    source->fileRequest(id, purpose, joinPaths(paths));
  }
}

Transport FileTransferRouter::transportTo(BaseClientProxy *client)
{
  return Transport{
      .start = [client](
                   uint32_t id, FileTransferPurpose purpose, const std::string &manifest
               ) { client->fileStart(id, purpose, manifest); },
      .chunk = [client](uint32_t id, const std::string &data) { client->fileChunk(id, data); },
      .ack = [client](uint32_t id, uint32_t chunks) { client->fileAck(id, chunks); },
      .end =
          [this, client](uint32_t id, FileTransferStatus status) {
            client->fileEnd(id, status);
            endRoute(id);
          }
  };
}

void FileTransferRouter::endRoute(uint32_t id)
{
  m_routes.erase(id);
}

void FileTransferRouter::localReceived(FileTransferPurpose purpose, uint32_t id, const std::vector<std::string> &paths)
{
  if (purpose == FileTransferPurpose::Clipboard) {
    m_localClipboardReceived = paths;
    if (!m_computer->setClipboardFiles(paths)) {
      LOG_WARN("this computer cannot put files on the clipboard");
    }
    removeOldClipboardTransfers(id);
  }
  ipcSendToClient(QStringLiteral("filesReceived"), describeReceived(purpose, paths));
}

void FileTransferRouter::onClientRemoved(BaseClientProxy *client)
{
  if (m_clipboard.owner == client) {
    m_clipboard = {};
  }
  m_clipboard.deliveredTo.erase(client);
  if (m_drag.owner == client) {
    m_drag = {};
  }

  for (auto it = m_routes.begin(); it != m_routes.end();) {
    const auto [id, route] = *it;
    if (route.source != client && route.destination != client) {
      ++it;
      continue;
    }

    it = m_routes.erase(it);
    LOG_INFO("file transfer %u: cancelled, \"%s\" disconnected", id, client->getName().c_str());
    auto *other = route.source == client ? route.destination : route.source;
    if (other == m_primary) {
      m_local.onEnd(id, FileTransferStatus::Cancelled);
    } else {
      other->fileEnd(id, FileTransferStatus::Cancelled);
    }
  }
}

void FileTransferRouter::onOffer(BaseClientProxy *from, FileTransferPurpose purpose, const std::string &paths)
{
  if (purpose == FileTransferPurpose::Clipboard) {
    onClipboardFiles(from, splitPaths(paths));
  } else {
    onDragFiles(from, splitPaths(paths));
  }
}

void FileTransferRouter::onStart(
    BaseClientProxy *from, uint32_t id, FileTransferPurpose purpose, const std::string &manifest
)
{
  auto it = m_routes.find(id);
  if (it == m_routes.end() || it->second.source != from) {
    LOG_WARN("file transfer %u: unexpected start from \"%s\"", id, from->getName().c_str());
    from->fileEnd(id, FileTransferStatus::Cancelled);
    return;
  }

  const auto route = it->second;
  std::string error;
  const auto parsed = parseManifest(manifest, error);
  if (!m_enabled || !parsed || totalSize(*parsed) > m_maxTransferBytes) {
    LOG_WARN("file transfer %u: refused, %s", id, !m_enabled ? "disabled" : parsed ? "too large" : error.c_str());
    m_routes.erase(it);
    from->fileEnd(id, FileTransferStatus::Cancelled);
    if (parsed && m_enabled) {
      ipcSendToClient(QStringLiteral("fileTransferFailed"), QStringLiteral("the files are larger than the limit"));
    }
    return;
  }

  if (route.destination == m_primary) {
    m_local.onStart(id, purpose, manifest, transportTo(from));
  } else {
    route.destination->fileStart(id, purpose, manifest);
  }
}

void FileTransferRouter::onChunk(BaseClientProxy *from, uint32_t id, const std::string &data)
{
  auto it = m_routes.find(id);
  if (it == m_routes.end() || it->second.source != from) {
    return;
  }

  if (it->second.destination == m_primary) {
    m_local.onChunk(id, data);
  } else {
    it->second.destination->fileChunk(id, data);
  }
}

void FileTransferRouter::onAck(BaseClientProxy *from, uint32_t id, uint32_t chunks)
{
  auto it = m_routes.find(id);
  if (it == m_routes.end() || it->second.destination != from) {
    return;
  }

  if (it->second.source == m_primary) {
    m_local.onAck(id, chunks);
  } else {
    it->second.source->fileAck(id, chunks);
  }
}

void FileTransferRouter::onEnd(BaseClientProxy *from, uint32_t id, FileTransferStatus status)
{
  auto it = m_routes.find(id);
  if (it == m_routes.end() || (it->second.source != from && it->second.destination != from)) {
    return;
  }

  const auto route = it->second;
  m_routes.erase(it);
  auto *other = route.source == from ? route.destination : route.source;
  if (other == m_primary) {
    m_local.onEnd(id, status);
  } else {
    other->fileEnd(id, status);
  }
}
