#pragma once
#include "platform.hpp"
#include "magika.hpp"
#include <string>

namespace hls {
struct FileType {
    std::string mime="application/octet-stream",extension,category="binary",label="文件",confidence="unknown";
    bool active=false,text=false;
    float score=0;
};
FileType extension_type(const std::string& name);
FileType detect_type(const std::shared_ptr<File>& file,const std::string& name);
magika::Features extract_magika_features(const std::shared_ptr<File>& file);
FileType detect_type(const std::shared_ptr<File>& file,const std::string& name,magika::Engine& engine);
std::string type_json(const FileType& type,const std::string& name);
}
