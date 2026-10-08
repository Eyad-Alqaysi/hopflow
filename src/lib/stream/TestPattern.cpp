/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/TestPattern.h"

#include <QBuffer>
#include <QPainter>

#include <algorithm>

namespace hopflow::stream {

TestPatternSource::TestPatternSource(QSize screen, QObject *parent) : QObject(parent), m_screen(screen)
{
  connect(&m_timer, &QTimer::timeout, this, &TestPatternSource::emitFrame);
}

void TestPatternSource::start(const Preset &preset, ConfigReady onConfig, FrameReady onFrame, Failed)
{
  m_size = outputSize(preset.resolution, m_screen);
  const auto fps = outputFps(preset.frameRate, 60);
  m_onFrame = std::move(onFrame);
  onConfig(
      VideoConfig{
          VideoCodec::Jpeg,
          static_cast<uint16_t>(m_size.width()),
          static_cast<uint16_t>(m_size.height()),
          static_cast<uint16_t>(fps),
          {}
      }
  );
  m_clock.start();
  m_timer.start(1000 / fps);
}

void TestPatternSource::stop()
{
  m_timer.stop();
  m_onFrame = nullptr;
}

void TestPatternSource::setBitrate(uint32_t bitsPerSecond)
{
  // map the bitrate onto JPEG quality: lower bitrate, rougher picture
  const auto full = startBitrate(m_size, 30);
  m_quality = std::clamp(static_cast<int>(80.0 * bitsPerSecond / std::max(1u, full)), 20, 80);
}

void TestPatternSource::requestKeyframe()
{
  // every JPEG frame is a keyframe
}

QImage TestPatternSource::render(QSize size, int64_t elapsedMs)
{
  QImage image(size, QImage::Format_RGB32);
  QPainter painter(&image);
  painter.fillRect(image.rect(), QColor(0x1e, 0x3a, 0x8a));

  // colour bars
  const QColor bars[] = {Qt::white, Qt::yellow, Qt::cyan, Qt::green, Qt::magenta, Qt::red, Qt::blue};
  const int barWidth = size.width() / 7;
  for (int i = 0; i < 7; ++i) {
    painter.fillRect(i * barWidth, 0, barWidth, size.height() / 3, bars[i]);
  }

  // a ball that moves, so dropped frames are visible
  const int travel = std::max(1, size.width() - 80);
  const int x = static_cast<int>((elapsedMs / 4) % (2 * travel));
  painter.setBrush(Qt::white);
  painter.drawEllipse(x < travel ? x : 2 * travel - x, size.height() / 2, 80, 80);

  // a running clock, to measure latency against a clock on the other screen
  painter.setPen(Qt::white);
  auto font = painter.font();
  font.setPixelSize(std::max(12, size.height() / 12));
  painter.setFont(font);
  painter.drawText(
      image.rect().adjusted(0, 0, 0, -size.height() / 10), Qt::AlignHCenter | Qt::AlignBottom,
      QStringLiteral("Hopflow test pattern  %1.%2 s")
          .arg(elapsedMs / 1000)
          .arg((elapsedMs % 1000) / 10, 2, 10, QLatin1Char('0'))
  );
  return image;
}

void TestPatternSource::emitFrame()
{
  if (!m_onFrame) {
    return;
  }

  QByteArray jpeg;
  QBuffer buffer(&jpeg);
  buffer.open(QIODevice::WriteOnly);
  render(m_size, m_clock.elapsed()).save(&buffer, "JPEG", m_quality);
  m_onFrame(EncodedVideo{m_clock.nsecsElapsed() / 1000, true, jpeg});
}

bool JpegDecoder::configure(const VideoConfig &config)
{
  return config.codec == VideoCodec::Jpeg;
}

QImage JpegDecoder::decode(const EncodedVideo &frame)
{
  return QImage::fromData(frame.data, "JPEG");
}

} // namespace hopflow::stream
