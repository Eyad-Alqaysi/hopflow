/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/MediaInterfaces.h"

#include <memory>

namespace hopflow::stream {

//! A display added with CoreGraphics' private CGVirtualDisplay, which macOS 11 and later have
/*!
Create and destroy it on the main thread.
*/
class MacVirtualDisplay : public IVirtualDisplay
{
public:
  MacVirtualDisplay();
  ~MacVirtualDisplay() override;
  MacVirtualDisplay(const MacVirtualDisplay &) = delete;
  MacVirtualDisplay &operator=(const MacVirtualDisplay &) = delete;

  bool create(const DisplayInfo &info) override;
  QString displayId() const override;
  void destroy() override;
  QString error() const override;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace hopflow::stream
