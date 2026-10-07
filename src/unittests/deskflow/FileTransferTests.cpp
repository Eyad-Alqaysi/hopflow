/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "FileTransferTests.h"

#include "deskflow/FileTransfer.h"
#include "deskflow/FileTransferManager.h"

#include <QTemporaryDir>
#include <QTest>

#include <deque>
#include <fstream>

using namespace deskflow::filetransfer;
namespace fs = std::filesystem;

namespace {

fs::path dirPath(const QTemporaryDir &dir)
{
  return pathFromUtf8(dir.path().toStdString());
}

void writeFile(const fs::path &path, const std::string &content)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << content;
}

std::string readFile(const fs::path &path)
{
  std::ifstream file(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(file), {});
}

std::string pattern(size_t size)
{
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    data[i] = static_cast<char>((i * 7 + i / 251) & 0xff);
  }
  return data;
}

bool hasStagingFolder(const fs::path &dir)
{
  for (const auto &entry : fs::directory_iterator(dir)) {
    if (pathToUtf8(entry.path().filename()).starts_with(".hopflow-incoming-")) {
      return true;
    }
  }
  return false;
}

//! Delivers transport calls through a queue, the way the event loop does
struct Wire
{
  std::deque<std::function<void()>> queue;

  void run()
  {
    while (!queue.empty()) {
      auto next = std::move(queue.front());
      queue.pop_front();
      next();
    }
  }
};

Transport towards(Wire &wire, FileTransferManager &peer)
{
  Transport transport;
  transport.start = [&wire, &peer](uint32_t id, FileTransferPurpose purpose, const std::string &manifest) {
    // the reply transport is set by the test, see connect()
    wire.queue.push_back([&peer, id, purpose, manifest] { peer.onStart(id, purpose, manifest, {}); });
  };
  transport.chunk = [&wire, &peer](uint32_t id, const std::string &data) {
    wire.queue.push_back([&peer, id, data] { peer.onChunk(id, data); });
  };
  transport.ack = [&wire, &peer](uint32_t id, uint32_t chunks) {
    wire.queue.push_back([&peer, id, chunks] { peer.onAck(id, chunks); });
  };
  transport.end = [&wire, &peer](uint32_t id, FileTransferStatus status) {
    wire.queue.push_back([&peer, id, status] { peer.onEnd(id, status); });
  };
  return transport;
}

//! Sender transport whose start message carries a reply transport back to the sender
Transport link(Wire &wire, FileTransferManager &sender, FileTransferManager &receiver)
{
  auto transport = towards(wire, receiver);
  auto reply = towards(wire, sender);
  transport.start = [&wire, &receiver, reply](uint32_t id, FileTransferPurpose purpose, const std::string &manifest) {
    wire.queue.push_back([&receiver, reply, id, purpose, manifest] { receiver.onStart(id, purpose, manifest, reply); });
  };
  return transport;
}

struct Outcome
{
  std::vector<std::string> received;
  std::vector<std::string> failures;
};

FileTransferManager::Callbacks callbacks(Outcome &outcome, const fs::path &destination)
{
  FileTransferManager::Callbacks result;
  result.destination = [destination](FileTransferPurpose, uint32_t) { return destination; };
  result.received = [&outcome](uint32_t, FileTransferPurpose, const std::vector<std::string> &paths) {
    outcome.received.insert(outcome.received.end(), paths.begin(), paths.end());
  };
  result.failed = [&outcome](const std::string &message) { outcome.failures.push_back(message); };
  return result;
}

} // namespace

void FileTransferTests::safePaths_data()
{
  QTest::addColumn<QString>("path");
  QTest::addColumn<bool>("safe");

  QTest::newRow("file") << "photo.jpg" << true;
  QTest::newRow("nested") << "folder/sub/file.txt" << true;
  QTest::newRow("unicode") << QStringLiteral("über/naïve 写真.txt") << true;
  QTest::newRow("dotfile") << ".gitignore" << true;
  QTest::newRow("empty") << "" << false;
  QTest::newRow("absolute") << "/etc/passwd" << false;
  QTest::newRow("parent") << "../secret" << false;
  QTest::newRow("parent inside") << "a/../../b" << false;
  QTest::newRow("current dir") << "a/./b" << false;
  QTest::newRow("double slash") << "a//b" << false;
  QTest::newRow("trailing slash") << "a/" << false;
  QTest::newRow("drive letter") << "C:/Windows" << false;
  QTest::newRow("backslash") << "a\\..\\b" << false;
  QTest::newRow("alternate stream") << "file.txt:hidden" << false;
  QTest::newRow("reserved name") << "con" << false;
  QTest::newRow("reserved with extension") << "dir/NUL.txt" << false;
  QTest::newRow("trailing dot") << "name." << false;
  QTest::newRow("trailing space") << "name " << false;
  QTest::newRow("control character") << QStringLiteral("a\nb") << false;
}

void FileTransferTests::safePaths()
{
  QFETCH(QString, path);
  QFETCH(bool, safe);
  QCOMPARE(isSafeRelativePath(path.toStdString()), safe);
}

void FileTransferTests::manifestRoundTrip()
{
  const Manifest manifest = {
      {"folder", 0, true}, {"folder/a.txt", 12, false}, {"folder/empty", 0, false}, {"big.bin", 5000000000ull, false}
  };

  std::string error;
  const auto parsed = parseManifest(serializeManifest(manifest), error);
  QVERIFY2(parsed.has_value(), error.c_str());
  QCOMPARE(*parsed, manifest);
  QCOMPARE(totalSize(manifest), 5000000012ull);
}

void FileTransferTests::manifestRejected_data()
{
  QTest::addColumn<QString>("json");

  QTest::newRow("not json") << "{";
  QTest::newRow("object") << R"({"path":"a","size":1})";
  QTest::newRow("empty array") << "[]";
  QTest::newRow("unsafe path") << R"([{"path":"../a","size":1}])";
  QTest::newRow("missing size") << R"([{"path":"a"}])";
  QTest::newRow("negative size") << R"([{"path":"a","size":-1}])";
  QTest::newRow("fractional size") << R"([{"path":"a","size":1.5}])";
  QTest::newRow("duplicate") << R"([{"path":"a","size":1},{"path":"a","size":2}])";
}

void FileTransferTests::manifestRejected()
{
  QFETCH(QString, json);
  std::string error;
  QVERIFY(!parseManifest(json.toStdString(), error).has_value());
  QVERIFY(!error.empty());
}

void FileTransferTests::splitAndJoinPaths()
{
  const std::vector<std::string> paths = {"/Users/me/a b.txt", "C:\\Users\\me\\c.txt"};
  QCOMPARE(splitPaths(joinPaths(paths)), paths);
  QVERIFY(splitPaths("").empty());
  QCOMPARE(splitPaths("a\n\nb\n"), (std::vector<std::string>{"a", "b"}));
}

void FileTransferTests::samePathsIgnoresNormalization()
{
  // "ö" precomposed, and as "o" plus a combining diaeresis, as macOS reports it
  const std::vector<std::string> composed = {"/tmp/f\xc3\xb6lder"};
  const std::vector<std::string> decomposed = {"/tmp/fo\xcc\x88lder"};
  QVERIFY(composed != decomposed);
  QVERIFY(samePaths(composed, decomposed));
  QVERIFY(!samePaths(composed, {"/tmp/folder"}));
  QVERIFY(!samePaths(composed, {}));
}

void FileTransferTests::uniqueDestination()
{
  QTemporaryDir dir;
  const auto root = dirPath(dir);
  QCOMPARE(deskflow::filetransfer::uniqueDestination(root, "a.txt"), root / "a.txt");

  writeFile(root / "a.txt", "1");
  writeFile(root / "a (1).txt", "2");
  QCOMPARE(deskflow::filetransfer::uniqueDestination(root, "a.txt"), root / "a (2).txt");

  fs::create_directory(root / "folder");
  QCOMPARE(deskflow::filetransfer::uniqueDestination(root, "folder"), root / "folder (1)");
}

void FileTransferTests::senderReceiverRoundTrip()
{
  QTemporaryDir source;
  QTemporaryDir destination;
  const auto src = dirPath(source);
  const auto dst = dirPath(destination);

  const auto big = pattern(10000);
  writeFile(src / "project" / "notes.txt", "hello");
  writeFile(src / "project" / "deep" / "big.bin", big);
  writeFile(src / "project" / "empty.txt", "");
  fs::create_directories(src / "project" / "empty-folder");
  writeFile(src / pathFromUtf8("写真.txt"), "photo");
  // an existing item with the same name must not be overwritten
  writeFile(dst / pathFromUtf8("写真.txt"), "keep me");

  std::string error;
  auto sender = Sender::create({pathToUtf8(src / "project"), pathToUtf8(src / pathFromUtf8("写真.txt"))}, error);
  QVERIFY2(sender.has_value(), error.c_str());

  Receiver receiver(dst, sender->manifest());
  QVERIFY2(receiver.begin(error), error.c_str());

  std::string chunk;
  // small chunks so files span chunk boundaries
  while (!sender->done()) {
    QVERIFY2(sender->readChunk(chunk, error, 3000), error.c_str());
    QVERIFY2(receiver.write(chunk, error), error.c_str());
  }
  QVERIFY(sender->readChunk(chunk, error));
  QVERIFY(chunk.empty());

  auto moved = receiver.finish(error);
  QVERIFY2(moved.has_value(), error.c_str());
  QCOMPARE(moved->size(), size_t(2));
  QCOMPARE(moved->at(0), pathToUtf8(dst / "project"));
  QCOMPARE(moved->at(1), pathToUtf8(dst / pathFromUtf8("写真 (1).txt")));

  QCOMPARE(readFile(dst / "project" / "notes.txt"), std::string("hello"));
  QCOMPARE(readFile(dst / "project" / "deep" / "big.bin"), big);
  QVERIFY(fs::exists(dst / "project" / "empty.txt"));
  QVERIFY(fs::is_directory(dst / "project" / "empty-folder"));
  QCOMPARE(readFile(dst / pathFromUtf8("写真 (1).txt")), std::string("photo"));
  QCOMPARE(readFile(dst / pathFromUtf8("写真.txt")), std::string("keep me"));
  QVERIFY(!hasStagingFolder(dst));
}

void FileTransferTests::receiverRejectsExtraData()
{
  QTemporaryDir destination;
  Receiver receiver(dirPath(destination), {{"a.txt", 3, false}});
  std::string error;
  QVERIFY(receiver.begin(error));
  QVERIFY(!receiver.write("abcd", error));
  QVERIFY(!error.empty());
}

void FileTransferTests::receiverAbortCleansUp()
{
  QTemporaryDir destination;
  const auto dst = dirPath(destination);
  std::string error;
  {
    Receiver receiver(dst, {{"a.txt", 10, false}});
    QVERIFY(receiver.begin(error));
    QVERIFY(receiver.write("abc", error));
    QVERIFY(!receiver.finish(error).has_value());
    QVERIFY(hasStagingFolder(dst));
  }
  // destroyed without finishing
  QVERIFY(!hasStagingFolder(dst));
  QVERIFY(!fs::exists(dst / "a.txt"));
}

void FileTransferTests::managerTransfer()
{
  QTemporaryDir source;
  QTemporaryDir destination;
  const auto src = dirPath(source);
  // several windows' worth of chunks so flow control has to resume the sender
  const auto data = pattern(kFileTransferChunkSize * kFileTransferWindow * 2 + 123);
  writeFile(src / "video.mov", data);

  Outcome sent;
  Outcome got;
  FileTransferManager sender(callbacks(sent, {}));
  FileTransferManager receiver(callbacks(got, dirPath(destination)));
  Wire wire;

  QVERIFY(sender.send(7, FileTransferPurpose::Drop, {pathToUtf8(src / "video.mov")}, link(wire, sender, receiver)));
  QVERIFY(sender.isActive(7));
  wire.run();

  QVERIFY(sent.failures.empty());
  QVERIFY(got.failures.empty());
  QVERIFY(!sender.isActive(7));
  QVERIFY(!receiver.isActive(7));
  QCOMPARE(got.received, (std::vector<std::string>{pathToUtf8(dirPath(destination) / "video.mov")}));
  QVERIFY(readFile(dirPath(destination) / "video.mov") == data);
}

void FileTransferTests::managerRefusesOverLimit()
{
  QTemporaryDir source;
  QTemporaryDir destination;
  writeFile(dirPath(source) / "a.bin", pattern(2048));

  Outcome sent;
  Outcome got;
  FileTransferManager sender(callbacks(sent, {}));
  FileTransferManager receiver(callbacks(got, dirPath(destination)), 1024);
  Wire wire;

  QVERIFY(
      sender.send(1, FileTransferPurpose::Drop, {pathToUtf8(dirPath(source) / "a.bin")}, link(wire, sender, receiver))
  );
  wire.run();

  QCOMPARE(got.failures.size(), size_t(1));
  QVERIFY(got.received.empty());
  QVERIFY(!sender.isActive(1));
  QVERIFY(!fs::exists(dirPath(destination) / "a.bin"));
}

void FileTransferTests::managerReceiverCancels()
{
  QTemporaryDir source;
  QTemporaryDir destination;
  writeFile(dirPath(source) / "a.bin", pattern(kFileTransferChunkSize * kFileTransferWindow * 2));

  Outcome sent;
  Outcome got;
  FileTransferManager sender(callbacks(sent, {}));
  FileTransferManager receiver(callbacks(got, dirPath(destination)));
  Wire wire;

  QVERIFY(
      sender.send(3, FileTransferPurpose::Drop, {pathToUtf8(dirPath(source) / "a.bin")}, link(wire, sender, receiver))
  );
  // deliver the start and the first window, then the receiver goes away
  for (int i = 0; i < 4 && !wire.queue.empty(); ++i) {
    auto next = std::move(wire.queue.front());
    wire.queue.pop_front();
    next();
  }
  receiver.cancelAll();
  wire.run();

  QVERIFY(!sender.isActive(3));
  QVERIFY(got.received.empty());
  QVERIFY(!fs::exists(dirPath(destination) / "a.bin"));
  QVERIFY(!hasStagingFolder(dirPath(destination)));
}

QTEST_MAIN(FileTransferTests)
