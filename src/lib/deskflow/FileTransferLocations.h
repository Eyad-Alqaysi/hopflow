/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ProtocolTypes.h"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace deskflow::filetransfer {

//! Folder that dropped files are saved to: the signed-in user's Downloads/Hopflow
/*!
On Windows the core may run as SYSTEM in the user's session, so this asks for
the Downloads folder of the user signed in at the console, not the process's.
The HOPFLOW_DOWNLOAD_DIR environment variable overrides it.
*/
std::filesystem::path receivedFilesFolder();

//! Folder a transfer of \p purpose is received into
/*!
Dropped files go straight to receivedFilesFolder(). Copied files are staged
per transfer in a .clipboard subfolder until they are pasted.
*/
std::filesystem::path transferFolder(FileTransferPurpose purpose, uint32_t id);

//! Delete staged clipboard transfers other than \p keepId
void removeOldClipboardTransfers(uint32_t keepId);

} // namespace deskflow::filetransfer
