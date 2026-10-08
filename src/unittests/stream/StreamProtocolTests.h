/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class StreamProtocolTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void frameRoundTrip();
  void parserHandlesSplitAndJoinedFrames();
  void parserRejectsUnknownType();
  void parserRejectsOversizedFrame();
  void payloadRoundTrips();
  void payloadRejectsMalformed_data();
  void payloadRejectsMalformed();
  void outputSize_data();
  void outputSize();
  void outputFps();
  void rateDropsOnBacklogAndStepsDown();
  void rateStepsBackUp();
  void rateNeverGoesBelowFloor();
};
