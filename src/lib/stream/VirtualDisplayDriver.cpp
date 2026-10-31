/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/VirtualDisplayDriver.h"

#include <QRegularExpression>
#include <QSize>
#include <QXmlStreamReader>

#include <algorithm>

namespace hopflow::stream::vdd {

namespace {

QString resolutionXml(const Mode &mode, const QString &indent)
{
  return QStringLiteral(
             "%1<resolution>\n%1    <width>%2</width>\n%1    <height>%3</height>\n"
             "%1    <refresh_rate>%4</refresh_rate>\n%1</resolution>\n"
  )
      .arg(indent)
      .arg(mode.width)
      .arg(mode.height)
      .arg(mode.refreshHz);
}

} // namespace

std::optional<Settings> parseSettings(const QString &xml)
{
  // like the driver, elements are recognised by name wherever they are, and a
  // resolution is complete at its height while a refresh rate adds an explicit mode
  Settings settings;
  QList<QSize> resolutions;
  QList<int> globalRates;
  QSize pending(0, 0);
  QString element;

  QXmlStreamReader reader(xml);
  while (!reader.atEnd()) {
    const auto token = reader.readNext();
    if (token == QXmlStreamReader::StartElement) {
      element = reader.name().toString();
      continue;
    }
    if (token != QXmlStreamReader::Characters || reader.isWhitespace()) {
      continue;
    }
    const auto text = reader.text().trimmed();
    if (element == QLatin1String("count")) {
      settings.monitorCount = std::max(text.toInt(), 1);
    } else if (element == QLatin1String("width")) {
      pending.setWidth(text.toInt());
    } else if (element == QLatin1String("height")) {
      pending.setHeight(text.toInt());
      if (!resolutions.contains(pending)) {
        resolutions.append(pending);
      }
    } else if (element == QLatin1String("refresh_rate")) {
      settings.modes.append(Mode{pending.width(), pending.height(), qRound(text.toDouble())});
    } else if (element == QLatin1String("g_refresh_rate")) {
      globalRates.append(text.toInt());
    }
  }
  if (reader.hasError()) {
    return std::nullopt;
  }

  for (const int rate : std::as_const(globalRates)) {
    for (const auto &size : std::as_const(resolutions)) {
      settings.modes.append(Mode{size.width(), size.height(), rate});
    }
  }
  return settings;
}

std::optional<QString> settingsOffering(const QString &xml, const Mode &mode)
{
  if (xml.trimmed().isEmpty()) {
    return QStringLiteral(
               "<?xml version='1.0' encoding='utf-8'?>\n<vdd_settings>\n"
               "    <monitors>\n        <count>1</count>\n    </monitors>\n"
               "    <resolutions>\n%1    </resolutions>\n</vdd_settings>\n"
    )
        .arg(resolutionXml(mode, QStringLiteral("        ")));
  }

  const auto settings = parseSettings(xml);
  if (!settings) {
    return std::nullopt;
  }
  if (settings->modes.contains(mode)) {
    return xml;
  }

  QString updated = xml;
  if (const auto end = updated.lastIndexOf(QLatin1String("</resolutions>")); end >= 0) {
    // keep the closing tag's own indentation, the new entry goes one level deeper
    const auto lineStart = updated.lastIndexOf(QLatin1Char('\n'), end) + 1;
    const auto indent = updated.mid(lineStart, end - lineStart);
    const bool tagStartsLine = indent.trimmed().isEmpty();
    const auto entry = resolutionXml(mode, tagStartsLine ? indent + QStringLiteral("    ") : QString());
    updated.insert(tagStartsLine ? lineStart : end, tagStartsLine ? entry : QLatin1Char('\n') + entry);
    return updated;
  }

  const QRegularExpression empty(QStringLiteral("<resolutions\\s*/>"));
  const auto block =
      QStringLiteral("<resolutions>\n%1    </resolutions>").arg(resolutionXml(mode, QStringLiteral("        ")));
  if (const auto match = empty.match(updated); match.hasMatch()) {
    updated.replace(match.capturedStart(), match.capturedLength(), block);
    return updated;
  }
  if (const auto end = updated.lastIndexOf(QLatin1String("</vdd_settings>")); end >= 0) {
    updated.insert(end, QStringLiteral("    ") + block + QLatin1Char('\n'));
    return updated;
  }
  return std::nullopt;
}

bool isDriverHardwareId(const QString &hardwareId)
{
  return hardwareId.compare(QLatin1String("Root\\MttVDD"), Qt::CaseInsensitive) == 0 ||
         hardwareId.compare(QLatin1String("MttVDD"), Qt::CaseInsensitive) == 0;
}

std::optional<qsizetype> findDriverSource(const QList<DisplaySource> &sources)
{
  std::optional<qsizetype> found;
  for (qsizetype i = 0; i < sources.size(); ++i) {
    const auto &source = sources[i];
    const bool driver = source.adapterId.contains(QLatin1String("MttVDD"), Qt::CaseInsensitive) ||
                        source.monitorId.contains(QLatin1String("MTT1337"), Qt::CaseInsensitive);
    if (!driver || source.monitorId.isEmpty()) {
      continue;
    }
    if (!source.attached) {
      return i;
    }
    if (!found) {
      found = i;
    }
  }
  return found;
}

QPoint besideDesktop(const QList<DisplaySource> &sources, const QString &deviceName)
{
  int right = 0;
  int top = 0;
  for (const auto &source : sources) {
    if (!source.attached || source.deviceName.compare(deviceName, Qt::CaseInsensitive) == 0) {
      continue;
    }
    right = std::max(right, source.bounds.x() + source.bounds.width());
    if (source.primary) {
      top = source.bounds.y();
    }
  }
  return {right, top};
}

} // namespace hopflow::stream::vdd
