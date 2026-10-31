/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <CoreAudio/AudioServerPlugIn.h>

#include <QObject>

//! Loads the built HopflowMic.driver the way coreaudiod does, without installing it
class MicDriverTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void cleanupTestCase();
  void publishesFeedAndMicrophone();
  void translatesFeedUid();
  void streamsMatchDirection();
  void loopsFeedIntoMicrophone();
  void clockStartsAtZero();

private:
  CFPlugInRef m_plugIn = nullptr;
  AudioServerPlugInDriverRef m_driver = nullptr;
};
