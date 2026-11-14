#include "version.hpp"
#include "config.hpp"
#include "http_server.hpp"
#include "platform.hpp"
#include "licenses.hpp"
#include <exception>
#include <iostream>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

int main(int argc,char** argv) {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
        int count=0; auto wide=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!wide) throw std::runtime_error("Cannot read command line");
        std::vector<std::string> arguments; std::vector<char*> pointers;
        for(int i=0;i<count;++i) arguments.push_back(hls::utf8_from_wide(wide[i]));
        LocalFree(wide); for(auto& argument:arguments) pointers.push_back(argument.data());
        argc=count; argv=pointers.data();
#endif
        auto config=hls::parse_config(argc,argv);
        if(config.help) { std::cout << hls::help_text(); return 0; }
        if(config.version) { std::cout << "HttpLocalServer " HLS_VERSION "\n"; return 0; }
        if(config.licenses) { std::cout << hls::third_party_licenses << '\n'; return 0; }
        config.root=hls::canonical_root(config.root);
        if(!std::filesystem::is_directory(config.root)) throw std::runtime_error("Root must be an existing directory");
        if(config.https&&!config.tls_self_signed){
            config.tls_cert=std::filesystem::canonical(config.tls_cert);
            config.tls_key=std::filesystem::canonical(config.tls_key);
        }
        if(!config.config_path.empty())std::cout<<"Config: "<<config.config_path.u8string()<<" (CLI overrides JSON)\n";
        return hls::run_server(config);
    } catch(const std::exception& e) { std::cerr << "HttpLocalServer: " << e.what() << "\nUse --help for options.\n"; return 1; }
}
