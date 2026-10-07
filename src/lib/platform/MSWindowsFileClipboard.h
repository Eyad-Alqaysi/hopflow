/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <windows.h>

#include <shellapi.h>

#include <string>
#include <vector>

//! Files on the Windows clipboard (CF_HDROP), as UTF-8 paths
namespace deskflow::mswindows {

std::vector<std::string> clipboardFiles(HWND owner);

//! Put \p paths on the clipboard as a copy (not a cut) so Explorer can paste them
bool setClipboardFiles(HWND owner, const std::vector<std::string> &paths);

//! Paths in a CF_HDROP handle
std::vector<std::string> filesFromDrop(HDROP drop);

} // namespace deskflow::mswindows
