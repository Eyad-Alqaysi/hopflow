/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <string>
#include <vector>

//! File URLs on the general and drag pasteboards, as UTF-8 paths
namespace deskflow::osx {

std::vector<std::string> clipboardFiles();
bool setClipboardFiles(const std::vector<std::string> &paths);

//! Change count of the drag pasteboard, which increases when a drag starts
long dragPasteboardChangeCount();
std::vector<std::string> dragPasteboardFiles();

} // namespace deskflow::osx
