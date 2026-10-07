/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/OptionTypes.h"
#include "deskflow/ProtocolTypes.h"

//! Swap Ctrl and Cmd for a client whose platform differs from the server's
/*!
When exactly one of the server and the client is macOS, appends modifier
mappings to \p options so the client turns Ctrl into Super (Cmd on macOS)
and Super into Ctrl. Clients apply options in order, so these override the
per-computer mappings sent earlier in the list.

Nothing changes when either platform is unknown (the client is not running
Hopflow), or when the user mapped Ctrl or Super to anything but itself:
an explicit mapping always wins.

Returns true if the swap was added.
*/
bool applyAutoCtrlCmdSwap(OptionsList &options, PeerPlatform server, PeerPlatform client);
