/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/Yuv.h"

#include <algorithm>

namespace hopflow::stream::yuv {

namespace {

// Rec. 709 coefficients in 16.16 fixed point
constexpr int kOne = 1 << 16;

inline uchar clampByte(int value)
{
  return static_cast<uchar>(std::clamp(value, 0, 255));
}

} // namespace

QImage nv12ToImage(const uchar *luma, int lumaStride, const uchar *chroma, int chromaStride, int width, int height)
{
  // R = 1.164(Y-16) + 1.793(Cr-128)
  // G = 1.164(Y-16) - 0.213(Cb-128) - 0.533(Cr-128)
  // B = 1.164(Y-16) + 2.112(Cb-128)
  constexpr int yScale = int(1.164 * kOne);
  constexpr int rCr = int(1.793 * kOne);
  constexpr int gCb = int(0.213 * kOne);
  constexpr int gCr = int(0.533 * kOne);
  constexpr int bCb = int(2.112 * kOne);

  QImage image(width, height, QImage::Format_RGB32);
  for (int y = 0; y < height; ++y) {
    const uchar *yRow = luma + y * lumaStride;
    const uchar *cRow = chroma + (y / 2) * chromaStride;
    auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const int c = (yRow[x] - 16) * yScale;
      const int cb = cRow[(x & ~1)] - 128;
      const int cr = cRow[(x & ~1) + 1] - 128;
      const int r = (c + rCr * cr + kOne / 2) >> 16;
      const int g = (c - gCb * cb - gCr * cr + kOne / 2) >> 16;
      const int b = (c + bCb * cb + kOne / 2) >> 16;
      out[x] = qRgb(clampByte(r), clampByte(g), clampByte(b));
    }
  }
  return image;
}

QByteArray imageToNv12(const QImage &source)
{
  const auto image = source.convertToFormat(QImage::Format_RGB32);
  const int width = image.width();
  const int height = image.height();
  QByteArray nv12(width * height * 3 / 2, Qt::Uninitialized);
  auto *luma = reinterpret_cast<uchar *>(nv12.data());
  auto *chroma = luma + width * height;

  for (int y = 0; y < height; ++y) {
    const auto *in = reinterpret_cast<const QRgb *>(image.constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const int r = qRed(in[x]);
      const int g = qGreen(in[x]);
      const int b = qBlue(in[x]);
      luma[y * width + x] =
          clampByte(16 + ((int(0.1826 * kOne) * r + int(0.6142 * kOne) * g + int(0.0620 * kOne) * b) >> 16));
    }
  }

  for (int y = 0; y < height; y += 2) {
    for (int x = 0; x < width; x += 2) {
      // average the 2x2 block the chroma sample covers
      int r = 0;
      int g = 0;
      int b = 0;
      for (int dy = 0; dy < 2; ++dy) {
        const auto *in = reinterpret_cast<const QRgb *>(image.constScanLine(y + dy));
        for (int dx = 0; dx < 2; ++dx) {
          r += qRed(in[x + dx]);
          g += qGreen(in[x + dx]);
          b += qBlue(in[x + dx]);
        }
      }
      r /= 4;
      g /= 4;
      b /= 4;
      auto *out = chroma + (y / 2) * width + x;
      out[0] = clampByte(128 + ((-int(0.1006 * kOne) * r - int(0.3386 * kOne) * g + int(0.4392 * kOne) * b) >> 16));
      out[1] = clampByte(128 + ((int(0.4392 * kOne) * r - int(0.3989 * kOne) * g - int(0.0403 * kOne) * b) >> 16));
    }
  }
  return nv12;
}

} // namespace hopflow::stream::yuv
