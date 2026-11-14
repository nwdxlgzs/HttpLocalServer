#include "platform.hpp"
#include "util.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <psapi.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#endif
#endif

namespace hls {
bool entry_hidden(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&(FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM))!=0;
#else
    const auto name=path.filename().native();
    if(!name.empty() && name.front()=='.')return true;
#ifdef __APPLE__
    struct stat info{};
    return lstat(path.c_str(),&info)==0 && (info.st_flags&UF_HIDDEN)!=0;
#else
    return false;
#endif
#endif
}
namespace {
[[noreturn]] void file_error() {
#ifdef _WIN32
    const auto code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) throw Failure(404, "File not found");
    if (code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION) throw Failure(403, "File access denied");
    throw Failure(500, "File operation failed (Win32 " + std::to_string(code) + ")");
#else
    const auto code = errno;
    if (code == ENOENT || code == ENOTDIR) throw Failure(404, "File not found");
    if (code == EACCES || code == EPERM || code == ELOOP) throw Failure(403, "File access denied");
    throw Failure(500, "File operation failed: " + std::string(std::strerror(code)));
#endif
}
}
std::string utf8_from_wide(const wchar_t* text) {
#ifdef _WIN32
    if (!text || !*text) return {};
    const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) throw std::runtime_error("Invalid Unicode input");
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr, nullptr);
    result.pop_back(); return result;
#else
    (void)text; return {};
#endif
}
bool path_within(const std::filesystem::path& root, const std::filesystem::path& path) {
    auto a = root.begin(), b = path.begin();
    for (; a != root.end(); ++a, ++b) {
        if (b == path.end()) return false;
#ifdef _WIN32
        if (CompareStringOrdinal(a->c_str(),-1,b->c_str(),-1,TRUE) != CSTR_EQUAL) return false;
#else
        if (*a != *b) return false;
#endif
    }
    return true;
}
std::filesystem::path canonical_root(const std::filesystem::path& root) {
    auto result=std::filesystem::canonical(root);
#ifdef _WIN32
    std::wstring expanded(32768,L'\0');
    const auto handle=CreateFileW(result.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(handle==INVALID_HANDLE_VALUE) file_error();
    const auto length=GetFinalPathNameByHandleW(handle,expanded.data(),static_cast<DWORD>(expanded.size()),FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if(length && length<expanded.size()) { expanded.resize(length); result=std::filesystem::path(expanded); }
    auto text=result.native();
    if(text.rfind(L"\\\\?\\UNC\\",0)==0) text=L"\\\\"+text.substr(8);
    else if(text.rfind(L"\\\\?\\",0)==0) text.erase(0,4);
    result=std::filesystem::path(text);
#endif
    return result;
}
bool link_like(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    std::error_code error; return std::filesystem::is_symlink(std::filesystem::symlink_status(path,error));
#endif
}
std::string file_identity(const std::filesystem::path& path){
    std::ostringstream identity;identity<<std::hex;
#ifdef _WIN32
    const auto handle=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(handle==INVALID_HANDLE_VALUE){const auto error=GetLastError();if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)return {};file_error();}
    BY_HANDLE_FILE_INFORMATION data{};const auto ok=GetFileInformationByHandle(handle,&data);const auto error=GetLastError();CloseHandle(handle);
    if(!ok){SetLastError(error);file_error();}
    if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))return {};
    identity<<"win:"<<data.dwVolumeSerialNumber<<'-'<<data.nFileIndexHigh<<'-'<<data.nFileIndexLow;
#else
    struct stat data{};if(lstat(path.c_str(),&data)!=0){if(errno==ENOENT||errno==ENOTDIR)return {};file_error();}
    if(!S_ISREG(data.st_mode))return {};
    identity<<"unix:"<<data.st_dev<<'-'<<data.st_ino;
#endif
    return identity.str();
}
std::filesystem::path resolve_path(const std::filesystem::path& root, const std::string& relative) {
    const auto normalized = normalize_path(relative);
    auto candidate = root;
    for (auto& component : std::filesystem::u8path(normalized.substr(1))) {
        candidate /= component;
        std::error_code ec;
        const auto state = std::filesystem::symlink_status(candidate, ec);
        if (ec) throw Failure(ec == std::errc::permission_denied ? 403 : 404, "Path is not accessible");
        if (std::filesystem::is_symlink(state) || link_like(candidate)) throw Failure(403, "Symbolic links and reparse points are not shared");
    }
    std::error_code ec; const auto canonical = std::filesystem::canonical(candidate, ec);
    if (ec) throw Failure(ec == std::errc::permission_denied ? 403 : 404, "Path is not accessible");
    if (!path_within(root, canonical)) throw Failure(403, "Path escapes the shared directory");
    return canonical;
}
std::shared_ptr<File> File::open(const std::filesystem::path& root, const std::string& relative) {
    const auto normalized = normalize_path(relative);
    auto file = std::shared_ptr<File>(new File);
#ifdef _WIN32
    const auto path = resolve_path(root, normalized);
    file->handle_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file->handle_ == INVALID_HANDLE_VALUE) file_error();
    BY_HANDLE_FILE_INFORMATION data{};
    if (!GetFileInformationByHandle(file->handle_, &data)) file_error();
    if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) throw Failure(403, "Only regular files are downloadable");
    std::wstring final_path(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(file->handle_, final_path.data(), static_cast<DWORD>(final_path.size()), FILE_NAME_NORMALIZED);
    if (!length || length >= final_path.size()) file_error();
    final_path.resize(length);
    if (final_path.rfind(L"\\\\?\\UNC\\",0) == 0) final_path = L"\\\\" + final_path.substr(8);
    else if (final_path.rfind(L"\\\\?\\",0) == 0) final_path.erase(0,4);
    if (!path_within(root, std::filesystem::path(final_path))) throw Failure(403, "File escapes the shared directory");
    if (GetFileType(file->handle_) != FILE_TYPE_DISK) throw Failure(403, "Only regular files are downloadable");
#else
    int directory = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) file_error();
    try {
        std::vector<std::string> parts;
        for (const auto& p : std::filesystem::path(normalized.substr(1))) parts.push_back(p.string());
        if (parts.empty()) throw Failure(403, "Directory cannot be downloaded as a file");
        for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
            const auto next = openat(directory, parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (next < 0) file_error();
            close(directory); directory = next;
        }
        file->handle_ = openat(directory, parts.back().c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (file->handle_ < 0) file_error();
        struct stat st{};
        if (fstat(file->handle_, &st) < 0) file_error();
        if (!S_ISREG(st.st_mode)) throw Failure(403, "Only regular files are downloadable");
        close(directory); directory = -1;
    } catch (...) { if (directory >= 0) close(directory); throw; }
#endif
    file->info_ = file->current_info();
    return file;
}
File::~File() {
#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
#else
    if (handle_ >= 0) close(handle_);
#endif
}
FileInfo File::current_info() const {
    FileInfo info; std::ostringstream version; version << std::hex;
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION data{}; if (!GetFileInformationByHandle(handle_, &data)) file_error();
    const auto ticks = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    info.size = (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    info.modified = static_cast<std::int64_t>(ticks / 10000000ull) - 11644473600ll;
    version << data.dwVolumeSerialNumber << '-' << data.nFileIndexHigh << '-' << data.nFileIndexLow;
    info.identity="win:"+version.str();
    version << '-' << info.size << '-' << ticks;
    info.content_version=version.str();
    FILE_BASIC_INFO basic{};
    if(GetFileInformationByHandleEx(handle_,FileBasicInfo,&basic,sizeof(basic))) version << '-' << basic.ChangeTime.QuadPart;
#else
    struct stat st{}; if (fstat(handle_, &st) < 0) file_error();
#ifdef __APPLE__
    const auto& modified=st.st_mtimespec; const auto& changed=st.st_ctimespec;
#else
    const auto& modified=st.st_mtim; const auto& changed=st.st_ctim;
#endif
    info.size = static_cast<std::uint64_t>(st.st_size); info.modified = modified.tv_sec;
    version << st.st_dev << '-' << st.st_ino;
    info.identity="unix:"+version.str();
    version << '-' << info.size << '-' << modified.tv_sec << '-' << modified.tv_nsec;
    info.content_version=version.str();
    version << '-' << changed.tv_sec << '-' << changed.tv_nsec;
#endif
    info.version = version.str(); return info;
}
bool File::unchanged() const {
    // Rename/unlink changes ctime but leaves this open generation's bytes intact.
    // New tickets still include ctime; active readers check object, size and mtime.
    return current_info().content_version==info_.content_version;
}
std::size_t File::read(std::uint64_t offset, void* destination, std::size_t bytes) const {
    if (!unchanged()) throw Failure(409, "File changed during transfer; restart the download");
    std::size_t total = 0;
    while (total < bytes) {
#ifdef _WIN32
        OVERLAPPED overlapped{};
        const auto position = offset + total;
        overlapped.Offset = static_cast<DWORD>(position); overlapped.OffsetHigh = static_cast<DWORD>(position >> 32);
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!overlapped.hEvent) file_error();
        DWORD received = 0;
        const BOOL started = ReadFile(handle_, static_cast<char*>(destination) + total, static_cast<DWORD>(bytes-total), &received, &overlapped);
        auto error = started ? ERROR_SUCCESS : GetLastError();
        if (!started && error == ERROR_IO_PENDING) { if (!GetOverlappedResult(handle_, &overlapped, &received, TRUE)) error = GetLastError(); else error = ERROR_SUCCESS; }
        CloseHandle(overlapped.hEvent);
        if (error != ERROR_SUCCESS) { SetLastError(error); file_error(); }
        if (!received) break;
        total += received;
#else
        const auto received = pread(handle_, static_cast<char*>(destination)+total, bytes-total, static_cast<off_t>(offset+total));
        if (received < 0) { if (errno == EINTR) continue; file_error(); }
        if (!received) break;
        total += static_cast<std::size_t>(received);
#endif
    }
    if (total != bytes || !unchanged()) throw Failure(409, "File changed during transfer; restart the download");
    return total;
}
std::vector<InterfaceAddress> interface_addresses() {
    std::vector<InterfaceAddress> result;
#ifdef _WIN32
    ULONG length = 16384; std::vector<unsigned char> storage(length);
    auto adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data());
    auto code = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &length);
    if (code == ERROR_BUFFER_OVERFLOW) { storage.resize(length); adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(storage.data()); code = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &length); }
    if (code != NO_ERROR) return result;
    for (auto adapter = adapters; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp) continue;
        for (auto unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
            char host[NI_MAXHOST]{};
            if (getnameinfo(unicast->Address.lpSockaddr, unicast->Address.iSockaddrLength, host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0)
                result.push_back({utf8_from_wide(adapter->FriendlyName), host});
        }
    }
#else
    ifaddrs* interfaces = nullptr; if (getifaddrs(&interfaces) != 0) return result;
    for (auto entry = interfaces; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || !(entry->ifa_flags & IFF_UP)) continue;
        const auto family = entry->ifa_addr->sa_family; char host[INET6_ADDRSTRLEN]{};
        if (family == AF_INET) inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(entry->ifa_addr)->sin_addr, host, sizeof(host));
        else if (family == AF_INET6) inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6*>(entry->ifa_addr)->sin6_addr, host, sizeof(host));
        else continue;
        std::string address = host;
        if (family == AF_INET6 && reinterpret_cast<sockaddr_in6*>(entry->ifa_addr)->sin6_scope_id) address += "%" + std::string(entry->ifa_name);
        result.push_back({entry->ifa_name, address});
    }
    freeifaddrs(interfaces);
#endif
    return result;
}
std::uint64_t available_memory() {
#ifdef _WIN32
    MEMORYSTATUSEX status{}; status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullAvailPhys : 512ull << 20;
#elif defined(__APPLE__)
    const auto host=mach_host_self();
    vm_size_t page=0; vm_statistics64_data_t info{};
    mach_msg_type_number_t count=HOST_VM_INFO64_COUNT;
    const bool okay=host_page_size(host,&page)==KERN_SUCCESS &&
        host_statistics64(host,HOST_VM_INFO64,reinterpret_cast<host_info64_t>(&info),&count)==KERN_SUCCESS;
    mach_port_deallocate(mach_task_self(),host);
    if(!okay)return 512ull<<20;
    // Darwin free_count already includes speculative pages. Reclaimable inactive
    // pages are an estimate; do not count active/compressed pages as free memory.
    const auto available=(static_cast<std::uint64_t>(info.free_count)+info.inactive_count)*page;
    std::uint64_t physical=0;std::size_t bytes=sizeof(physical);
    if(sysctlbyname("hw.memsize",&physical,&bytes,nullptr,0)==0 && bytes==sizeof(physical))return std::min(available,physical);
    return available;
#else
    std::ifstream input("/proc/meminfo"); std::string line; std::uint64_t available = 0, fallback = 0;
    while (std::getline(input,line)) {
        std::istringstream fields(line); std::string key; std::uint64_t value = 0; fields >> key >> value;
        if (key == "MemAvailable:") available = value * 1024;
        if (key == "MemFree:" || key == "Buffers:" || key == "Cached:") fallback += value * 1024;
    }
    if (!available) available = fallback ? fallback : 512ull << 20;
    std::ifstream limit_file("/sys/fs/cgroup/memory.max"), used_file("/sys/fs/cgroup/memory.current");
    std::string limit; std::uint64_t used = 0; limit_file >> limit; used_file >> used;
    if (!limit.empty() && limit != "max") {
        try { const auto cap = std::stoull(limit); available = std::min(available, static_cast<std::uint64_t>(cap > used ? cap-used : 0)); } catch (...) {}
    }
    return available;
#endif
}
std::uint64_t resident_memory() {
#ifdef _WIN32
    // Resolve dynamically to retain the Win7 API floor without a PSAPI DLL dependency.
    using Query = BOOL (WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
    PROCESS_MEMORY_COUNTERS counters{};
    return query && query(GetCurrentProcess(), &counters, sizeof(counters)) ? counters.WorkingSetSize : 0;
#elif defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count=MACH_TASK_BASIC_INFO_COUNT;
    return task_info(mach_task_self(),MACH_TASK_BASIC_INFO,reinterpret_cast<task_info_t>(&info),&count)==KERN_SUCCESS?info.resident_size:0;
#else
    std::ifstream input("/proc/self/statm"); std::uint64_t pages = 0, resident = 0; input >> pages >> resident;
    return resident * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#endif
}
std::size_t page_allocation_size(std::size_t bytes) {
#ifdef _WIN32
    SYSTEM_INFO info{}; GetSystemInfo(&info); const auto page = static_cast<std::size_t>(info.dwPageSize);
#else
    const auto page = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
#endif
    return ((bytes + page - 1) / page) * page;
}
void* allocate_pages(std::size_t bytes) {
#ifdef _WIN32
    auto result = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!result) throw std::bad_alloc();
    return result;
#else
    auto result = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) throw std::bad_alloc();
    return result;
#endif
}
void release_pages(void* address, std::size_t bytes) {
#ifdef _WIN32
    (void)bytes; if (address) VirtualFree(address, 0, MEM_RELEASE);
#else
    if (address) munmap(address, bytes);
#endif
}
void open_browser(const std::string& url) {
#ifdef _WIN32
    const auto length = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0'); MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wide.data(), length);
    ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    const auto child = fork();
    if (child == 0) {
        setsid(); const auto sink = ::open("/dev/null", O_RDWR);
        if (sink >= 0) { dup2(sink, STDIN_FILENO); dup2(sink, STDOUT_FILENO); dup2(sink, STDERR_FILENO); if (sink > 2) close(sink); }
#ifdef __APPLE__
        execl("/usr/bin/open", "open", url.c_str(), static_cast<char*>(nullptr));
#else
        execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
#endif
        _exit(127);
    }
#endif
}
}
