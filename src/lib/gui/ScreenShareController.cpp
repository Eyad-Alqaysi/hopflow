/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenShareController.h"

#include "StreamSupport.h"
#include "common/Settings.h"
#include "dialogs/ShareScreenDialog.h"
#include "stream/StreamPeers.h"
#include "stream/StreamReceiver.h"
#include "stream/StreamSender.h"
#include "stream/TestPattern.h"
#include "widgets/ScreenViewerWindow.h"

#include <QAction>
#include <QCursor>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>
#include <QSslSocket>

using namespace hopflow::stream;
using namespace hopflow::gui;

namespace {

std::unique_ptr<IVideoSource> videoSource()
{
  if (auto source = createScreenVideoSource()) {
    return source;
  }

  // for testing the pipeline where capture is not written yet
  if (qEnvironmentVariableIsSet("HOPFLOW_STREAM_TEST_PATTERN")) {
    const auto *screen = QGuiApplication::primaryScreen();
    return std::make_unique<TestPatternSource>(
        screen ? screen->size() * screen->devicePixelRatio() : QSize(1920, 1080)
    );
  }
  return nullptr;
}

} // namespace

ScreenShareController::ScreenShareController(QWidget *window, std::function<QStringList()> targets, QObject *parent)
    : QObject(parent),
      m_window(window),
      m_targets(std::move(targets)),
      m_shareAction(new QAction(tr("Share Screen…"), this)),
      m_stopAction(new QAction(tr("Stop Sharing Screen"), this)),
      m_server(std::make_unique<StreamServer>(trustedDatabases(), Settings::tlsTrustedScreensDb())),
      m_sender(makeSender())
{
  connect(m_shareAction, &QAction::triggered, this, &ScreenShareController::chooseAndShare);
  connect(m_stopAction, &QAction::triggered, this, &ScreenShareController::stopSharing);

  connect(m_server.get(), &StreamServer::connectionReady, this, &ScreenShareController::onViewerConnected);
  connect(m_server.get(), &StreamServer::error, this, [this](const QString &message) {
    qWarning().noquote() << "screen sharing:" << message;
    Q_EMIT notify(message);
  });
  connect(
      m_server.get(), &StreamServer::approvalNeeded, this,
      [this](const QByteArray &fingerprint, const QString &from) {
        const auto question = tr("A computer at %1 wants to send its screen or sound here.").arg(from);
        m_server->gate()->resolve(askToTrust(m_window, fingerprint, question));
      }
  );

  connect(m_sender.get(), &StreamSender::streaming, this, [this](const QString &viewer) {
    qInfo().noquote() << "screen sharing connected to" << viewer;
    m_hostsToTry.clear();
    m_streaming = true;
    Q_EMIT notify(tr("Sharing this screen with %1").arg(viewer));
    updateActions();
  });
  connect(m_sender.get(), &StreamSender::stopped, this, [this](const QString &reason) {
    qInfo().noquote() << "screen sharing stopped:" << reason;
    // could not reach this address: try the next one, such as Wi-Fi after a cable
    if (!m_streaming && !m_hostsToTry.isEmpty()) {
      QMetaObject::invokeMethod(this, &ScreenShareController::tryNextHost, Qt::QueuedConnection);
      return;
    }

    m_streaming = false;
    updateActions();
    if (reason == tr("stopped")) {
      Q_EMIT notify(tr("Screen sharing stopped"));
      return;
    }

    // anything but the user stopping it must be visible: notifications may be turned off
    auto *box = new QMessageBox(
        QMessageBox::Warning, tr("Share Screen"), tr("Screen sharing stopped: %1").arg(reason), QMessageBox::Ok,
        m_window
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
  applySettings();
}

ScreenShareController::~ScreenShareController()
{
  for (const auto &viewer : {m_viewer, m_extendViewer}) {
    if (viewer) {
      viewer->disconnect(this);
      delete viewer;
    }
  }
}

void ScreenShareController::applySettings()
{
  const bool allow = Settings::value(Settings::Stream::AllowViewing).toBool();
  if (!allow) {
    m_server->close();
    return;
  }

  if (m_server->isListening() && m_server->port() == streamPort()) {
    return;
  }

  if (const auto tls = localTls()) {
    if (m_server->listen(streamPort(), *tls)) {
      qInfo().noquote() << "accepting shared screens on port" << streamPort()
                        << "(TLS:" << QSslSocket::activeBackend() + QStringLiteral(")");
    }
  } else {
    Q_EMIT notify(tr("Screen sharing needs a TLS certificate, which could not be created"));
  }
}

void ScreenShareController::chooseAndShare()
{
  ShareScreenDialog dialog(m_targets(), m_window);
  if (dialog.exec() == QDialog::Accepted) {
    share(dialog.target(), dialog.preset());
  }
}

void ScreenShareController::share(const QString &target, const Preset &preset)
{
  // a list of addresses is tried in order, like the server addresses of a client
  m_hostsToTry = splitHosts(target);
  m_preset = preset;
  m_streaming = false;
  tryNextHost();
}

void ScreenShareController::tryNextHost()
{
  if (m_hostsToTry.isEmpty()) {
    return;
  }

  auto video = videoSource();
  if (!video) {
    m_hostsToTry.clear();
    QMessageBox::information(m_window, tr("Share Screen"), tr("Screen capture is not available on this computer yet."));
    return;
  }

  const auto tls = localTls();
  if (!tls) {
    m_hostsToTry.clear();
    Q_EMIT notify(tr("Screen sharing needs a TLS certificate, which could not be created"));
    return;
  }

  const auto host = m_hostsToTry.takeFirst();
  qInfo().noquote() << "sharing this screen with" << host << "on port" << streamPort();
  m_sender->start(host, streamPort(), *tls, m_preset, std::move(video), createSystemAudioSource());
  updateActions();
}

void ScreenShareController::stopSharing()
{
  m_hostsToTry.clear();
  m_sender->stop();
  updateActions();
}

void ScreenShareController::updateActions()
{
  const bool sharing = m_sender->isActive();
  m_shareAction->setEnabled(!sharing);
  m_stopAction->setEnabled(sharing);
}

void ScreenShareController::onViewerConnected(StreamConnection *connection)
{
  // what it carries is only known from its hello
  qInfo().noquote() << "stream connected from" << connection->peerAddress();
  auto receiver = std::make_unique<StreamReceiver>(connection);
  auto *raw = receiver.get();
  m_pending.push_back(std::move(receiver));

  connect(raw, &StreamReceiver::started, this, [this, raw](const QString &name) { onSessionStarted(raw, name); });
  connect(raw, &StreamReceiver::stopped, this, [this, raw](const QString &reason) {
    // one that closed before its hello is dropped once its signal has returned
    QMetaObject::invokeMethod(
        this,
        [this, raw] {
          std::erase_if(m_pending, [raw](const auto &pending) { return pending.get() == raw; });
        },
        Qt::QueuedConnection
    );

    const auto kind = raw->kind();
    qInfo().noquote() << "stream from" << raw->computerName() << "stopped:" << reason;
    if (auto *viewer = viewerFor(kind)) {
      viewer->showMessage(tr("Sharing stopped: %1").arg(reason));
    } else if (kind == SessionKind::Sound || kind == SessionKind::Microphone) {
      Q_EMIT notify(tr("%1 stopped sending its %2").arg(raw->computerName(), soundName(kind)));
    }
  });
  connect(raw, &StreamReceiver::audioUnavailable, this, [this, raw](const QString &reason) {
    auto *box = new QMessageBox(
        QMessageBox::Warning, tr("Sound"),
        tr("The %1 from %2 cannot be played: %3").arg(soundName(raw->kind()), raw->computerName(), reason),
        QMessageBox::Ok, m_window
    );
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
  });
}

void ScreenShareController::onSessionStarted(StreamReceiver *receiver, const QString &name)
{
  const auto it = std::find_if(m_pending.begin(), m_pending.end(), [receiver](const auto &pending) {
    return pending.get() == receiver;
  });
  if (it == m_pending.end()) {
    return;
  }

  // one session of each kind at a time; the newest replaces the old one
  const auto kind = receiver->kind();
  auto &slot = m_receivers[kind];
  if (slot) {
    slot->disconnect(this);
    slot->stop();
  }
  slot = std::move(*it);
  m_pending.erase(it);

  switch (kind) {
  case SessionKind::ScreenShare:
  case SessionKind::ExtendDisplay:
    showViewer(receiver, name);
    break;
  case SessionKind::Sound:
  case SessionKind::Microphone:
    Q_EMIT notify(tr("Playing the %1 of %2").arg(soundName(kind), name));
    break;
  }
}

void ScreenShareController::showViewer(StreamReceiver *receiver, const QString &name)
{
  const auto kind = receiver->kind();
  auto &viewer = kind == SessionKind::ExtendDisplay ? m_extendViewer : m_viewer;
  if (!viewer) {
    viewer = new ScreenViewerWindow(
        name, kind == SessionKind::ExtendDisplay ? ScreenViewerWindow::Mode::ExtendedDisplay
                                                 : ScreenViewerWindow::Mode::SharedScreen
    );
    viewer->setAttribute(Qt::WA_DeleteOnClose);
    connect(viewer, &ScreenViewerWindow::closed, this, [this, kind] {
      if (const auto &slot = m_receivers[kind]) {
        slot->stop();
      }
    });
  }
  connect(receiver, &StreamReceiver::frameReady, viewer, &ScreenViewerWindow::showFrame);

  if (kind == SessionKind::ExtendDisplay) {
    // the sender makes a display matching this monitor, so it fills it without scaling
    auto *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
      screen = QGuiApplication::primaryScreen();
    }
    const auto ratio = screen->devicePixelRatio();
    receiver->sendDisplayInfo(
        DisplayInfo{
            static_cast<uint16_t>(qRound(screen->size().width() * ratio)),
            static_cast<uint16_t>(qRound(screen->size().height() * ratio)),
            static_cast<uint16_t>(qRound(screen->refreshRate())), static_cast<uint16_t>(qRound(ratio * 100))
        }
    );
    viewer->setScreen(screen);
    viewer->showFullScreen();
  } else {
    viewer->show();
  }
  viewer->raise();
}

ScreenViewerWindow *ScreenShareController::viewerFor(SessionKind kind) const
{
  switch (kind) {
  case SessionKind::ScreenShare:
    return m_viewer;
  case SessionKind::ExtendDisplay:
    return m_extendViewer;
  default:
    return nullptr;
  }
}

QString ScreenShareController::soundName(SessionKind kind)
{
  return kind == SessionKind::Microphone ? tr("microphone") : tr("sound");
}
