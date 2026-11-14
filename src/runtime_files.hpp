// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <filesystem>
#include <memory>
#include <string>
namespace hls {
// Scoped protection for files this process is currently using for a transaction.
// Ordinary files with matching basenames are never registered implicitly.
class RuntimeFile {
  public:
    explicit RuntimeFile(const std::filesystem::path &);
    ~RuntimeFile();
    RuntimeFile(const RuntimeFile &) = delete;
    RuntimeFile &operator=(const RuntimeFile &) = delete;
    void opened();

  private:
    struct Entry;
    std::shared_ptr<Entry> entry_;
};
bool runtime_file_frozen(const std::filesystem::path &);
bool runtime_file_identity(const std::string &);
} // namespace hls
