/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QByteArray>
#include <QList>

#include <optional>

//! H.264 bitstream helpers shared by the platform codecs
/*!
On the wire, H.264 frames are length prefixed NAL units (AVCC, 4 byte big
endian lengths) and the stream setup is an avcC record, which is what
VideoToolbox uses. Media Foundation uses start codes (Annex B) instead, so
Windows converts with these helpers.
*/
namespace hopflow::stream::h264 {

struct ParameterSets
{
  QList<QByteArray> sps;
  QList<QByteArray> pps;
};

//! Build an avcC record (ISO/IEC 14496-15) from parameter sets
QByteArray makeAvcC(const ParameterSets &sets);

//! Read the parameter sets out of an avcC record
std::optional<ParameterSets> parseAvcC(const QByteArray &avcC);

//! Split a length prefixed access unit into NAL units
std::optional<QList<QByteArray>> splitAvcc(const QByteArray &data);

//! Split an Annex B stream into NAL units, dropping the start codes
QList<QByteArray> splitAnnexB(const QByteArray &data);

QByteArray joinAvcc(const QList<QByteArray> &nals);
QByteArray joinAnnexB(const QList<QByteArray> &nals);

//! NAL unit type, the low five bits of the first byte
inline int nalType(const QByteArray &nal)
{
  return nal.isEmpty() ? -1 : (static_cast<uint8_t>(nal[0]) & 0x1f);
}

inline constexpr int kNalIdr = 5;
inline constexpr int kNalSps = 7;
inline constexpr int kNalPps = 8;

} // namespace hopflow::stream::h264
