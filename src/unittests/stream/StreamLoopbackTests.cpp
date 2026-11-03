/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StreamLoopbackTests.h"

#include "net/SecureUtils.h"
#include "stream/AudioBuffer.h"
#include "stream/StreamPeers.h"
#include "stream/StreamReceiver.h"
#include "stream/StreamSender.h"
#include "stream/TestPattern.h"

#include <QSignalSpy>
#include <QSslCertificate>
#include <QTest>
#include <QTimer>

using namespace hopflow::stream;

namespace {

QByteArray fingerprintOf(const QString &pem)
{
  return QSslCertificate::fromPath(pem).first().digest(QCryptographicHash::Sha256);
}

//! A viewer listening on a free port, playing whatever connects
struct Viewer
{
  StreamServer server;
  std::unique_ptr<StreamReceiver> receiver;
  QList<QImage> frames;
  QString sender;
  QString stopReason;

  Viewer(const QStringList &trusted, const QString &approved) : server(trusted, approved)
  {
    QObject::connect(&server, &StreamServer::connectionReady, [this](StreamConnection *connection) {
      receiver = std::make_unique<StreamReceiver>(connection);
      QObject::connect(receiver.get(), &StreamReceiver::started, [this](const QString &name) { sender = name; });
      QObject::connect(receiver.get(), &StreamReceiver::frameReady, [this](const QImage &image) {
        frames.append(image);
      });
      QObject::connect(receiver.get(), &StreamReceiver::stopped, [this](const QString &reason) {
        stopReason = reason;
      });
    });
  }
};

//! Records the raw frames of whatever connects, without playing them
struct FrameRecorder
{
  StreamServer server;
  std::unique_ptr<StreamConnection> connection;
  QList<Frame> frames;

  FrameRecorder(const QStringList &trusted, const QString &approved) : server(trusted, approved)
  {
    QObject::connect(&server, &StreamServer::connectionReady, [this](StreamConnection *accepted) {
      connection.reset(accepted);
      QObject::connect(accepted, &StreamConnection::frameReceived, [this](const Frame &frame) {
        frames.append(frame);
      });
    });
  }

  QList<Frame> framesOf(FrameType type) const
  {
    QList<Frame> matching;
    std::copy_if(frames.begin(), frames.end(), std::back_inserter(matching), [type](const Frame &frame) {
      return frame.type == type;
    });
    return matching;
  }
};

//! Sends a steady Pcm16 tone in 10 ms packets, like routed sound
class ToneSource : public IAudioSource
{
public:
  void start(ConfigReady onConfig, PacketReady onPacket, Failed) override
  {
    onConfig(audioConfigFor(AudioCodec::Pcm16));
    QObject::connect(&m_timer, &QTimer::timeout, [this, onPacket] {
      const std::vector<float> samples(size_t(kPcmFramesPerPacket) * kAudioChannels, 0.25f);
      for (const auto &packet : m_packetizer.pack(samples.data(), kPcmFramesPerPacket)) {
        onPacket(packet);
      }
    });
    m_timer.start(10);
  }

  void stop() override
  {
    m_timer.stop();
    m_timer.disconnect();
  }

private:
  QTimer m_timer;
  Pcm16Packetizer m_packetizer;
};

Preset smallPreset()
{
  return Preset{Resolution::P720, FrameRate::Fps30, false};
}

} // namespace

void StreamLoopbackTests::initTestCase()
{
  QVERIFY(QSslSocket::supportsSsl());
  QVERIFY(m_dir.isValid());
  m_viewerPem = m_dir.filePath("viewer.pem");
  m_senderPem = m_dir.filePath("sender.pem");
  deskflow::generatePemSelfSignedCert(m_viewerPem);
  deskflow::generatePemSelfSignedCert(m_senderPem);
  m_viewerFingerprint = fingerprintOf(m_viewerPem);
  m_senderFingerprint = fingerprintOf(m_senderPem);
  QVERIFY(m_viewerFingerprint != m_senderFingerprint);
}

void StreamLoopbackTests::pairedComputersStream()
{
  const auto viewerDb = m_dir.filePath("paired-viewer-trusts");
  const auto senderDb = m_dir.filePath("paired-sender-trusts");
  QVERIFY(trustPeer(m_senderFingerprint, viewerDb));
  QVERIFY(trustPeer(m_viewerFingerprint, senderDb));

  Viewer viewer({viewerDb}, m_dir.filePath("unused"));
  QVERIFY(viewer.server.listen(0, *tlsConfiguration(m_viewerPem)));

  StreamSender sender(QStringLiteral("Test Mac"), {senderDb}, m_dir.filePath("unused"));
  QSignalSpy streaming(&sender, &StreamSender::streaming);
  sender.start(
      QStringLiteral("127.0.0.1"), viewer.server.port(), *tlsConfiguration(m_senderPem), smallPreset(),
      std::make_unique<TestPatternSource>(QSize(640, 400)), nullptr
  );

  QTRY_COMPARE_WITH_TIMEOUT(streaming.count(), 1, 5000);
  QTRY_VERIFY_WITH_TIMEOUT(viewer.frames.size() >= 5, 5000);
  QCOMPARE(viewer.sender, QStringLiteral("Test Mac"));
  // smaller than 720p, so sent at its own size
  QCOMPARE(viewer.frames.last().size(), QSize(640, 400));
  QVERIFY(sender.bitrate() > 0);
}

void StreamLoopbackTests::unknownSenderNeedsApproval_data()
{
  QTest::addColumn<bool>("accept");
  QTest::newRow("accepted") << true;
  QTest::newRow("refused") << false;
}

void StreamLoopbackTests::unknownSenderNeedsApproval()
{
  QFETCH(bool, accept);

  const auto senderDb = m_dir.filePath(QStringLiteral("approval-sender-trusts-%1").arg(accept));
  const auto approvedDb = m_dir.filePath(QStringLiteral("approval-viewer-approved-%1").arg(accept));
  QVERIFY(trustPeer(m_viewerFingerprint, senderDb));

  // the viewer knows nobody yet
  Viewer viewer({m_dir.filePath("empty")}, approvedDb);
  QSignalSpy approval(&viewer.server, &StreamServer::approvalNeeded);
  QVERIFY(viewer.server.listen(0, *tlsConfiguration(m_viewerPem)));

  StreamSender sender(QStringLiteral("Stranger"), {senderDb}, m_dir.filePath("unused"));
  QSignalSpy stopped(&sender, &StreamSender::stopped);
  sender.start(
      QStringLiteral("127.0.0.1"), viewer.server.port(), *tlsConfiguration(m_senderPem), smallPreset(),
      std::make_unique<TestPatternSource>(QSize(320, 200)), nullptr
  );

  QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 1, 5000);
  QCOMPARE(approval.first().at(0).toByteArray(), m_senderFingerprint);
  QVERIFY(viewer.frames.isEmpty());

  viewer.server.gate()->resolve(accept);
  if (accept) {
    QTRY_VERIFY_WITH_TIMEOUT(viewer.frames.size() >= 3, 5000);
    // remembered for next time
    QVERIFY(isPeerTrusted(m_senderFingerprint, {approvedDb}));
  } else {
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
    QVERIFY(viewer.frames.isEmpty());
    QVERIFY(!isPeerTrusted(m_senderFingerprint, {approvedDb}));
  }
}

void StreamLoopbackTests::unknownViewerNeedsApproval()
{
  const auto viewerDb = m_dir.filePath("viewer-only-trusts");
  QVERIFY(trustPeer(m_senderFingerprint, viewerDb));

  Viewer viewer({viewerDb}, m_dir.filePath("unused"));
  QVERIFY(viewer.server.listen(0, *tlsConfiguration(m_viewerPem)));

  // the sender has never seen this viewer
  StreamSender sender(QStringLiteral("Careful Mac"), {m_dir.filePath("empty")}, m_dir.filePath("sender-approved"));
  QSignalSpy approval(&sender, &StreamSender::approvalNeeded);
  sender.start(
      QStringLiteral("127.0.0.1"), viewer.server.port(), *tlsConfiguration(m_senderPem), smallPreset(),
      std::make_unique<TestPatternSource>(QSize(320, 200)), nullptr
  );

  QTRY_COMPARE_WITH_TIMEOUT(approval.count(), 1, 5000);
  QCOMPARE(approval.first().at(0).toByteArray(), m_viewerFingerprint);
  // nothing is sent before the user agrees
  QTest::qWait(300);
  QVERIFY(viewer.frames.isEmpty());

  sender.resolveApproval(true);
  QTRY_VERIFY_WITH_TIMEOUT(viewer.frames.size() >= 3, 5000);
}

void StreamLoopbackTests::stoppingEndsBothSides()
{
  const auto viewerDb = m_dir.filePath("stop-viewer-trusts");
  const auto senderDb = m_dir.filePath("stop-sender-trusts");
  trustPeer(m_senderFingerprint, viewerDb);
  trustPeer(m_viewerFingerprint, senderDb);

  Viewer viewer({viewerDb}, m_dir.filePath("unused"));
  QVERIFY(viewer.server.listen(0, *tlsConfiguration(m_viewerPem)));
  StreamSender sender(QStringLiteral("Mac"), {senderDb}, m_dir.filePath("unused"));
  QSignalSpy stopped(&sender, &StreamSender::stopped);
  sender.start(
      QStringLiteral("127.0.0.1"), viewer.server.port(), *tlsConfiguration(m_senderPem), smallPreset(),
      std::make_unique<TestPatternSource>(QSize(320, 200)), nullptr
  );
  QTRY_VERIFY_WITH_TIMEOUT(!viewer.frames.isEmpty(), 5000);

  sender.stop();
  QCOMPARE(stopped.count(), 1);
  QVERIFY(!sender.isActive());
  QTRY_VERIFY_WITH_TIMEOUT(!viewer.stopReason.isEmpty(), 5000);
}

void StreamLoopbackTests::soundSessionSendsOnlyAudio()
{
  const auto viewerDb = m_dir.filePath("sound-viewer-trusts");
  const auto senderDb = m_dir.filePath("sound-sender-trusts");
  QVERIFY(trustPeer(m_senderFingerprint, viewerDb));
  QVERIFY(trustPeer(m_viewerFingerprint, senderDb));

  FrameRecorder recorder({viewerDb}, m_dir.filePath("unused"));
  QVERIFY(recorder.server.listen(0, *tlsConfiguration(m_viewerPem)));

  StreamSender sender(QStringLiteral("Sound Mac"), {senderDb}, m_dir.filePath("unused"));
  QSignalSpy streaming(&sender, &StreamSender::streaming);
  Preset preset;
  preset.kind = SessionKind::Sound;
  sender.start(
      QStringLiteral("127.0.0.1"), recorder.server.port(), *tlsConfiguration(m_senderPem), preset, nullptr,
      std::make_unique<ToneSource>()
  );

  QTRY_COMPARE_WITH_TIMEOUT(streaming.count(), 1, 5000);
  QTRY_VERIFY_WITH_TIMEOUT(recorder.framesOf(FrameType::AudioPacket).size() >= 5, 5000);

  const auto hellos = recorder.framesOf(FrameType::Hello);
  QCOMPARE(hellos.size(), 1);
  const auto hello = decodeHello(hellos.first().payload);
  QVERIFY(hello.has_value());
  QCOMPARE(hello->kind, SessionKind::Sound);
  QCOMPARE(hello->computerName, QStringLiteral("Sound Mac"));
  QCOMPARE(recorder.frames.first().type, FrameType::Hello);

  const auto configs = recorder.framesOf(FrameType::AudioConfig);
  QCOMPARE(configs.size(), 1);
  const auto config = decodeAudioConfig(configs.first().payload);
  QVERIFY(config.has_value());
  QCOMPARE(config->codec, AudioCodec::Pcm16);
  QCOMPARE(config->sampleRate, kAudioSampleRate);
  QCOMPARE(config->channels, uint8_t(kAudioChannels));

  // the config comes before the first packet, and each packet is 10 ms of stereo s16
  const auto packets = recorder.framesOf(FrameType::AudioPacket);
  const auto firstConfig = std::find_if(recorder.frames.begin(), recorder.frames.end(), [](const Frame &frame) {
    return frame.type == FrameType::AudioConfig;
  });
  const auto firstPacket = std::find_if(recorder.frames.begin(), recorder.frames.end(), [](const Frame &frame) {
    return frame.type == FrameType::AudioPacket;
  });
  QVERIFY(firstConfig < firstPacket);
  for (const auto &packet : packets) {
    QCOMPARE(packet.payload.size(), qsizetype(kPcmFramesPerPacket * kAudioChannels * sizeof(int16_t)));
  }
  QVERIFY(recorder.framesOf(FrameType::VideoConfig).isEmpty());
  QVERIFY(recorder.framesOf(FrameType::VideoFrame).isEmpty());

  sender.stop();
}

QTEST_MAIN(StreamLoopbackTests)
