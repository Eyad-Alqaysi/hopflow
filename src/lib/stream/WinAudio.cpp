/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/WinAudio.h"

#include "stream/AudioBuffer.h"
#include "stream/WinMedia.h"

#include <audioclient.h>
#include <ksmedia.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <wmcodecdsp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
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

ComPtr<IAudioClient> defaultAudioClient()
{
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> device;
  ComPtr<IAudioClient> client;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) ||
      FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) ||
      FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) {
    return nullptr;
  }
  return client;
}

ComPtr<IMFMediaType> audioType(const GUID &subtype, UINT32 bitsPerSample)
{
  ComPtr<IMFMediaType> type;
  if (FAILED(MFCreateMediaType(&type))) {
    return nullptr;
  }
  type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
  type->SetGUID(MF_MT_SUBTYPE, subtype);
  type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kAudioSampleRate);
  type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, kAudioChannels);
  type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample);
  return type;
}

//! The user data the Microsoft AAC decoder expects: the tail of HEAACWAVEINFO, then the AudioSpecificConfig
QByteArray aacUserData(const QByteArray &audioSpecificConfig)
{
  QByteArray data(12, '\0'); // raw AAC payload, profile level unspecified
  data[2] = char(0xfe);
  data.append(audioSpecificConfig);
  return data;
}

} // namespace

//
// WinSystemAudioSource
//

struct WinSystemAudioSource::Impl
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

void WinSystemAudioSource::Impl::run()
{
  MediaFoundationScope mediaFoundation;

  // AAC encoder: 16-bit PCM in, raw AAC packets out
  ComPtr<IMFTransform> encoder;
  if (FAILED(CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&encoder)))) {
    fail(QStringLiteral("cannot start the sound encoder"));
    return;
  }
  auto pcmType = audioType(MFAudioFormat_PCM, 16);
  pcmType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, kAudioChannels * 2);
  pcmType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAudioSampleRate * kAudioChannels * 2);
  auto aacType = audioType(MFAudioFormat_AAC, 16);
  aacType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kAacBitrate / 8);
  aacType->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
  // Windows versions differ on which type the encoder wants first
  const bool inputFirst =
      SUCCEEDED(encoder->SetInputType(0, pcmType.Get(), 0)) && SUCCEEDED(encoder->SetOutputType(0, aacType.Get(), 0));
  if (!inputFirst &&
      (FAILED(encoder->SetOutputType(0, aacType.Get(), 0)) || FAILED(encoder->SetInputType(0, pcmType.Get(), 0)))) {
    fail(QStringLiteral("the sound encoder rejected the format"));
    return;
  }
  MFT_OUTPUT_STREAM_INFO info{};
  encoder->GetOutputStreamInfo(0, &info);
  const DWORD outputSize = std::max<DWORD>(info.cbSize, 4096);
  const bool providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
  encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);

  // loopback capture of the default output, converted by Windows to 48 kHz stereo floats
  auto client = defaultAudioClient();
  auto format = floatFormat();
  ComPtr<IAudioCaptureClient> capture;
  if (!client ||
      FAILED(client->Initialize(
          AUDCLNT_SHAREMODE_SHARED,
          AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
          kHundredNsPerSecond / 5, 0, &format.Format, nullptr
      )) ||
      FAILED(client->GetService(IID_PPV_ARGS(&capture))) || FAILED(client->Start())) {
    fail(QStringLiteral("cannot capture the sound playing on this PC"));
    return;
  }

  int64_t capturedFrames = 0;
  std::vector<int16_t> pcm;
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    pcm.clear();
    UINT32 packetFrames = 0;
    while (SUCCEEDED(capture->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
      BYTE *data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
        break;
      }
      const auto *samples = reinterpret_cast<const float *>(data);
      for (size_t i = 0; i < size_t(frames) * kAudioChannels; ++i) {
        const float value = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 ? 0.0f : samples[i];
        pcm.push_back(int16_t(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
      }
      capture->ReleaseBuffer(frames);
    }
    if (pcm.empty()) {
      continue;
    }

    const QByteArray bytes(reinterpret_cast<const char *>(pcm.data()), qsizetype(pcm.size() * sizeof(int16_t)));
    const auto timestamp = capturedFrames * 1'000'000 / kAudioSampleRate;
    capturedFrames += int64_t(pcm.size() / kAudioChannels);
    auto sample = sampleWithBytes(bytes, timestamp);
    if (!sample || FAILED(encoder->ProcessInput(0, sample.Get(), 0))) {
      continue;
    }

    drainTransform(
        encoder.Get(), outputSize, providesSamples,
        [this](IMFSample *encoded) {
          LONGLONG time = 0;
          encoded->GetSampleTime(&time);
          const auto packet = sampleBytes(encoded);
          std::scoped_lock lock(callbackMutex);
          if (running && !packet.isEmpty()) {
            onPacket(EncodedAudio{time / kHundredNsPerUs, packet});
          }
        },
        [] { return false; }
    );
  }
  client->Stop();
}

WinSystemAudioSource::WinSystemAudioSource() : m_impl(std::make_unique<Impl>())
{
}

WinSystemAudioSource::~WinSystemAudioSource()
{
  stop();
}

void WinSystemAudioSource::start(ConfigReady onConfig, PacketReady onPacket, Failed onFailed)
{
  stop();
  m_impl->onPacket = std::move(onPacket);
  m_impl->onFailed = std::move(onFailed);
  m_impl->running = true;
  onConfig(
      AudioConfig{
          AudioCodec::Aac, kAudioSampleRate, kAudioChannels, aacAudioSpecificConfig(kAudioSampleRate, kAudioChannels)
      }
  );
  m_impl->worker = std::thread([impl = m_impl.get()] { impl->run(); });
}

void WinSystemAudioSource::stop()
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
// WinAudioSink
//

struct WinAudioSink::Impl
{
  // start once 50 ms are buffered, never let more than 250 ms build up
  PcmJitterBuffer buffer{kAudioChannels, kAudioSampleRate / 20, kAudioSampleRate / 4};
  std::unique_ptr<MediaFoundationScope> mediaFoundation;
  ComPtr<IMFTransform> decoder;
  DWORD outputSize = 0;
  bool providesSamples = false;
  bool floatOutput = true;
  bool pcm = false;
  std::thread renderer;
  std::atomic<bool> running{false};

  ~Impl()
  {
    running = false;
    if (renderer.joinable()) {
      renderer.join();
    }
  }

  bool chooseOutput()
  {
    for (DWORD i = 0;; ++i) {
      ComPtr<IMFMediaType> type;
      if (FAILED(decoder->GetOutputAvailableType(0, i, &type))) {
        return false;
      }
      GUID subtype{};
      type->GetGUID(MF_MT_SUBTYPE, &subtype);
      if ((subtype == MFAudioFormat_Float || subtype == MFAudioFormat_PCM) &&
          SUCCEEDED(decoder->SetOutputType(0, type.Get(), 0))) {
        floatOutput = subtype == MFAudioFormat_Float;
        MFT_OUTPUT_STREAM_INFO info{};
        decoder->GetOutputStreamInfo(0, &info);
        outputSize = std::max<DWORD>(info.cbSize, kAacFramesPerPacket * kAudioChannels * 4);
        providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
        return true;
      }
    }
  }

  void pushPcm16(const QByteArray &bytes)
  {
    const auto *pcm16 = reinterpret_cast<const int16_t *>(bytes.constData());
    std::vector<float> samples(size_t(bytes.size()) / sizeof(int16_t));
    for (size_t i = 0; i < samples.size(); ++i) {
      samples[i] = float(pcm16[i]) / 32768.0f;
    }
    buffer.push(samples.data(), uint32_t(samples.size() / kAudioChannels));
  }

  void render()
  {
    MediaFoundationScope com;
    auto client = defaultAudioClient();
    auto format = floatFormat();
    HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ComPtr<IAudioRenderClient> output;
    UINT32 bufferFrames = 0;
    if (!client ||
        FAILED(client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            kHundredNsPerSecond / 20, 0, &format.Format, nullptr
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

WinAudioSink::WinAudioSink() : m_impl(std::make_unique<Impl>())
{
  m_impl->mediaFoundation = std::make_unique<MediaFoundationScope>();
}

WinAudioSink::~WinAudioSink() = default;

bool WinAudioSink::configure(const AudioConfig &config)
{
  if (config.sampleRate != kAudioSampleRate || config.channels != kAudioChannels) {
    return false;
  }

  m_impl->pcm = config.codec == AudioCodec::Pcm16;
  if (!m_impl->pcm) {
    if (FAILED(CoCreateInstance(CLSID_MSAACDecMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_impl->decoder)))) {
      return false;
    }
    auto input = audioType(MFAudioFormat_AAC, 16);
    input->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);
    const auto userData = aacUserData(config.codecData);
    input->SetBlob(MF_MT_USER_DATA, reinterpret_cast<const UINT8 *>(userData.constData()), UINT32(userData.size()));
    if (FAILED(m_impl->decoder->SetInputType(0, input.Get(), 0)) || !m_impl->chooseOutput()) {
      m_impl->decoder.Reset();
      return false;
    }
    m_impl->decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  }

  m_impl->running = true;
  m_impl->renderer = std::thread([impl = m_impl.get()] { impl->render(); });
  return true;
}

void WinAudioSink::play(const EncodedAudio &packet)
{
  if (m_impl->pcm) {
    m_impl->pushPcm16(packet.data);
    return;
  }
  if (!m_impl->decoder) {
    return;
  }

  auto sample = sampleWithBytes(packet.data, packet.timestampUs);
  if (!sample || FAILED(m_impl->decoder->ProcessInput(0, sample.Get(), 0))) {
    return;
  }
  auto *impl = m_impl.get();
  drainTransform(
      impl->decoder.Get(), impl->outputSize, impl->providesSamples,
      [impl](IMFSample *decoded) {
        const auto bytes = sampleBytes(decoded);
        if (impl->floatOutput) {
          impl->buffer.push(
              reinterpret_cast<const float *>(bytes.constData()),
              uint32_t(size_t(bytes.size()) / (sizeof(float) * kAudioChannels))
          );
        } else {
          impl->pushPcm16(bytes);
        }
      },
      [impl] { return impl->chooseOutput(); }
  );
}

int64_t WinAudioSink::positionUs() const
{
  return -1;
}

} // namespace hopflow::stream
