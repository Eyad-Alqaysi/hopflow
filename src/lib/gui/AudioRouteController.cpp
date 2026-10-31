/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "AudioRouteController.h"

#include "StreamSupport.h"
#include "common/Settings.h"
#include "stream/MediaInterfaces.h"
#include "stream/StreamSender.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QPushButton>

using namespace hopflow::stream;
using namespace hopflow::gui;

AudioRouteController::AudioRouteController(
    SessionKind kind, QWidget *window, std::function<QStringList()> targets, QObject *parent
)
    : QObject(parent),
      m_kind(kind),
      m_window(window),
      m_targets(std::move(targets)),
      m_sendAction(new QAction(isSound() ? tr("Send Sound To…") : tr("Send Microphone To…"), this)),
      m_stopAction(new QAction(isSound() ? tr("Stop Sending Sound") : tr("Stop Sending Microphone"), this)),
      m_sender(makeSender())
{
  connect(m_sendAction, &QAction::triggered, this, &AudioRouteController::chooseAndSend);
  connect(m_stopAction, &QAction::triggered, this, &AudioRouteController::stopSending);

  connect(m_sender.get(), &StreamSender::streaming, this, [this](const QString &to) {
    qInfo().noquote() << "sending" << (isSound() ? "sound" : "microphone") << "to" << to;
    m_hostsToTry.clear();
    m_streaming = true;
    Q_EMIT notify(
        isSound() ? tr("Playing this computer's sound on %1").arg(to)
                  : tr("Sending this computer's microphone to %1").arg(to)
    );
    updateActions();
  });
  connect(m_sender.get(), &StreamSender::stopped, this, [this](const QString &reason) {
    qInfo().noquote() << "sending" << (isSound() ? "sound" : "microphone") << "stopped:" << reason;
    // could not reach this address: try the next one, such as Wi-Fi after a cable
    if (!m_streaming && !m_stopping && !m_hostsToTry.isEmpty()) {
      QMetaObject::invokeMethod(this, &AudioRouteController::tryNextHost, Qt::QueuedConnection);
      return;
    }

    m_streaming = false;
    updateActions();
    if (m_stopping) {
      Q_EMIT notify(isSound() ? tr("Stopped sending sound") : tr("Stopped sending the microphone"));
      return;
    }

    // anything but the user stopping it must be visible: notifications may be turned off
    auto *box = new QMessageBox(
        QMessageBox::Warning, title(),
        isSound() ? tr("Sending sound stopped: %1").arg(reason) : tr("Sending the microphone stopped: %1").arg(reason),
        QMessageBox::Ok, m_window
    );
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
  });
  connect(
      m_sender.get(), &StreamSender::approvalNeeded, this,
      [this](const QByteArray &fingerprint, const QString &to) {
        const auto question = tr("The computer at %1 has not been paired with this one.").arg(to);
        m_sender->resolveApproval(askToTrust(m_window, fingerprint, question));
      }
  );

  updateActions();
}

AudioRouteController::~AudioRouteController() = default;

bool AudioRouteController::isSound() const
{
  return m_kind == SessionKind::Sound;
}

QString AudioRouteController::title() const
{
  return isSound() ? tr("Send Sound") : tr("Send Microphone");
}

void AudioRouteController::chooseAndSend()
{
  const auto &targetKey = isSound() ? Settings::Stream::SoundTarget : Settings::Stream::MicTarget;

  QDialog dialog(m_window);
  dialog.setWindowTitle(title());

  auto *target = new QComboBox(&dialog);
  target->setEditable(true);
  target->addItems(m_targets());
  if (const auto last = Settings::value(targetKey).toString(); !last.isEmpty()) {
    target->setCurrentText(last);
  }
  target->setToolTip(tr("The computer to play on. Hopflow must be running there."));

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  auto *ok = buttons->button(QDialogButtonBox::Ok);
  ok->setText(tr("Send"));
  ok->setEnabled(!target->currentText().trimmed().isEmpty());
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  connect(target, &QComboBox::currentTextChanged, ok, [ok](const QString &text) {
    ok->setEnabled(!text.trimmed().isEmpty());
  });

  auto *form = new QFormLayout(&dialog);
  form->addRow(tr("Play on:"), target);
  QCheckBox *mute = nullptr;
  if (isSound()) {
    mute = new QCheckBox(tr("Mute this computer while sending"), &dialog);
    mute->setChecked(Settings::value(Settings::Stream::MuteLocalSound).toBool());
    form->addRow(QString(), mute);
  }
  form->addRow(buttons);

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  Settings::setValue(targetKey, target->currentText().trimmed());
  if (mute) {
    Settings::setValue(Settings::Stream::MuteLocalSound, mute->isChecked());
  }
  send(target->currentText().trimmed());
}

void AudioRouteController::send(const QString &target)
{
  // a list of addresses is tried in order, like the server addresses of a client
  m_hostsToTry = splitHosts(target);
  m_streaming = false;
  tryNextHost();
}

void AudioRouteController::tryNextHost()
{
  if (m_hostsToTry.isEmpty()) {
    return;
  }

  auto audio = createSource();
  if (!audio) {
    m_hostsToTry.clear();
    QMessageBox::information(
        m_window, title(),
        isSound() ? tr("Sending sound is not supported on this computer yet.")
                  : tr("Sending the microphone is not supported on this computer yet.")
    );
    return;
  }

  const auto tls = localTls();
  if (!tls) {
    m_hostsToTry.clear();
    QMessageBox::warning(m_window, title(), tr("Sending needs a TLS certificate, which could not be created."));
    return;
  }

  const auto host = m_hostsToTry.takeFirst();
  qInfo().noquote() << "connecting to" << host << "on port" << streamPort() << "to send"
                    << (isSound() ? "sound" : "microphone");
  Preset preset;
  preset.kind = m_kind;
  m_sender->start(host, streamPort(), *tls, preset, nullptr, std::move(audio));
  updateActions();
}

void AudioRouteController::stopSending()
{
  m_hostsToTry.clear();
  // stopped() is emitted from inside stop(), so it knows the user asked for it
  m_stopping = true;
  m_sender->stop();
  m_stopping = false;
  updateActions();
}

void AudioRouteController::updateActions()
{
  const bool sending = m_sender->isActive();
  m_sendAction->setEnabled(!sending);
  m_stopAction->setEnabled(sending);
}

std::unique_ptr<IAudioSource> AudioRouteController::createSource() const
{
  if (isSound()) {
    return createSystemAudioSource(Settings::value(Settings::Stream::MuteLocalSound).toBool(), AudioLatency::Low);
  }
  return createMicrophoneSource();
}
