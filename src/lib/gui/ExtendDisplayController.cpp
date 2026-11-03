/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ExtendDisplayController.h"

#include "StreamSupport.h"
#include "common/Settings.h"
#include "stream/MediaInterfaces.h"
#include "stream/StreamSender.h"

#include <QAction>
#include <QInputDialog>
#include <QMessageBox>
#include <QThread>

#include <algorithm>

using namespace hopflow::stream;
using namespace hopflow::gui;

ExtendDisplayController::ExtendDisplayController(
    QWidget *window, std::function<QStringList()> targets, std::function<bool()> pauseSharing,
    std::function<void()> resumeSharing, QObject *parent
)
    : QObject(parent),
      m_window(window),
      m_targets(std::move(targets)),
      m_pauseSharing(std::move(pauseSharing)),
      m_resumeSharing(std::move(resumeSharing)),
      m_extendAction(new QAction(tr("Extend Display To…"), this)),
      m_stopAction(new QAction(tr("Stop Extending Display"), this)),
      m_sender(makeSender())
{
  connect(m_extendAction, &QAction::triggered, this, &ExtendDisplayController::chooseAndExtend);
  connect(m_stopAction, &QAction::triggered, this, &ExtendDisplayController::stopExtending);

  connect(m_sender.get(), &StreamSender::streaming, this, [this](const QString &viewer) {
    qInfo().noquote() << "extended display connected to" << viewer;
    m_hostsToTry.clear();
    m_streaming = true;
  });
  connect(m_sender.get(), &StreamSender::displayInfoReceived, this, &ExtendDisplayController::onDisplayInfo);
  connect(m_sender.get(), &StreamSender::stopped, this, &ExtendDisplayController::onStopped);
  connect(
      m_sender.get(), &StreamSender::approvalNeeded, this,
      [this](const QByteArray &fingerprint, const QString &to) {
        const auto question = tr("The computer at %1 has not been paired with this one.").arg(to);
        m_sender->resolveApproval(askToTrust(m_window, fingerprint, question));
      }
  );

  updateActions();
}

ExtendDisplayController::~ExtendDisplayController() = default;

void ExtendDisplayController::chooseAndExtend()
{
  auto targets = m_targets();
  const auto last = Settings::value(Settings::Stream::ExtendTarget).toString();
  if (!last.isEmpty() && !targets.contains(last)) {
    targets.prepend(last);
  }

  bool accepted = false;
  const auto current = std::max<qsizetype>(targets.indexOf(last), 0);
  const auto choice = QInputDialog::getItem(
      m_window, tr("Extend Display"), tr("Add a display to this computer, shown on the monitor of:"), targets, current,
      true, &accepted
  );
  const auto target = choice.trimmed();
  if (!accepted || target.isEmpty()) {
    return;
  }

  Settings::setValue(Settings::Stream::ExtendTarget, target);
  extend(target);
}

void ExtendDisplayController::extend(const QString &target)
{
  // a list of addresses is tried in order, like the server addresses of a client
  const auto hosts = splitHosts(target);
  if (hosts.isEmpty()) {
    showError(tr("\"%1\" is not the address of a computer.").arg(target));
    return;
  }

  m_display = createVirtualDisplay();
  if (!m_display) {
    showError(tr("Extending the display is not available on this computer yet."));
    return;
  }

  m_hostsToTry = hosts;
  m_streaming = false;
  tryNextHost();
}

void ExtendDisplayController::tryNextHost()
{
  if (m_hostsToTry.isEmpty()) {
    return;
  }

  const auto tls = localTls();
  if (!tls) {
    m_hostsToTry.clear();
    showError(tr("Extending the display needs a TLS certificate, which could not be created."));
    return;
  }

  Preset preset;
  preset.kind = SessionKind::ExtendDisplay;
  // the display is made to the viewer's size and refresh rate, up to 60 Hz, so capture all of it
  preset.resolution = Resolution::Native;
  preset.frameRate = FrameRate::Native;
  preset.audio = false;

  const auto host = m_hostsToTry.takeFirst();
  qInfo().noquote() << "extending the display to" << host << "on port" << streamPort();
  // the video starts once the viewer has said what its monitor is like
  m_sender->start(host, streamPort(), *tls, preset, nullptr, nullptr);
  updateActions();
}

void ExtendDisplayController::stopExtending()
{
  m_hostsToTry.clear();
  m_sender->stop();
}

void ExtendDisplayController::onDisplayInfo(const DisplayInfo &info)
{
  if (!m_display || m_creating || !m_display->displayId().isEmpty()) {
    qWarning() << "ignoring a second description of the viewer's monitor";
    return;
  }

  qInfo() << "viewer monitor:" << info.width << "x" << info.height << "at" << info.refreshHz << "Hz,"
          << info.scalePercent << "%";

  if (m_display->createsSlowly()) {
    createInBackground(info);
    return;
  }
  onDisplayCreated(m_display->create(info));
}

void ExtendDisplayController::createInBackground(const DisplayInfo &info)
{
  m_creating = true;
  updateActions();
  Q_EMIT notify(tr("Adding the extended display, which can take a few seconds…"));

  auto created = std::make_shared<bool>(false);
  auto *worker = QThread::create([display = m_display, info, created]() mutable {
    *created = display->create(info);
    // when the session ended meanwhile this is the last owner, so the display goes away here
    display.reset();
  });
  const std::weak_ptr<IVirtualDisplay> display = m_display;
  connect(worker, &QThread::finished, this, [this, display, created] {
    m_creating = false;
    if (const auto current = display.lock(); current && current == m_display) {
      onDisplayCreated(*created);
    }
    updateActions();
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

void ExtendDisplayController::onDisplayCreated(bool created)
{
  if (!created) {
    const auto reason = m_display->error();
    m_sender->stop();
    showError(tr("The extended display could not be created: %1").arg(reason));
    return;
  }

  auto video = createScreenVideoSource(m_display->displayId());
  if (!video) {
    m_display->destroy();
    m_sender->stop();
    showError(tr("The extended display cannot be captured on this computer yet."));
    return;
  }

  if (m_pauseSharing()) {
    m_pausedSharing = true;
    Q_EMIT notify(tr("Mouse and keyboard sharing is paused while the display is extended"));
  } else {
    Q_EMIT notify(tr("Extending this display"));
  }
  m_sender->startVideo(std::move(video));
}

void ExtendDisplayController::onStopped(const QString &reason)
{
  qInfo().noquote() << "extended display stopped:" << reason;
  // could not reach this address: try the next one, such as Wi-Fi after a cable
  if (!m_streaming && !m_hostsToTry.isEmpty()) {
    QMetaObject::invokeMethod(this, &ExtendDisplayController::tryNextHost, Qt::QueuedConnection);
    return;
  }

  const bool hadDisplay = m_display && !m_creating && !m_display->displayId().isEmpty();
  endSession();
  if (reason == tr("stopped")) {
    if (hadDisplay) {
      Q_EMIT notify(tr("Stopped extending the display"));
    }
    return;
  }
  showError(tr("Extending the display stopped: %1").arg(reason));
}

void ExtendDisplayController::endSession()
{
  m_streaming = false;
  m_display.reset();
  if (m_pausedSharing) {
    m_pausedSharing = false;
    m_resumeSharing();
  }
  updateActions();
}

void ExtendDisplayController::showError(const QString &message)
{
  // notifications may be turned off, so failures get a message box
  auto *box = new QMessageBox(QMessageBox::Warning, tr("Extend Display"), message, QMessageBox::Ok, m_window);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->open();
}

void ExtendDisplayController::updateActions()
{
  const bool extending = m_sender->isActive();
  m_extendAction->setEnabled(!extending && !m_creating);
  m_stopAction->setEnabled(extending);
}
