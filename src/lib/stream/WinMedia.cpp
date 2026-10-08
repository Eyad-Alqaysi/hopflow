/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/WinMedia.h"

#include <mferror.h>

#include <cstring>

namespace hopflow::stream::win {

QString hresultText(const char *what, HRESULT hr)
{
  return QStringLiteral("%1 failed (0x%2)").arg(QString::fromLatin1(what)).arg(uint32_t(hr), 8, 16, QLatin1Char('0'));
}

void setCodecValue(ICodecAPI *codec, const GUID &key, uint32_t value)
{
  VARIANT variant;
  VariantInit(&variant);
  variant.vt = VT_UI4;
  variant.ulVal = value;
  codec->SetValue(&key, &variant);
}

void setCodecFlag(ICodecAPI *codec, const GUID &key, bool value)
{
  VARIANT variant;
  VariantInit(&variant);
  variant.vt = VT_BOOL;
  variant.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
  codec->SetValue(&key, &variant);
}

ComPtr<IMFSample> sampleWithBuffer(DWORD size)
{
  ComPtr<IMFSample> sample;
  ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(MFCreateSample(&sample)) || FAILED(MFCreateMemoryBuffer(size, &buffer))) {
    return nullptr;
  }
  sample->AddBuffer(buffer.Get());
  return sample;
}

ComPtr<IMFSample> sampleWithBytes(const QByteArray &bytes, int64_t timestampUs)
{
  auto sample = sampleWithBuffer(static_cast<DWORD>(bytes.size()));
  if (!sample) {
    return nullptr;
  }
  ComPtr<IMFMediaBuffer> buffer;
  sample->GetBufferByIndex(0, &buffer);
  BYTE *data = nullptr;
  if (FAILED(buffer->Lock(&data, nullptr, nullptr))) {
    return nullptr;
  }
  std::memcpy(data, bytes.constData(), static_cast<size_t>(bytes.size()));
  buffer->Unlock();
  buffer->SetCurrentLength(static_cast<DWORD>(bytes.size()));
  sample->SetSampleTime(timestampUs * kHundredNsPerUs);
  return sample;
}

QByteArray sampleBytes(IMFSample *sample)
{
  ComPtr<IMFMediaBuffer> buffer;
  if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
    return {};
  }
  BYTE *data = nullptr;
  DWORD length = 0;
  if (FAILED(buffer->Lock(&data, nullptr, &length))) {
    return {};
  }
  QByteArray bytes(reinterpret_cast<const char *>(data), static_cast<qsizetype>(length));
  buffer->Unlock();
  return bytes;
}

MediaFoundationScope::MediaFoundationScope()
{
  m_com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  m_mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
}

MediaFoundationScope::~MediaFoundationScope()
{
  if (m_mf) {
    MFShutdown();
  }
  if (m_com) {
    CoUninitialize();
  }
}

} // namespace hopflow::stream::win
