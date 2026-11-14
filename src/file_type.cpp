#include "file_type.hpp"
#include "util.hpp"
#include "magika_labels.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

namespace hls {
namespace {
struct TypeRow{const char* extensions;const char* mime;const char* category;const char* label;bool active;bool text;};
const TypeRow rows[]={
 {"html htm xhtml","text/html; charset=utf-8","web","HTML 网页",true,true},
 {"css","text/css; charset=utf-8","code","CSS 样式表",false,true},
 {"js mjs cjs","text/javascript; charset=utf-8","code","JavaScript 文件",true,true},
 {"json map jsonl","application/json; charset=utf-8","code","JSON 数据",false,true},
 {"xml xsl xslt rss atom","application/xml; charset=utf-8","code","XML 文档",true,true},
 {"svg svgz","image/svg+xml","image","SVG 图像",true,true},
 {"txt log md markdown rst ini cfg conf toml yaml yml srt ass vtt nfo","text/plain; charset=utf-8","text","文本文档",false,true},
 {"csv tsv","text/plain; charset=utf-8","sheet","表格数据",false,true},
 {"c cpp cc cxx h hpp hxx cs java py rb go rs php sh bat cmd ps1 sql lua swift kt dart vue ts tsx jsx cmake make dockerfile","text/plain; charset=utf-8","code","源代码",false,true},
 {"png","image/png","image","PNG 图像",false,false}, {"jpg jpeg jpe jfif","image/jpeg","image","JPEG 图像",false,false},
 {"gif","image/gif","image","GIF 图像",false,false},{"webp","image/webp","image","WebP 图像",false,false},
 {"avif","image/avif","image","AVIF 图像",false,false},{"heic heif","image/heic","image","HEIF 图像",false,false},
 {"bmp dib","image/bmp","image","位图图像",false,false},{"tif tiff","image/tiff","image","TIFF 图像",false,false},
 {"ico cur","image/x-icon","image","图标文件",false,false},{"psd","image/vnd.adobe.photoshop","image","Photoshop 图像",false,false},
 {"pdf","application/pdf","document","PDF 文档",false,false},{"rtf","application/rtf","document","RTF 文档",false,false},
 {"doc","application/msword","document","Word 文档",false,false},{"docx","application/vnd.openxmlformats-officedocument.wordprocessingml.document","document","Word 文档",false,false},
 {"xls","application/vnd.ms-excel","sheet","Excel 工作簿",false,false},{"xlsx","application/vnd.openxmlformats-officedocument.spreadsheetml.sheet","sheet","Excel 工作簿",false,false},
 {"ppt","application/vnd.ms-powerpoint","slides","PowerPoint 演示文稿",false,false},{"pptx","application/vnd.openxmlformats-officedocument.presentationml.presentation","slides","PowerPoint 演示文稿",false,false},
 {"odt","application/vnd.oasis.opendocument.text","document","OpenDocument 文档",false,false},{"ods","application/vnd.oasis.opendocument.spreadsheet","sheet","OpenDocument 表格",false,false},
 {"epub","application/epub+zip","document","EPUB 图书",false,false},{"zip","application/zip","archive","ZIP 压缩文件",false,false},
 {"7z","application/x-7z-compressed","archive","7-Zip 压缩文件",false,false},{"rar","application/vnd.rar","archive","RAR 压缩文件",false,false},
 {"gz gzip tgz","application/gzip","archive","Gzip 压缩文件",false,false},{"bz2","application/x-bzip2","archive","Bzip2 压缩文件",false,false},
 {"xz txz","application/x-xz","archive","XZ 压缩文件",false,false},{"zst zstd","application/zstd","archive","Zstandard 压缩文件",false,false},
 {"tar","application/x-tar","archive","TAR 归档",false,false},{"iso img","application/x-iso9660-image","archive","磁盘映像",false,false},
 {"mp3","audio/mpeg","audio","MP3 音频",false,false},{"wav","audio/wav","audio","WAV 音频",false,false},
 {"flac","audio/flac","audio","FLAC 音频",false,false},{"ogg oga opus","audio/ogg","audio","Ogg 音频",false,false},
 {"m4a","audio/mp4","audio","M4A 音频",false,false},{"aac adts loas","audio/aac","audio","AAC 音频",false,false},{"wma","audio/x-ms-wma","audio","Windows 音频",false,false},
 {"mp4 m4v","video/mp4","video","MP4 视频",false,false},{"webm","video/webm","video","WebM 视频",false,false},
 {"mkv","video/x-matroska","video","Matroska 视频",false,false},{"mov qt","video/quicktime","video","QuickTime 视频",false,false},
 {"avi","video/x-msvideo","video","AVI 视频",false,false},{"wmv","video/x-ms-wmv","video","Windows 视频",false,false},
 {"mpeg mpg","video/mpeg","video","MPEG 视频",false,false},{"exe dll sys msi com","application/octet-stream","binary","Windows 程序",false,false},
 {"elf so o a lib bin dat pak","application/octet-stream","binary","二进制文件",false,false},
 {"woff","font/woff","font","Web 字体",false,false},{"woff2","font/woff2","font","Web 字体",false,false},
 {"ttf","font/ttf","font","TrueType 字体",false,false},{"otf","font/otf","font","OpenType 字体",false,false},
 {"sqlite sqlite3 db","application/vnd.sqlite3","database","数据库",false,false},{"wasm","application/wasm","binary","WebAssembly 模块",false,false}
};
FileType known(const char* extension){auto value=extension_type(std::string("file.")+extension);value.extension=extension;value.confidence="signature";return value;}
}
FileType extension_type(const std::string& name){
    const auto extension=lower_ascii(std::filesystem::u8path(name).extension().u8string());
    const auto key=extension.empty()?"":extension.substr(1);
    for(const auto& row:rows){const std::string choices=" "+std::string(row.extensions)+" ";if(!key.empty() && choices.find(" "+key+" ")!=std::string::npos){FileType value;value.mime=row.mime;value.extension=key;value.category=row.category;value.label=row.label;value.active=row.active;value.text=row.text;value.confidence="extension";return value;}}
    FileType result;if(!key.empty())result.label=key+" 文件";return result;
}
FileType detect_type(const std::shared_ptr<File>& file,const std::string& name){
    auto by_name=extension_type(name);
    const auto size=file->info().size;
    if(!size){by_name.confidence=by_name.extension.empty()?"empty":"extension";return by_name;}
    const auto length=static_cast<std::size_t>(std::min<std::uint64_t>(size,16384));
    std::vector<unsigned char> data(length);file->read(0,data.data(),data.size());
    auto matches=[&](std::size_t at,const char* signature,std::size_t bytes){return at+bytes<=data.size() && std::memcmp(data.data()+at,signature,bytes)==0;};
    if(matches(0,"\x89PNG\r\n\x1a\n",8))return known("png");
    if(matches(0,"\xff\xd8\xff",3))return known("jpg");
    if(matches(0,"GIF87a",6)||matches(0,"GIF89a",6))return known("gif");
    if(matches(0,"%PDF-",5))return known("pdf");
    if(matches(0,"7z\xbc\xaf\x27\x1c",6))return known("7z");
    if(matches(0,"Rar!\x1a\x07",6))return known("rar");
    if(matches(0,"\x1f\x8b",2))return known("gz");
    if(matches(0,"BZh",3))return known("bz2");
    if(matches(0,"\xfd" "7zXZ\0",6))return known("xz");
    if(matches(0,"\x28\xb5\x2f\xfd",4))return known("zst");
    if(matches(0,"RIFF",4)){if(matches(8,"WEBP",4))return known("webp");
    if(matches(8,"WAVE",4))return known("wav");
    if(matches(8,"AVI ",4))return known("avi");}
    if(matches(0,"fLaC",4))return known("flac");
    if(matches(0,"OggS",4))return known("ogg");
    if(matches(0,"ID3",3))return known("mp3");
    if(matches(0,"BM",2))return known("bmp");
    if(matches(0,"II\x2a\0",4)||matches(0,"MM\0\x2a",4))return known("tif");
    if(matches(0,"\0\0\x01\0",4))return known("ico");
    if(matches(0,"8BPS",4))return known("psd");
    if(matches(0,"wOFF",4))return known("woff");
    if(matches(0,"wOF2",4))return known("woff2");
    if(matches(0,"OTTO",4))return known("otf");
    if(matches(0,"SQLite format 3\0",16))return known("sqlite");
    if(matches(0,"\0asm",4))return known("wasm");
    if(matches(257,"ustar",5))return known("tar");
    if(matches(0,"MZ",2))return known("exe");
    if(matches(0,"\x7f" "ELF",4))return known("elf");
    if(matches(0,"\x1a\x45\xdf\xa3",4)){std::string bytes(data.begin(),data.end());return known(bytes.find("webm")!=std::string::npos?"webm":"mkv");}
    if(matches(4,"ftyp",4)){
        if(matches(8,"avif",4)||matches(8,"avis",4))return known("avif");
        if(matches(8,"heic",4)||matches(8,"heix",4)||matches(8,"mif1",4))return known("heic");
        if(matches(8,"M4A ",4))return known("m4a");
        if(matches(8,"qt  ",4))return known("mov");
        return known("mp4");
    }
    if(matches(0,"PK\x03\x04",4)||matches(0,"PK\x05\x06",4)||matches(0,"PK\x07\x08",4)){
        std::string signatures(data.begin(),data.end());
        if(size>length){const auto tail=static_cast<std::size_t>(std::min<std::uint64_t>(65536,size-length));std::vector<char> end(tail);file->read(size-tail,end.data(),tail);signatures.append(end.data(),tail);}
        if(signatures.find("word/")!=std::string::npos)return known("docx");
        if(signatures.find("xl/")!=std::string::npos)return known("xlsx");
        if(signatures.find("ppt/")!=std::string::npos)return known("pptx");
        if(signatures.find("application/epub+zip")!=std::string::npos)return known("epub");
        if(signatures.find("application/vnd.oasis.opendocument.text")!=std::string::npos)return known("odt");
        return known("zip");
    }
    if(matches(0,"\xff\xfe",2)||matches(0,"\xfe\xff",2)){auto value=known("txt");value.mime=matches(0,"\xff\xfe",2)?"text/plain; charset=utf-16le":"text/plain; charset=utf-16be";return value;}
    std::string text(data.begin(),data.end());unsigned controls=0;
    for(unsigned char c:data)if(c==0 || (c<32 && c!='\t' && c!='\n' && c!='\r' && c!='\f'))++controls;
    bool utf8=valid_utf8(text);for(int i=0;!utf8 && i<3 && !text.empty();++i){text.pop_back();utf8=valid_utf8(text);}
    if(!controls && utf8){
        const auto lowered=lower_ascii(text);const auto first=lowered.find_first_not_of(" \t\r\n\xef\xbb\xbf");
        const auto prefix=first==std::string::npos?"":lowered.substr(first,512);
        if(prefix.find("<!doctype html")!=std::string::npos || prefix.rfind("<html",0)==0 || (prefix.rfind("<!--",0)==0 && lowered.find("<html")!=std::string::npos))return known("html");
        if(prefix.rfind("<svg",0)==0 || (prefix.rfind("<?xml",0)==0 && lowered.find("<svg")!=std::string::npos))return known("svg");
        if(prefix.rfind("<?xml",0)==0)return known("xml");
        if(prefix.rfind("{\\rtf",0)==0)return known("rtf");
        if(by_name.confidence=="extension")return by_name;
        auto value=known("txt");value.confidence="heuristic";return value;
    }
    return by_name;
}
std::string type_json(const FileType& type,const std::string& name){
    const auto existing=extension_type(name);const bool mismatch=!type.extension.empty() && (type.confidence=="signature" || type.confidence=="heuristic" || type.confidence=="magika") && (existing.mime!=type.mime || existing.extension.empty());
    auto proposed=name;
    if(mismatch){auto stem=std::filesystem::u8path(name).stem().u8string();if(std::filesystem::u8path(name).extension().empty())stem=name;proposed=stem+"."+type.extension;}
    return "{\"mime\":"+json_quote(type.mime)+",\"extension\":"+json_quote(type.extension)+",\"category\":"+json_quote(type.category)+",\"label\":"+json_quote(type.label)+",\"confidence\":"+json_quote(type.confidence)+",\"score\":"+std::to_string(type.score)+",\"active\":"+(type.active?"true":"false")+",\"text\":"+(type.text?"true":"false")+",\"mismatch\":"+(mismatch?"true":"false")+",\"suggestedName\":"+json_quote(proposed)+"}";
}
magika::Features extract_magika_features(const std::shared_ptr<File>& file) {
    magika::Features features;features.fill(256);
    const auto length=static_cast<std::size_t>(std::min<std::uint64_t>(4096,file->info().size));
    if(!length)return features;
    std::vector<unsigned char> beginning(length),ending(length);file->read(0,beginning.data(),length);
    if(file->info().size<=4096)ending=beginning;else file->read(file->info().size-length,ending.data(),length);
    auto whitespace=[](unsigned char c){return c==' ' || (c>=9&&c<=13);};
    std::size_t first=0,last=length;while(first<length&&whitespace(beginning[first]))++first;while(last&&whitespace(ending[last-1]))--last;
    const auto start_count=std::min<std::size_t>(1024,length-first),end_count=std::min<std::size_t>(1024,last);
    for(std::size_t i=0;i<start_count;++i)features[i]=beginning[first+i];
    for(std::size_t i=0;i<end_count;++i)features[2048-end_count+i]=ending[last-end_count+i];
    return features;
}
FileType detect_type(const std::shared_ptr<File>& file,const std::string& name,magika::Engine& engine) {
    auto fallback=detect_type(file,name);
    if(fallback.confidence=="signature" || file->info().size<8)return fallback;
    const auto features=extract_magika_features(file);
    std::size_t usable=0;for(std::size_t i=0;i<1024;++i)if(features[i]!=256)++usable;
    if(usable<8)return fallback;
    const auto probabilities=engine.infer(features);
    const auto best=static_cast<std::size_t>(std::max_element(probabilities.begin(),probabilities.end())-probabilities.begin());
    const auto& prediction=magika::labels[best];
    if(probabilities[best]<prediction.threshold || std::string(prediction.name)=="unknown")return fallback;
    FileType result=extension_type(std::string("file.")+prediction.extension);
    result.extension=prediction.extension;result.category=prediction.category;result.mime=prediction.mime;
    if(result.label=="文件" || result.confidence=="unknown")result.label=prediction.description;
    if(prediction.text && result.mime.find("charset=")==std::string::npos)result.mime+="; charset=utf-8";
    result.text=prediction.text;result.active=result.category=="web"||std::string(prediction.name)=="svg"||std::string(prediction.name)=="xml";
    result.confidence="magika";result.score=probabilities[best];return result;
}
}
