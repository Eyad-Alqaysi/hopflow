/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenShareController.h"

#include "TlsUtility.h"
#include "common/Settings.h"
#include "dialogs/ShareScreenDialog.h"
#include "net/SecureUtils.h"
#include "stream/StreamPeers.h"
#include "stream/StreamReceiver.h"
#include "stream/StreamSender.h"
#include "stream/TestPattern.h"
#include "widgets/ScreenViewerWindow.h"

#include <QAbstractButton>
#include <QAction>
#include <QGuiApplication>
#include <QMessageBox>
#include <QScreen>

using namespace hopflow::stream;

namespace {

std::optional<QSslConfiguration> localTls()
{
  using namespace deskflow::gui;
  if (!TlsUtility::isCertValid() && !TlsUtility::generateCertificate()) {
    return std::nullopt;
  }
  return tlsConfiguration(Settings::value(Settings::Security::Certificate).toString());
}

quint16 streamPort()
{
  return static_cast<quint16>(Settings::value(Settings::Stream::Port).toUInt());
}

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
      m_sender(
          std::make_unique<StreamSender>(
              Settings::value(Settings::Core::ComputerName).toString(), trustedDatabases(),
              Settings::tlsTrustedScreensDb()
          )
      )
{
  connect(m_shareAction, &QAction::triggered, this, &ScreenShareController::chooseAndShare);
  connect(m_stopAction, &QAction::triggered, this, &ScreenShareController::stopSharing);

  connect(m_server.get(), &StreamServer::connectionReady, this, &ScreenShareController::onViewerConnected);
  connect(m_server.get(), &StreamServer::error, this, &ScreenShareController::notify);
  connect(
      m_server.get(), &StreamServer::approvalNeeded, this,
      [this](const QByteArray &fingerprint, const QString &from) {
        const auto question = tr("A computer at %1 wants to show its screen here.").arg(from);
        m_server->gate()->resolve(askToTrust(fingerprint, question));
      }
  );

  connect(m_sender.get(), &StreamSender::streaming, this, [this](const QString &viewer) {
    m_hostsToTry.clear();
    m_streaming = true;
    Q_EMIT notify(tr("Sharing this screen with %1").arg(viewer));
    updateActions();
  });
  connect(m_sender.get(), &StreamSender::stopped, this, [this](const QString &reason) {
    // could not reach this address: try the next one, such as Wi-Fi after a cable
    if (!m_streaming && !m_hostsToTry.isEmpty()) {
      QMetaObject::invokeMethod(this, &ScreenShareController::tryNextHost, Qt::QueuedConnection);
      return;
    }
    m_streaming = false;
    Q_EMIT notify(tr("Screen sharing stopped: %1").arg(reason));
    updateActions();
  });
  connect(
      m_sender.get(), &StreamSender::approvalNeeded, this,
      [this](const QByteArray &fingerprint, const QString &to) {
        const auto question = tr("The computer at %1 has not been paired with this one.").arg(to);
        m_sender->resolveApproval(askToTrust(fingerprint, question));
      }
  );

  updateActions();
  applySettings();
}

ScreenShareController::~ScreenShareController()
{
  if (m_viewer) {
    m_viewer->disconnect(this);
    delete m_viewer;
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
    m_server->listen(streamPort(), *tls);
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
  m_hostsToTry.clear();
  for (const auto &host : target.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
    if (const auto trimmed = host.trimmed(); !trimmed.isEmpty()) {
      m_hostsToTry.append(trimmed);
    }
  }
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
  // one screen at a time; the newest replaces the old one
  if (m_receiver) {
    m_receiver->stop();
    m_receiver.reset();
  }

  m_receiver = std::make_unique<StreamReceiver>(connection);
  connect(m_receiver.get(), &StreamReceiver::started, this, [this](const QString &name) {
    if (!m_viewer) {
      m_viewer = new ScreenViewerWindow(name);
      m_viewer->setAttribute(Qt::WA_DeleteOnClose);
      connect(m_viewer, &ScreenViewerWindow::closed, this, [this] {
        if (m_receiver) {
          m_receiver->stop();
        }
      });
    }
    m_viewer->show();
    m_viewer->raise();
  });
  connect(m_receiver.get(), &StreamReceiver::frameReady, this, [this](const QImage &image) {
    if (m_viewer) {
      m_viewer->showFrame(image);
    }
  });
  connect(m_receiver.get(), &StreamReceiver::stopped, this, [this](const QString &reason) {
    if (m_viewer) {
      m_viewer->showMessage(tr("Sharing stopped: %1").arg(reason));
    }
  });
}

bool ScreenShareController::askToTrust(const QByteArray &fingerprint, const QString &question)
{
  QMessageBox box(m_window);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(tr("Screen Sharing"));
  box.setText(question);
  box.setInformativeText(
      tr("Only allow it if this fingerprint matches the one shown in %1 on that computer:\n\n%2")
          .arg(QGuiApplication::applicationDisplayName(), deskflow::formatSSLFingerprintColumns(fingerprint))
  );
  box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  box.setDefaultButton(QMessageBox::No);
  box.button(QMessageBox::Yes)->setText(tr("Allow"));
  box.button(QMessageBox::No)->setText(tr("Don't Allow"));
  return box.exec() == QMessageBox::Yes;
}
