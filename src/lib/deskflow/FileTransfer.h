/*
 * Hopflow -- mouse, keyboard and file sharing, built on Deskflow
 * SPDX-FileCopyrightText: (C) 2026 Hopflow Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ProtocolTypes.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

//! Building blocks for streaming files between computers
/*!
A transfer is a manifest of relative paths followed by the file contents,
concatenated in manifest order. The Sender reads local files in that order and
the Receiver writes them into a private staging folder, only moving them to
their destination once every byte has arrived.
*/
namespace deskflow::filetransfer {

struct ManifestEntry
{
  std::string path; ///< Relative, '/' separated, UTF-8
  uint64_t size = 0;
  bool directory = false;

  bool operator==(const ManifestEntry &) const = default;
};

using Manifest = std::vector<ManifestEntry>;

//! Upper bound on entries in one transfer, to bound memory used by a manifest
inline constexpr size_t kMaxManifestEntries = 100000;

std::filesystem::path pathFromUtf8(const std::string &utf8);
std::string pathToUtf8(const std::filesystem::path &path);

//! Join paths for the protocol (newline separated), and split them back
std::string joinPaths(const std::vector<std::string> &paths);
std::vector<std::string> splitPaths(const std::string &joined);

//! True if \p path is relative and cannot leave the folder it is resolved in
bool isSafeRelativePath(const std::string &path);

uint64_t totalSize(const Manifest &manifest);
std::string serializeManifest(const Manifest &manifest);

//! Parse and validate a manifest received from a peer
std::optional<Manifest> parseManifest(const std::string &json, std::string &error);

//! A path in \p dir named \p name, or "name (n).ext" if that exists
std::filesystem::path uniqueDestination(const std::filesystem::path &dir, const std::string &name);

//! Reads local files and folders as one stream of chunks
class Sender
{
public:
  //! Collect \p roots (absolute paths to files or folders) into a manifest
  /*!
  Folders are included recursively. Symbolic links are skipped so a transfer
  never sends files from outside what the user selected.
  */
  static std::optional<Sender> create(const std::vector<std::string> &roots, std::string &error);

  const Manifest &manifest() const
  {
    return m_manifest;
  }

  //! Read the next chunk of at most \p maxSize bytes into \p chunk
  /*!
  Returns false on a read error. When everything has been read, returns true
  with an empty chunk and done() is true.
  */
  bool readChunk(std::string &chunk, std::string &error, size_t maxSize = kFileTransferChunkSize);

  bool done() const;

private:
  Sender() = default;
  void addEntry(const std::filesystem::path &source, const std::string &relative, bool directory, uint64_t size);
  bool openNextFile(std::string &error);

  Manifest m_manifest;
  std::vector<std::filesystem::path> m_sources;
  size_t m_index = 0;
  uint64_t m_remainingInFile = 0;
  std::ifstream m_file;
};

//! Writes a stream of chunks into files described by a manifest
class Receiver
{
public:
  Receiver(std::filesystem::path destination, Manifest manifest);
  Receiver(const Receiver &) = delete;
  Receiver &operator=(const Receiver &) = delete;
  ~Receiver();

  //! Create the staging folder and the folders and empty files of the manifest
  bool begin(std::string &error);

  //! Append \p data to the transfer; fails if it is more than the manifest declared
  bool write(const std::string &data, std::string &error);

  //! Verify every file is complete and move the top-level items into the destination
  /*!
  Returns the absolute paths of the moved top-level items.
  */
  std::optional<std::vector<std::string>> finish(std::string &error);

  //! Delete everything received so far
  void abort();

  uint64_t bytesReceived() const
  {
    return m_received;
  }

private:
  bool openNextFile(std::string &error);

  std::filesystem::path m_destination;
  std::filesystem::path m_staging;
  Manifest m_manifest;
  size_t m_index = 0;
  uint64_t m_remainingInFile = 0;
  uint64_t m_received = 0;
  std::ofstream m_file;
  bool m_open = false;
};

} // namespace deskflow::filetransfer
