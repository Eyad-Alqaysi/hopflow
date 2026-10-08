/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/StreamPreset.h"

#include <QDialog>

class QCheckBox;
class QComboBox;

//! Choose where to show this screen and how
class ShareScreenDialog : public QDialog
{
  Q_OBJECT

public:
  //! \p targets are suggested computers; the user may also type an address
  ShareScreenDialog(const QStringList &targets, QWidget *parent = nullptr);

  QString target() const;
  hopflow::stream::Preset preset() const;

private:
  void saveChoices() const;

  QComboBox *m_target;
  QComboBox *m_resolution;
  QComboBox *m_frameRate;
  QCheckBox *m_audio;
};
