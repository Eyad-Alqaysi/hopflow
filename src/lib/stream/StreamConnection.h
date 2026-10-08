/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "stream/StreamProtocol.h"

#include <QObject>
#include <QPointer>
#include <QSslConfiguration>
#include <QSslSocket>

class QSslServer;

namespace hopflow::stream {

//! An encrypted, trusted connection carrying stream frames
class StreamConnection : public QObject
{
  Q_OBJECT

public:
  //! Takes ownership of \p socket, which must already be encrypted
  explicit StreamConnection(QSslSocket *socket, QObject *parent = nullptr);
  ~StreamConnection() override;

  void send(const Frame &frame);

  //! Bytes queued but not yet sent, used to detect a slow network
  uint64_t backlog() const;

  QString peerAddress() const;

  //! Send Bye and close
  void close();

Q_SIGNALS:
  void frameReceived(const hopflow::stream::Frame &frame);
  void closed(const QString &reason);

private:
  void readFrames();
  void finish(const QString &reason);

  QPointer<QSslSocket> m_socket;
  FrameParser m_parser;
  bool m_finished = false;
};

//! Decides whether a newly connected peer may share screens
/*!
Known peers are accepted straight away. For an unknown peer it emits
approvalNeeded and waits for resolve() with the user's answer.
*/
class PeerGate : public QObject
{
  Q_OBJECT

public:
  PeerGate(QStringList databases, QString approvedDatabase, QObject *parent = nullptr);

  //! Check \p socket, which is encrypted; takes ownership of it
  void check(QSslSocket *socket);

  //! Answer for the peer waiting for approval
  void resolve(bool accept);

Q_SIGNALS:
  void accepted(hopflow::stream::StreamConnection *connection);
  void approvalNeeded(const QByteArray &fingerprint, const QString &address);
  void rejected(const QString &reason);

private:
  void accept(QSslSocket *socket);

  QStringList m_databases;
  QString m_approvedDatabase;
  QPointer<QSslSocket> m_waiting;
};

//! Listens for computers that want to show their screen here
class StreamServer : public QObject
{
  Q_OBJECT

public:
  StreamServer(QStringList databases, QString approvedDatabase, QObject *parent = nullptr);
  ~StreamServer() override;

  bool listen(quint16 port, const QSslConfiguration &config);
  void close();
  bool isListening() const;
  quint16 port() const;

  PeerGate *gate()
  {
    return &m_gate;
  }

Q_SIGNALS:
  void connectionReady(hopflow::stream::StreamConnection *connection);
  void approvalNeeded(const QByteArray &fingerprint, const QString &address);
  void error(const QString &message);

private:
  QSslServer *m_server = nullptr;
  PeerGate m_gate;
};

//! Connects to a computer to show this screen there
class StreamClient : public QObject
{
  Q_OBJECT

public:
  StreamClient(QStringList databases, QString approvedDatabase, QObject *parent = nullptr);

  void connectTo(const QString &host, quint16 port, const QSslConfiguration &config);

  PeerGate *gate()
  {
    return &m_gate;
  }

Q_SIGNALS:
  void connected(hopflow::stream::StreamConnection *connection);
  void approvalNeeded(const QByteArray &fingerprint, const QString &address);
  void failed(const QString &message);

private:
  PeerGate m_gate;
};

} // namespace hopflow::stream
