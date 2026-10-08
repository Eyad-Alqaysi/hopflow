/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QString>

#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <wrl/client.h>

//! Media Foundation plumbing shared by the Windows audio and video code
namespace hopflow::stream::win {

using Microsoft::WRL::ComPtr;

inline constexpr int64_t kHundredNsPerUs = 10;

QString hresultText(const char *what, HRESULT hr);

void setCodecValue(ICodecAPI *codec, const GUID &key, uint32_t value);
void setCodecFlag(ICodecAPI *codec, const GUID &key, bool value);

ComPtr<IMFSample> sampleWithBuffer(DWORD size);
ComPtr<IMFSample> sampleWithBytes(const QByteArray &bytes, int64_t timestampUs);
QByteArray sampleBytes(IMFSample *sample);

//! Run every output the transform has ready through \p handle
/*!
\p onStreamChange is called when the transform changes its output type and
must choose a new one; returns false to give up.
*/
template <typename Handle, typename StreamChange>
void drainTransform(
    IMFTransform *mft, DWORD outputSize, bool providesSamples, Handle handle, StreamChange onStreamChange
)
{
  while (true) {
    MFT_OUTPUT_DATA_BUFFER output{};
    ComPtr<IMFSample> sample;
    if (!providesSamples) {
      sample = sampleWithBuffer(outputSize);
      output.pSample = sample.Get();
    }
    DWORD status = 0;
    const HRESULT hr = mft->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents != nullptr) {
      output.pEvents->Release();
    }
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
      if (!onStreamChange()) {
        return;
      }
      continue;
    }
    if (FAILED(hr)) {
      return; // usually MF_E_TRANSFORM_NEED_MORE_INPUT: nothing more for now
    }
    handle(output.pSample);
    if (providesSamples) {
      output.pSample->Release();
    }
  }
}

//! Media Foundation and COM need starting on every thread that uses them
class MediaFoundationScope
{
public:
  MediaFoundationScope();
  MediaFoundationScope(const MediaFoundationScope &) = delete;
  MediaFoundationScope &operator=(const MediaFoundationScope &) = delete;
  ~MediaFoundationScope();

private:
  bool m_com = false;
  bool m_mf = false;
};

} // namespace hopflow::stream::win
