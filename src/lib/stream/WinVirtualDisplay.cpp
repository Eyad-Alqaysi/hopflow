/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// defines GUID_DEVCLASS_DISPLAY, which has no import library
#include <initguid.h>

#include "stream/WinVirtualDisplay.h"

#include "stream/VirtualDisplayDriver.h"
#include "stream/WinMedia.h"

#include <windows.h>

#include <cfgmgr32.h>
#include <devguid.h>
#include <setupapi.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <chrono>
#include <cwchar>
#include <optional>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace hopflow::stream::win;

namespace hopflow::stream {

namespace {

// the driver restarts, then Windows sets the new monitor up
constexpr auto kMonitorTimeout = 15s;
constexpr auto kOutputTimeout = 5s;
constexpr int kDefaultRefreshHz = 60;

//! Poll \p ready until it holds or \p timeout passes
template <typename Ready> bool waitFor(std::chrono::milliseconds timeout, Ready ready)
{
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!ready()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      return false;
    }
    std::this_thread::sleep_for(100ms);
  }
  return true;
}

bool hasDriverHardwareId(HDEVINFO devices, SP_DEVINFO_DATA &device)
{
  DWORD size = 0;
  SetupDiGetDeviceRegistryPropertyW(devices, &device, SPDRP_HARDWAREID, nullptr, nullptr, 0, &size);
  if (size == 0) {
    return false;
  }
  // two spare zeros so the list always ends, even if the registry value does not
  std::vector<wchar_t> ids(size / sizeof(wchar_t) + 2, L'\0');
  if (!SetupDiGetDeviceRegistryPropertyW(
          devices, &device, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids.data()), size, nullptr
      )) {
    return false;
  }
  // REG_MULTI_SZ: strings one after another, then an empty one
  for (const wchar_t *id = ids.data(); *id != L'\0'; id += std::wcslen(id) + 1) {
    if (vdd::isDriverHardwareId(QString::fromWCharArray(id))) {
      return true;
    }
  }
  return false;
}

//! The driver's device node
class DriverDevice
{
public:
  DriverDevice() : m_devices(SetupDiGetClassDevsW(&GUID_DEVCLASS_DISPLAY, nullptr, nullptr, DIGCF_PRESENT))
  {
    if (m_devices == INVALID_HANDLE_VALUE) {
      return;
    }
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(m_devices, i, &device); ++i) {
      if (hasDriverHardwareId(m_devices, device)) {
        m_device = device;
        m_found = true;
        return;
      }
    }
  }

  DriverDevice(const DriverDevice &) = delete;
  DriverDevice &operator=(const DriverDevice &) = delete;

  ~DriverDevice()
  {
    if (m_devices != INVALID_HANDLE_VALUE) {
      SetupDiDestroyDeviceInfoList(m_devices);
    }
  }

  bool found() const
  {
    return m_found;
  }

  bool isDisabled() const
  {
    ULONG status = 0;
    ULONG problem = 0;
    return CM_Get_DevNode_Status(&status, &problem, m_device.DevInst, 0) == CR_SUCCESS &&
           (status & DN_HAS_PROBLEM) != 0 && problem == CM_PROB_DISABLED;
  }

  //! Turn the device on (DICS_ENABLE), off (DICS_DISABLE) or restart it (DICS_PROPCHANGE); a Windows error code
  DWORD change(DWORD stateChange)
  {
    SP_PROPCHANGE_PARAMS params{};
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    params.StateChange = stateChange;
    // as devcon does: a restart is for the current hardware profile, on and off for all of them
    params.Scope = stateChange == DICS_PROPCHANGE ? DICS_FLAG_CONFIGSPECIFIC : DICS_FLAG_GLOBAL;
    if (!SetupDiSetClassInstallParamsW(m_devices, &m_device, &params.ClassInstallHeader, sizeof(params)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, m_devices, &m_device)) {
      return GetLastError();
    }
    return ERROR_SUCCESS;
  }

private:
  HDEVINFO m_devices = INVALID_HANDLE_VALUE;
  SP_DEVINFO_DATA m_device{};
  bool m_found = false;
};

bool changeDevice(DriverDevice &device, DWORD stateChange, QString &error)
{
  const DWORD result = device.change(stateChange);
  if (result == ERROR_SUCCESS) {
    return true;
  }
  const char *action = stateChange == DICS_ENABLE    ? "turning on the Virtual Display Driver"
                       : stateChange == DICS_DISABLE ? "turning off the Virtual Display Driver"
                                                     : "restarting the Virtual Display Driver";
  error = result == ERROR_ACCESS_DENIED
              ? QStringLiteral("%1 needs administrator rights; run Hopflow as administrator once and try again")
                    .arg(QString::fromLatin1(action))
              : hresultText(action, HRESULT_FROM_WIN32(result));
  return false;
}

QString settingsPath()
{
  wchar_t folder[MAX_PATH]{};
  DWORD size = sizeof(folder);
  const LSTATUS read = RegGetValueW(
      HKEY_LOCAL_MACHINE, L"SOFTWARE\\MikeTheTech\\VirtualDisplayDriver", L"VDDPATH", RRF_RT_REG_SZ, nullptr, folder,
      &size
  );
  const auto dir = read == ERROR_SUCCESS ? QString::fromWCharArray(folder) : QStringLiteral("C:\\VirtualDisplayDriver");
  return QDir(dir).filePath(QStringLiteral("vdd_settings.xml"));
}

//! Make the driver's settings offer \p mode; whether they changed, or nullopt with the reason in \p error
std::optional<bool> offerMode(const vdd::Mode &mode, QString &error)
{
  const auto path = settingsPath();
  const auto shownPath = QDir::toNativeSeparators(path);
  QString current;
  if (QFile file(path); file.exists()) {
    if (!file.open(QIODevice::ReadOnly)) {
      error = QStringLiteral("cannot read the Virtual Display Driver settings in %1: %2")
                  .arg(shownPath, file.errorString());
      return std::nullopt;
    }
    current = QString::fromUtf8(file.readAll());
  }

  const auto updated = vdd::settingsOffering(current, mode);
  if (!updated) {
    error = QStringLiteral("the Virtual Display Driver settings in %1 are not valid XML").arg(shownPath);
    return std::nullopt;
  }
  if (*updated == current) {
    return false;
  }

  QSaveFile file(path);
  // the folder may only let administrators create files while the file itself is writable
  file.setDirectWriteFallback(true);
  if (!file.open(QIODevice::WriteOnly) || file.write(updated->toUtf8()) < 0 || !file.commit()) {
    error = QStringLiteral("cannot add %1x%2 at %3 Hz to the Virtual Display Driver settings in %4 (%5); "
                           "run Hopflow as administrator once, or add that resolution there yourself")
                .arg(mode.width)
                .arg(mode.height)
                .arg(mode.refreshHz)
                .arg(shownPath, file.errorString());
    return std::nullopt;
  }
  return true;
}

QList<vdd::DisplaySource> displaySources()
{
  QList<vdd::DisplaySource> sources;
  DISPLAY_DEVICEW adapter{};
  adapter.cb = sizeof(adapter);
  for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &adapter, 0); ++i) {
    vdd::DisplaySource source;
    source.deviceName = QString::fromWCharArray(adapter.DeviceName);
    source.adapterId = QString::fromWCharArray(adapter.DeviceID);
    source.attached = (adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0;
    source.primary = (adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) != 0;

    DISPLAY_DEVICEW monitor{};
    monitor.cb = sizeof(monitor);
    if (EnumDisplayDevicesW(adapter.DeviceName, 0, &monitor, EDD_GET_DEVICE_INTERFACE_NAME)) {
      source.monitorId = QString::fromWCharArray(monitor.DeviceID);
    }

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (source.attached && EnumDisplaySettingsW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &mode)) {
      source.bounds = QRect(mode.dmPosition.x, mode.dmPosition.y, int(mode.dmPelsWidth), int(mode.dmPelsHeight));
      source.refreshHz = int(mode.dmDisplayFrequency);
    }

    sources.append(source);
    adapter = {};
    adapter.cb = sizeof(adapter);
  }
  return sources;
}

//! Put \p deviceName at \p bounds, or take it off the desktop when \p bounds is empty
LONG applyMode(const QString &deviceName, const QRect &bounds, int refreshHz)
{
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  mode.dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
  mode.dmPosition = POINTL{bounds.x(), bounds.y()};
  mode.dmPelsWidth = DWORD(bounds.width());
  mode.dmPelsHeight = DWORD(bounds.height());
  if (refreshHz > 0) {
    mode.dmFields |= DM_DISPLAYFREQUENCY;
    mode.dmDisplayFrequency = DWORD(refreshHz);
  }

  // stage the change in the registry, then apply everything staged at once
  const auto name = deviceName.toStdWString();
  const LONG staged = ChangeDisplaySettingsExW(name.c_str(), &mode, nullptr, CDS_UPDATEREGISTRY | CDS_NORESET, nullptr);
  if (staged != DISP_CHANGE_SUCCESSFUL) {
    return staged;
  }
  return ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
}

bool isOnDesktop(const QString &deviceName)
{
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<IDXGIOutput> output;
  return findOutput(deviceName, adapter, output);
}

} // namespace

WinVirtualDisplay::~WinVirtualDisplay()
{
  destroy();
}

bool WinVirtualDisplay::create(const DisplayInfo &info)
{
  destroy();
  m_error.clear();
  if (info.width == 0 || info.height == 0) {
    return fail(QStringLiteral("the other computer did not say how large its display is"));
  }
  const vdd::Mode mode{info.width, info.height, info.refreshHz > 0 ? int(info.refreshHz) : kDefaultRefreshHz};

  DriverDevice device;
  if (!device.found()) {
    return fail(QStringLiteral("Using this computer's desktop on another screen needs the free Virtual Display Driver. "
                               "Install it from %1, then try again.")
                    .arg(QLatin1String(vdd::kReleasesUrl)));
  }

  QString error;
  const auto changed = offerMode(mode, error);
  if (!changed) {
    return fail(error);
  }
  // the driver reads its settings only as it starts
  if (device.isDisabled()) {
    if (!changeDevice(device, DICS_ENABLE, error)) {
      return fail(error);
    }
    m_enabledDevice = true;
  } else if (*changed && !changeDevice(device, DICS_PROPCHANGE, error)) {
    return fail(error);
  }

  std::optional<vdd::DisplaySource> source;
  const bool connected = waitFor(kMonitorTimeout, [&source] {
    const auto sources = displaySources();
    if (const auto index = vdd::findDriverSource(sources)) {
      source = sources[*index];
    }
    return source.has_value();
  });
  if (!connected) {
    destroy();
    return fail(QStringLiteral("the Virtual Display Driver did not connect a monitor; "
                               "check that its settings ask for at least one"));
  }

  m_displayId = source->deviceName;
  m_wasAttached = source->attached;
  m_previousBounds = source->bounds;
  m_previousRefreshHz = source->refreshHz;

  const auto position =
      source->primary ? source->bounds.topLeft() : vdd::besideDesktop(displaySources(), source->deviceName);
  if (const LONG result = applyMode(m_displayId, QRect(position, QSize(mode.width, mode.height)), mode.refreshHz);
      result != DISP_CHANGE_SUCCESSFUL) {
    destroy();
    return fail(QStringLiteral("Windows would not show the virtual display at %1x%2, %3 Hz (error %4)")
                    .arg(mode.width)
                    .arg(mode.height)
                    .arg(mode.refreshHz)
                    .arg(result));
  }

  if (!waitFor(kOutputTimeout, [this] { return isOnDesktop(m_displayId); })) {
    destroy();
    return fail(QStringLiteral("the virtual display did not join the desktop"));
  }
  return true;
}

QString WinVirtualDisplay::displayId() const
{
  return m_displayId;
}

void WinVirtualDisplay::destroy()
{
  if (!m_displayId.isEmpty()) {
    // as it was: off the desktop, or back in its old place and mode
    const LONG result =
        applyMode(m_displayId, m_wasAttached ? m_previousBounds : QRect(), m_wasAttached ? m_previousRefreshHz : 0);
    if (result != DISP_CHANGE_SUCCESSFUL) {
      qWarning().noquote() << "virtual display: could not put back" << m_displayId << "error" << result;
    }
    m_displayId.clear();
  }
  if (m_enabledDevice) {
    DriverDevice device;
    if (QString error; device.found() && !changeDevice(device, DICS_DISABLE, error)) {
      qWarning().noquote() << "virtual display:" << error;
    }
    m_enabledDevice = false;
  }
}

QString WinVirtualDisplay::error() const
{
  return m_error;
}

bool WinVirtualDisplay::fail(const QString &message)
{
  m_error = message;
  return false;
}

} // namespace hopflow::stream
