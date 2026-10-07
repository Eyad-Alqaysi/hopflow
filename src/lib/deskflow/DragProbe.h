/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <string>
#include <vector>

//! Finds out which files are being dragged with help from the GUI
/*!
Where the core cannot see a drag itself, for example on Windows where it runs
elevated and Explorer may not drag into it, the GUI shows a tiny window under
the cursor that accepts drags and reports the files it sees. These functions
are thread safe: the core thread starts and stops a probe, and the IPC thread
delivers the files.
*/
namespace deskflow::dragprobe {

//! Ask the GUI to look for a drag at \p x, \p y (physical pixels); repeated calls are ignored
void start(int x, int y);

//! Tell the GUI to stop looking and forget the files
void stop();

//! Files the GUI saw in the current probe
std::vector<std::string> files();

//! Called when the GUI reports the dragged files
void setFiles(const std::vector<std::string> &paths);

} // namespace deskflow::dragprobe
