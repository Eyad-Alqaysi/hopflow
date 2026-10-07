/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "HopflowFileTransferTests.h"

#include "../deskflow/MockEventQueue.h"
#include "client/ServerProxyHopflow.h"
#include "deskflow/AppUtil.h"
#include "deskflow/FileTransfer.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "io/IStream.h"

#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <fstream>

using namespace deskflow::filetransfer;
namespace fs = std::filesystem;

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

//! Reads what the test feeds it and records what the proxy writes
class WireStream : public deskflow::IStream
{
public:
  std::string input;
  std::string output;

  void write(const void *buffer, uint32_t n) override
  {
    output.append(static_cast<const char *>(buffer), n);
  }

  uint32_t read(void *buffer, uint32_t n) override
  {
    n = std::min<uint32_t>(n, static_cast<uint32_t>(input.size()));
    if (buffer != nullptr) {
      std::memcpy(buffer, input.data(), n);
    }
    input.erase(0, n);
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
    return const_cast<WireStream *>(this);
  }
  bool isReady() const override
  {
    return !input.empty();
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(input.size());
  }
};

class TestProxy : public ServerProxyHopflow
{
public:
  using ServerProxyHopflow::ServerProxyHopflow;

  bool parse(const uint8_t *code)
  {
    return parseMessage(code) == ConnectionResult::Okay;
  }
};

//! Deliver every complete message in \p from to \p to; returns how many were delivered
int deliver(WireStream &from, WireStream &to, TestProxy &proxy)
{
  int count = 0;
  while (from.output.size() >= 4) {
    // each message is its 4 byte code then arguments; hand the proxy the arguments and let it parse
    const auto code = from.output.substr(0, 4);
    from.output.erase(0, 4);
    to.input = from.output;
    from.output.clear();
    if (!proxy.parse(reinterpret_cast<const uint8_t *>(code.data()))) {
      return -1;
    }
    // whatever the proxy did not read belongs to the following messages
    from.output = to.input;
    to.input.clear();
    ++count;
  }
  return count;
}

std::string pattern(size_t size)
{
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((i * 13 + i / 509) & 0xff);
  }
  return data;
}

Client *undereferenceableClient()
{
  // dropped files never touch the client; only copied files go to its clipboard
  return reinterpret_cast<Client *>(0x1);
}

} // namespace

void HopflowFileTransferTests::initTestCase()
{
  static TestAppUtil appUtil;
  // received files are reported to the GUI
  static deskflow::core::ipc::CoreIpcServer ipcServer(nullptr);
}

void HopflowFileTransferTests::transferOverTheWire()
{
  QTemporaryDir source;
  QTemporaryDir destination;
  qputenv("HOPFLOW_DOWNLOAD_DIR", destination.path().toUtf8());

  // enough chunks that the sender must wait for acknowledgements
  const auto data = pattern(kFileTransferChunkSize * (kFileTransferWindow + 9) + 77);
  const auto file = pathFromUtf8(source.path().toStdString()) / "movie.mov";
  std::ofstream(file, std::ios::binary) << data;

  MockEventQueue events;
  auto *senderStream = new WireStream;
  auto *receiverStream = new WireStream;
  TestProxy sender(undereferenceableClient(), senderStream, &events);
  TestProxy receiver(undereferenceableClient(), receiverStream, &events);

  // the server asks the sender for the files
  const auto paths = pathToUtf8(file);
  WireStream request;
  ProtocolUtil::writef(&request, kMsgHFileRequest, 5u, static_cast<uint8_t>(FileTransferPurpose::Drop), &paths);
  QCOMPARE(deliver(request, *senderStream, sender), 1);

  // relay both ways until nothing is left to say
  int rounds = 0;
  while (!senderStream->output.empty() || !receiverStream->output.empty()) {
    QVERIFY(deliver(*senderStream, *receiverStream, receiver) >= 0);
    QVERIFY(deliver(*receiverStream, *senderStream, sender) >= 0);
    QVERIFY(++rounds < 100);
  }
  // several rounds prove the sender paused for acknowledgements
  QVERIFY(rounds > 1);

  const auto received = pathFromUtf8(destination.path().toStdString()) / "movie.mov";
  std::ifstream in(received, std::ios::binary);
  const std::string got((std::istreambuf_iterator<char>(in)), {});
  QCOMPARE(got.size(), data.size());
  QVERIFY(got == data);
  qunsetenv("HOPFLOW_DOWNLOAD_DIR");
}

void HopflowFileTransferTests::receiverRefusesUnsafeManifest()
{
  QTemporaryDir destination;
  qputenv("HOPFLOW_DOWNLOAD_DIR", destination.path().toUtf8());

  MockEventQueue events;
  auto *stream = new WireStream;
  TestProxy receiver(undereferenceableClient(), stream, &events);

  const std::string manifest = R"([{"path":"../../escape.txt","size":4}])";
  WireStream start;
  ProtocolUtil::writef(&start, kMsgHFileStart, 9u, static_cast<uint8_t>(FileTransferPurpose::Drop), &manifest);
  QCOMPARE(deliver(start, *stream, receiver), 1);

  // the receiver cancels the transfer
  QCOMPARE(stream->output.substr(0, 4), std::string("HFEN"));
  QVERIFY(!fs::exists(pathFromUtf8(destination.path().toStdString()).parent_path() / "escape.txt"));
  qunsetenv("HOPFLOW_DOWNLOAD_DIR");
}

QTEST_MAIN(HopflowFileTransferTests)
