/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/DragProbe.h"

#include "base/Log.h"
#include "deskflow/ipc/CoreIpc.h"

#include <QString>

#include <mutex>

namespace deskflow::dragprobe {

namespace {

std::mutex s_mutex;
bool s_active = false;
std::vector<std::string> s_files;

} // namespace

void start(int x, int y)
{
  {
    std::scoped_lock lock(s_mutex);
    if (s_active) {
      return;
    }
    s_active = true;
    s_files.clear();
  }

  LOG_DEBUG("asking the gui for dragged files at %d,%d", x, y);
  ipcSendToClient(QStringLiteral("dragProbe"), QStringLiteral("%1,%2").arg(x).arg(y));
}

void stop()
{
  {
    std::scoped_lock lock(s_mutex);
    if (!s_active) {
      return;
    }
    s_active = false;
    s_files.clear();
  }

  ipcSendToClient(QStringLiteral("dragProbeEnd"));
}

std::vector<std::string> files()
{
  std::scoped_lock lock(s_mutex);
  return s_files;
}

void setFiles(const std::vector<std::string> &paths)
{
  std::scoped_lock lock(s_mutex);
  // a late report after the drag ended must not start a new one
  if (s_active) {
    s_files = paths;
  }
}

} // namespace deskflow::dragprobe
