/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <QRect>

namespace hopflow::stream {

//! A monitor of the Virtual Display Driver, added to the desktop right of the others
/*!
See stream/VirtualDisplayDriver.h for how the driver is controlled. create()
may restart the driver so it offers the size asked for, then waits for
Windows to bring the monitor up, so it can block for several seconds and
createsSlowly() says so. Changing the driver's settings, or turning it on, needs administrator rights
the first time a size is used; error() says so when Windows refuses.
*/
class WinVirtualDisplay : public IVirtualDisplay
{
public:
  WinVirtualDisplay() = default;
  WinVirtualDisplay(const WinVirtualDisplay &) = delete;
  WinVirtualDisplay &operator=(const WinVirtualDisplay &) = delete;
  ~WinVirtualDisplay() override;

  bool create(const DisplayInfo &info) override;
  QString displayId() const override;
  void destroy() override;
  QString error() const override;
  bool createsSlowly() const override
  {
    return true;
  }

private:
  bool fail(const QString &message);

  QString m_displayId;
  QString m_error;
  //! How the monitor was before create(), to put it back
  bool m_wasAttached = false;
  QRect m_previousBounds;
  int m_previousRefreshHz = 0;
  //! create() turned the driver's device on, so destroy() turns it off again
  bool m_enabledDevice = false;
};

} // namespace hopflow::stream
