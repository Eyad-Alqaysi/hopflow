/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ShareScreenDialog.h"

#include "common/Settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

using namespace hopflow::stream;

ShareScreenDialog::ShareScreenDialog(const QStringList &targets, QWidget *parent)
    : QDialog(parent),
      m_target(new QComboBox(this)),
      m_resolution(new QComboBox(this)),
      m_frameRate(new QComboBox(this)),
      m_audio(new QCheckBox(tr("Include sound"), this))
{
  setWindowTitle(tr("Share Screen"));

  m_target->setEditable(true);
  m_target->addItems(targets);
  m_target->setToolTip(tr("The computer to show this screen on. Hopflow must be running there."));

  m_resolution->addItem(tr("720p"), QStringLiteral("720p"));
  m_resolution->addItem(tr("1080p"), QStringLiteral("1080p"));
  m_resolution->addItem(tr("Native"), QStringLiteral("native"));
  m_resolution->setCurrentIndex(m_resolution->findData(Settings::value(Settings::Stream::Resolution)));

  m_frameRate->addItem(tr("30 fps"), QStringLiteral("30"));
  m_frameRate->addItem(tr("60 fps"), QStringLiteral("60"));
  m_frameRate->addItem(tr("Native (display refresh rate)"), QStringLiteral("native"));
  m_frameRate->setCurrentIndex(m_frameRate->findData(Settings::value(Settings::Stream::FrameRate)));

  m_audio->setChecked(Settings::value(Settings::Stream::Audio).toBool());

  auto *hint = new QLabel(
      tr("The screen opens in a window on the other computer. To show it on Discord there, share that window."), this
  );
  hint->setWordWrap(true);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Ok)->setText(tr("Share"));
  connect(buttons, &QDialogButtonBox::accepted, this, [this] {
    saveChoices();
    accept();
  });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_target, &QComboBox::currentTextChanged, buttons, [buttons](const QString &text) {
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!text.trimmed().isEmpty());
  });
  buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_target->currentText().trimmed().isEmpty());

  auto *form = new QFormLayout(this);
  form->addRow(tr("Show on:"), m_target);
  form->addRow(tr("Resolution:"), m_resolution);
  form->addRow(tr("Frame rate:"), m_frameRate);
  form->addRow(QString(), m_audio);
  form->addRow(hint);
  form->addRow(buttons);
}

QString ShareScreenDialog::target() const
{
  return m_target->currentText().trimmed();
}

Preset ShareScreenDialog::preset() const
{
  Preset preset;
  const auto resolution = m_resolution->currentData().toString();
  preset.resolution = resolution == QStringLiteral("720p")     ? Resolution::P720
                      : resolution == QStringLiteral("native") ? Resolution::Native
                                                               : Resolution::P1080;
  const auto frameRate = m_frameRate->currentData().toString();
  preset.frameRate = frameRate == QStringLiteral("60")       ? FrameRate::Fps60
                     : frameRate == QStringLiteral("native") ? FrameRate::Native
                                                             : FrameRate::Fps30;
  preset.audio = m_audio->isChecked();
  return preset;
}

void ShareScreenDialog::saveChoices() const
{
  Settings::setValue(Settings::Stream::Resolution, m_resolution->currentData());
  Settings::setValue(Settings::Stream::FrameRate, m_frameRate->currentData());
  Settings::setValue(Settings::Stream::Audio, m_audio->isChecked());
}
