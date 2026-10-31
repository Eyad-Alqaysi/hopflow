/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// built with ARC (see CMakeLists.txt)

#include "stream/MacVirtualDisplay.h"

#include "stream/VirtualDisplayMode.h"

#import "stream/CGVirtualDisplayPrivate.h"

#include <QDebug>

namespace {

// fixed, so macOS remembers where the user arranged the display last time
constexpr unsigned int kVendorId = 0x484f;  // "HO"
constexpr unsigned int kProductId = 0x5046; // "PF"
constexpr unsigned int kSerialNumber = 1;

} // namespace

namespace hopflow::stream {

struct MacVirtualDisplay::Impl
{
  CGVirtualDisplay *display = nil;
  QString error;
};

MacVirtualDisplay::MacVirtualDisplay() : m_impl(std::make_unique<Impl>())
{
}

MacVirtualDisplay::~MacVirtualDisplay()
{
  destroy();
}

bool MacVirtualDisplay::create(const DisplayInfo &info)
{
  destroy();
  m_impl->error.clear();

  Class displayClass = NSClassFromString(@"CGVirtualDisplay");
  Class descriptorClass = NSClassFromString(@"CGVirtualDisplayDescriptor");
  Class modeClass = NSClassFromString(@"CGVirtualDisplayMode");
  Class settingsClass = NSClassFromString(@"CGVirtualDisplaySettings");
  if (displayClass == nil || descriptorClass == nil || modeClass == nil || settingsClass == nil) {
    m_impl->error = QStringLiteral("this version of macOS cannot create virtual displays");
    return false;
  }

  const auto mode = virtualDisplayMode(info);

  CGVirtualDisplayDescriptor *descriptor = [[descriptorClass alloc] init];
  descriptor.queue = dispatch_get_main_queue();
  descriptor.name = @"Hopflow Display";
  descriptor.maxPixelsWide = static_cast<unsigned int>(mode.pixels.width());
  descriptor.maxPixelsHigh = static_cast<unsigned int>(mode.pixels.height());
  descriptor.sizeInMillimeters = CGSizeMake(mode.millimeters.width(), mode.millimeters.height());
  descriptor.vendorID = kVendorId;
  descriptor.productID = kProductId;
  descriptor.serialNum = kSerialNumber;
  descriptor.terminationHandler = ^(id, CGVirtualDisplay *display) {
    qWarning().noquote() << "macOS removed virtual display" << display.displayID;
  };

  CGVirtualDisplay *display = [[displayClass alloc] initWithDescriptor:descriptor];
  if (display == nil || display.displayID == kCGNullDirectDisplay) {
    m_impl->error = QStringLiteral("macOS refused to create a virtual display");
    return false;
  }

  CGVirtualDisplaySettings *settings = [[settingsClass alloc] init];
  settings.hiDPI = mode.hiDpi ? 1 : 0;
  settings.modes = @[ [[modeClass alloc] initWithWidth:NSUInteger(mode.logical.width())
                                                height:NSUInteger(mode.logical.height())
                                           refreshRate:mode.refreshHz] ];
  if (![display applySettings:settings]) {
    m_impl->error = QStringLiteral("macOS refused a %1×%2 display at %3 Hz")
                        .arg(mode.pixels.width())
                        .arg(mode.pixels.height())
                        .arg(mode.refreshHz);
    return false;
  }

  m_impl->display = display;
  qInfo().noquote() << "created virtual display" << display.displayID << mode.pixels << "at" << mode.refreshHz << "Hz"
                    << (mode.hiDpi ? "HiDPI" : "");
  return true;
}

QString MacVirtualDisplay::displayId() const
{
  return m_impl->display ? QString::number(m_impl->display.displayID) : QString();
}

void MacVirtualDisplay::destroy()
{
  // releasing the last reference removes the display from the desktop
  m_impl->display = nil;
}

QString MacVirtualDisplay::error() const
{
  return m_impl->error;
}

} // namespace hopflow::stream
