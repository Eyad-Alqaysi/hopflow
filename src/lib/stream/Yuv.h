/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QImage>

//! Conversions between RGB and NV12, Rec. 709 limited range, as the codecs use
namespace hopflow::stream::yuv {

//! Convert an NV12 picture (a luma plane, then interleaved CbCr at half size) to RGB32
QImage nv12ToImage(const uchar *luma, int lumaStride, const uchar *chroma, int chromaStride, int width, int height);

//! Convert \p image to NV12 with tightly packed planes; width and height must be even
QByteArray imageToNv12(const QImage &image);

} // namespace hopflow::stream::yuv
