/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QPoint>
#include <QRect>
#include <QString>

#include <optional>

//! Rules for driving the Windows "Virtual Display Driver", kept free of Windows APIs so they can be tested anywhere
/*!
The driver (https://github.com/VirtualDrivers/Virtual-Display-Driver, MIT) is
a signed IddCx indirect display driver. What Hopflow relies on, read from its
source (Virtual Display Driver (HDR)/MttVDD/Driver.cpp, MttVDD.inf):

- It is a root enumerated device of the Display class with the hardware ID
  "Root\MttVDD" (the INF also lists plain "MttVDD").
- Settings live in vdd_settings.xml in the folder named by the REG_SZ value
  HKLM\SOFTWARE\MikeTheTech\VirtualDisplayDriver\VDDPATH, by default
  C:\VirtualDisplayDriver. <monitors><count> is the number of monitors
  (0 counts as 1); each <resolutions><resolution> holds <width>, <height> and
  <refresh_rate>, and every <global><g_refresh_rate> is offered for every
  resolution as well.
- The settings are read only when the device starts (EvtDeviceAdd), so a new
  mode needs the device restarted.
- Its monitors carry an EDID with the PnP ID "MTT1337", unless the user loads
  their own EDID and turns off its manufacturer override.
- It also serves \\.\pipe\MTTVirtualDisplayPipe (UTF-16 messages such as
  "PING", "RELOAD_DRIVER", "SETDISPLAYCOUNT n", "GETSETTINGS"). Hopflow does
  not use it: RELOAD_DRIVER and SETDISPLAYCOUNT re-initialise the adapter
  without re-reading the resolutions, and look up the device context from the
  pipe handle instead of the WDF device, so they cannot add a mode.
*/
namespace hopflow::stream::vdd {

inline constexpr auto kReleasesUrl = "https://github.com/VirtualDrivers/Virtual-Display-Driver/releases";

struct Mode
{
  int width = 0;
  int height = 0;
  int refreshHz = 0;

  bool operator==(const Mode &) const = default;
};

struct Settings
{
  int monitorCount = 1;
  //! Every mode offered, explicit ones first, then each resolution at each global rate
  QList<Mode> modes;
};

//! Read vdd_settings.xml the way the driver does; nullopt when it is not well formed
std::optional<Settings> parseSettings(const QString &xml);

//! \p xml changed as little as possible so the driver offers \p mode; nullopt when it cannot be read
/*!
Empty \p xml, a missing file, gives a minimal settings file with one monitor.
*/
std::optional<QString> settingsOffering(const QString &xml, const Mode &mode);

bool isDriverHardwareId(const QString &hardwareId);

//! A GDI display source (\\.\DISPLAYn) and the monitor connected to it, if any
struct DisplaySource
{
  QString deviceName;
  //! PnP ID of the adapter driving the source
  QString adapterId;
  //! Device interface of the connected monitor, empty when none is
  QString monitorId;
  bool attached = false;
  bool primary = false;
  //! Desktop position and size, when attached
  QRect bounds;
  int refreshHz = 0;
};

//! The driver's source with a monitor, preferring one that is not on the desktop yet
std::optional<qsizetype> findDriverSource(const QList<DisplaySource> &sources);

//! A route QueryDisplayConfig(QDC_ALL_PATHS) offers from a display source to a monitor
struct DisplayPath
{
  //! GDI name of the source, \\.\DISPLAYn
  QString sourceName;
  //! Device path of the monitor
  QString monitorPath;
  //! The route is in use on the desktop
  bool active = false;
  //! The monitor is connected and could be shown
  bool targetAvailable = false;
};

//! The source to show the driver's monitor on, for when no source in \p sources lists that monitor
/*!
Windows offers a route from every source of an adapter to each of its
monitors. Like findDriverSource() this prefers a monitor not on the desktop
yet, taking a source no route in use has; a monitor already shown keeps its
source.
The source comes from \p sources when it is listed there, with just its name
otherwise.
*/
std::optional<DisplaySource>
findDriverSourceByPath(const QList<DisplaySource> &sources, const QList<DisplayPath> &paths);

//! Where a display goes to sit right of the desktop, level with the primary display's top
QPoint besideDesktop(const QList<DisplaySource> &sources, const QString &deviceName);

} // namespace hopflow::stream::vdd
