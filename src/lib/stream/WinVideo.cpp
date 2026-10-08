/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

// defines the codec API GUIDs, which have no import library
#include <initguid.h>

#include "stream/WinVideo.h"

#include "stream/H264.h"
#include "stream/Yuv.h"

#include <windows.h>

#include <codecapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <strmif.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <QElapsedTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace hopflow::stream {

namespace {

constexpr int64_t kHundredNsPerUs = 10;

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

ComPtr<IMFMediaType> videoType(const GUID &subtype, UINT32 width, UINT32 height, UINT32 fps)
{
  ComPtr<IMFMediaType> type;
  if (FAILED(MFCreateMediaType(&type))) {
    return nullptr;
  }
  type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  type->SetGUID(MF_MT_SUBTYPE, subtype);
  type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, width, height);
  MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, fps, 1);
  MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  return type;
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

//! Media Foundation needs starting on every thread that uses it
class MediaFoundationScope
{
public:
  MediaFoundationScope()
  {
    m_com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    m_mf = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
  }
  MediaFoundationScope(const MediaFoundationScope &) = delete;
  MediaFoundationScope &operator=(const MediaFoundationScope &) = delete;
  ~MediaFoundationScope()
  {
    if (m_mf) {
      MFShutdown();
    }
    if (m_com) {
      CoUninitialize();
    }
  }

private:
  bool m_com = false;
  bool m_mf = false;
};

//! The Microsoft H.264 encoder, set up for screen sharing
class H264Encoder
{
public:
  struct Output
  {
    int64_t timestampUs = 0;
    bool keyframe = false;
    QList<QByteArray> nals;
  };

  bool open(UINT32 width, UINT32 height, UINT32 fps, uint32_t bitrate, QString &error)
  {
    m_width = width;
    m_height = height;
    m_fps = fps;

    HRESULT hr = CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_mft));
    if (FAILED(hr)) {
      error = hresultText("creating the H.264 encoder", hr);
      return false;
    }
    m_mft.As(&m_codec);
    if (m_codec) {
      setCodecFlag(m_codec.Get(), CODECAPI_AVLowLatencyMode, true);
      setCodecValue(m_codec.Get(), CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
      setCodecValue(m_codec.Get(), CODECAPI_AVEncMPVDefaultBPictureCount, 0);
      setCodecValue(m_codec.Get(), CODECAPI_AVEncMPVGOPSize, fps * 5);
      setCodecValue(m_codec.Get(), CODECAPI_AVEncCommonMeanBitRate, bitrate);
    }

    auto output = videoType(MFVideoFormat_H264, width, height, fps);
    output->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
    output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_High);
    // screens are Rec. 709, matching the macOS encoder
    output->SetUINT32(MF_MT_VIDEO_PRIMARIES, MFVideoPrimaries_BT709);
    output->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    output->SetUINT32(MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_709);
    output->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    hr = m_mft->SetOutputType(0, output.Get(), 0);
    if (FAILED(hr)) {
      error = hresultText("setting the encoder output", hr);
      return false;
    }

    auto input = videoType(MFVideoFormat_NV12, width, height, fps);
    input->SetUINT32(MF_MT_DEFAULT_STRIDE, width);
    hr = m_mft->SetInputType(0, input.Get(), 0);
    if (FAILED(hr)) {
      error = hresultText("setting the encoder input", hr);
      return false;
    }

    MFT_OUTPUT_STREAM_INFO info{};
    m_mft->GetOutputStreamInfo(0, &info);
    m_outputSize = std::max<DWORD>(info.cbSize, width * height);
    m_providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;

    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    m_mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    return true;
  }

  void setBitrate(uint32_t bitrate)
  {
    if (m_codec) {
      setCodecValue(m_codec.Get(), CODECAPI_AVEncCommonMeanBitRate, bitrate);
    }
  }

  void forceKeyframe()
  {
    if (m_codec) {
      setCodecValue(m_codec.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1);
    }
  }

  //! SPS and PPS from the output type, in case frames do not carry them
  QList<QByteArray> sequenceHeader() const
  {
    ComPtr<IMFMediaType> type;
    UINT32 size = 0;
    if (FAILED(m_mft->GetOutputCurrentType(0, &type)) || FAILED(type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) ||
        size == 0) {
      return {};
    }
    QByteArray header(static_cast<qsizetype>(size), Qt::Uninitialized);
    type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, reinterpret_cast<UINT8 *>(header.data()), size, nullptr);
    return h264::splitAnnexB(header);
  }

  //! Encode one NV12 picture with tightly packed planes
  std::vector<Output> encode(const QByteArray &nv12, int64_t timestampUs)
  {
    std::vector<Output> outputs;
    auto sample = sampleWithBuffer(static_cast<DWORD>(nv12.size()));
    ComPtr<IMFMediaBuffer> buffer;
    sample->GetBufferByIndex(0, &buffer);
    BYTE *data = nullptr;
    buffer->Lock(&data, nullptr, nullptr);
    memcpy(data, nv12.constData(), static_cast<size_t>(nv12.size()));
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(nv12.size()));
    sample->SetSampleTime(timestampUs * kHundredNsPerUs);
    sample->SetSampleDuration(10'000'000 / m_fps);

    HRESULT hr = m_mft->ProcessInput(0, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
      drain(outputs);
      hr = m_mft->ProcessInput(0, sample.Get(), 0);
    }
    if (SUCCEEDED(hr)) {
      drain(outputs);
    }
    return outputs;
  }

private:
  void drain(std::vector<Output> &outputs)
  {
    while (true) {
      MFT_OUTPUT_DATA_BUFFER output{};
      ComPtr<IMFSample> sample;
      if (!m_providesSamples) {
        sample = sampleWithBuffer(m_outputSize);
        output.pSample = sample.Get();
      }

      DWORD status = 0;
      const HRESULT hr = m_mft->ProcessOutput(0, 1, &output, &status);
      if (output.pEvents != nullptr) {
        output.pEvents->Release();
      }
      if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> type;
        if (SUCCEEDED(m_mft->GetOutputAvailableType(0, 0, &type))) {
          m_mft->SetOutputType(0, type.Get(), 0);
        }
        continue;
      }
      if (FAILED(hr)) {
        return; // MF_E_TRANSFORM_NEED_MORE_INPUT: all out
      }

      IMFSample *produced = output.pSample;
      Output out;
      LONGLONG time = 0;
      produced->GetSampleTime(&time);
      out.timestampUs = time / kHundredNsPerUs;
      UINT32 clean = 0;
      out.keyframe = SUCCEEDED(produced->GetUINT32(MFSampleExtension_CleanPoint, &clean)) && clean != 0;
      out.nals = h264::splitAnnexB(sampleBytes(produced));
      if (m_providesSamples) {
        produced->Release();
      }
      if (!out.nals.isEmpty()) {
        outputs.push_back(std::move(out));
      }
    }
  }

  ComPtr<IMFTransform> m_mft;
  ComPtr<ICodecAPI> m_codec;
  UINT32 m_width = 0;
  UINT32 m_height = 0;
  UINT32 m_fps = 30;
  DWORD m_outputSize = 0;
  bool m_providesSamples = false;
};

//! Shape and position of the mouse pointer, drawn onto captured frames
/*!
Desktop duplication leaves the pointer out of the picture.
*/
struct Pointer
{
  bool visible = false;
  POINT position{};
  DXGI_OUTDUPL_POINTER_SHAPE_INFO shape{};
  std::vector<BYTE> pixels;

  //! Blend the pointer into BGRA pixels whose top left is at \p origin
  void drawOnto(BYTE *bgra, UINT pitch, POINT origin, UINT width, UINT height) const
  {
    const bool monochrome = shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
    const UINT shapeHeight = monochrome ? shape.Height / 2 : shape.Height;
    for (UINT row = 0; row < shapeHeight; ++row) {
      const LONG y = position.y + LONG(row) - origin.y;
      if (y < 0 || y >= LONG(height)) {
        continue;
      }
      for (UINT column = 0; column < shape.Width; ++column) {
        const LONG x = position.x + LONG(column) - origin.x;
        if (x < 0 || x >= LONG(width)) {
          continue;
        }
        auto *dst = bgra + size_t(y) * pitch + size_t(x) * 4;
        if (monochrome) {
          const BYTE bit = BYTE(0x80 >> (column % 8));
          const bool andBit = (pixels[row * shape.Pitch + column / 8] & bit) != 0;
          const bool xorBit = (pixels[(row + shapeHeight) * shape.Pitch + column / 8] & bit) != 0;
          for (int c = 0; c < 3; ++c) {
            dst[c] = BYTE((andBit ? dst[c] : 0) ^ (xorBit ? 0xff : 0));
          }
          continue;
        }

        const BYTE *src = pixels.data() + row * shape.Pitch + column * 4;
        if (shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR) {
          for (int c = 0; c < 3; ++c) {
            dst[c] = src[3] == 0 ? src[c] : BYTE(dst[c] ^ src[c]);
          }
        } else {
          const UINT alpha = src[3];
          for (int c = 0; c < 3; ++c) {
            dst[c] = BYTE((src[c] * alpha + dst[c] * (255 - alpha)) / 255);
          }
        }
      }
    }
  }

  UINT drawnHeight() const
  {
    return shape.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME ? shape.Height / 2 : shape.Height;
  }
};

} // namespace

struct WinScreenSource::Impl
{
  std::thread worker;
  std::atomic<bool> running{false};
  std::atomic<bool> keyframeRequested{true};
  std::atomic<uint32_t> bitrate{0};
  std::mutex callbackMutex;
  ConfigReady onConfig;
  FrameReady onFrame;
  Failed onFailed;

  void fail(const QString &message)
  {
    std::scoped_lock lock(callbackMutex);
    if (running.exchange(false) && onFailed) {
      onFailed(message);
    }
  }

  void run(Preset preset);
};

void WinScreenSource::Impl::run(Preset preset)
{
  MediaFoundationScope mediaFoundation;

  // the primary display, through its adapter
  ComPtr<ID3D11Device> device;
  ComPtr<ID3D11DeviceContext> context;
  HRESULT hr = D3D11CreateDevice(
      nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
      nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context
  );
  if (FAILED(hr)) {
    fail(hresultText("starting Direct3D", hr));
    return;
  }

  ComPtr<IDXGIDevice> dxgiDevice;
  ComPtr<IDXGIAdapter> adapter;
  ComPtr<IDXGIOutput> output;
  ComPtr<IDXGIOutput1> output1;
  device.As(&dxgiDevice);
  dxgiDevice->GetAdapter(&adapter);
  if (FAILED(adapter->EnumOutputs(0, &output)) || FAILED(output.As(&output1))) {
    fail(QStringLiteral("no display to capture"));
    return;
  }

  DXGI_OUTPUT_DESC outputDesc{};
  output->GetDesc(&outputDesc);
  const UINT screenWidth = UINT(outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left);
  const UINT screenHeight = UINT(outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top);

  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  int refresh = 60;
  if (EnumDisplaySettingsW(outputDesc.DeviceName, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
    refresh = int(mode.dmDisplayFrequency);
  }

  const auto size = outputSize(preset.resolution, QSize(int(screenWidth), int(screenHeight)));
  const UINT width = UINT(size.width());
  const UINT height = UINT(size.height());
  const UINT fps = UINT(outputFps(preset.frameRate, refresh));
  bitrate = startBitrate(size, int(fps));

  ComPtr<IDXGIOutputDuplication> duplication;
  hr = output1->DuplicateOutput(device.Get(), &duplication);
  if (FAILED(hr)) {
    fail(hresultText("capturing the screen", hr));
    return;
  }

  // a copy of the desktop we own, the picture with the pointer, and the scaled NV12 result
  auto texture = [&](UINT w, UINT h, DXGI_FORMAT format, D3D11_USAGE usage, UINT bind, UINT cpu) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = usage;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = cpu;
    ComPtr<ID3D11Texture2D> created;
    device->CreateTexture2D(&desc, nullptr, &created);
    return created;
  };
  auto desktop = texture(
      screenWidth, screenHeight, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_DEFAULT,
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0
  );
  auto composed = texture(
      screenWidth, screenHeight, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_DEFAULT,
      D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0
  );
  auto scaled = texture(width, height, DXGI_FORMAT_NV12, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0);
  auto readback = texture(width, height, DXGI_FORMAT_NV12, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
  if (!desktop || !composed || !scaled || !readback) {
    fail(QStringLiteral("cannot create capture surfaces"));
    return;
  }

  // the GPU scales the picture and converts it to Rec. 709 YUV
  ComPtr<ID3D11VideoDevice> videoDevice;
  ComPtr<ID3D11VideoContext> videoContext;
  device.As(&videoDevice);
  context.As(&videoContext);
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
  content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputWidth = screenWidth;
  content.InputHeight = screenHeight;
  content.OutputWidth = width;
  content.OutputHeight = height;
  content.InputFrameRate = {fps, 1};
  content.OutputFrameRate = {fps, 1};
  content.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
  ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
  ComPtr<ID3D11VideoProcessor> processor;
  ComPtr<ID3D11VideoProcessorInputView> inputView;
  ComPtr<ID3D11VideoProcessorOutputView> outputView;
  if (!videoDevice || !videoContext || FAILED(videoDevice->CreateVideoProcessorEnumerator(&content, &enumerator)) ||
      FAILED(videoDevice->CreateVideoProcessor(enumerator.Get(), 0, &processor))) {
    fail(QStringLiteral("this graphics card cannot scale the screen"));
    return;
  }
  D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc{};
  inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDescView{};
  outputDescView.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
  videoDevice->CreateVideoProcessorInputView(composed.Get(), enumerator.Get(), &inputDesc, &inputView);
  videoDevice->CreateVideoProcessorOutputView(scaled.Get(), enumerator.Get(), &outputDescView, &outputView);
  if (!inputView || !outputView) {
    fail(QStringLiteral("this graphics card cannot convert the screen"));
    return;
  }
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{};
  rgb.RGB_Range = 0; // full range
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE yuv{};
  yuv.YCbCr_Matrix = 1; // BT.709
  yuv.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
  videoContext->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &rgb);
  videoContext->VideoProcessorSetOutputColorSpace(processor.Get(), &yuv);

  H264Encoder encoder;
  QString error;
  if (!encoder.open(width, height, fps, bitrate, error)) {
    fail(error);
    return;
  }

  Pointer pointer;
  ComPtr<ID3D11Texture2D> pointerPatch;
  QByteArray avcC;
  QList<QByteArray> parameterSets;
  bool havePicture = false;
  bool changed = false;
  uint32_t encoderBitrate = bitrate;
  QElapsedTimer clock;
  clock.start();
  const auto interval = std::chrono::microseconds(1'000'000 / fps);
  auto nextFrame = std::chrono::steady_clock::now();

  while (running) {
    // wait for the screen to change, but no longer than one frame
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    hr = duplication->AcquireNextFrame(UINT(1000 / fps), &info, &resource);
    if (hr == DXGI_ERROR_ACCESS_LOST) {
      // the desktop switched (lock screen, UAC, resolution change); duplicate again
      duplication.Reset();
      if (FAILED(output1->DuplicateOutput(device.Get(), &duplication))) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        output1->DuplicateOutput(device.Get(), &duplication);
      }
      if (!duplication) {
        fail(QStringLiteral("lost the screen"));
        return;
      }
      continue;
    }

    if (SUCCEEDED(hr)) {
      if (info.LastPresentTime.QuadPart != 0) {
        ComPtr<ID3D11Texture2D> frame;
        resource.As(&frame);
        context->CopyResource(desktop.Get(), frame.Get());
        havePicture = true;
        changed = true;
      }
      if (info.LastMouseUpdateTime.QuadPart != 0) {
        pointer.visible = info.PointerPosition.Visible != FALSE;
        pointer.position = POINT{info.PointerPosition.Position.x, info.PointerPosition.Position.y};
        changed = true;
      }
      if (info.PointerShapeBufferSize > 0) {
        pointer.pixels.resize(info.PointerShapeBufferSize);
        UINT required = 0;
        duplication->GetFramePointerShape(
            info.PointerShapeBufferSize, pointer.pixels.data(), &required, &pointer.shape
        );
        pointerPatch.Reset();
      }
      duplication->ReleaseFrame();
    } else if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
      fail(hresultText("capturing a frame", hr));
      return;
    }

    const bool keyframe = keyframeRequested.exchange(false);
    const auto now = std::chrono::steady_clock::now();
    if (!havePicture || (!changed && !keyframe) || now < nextFrame) {
      if (keyframe) {
        keyframeRequested = true; // keep it for the next frame we send
      }
      continue;
    }
    nextFrame = std::max(nextFrame + interval, now);
    changed = false;

    // draw the pointer onto a copy of the desktop, through a small CPU patch
    context->CopyResource(composed.Get(), desktop.Get());
    if (pointer.visible && !pointer.pixels.empty() && pointer.shape.Width > 0) {
      const UINT patchWidth = pointer.shape.Width;
      const UINT patchHeight = pointer.drawnHeight();
      if (!pointerPatch) {
        pointerPatch = texture(
            patchWidth, patchHeight, DXGI_FORMAT_B8G8R8A8_UNORM, D3D11_USAGE_STAGING, 0,
            D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE
        );
      }
      const LONG left = std::clamp<LONG>(pointer.position.x, 0, LONG(screenWidth) - 1);
      const LONG top = std::clamp<LONG>(pointer.position.y, 0, LONG(screenHeight) - 1);
      const UINT right = std::min<UINT>(screenWidth, UINT(left) + patchWidth);
      const UINT bottom = std::min<UINT>(screenHeight, UINT(top) + patchHeight);
      if (pointerPatch && right > UINT(left) && bottom > UINT(top)) {
        D3D11_BOX box{UINT(left), UINT(top), 0, right, bottom, 1};
        context->CopySubresourceRegion(pointerPatch.Get(), 0, 0, 0, 0, composed.Get(), 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(pointerPatch.Get(), 0, D3D11_MAP_READ_WRITE, 0, &mapped))) {
          pointer.drawOnto(
              static_cast<BYTE *>(mapped.pData), mapped.RowPitch, POINT{left, top}, right - UINT(left),
              bottom - UINT(top)
          );
          context->Unmap(pointerPatch.Get(), 0);
          D3D11_BOX patchBox{0, 0, 0, right - UINT(left), bottom - UINT(top), 1};
          context->CopySubresourceRegion(composed.Get(), 0, UINT(left), UINT(top), 0, pointerPatch.Get(), 0, &patchBox);
        }
      }
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();
    if (FAILED(videoContext->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream))) {
      continue;
    }

    context->CopyResource(readback.Get(), scaled.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
      continue;
    }
    QByteArray nv12(qsizetype(width) * height * 3 / 2, Qt::Uninitialized);
    const auto *source = static_cast<const BYTE *>(mapped.pData);
    for (UINT row = 0; row < height * 3 / 2; ++row) {
      // NV12 surfaces store the CbCr plane right after the luma rows
      memcpy(nv12.data() + size_t(row) * width, source + size_t(row) * mapped.RowPitch, width);
    }
    context->Unmap(readback.Get(), 0);

    if (const auto wanted = bitrate.load(); wanted != encoderBitrate) {
      encoder.setBitrate(wanted);
      encoderBitrate = wanted;
    }
    if (keyframe) {
      encoder.forceKeyframe();
    }

    for (auto &out : encoder.encode(nv12, clock.nsecsElapsed() / 1000)) {
      // parameter sets go in the avcC header, not in the frames
      QList<QByteArray> picture;
      QList<QByteArray> sps;
      QList<QByteArray> pps;
      for (const auto &nal : out.nals) {
        const auto type = h264::nalType(nal);
        if (type == h264::kNalSps) {
          sps.append(nal);
        } else if (type == h264::kNalPps) {
          pps.append(nal);
        } else if (type == h264::kNalIdr || type == 1) {
          picture.append(nal);
          out.keyframe = out.keyframe || type == h264::kNalIdr;
        }
      }
      if (sps.isEmpty() && out.keyframe) {
        for (const auto &nal : encoder.sequenceHeader()) {
          (h264::nalType(nal) == h264::kNalSps ? sps : pps).append(nal);
        }
      }

      std::scoped_lock lock(callbackMutex);
      if (!running) {
        break;
      }
      if (!sps.isEmpty() && !pps.isEmpty()) {
        const auto header = h264::makeAvcC(h264::ParameterSets{sps, pps});
        if (header != avcC) {
          avcC = header;
          onConfig(VideoConfig{VideoCodec::H264, uint16_t(width), uint16_t(height), uint16_t(fps), avcC});
        }
      }
      if (!avcC.isEmpty() && !picture.isEmpty()) {
        onFrame(EncodedVideo{out.timestampUs, out.keyframe, h264::joinAvcc(picture)});
      }
    }
  }
}

WinScreenSource::WinScreenSource() : m_impl(std::make_unique<Impl>())
{
}

WinScreenSource::~WinScreenSource()
{
  stop();
}

void WinScreenSource::start(const Preset &preset, ConfigReady onConfig, FrameReady onFrame, Failed onFailed)
{
  stop();
  m_impl->onConfig = std::move(onConfig);
  m_impl->onFrame = std::move(onFrame);
  m_impl->onFailed = std::move(onFailed);
  m_impl->keyframeRequested = true;
  m_impl->running = true;
  m_impl->worker = std::thread([impl = m_impl.get(), preset] { impl->run(preset); });
}

void WinScreenSource::stop()
{
  {
    std::scoped_lock lock(m_impl->callbackMutex);
    m_impl->running = false;
  }
  if (m_impl->worker.joinable()) {
    m_impl->worker.join();
  }
}

void WinScreenSource::setBitrate(uint32_t bitsPerSecond)
{
  m_impl->bitrate = bitsPerSecond;
}

void WinScreenSource::requestKeyframe()
{
  m_impl->keyframeRequested = true;
}

//
// WinVideoDecoder
//

struct WinVideoDecoder::Impl
{
  std::unique_ptr<MediaFoundationScope> mediaFoundation;
  ComPtr<IMFTransform> mft;
  h264::ParameterSets sets;
  UINT32 width = 0;
  UINT32 height = 0;
  UINT32 alignedHeight = 0;
  DWORD outputSize = 0;
  bool providesSamples = false;

  bool chooseNv12Output()
  {
    for (DWORD i = 0;; ++i) {
      ComPtr<IMFMediaType> type;
      if (FAILED(mft->GetOutputAvailableType(0, i, &type))) {
        return false;
      }
      GUID subtype{};
      type->GetGUID(MF_MT_SUBTYPE, &subtype);
      if (subtype == MFVideoFormat_NV12 && SUCCEEDED(mft->SetOutputType(0, type.Get(), 0))) {
        UINT32 w = 0;
        UINT32 h = 0;
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
        alignedHeight = std::max(h, height);
        MFT_OUTPUT_STREAM_INFO info{};
        mft->GetOutputStreamInfo(0, &info);
        outputSize = std::max<DWORD>(info.cbSize, w * alignedHeight * 3 / 2);
        providesSamples = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
        return true;
      }
    }
  }

  QImage toImage(IMFSample *sample)
  {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample->GetBufferByIndex(0, &buffer))) {
      return {};
    }

    ComPtr<IMF2DBuffer> buffer2d;
    BYTE *scan0 = nullptr;
    LONG pitch = 0;
    QImage image;
    if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scan0, &pitch)) && pitch > 0) {
      image = yuv::nv12ToImage(
          scan0, int(pitch), scan0 + size_t(pitch) * alignedHeight, int(pitch), int(width), int(height)
      );
      buffer2d->Unlock2D();
    } else {
      BYTE *data = nullptr;
      if (SUCCEEDED(buffer->Lock(&data, nullptr, nullptr))) {
        image = yuv::nv12ToImage(
            data, int(width), data + size_t(width) * alignedHeight, int(width), int(width), int(height)
        );
        buffer->Unlock();
      }
    }
    return image;
  }
};

WinVideoDecoder::WinVideoDecoder() : m_impl(std::make_unique<Impl>())
{
  m_impl->mediaFoundation = std::make_unique<MediaFoundationScope>();
}

WinVideoDecoder::~WinVideoDecoder()
{
  m_impl->mft.Reset();
}

bool WinVideoDecoder::configure(const VideoConfig &config)
{
  m_impl->mft.Reset();
  const auto sets = h264::parseAvcC(config.codecData);
  if (config.codec != VideoCodec::H264 || !sets) {
    return false;
  }
  m_impl->sets = *sets;
  m_impl->width = config.width;
  m_impl->height = config.height;

  if (FAILED(CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_impl->mft)))) {
    return false;
  }

  ComPtr<ICodecAPI> codec;
  if (SUCCEEDED(m_impl->mft.As(&codec))) {
    // hand each frame out as soon as it is decoded
    setCodecFlag(codec.Get(), CODECAPI_AVLowLatencyMode, true);
  }

  auto input = videoType(MFVideoFormat_H264, config.width, config.height, std::max<UINT32>(config.fps, 1));
  if (FAILED(m_impl->mft->SetInputType(0, input.Get(), 0)) || !m_impl->chooseNv12Output()) {
    m_impl->mft.Reset();
    return false;
  }

  m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
  m_impl->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  return true;
}

QImage WinVideoDecoder::decode(const EncodedVideo &frame)
{
  if (!m_impl->mft) {
    return {};
  }

  auto nals = h264::splitAvcc(frame.data);
  if (!nals) {
    return {};
  }
  // Media Foundation reads the parameter sets in band, before each keyframe
  if (frame.keyframe) {
    QList<QByteArray> withSets = m_impl->sets.sps + m_impl->sets.pps;
    withSets.append(*nals);
    nals = withSets;
  }
  const auto annexB = h264::joinAnnexB(*nals);

  auto input = sampleWithBuffer(static_cast<DWORD>(annexB.size()));
  ComPtr<IMFMediaBuffer> buffer;
  input->GetBufferByIndex(0, &buffer);
  BYTE *data = nullptr;
  buffer->Lock(&data, nullptr, nullptr);
  memcpy(data, annexB.constData(), size_t(annexB.size()));
  buffer->Unlock();
  buffer->SetCurrentLength(static_cast<DWORD>(annexB.size()));
  input->SetSampleTime(frame.timestampUs * kHundredNsPerUs);
  if (frame.keyframe) {
    input->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
  }

  if (FAILED(m_impl->mft->ProcessInput(0, input.Get(), 0))) {
    return {};
  }

  QImage image;
  while (true) {
    MFT_OUTPUT_DATA_BUFFER output{};
    ComPtr<IMFSample> sample;
    if (!m_impl->providesSamples) {
      sample = sampleWithBuffer(m_impl->outputSize);
      output.pSample = sample.Get();
    }
    DWORD status = 0;
    const HRESULT hr = m_impl->mft->ProcessOutput(0, 1, &output, &status);
    if (output.pEvents != nullptr) {
      output.pEvents->Release();
    }
    if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
      if (!m_impl->chooseNv12Output()) {
        return {};
      }
      continue;
    }
    if (FAILED(hr)) {
      break;
    }
    image = m_impl->toImage(output.pSample);
    if (m_impl->providesSamples) {
      output.pSample->Release();
    }
  }
  return image;
}

} // namespace hopflow::stream
