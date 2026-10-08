/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QSize>

#include <cstdint>

namespace hopflow::stream {

enum class Resolution : uint8_t
{
  P720,
  P1080,
  Native
};

enum class FrameRate : uint8_t
{
  Fps30,
  Fps60,
  Native
};

struct Preset
{
  Resolution resolution = Resolution::P1080;
  FrameRate frameRate = FrameRate::Fps30;
  bool audio = true;
};

//! Size to encode at for a screen of \p screen pixels, keeping its aspect ratio
/*!
Never scales up, and keeps both sides even as H.264 requires.
*/
inline QSize outputSize(Resolution resolution, QSize screen)
{
  int maxHeight = screen.height();
  if (resolution == Resolution::P720) {
    maxHeight = 720;
  } else if (resolution == Resolution::P1080) {
    maxHeight = 1080;
  }

  QSize size = screen;
  if (screen.height() > maxHeight) {
    size = QSize(static_cast<int>(int64_t(screen.width()) * maxHeight / screen.height()), maxHeight);
  }
  return QSize(size.width() & ~1, size.height() & ~1);
}

//! Frames per second for \p frameRate on a display refreshing at \p displayHz
inline int outputFps(FrameRate frameRate, int displayHz)
{
  switch (frameRate) {
  case FrameRate::Fps30:
    return 30;
  case FrameRate::Fps60:
    return 60;
  case FrameRate::Native:
    break;
  }
  return displayHz > 0 ? displayHz : 60;
}

//! Starting bitrate in bits per second for a stream of \p size at \p fps
/*!
About 0.1 bits per pixel per frame at 30 fps, which H.264 handles well for
screen content; higher frame rates need proportionally less per frame.
*/
inline uint32_t startBitrate(QSize size, int fps)
{
  const double pixelsPerSecond = double(size.width()) * size.height() * fps;
  const double bitsPerPixel = fps > 30 ? 0.07 : 0.1;
  return static_cast<uint32_t>(pixelsPerSecond * bitsPerPixel);
}

} // namespace hopflow::stream
