/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "stream/StreamConnection.h"

#include "stream/StreamPeers.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QSslServer>

namespace hopflow::stream {

namespace {

QByteArray peerFingerprint(const QSslSocket *socket)
{
  const auto certificate = socket->peerCertificate();
  return certificate.isNull() ? QByteArray() : certificate.digest(QCryptographicHash::Sha256);
}

QString describe(const QSslSocket *socket)
{
  return socket->peerAddress().toString();
}

} // namespace

//
// StreamConnection
//

StreamConnection::StreamConnection(QSslSocket *socket, QObject *parent) : QObject(parent), m_socket(socket)
{
  socket->setParent(this);
  socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
  connect(socket, &QSslSocket::readyRead, this, &StreamConnection::readFrames);
  connect(socket, &QSslSocket::disconnected, this, [this] { finish(tr("the other computer disconnected")); });
  connect(socket, &QSslSocket::errorOccurred, this, [this] {
    if (m_socket) {
      finish(m_socket->errorString());
    }
  });

  // frames may already be waiting from before this object existed
  if (socket->bytesAvailable() > 0) {
    QMetaObject::invokeMethod(this, &StreamConnection::readFrames, Qt::QueuedConnection);
  }
}

StreamConnection::~StreamConnection()
{
  if (m_socket) {
    m_socket->disconnect(this);
  }
}

void StreamConnection::send(const Frame &frame)
{
  if (m_socket && !m_finished) {
    m_socket->write(encodeFrame(frame));
  }
}

uint64_t StreamConnection::backlog() const
{
  return m_socket ? static_cast<uint64_t>(m_socket->bytesToWrite() + m_socket->encryptedBytesToWrite()) : 0;
}

QString StreamConnection::peerAddress() const
{
  return m_socket ? describe(m_socket) : QString();
}

void StreamConnection::close()
{
  if (m_socket && !m_finished) {
    send(Frame{FrameType::Bye, 0, {}});
    m_socket->flush();
    m_socket->disconnectFromHost();
  }
  finish(tr("stopped"));
}

void StreamConnection::readFrames()
{
  if (!m_socket || m_finished) {
    return;
  }

  m_parser.feed(m_socket->readAll());
  while (auto frame = m_parser.next()) {
    if (frame->type == FrameType::Bye) {
      m_socket->disconnectFromHost();
      finish(tr("the other computer stopped sharing"));
      return;
    }
    Q_EMIT frameReceived(*frame);
    if (m_finished) {
      return;
    }
  }

  if (m_parser.hasError()) {
    m_socket->abort();
    finish(m_parser.error());
  }
}

void StreamConnection::finish(const QString &reason)
{
  if (m_finished) {
    return;
  }
  m_finished = true;
  Q_EMIT closed(reason);
}

//
// PeerGate
//

PeerGate::PeerGate(QStringList databases, QString approvedDatabase, QObject *parent)
    : QObject(parent),
      m_databases(std::move(databases)),
      m_approvedDatabase(std::move(approvedDatabase))
{
}

void PeerGate::check(QSslSocket *socket)
{
  socket->setParent(this);
  const auto fingerprint = peerFingerprint(socket);
  if (fingerprint.isEmpty()) {
    socket->abort();
    socket->deleteLater();
    Q_EMIT rejected(tr("the other computer did not identify itself"));
    return;
  }

  if (isPeerTrusted(fingerprint, m_databases)) {
    accept(socket);
    return;
  }

  // one stranger at a time; a newer one replaces an unanswered one
  if (m_waiting) {
    m_waiting->abort();
    m_waiting->deleteLater();
  }
  m_waiting = socket;
  connect(socket, &QSslSocket::disconnected, this, [this, socket] {
    if (m_waiting == socket) {
      m_waiting = nullptr;
    }
  });
  Q_EMIT approvalNeeded(fingerprint, describe(socket));
}

void PeerGate::resolve(bool accepted)
{
  QSslSocket *socket = m_waiting;
  m_waiting = nullptr;
  if (socket == nullptr) {
    return;
  }

  if (!accepted) {
    socket->abort();
    socket->deleteLater();
    Q_EMIT rejected(tr("not allowed"));
    return;
  }

  trustPeer(peerFingerprint(socket), m_approvedDatabase);
  accept(socket);
}

void PeerGate::accept(QSslSocket *socket)
{
  socket->disconnect(this);
  Q_EMIT accepted(new StreamConnection(socket));
}

//
// StreamServer
//

StreamServer::StreamServer(QStringList databases, QString approvedDatabase, QObject *parent)
    : QObject(parent),
      m_gate(std::move(databases), std::move(approvedDatabase))
{
  connect(&m_gate, &PeerGate::accepted, this, &StreamServer::connectionReady);
  connect(&m_gate, &PeerGate::approvalNeeded, this, &StreamServer::approvalNeeded);
}

StreamServer::~StreamServer() = default;

bool StreamServer::listen(quint16 port, const QSslConfiguration &config)
{
  close();
  m_server = new QSslServer(this);
  m_server->setSslConfiguration(config);
  connect(m_server, &QSslServer::pendingConnectionAvailable, this, [this] {
    while (m_server && m_server->hasPendingConnections()) {
      if (auto *socket = qobject_cast<QSslSocket *>(m_server->nextPendingConnection())) {
        m_gate.check(socket);
      }
    }
  });
  // peers use self-signed certificates; trust is decided by fingerprint once the handshake is done
  connect(m_server, &QSslServer::sslErrors, this, [](QSslSocket *socket, const QList<QSslError> &errors) {
    for (const auto &sslError : errors) {
      qInfo().noquote() << "screen sharing: accepting TLS error, checked by fingerprint instead:"
                        << sslError.errorString();
    }
    socket->ignoreSslErrors();
  });
  connect(m_server, &QSslServer::errorOccurred, this, [this](QSslSocket *socket, QAbstractSocket::SocketError) {
    const auto reason = socket ? socket->errorString() : QString();
    qWarning().noquote() << "screen sharing: incoming connection failed:" << reason;
    Q_EMIT error(tr("a computer could not connect to share its screen: %1").arg(reason));
  });

  if (!m_server->listen(QHostAddress::Any, port)) {
    Q_EMIT error(tr("cannot listen on port %1: %2").arg(port).arg(m_server->errorString()));
    close();
    return false;
  }
  return true;
}

void StreamServer::close()
{
  if (m_server) {
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
  }
}

bool StreamServer::isListening() const
{
  return m_server && m_server->isListening();
}

quint16 StreamServer::port() const
{
  return m_server ? m_server->serverPort() : 0;
}

//
// StreamClient
//

StreamClient::StreamClient(QStringList databases, QString approvedDatabase, QObject *parent)
    : QObject(parent),
      m_gate(std::move(databases), std::move(approvedDatabase))
{
  connect(&m_gate, &PeerGate::accepted, this, &StreamClient::connected);
  connect(&m_gate, &PeerGate::approvalNeeded, this, &StreamClient::approvalNeeded);
  connect(&m_gate, &PeerGate::rejected, this, &StreamClient::failed);
}

bool preferOpenSslBackend()
{
  // the main connection uses OpenSSL; use it here too rather than Schannel or
  // Secure Transport, so every platform behaves the same
  // asking for the active backend would pick a default and lock it in, so set it first
  const auto backend = QStringLiteral("openssl");
  if (QSslSocket::availableBackends().contains(backend) && QSslSocket::setActiveBackend(backend)) {
    return true;
  }
  qWarning().noquote() << "screen sharing: OpenSSL is not available, using" << QSslSocket::activeBackend();
  return false;
}

void StreamClient::connectTo(const QString &host, quint16 port, const QSslConfiguration &config)
{
  auto *socket = new QSslSocket(this);
  socket->setSslConfiguration(config);
  // trust comes from the fingerprint, checked once the handshake is done
  connect(socket, &QSslSocket::sslErrors, socket, [socket] { socket->ignoreSslErrors(); });
  connect(socket, &QSslSocket::encrypted, this, [this, socket] {
    socket->disconnect(this);
    m_gate.check(socket);
  });
  connect(socket, &QSslSocket::errorOccurred, this, [this, socket] {
    Q_EMIT failed(socket->errorString());
    socket->deleteLater();
  });
  socket->connectToHostEncrypted(host, port);
}

} // namespace hopflow::stream
