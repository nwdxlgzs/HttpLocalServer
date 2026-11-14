#include "storage.hpp"
#include "runtime_files.hpp"
#include "util.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <random>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif
#ifdef __APPLE__
#include <sys/stdio.h>
#endif
#include <unistd.h>
#endif

namespace hls {
namespace {
[[noreturn]] void write_error(const char *operation) {
#ifdef _WIN32
    const auto code = GetLastError();
    if (code == ERROR_ALREADY_EXISTS || code == ERROR_FILE_EXISTS)
        throw Failure(409, "Destination already exists");
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
        throw Failure(404, "Entry does not exist");
    if (code == ERROR_DIR_NOT_EMPTY)
        throw Failure(409, "Directory is not empty");
    if (code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL)
        throw Failure(507, "Insufficient storage");
    throw Failure(403, std::string(operation) + " failed (Win32 " + std::to_string(code) + ")");
#else
    const auto code = errno;
    if (code == EEXIST)
        throw Failure(409, "Destination already exists");
    if (code == ENOENT)
        throw Failure(404, "Entry does not exist");
    if (code == ENOTEMPTY)
        throw Failure(409, "Directory is not empty");
    if (code == ENOSPC || code == EDQUOT)
        throw Failure(507, "Insufficient storage");
    throw Failure(403, std::string(operation) + " failed: " + std::strerror(code));
#endif
}
struct Parent {
    std::filesystem::path full;
    std::string name;
#ifdef _WIN32
    std::vector<HANDLE> guards;
    ~Parent() {
        for (auto h : guards)
            CloseHandle(h);
    }
#else
    int fd = -1;
    ~Parent() {
        if (fd >= 0)
            close(fd);
    }
#endif
    Parent(const std::filesystem::path &root, const std::string &logical) {
        const auto normalized = normalize_path(logical);
        if (normalized == "/")
            throw Failure(403, "The share root cannot be modified");
        const auto slash = normalized.rfind('/');
        name = normalized.substr(slash + 1);
        full = root;
        std::vector<std::filesystem::path> parts;
        for (const auto &part : std::filesystem::u8path(normalized.substr(1, slash ? slash - 1 : 0)))
            parts.push_back(part);
        try {
#ifdef _WIN32
            auto protect = [&](const std::filesystem::path &path) {
                auto handle =
                    CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                    write_error("Open parent");
                BY_HANDLE_FILE_INFORMATION info{};
                if (!GetFileInformationByHandle(handle, &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    CloseHandle(handle);
                    throw Failure(403, "Unsafe parent directory");
                }
                guards.push_back(handle);
            };
            protect(root);
            for (const auto &part : parts) {
                full /= part;
                protect(full);
            }
#else
            fd = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd < 0)
                write_error("Open root");
            for (const auto &part : parts) {
                auto next = openat(fd, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
                if (next < 0)
                    write_error("Open parent");
                close(fd);
                fd = next;
                full /= part;
            }
#endif
        } catch (...) {
#ifdef _WIN32
            for (auto h : guards)
                CloseHandle(h);
            guards.clear();
#else
            if (fd >= 0)
                close(fd);
            fd = -1;
#endif
            throw;
        }
    }
    Parent(const Parent &) = delete;
    Parent &operator=(const Parent &) = delete;
};
#ifdef _WIN32
struct Handle {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~Handle() {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
void check_handle(HANDLE handle) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle, &info))
        write_error("Inspect entry");
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        throw Failure(403, "Reparse points cannot be modified");
}
void move_handle(HANDLE handle, const std::filesystem::path &target) {
    const auto text = target.native();
    std::vector<unsigned char> bytes(sizeof(FILE_RENAME_INFO) + text.size() * sizeof(wchar_t));
    auto *info = reinterpret_cast<FILE_RENAME_INFO *>(bytes.data());
    info->ReplaceIfExists = FALSE;
    info->RootDirectory = nullptr;
    info->FileNameLength = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    std::memcpy(info->FileName, text.data(), info->FileNameLength);
    if (!SetFileInformationByHandle(handle, FileRenameInfo, info, static_cast<DWORD>(bytes.size())))
        write_error("Rename");
}
void delete_windows(const std::filesystem::path &path, bool recursive, std::size_t &remaining, unsigned depth) {
    if (depth > 128 || !remaining--)
        throw Failure(413, "Delete traversal limit exceeded");
    Handle handle;
    handle.h = CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle.h == INVALID_HANDLE_VALUE)
        write_error("Open entry");
    check_handle(handle.h);
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.h, &info))
        write_error("Inspect entry");
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && recursive) {
        std::error_code error;
        std::filesystem::directory_iterator it(path, error), end;
        if (error)
            throw Failure(403, "Cannot enumerate directory");
        for (; it != end; it.increment(error)) {
            if (error)
                throw Failure(403, "Cannot enumerate directory");
            delete_windows(it->path(), true, remaining, depth + 1);
        }
    }
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!SetFileInformationByHandle(handle.h, FileDispositionInfo, &disposition, sizeof(disposition)))
        write_error("Delete");
}
#else
void delete_posix(int parent, const std::string &name, bool recursive, std::size_t &remaining, unsigned depth) {
    if (depth > 128 || !remaining--)
        throw Failure(413, "Delete traversal limit exceeded");
    struct stat st{};
    if (fstatat(parent, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) < 0)
        write_error("Inspect entry");
    if (S_ISDIR(st.st_mode) && recursive) {
        auto fd = openat(parent, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0)
            write_error("Open directory");
        auto *directory = fdopendir(fd);
        if (!directory) {
            close(fd);
            write_error("Enumerate directory");
        }
        try {
            while (auto *entry = readdir(directory)) {
                std::string child = entry->d_name;
                if (child == "." || child == "..")
                    continue;
                delete_posix(fd, child, true, remaining, depth + 1);
            }
        } catch (...) {
            closedir(directory);
            throw;
        }
        closedir(directory);
    }
    if (unlinkat(parent, name.c_str(), S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) < 0)
        write_error("Delete");
}
#endif
} // namespace
std::string random_token() {
    std::random_device random;
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(48);
    for (int i = 0; i < 6; ++i) {
        const auto value = random();
        for (unsigned j = 0; j < 8; ++j)
            result += hex[(value >> (j * 4)) & 15];
    }
    return result;
}
void secure_mkdir(const std::filesystem::path &root, const std::string &path, bool allow_existing) {
    Parent parent(root, path);
#ifdef _WIN32
    const auto target = parent.full / std::filesystem::u8path(parent.name);
    if (!CreateDirectoryW(target.c_str(), nullptr)) {
        if (allow_existing && GetLastError() == ERROR_ALREADY_EXISTS) {
            resolve_path(root, path);
            if (std::filesystem::is_directory(target))
                return;
        }
        write_error("Create directory");
    }
#else
    if (mkdirat(parent.fd, parent.name.c_str(), 0755) < 0) {
        if (allow_existing && errno == EEXIST) {
            auto fd = openat(parent.fd, parent.name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd >= 0) {
                close(fd);
                return;
            }
        }
        write_error("Create directory");
    }
#endif
}
void secure_rename(const std::filesystem::path &root, const std::string &from, const std::string &to) {
    secure_relocate(root, from, root, to);
}
void secure_relocate(const std::filesystem::path &source_root, const std::string &from,
                     const std::filesystem::path &target_root, const std::string &to) {
    Parent source(source_root, from), target(target_root, to);
    if (source_root == target_root && from == to)
        return;
    if (source_root == target_root && to.size() > from.size() && to.compare(0, from.size(), from) == 0 &&
        to[from.size()] == '/')
        throw Failure(400, "Cannot move a directory into itself");
#ifdef _WIN32
    Handle handle;
    const auto path = source.full / std::filesystem::u8path(source.name);
    handle.h = CreateFileW(path.c_str(), DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle.h == INVALID_HANDLE_VALUE)
        write_error("Open entry");
    check_handle(handle.h);
    move_handle(handle.h, target.full / std::filesystem::u8path(target.name));
#else
    struct stat source_info{};
    if (fstatat(source.fd, source.name.c_str(), &source_info, AT_SYMLINK_NOFOLLOW) < 0)
        write_error("Inspect entry");
    if (S_ISLNK(source_info.st_mode))
        throw Failure(403, "Symbolic links cannot be renamed");
    // Both APIs exclude an existing destination atomically, including folders.
#if defined(__APPLE__)
    if (renameatx_np(source.fd, source.name.c_str(), target.fd, target.name.c_str(), RENAME_EXCL) == 0)
        return;
    if (errno != ENOTSUP && errno != ENOSYS && errno != EINVAL)
        write_error("Rename");
#elif defined(__linux__) && defined(SYS_renameat2)
    if (syscall(SYS_renameat2, source.fd, source.name.c_str(), target.fd, target.name.c_str(), 1) == 0)
        return;
    if (errno != ENOSYS && errno != EINVAL)
        write_error("Rename");
#endif
    struct stat info{};
    if (fstatat(target.fd, target.name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0)
        throw Failure(409, "Destination already exists");
    if (errno != ENOENT)
        write_error("Inspect destination");
    if (!S_ISREG(source_info.st_mode))
        throw Failure(501, "This filesystem does not support atomic no-replace directory moves");
    if (linkat(source.fd, source.name.c_str(), target.fd, target.name.c_str(), 0) < 0)
        write_error("Rename");
    if (unlinkat(source.fd, source.name.c_str(), 0) < 0)
        write_error("Remove old filename");
#endif
}
void secure_replace_move(const std::filesystem::path &root, const std::string &from, const std::string &to,
                         const std::string &from_version, const std::string &to_version) {
    if (from == to)
        throw Failure(409, "Source and destination are identical");
    Parent source(root, from), target(root, to);
    {
        const auto a = File::open(root, from), b = File::open(root, to);
        if (a->info().identity == b->info().identity)
            throw Failure(409, "Source and destination refer to the same file");
        if (a->info().version != from_version || b->info().version != to_version)
            throw Failure(409, "Files changed before replacement");
    }
#ifdef _WIN32
    const auto destination = target.full / std::filesystem::u8path(target.name),
               original = source.full / std::filesystem::u8path(source.name),
               backup = target.full / std::filesystem::u8path(".hls-previous-" + random_token());
    RuntimeFile replacing(backup);
    if (!ReplaceFileW(destination.c_str(), original.c_str(), backup.c_str(), 0, nullptr, nullptr)) {
        const auto code = GetLastError();
        if (GetFileAttributesW(destination.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
            MoveFileExW(backup.c_str(), destination.c_str(), 0);
        if (code == ERROR_SHARING_VIOLATION)
            throw Failure(409, "Source has active readers; retry the replacement move after they finish");
        SetLastError(code);
        write_error("Replace move");
    }
    DeleteFileW(backup.c_str());
#else
    if (renameat(source.fd, source.name.c_str(), target.fd, target.name.c_str()) < 0)
        write_error("Replace move");
    fsync(source.fd);
    fsync(target.fd);
#endif
}
void discard_upload_temporary(const std::filesystem::path &root, const std::string &destination,
                              const std::string &name) {
    if (name.size() != 60 || name.rfind(".hls-upload-", 0) != 0 ||
        !std::all_of(name.begin() + 12, name.end(),
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        throw Failure(400, "Invalid upload recovery identity");
    Parent parent(root, destination);
#ifdef _WIN32
    const auto file = parent.full / std::filesystem::u8path(name);
    if (!DeleteFileW(file.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
        write_error("Recover upload temporary");
#else
    if (unlinkat(parent.fd, name.c_str(), 0) < 0 && errno != ENOENT)
        write_error("Recover upload temporary");
#endif
}
void secure_remove(const std::filesystem::path &root, const std::string &path, bool recursive, std::size_t limit) {
    Parent parent(root, path);
#ifdef _WIN32
    delete_windows(parent.full / std::filesystem::u8path(parent.name), recursive, limit, 0);
#else
    delete_posix(parent.fd, parent.name, recursive, limit, 0);
#endif
}
void secure_spool(const std::filesystem::path &root, const std::string &path, std::uint64_t offset,
                  const std::string &bytes) {
    Parent parent(root, path);
#ifdef _WIN32
    Handle h;
    h.h = CreateFileW((parent.full / std::filesystem::u8path(parent.name)).c_str(), GENERIC_READ | GENERIC_WRITE,
                      FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h.h == INVALID_HANDLE_VALUE)
        write_error("Open resumable upload");
    check_handle(h.h);
    LARGE_INTEGER length{}, at{};
    at.QuadPart = static_cast<LONGLONG>(offset);
    if (!GetFileSizeEx(h.h, &length) || static_cast<std::uint64_t>(length.QuadPart) < offset)
        throw Failure(409, "Upload spool is shorter than acknowledged offset");
    if (!SetFilePointerEx(h.h, at, nullptr, FILE_BEGIN) || !SetEndOfFile(h.h))
        write_error("Recover upload");
    DWORD n = 0;
    if (!bytes.empty() &&
        (!WriteFile(h.h, bytes.data(), static_cast<DWORD>(bytes.size()), &n, nullptr) || n != bytes.size()))
        write_error("Write upload chunk");
    if (!FlushFileBuffers(h.h))
        write_error("Flush upload chunk");
#else
    const auto fd = openat(parent.fd, parent.name.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        write_error("Open resumable upload");
    try {
        struct stat st{};
        if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
            static_cast<std::uint64_t>(st.st_size) < offset)
            throw Failure(409, "Invalid upload spool");
        if (ftruncate(fd, static_cast<off_t>(offset)) < 0 || lseek(fd, static_cast<off_t>(offset), SEEK_SET) < 0)
            write_error("Recover upload");
        std::size_t at = 0;
        while (at < bytes.size()) {
            const auto n = write(fd, bytes.data() + at, bytes.size() - at);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                write_error("Write upload chunk");
            at += static_cast<std::size_t>(n);
        }
        if (fsync(fd) < 0)
            write_error("Flush upload chunk");
        if (fsync(parent.fd) < 0 && errno != EINVAL && errno != ENOTSUP)
            write_error("Flush spool directory");
    } catch (...) {
        close(fd);
        throw;
    }
    close(fd);
#endif
}
struct Upload::Impl {
    std::filesystem::path root;
    std::string logical, temporary, replace_version;
    Parent parent;
    std::uint64_t bytes = 0, maximum;
    bool committed = false, prepared = false;
    std::unique_ptr<RuntimeFile> transaction;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    Impl(const std::filesystem::path &root_value, const std::string &path, std::uint64_t max,
         const std::string &expected)
        : root(root_value), logical(normalize_path(path)), temporary(".hls-upload-" + random_token()),
          replace_version(expected), parent(root_value, path), maximum(max) {
        transaction=std::make_unique<RuntimeFile>(parent.full/std::filesystem::u8path(temporary));
        if (!replace_version.empty() && File::open(root, logical)->info().version != replace_version)
            throw Failure(409, "File changed before upload; refresh before replacing it");
#ifdef _WIN32
        const auto destination = parent.full / std::filesystem::u8path(parent.name);
        if (replace_version.empty() && GetFileAttributesW(destination.c_str()) != INVALID_FILE_ATTRIBUTES)
            throw Failure(409, "Destination already exists");
        const auto temp = parent.full / std::filesystem::u8path(temporary);
        handle = CreateFileW(temp.c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            write_error("Create upload");
#else
        if (replace_version.empty()) {
            struct stat info{};
            if (fstatat(parent.fd, parent.name.c_str(), &info, AT_SYMLINK_NOFOLLOW) == 0)
                throw Failure(409, "Destination already exists");
            if (errno != ENOENT)
                write_error("Inspect destination");
        }
        handle = openat(parent.fd, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (handle < 0)
            write_error("Create upload");
#endif
        transaction->opened();
    }
    ~Impl() {
#ifdef _WIN32
        if (handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
        if (!committed)
            DeleteFileW((parent.full / std::filesystem::u8path(temporary)).c_str());
#else
        if (handle >= 0)
            close(handle);
        if (!committed)
            unlinkat(parent.fd, temporary.c_str(), 0);
#endif
    }
};
Upload::Upload(const std::filesystem::path &root, const std::string &path, std::uint64_t maximum,
               const std::string &replace_version, const std::string &public_path,
               std::function<void(std::size_t)> charge)
    : impl_(new Impl(root, path, maximum, replace_version)), public_path_(public_path.empty() ? path : public_path),
      charge_(std::move(charge)) {}
Upload::~Upload() = default;
void Upload::append(const void *bytes, std::size_t size) {
    auto &u = *impl_;
    if (u.committed || size > u.maximum - u.bytes)
        throw Failure(413, "Upload exceeds configured limit");
    if (charge_)
        charge_(size);
    std::size_t done = 0;
    while (done < size) {
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(u.handle, static_cast<const char *>(bytes) + done, static_cast<DWORD>(size - done), &written,
                       nullptr))
            write_error("Write upload");
        if (!written)
            throw Failure(507, "Upload made no progress");
        done += written;
#else
        const auto written = ::write(u.handle, static_cast<const char *>(bytes) + done, size - done);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            write_error("Write upload");
        }
        if (!written)
            throw Failure(507, "Upload made no progress");
        done += static_cast<std::size_t>(written);
#endif
    }
    u.bytes += size;
    u.prepared = false;
}
void Upload::prepare() {
    auto &u = *impl_;
    if (u.prepared)
        return;
    if (u.committed)
        throw Failure(409, "Upload was already committed");
#ifdef _WIN32
    if (!FlushFileBuffers(u.handle))
        write_error("Flush upload");
#else
    if (fchmod(u.handle, 0644) < 0 || fsync(u.handle) < 0)
        write_error("Flush upload");
#endif
    u.prepared = true;
}
void Upload::commit() {
    auto &u = *impl_;
    if (u.committed)
        throw Failure(409, "Upload was already committed");
    prepare();
    // Service serializes its mutations. Readers retain their original open file;
    // replacement publishes a new file object, never writes into the old one.
    if (!u.replace_version.empty() && File::open(u.root, u.logical)->info().version != u.replace_version)
        throw Failure(409, "File changed during upload; replacement was not applied");
#ifdef _WIN32
    if (!u.replace_version.empty()) {
        const auto destination = u.parent.full / std::filesystem::u8path(u.parent.name),
                   temporary = u.parent.full / std::filesystem::u8path(u.temporary);
        const auto backup = u.parent.full / std::filesystem::u8path(".hls-previous-" + random_token());
        RuntimeFile replacing(backup);
            // ReplaceFile is supported on Windows 7 and preserves existing readers.
        // FileRenameInfo cannot replace an open destination on those systems.
        CloseHandle(u.handle);
        u.handle = INVALID_HANDLE_VALUE;
        if (!ReplaceFileW(destination.c_str(), temporary.c_str(), backup.c_str(), 0, nullptr, nullptr)) {
            const auto code = GetLastError();
            if (GetFileAttributesW(destination.c_str()) == INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES)
                MoveFileExW(backup.c_str(), destination.c_str(),
                            0); // Never overwrite a newly published name while restoring.
            SetLastError(code);
            write_error("Replace upload");
        }
        u.committed = true;
        DeleteFileW(backup.c_str()); // Old handles retain their generation until closed.
    } else {
        move_handle(u.handle, u.parent.full / std::filesystem::u8path(u.parent.name));
        u.committed = true;
        CloseHandle(u.handle);
        u.handle = INVALID_HANDLE_VALUE;
    }
    SetFileAttributesW((u.parent.full / std::filesystem::u8path(u.parent.name)).c_str(), FILE_ATTRIBUTE_NORMAL);
#else
    if (!u.replace_version.empty()) {
        if (renameat(u.parent.fd, u.temporary.c_str(), u.parent.fd, u.parent.name.c_str()) < 0)
            write_error("Replace upload");
    } else if (linkat(u.parent.fd, u.temporary.c_str(), u.parent.fd, u.parent.name.c_str(), 0) < 0)
        write_error("Commit upload");
    u.committed = true;
    unlinkat(u.parent.fd, u.temporary.c_str(), 0);
    if (fsync(u.parent.fd) < 0 && errno != EINVAL && errno != ENOTSUP)
        write_error("Flush published upload directory");
    close(u.handle);
    u.handle = -1;
#endif
    u.transaction.reset();
}
std::uint64_t Upload::size() const { return impl_->bytes; }
const std::string &Upload::path() const { return public_path_; }
bool Upload::replacing() const { return !impl_->replace_version.empty(); }
std::string Upload::temporary_name() const { return impl_->temporary; }
} // namespace hls
