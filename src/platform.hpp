#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace hls {
struct Failure : std::runtime_error { int status; Failure(int code, const std::string& message) : std::runtime_error(message), status(code) {} };
struct FileInfo { std::uint64_t size = 0; std::int64_t modified = 0; std::string version,content_version,identity; };
class File {
public:
    static std::shared_ptr<File> open(const std::filesystem::path& root, const std::string& relative);
    ~File();
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    const FileInfo& info() const { return info_; }
    bool unchanged() const;
    std::size_t read(std::uint64_t offset, void* destination, std::size_t bytes) const;
private:
    File() = default;
    FileInfo current_info() const;
#ifdef _WIN32
    void* handle_ = reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));
#else
    int handle_ = -1;
#endif
    FileInfo info_;
};
struct InterfaceAddress { std::string name, address; };
std::vector<InterfaceAddress> interface_addresses();
std::filesystem::path resolve_path(const std::filesystem::path& root, const std::string& relative);
std::filesystem::path canonical_root(const std::filesystem::path& root);
bool link_like(const std::filesystem::path& path);
// Metadata-only identity: never reads the file body or follows the final link.
std::string file_identity(const std::filesystem::path& path);
bool entry_hidden(const std::filesystem::path& path);
bool path_within(const std::filesystem::path& root, const std::filesystem::path& path);
std::uint64_t available_memory();
std::uint64_t resident_memory();
void* allocate_pages(std::size_t bytes);
void release_pages(void* address, std::size_t bytes);
std::size_t page_allocation_size(std::size_t bytes);
void open_browser(const std::string& url);
std::string utf8_from_wide(const wchar_t* text);
}
