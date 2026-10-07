/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferRouterTests.h"

#include "deskflow/FileTransfer.h"
#include "deskflow/ipc/CoreIpcServer.h"
#include "server/BaseClientProxy.h"
#include "server/FileTransferRouter.h"

#include <QTest>

#include <memory>

using namespace deskflow::filetransfer;

namespace {

//! Client that records the file transfer calls the router makes
class FakeClient : public BaseClientProxy
{
public:
  FakeClient(const std::string &name, bool hopflow, PeerPlatform platform = PeerPlatform::Unknown)
      : BaseClientProxy(name),
        m_hopflow(hopflow)
  {
    setPlatform(platform);
  }

  struct Call
  {
    std::string what;
    uint32_t id;
    std::string text;
    uint32_t number = 0;
  };
  std::vector<Call> calls;

  bool isHopflow() const override
  {
    return m_hopflow;
  }

  void fileRequest(uint32_t id, FileTransferPurpose purpose, const std::string &paths) override
  {
    calls.push_back({"request", id, paths, static_cast<uint32_t>(purpose)});
  }
  void fileStart(uint32_t id, FileTransferPurpose purpose, const std::string &manifest) override
  {
    calls.push_back({"start", id, manifest, static_cast<uint32_t>(purpose)});
  }
  void fileChunk(uint32_t id, const std::string &data) override
  {
    calls.push_back({"chunk", id, data});
  }
  void fileAck(uint32_t id, uint32_t chunks) override
  {
    calls.push_back({"ack", id, {}, chunks});
  }
  void fileEnd(uint32_t id, FileTransferStatus status) override
  {
    calls.push_back({"end", id, {}, static_cast<uint32_t>(status)});
  }

  // nothing else is used by the router
  void *getEventTarget() const override
  {
    return nullptr;
  }
  bool getClipboard(ClipboardID, IClipboard *) const override
  {
    return false;
  }
  void getShape(int32_t &, int32_t &, int32_t &, int32_t &) const override
  {
  }
  void getCursorPos(int32_t &, int32_t &) const override
  {
  }
  void enter(int32_t, int32_t, uint32_t, KeyModifierMask, bool) override
  {
  }
  bool leave() override
  {
    return true;
  }
  void setClipboard(ClipboardID, const IClipboard *) override
  {
  }
  void grabClipboard(ClipboardID) override
  {
  }
  void setClipboardDirty(ClipboardID, bool) override
  {
  }
  void keyDown(KeyID, KeyModifierMask, KeyButton, const std::string &) override
  {
  }
  void keyRepeat(KeyID, KeyModifierMask, int32_t, KeyButton, const std::string &) override
  {
  }
  void keyUp(KeyID, KeyModifierMask, KeyButton) override
  {
  }
  void mouseDown(ButtonID) override
  {
  }
  void mouseUp(ButtonID) override
  {
  }
  void mouseMove(int32_t, int32_t) override
  {
  }
  void mouseRelativeMove(int32_t, int32_t) override
  {
  }
  void mouseWheel(int32_t, int32_t) override
  {
  }
  void screensaver(bool) override
  {
  }
  void resetOptions() override
  {
  }
  void setOptions(const OptionsList &) override
  {
  }
  void sendDragInfo(uint32_t, const char *, size_t) override
  {
  }
  void fileChunkSending(uint8_t, char *, size_t) override
  {
  }
  std::string getSecureInputApp() const override
  {
    return {};
  }
  void secureInputNotification(const std::string &) const override
  {
  }
  deskflow::IStream *getStream() const override
  {
    return nullptr;
  }

private:
  bool m_hopflow;
};

struct Setup
{
  FakeClient server{"server", true, PeerPlatform::MacOS};
  FakeClient a{"a", true, PeerPlatform::Windows};
  FakeClient b{"b", true, PeerPlatform::MacOS};
  FakeClient linuxClient{"linux", true, PeerPlatform::Linux};
  FakeClient deskflow{"deskflow", false};
  // the server computer is only used when it sends or receives itself
  FileTransferRouter router{&server, nullptr};
};

const std::vector<std::string> kPaths = {"/Users/me/report.pdf", "/Users/me/photos"};
const std::string kManifest = serializeManifest({{"report.pdf", 100, false}});

uint32_t requestedId(const FakeClient &client)
{
  return client.calls.at(0).id;
}

} // namespace

void FileTransferRouterTests::initTestCase()
{
  // the router reports failures to the GUI through the IPC server
  static deskflow::core::ipc::CoreIpcServer ipcServer(nullptr);
}

void FileTransferRouterTests::clipboardDeliveredOnceOnEnter()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));

  s.router.onEnter(&s.b);
  QCOMPARE(s.a.calls.size(), size_t(1));
  QCOMPARE(s.a.calls[0].what, std::string("request"));
  QCOMPARE(s.a.calls[0].text, joinPaths(kPaths));
  QCOMPARE(s.a.calls[0].number, uint32_t(FileTransferPurpose::Clipboard));

  // already delivered
  s.router.onEnter(&s.b);
  QCOMPARE(s.a.calls.size(), size_t(1));

  // a newer copy is delivered again
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.b);
  QCOMPARE(s.a.calls.size(), size_t(2));
}

void FileTransferRouterTests::clipboardNotSentBackToOwner()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.a);
  QVERIFY(s.a.calls.empty());
}

void FileTransferRouterTests::clipboardNotSentToDeskflowClient()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.deskflow);
  QVERIFY(s.a.calls.empty());
}

void FileTransferRouterTests::clipboardNotSentToLinux()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.linuxClient);
  QVERIFY(s.a.calls.empty());

  // dropped files do go to Linux
  s.router.onOffer(&s.a, FileTransferPurpose::Drop, joinPaths(kPaths));
  s.router.onDrop(&s.linuxClient);
  QCOMPARE(s.a.calls.size(), size_t(1));
}

void FileTransferRouterTests::newClipboardWithoutFilesClearsOffer()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  // b copied text
  s.router.onOffer(&s.b, FileTransferPurpose::Clipboard, "");
  s.router.onEnter(&s.server);
  QVERIFY(s.a.calls.empty());
}

void FileTransferRouterTests::deskflowClientGrabClearsOffer()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));

  // hopflow clients report their own clipboard files, so their grab changes nothing
  s.router.onClipboardTakenByOther(&s.b);
  s.router.onEnter(&s.server);
  QCOMPARE(s.a.calls.size(), size_t(1));

  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onClipboardTakenByOther(&s.deskflow);
  s.router.onEnter(&s.b);
  QCOMPARE(s.a.calls.size(), size_t(1));
}

void FileTransferRouterTests::relaysTransferBetweenClients()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.b);
  const auto id = requestedId(s.a);

  s.router.onStart(&s.a, id, FileTransferPurpose::Clipboard, kManifest);
  s.router.onChunk(&s.a, id, "data");
  s.router.onAck(&s.b, id, 1);
  s.router.onEnd(&s.a, id, FileTransferStatus::Ok);

  QCOMPARE(s.b.calls.size(), size_t(3));
  QCOMPARE(s.b.calls[0].what, std::string("start"));
  QCOMPARE(s.b.calls[0].text, kManifest);
  QCOMPARE(s.b.calls[1].what, std::string("chunk"));
  QCOMPARE(s.b.calls[1].text, std::string("data"));
  QCOMPARE(s.b.calls[2].what, std::string("end"));
  QCOMPARE(s.b.calls[2].number, uint32_t(FileTransferStatus::Ok));

  QCOMPARE(s.a.calls.size(), size_t(2));
  QCOMPARE(s.a.calls[1].what, std::string("ack"));
  QCOMPARE(s.a.calls[1].number, uint32_t(1));

  // the route is gone once the transfer ended
  s.router.onChunk(&s.a, id, "late");
  QCOMPARE(s.b.calls.size(), size_t(3));
}

void FileTransferRouterTests::ignoresMessagesFromWrongEnd()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.b);
  const auto id = requestedId(s.a);
  s.router.onStart(&s.a, id, FileTransferPurpose::Clipboard, kManifest);

  // only the sender sends data, only the receiver acknowledges
  s.router.onChunk(&s.b, id, "spoofed");
  s.router.onAck(&s.a, id, 5);
  s.router.onChunk(&s.deskflow, id, "spoofed");
  QCOMPARE(s.b.calls.size(), size_t(1));
  QCOMPARE(s.a.calls.size(), size_t(1));
}

void FileTransferRouterTests::refusesUnknownTransfer()
{
  Setup s;
  s.router.onStart(&s.a, 99, FileTransferPurpose::Drop, kManifest);
  QCOMPARE(s.a.calls.size(), size_t(1));
  QCOMPARE(s.a.calls[0].what, std::string("end"));
  QCOMPARE(s.a.calls[0].number, uint32_t(FileTransferStatus::Cancelled));
}

void FileTransferRouterTests::refusesTooLarge()
{
  Setup s;
  s.router.setMaxTransferBytes(50);
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.b);
  const auto id = requestedId(s.a);

  s.router.onStart(&s.a, id, FileTransferPurpose::Clipboard, kManifest);
  QVERIFY(s.b.calls.empty());
  QCOMPARE(s.a.calls.back().what, std::string("end"));
  QCOMPARE(s.a.calls.back().number, uint32_t(FileTransferStatus::Cancelled));
}

void FileTransferRouterTests::dropStartsTransfer()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Drop, joinPaths(kPaths));
  QVERIFY(s.router.isDragging(&s.a));
  QVERIFY(!s.router.isDragging(&s.b));

  s.router.onDrop(&s.b);
  QCOMPARE(s.a.calls.size(), size_t(1));
  QCOMPARE(s.a.calls[0].what, std::string("request"));
  QCOMPARE(s.a.calls[0].number, uint32_t(FileTransferPurpose::Drop));
  QVERIFY(!s.router.isDragging(&s.a));

  // the drag is over
  s.router.onDrop(&s.b);
  QCOMPARE(s.a.calls.size(), size_t(1));
}

void FileTransferRouterTests::dropOnSameComputerDoesNothing()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Drop, joinPaths(kPaths));
  s.router.onDrop(&s.a);
  QVERIFY(s.a.calls.empty());
  QVERIFY(!s.router.isDragging(&s.a));
}

void FileTransferRouterTests::disconnectCancelsTransfer()
{
  Setup s;
  s.router.onOffer(&s.a, FileTransferPurpose::Drop, joinPaths(kPaths));
  s.router.onDrop(&s.b);
  const auto id = requestedId(s.a);
  s.router.onStart(&s.a, id, FileTransferPurpose::Drop, kManifest);

  s.router.onClientRemoved(&s.b);
  QCOMPARE(s.a.calls.back().what, std::string("end"));
  QCOMPARE(s.a.calls.back().number, uint32_t(FileTransferStatus::Cancelled));

  // nothing left to relay
  s.router.onChunk(&s.a, id, "data");
  QCOMPARE(s.b.calls.size(), size_t(1));
}

void FileTransferRouterTests::disabledDoesNothing()
{
  Setup s;
  s.router.setEnabled(false);
  s.router.onOffer(&s.a, FileTransferPurpose::Clipboard, joinPaths(kPaths));
  s.router.onEnter(&s.b);
  s.router.onOffer(&s.a, FileTransferPurpose::Drop, joinPaths(kPaths));
  QVERIFY(!s.router.isDragging(&s.a));
  s.router.onDrop(&s.b);
  QVERIFY(s.a.calls.empty());
}

QTEST_MAIN(FileTransferRouterTests)
