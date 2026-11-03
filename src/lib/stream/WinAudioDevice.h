/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/WinMedia.h"

// declared rather than included, so that files defining property keys with initguid.h can
// include this before mmdeviceapi.h
struct IMMDevice;
struct IMMDeviceEnumerator;

//! Finding the default Windows audio devices, and following them as the user changes them
namespace hopflow::stream::win {

enum class DefaultDevice
{
  //! The output sound plays on
  Output,
  //! The microphone for calls
  Microphone
};

ComPtr<IMMDeviceEnumerator> deviceEnumerator();
//! The current default device, or null when there is none
ComPtr<IMMDevice> defaultDevice(IMMDeviceEnumerator *enumerator, DefaultDevice which);
//! The endpoint ID of \p device, or empty when Windows does not give it
QString endpointId(IMMDevice *device);

class DefaultDeviceClient;

//! Signals changed() each time Windows makes another device the default
class DefaultDeviceWatch
{
public:
  DefaultDeviceWatch(IMMDeviceEnumerator *enumerator, DefaultDevice which);
  DefaultDeviceWatch(const DefaultDeviceWatch &) = delete;
  DefaultDeviceWatch &operator=(const DefaultDeviceWatch &) = delete;
  ~DefaultDeviceWatch();

  //! An auto-reset event, or null when changes cannot be followed
  HANDLE changed() const;

private:
  DefaultDeviceClient *m_client = nullptr;
};

enum class AudioWake
{
  //! The stream's event is set: there is sound to read, or room to write it
  Ready,
  DefaultChanged,
  TimedOut
};

//! Wait for \p ready, or for \p watch to see another default device
AudioWake waitForAudio(HANDLE ready, const DefaultDeviceWatch &watch, DWORD timeoutMs);

} // namespace hopflow::stream::win
