/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/WinAudioDevice.h"

#include <QDebug>

#include <mmdeviceapi.h>

#include <atomic>

namespace hopflow::stream::win {

namespace {

EDataFlow flowOf(DefaultDevice which)
{
  return which == DefaultDevice::Output ? eRender : eCapture;
}

ERole roleOf(DefaultDevice which)
{
  return which == DefaultDevice::Output ? eConsole : eCommunications;
}

} // namespace

//! Sets an event when Windows makes another device the default, for as long as it is registered
/*!
Windows calls it on a thread of its own, so it does no more than set the event.
*/
class DefaultDeviceClient final : public IMMNotificationClient
{
public:
  DefaultDeviceClient(IMMDeviceEnumerator *enumerator, DefaultDevice which)
      : m_enumerator(enumerator),
        m_flow(flowOf(which)),
        m_role(roleOf(which)),
        m_changed(CreateEventW(nullptr, FALSE, FALSE, nullptr))
  {
  }

  DefaultDeviceClient(const DefaultDeviceClient &) = delete;
  DefaultDeviceClient &operator=(const DefaultDeviceClient &) = delete;

  bool registerClient()
  {
    return m_enumerator && m_changed != nullptr && SUCCEEDED(m_enumerator->RegisterEndpointNotificationCallback(this));
  }

  void unregisterClient()
  {
    m_enumerator->UnregisterEndpointNotificationCallback(this);
  }

  HANDLE changed() const
  {
    return m_changed;
  }

  ULONG STDMETHODCALLTYPE AddRef() override
  {
    return ++m_references;
  }

  ULONG STDMETHODCALLTYPE Release() override
  {
    const ULONG references = --m_references;
    if (references == 0) {
      delete this;
    }
    return references;
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override
  {
    if (object == nullptr) {
      return E_POINTER;
    }
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
      *object = static_cast<IMMNotificationClient *>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override
  {
    if (flow == m_flow && role == m_role) {
      SetEvent(m_changed);
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override
  {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override
  {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override
  {
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override
  {
    return S_OK;
  }

private:
  ~DefaultDeviceClient()
  {
    if (m_changed != nullptr) {
      CloseHandle(m_changed);
    }
  }

  ComPtr<IMMDeviceEnumerator> m_enumerator;
  const EDataFlow m_flow;
  const ERole m_role;
  const HANDLE m_changed;
  std::atomic<ULONG> m_references{1};
};

ComPtr<IMMDeviceEnumerator> deviceEnumerator()
{
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    return nullptr;
  }
  return enumerator;
}

ComPtr<IMMDevice> defaultDevice(IMMDeviceEnumerator *enumerator, DefaultDevice which)
{
  ComPtr<IMMDevice> device;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(flowOf(which), roleOf(which), &device))) {
    return nullptr;
  }
  return device;
}

QString endpointId(IMMDevice *device)
{
  LPWSTR id = nullptr;
  if (FAILED(device->GetId(&id))) {
    return {};
  }
  const auto result = QString::fromWCharArray(id);
  CoTaskMemFree(id);
  return result;
}

DefaultDeviceWatch::DefaultDeviceWatch(IMMDeviceEnumerator *enumerator, DefaultDevice which)
    : m_client(new DefaultDeviceClient(enumerator, which))
{
  if (!m_client->registerClient()) {
    qWarning().noquote() << "cannot follow changes of the default sound device";
    m_client->Release();
    m_client = nullptr;
  }
}

DefaultDeviceWatch::~DefaultDeviceWatch()
{
  if (m_client != nullptr) {
    m_client->unregisterClient();
    m_client->Release();
  }
}

HANDLE DefaultDeviceWatch::changed() const
{
  return m_client != nullptr ? m_client->changed() : nullptr;
}

AudioWake waitForAudio(HANDLE ready, const DefaultDeviceWatch &watch, DWORD timeoutMs)
{
  const HANDLE events[] = {ready, watch.changed()};
  const DWORD count = events[1] != nullptr ? 2 : 1;
  switch (WaitForMultipleObjects(count, events, FALSE, timeoutMs)) {
  case WAIT_OBJECT_0:
    return AudioWake::Ready;
  case WAIT_OBJECT_0 + 1:
    return AudioWake::DefaultChanged;
  default:
    return AudioWake::TimedOut;
  }
}

} // namespace hopflow::stream::win
