/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsFileClipboard.h"

#include "base/Log.h"

#include <QString>

#include <ShlObj.h>
#include <shellapi.h>

#include <cstring>

namespace deskflow::mswindows {

namespace {

//! Closes the clipboard when it goes out of scope
class ClipboardLock
{
public:
  explicit ClipboardLock(HWND owner) : m_open(OpenClipboard(owner) != 0)
  {
  }
  ClipboardLock(const ClipboardLock &) = delete;
  ClipboardLock &operator=(const ClipboardLock &) = delete;
  ~ClipboardLock()
  {
    if (m_open) {
      CloseClipboard();
    }
  }

  bool isOpen() const
  {
    return m_open;
  }

private:
  bool m_open;
};

HGLOBAL makeGlobal(const void *data, size_t size)
{
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
  if (memory == nullptr) {
    return nullptr;
  }
  std::memcpy(GlobalLock(memory), data, size);
  GlobalUnlock(memory);
  return memory;
}

} // namespace

std::vector<std::string> filesFromDrop(HDROP drop)
{
  std::vector<std::string> paths;
  const auto count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT i = 0; i < count; ++i) {
    const auto length = DragQueryFileW(drop, i, nullptr, 0);
    std::wstring path(length + 1, L'\0');
    DragQueryFileW(drop, i, path.data(), length + 1);
    path.resize(length);
    paths.push_back(QString::fromStdWString(path).toStdString());
  }
  return paths;
}

std::vector<std::string> clipboardFiles(HWND owner)
{
  if (!IsClipboardFormatAvailable(CF_HDROP)) {
    return {};
  }

  ClipboardLock lock(owner);
  if (!lock.isOpen()) {
    LOG_DEBUG("cannot open the clipboard to read files");
    return {};
  }

  auto *drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
  if (drop == nullptr) {
    return {};
  }
  return filesFromDrop(drop);
}

bool setClipboardFiles(HWND owner, const std::vector<std::string> &paths)
{
  // DROPFILES is followed by the paths, each null terminated, then one more null
  std::wstring list;
  for (const auto &path : paths) {
    list += QString::fromStdString(path).toStdWString();
    list += L'\0';
  }
  list += L'\0';

  std::string buffer(sizeof(DROPFILES) + list.size() * sizeof(wchar_t), '\0');
  auto *header = reinterpret_cast<DROPFILES *>(buffer.data());
  header->pFiles = sizeof(DROPFILES);
  header->fWide = TRUE;
  std::memcpy(buffer.data() + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));

  const DWORD effect = DROPEFFECT_COPY;
  HGLOBAL files = makeGlobal(buffer.data(), buffer.size());
  HGLOBAL dropEffect = makeGlobal(&effect, sizeof(effect));
  if (files == nullptr || dropEffect == nullptr) {
    GlobalFree(files);
    GlobalFree(dropEffect);
    return false;
  }

  ClipboardLock lock(owner);
  if (!lock.isOpen() || !EmptyClipboard()) {
    GlobalFree(files);
    GlobalFree(dropEffect);
    return false;
  }

  // the clipboard owns the memory once SetClipboardData succeeds
  if (SetClipboardData(CF_HDROP, files) == nullptr) {
    GlobalFree(files);
    GlobalFree(dropEffect);
    return false;
  }
  if (SetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), dropEffect) == nullptr) {
    GlobalFree(dropEffect);
  }
  return true;
}

} // namespace deskflow::mswindows
