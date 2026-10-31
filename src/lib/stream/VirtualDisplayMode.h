/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/StreamProtocol.h"

#include <QSize>
#include <QSizeF>

#include <algorithm>

namespace hopflow::stream {

//! Highest frame rate an extended display is streamed at, whatever the viewer's monitor can show
inline constexpr int kMaxExtendFps = 60;

//! The display mode a virtual display needs to match a viewer's monitor
struct VirtualDisplayMode
{
  //! Size in physical pixels, as captured and streamed
  QSize pixels;
  //! Size the desktop lays out windows in; half of pixels when hiDpi
  QSize logical;
  int refreshHz = 0;
  bool hiDpi = false;
  QSizeF millimeters;
};

//! Frame rate for a monitor refreshing at \p refreshHz: its own rate, up to kMaxExtendFps
inline int extendFps(int refreshHz)
{
  return refreshHz > 0 ? std::min(refreshHz, kMaxExtendFps) : kMaxExtendFps;
}

//! Mode for a virtual display that \p info's monitor shows pixel for pixel
/*!
At 200% scaling or more the display is HiDPI, so text keeps the size it has
on that monitor. The physical size assumes 96 dots per inch at 100%, as
Windows does, so the desktop picks the same scaling for it.
*/
inline VirtualDisplayMode virtualDisplayMode(const DisplayInfo &info)
{
  VirtualDisplayMode mode;
  // H.264 needs even sides
  mode.pixels = QSize(info.width & ~1, info.height & ~1);
  mode.hiDpi = info.scalePercent >= 200;
  mode.logical = mode.hiDpi ? mode.pixels / 2 : mode.pixels;
  mode.refreshHz = extendFps(info.refreshHz);

  const double scale = std::max<int>(info.scalePercent, 100) / 100.0;
  const double millimetersPerPixel = 25.4 / (96.0 * scale);
  mode.millimeters = QSizeF(mode.pixels.width() * millimetersPerPixel, mode.pixels.height() * millimetersPerPixel);
  return mode;
}

} // namespace hopflow::stream
