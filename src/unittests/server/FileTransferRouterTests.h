/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class FileTransferRouterTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void clipboardDeliveredOnceOnEnter();
  void clipboardNotSentBackToOwner();
  void clipboardNotSentToDeskflowClient();
  void newClipboardWithoutFilesClearsOffer();
  void deskflowClientGrabClearsOffer();
  void relaysTransferBetweenClients();
  void ignoresMessagesFromWrongEnd();
  void refusesUnknownTransfer();
  void refusesTooLarge();
  void dropStartsTransfer();
  void dropOnSameComputerDoesNothing();
  void disconnectCancelsTransfer();
  void disabledDoesNothing();

private:
  Log m_log;
};
