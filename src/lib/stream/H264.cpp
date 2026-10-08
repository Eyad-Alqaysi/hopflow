/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/H264.h"

namespace hopflow::stream::h264 {

namespace {

void appendBigEndian(QByteArray &out, uint32_t value, int bytes)
{
  for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
    out.append(static_cast<char>((value >> shift) & 0xff));
  }
}

uint32_t readBigEndian(const QByteArray &data, qsizetype offset, int bytes)
{
  uint32_t value = 0;
  for (int i = 0; i < bytes; ++i) {
    value = (value << 8) | static_cast<uint8_t>(data[offset + i]);
  }
  return value;
}

} // namespace

QByteArray makeAvcC(const ParameterSets &sets)
{
  if (sets.sps.isEmpty() || sets.sps.first().size() < 4) {
    return {};
  }

  const auto &sps = sets.sps.first();
  QByteArray out;
  out.append(char(1));    // configurationVersion
  out.append(sps[1]);     // AVCProfileIndication
  out.append(sps[2]);     // profile_compatibility
  out.append(sps[3]);     // AVCLevelIndication
  out.append(char(0xff)); // 6 reserved bits, lengthSizeMinusOne = 3
  out.append(static_cast<char>(0xe0 | (sets.sps.size() & 0x1f)));
  for (const auto &set : sets.sps) {
    appendBigEndian(out, static_cast<uint32_t>(set.size()), 2);
    out.append(set);
  }
  out.append(static_cast<char>(sets.pps.size()));
  for (const auto &set : sets.pps) {
    appendBigEndian(out, static_cast<uint32_t>(set.size()), 2);
    out.append(set);
  }
  return out;
}

std::optional<ParameterSets> parseAvcC(const QByteArray &avcC)
{
  if (avcC.size() < 7 || avcC[0] != char(1) || (static_cast<uint8_t>(avcC[4]) & 0x03) != 3) {
    return std::nullopt;
  }

  ParameterSets sets;
  qsizetype offset = 5;
  auto readSets = [&](int count, QList<QByteArray> &into) {
    for (int i = 0; i < count; ++i) {
      if (offset + 2 > avcC.size()) {
        return false;
      }
      const auto length = readBigEndian(avcC, offset, 2);
      offset += 2;
      if (offset + length > avcC.size()) {
        return false;
      }
      into.append(avcC.mid(offset, length));
      offset += length;
    }
    return true;
  };

  if (!readSets(static_cast<uint8_t>(avcC[offset++]) & 0x1f, sets.sps) || offset >= avcC.size()) {
    return std::nullopt;
  }
  if (!readSets(static_cast<uint8_t>(avcC[offset++]), sets.pps) || sets.sps.isEmpty() || sets.pps.isEmpty()) {
    return std::nullopt;
  }
  return sets;
}

std::optional<QList<QByteArray>> splitAvcc(const QByteArray &data)
{
  QList<QByteArray> nals;
  qsizetype offset = 0;
  while (offset < data.size()) {
    if (offset + 4 > data.size()) {
      return std::nullopt;
    }
    const auto length = readBigEndian(data, offset, 4);
    offset += 4;
    if (length == 0 || offset + length > data.size()) {
      return std::nullopt;
    }
    nals.append(data.mid(offset, length));
    offset += length;
  }
  return nals;
}

QList<QByteArray> splitAnnexB(const QByteArray &data)
{
  QList<QByteArray> nals;
  qsizetype start = -1;
  qsizetype i = 0;
  while (i + 2 < data.size()) {
    // a start code is 00 00 01, possibly after one more zero
    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
      if (start >= 0) {
        auto end = i;
        while (end > start && data[end - 1] == 0) {
          --end; // trailing zeros belong to the next start code
        }
        nals.append(data.mid(start, end - start));
      }
      i += 3;
      start = i;
    } else {
      ++i;
    }
  }
  if (start >= 0 && start < data.size()) {
    nals.append(data.mid(start));
  }
  return nals;
}

QByteArray joinAvcc(const QList<QByteArray> &nals)
{
  QByteArray out;
  for (const auto &nal : nals) {
    appendBigEndian(out, static_cast<uint32_t>(nal.size()), 4);
    out.append(nal);
  }
  return out;
}

QByteArray joinAnnexB(const QList<QByteArray> &nals)
{
  QByteArray out;
  for (const auto &nal : nals) {
    out.append("\0\0\0\1", 4);
    out.append(nal);
  }
  return out;
}

} // namespace hopflow::stream::h264
