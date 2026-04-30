#include "platform.h"

#include <filesystem>
#include <vector>
#include <windows.h>

// 旧版 MinGW / Cygwin 头文件可能缺少以下定义
#ifndef LOAD_LIBRARY_SEARCH_DEFAULT_DIRS
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS 0x1000
#endif
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x100
#endif

// Cygwin（非 MinGW）需要路径转换
#if defined(__CYGWIN__) && !defined(_WIN32)
#include <sys/cygwin.h>
#endif

namespace fs = std::filesystem;

namespace platform {

std::string get_executable_path() {
    char buf[32768];
    DWORD len = GetModuleFileNameA(NULL, buf, sizeof(buf));
    if (len > 0 && len < sizeof(buf))
        return std::string(buf, len);
    return "(unknown)";
}

std::string to_native_path(const std::string& path) {
#if defined(__CYGWIN__) && !defined(_WIN32)
    char buf[32768];
    if (cygwin_conv_path(CCP_POSIX_TO_WIN_A, path.c_str(), buf, sizeof(buf)) == 0)
        return buf;
#endif
    return path;
}

char classpath_separator() { return ';'; }

void* load_library(const char* path) {
    return LoadLibraryExA(path, NULL,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR);
}

void* get_symbol(void* handle, const char* name) {
    return reinterpret_cast<void*>(
        GetProcAddress(static_cast<HMODULE>(handle), name));
}

std::string load_library_error() {
    DWORD err = GetLastError();
    if (err == 0) return {};
    LPSTR buf = nullptr;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPSTR)&buf, 0, NULL);
    std::string msg(buf ? buf : "unknown error");
    LocalFree(buf);
    return msg;
}

void add_library_search_dir(const std::string& dir) {
    SetDllDirectoryA(to_native_path(dir).c_str());
}

std::string find_jvm_library(const std::string& java_home) {
    if (java_home.empty()) return {};
    fs::path hp(java_home);

    std::vector<fs::path> candidates = {
        hp / "bin" / "server" / "jvm.dll",
        hp / "jre" / "bin" / "server" / "jvm.dll",
        hp / "bin" / "client" / "jvm.dll",
    };

    for (auto& c : candidates)
        if (fs::exists(c))
            return to_native_path(c.string());
    return {};
}

} // namespace platform
