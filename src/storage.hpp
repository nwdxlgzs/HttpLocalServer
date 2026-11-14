#pragma once
#include "platform.hpp"
#include <memory>
#include <string>
#include <functional>

namespace hls {
std::string random_token();
void secure_mkdir(const std::filesystem::path& root,const std::string& path,bool allow_existing=false);
void secure_rename(const std::filesystem::path& root,const std::string& from,const std::string& to);
void secure_relocate(const std::filesystem::path& source_root,const std::string& from,const std::filesystem::path& target_root,const std::string& to);
void secure_replace_move(const std::filesystem::path& root,const std::string& from,const std::string& to,const std::string& from_version,const std::string& to_version);
void secure_spool(const std::filesystem::path& root,const std::string& path,std::uint64_t offset,const std::string& bytes);
void secure_remove(const std::filesystem::path& root,const std::string& path,bool recursive,std::size_t limit);
class Upload {
public:
    Upload(const std::filesystem::path& root,const std::string& path,std::uint64_t maximum,const std::string& replace_version="",const std::string& public_path="",std::function<void(std::size_t)> charge={});
    ~Upload();
    Upload(const Upload&)=delete;
    Upload& operator=(const Upload&)=delete;
    void append(const void* bytes,std::size_t size);
    void prepare();
    void commit();
    std::uint64_t size() const;
    const std::string& path() const;
    bool replacing() const;
    std::string temporary_name()const;
private:
    struct Impl; std::unique_ptr<Impl> impl_;std::string public_path_;std::function<void(std::size_t)> charge_;
};
void discard_upload_temporary(const std::filesystem::path& root,const std::string& destination,const std::string& name);
}
