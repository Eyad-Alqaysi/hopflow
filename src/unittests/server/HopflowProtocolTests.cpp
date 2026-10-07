/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "HopflowProtocolTests.h"

#include "../deskflow/MockEventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/ProtocolTypes.h"
#include "io/IStream.h"
#include "server/ClientProxyHopflow.h"

#include <QByteArray>
#include <QTest>

#include <cstring>

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

//! Stream that reads from a preset buffer and records writes
class BufferStream : public deskflow::IStream
{
public:
  QByteArray input;
  QByteArray output;

  void write(const void *buffer, uint32_t n) override
  {
    output.append(static_cast<const char *>(buffer), n);
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    n = std::min<uint32_t>(n, static_cast<uint32_t>(input.size()));
    if (buffer != nullptr) {
      std::memcpy(buffer, input.constData(), n);
    }
    input.remove(0, n);
    return n;
  }

  void close() override
  {
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<BufferStream *>(this);
  }

  bool isReady() const override
  {
    return !input.isEmpty();
  }

  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(input.size());
  }
};

class TestableProxy : public ClientProxyHopflow
{
public:
  using ClientProxyHopflow::ClientProxyHopflow;
  using ClientProxyHopflow::parseHandshakeMessage;
  using ClientProxyHopflow::parseMessage;
};

} // namespace

void HopflowProtocolTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

void HopflowProtocolTests::clientMinorVersion_data()
{
  QTest::addColumn<int>("serverMinor");
  QTest::addColumn<int>("expected");

  QTest::newRow("Deskflow 1.6") << 6 << 6;
  QTest::newRow("Deskflow 1.8") << 8 << 8;
  QTest::newRow("future Deskflow 1.9") << 9 << 8;
  QTest::newRow("future Deskflow 1.99") << 99 << 8;
  QTest::newRow("Hopflow") << 100 << 100;
  QTest::newRow("future Hopflow") << 101 << 100;
}

void HopflowProtocolTests::clientMinorVersion()
{
  QFETCH(int, serverMinor);
  QFETCH(int, expected);

  QCOMPARE(clientProtocolMinorVersion(static_cast<int16_t>(serverMinor)), expected);
}

void HopflowProtocolTests::platformMessage_data()
{
  QTest::addColumn<bool>("handshake");
  QTest::addColumn<int>("wire");
  QTest::addColumn<int>("expected");

  QTest::newRow("windows during handshake") << true << 1 << static_cast<int>(PeerPlatform::Windows);
  QTest::newRow("macos after handshake") << false << 2 << static_cast<int>(PeerPlatform::MacOS);
  QTest::newRow("out of range") << false << 42 << static_cast<int>(PeerPlatform::Unknown);
}

void HopflowProtocolTests::platformMessage()
{
  QFETCH(bool, handshake);
  QFETCH(int, wire);
  QFETCH(int, expected);

  MockEventQueue events;
  auto *stream = new BufferStream;
  // the constructor only stores the server pointer
  TestableProxy proxy("client", stream, reinterpret_cast<Server *>(0x1), &events);
  QCOMPARE(proxy.getPlatform(), PeerPlatform::Unknown);

  stream->input = QByteArray(1, static_cast<char>(wire));
  const auto *code = reinterpret_cast<const uint8_t *>(kMsgHPlatform);
  QVERIFY(handshake ? proxy.parseHandshakeMessage(code) : proxy.parseMessage(code));
  QCOMPARE(static_cast<int>(proxy.getPlatform()), expected);
  QVERIFY(stream->input.isEmpty());
}

QTEST_MAIN(HopflowProtocolTests)
