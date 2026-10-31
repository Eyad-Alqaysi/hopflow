/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/WinAudio.h"

#include "stream/AudioBuffer.h"
#include "stream/MutedSpeakers.h"
#include "stream/WinMedia.h"

#include <QDebug>

#include <audioclient.h>
#include <endpointvolume.h>
#include <ksmedia.h>
#include <mferror.h>
#include <mmdeviceapi.h>
#include <wmcodecdsp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
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

ComPtr<IMMDevice> defaultRenderDevice()
{
  auto enumerator = deviceEnumerator();
  ComPtr<IMMDevice> device;
  if (!enumerator || FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device))) {
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

//! Unmute the output devices a crashed run left muted
void unmuteLeftovers()
{
  MutedSpeakers record;
  const auto devices = record.devices();
  if (devices.isEmpty()) {
    return;
  }
  auto enumerator = deviceEnumerator();
  if (!enumerator) {
    return;
  }
  for (const auto &id : devices) {
    const auto wideId = id.toStdWString();
    ComPtr<IMMDevice> device;
    ComPtr<IAudioEndpointVolume> volume;
    // a device that cannot be reached now keeps its record, to be unmuted another time
    if (FAILED(enumerator->GetDevice(wideId.c_str(), &device)) ||
        FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &volume)) ||
        FAILED(volume->SetMute(FALSE, nullptr))) {
      continue;
    }
    record.remove(id);
    qInfo().noquote() << "unmuted a sound output Hopflow had muted before it stopped unexpectedly";
  }
}

ComPtr<IAudioClient> defaultAudioClient()
{
  auto device = defaultRenderDevice();
  ComPtr<IAudioClient> client;
  if (!device || FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) {
    return nullptr;
  }
  return client;
}

//! Mutes the default output while it exists, then unmutes it unless it was muted already
/*!
The output is noted in MutedSpeakers before it is muted, so that if Hopflow
crashes unmuteLeftovers() unmutes it the next time.
*/
class EndpointMute
{
public:
  EndpointMute()
  {
    auto device = defaultRenderDevice();
    if (device) {
      m_id = endpointId(device.Get());
    }
    BOOL wasMuted = FALSE;
    if (m_id.isEmpty() || FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &m_volume)) ||
        FAILED(m_volume->GetMute(&wasMuted))) {
      m_volume.Reset();
      return;
    }
    // already quiet, so there is nothing to do or undo
    if (wasMuted != FALSE) {
      return;
    }
    if (!MutedSpeakers().add(m_id)) {
      qWarning().noquote() << "cannot note that the sound output is muted, so it is left as it is";
      m_volume.Reset();
      return;
    }
    if (FAILED(m_volume->SetMute(TRUE, nullptr))) {
      MutedSpeakers().remove(m_id);
      m_volume.Reset();
      return;
    }
    m_muted = true;
  }

  ~EndpointMute()
  {
    if (!m_muted) {
      return;
    }
    if (const HRESULT hr = m_volume->SetMute(FALSE, nullptr); FAILED(hr)) {
      // the record stays, so the output is unmuted the next time Hopflow can reach it
      qWarning().noquote() << hresultText("unmuting the sound output", hr);
      return;
    }
    MutedSpeakers().remove(m_id);
  }

  EndpointMute(const EndpointMute &) = delete;
  EndpointMute &operator=(const EndpointMute &) = delete;

  bool isActive() const
  {
    return m_volume != nullptr;
  }

private:
  QString m_id;
  ComPtr<IAudioEndpointVolume> m_volume;
  //! Hopflow muted the output, so it unmutes it
  bool m_muted = false;
};

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

//! Microsoft's AAC encoder taking 16-bit PCM, or null with \p error set
ComPtr<IMFTransform> aacEncoder(QString &error)
{
  ComPtr<IMFTransform> encoder;
  if (FAILED(CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&encoder)))) {
    error = QStringLiteral("cannot start the sound encoder");
    return nullptr;
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
    error = QStringLiteral("the sound encoder rejected the format");
    return nullptr;
  }
  return encoder;
}

} // namespace

//
// WinSystemAudioSource
//

struct WinSystemAudioSource::Impl
{
  Impl(AudioCodec codec, bool muteLocal) : codec(codec), muteLocal(muteLocal)
  {
  }

  const AudioCodec codec;
  const bool muteLocal;
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

  // AAC encoder: 16-bit PCM in, raw AAC packets out; low latency sound is sent as it is
  ComPtr<IMFTransform> encoder;
  DWORD outputSize = 0;
  bool providesSamples = false;
  if (codec == AudioCodec::Aac) {
    QString error;
    encoder = aacEncoder(error);
    if (!encoder) {
      fail(error);
      return;
    }
    MFT_OUTPUT_STREAM_INFO info{};
    encoder->GetOutputStreamInfo(0, &info);
    outputSize = std::max<DWORD>(info.cbSize, 4096);
    providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
    encoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  }

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

  // the speakers go quiet, but loopback still hears the mix; unmuted again when this returns
  std::optional<EndpointMute> mute;
  if (muteLocal) {
    unmuteLeftovers();
    mute.emplace();
    if (!mute->isActive()) {
      qWarning().noquote() << "cannot mute this PC while sending its sound";
    }
  }

  int64_t capturedFrames = 0;
  Pcm16Packetizer packetizer;
  std::vector<float> captured;
  std::vector<int16_t> pcm;
  while (running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    captured.clear();
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
        captured.push_back((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 ? 0.0f : samples[i]);
      }
      capture->ReleaseBuffer(frames);
    }
    if (captured.empty()) {
      continue;
    }

    if (codec == AudioCodec::Pcm16) {
      const auto packets = packetizer.pack(captured.data(), uint32_t(captured.size() / kAudioChannels));
      std::scoped_lock lock(callbackMutex);
      if (running) {
        for (const auto &packet : packets) {
          onPacket(packet);
        }
      }
      continue;
    }

    pcm.clear();
    for (const float value : captured) {
      pcm.push_back(int16_t(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
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

WinSystemAudioSource::WinSystemAudioSource(AudioCodec codec, bool muteLocal)
    : m_impl(std::make_unique<Impl>(codec, muteLocal))
{
}

void WinSystemAudioSource::restoreMutedSpeakers()
{
  // on a thread of its own, so COM starts there as the audio code expects, whatever the caller's thread uses
  std::thread([] {
    MediaFoundationScope com;
    unmuteLeftovers();
  }).join();
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
  onConfig(audioConfigFor(m_impl->codec));
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
  explicit Impl(AudioLatency latency)
      : buffer(kAudioChannels, jitterDepth(latency)),
        deviceBuffer(latency == AudioLatency::Low ? kHundredNsPerSecond / 50 : kHundredNsPerSecond / 20)
  {
  }

  PcmJitterBuffer buffer;
  //! How far ahead the sound card is fed
  const REFERENCE_TIME deviceBuffer;
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
            deviceBuffer, 0, &format.Format, nullptr
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

WinAudioSink::WinAudioSink(AudioLatency latency) : m_impl(std::make_unique<Impl>(latency))
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
