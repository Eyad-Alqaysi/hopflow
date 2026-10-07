/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/CtrlCmdSwap.h"

#include "common/KeyModifierID.h"

namespace {

bool isMappedToItself(const OptionsList &options, OptionID option, KeyModifierID modifier)
{
  for (size_t i = 0; i + 1 < options.size(); i += 2) {
    if (options[i] == option && options[i + 1] != modifier) {
      return false;
    }
  }
  return true;
}

} // namespace

bool applyAutoCtrlCmdSwap(OptionsList &options, PeerPlatform server, PeerPlatform client)
{
  if (server == PeerPlatform::Unknown || client == PeerPlatform::Unknown) {
    return false;
  }

  if ((server == PeerPlatform::MacOS) == (client == PeerPlatform::MacOS)) {
    return false;
  }

  if (!isMappedToItself(options, kOptionModifierMapForControl, kKeyModifierIDControl) ||
      !isMappedToItself(options, kOptionModifierMapForSuper, kKeyModifierIDSuper)) {
    return false;
  }

  options.push_back(kOptionModifierMapForControl);
  options.push_back(kKeyModifierIDSuper);
  options.push_back(kOptionModifierMapForSuper);
  options.push_back(kKeyModifierIDControl);
  return true;
}
