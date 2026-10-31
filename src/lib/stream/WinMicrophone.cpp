/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/WinMicrophone.h"

#include "stream/AudioBuffer.h"
#include "stream/Microphone.h"
#include "stream/WinMedia.h"

// defines PKEY_Device_FriendlyName here rather than expecting a library to
#include <initguid.h>

#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace hopflow::stream::win;

namespace hopflow::stream {

namespace {

constexpr REFERENCE_TIME kHundredNsPerSecond = 10'000'000;

//! 48 kHz stereo floats; Windows converts to and from the device's own format
WAVEFORMATEXTENSIBLE floatFormat()
{
  WAVEFORMATEXTENSIBLE format{};
  format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  format.Format.nChannels = WORD(kAudioChannels);
  format.Format.nSamplesPerSec = kAudioSampleRate;
  format.Format.wBitsPerSample = 32;
  format.Format.nBlockAlign = WORD(kAudioChannels * sizeof(float));
  format.Format.nAvgBytesPerSec = kAudioSampleRate * format.Format.nBlockAlign;
  format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
  format.Samples.wValidBitsPerSample = 32;
  format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
  format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
  return format;
}

ComPtr<IMMDeviceEnumerator> deviceEnumerator()
{
  ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
    return nullptr;
  }
  return enumerator;
}

QString friendlyName(IMMDevice *device)
{
  ComPtr<IPropertyStore> properties;
  if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) {
    return {};
  }
  PROPVARIANT value;
  PropVariantInit(&value);
  QString name;
  if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR) {
    name = QString::fromWCharArray(value.pwszVal);
  }
  PropVariantClear(&value);
  return name;
}

} // namespace

//
// WinMicrophoneSource
//

struct WinMicrophoneSource::Impl
{
  std::thread worker;
  std::atomic<bool> running{false};
  std::mutex callbackMutex;
  PacketReady onPacket;
  Failed onFailed;

  void fail(const QString &message)
  {
    std::scoped_lock lock(callbackMutex);
    if (running.exchange(false) && onFailed) {
      onFailed(message);
    }
  }

  void run();
};

void WinMicrophoneSource::Impl::run()
{
  MediaFoundationScope com;

  auto enumerator = deviceEnumerator();
  ComPtr<IMMDevice> device;
  if (!enumerator || FAILED(enumerator->GetDefaultAudioEndpoint(eCapture, eCommunications, &device))) {
    fail(QStringLiteral("this PC has no microphone to send"));
    return;
  }
  ComPtr<IAudioClient> client;
  const HRESULT activated = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
  if (activated == E_ACCESSDENIED) {
    fail(QStringLiteral(
        "Windows does not let Hopflow use the microphone: allow desktop apps in Settings, Privacy & security, "
        "Microphone"
    ));
    return;
  }

  // event driven capture, converted by Windows to 48 kHz stereo floats
  auto format = floatFormat();
  HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  ComPtr<IAudioCaptureClient> capture;
  if (FAILED(activated) ||
      FAILED(client->Initialize(
          AUDCLNT_SHAREMODE_SHARED,
          AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
              AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
          kHundredNsPerSecond / 50, 0, &format.Format, nullptr
      )) ||
      FAILED(client->SetEventHandle(ready)) || FAILED(client->GetService(IID_PPV_ARGS(&capture))) ||
      FAILED(client->Start())) {
    CloseHandle(ready);
    fail(QStringLiteral("cannot open the microphone on this PC"));
    return;
  }

  int64_t capturedFrames = 0;
  std::vector<float> samples;
  while (running) {
    if (WaitForSingleObject(ready, 200) != WAIT_OBJECT_0) {
      continue;
    }

    samples.clear();
    UINT32 packetFrames = 0;
    HRESULT hr = S_OK;
    while (SUCCEEDED(hr = capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
      BYTE *data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
        break;
      }
      const auto count = size_t(frames) * kAudioChannels;
      if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
        samples.insert(samples.end(), count, 0.0f);
      } else {
        const auto *floats = reinterpret_cast<const float *>(data);
        samples.insert(samples.end(), floats, floats + count);
      }
      capture->ReleaseBuffer(frames);
    }
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
      fail(QStringLiteral("the microphone was disconnected"));
      break;
    }
    if (samples.empty()) {
      continue;
    }

    const auto timestamp = capturedFrames * 1'000'000 / kAudioSampleRate;
    capturedFrames += int64_t(samples.size() / kAudioChannels);
    const auto packet = floatToPcm16(samples.data(), samples.size());
    std::scoped_lock lock(callbackMutex);
    if (running) {
      onPacket(EncodedAudio{timestamp, packet});
    }
  }
  client->Stop();
  CloseHandle(ready);
}

WinMicrophoneSource::WinMicrophoneSource() : m_impl(std::make_unique<Impl>())
{
}

WinMicrophoneSource::~WinMicrophoneSource()
{
  stop();
}

void WinMicrophoneSource::start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed)
{
  stop();
  m_impl->onPacket = std::move(onPacket);
  m_impl->onFailed = std::move(onFailed);
  m_impl->running = true;
  onConfig(microphoneConfig());
  m_impl->worker = std::thread([impl = m_impl.get()] { impl->run(); });
}

void WinMicrophoneSource::stop()
{
  {
    std::scoped_lock lock(m_impl->callbackMutex);
    m_impl->running = false;
  }
  if (m_impl->worker.joinable()) {
    m_impl->worker.join();
  }
}

//
// WinVirtualMicSink
//

struct WinVirtualMicSink::Impl
{
  // a microphone has to keep up with the conversation: start at 20 ms, never more than 80 ms
  PcmJitterBuffer buffer{kAudioChannels, kAudioSampleRate / 50, kAudioSampleRate * 2 / 25};
  std::wstring deviceId;
  std::thread renderer;
  std::atomic<bool> running{false};

  ~Impl()
  {
    running = false;
    if (renderer.joinable()) {
      renderer.join();
    }
  }

  void render()
  {
    MediaFoundationScope com;
    auto enumerator = deviceEnumerator();
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    if (!enumerator || FAILED(enumerator->GetDevice(deviceId.c_str(), &device)) ||
        FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) {
      return;
    }

    auto format = floatFormat();
    HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioRenderClient> output;
    UINT32 bufferFrames = 0;
    if (FAILED(client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            kHundredNsPerSecond / 50, 0, &format.Format, nullptr
        )) ||
        FAILED(client->SetEventHandle(ready)) || FAILED(client->GetService(IID_PPV_ARGS(&output))) ||
        FAILED(client->GetBufferSize(&bufferFrames)) || FAILED(client->Start())) {
      CloseHandle(ready);
      return;
    }

    while (running) {
      if (WaitForSingleObject(ready, 200) != WAIT_OBJECT_0) {
        continue;
      }
      UINT32 padding = 0;
      if (FAILED(client->GetCurrentPadding(&padding))) {
        break;
      }
      const UINT32 frames = bufferFrames - padding;
      BYTE *data = nullptr;
      if (frames == 0 || FAILED(output->GetBuffer(frames, &data))) {
        continue;
      }
      buffer.pull(reinterpret_cast<float *>(data), frames);
      output->ReleaseBuffer(frames, 0);
    }
    client->Stop();
    CloseHandle(ready);
  }
};

WinVirtualMicSink::WinVirtualMicSink(const QString &deviceId) : m_impl(std::make_unique<Impl>())
{
  m_impl->deviceId = deviceId.toStdWString();
}

WinVirtualMicSink::~WinVirtualMicSink() = default;

QString WinVirtualMicSink::findDevice()
{
  MediaFoundationScope com;
  auto enumerator = deviceEnumerator();
  ComPtr<IMMDeviceCollection> devices;
  UINT count = 0;
  if (!enumerator || FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)) ||
      FAILED(devices->GetCount(&count))) {
    return {};
  }

  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    LPWSTR id = nullptr;
    if (FAILED(devices->Item(i, &device)) || !isVirtualCableInput(friendlyName(device.Get())) ||
        FAILED(device->GetId(&id))) {
      continue;
    }
    const auto result = QString::fromWCharArray(id);
    CoTaskMemFree(id);
    return result;
  }
  return {};
}

bool WinVirtualMicSink::configure(const AudioConfig &config)
{
  // microphones always send PCM; see createMicrophoneSource()
  if (config.codec != AudioCodec::Pcm16 || config.sampleRate != kAudioSampleRate || config.channels != kAudioChannels) {
    return false;
  }
  m_impl->running = true;
  m_impl->renderer = std::thread([impl = m_impl.get()] { impl->render(); });
  return true;
}

void WinVirtualMicSink::play(const EncodedAudio &packet)
{
  const auto samples = pcm16ToFloat(packet.data);
  m_impl->buffer.push(samples.data(), uint32_t(samples.size() / kAudioChannels));
}

int64_t WinVirtualMicSink::positionUs() const
{
  return -1;
}

} // namespace hopflow::stream
