/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/FileTransfer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string_view>

namespace fs = std::filesystem;

namespace deskflow::filetransfer {

namespace {

// largest integer a JSON number (double) holds exactly
constexpr double kMaxJsonInteger = 9007199254740992.0;
constexpr size_t kMaxPathLength = 4096;

bool isReservedWindowsName(std::string_view component)
{
  static const std::array<std::string_view, 22> reserved = {"CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2",
                                                            "COM3", "COM4", "COM5", "COM6", "COM7", "COM8",
                                                            "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5",
                                                            "LPT6", "LPT7", "LPT8", "LPT9"};
  const auto stem = component.substr(0, component.find('.'));
  return std::ranges::any_of(reserved, [stem](std::string_view name) {
    return std::ranges::equal(stem, name, [](char a, char b) { return std::toupper(a) == b; });
  });
}

bool isSafeComponent(std::string_view component)
{
  if (component.empty() || component == "." || component == "..") {
    return false;
  }
  // trailing dots and spaces are silently stripped by Windows, which could make two names collide
  if (component.back() == '.' || component.back() == ' ') {
    return false;
  }
  return !isReservedWindowsName(component);
}

std::string firstComponent(const std::string &path)
{
  return path.substr(0, path.find('/'));
}

std::string randomSuffix()
{
  return QString::number(QRandomGenerator::global()->generate64(), 16).toStdString();
}

} // namespace

fs::path pathFromUtf8(const std::string &utf8)
{
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}

std::string pathToUtf8(const fs::path &path)
{
  const auto utf8 = path.u8string();
  return std::string(utf8.begin(), utf8.end());
}

std::string joinPaths(const std::vector<std::string> &paths)
{
  std::string joined;
  for (const auto &path : paths) {
    if (!joined.empty()) {
      joined += '\n';
    }
    joined += path;
  }
  return joined;
}

std::vector<std::string> splitPaths(const std::string &joined)
{
  std::vector<std::string> paths;
  size_t start = 0;
  while (start <= joined.size()) {
    const auto end = std::min(joined.find('\n', start), joined.size());
    if (end > start) {
      paths.push_back(joined.substr(start, end - start));
    }
    start = end + 1;
  }
  return paths;
}

bool isSafeRelativePath(const std::string &path)
{
  if (path.empty() || path.size() > kMaxPathLength || path.front() == '/') {
    return false;
  }

  // backslashes and colons would be separators or drive letters on Windows
  if (std::ranges::any_of(path, [](char c) { return c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20; })) {
    return false;
  }

  size_t start = 0;
  while (start <= path.size()) {
    const auto end = std::min(path.find('/', start), path.size());
    if (!isSafeComponent(std::string_view(path).substr(start, end - start))) {
      return false;
    }
    start = end + 1;
  }
  return true;
}

uint64_t totalSize(const Manifest &manifest)
{
  uint64_t total = 0;
  for (const auto &entry : manifest) {
    total += entry.size;
  }
  return total;
}

std::string serializeManifest(const Manifest &manifest)
{
  QJsonArray array;
  for (const auto &entry : manifest) {
    QJsonObject object;
    object[QStringLiteral("path")] = QString::fromStdString(entry.path);
    if (entry.directory) {
      object[QStringLiteral("dir")] = true;
    } else {
      object[QStringLiteral("size")] = static_cast<double>(entry.size);
    }
    array.append(object);
  }
  return QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString();
}

std::optional<Manifest> parseManifest(const std::string &json, std::string &error)
{
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(QByteArray::fromStdString(json), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
    error = "manifest is not a JSON array";
    return std::nullopt;
  }

  const auto array = document.array();
  if (array.isEmpty() || static_cast<size_t>(array.size()) > kMaxManifestEntries) {
    error = "manifest has no entries or too many";
    return std::nullopt;
  }

  Manifest manifest;
  std::set<std::string> seen;
  for (const auto &value : array) {
    const auto object = value.toObject();
    ManifestEntry entry;
    entry.path = object.value(QStringLiteral("path")).toString().toStdString();
    entry.directory = object.value(QStringLiteral("dir")).toBool(false);

    if (!isSafeRelativePath(entry.path)) {
      error = "manifest contains an unsafe path: " + entry.path;
      return std::nullopt;
    }
    if (!seen.insert(entry.path).second) {
      error = "manifest contains a duplicate path: " + entry.path;
      return std::nullopt;
    }

    if (!entry.directory) {
      const auto size = object.value(QStringLiteral("size")).toDouble(-1);
      if (size < 0 || size > kMaxJsonInteger || size != static_cast<double>(static_cast<uint64_t>(size))) {
        error = "manifest contains an invalid size for: " + entry.path;
        return std::nullopt;
      }
      entry.size = static_cast<uint64_t>(size);
    }
    manifest.push_back(std::move(entry));
  }
  return manifest;
}

fs::path uniqueDestination(const fs::path &dir, const std::string &name)
{
  auto candidate = dir / pathFromUtf8(name);
  std::error_code ec;
  if (!fs::exists(fs::symlink_status(candidate, ec))) {
    return candidate;
  }

  const auto original = pathFromUtf8(name);
  const auto stem = pathToUtf8(original.stem());
  const auto extension = pathToUtf8(original.extension());
  for (int n = 1;; ++n) {
    candidate = dir / pathFromUtf8(stem + " (" + std::to_string(n) + ")" + extension);
    if (!fs::exists(fs::symlink_status(candidate, ec))) {
      return candidate;
    }
  }
}

//
// Sender
//

std::optional<Sender> Sender::create(const std::vector<std::string> &roots, std::string &error)
{
  Sender sender;
  std::set<std::string> topLevelNames;

  for (const auto &root : roots) {
    const auto rootPath = pathFromUtf8(root);
    std::error_code ec;
    const auto status = fs::status(rootPath, ec);
    if (ec || !fs::exists(status)) {
      error = "cannot read " + root;
      return std::nullopt;
    }

    const auto baseName = pathToUtf8(rootPath.filename());
    if (baseName.empty()) {
      error = "cannot send " + root;
      return std::nullopt;
    }

    // two selected items with the same name, e.g. from different folders
    auto name = baseName;
    for (int n = 1; topLevelNames.contains(name); ++n) {
      const auto original = pathFromUtf8(baseName);
      name = pathToUtf8(original.stem()) + " (" + std::to_string(n) + ")" + pathToUtf8(original.extension());
    }
    topLevelNames.insert(name);

    if (fs::is_regular_file(status)) {
      sender.addEntry(rootPath, name, false, fs::file_size(rootPath, ec));
    } else if (fs::is_directory(status)) {
      sender.addEntry(rootPath, name, true, 0);
      auto it = fs::recursive_directory_iterator(rootPath, fs::directory_options::skip_permission_denied, ec);
      for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const auto &item = *it;
        if (item.is_symlink(ec)) {
          continue;
        }
        const auto relative = name + "/" + pathToUtf8(fs::relative(item.path(), rootPath, ec).generic_u8string());
        if (item.is_directory(ec)) {
          sender.addEntry(item.path(), relative, true, 0);
        } else if (item.is_regular_file(ec)) {
          sender.addEntry(item.path(), relative, false, item.file_size(ec));
        }
        if (sender.m_manifest.size() > kMaxManifestEntries) {
          error = "too many files";
          return std::nullopt;
        }
      }
    }

    if (ec) {
      error = "cannot read " + root + ": " + ec.message();
      return std::nullopt;
    }
  }

  if (sender.m_manifest.empty()) {
    error = "nothing to send";
    return std::nullopt;
  }

  for (const auto &entry : sender.m_manifest) {
    if (!isSafeRelativePath(entry.path)) {
      error = "cannot send a file with this name: " + entry.path;
      return std::nullopt;
    }
  }
  return sender;
}

void Sender::addEntry(const fs::path &source, const std::string &relative, bool directory, uint64_t size)
{
  m_manifest.push_back({relative, directory ? 0 : size, directory});
  m_sources.push_back(source);
}

bool Sender::openNextFile(std::string &error)
{
  m_file.close();
  while (m_index < m_manifest.size()) {
    const auto &entry = m_manifest[m_index];
    const auto &source = m_sources[m_index];
    ++m_index;
    if (entry.directory || entry.size == 0) {
      continue;
    }
    m_file.open(source, std::ios::binary);
    if (!m_file) {
      error = "cannot open " + pathToUtf8(source);
      return false;
    }
    m_remainingInFile = entry.size;
    return true;
  }
  return true;
}

bool Sender::readChunk(std::string &chunk, std::string &error, size_t maxSize)
{
  chunk.clear();
  while (chunk.size() < maxSize) {
    if (m_remainingInFile == 0) {
      if (!openNextFile(error)) {
        return false;
      }
      if (m_remainingInFile == 0) {
        break; // no files left
      }
    }

    const auto offset = chunk.size();
    const auto count = static_cast<size_t>(std::min<uint64_t>(maxSize - offset, m_remainingInFile));
    chunk.resize(offset + count);
    m_file.read(chunk.data() + offset, static_cast<std::streamsize>(count));
    if (static_cast<size_t>(m_file.gcount()) != count) {
      error = "file changed while sending: " + m_manifest[m_index - 1].path;
      return false;
    }
    m_remainingInFile -= count;
  }
  return true;
}

bool Sender::done() const
{
  if (m_remainingInFile > 0) {
    return false;
  }
  return std::ranges::none_of(
      m_manifest.begin() + static_cast<std::ptrdiff_t>(m_index), m_manifest.end(),
      [](const auto &e) { return !e.directory && e.size > 0; }
  );
}

//
// Receiver
//

Receiver::Receiver(fs::path destination, Manifest manifest)
    : m_destination(std::move(destination)),
      m_manifest(std::move(manifest))
{
}

Receiver::~Receiver()
{
  if (m_open) {
    abort();
  }
}

bool Receiver::begin(std::string &error)
{
  std::error_code ec;
  fs::create_directories(m_destination, ec);
  if (ec) {
    error = "cannot create " + pathToUtf8(m_destination) + ": " + ec.message();
    return false;
  }

  m_staging = m_destination / pathFromUtf8(".hopflow-incoming-" + randomSuffix());
  if (!fs::create_directory(m_staging, ec) || ec) {
    error = "cannot create a staging folder in " + pathToUtf8(m_destination);
    return false;
  }
  m_open = true;

  for (const auto &entry : m_manifest) {
    const auto path = m_staging / pathFromUtf8(entry.path);
    if (entry.directory) {
      fs::create_directories(path, ec);
    } else {
      fs::create_directories(path.parent_path(), ec);
      if (!ec && entry.size == 0) {
        std::ofstream(path, std::ios::binary);
      }
    }
    if (ec) {
      error = "cannot create " + entry.path + ": " + ec.message();
      abort();
      return false;
    }
  }
  return true;
}

bool Receiver::openNextFile(std::string &error)
{
  m_file.close();
  while (m_index < m_manifest.size()) {
    const auto &entry = m_manifest[m_index++];
    if (entry.directory || entry.size == 0) {
      continue;
    }
    m_file.open(m_staging / pathFromUtf8(entry.path), std::ios::binary | std::ios::trunc);
    if (!m_file) {
      error = "cannot write " + entry.path;
      return false;
    }
    m_remainingInFile = entry.size;
    return true;
  }
  return true;
}

bool Receiver::write(const std::string &data, std::string &error)
{
  if (!m_open) {
    error = "transfer is not open";
    return false;
  }

  size_t offset = 0;
  while (offset < data.size()) {
    if (m_remainingInFile == 0) {
      if (!openNextFile(error)) {
        return false;
      }
      if (m_remainingInFile == 0) {
        error = "received more data than the manifest declared";
        return false;
      }
    }

    const auto count = static_cast<size_t>(std::min<uint64_t>(data.size() - offset, m_remainingInFile));
    m_file.write(data.data() + offset, static_cast<std::streamsize>(count));
    if (!m_file) {
      error = "cannot write " + m_manifest[m_index - 1].path + " (disk full?)";
      return false;
    }
    offset += count;
    m_remainingInFile -= count;
    m_received += count;
  }
  return true;
}

std::optional<std::vector<std::string>> Receiver::finish(std::string &error)
{
  if (!m_open) {
    error = "transfer is not open";
    return std::nullopt;
  }

  m_file.close();
  if (m_remainingInFile > 0 || m_received != totalSize(m_manifest)) {
    error = "transfer ended before all data arrived";
    return std::nullopt;
  }

  std::vector<std::string> moved;
  std::set<std::string> topLevel;
  for (const auto &entry : m_manifest) {
    const auto name = firstComponent(entry.path);
    if (!topLevel.insert(name).second) {
      continue;
    }

    const auto target = uniqueDestination(m_destination, name);
    std::error_code ec;
    fs::rename(m_staging / pathFromUtf8(name), target, ec);
    if (ec) {
      error = "cannot move " + name + " into place: " + ec.message();
      return std::nullopt;
    }
    moved.push_back(pathToUtf8(target));
  }

  std::error_code ec;
  fs::remove_all(m_staging, ec);
  m_open = false;
  return moved;
}

void Receiver::abort()
{
  m_file.close();
  if (!m_staging.empty()) {
    std::error_code ec;
    fs::remove_all(m_staging, ec);
  }
  m_open = false;
}

} // namespace deskflow::filetransfer
