/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ProtocolTypes.h"

#include <string>
#include <vector>

//! Tell the GUI about file transfers, for notifications
namespace deskflow::filetransfer {

void notifyReceived(FileTransferPurpose purpose, const std::vector<std::string> &paths);
void notifyFailed(const std::string &message);

} // namespace deskflow::filetransfer
